// SPDX-License-Identifier: GPL-2.0-or-later
// PS4Recomp - backend selection (ps4_gnm_backend.h, docs/metal4-backend.md "Selection").
#include "ps4_gnm_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ps4gnm {

// The barrier bits are MTLStages and MTLBarrierScope values (ps4_gnm_backend.h); Metal 4 passes
// them on unchanged. The casts avoid comparing two enum types (-Wdeprecated-anon-enum-enum-conversion).
static_assert((uint64_t)kGnmStageVertex == (uint64_t)MTLStageVertex, "kGnmStageVertex != MTLStageVertex");
static_assert((uint64_t)kGnmStageFragment == (uint64_t)MTLStageFragment, "kGnmStageFragment != MTLStageFragment");
static_assert((uint64_t)kGnmStageDispatch == (uint64_t)MTLStageDispatch, "kGnmStageDispatch != MTLStageDispatch");
static_assert((uint64_t)kGnmStageBlit == (uint64_t)MTLStageBlit, "kGnmStageBlit != MTLStageBlit");
static_assert((uint64_t)kGnmScopeBuffers == (uint64_t)MTLBarrierScopeBuffers, "kGnmScopeBuffers != MTLBarrierScopeBuffers");
static_assert((uint64_t)kGnmScopeTextures == (uint64_t)MTLBarrierScopeTextures,
              "kGnmScopeTextures != MTLBarrierScopeTextures");

const char* BackendName(GnmBackendKind k) { return k == GnmBackendKind::Metal4 ? "metal4" : "metal3"; }

[[noreturn]] static void UnsetBindOp(GnmBackend* be, const char* op) {
    fprintf(stderr, "[gnm-draw] fatal: bind op '%s' is not set by the %s backend (GnmBindOps)\n", op,
            be ? BackendName(be->Kind()) : "(no)");
    abort();
}
void GnmUnsetBindBuffer(GnmBackend* be, GnmStage, uint32_t, __unsafe_unretained id<MTLBuffer>, uint64_t) {
    UnsetBindOp(be, "buffer");
}
void GnmUnsetBindBufferOffset(GnmBackend* be, GnmStage, uint32_t, uint64_t) { UnsetBindOp(be, "buffer_offset"); }
void GnmUnsetBindBytes(GnmBackend* be, GnmStage, uint32_t, const void*, size_t) { UnsetBindOp(be, "bytes"); }
void GnmUnsetBindTexture(GnmBackend* be, GnmStage, uint32_t, __unsafe_unretained id<MTLTexture>) {
    UnsetBindOp(be, "texture");
}
void GnmUnsetBindSampler(GnmBackend* be, GnmStage, uint32_t, __unsafe_unretained id<MTLSamplerState>) {
    UnsetBindOp(be, "sampler");
}
void GnmUnsetBindUse(GnmBackend* be, GnmStage, __unsafe_unretained id<MTLBuffer>, bool) { UnsetBindOp(be, "use"); }

static std::string Missing(uint32_t caps) {
    std::string s;
    const auto add = [&](uint32_t bit, const char* what) {
        if (caps & bit) return;
        if (!s.empty()) s += ", ";
        s += what;
    };
    add(kGnmCapClears, "render passes");
    add(kGnmCapCompute, "compute");
    add(kGnmCapCopies, "copies");
    add(kGnmCapDraws, "draws");
    return s;
}

// Metal 3 stays the default: in the Phase 6 A/B (docs/metal4-baseline.md, "Phase 6: performance,
// Metal 4 against Metal 3") Metal 4 was worse beyond noise in fps (85.5 against 98.9, -13.4, noise
// +-1.3) because of its GPU busy time (11.4 against 9.3 ms per frame); encoder and CP time were
// equal or lower. The default changes only when Metal 4 at least equals Metal 3
// (docs/metal4-backend.md, "Selection").
bool DefaultPrefersMetal4() { return false; }

GnmBackendChoice ChooseBackend(const char* env, bool device_has_metal4, uint32_t metal4_caps, bool only_clears,
                               bool prefer_metal4) {
    // PS4_GNM_MTL4_ONLY_CLEARS acts only on the Metal 4 backend; a Metal 3 choice says it is ignored.
    static const char kOnlyClearsIgnored[] = "; PS4_GNM_MTL4_ONLY_CLEARS ignored on Metal 3";
    GnmBackendChoice c;
    if (!env || !*env) {
        c.ok = true;
        if (!prefer_metal4) {
            c.why = "default (Metal 3)";
        } else if (!device_has_metal4) {
            c.why = "default: Metal 3, the GPU does not support MTLGPUFamilyMetal4";
        } else if (metal4_caps != kGnmCapAll) {
            c.why = "default: Metal 3, the Metal 4 backend lacks " + Missing(metal4_caps);
        } else {
            c.kind = GnmBackendKind::Metal4;
            c.why = "default: Metal 4 (the GPU supports MTLGPUFamilyMetal4)";
        }
        if (only_clears && c.kind == GnmBackendKind::Metal3) c.why += kOnlyClearsIgnored;
        return c;
    }
    if (!strcmp(env, "3")) {
        c.ok = true;
        c.why = "PS4_GNM_METAL=3";
        if (only_clears) c.why += kOnlyClearsIgnored;
        return c;
    }
    if (strcmp(env, "4")) {
        c.why = std::string("PS4_GNM_METAL must be 3 or 4 (it is \"") + env + "\")";
        return c;
    }
    if (!device_has_metal4) {
        c.why = "PS4_GNM_METAL=4: this GPU does not support MTLGPUFamilyMetal4; use PS4_GNM_METAL=3";
        return c;
    }
    if (!metal4_caps) {
        c.why = "PS4_GNM_METAL=4: the Metal 4 backend is not implemented in this build; use PS4_GNM_METAL=3";
        return c;
    }
    if (only_clears && !(metal4_caps & kGnmCapClears)) {
        c.why = "PS4_GNM_METAL=4 with PS4_GNM_MTL4_ONLY_CLEARS=1: this build's Metal 4 backend lacks " +
                Missing(metal4_caps) + "; use PS4_GNM_METAL=3";
        return c;
    }
    if (!only_clears && metal4_caps != kGnmCapAll) {
        c.why = "PS4_GNM_METAL=4: this build's Metal 4 backend lacks " + Missing(metal4_caps) +
                "; set PS4_GNM_MTL4_ONLY_CLEARS=1 to run it with draws skipped (debug)";
        return c;
    }
    c.ok = true;
    c.kind = GnmBackendKind::Metal4;
    c.why = only_clears ? "PS4_GNM_METAL=4 with PS4_GNM_MTL4_ONLY_CLEARS=1 (draws skipped)" : "PS4_GNM_METAL=4";
    return c;
}

} // namespace ps4gnm
