// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of ps4_gnm_pass_writes.h: which attachments of its render pass a draw writes, from
// the decoded render state the renderer encodes it with, and the depth target plane sizes its
// overlap check uses. Driven by gnm_pass_writes_test.go: one case per run, named by argv[1];
// prints PASS or FAIL lines, exits non-zero on failure.
#include "ps4_gnm_pass_writes.h"

#include <stdio.h>
#include <string.h>

using namespace ps4gnm;

static int g_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while (0)

// MTLStencilOperation values
constexpr uint32_t kKeep = 0, kZero = 1, kReplace = 2, kIncrClamp = 3;
constexpr uint32_t kColorAll = 0xF; // MTLColorWriteMaskAll

// A draw tested against the depth buffer it is bound with, without writing it (soft particles).
static RenderState DepthTested() {
    RenderState rs;
    rs.depth.bound = true;
    rs.ds.depth_test = true;
    rs.ds.depth_write = false;
    return rs;
}

static RenderState StencilTested(uint32_t fail, uint32_t depth_fail, uint32_t pass, uint8_t write_mask) {
    RenderState rs = DepthTested();
    rs.ds.stencil_test = true;
    rs.ds.front.fail = fail;
    rs.ds.front.depth_fail = depth_fail;
    rs.ds.front.pass = pass;
    rs.ds.front.write_mask = write_mask;
    rs.ds.back = rs.ds.front;
    return rs;
}

static void CaseDepth() {
    RenderState rs = DepthTested();
    CHECK(!WritesDepthStencil(rs), "a depth-tested draw without depth writes does not write");
    rs.ds.depth_write = true;
    CHECK(WritesDepthStencil(rs), "depth writes");
}

static void CaseStencil() {
    CHECK(!WritesDepthStencil(StencilTested(kKeep, kKeep, kKeep, 0xFF)), "stencil test with Keep everywhere");
    CHECK(!WritesDepthStencil(StencilTested(kReplace, kZero, kIncrClamp, 0)), "stencil write mask 0");
    CHECK(WritesDepthStencil(StencilTested(kKeep, kKeep, kReplace, 0xFF)), "Replace on pass");
    CHECK(WritesDepthStencil(StencilTested(kKeep, kZero, kKeep, 0x01)), "Zero on depth fail, one mask bit");
    CHECK(WritesDepthStencil(StencilTested(kIncrClamp, kKeep, kKeep, 0x80)), "IncrementClamp on fail");

    RenderState back_only = StencilTested(kKeep, kKeep, kKeep, 0xFF);
    back_only.ds.back.pass = kReplace;
    CHECK(WritesDepthStencil(back_only), "the back face writes");

    RenderState off = StencilTested(kReplace, kReplace, kReplace, 0xFF);
    off.ds.stencil_test = false;
    CHECK(!WritesDepthStencil(off), "stencil operations without the stencil test");

    RenderState odd = StencilTested(kKeep, kKeep, kKeep, 0xFF);
    odd.ds.unsupported_stencil_op = true;
    CHECK(WritesDepthStencil(odd), "an operation Metal cannot express counts as a write");
}

static void CaseClear() {
    RenderState rs = DepthTested();
    rs.depth.depth_clear_enable = true;
    CHECK(WritesDepthStencil(rs), "DB depth clear");
    rs = DepthTested();
    rs.depth.stencil_clear_enable = true;
    CHECK(WritesDepthStencil(rs), "DB stencil clear");
}

static void CaseDrawWrites() {
    const uint32_t masks[8] = {kColorAll, 0, kColorAll, 0, 0, 0, 0, kColorAll};
    RenderState rs = DepthTested();
    const uint32_t bound = kPassColor0 | (kPassColor0 << 1) | (kPassColor0 << 2) | kPassDepth;
    uint32_t w = DrawPassWrites(bound, masks, rs);
    CHECK(w == (kPassColor0 | (kPassColor0 << 2)), "bound slots with a write mask, no depth: 0x%x", w);

    rs.ds.depth_write = true;
    w = DrawPassWrites(bound, masks, rs);
    CHECK(w == (kPassColor0 | (kPassColor0 << 2) | kPassDepth), "with depth writes: 0x%x", w);

    // slot 7 has a write mask but is not bound; the depth target is not bound either
    w = DrawPassWrites(kPassColor0, masks, rs);
    CHECK(w == kPassColor0, "only bound attachments: 0x%x", w);

    const uint32_t none[8] = {};
    w = DrawPassWrites(kPassAllAttachments, none, DepthTested());
    CHECK(w == 0, "a depth-tested draw with every color write masked off: 0x%x", w);
    CHECK((kPassAllAttachments & kPassDepth) && (kPassAllAttachments >> 9) == 0, "attachment bits");
}

// The plane sizes of a depth target (DepthTarget::DepthBytes / StencilBytes), with the registers
// of Gravity Rush's 1920x1080 Z32F+S8 target: DB_DEPTH_SLICE.SLICE_TILE_MAX 0x8FFF (2048x1152
// padded), Z at 0x4eff0000, stencil at 0x4f8f0000, its shadow map array at 0x4fb60000.
static void CaseStencilPlane() {
    DepthTarget d;
    d.z_format = ZFormat::Z32Float;
    d.stencil_format = StencilFormat::Stencil8;
    d.z_read_address = 0x4eff0000;
    d.stencil_read_address = 0x4f8f0000;
    d.bpp = 32;
    d.slice_bytes = (0x8FFF + 1) * 64 * 4; // DecodeDepthTarget
    d.num_slices = 1;
    CHECK(d.DepthBytes() == 0x900000, "depth plane 0x%llx", (unsigned long long)d.DepthBytes());
    CHECK(d.StencilBytes() == 0x240000, "stencil plane 0x%llx", (unsigned long long)d.StencilBytes());
    CHECK(d.stencil_read_address + d.StencilBytes() <= 0x4fb60000, "the stencil plane reaches the shadow map");
    d.num_slices = 3;
    CHECK(d.StencilBytes() == 3 * 0x240000, "3 slices: 0x%llx", (unsigned long long)d.StencilBytes());
    d.num_slices = 1;
    d.bpp = 16; // Z16: half the bytes per tile, the same stencil plane
    d.slice_bytes = (0x8FFF + 1) * 64 * 2;
    CHECK(d.StencilBytes() == 0x240000, "Z16 stencil plane 0x%llx", (unsigned long long)d.StencilBytes());
    d.stencil_format = StencilFormat::Invalid;
    CHECK(d.StencilBytes() == 0, "no stencil plane: 0x%llx", (unsigned long long)d.StencilBytes());
    d.stencil_format = StencilFormat::Stencil8;
    d.stencil_read_address = 0;
    CHECK(d.StencilBytes() == 0, "no stencil base: 0x%llx", (unsigned long long)d.StencilBytes());
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: %s <case>\n", argv[0]);
        return 2;
    }
    const char* c = argv[1];
    if (!strcmp(c, "depth")) CaseDepth();
    else if (!strcmp(c, "stencil")) CaseStencil();
    else if (!strcmp(c, "clear")) CaseClear();
    else if (!strcmp(c, "draw-writes")) CaseDrawWrites();
    else if (!strcmp(c, "stencil-plane")) CaseStencilPlane();
    else {
        printf("unknown case %s\n", c);
        return 2;
    }
    if (g_fail) return 1;
    printf("PASS %s\n", c);
    return 0;
}
