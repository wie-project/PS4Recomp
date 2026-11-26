// SPDX-License-Identifier: GPL-2.0-or-later
// gcn_profile::needs_sampler_lod_bias: on a GPU that ignores MTLSamplerDescriptor.lodBias the
// recompiled sample instructions add the S# LOD_BIAS (as Vulkan adds mipLodBias in shadPS4): an
// implicit-LOD sample gets it as its bias operand, an explicit LOD is offset by it, and the
// shader is specialized on it. Without the flag the shader neither adds nor depends on it.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcn_recompiler.h"

static void Check(bool condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

// image_sample v[0:3], v[0:1], s[0:7], s[8:11] dmask:0xf (implicit LOD)
static const uint32_t k_sample[] = {
    0xF0800F00u, 0x00400000u, // image_sample
    0xBF8C0F70u,              // s_waitcnt vmcnt(0)
    0xF800180Fu, 0x03020100u, // exp mrt0 v0, v1, v2, v3 done vm
    0xBF810000u,              // s_endpgm
};

// image_sample_l v[0:3], v[0:2], s[0:7], s[8:11] dmask:0xf with LOD v2 = 0
static const uint32_t k_sample_l[] = {
    0x7E040280u,              // v_mov_b32 v2, 0
    0xF0900F00u, 0x00400000u, // image_sample_l
    0xBF8C0F70u,              // s_waitcnt vmcnt(0)
    0xF800180Fu, 0x03020100u, // exp mrt0 v0, v1, v2, v3 done vm
    0xBF810000u,              // s_endpgm
};

// image_sample_d v[0:3], v[0:5], s[0:7], s[8:11] dmask:0xf: derivatives v[0:3] (set to 0.5), x, y
static const uint32_t k_sample_d[] = {
    0x7E0002F0u, 0x7E0202F0u, 0x7E0402F0u, 0x7E0602F0u, // v_mov_b32 v0..v3, 0.5
    0xF0880F00u, 0x00400000u, // image_sample_d
    0xBF8C0F70u,              // s_waitcnt vmcnt(0)
    0xF800180Fu, 0x03020100u, // exp mrt0 v0, v1, v2, v3 done vm
    0xBF810000u,              // s_endpgm
};

// T# in s[0:7]: a linear 4x4 RGBA8 unorm 2D texture with three levels; S# in s[8:11]: point
// filtering, LOD 0..2 and the given bias (1/256 steps).
static void UserData(float bias, uint32_t ud[16]) {
    memset(ud, 0, 16 * sizeof(uint32_t));
    ud[0] = 0x100000u >> 8;
    ud[1] = 10u << 20; // 8_8_8_8, unorm
    ud[2] = 3u | (3u << 14);
    ud[3] = 4u | (5u << 3) | (6u << 6) | (7u << 9) | (2u << 16) | (8u << 20) | (9u << 28);
    ud[4] = 3u << 13;
    ud[9] = 2u * 256u << 12;
    ud[10] = ((uint32_t)(int32_t)(bias * 256.f) & 0x3fffu) | (1u << 26);
}

static void Runtime(gcn_runtime_info* rt) {
    memset(rt, 0, sizeof(*rt));
    rt->hw_stage = GCN_HW_PS;
    rt->sw_stage = GCN_SW_FRAGMENT;
    rt->num_allocated_vgprs = 8;
    rt->num_user_data = 12;
    rt->fs.en_flags = rt->fs.addr_flags = 1u << 1; // PERSP_CENTER: v0, v1
    rt->fs.color_buffers[0].data_format = 10;     // 8_8_8_8 unorm
    rt->fs.color_buffers[0].export_format = 9;    // 32_ABGR
    rt->fs.color_buffers[0].swizzle[0] = 4;
    rt->fs.color_buffers[0].swizzle[1] = 5;
    rt->fs.color_buffers[0].swizzle[2] = 6;
    rt->fs.color_buffers[0].swizzle[3] = 7;
}

static gcn_shader* Compile(const uint32_t* code, size_t size, float bias, bool shader_bias) {
    gcn_runtime_info rt;
    Runtime(&rt);
    gcn_profile profile;
    gcn_profile_default_metal(&profile);
    profile.needs_sampler_lod_bias = shader_bias;
    gcn_compile_desc d;
    memset(&d, 0, sizeof(d));
    d.hw_stage = GCN_HW_PS;
    d.sw_stage = GCN_SW_FRAGMENT;
    d.code = code;
    d.code_size = (uint32_t)size;
    d.code_addr = 0x1000;
    UserData(bias, d.user_data);
    d.runtime = &rt;
    d.profile = &profile;
    d.flags = GCN_COMPILE_EMIT_MSL | GCN_COMPILE_ALLOW_NO_FOOTER;
    gcn_shader* sh = NULL;
    char err[256];
    if (gcn_compile(&d, &sh, err, sizeof(err)) != GCN_OK) {
        fprintf(stderr, "FAIL: compile: %s\n", err);
        exit(1);
    }
    return sh;
}

static void CheckMetalCompiles(id<MTLDevice> dev, const char* msl) {
    if (!dev) return;
    NSError* error = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:msl] options:nil error:&error];
    if (!lib) fprintf(stderr, "%s\n%s\n", msl, error.localizedDescription.UTF8String);
    Check(lib != nil, "the generated MSL compiles");
}

// Whether the level(...) operand of a sample in `msl` contains `term`.
static bool LevelHas(const char* msl, const char* term) {
    const char* level = strstr(msl, "level(");
    if (!level) return false;
    const char* end = strchr(level, ';');
    const char* at = strstr(level, term);
    return at && (!end || at < end);
}

// The specialization hash of a draw with the given S# bias against shader `base`.
static uint64_t DrawSpecHash(const gcn_shader* base, float bias) {
    gcn_runtime_info rt;
    Runtime(&rt);
    uint32_t ud[16];
    UserData(bias, ud);
    gcn_draw_spec* spec = gcn_draw_spec_create(base, ud, &rt, NULL, NULL);
    Check(spec != NULL, "draw specialization");
    uint64_t mask[2];
    gcn_draw_spec_mask(spec, mask);
    const uint64_t h = gcn_draw_spec_hash(spec, mask);
    gcn_draw_spec_destroy(spec);
    return h;
}

int main(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();

        gcn_shader* plain = Compile(k_sample, sizeof(k_sample), 1.25f, false);
        const char* msl = gcn_shader_msl(plain);
        Check(msl != NULL, "MSL without the shader bias");
        Check(strstr(msl, "bias(") == NULL, "the sampler state applies the bias: no bias operand");
        CheckMetalCompiles(dev, msl);
        Check(DrawSpecHash(plain, 1.25f) == DrawSpecHash(plain, -0.75f),
              "the sampler state applies the bias: the shader does not specialize on it");
        gcn_shader_destroy(plain);

        gcn_shader* implicit = Compile(k_sample, sizeof(k_sample), 1.25f, true);
        msl = gcn_shader_msl(implicit);
        Check(msl != NULL, "MSL of the implicit-LOD sample");
        if (!strstr(msl, "bias(1.25)")) fprintf(stderr, "%s\n", msl);
        Check(strstr(msl, "bias(1.25)") != NULL, "implicit LOD: the S# bias is the sample's bias operand");
        CheckMetalCompiles(dev, msl);
        Check(DrawSpecHash(implicit, 1.25f) != DrawSpecHash(implicit, -0.75f),
              "the shader bias: another S# bias is another permutation");
        gcn_shader_destroy(implicit);

        gcn_shader* none = Compile(k_sample, sizeof(k_sample), 0.f, true);
        msl = gcn_shader_msl(none);
        Check(msl != NULL && strstr(msl, "bias(") == NULL, "a zero S# bias adds no bias operand");
        gcn_shader_destroy(none);

        gcn_shader* explicit_lod = Compile(k_sample_l, sizeof(k_sample_l), -0.75f, true);
        msl = gcn_shader_msl(explicit_lod);
        Check(msl != NULL, "MSL of the explicit-LOD sample");
        if (!LevelHas(msl, "-0.75")) fprintf(stderr, "%s\n", msl);
        Check(LevelHas(msl, "-0.75"), "explicit LOD 0: the S# bias offsets the level");
        CheckMetalCompiles(dev, msl);
        gcn_shader_destroy(explicit_lod);

        // 2^1.25 = 2.3784142: scaled derivatives move their LOD by the bias
        gcn_shader* gradient = Compile(k_sample_d, sizeof(k_sample_d), 1.25f, true);
        msl = gcn_shader_msl(gradient);
        Check(msl != NULL, "MSL of the derivative sample");
        if (!strstr(msl, "gradient2d(") || !strstr(msl, "2.378414")) fprintf(stderr, "%s\n", msl);
        Check(strstr(msl, "gradient2d(") != NULL && strstr(msl, "2.378414") != NULL,
              "derivatives: scaled by 2^bias");
        Check(strstr(msl, "bias(") == NULL, "derivatives: no bias operand");
        CheckMetalCompiles(dev, msl);
        gcn_shader_destroy(gradient);
    }
    puts("sampler LOD bias in the shader: bias operand, offset level, specialization");
    return 0;
}
