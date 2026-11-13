// SPDX-License-Identifier: GPL-2.0-or-later
//
// Host test of ps4_gnm_pass_writes.h: which attachments of its render pass a draw writes, from
// the decoded render state the renderer encodes it with. Driven by gnm_pass_writes_test.go: one
// case per run, named by argv[1]; prints PASS or FAIL lines, exits non-zero on failure.
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
    else {
        printf("unknown case %s\n", c);
        return 2;
    }
    if (g_fail) return 1;
    printf("PASS %s\n", c);
    return 0;
}
