// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// SPIR-V -> MSL via SPIRV-Cross (vendored at PS4Recomp/3rdparty/spirv-cross).

#include "api/gcn_msl.h"

#ifdef GCN_HAVE_MSL
#include <algorithm>
#include <set>
#include "spirv_msl.hpp"
#endif

namespace GcnMsl {

#ifdef GCN_HAVE_MSL
namespace {

// One SPIRV-Cross run. arg_ids: SPIR-V binding -> [[id(n)]] of the storage buffers to place in
// the descriptor set 0 argument buffer (empty: every resource gets a discrete slot).
// capture: non-null for a vertex shader whose outputs go to a buffer instead of the rasterizer
// (SPIRV-Cross capture_output_to_buffer).
bool ConvertOnce(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt,
                 const std::map<uint32_t, uint32_t>& arg_ids, std::string& out, Bindings* bindings,
                 uint32_t* num_buffer_slots, const CaptureSlots* capture = nullptr) {
    using namespace spirv_cross;
    CompilerMSL msl(spirv);
    auto mo = msl.get_msl_options();
    mo.platform = CompilerMSL::Options::macOS;
    mo.set_msl_version(opt.msl_version_major ? opt.msl_version_major : 2,
                       opt.msl_version_major ? opt.msl_version_minor : 3);
    const bool use_arg_buffer = !arg_ids.empty();
    mo.argument_buffers = opt.argument_buffers || use_arg_buffer;
    if (use_arg_buffer) {
        // Tier 2 (all Apple GPUs): resources are plain 64-bit GPU addresses in [[id]] order, so
        // the runtime can fill the argument buffer without an MTLArgumentEncoder.
        mo.argument_buffers_tier = CompilerMSL::Options::ArgumentBuffersTier::Tier2;
    }
    mo.pad_fragment_output_components = opt.pad_fragment_output_components;
    mo.enable_point_size_builtin = opt.enable_point_size_builtin;
    mo.invariant_float_math = opt.invariant_float_math;
    if (capture) {
        mo.capture_output_to_buffer = true;
        mo.shader_output_buffer_index = capture->output_buffer;
        mo.indirect_params_buffer_index = capture->params_buffer;
    }
    msl.set_msl_options(mo);

    std::set<uint32_t> coherent_ids; // [[id]]s of Coherent/Volatile storage buffers
    if (use_arg_buffer) {
        // Only the chosen storage buffers stay in set 0 (the argument buffer); everything else
        // moves to set 1, which keeps discrete [[buffer/texture/sampler(n)]] slots.
        const auto res = msl.get_shader_resources();
        for (const auto& r : res.storage_buffers) {
            const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
            if (arg_ids.count(b) && (msl.has_decoration(r.id, spv::DecorationCoherent) ||
                                     msl.has_decoration(r.id, spv::DecorationVolatile))) {
                coherent_ids.insert(arg_ids.at(b));
            }
        }
        const auto to_discrete = [&](const SmallVector<Resource>& list, bool keep_args) {
            for (const auto& r : list) {
                const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
                if (!(keep_args && arg_ids.count(b))) {
                    msl.set_decoration(r.id, spv::DecorationDescriptorSet, 1);
                }
            }
        };
        to_discrete(res.storage_buffers, true);
        to_discrete(res.uniform_buffers, false);
        to_discrete(res.separate_images, false);
        to_discrete(res.storage_images, false);
        to_discrete(res.sampled_images, false);
        to_discrete(res.separate_samplers, false);
        msl.add_discrete_descriptor_set(1);
        const auto model = msl.get_execution_model();
        for (const auto& [b, id] : arg_ids) {
            MSLResourceBinding rb{};
            rb.stage = model;
            rb.desc_set = 0;
            rb.binding = b;
            rb.count = 1;
            rb.msl_buffer = id;
            msl.add_msl_resource_binding(rb);
        }
        // The argument buffer itself takes slot 0; automatic discrete slots start after it.
        MSLResourceBinding ab{};
        ab.stage = model;
        ab.desc_set = 0;
        ab.binding = kArgumentBufferBinding;
        ab.msl_buffer = 0;
        msl.add_msl_resource_binding(ab);
    }
    out = msl.compile();

    if (use_arg_buffer) {
        // The runtime writes entry n at byte offset 8*n: make sure the emitted struct holds
        // exactly the expected members, in order, with nothing else in between.
        const size_t sb = out.find("struct spvDescriptorSetBuffer0");
        const size_t se = sb == std::string::npos ? sb : out.find("};", sb);
        if (se == std::string::npos) {
            throw CompilerError("argument buffer struct missing");
        }
        std::string body = out.substr(sb, se - sb);
        uint32_t n = 0;
        for (size_t p = body.find(';'); p != std::string::npos; p = body.find(';', p + 1)) {
            const size_t ls = body.rfind('\n', p);
            const std::string member = body.substr(ls + 1, p - ls - 1);
            const std::string want = "[[id(" + std::to_string(n) + ")]]";
            if (member.find('*') == std::string::npos || member.find(want) == std::string::npos) {
                throw CompilerError("unexpected argument buffer member: " + member);
            }
            // SPIRV-Cross drops the Coherent -> volatile qualifier on argument buffer members;
            // restore it so these buffers behave as in discrete-slot shaders.
            const size_t dev = member.find("device ");
            if (coherent_ids.count(n) && dev != std::string::npos &&
                member.find("volatile") == std::string::npos) {
                body.insert(ls + 1 + dev, "volatile ");
                p += 9;
            }
            n++;
        }
        if (n != arg_ids.size()) {
            throw CompilerError("argument buffer member count mismatch");
        }
        out.replace(sb, se - sb, body);
    }

    uint32_t max_buf = use_arg_buffer ? 1 : 0;
    Bindings local;
    Bindings& bnd = bindings ? *bindings : local;
    bnd = {};
    const auto res = msl.get_shader_resources();
    const auto note_buf = [&](uint32_t slot) {
        if (slot != uint32_t(-1)) {
            max_buf = std::max(max_buf, slot + 1);
        }
    };
    const auto add_buffers = [&](const SmallVector<Resource>& list) {
        for (const auto& r : list) {
            const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
            if (use_arg_buffer && msl.get_decoration(r.id, spv::DecorationDescriptorSet) == 0) {
                continue; // in the argument buffer
            }
            const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
            if (slot != uint32_t(-1)) {
                auto& v = bnd.buffers[b];
                if (std::find(v.begin(), v.end(), slot) == v.end()) {
                    v.push_back(slot);
                }
            }
            note_buf(slot);
        }
    };
    add_buffers(res.storage_buffers);
    add_buffers(res.uniform_buffers);
    for (const auto& r : res.push_constant_buffers) {
        const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
        if (slot != uint32_t(-1)) {
            bnd.push_constant_buffer = int32_t(slot);
        }
        note_buf(slot);
    }
    uint32_t max_tex = 0;
    const auto add_tex = [&](const Resource& r) {
        const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
        const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
        if (slot == uint32_t(-1)) {
            return;
        }
        bnd.textures[b] = slot;
        const auto& type = msl.get_type(r.type_id);
        uint32_t n = 1;
        for (auto a : type.array) {
            n *= a;
        }
        max_tex = std::max(max_tex, slot + n);
    };
    for (const auto& r : res.separate_images) {
        add_tex(r);
    }
    for (const auto& r : res.storage_images) {
        add_tex(r);
    }
    for (const auto& r : res.sampled_images) {
        add_tex(r);
    }
    uint32_t max_samp = 0;
    for (const auto& r : res.separate_samplers) {
        const uint32_t b = msl.get_decoration(r.id, spv::DecorationBinding);
        const uint32_t slot = msl.get_automatic_msl_resource_binding(r.id);
        if (slot == uint32_t(-1)) {
            continue;
        }
        bnd.samplers[b] = slot;
        max_samp = std::max(max_samp, slot + 1);
    }
    bnd.first_free_buffer = max_buf;
    bnd.num_textures = max_tex;
    bnd.num_samplers = max_samp;
    if (use_arg_buffer) {
        bnd.arg_entries = arg_ids;
        bnd.argument_buffer = 0;
        bnd.num_arg_entries = uint32_t(arg_ids.size());
    }
    *num_buffer_slots = max_buf;
    return true;
}

} // namespace

bool Convert(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt, std::string& out,
             std::string& err, Bindings* bindings, const std::vector<uint32_t>* arg_candidates) {
    try {
        uint32_t slots = 0;
        ConvertOnce(spirv, opt, {}, out, bindings, &slots);
        const uint32_t limit = opt.max_buffer_slots ? opt.max_buffer_slots : 31;
        if (slots <= limit || !arg_candidates || arg_candidates->empty() || opt.argument_buffers) {
            return true;
        }
        // Too many discrete buffers for Metal (e.g. cloth simulation kernels binding 30+ guest
        // buffers): reach the active guest storage buffers through one argument buffer.
        // Bindings with several typed aliases stay discrete (they would overlap in it).
        spirv_cross::CompilerMSL probe(spirv);
        const auto active = probe.get_active_interface_variables();
        const auto res = probe.get_shader_resources(active);
        std::map<uint32_t, uint32_t> count;
        for (const auto& r : res.storage_buffers) {
            count[probe.get_decoration(r.id, spv::DecorationBinding)]++;
        }
        std::map<uint32_t, uint32_t> arg_ids;
        for (const auto& [b, n] : count) {
            if (n == 1 && std::find(arg_candidates->begin(), arg_candidates->end(), b) !=
                              arg_candidates->end()) {
                const uint32_t id = uint32_t(arg_ids.size());
                arg_ids[b] = id;
            }
        }
        if (arg_ids.empty()) {
            return true; // nothing movable; Metal will reject the shader with a clear error
        }
        std::string out2;
        Bindings b2;
        ConvertOnce(spirv, opt, arg_ids, out2, &b2, &slots);
        out = std::move(out2);
        if (bindings) {
            *bindings = std::move(b2);
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}
bool ConvertCapture(const std::vector<uint32_t>& spirv, const gcn_msl_options& opt,
                    const Bindings& like, const CaptureSlots& slots, std::string& out,
                    std::string& err) {
    try {
        // The same argument buffer choice as the shader's own MSL, so that every resource
        // keeps its slot.
        uint32_t n = 0;
        Bindings b;
        ConvertOnce(spirv, opt, like.arg_entries, out, &b, &n, &slots);
        if (b.buffers != like.buffers || b.textures != like.textures || b.samplers != like.samplers ||
            b.push_constant_buffer != like.push_constant_buffer ||
            b.argument_buffer != like.argument_buffer) {
            err = "resource slots of the capture variant differ from the shader's";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}
#else
bool Convert(const std::vector<uint32_t>&, const gcn_msl_options&, std::string&, std::string& err,
             Bindings*, const std::vector<uint32_t>*) {
    err = "gcn_recompiler was built without SPIRV-Cross (GCN_BUILD_MSL=OFF)";
    return false;
}
bool ConvertCapture(const std::vector<uint32_t>&, const gcn_msl_options&, const Bindings&,
                    const CaptureSlots&, std::string&, std::string& err) {
    err = "gcn_recompiler was built without SPIRV-Cross (GCN_BUILD_MSL=OFF)";
    return false;
}
#endif

} // namespace GcnMsl
