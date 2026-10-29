// SPDX-FileCopyrightText: Copyright 2026 PS4Recomp contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// RectList (AmdGpu::PrimitiveType::RectList) on Metal: the vertex shader's capture variant and
// the vertex shader that expands the captured vertices to rectangles. See
// gcn_shader_rectlist_msl() in gcn_recompiler.h.
//
// The fourth vertex follows shadPS4's RectList tessellation control shader
// (backend/spirv/emit_spirv_quad_rect.cpp, EmitRectListTCS): the vertex that shares x with one
// of the others and y with the other is the corner, and the fourth vertex is the sum of the
// other two minus the corner, for the position and every output. When no vertex is such a
// corner (a rectangle that is not axis aligned), vertex 0 is taken as the corner.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "api/gcn_msl.h"
#include "api/gcn_shader_internal.h"
#include "compat/host_fp_env.h"

namespace {

struct Member {
    std::string type, name, attr;
    uint32_t array = 0; // 0: not an array
    uint32_t size = 0, align = 0;
};

// Size and alignment of an MSL scalar or vector type in device memory.
bool TypeLayout(const std::string& t, uint32_t* size, uint32_t* align) {
    static const char* const kScalars[] = {"float", "int", "uint"};
    for (const char* s : kScalars) {
        const size_t n = std::strlen(s);
        if (t.compare(0, n, s) != 0) {
            continue;
        }
        const std::string rest = t.substr(n);
        if (rest.empty()) {
            *size = *align = 4;
        } else if (rest == "2") {
            *size = *align = 8;
        } else if (rest == "3" || rest == "4") {
            *size = *align = 16;
        } else {
            continue;
        }
        return true;
    }
    return false;
}

// Members of "struct main0_out { ... };" in SPIRV-Cross output, e.g.
//     float4 out_attr0 [[user(locn0)]];
//     float gl_ClipDistance [[clip_distance]] [1];
bool ParseOutputStruct(const std::string& msl, std::string* body, std::vector<Member>* members,
                       std::string* err) {
    const size_t sb = msl.find("struct main0_out\n{\n");
    const size_t se = sb == std::string::npos ? sb : msl.find("\n};", sb);
    if (se == std::string::npos) {
        *err = "the vertex shader has no output struct";
        return false;
    }
    *body = msl.substr(sb, se + 3 - sb);
    size_t at = msl.find('{', sb) + 2;
    while (at < se) {
        size_t eol = msl.find('\n', at);
        if (eol == std::string::npos || eol > se) {
            eol = se;
        }
        std::string line = msl.substr(at, eol - at);
        at = eol + 1;
        const size_t first = line.find_first_not_of(' ');
        if (first == std::string::npos) {
            continue;
        }
        line = line.substr(first);
        Member m;
        const size_t sp = line.find(' ');
        const size_t a0 = line.find(" [[");
        const size_t a1 = line.find("]]");
        if (sp == std::string::npos || a0 == std::string::npos || a1 == std::string::npos || a0 < sp ||
            line.back() != ';') {
            *err = "unexpected output struct member: " + line;
            return false;
        }
        m.type = line.substr(0, sp);
        m.name = line.substr(sp + 1, a0 - sp - 1);
        m.attr = line.substr(a0 + 3, a1 - a0 - 3);
        const std::string tail = line.substr(a1 + 2, line.size() - 1 - (a1 + 2));
        if (!tail.empty()) {
            if (tail.size() < 4 || tail[0] != ' ' || tail[1] != '[' || tail.back() != ']') {
                *err = "unexpected output struct member: " + line;
                return false;
            }
            m.array = uint32_t(std::strtoul(tail.c_str() + 2, nullptr, 10));
            if (m.array == 0) {
                *err = "unexpected output struct member: " + line;
                return false;
            }
        }
        if (m.name.find_first_of(" []") != std::string::npos || !TypeLayout(m.type, &m.size, &m.align)) {
            *err = "unsupported output struct member: " + line;
            return false;
        }
        if (m.array) {
            m.size *= m.array;
        }
        members->push_back(m);
    }
    if (members->empty()) {
        *err = "the vertex shader output struct is empty";
        return false;
    }
    return true;
}

std::string ExpandMsl(const std::string& out_struct, const std::vector<Member>& members,
                      const Member& pos, uint32_t stride) {
    std::string s;
    s += "// RectList expansion (gcn_shader_rectlist_msl): 6 vertices per rectangle from the 3\n"
         "// captured vertices of the guest vertex shader.\n"
         "#include <metal_stdlib>\n"
         "using namespace metal;\n\n";
    s += out_struct + "\n\n";
    s += "static_assert(sizeof(main0_out) == " + std::to_string(stride) +
         ", \"captured vertex layout\");\n\n";
    s += "vertex main0_out main0(const device main0_out* spvRectIn [[buffer(0)]],\n"
         "                       const device uint* spvRectSlots [[buffer(1)]],\n"
         "                       const device uint* spvRectParams [[buffer(2)]],\n"
         "                       uint gl_VertexIndex [[vertex_id]], uint gl_InstanceIndex [[instance_id]],\n"
         "                       uint gl_BaseInstance [[base_instance]])\n"
         "{\n"
         "    const uint rect = gl_VertexIndex / 6u;\n"
         "    const device main0_out* in = spvRectIn + (gl_InstanceIndex - gl_BaseInstance) * spvRectParams[0];\n"
         "    const main0_out v[3] = {in[spvRectSlots[rect * 3u]], in[spvRectSlots[rect * 3u + 1u]],\n"
         "                            in[spvRectSlots[rect * 3u + 2u]]};\n"
         "    float2 p[3];\n"
         "    for (uint i = 0; i < 3u; i++) {\n"
         "        p[i] = v[i]." + pos.name + ".xy;\n"
         "    }\n"
         "    // The corner shares x with one neighbour and y with the other.\n"
         "    bool corner[3];\n"
         "    for (uint i = 0; i < 3u; i++) {\n"
         "        const bool2 next = p[i] == p[(i + 1u) % 3u];\n"
         "        const bool2 prev = p[(i + 2u) % 3u] == p[i];\n"
         "        corner[i] = (next.x && prev.y) || (next.y && prev.x);\n"
         "    }\n"
         "    const uint c = corner[1] ? 1u : (corner[2] ? 2u : 0u);\n"
         "    // Triangles (q0, q1, q2) and (q3, q2, q1) with q0 the corner, q1 and q2 the next\n"
         "    // vertices in the draw's order and q3 the fourth vertex: both keep the winding of the\n"
         "    // rectangle's three vertices.\n"
         "    const uint order[6] = {0u, 1u, 2u, 3u, 2u, 1u};\n"
         "    const uint q = order[gl_VertexIndex % 6u];\n"
         "    if (q < 3u) {\n"
         "        return v[(c + q) % 3u];\n"
         "    }\n"
         "    const main0_out a = v[(c + 1u) % 3u];\n"
         "    const main0_out b = v[(c + 2u) % 3u];\n"
         "    const main0_out o = v[c];\n"
         "    main0_out out = {};\n";
    for (const Member& m : members) {
        if (m.array) {
            s += "    for (uint j = 0; j < " + std::to_string(m.array) + "u; j++) {\n";
            s += "        out." + m.name + "[j] = a." + m.name + "[j] + b." + m.name + "[j] - o." + m.name +
                 "[j];\n    }\n";
        } else {
            s += "    out." + m.name + " = a." + m.name + " + b." + m.name + " - o." + m.name + ";\n";
        }
    }
    s += "    return out;\n}\n";
    return s;
}

char* Dup(const std::string& s) {
    char* r = static_cast<char*>(std::malloc(s.size() + 1));
    std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

void SetErr(char* err, size_t err_size, const std::string& msg) {
    if (err && err_size) {
        std::snprintf(err, err_size, "%s", msg.c_str());
    }
}

} // namespace

GCN_API gcn_status gcn_shader_rectlist_msl(const gcn_shader* vs, const gcn_msl_options* opt,
                                           gcn_rectlist_msl* out, char* err, size_t err_size) {
    if (!vs || !out) {
        SetErr(err, err_size, "invalid arguments");
        return GCN_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));
    if (vs->info.hw_stage != Shader::HwStage::Vertex || vs->msl.empty()) {
        SetErr(err, err_size, "not a vertex shader with MSL");
        return GCN_ERR_INVALID_ARG;
    }
    std::string body, e;
    std::vector<Member> members;
    if (!ParseOutputStruct(vs->msl, &body, &members, &e)) {
        SetErr(err, err_size, e);
        return GCN_ERR_MSL;
    }
    const Member* pos = nullptr;
    uint32_t stride = 0, align = 4;
    for (const Member& m : members) {
        if (m.attr.compare(0, 8, "position") == 0) {
            pos = &m;
        }
        stride = (stride + m.align - 1) / m.align * m.align + m.size;
        align = std::max(align, m.align);
    }
    stride = (stride + align - 1) / align * align;
    if (!pos || pos->type != "float4" || pos->array) {
        SetErr(err, err_size, "the vertex shader writes no float4 position");
        return GCN_ERR_MSL;
    }
    // Above the shader's buffers and its vertex buffers (any slot >= msl_first_free_buffer, one
    // per attribute, as the runtime binds them).
    GcnMsl::CaptureSlots slots;
    slots.output_buffer =
        vs->msl_bindings.first_free_buffer +
        static_cast<uint32_t>(vs->spec.fetch_shader_data.attributes.size());
    slots.params_buffer = slots.output_buffer + 1;
    if (slots.params_buffer > 30) {
        SetErr(err, err_size, "no free buffer slots for the capture variant");
        return GCN_ERR_MSL;
    }
    gcn_msl_options mo;
    if (opt) {
        mo = *opt;
    } else {
        gcn_msl_options_default(&mo);
    }
    std::string capture;
    const GcnShim::HostFpEnvScope fp_env; // SPIRV-Cross prints float literals through a double
    if (!GcnMsl::ConvertCapture(vs->spirv, mo, vs->msl_bindings, slots, capture, e)) {
        SetErr(err, err_size, "SPIRV-Cross: " + e);
        return GCN_ERR_MSL;
    }
    out->capture_msl = Dup(capture);
    out->expand_msl = Dup(ExpandMsl(body, members, *pos, stride));
    out->vertex_stride = stride;
    out->capture_buffer = slots.output_buffer;
    out->params_buffer = slots.params_buffer;
    return GCN_OK;
}

GCN_API void gcn_rectlist_msl_free(gcn_rectlist_msl* m) {
    if (!m) {
        return;
    }
    std::free(m->capture_msl);
    std::free(m->expand_msl);
    m->capture_msl = m->expand_msl = nullptr;
}
