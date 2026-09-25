package lifter_test

import (
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
)

func TestRegTypes(t *testing.T) {
	regs := []x86asm.Reg{
		x86asm.RAX, x86asm.EAX, x86asm.AX, x86asm.AL, x86asm.AH,
		x86asm.R8, x86asm.R8L, x86asm.R8W, x86asm.R8B,
		x86asm.X0, x86asm.RIP,
	}
	for _, r := range regs {
		t.Logf("Reg %v: string=%q", r, r.String())
	}
}

func TestLiftNewInstructions(t *testing.T) {
	l := lifter.NewLifter(map[uint64]bool{})

	tests := []struct {
		name     string
		inst     disasm.Instruction
		contains []string
	}{
		{
			name: "CWDE",
			inst: disasm.Instruction{
				Address: 0x1000,
				Inst:    x86asm.Inst{Op: x86asm.CWDE, Len: 1},
			},
			contains: []string{"ctx->rax = (uint32_t)(int32_t)(int16_t)ctx->rax;"},
		},
		{
			name: "CBW",
			inst: disasm.Instruction{
				Address: 0x1001,
				Inst:    x86asm.Inst{Op: x86asm.CBW, Len: 1},
			},
			contains: []string{"ctx->rax = (ctx->rax & ~0xFFFFULL) | (uint16_t)(int16_t)(int8_t)ctx->rax;"},
		},
		{
			name: "PREFETCHNTA",
			inst: disasm.Instruction{
				Address: 0x1002,
				Inst:    x86asm.Inst{Op: x86asm.PREFETCHNTA, Len: 3},
			},
			contains: []string{"/* pause/prefetch */"},
		},
		{
			name: "INSB",
			inst: disasm.Instruction{
				Address: 0x1003,
				Inst:    x86asm.Inst{Op: x86asm.INSB, Len: 1},
			},
			contains: []string{"/* insb */"},
		},
		{
			name: "REP MOVSB",
			inst: disasm.Instruction{
				Address: 0x1100,
				Inst: x86asm.Inst{
					Op:     x86asm.MOVSB,
					Prefix: x86asm.Prefixes{x86asm.PrefixREP},
					Len:    2,
				},
			},
			contains: []string{"memmove(ctx->mem_base + ctx->rdi"},
		},
		{
			name: "VADDPS",
			inst: disasm.Instruction{
				Address: 0x1004,
				Inst: x86asm.Inst{
					Op:   x86asm.VADDPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].f32[0] += src.f32[0];"},
		},
		{
			name: "VMULPS",
			inst: disasm.Instruction{
				Address: 0x1008,
				Inst: x86asm.Inst{
					Op:   x86asm.VMULPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].f32[0] *= src.f32[0];"},
		},
		{
			name: "VMAXPS",
			inst: disasm.Instruction{
				Address: 0x100C,
				Inst: x86asm.Inst{
					Op:   x86asm.VMAXPS,
					Args: x86asm.Args{x86asm.X3, x86asm.X3, x86asm.X4},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[3].f32[0] = src.f32[0];"},
		},
		{
			name: "VBROADCASTSS",
			inst: disasm.Instruction{
				Address: 0x1010,
				Inst: x86asm.Inst{
					Op:   x86asm.VBROADCASTSS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  5,
				},
			},
			contains: []string{"val = ctx->xmm[1].f32[0];", "ctx->xmm[0].f32[0] = val;", "ctx->xmm[0].f32[3] = val;"},
		},
		{
			name: "VCVTDQ2PS",
			inst: disasm.Instruction{
				Address: 0x1015,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTDQ2PS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f32[0] = (float)src.s32[0];"},
		},
		{
			name: "VCVTPS2DQ",
			inst: disasm.Instruction{
				Address: 0x1019,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTPS2DQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].s32[0] = (int32_t)roundf(src.f32[0]);"},
		},
		{
			name: "VPAVGB",
			inst: disasm.Instruction{
				Address: 0x101D,
				Inst: x86asm.Inst{
					Op:   x86asm.VPAVGB,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].u8[i] = (uint8_t)(((uint32_t)ctx->xmm[0].u8[i] + (uint32_t)src.u8[i] + 1) >> 1);"},
		},
		{
			name: "VPSRAD",
			inst: disasm.Instruction{
				Address: 0x1022,
				Inst: x86asm.Inst{
					Op:   x86asm.VPSRAD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.Imm(4)},
					Len:  5,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "for (int i = 0; i < 4; i++) ctx->xmm[0].s32[i] >>= shift;"},
		},
		{
			name: "VROUNDSS",
			inst: disasm.Instruction{
				Address: 0x1027,
				Inst: x86asm.Inst{
					Op:   x86asm.VROUNDSS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2, x86asm.Imm(1)},
					Len:  6,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].f32[0] = floorf("},
		},
		{
			name: "VSQRTSS",
			inst: disasm.Instruction{
				Address: 0x102D,
				Inst: x86asm.Inst{
					Op:   x86asm.VSQRTSS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].f32[0] = sqrtf("},
		},
		{
			name: "VHADDPS",
			inst: disasm.Instruction{
				Address: 0x1031,
				Inst: x86asm.Inst{
					Op:   x86asm.VHADDPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: []string{"ctx->xmm[0].f32[0] = s1.f32[0] + s1.f32[1];", "ctx->xmm[0].f32[2] = s2.f32[0] + s2.f32[1];"},
		},
		{
			name: "VPUNPCKLDQ",
			inst: disasm.Instruction{
				Address: 0x1036,
				Inst: x86asm.Inst{
					Op:   x86asm.VPUNPCKLDQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].u32[0] = d[0];", "ctx->xmm[0].u32[1] = s[0];"},
		},
		{
			name: "VPUNPCKHDQ",
			inst: disasm.Instruction{
				Address: 0x103A,
				Inst: x86asm.Inst{
					Op:   x86asm.VPUNPCKHDQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].u32[0] = d[0];", "ctx->xmm[0].u32[1] = s[0];"},
		},
		{
			name: "LEAVE",
			inst: disasm.Instruction{
				Address: 0x1040,
				Inst:    x86asm.Inst{Op: x86asm.LEAVE, Len: 1},
			},
			contains: []string{"ctx->rsp = ctx->rbp;", "ctx->rbp = MEM_U64(ctx->rsp);", "ctx->rsp += 8;"},
		},
		{
			name: "ENTER",
			inst: disasm.Instruction{
				Address: 0x1041,
				Inst: x86asm.Inst{
					Op:   x86asm.ENTER,
					Args: x86asm.Args{x86asm.Imm(0x20), x86asm.Imm(0)},
					Len:  4,
				},
			},
			contains: []string{"MEM_U64(ctx->rsp) = ctx->rbp;", "ctx->rbp = frame;", "ctx->rsp -= 32ULL;"},
		},
		{
			name: "SAHF",
			inst: disasm.Instruction{
				Address: 0x1045,
				Inst:    x86asm.Inst{Op: x86asm.SAHF, Len: 1},
			},
			contains: []string{"uint8_t ah = (uint8_t)(ctx->rax >> 8);", "ctx->cf = ah & 1;"},
		},
		{
			name: "LAHF",
			inst: disasm.Instruction{
				Address: 0x1046,
				Inst:    x86asm.Inst{Op: x86asm.LAHF, Len: 1},
			},
			contains: []string{"ctx->rax = (ctx->rax & ~0xFF00ULL) | ((uint64_t)ah << 8);"},
		},
		{
			name: "LODSB",
			inst: disasm.Instruction{
				Address: 0x1047,
				Inst:    x86asm.Inst{Op: x86asm.LODSB, Len: 1},
			},
			contains: []string{"MEM_U8(ctx->rsi)", "ctx->rsi += 1;"},
		},
		{
			name: "REP STOSD",
			inst: disasm.Instruction{
				Address: 0x1048,
				Inst: x86asm.Inst{
					Op:     x86asm.STOSD,
					Prefix: x86asm.Prefixes{x86asm.PrefixREP},
					Len:    2,
				},
			},
			contains: []string{"uint32_t v = (uint32_t)ctx->rax;", "ctx->rdi += n * 4ULL;"},
		},
		{
			name: "CMPSB",
			inst: disasm.Instruction{
				Address: 0x104A,
				Inst:    x86asm.Inst{Op: x86asm.CMPSB, Len: 1},
			},
			contains: []string{"set_flags_sub_u8(ctx, va, vb, res);", "ctx->rsi += 1; ctx->rdi += 1;"},
		},
		{
			name: "SCASD",
			inst: disasm.Instruction{
				Address: 0x104B,
				Inst:    x86asm.Inst{Op: x86asm.SCASD, Len: 1},
			},
			contains: []string{"set_flags_sub_u32(ctx, v, al, res);", "ctx->rdi += 4;"},
		},
		{
			name: "VMOVSLDUP",
			inst: disasm.Instruction{
				Address: 0x104C,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVSLDUP,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f32[0] = src.f32[0];", "ctx->xmm[0].f32[1] = src.f32[0];", "ctx->xmm[0].f32[2] = src.f32[2];"},
		},
		{
			name: "VEXTRACTPS",
			inst: disasm.Instruction{
				Address: 0x1050,
				Inst: x86asm.Inst{
					Op:   x86asm.VEXTRACTPS,
					Args: x86asm.Args{x86asm.EAX, x86asm.X1, x86asm.Imm(2)},
					Len:  6,
				},
			},
			contains: []string{"ctx->xmm[1].u32[2]"},
		},
		{
			name: "VRSQRTSS",
			inst: disasm.Instruction{
				Address: 0x1060,
				Inst: x86asm.Inst{
					Op:   x86asm.VRSQRTSS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].f32[0] = 1.0f / sqrtf(ctx->xmm[2].f32[0]);"},
		},
		{
			name: "VRSQRTPS",
			inst: disasm.Instruction{
				Address: 0x1064,
				Inst: x86asm.Inst{
					Op:   x86asm.VRSQRTPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f32[0] = 1.0f / sqrtf(src.f32[0]);", "ctx->xmm[0].f32[3] = 1.0f / sqrtf(src.f32[3]);"},
		},
		{
			name: "VMOVAPD",
			inst: disasm.Instruction{
				Address: 0x1068,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVAPD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];"},
		},
		{
			name: "VMOVDDUP",
			inst: disasm.Instruction{
				Address: 0x106C,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVDDUP,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f64[0] = ctx->xmm[1].f64[0];", "ctx->xmm[0].f64[1] = ctx->xmm[1].f64[0];"},
		},
		{
			name: "VCVTSS2SI",
			inst: disasm.Instruction{
				Address: 0x1070,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTSS2SI,
					Args: x86asm.Args{x86asm.RAX, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"llrintf(ctx->xmm[1].f32[0])"},
		},
		{
			name: "VCVTSD2SI",
			inst: disasm.Instruction{
				Address: 0x1074,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTSD2SI,
					Args: x86asm.Args{x86asm.RAX, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"llrint(ctx->xmm[2].f64[0])"},
		},
		{
			name: "VEXTRACTF128_lo",
			inst: disasm.Instruction{
				Address: 0x1078,
				Inst: x86asm.Inst{
					Op:   x86asm.VEXTRACTF128,
					Args: x86asm.Args{x86asm.X0, x86asm.Y1, x86asm.Imm(0)},
					Len:  6,
				},
			},
			contains: []string{"ctx->xmm[1]", "ctx->xmm[0] = tmp;", "memset(&ctx->ymmh[0], 0, 16);"},
		},
		{
			name: "VEXTRACTF128_hi",
			inst: disasm.Instruction{
				Address: 0x107E,
				Inst: x86asm.Inst{
					Op:   x86asm.VEXTRACTF128,
					Args: x86asm.Args{x86asm.X0, x86asm.Y1, x86asm.Imm(1)},
					Len:  6,
				},
			},
			contains: []string{"ctx->ymmh[1]", "ctx->xmm[0] = tmp;", "memset(&ctx->ymmh[0], 0, 16);"},
		},
		{
			name: "VINSERTF128",
			inst: disasm.Instruction{
				Address: 0x1084,
				Inst: x86asm.Inst{
					Op:   x86asm.VINSERTF128,
					Args: x86asm.Args{x86asm.Y0, x86asm.Y1, x86asm.X2, x86asm.Imm(1)},
					Len:  6,
				},
			},
			contains: []string{"ctx->xmm[0] = s1_lo;", "ctx->ymmh[0] = s2;"},
		},
		{
			name: "MOVBE_reg",
			inst: disasm.Instruction{
				Address: 0x108A,
				Inst: x86asm.Inst{
					Op:   x86asm.MOVBE,
					Args: x86asm.Args{x86asm.RAX, x86asm.Mem{Base: x86asm.RDI}},
					Len:  4,
				},
			},
			contains: []string{"__builtin_bswap64"},
		},
		{
			name: "HADDPD",
			inst: disasm.Instruction{
				Address: 0x108E,
				Inst: x86asm.Inst{
					Op:   x86asm.HADDPD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f64[0] = s1.f64[0] + s1.f64[1];", "ctx->xmm[0].f64[1] = s2.f64[0] + s2.f64[1];"},
		},
		{
			name: "VHADDPD",
			inst: disasm.Instruction{
				Address: 0x1092,
				Inst: x86asm.Inst{
					Op:   x86asm.VHADDPD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f64[0] = s1.f64[0] + s1.f64[1];", "ctx->xmm[0].f64[1] = s2.f64[0] + s2.f64[1];"},
		},
		{
			name: "PCMPGTQ",
			inst: disasm.Instruction{
				Address: 0x1096,
				Inst: x86asm.Inst{
					Op:   x86asm.PCMPGTQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].u64[0] = ((int64_t)ctx->xmm[0].u64[0] > (int64_t)(ctx->xmm[1].u64[0])) ? 0xFFFFFFFFFFFFFFFFULL : 0;"},
		},
		{
			name: "VPCMPGTQ",
			inst: disasm.Instruction{
				Address: 0x109A,
				Inst: x86asm.Inst{
					Op:   x86asm.VPCMPGTQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].u64[0] = ((int64_t)ctx->xmm[0].u64[0] > (int64_t)(ctx->xmm[2].u64[0])) ? 0xFFFFFFFFFFFFFFFFULL : 0;"},
		},
		{
			name: "CVTPS2PD",
			inst: disasm.Instruction{
				Address: 0x109E,
				Inst: x86asm.Inst{
					Op:   x86asm.CVTPS2PD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  3,
				},
			},
			contains: []string{"ctx->xmm[0].f64[0] = (double)f0;", "ctx->xmm[0].f64[1] = (double)f1;"},
		},
		{
			name: "VCVTPS2PD",
			inst: disasm.Instruction{
				Address: 0x10A1,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTPS2PD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f64[0] = (double)f0;", "ctx->xmm[0].f64[1] = (double)f1;"},
		},
		{
			name: "VMINSS",
			inst: disasm.Instruction{
				Address: 0x1056,
				Inst: x86asm.Inst{
					Op:   x86asm.VMINSS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "if (s < ctx->xmm[0].f32[0]) ctx->xmm[0].f32[0] = s;"},
		},
		{
			name: "VMAXSS",
			inst: disasm.Instruction{
				Address: 0x105A,
				Inst: x86asm.Inst{
					Op:   x86asm.VMAXSS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "if (s > ctx->xmm[0].f32[0]) ctx->xmm[0].f32[0] = s;"},
		},
		{
			name: "VORPS",
			inst: disasm.Instruction{
				Address: 0x105E,
				Inst: x86asm.Inst{
					Op:   x86asm.VORPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", " | "},
		},
		{
			name: "VMOVUPD",
			inst: disasm.Instruction{
				Address: 0x1062,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVUPD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];"},
		},
		{
			name: "VCMPSS",
			inst: disasm.Instruction{
				Address: 0x1066,
				Inst: x86asm.Inst{
					Op:   x86asm.VCMPSS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2, x86asm.Imm(1)},
					Len:  6,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].u32[0] = (", "0xFFFFFFFFU"},
		},
		{
			name: "VPBLENDVB",
			inst: disasm.Instruction{
				Address: 0x106C,
				Inst: x86asm.Inst{
					Op:   x86asm.VPBLENDVB,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2, x86asm.X3},
					Len:  6,
				},
			},
			contains: []string{"(mask.u8[i] & 0x80U) ? s2.u8[i] : s1.u8[i]"},
		},
		{
			name: "VADDPD",
			inst: disasm.Instruction{
				Address: 0x1072,
				Inst: x86asm.Inst{
					Op:   x86asm.VADDPD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].f64[0] += src.f64[0];"},
		},
		{
			name: "VPSHUFB",
			inst: disasm.Instruction{
				Address: 0x1076,
				Inst: x86asm.Inst{
					Op:   x86asm.VPSHUFB,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"(idx & 0x80U) ? 0 : data.u8[idx & 15]"},
		},
		{
			name: "VPALIGNR",
			inst: disasm.Instruction{
				Address: 0x107A,
				Inst: x86asm.Inst{
					Op:   x86asm.VPALIGNR,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2, x86asm.Imm(3)},
					Len:  6,
				},
			},
			contains: []string{"memcpy(cat, s2.u8, 16);", "memcpy(ctx->xmm[0].u8, cat + 3, 16);"},
		},
		{
			name: "PEXTRD",
			inst: disasm.Instruction{
				Address: 0x1080,
				Inst: x86asm.Inst{
					Op:   x86asm.PEXTRD,
					Args: x86asm.Args{x86asm.EAX, x86asm.X1, x86asm.Imm(1)},
					Len:  6,
				},
			},
			contains: []string{"ctx->xmm[1].u32[1]"},
		},
		{
			name: "PMINSD",
			inst: disasm.Instruction{
				Address: 0x1086,
				Inst: x86asm.Inst{
					Op:   x86asm.PMINSD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  5,
				},
			},
			contains: []string{"src.s32[i] < ctx->xmm[0].s32[i]"},
		},
		{
			name: "VMOVLHPS",
			inst: disasm.Instruction{
				Address: 0x108B,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVLHPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  4,
				},
			},
			contains: []string{"uint64_t lo = ctx->xmm[1].u64[0];", "uint64_t hi = ctx->xmm[2].u64[0];"},
		},
		{
			name: "PTEST",
			inst: disasm.Instruction{
				Address: 0x108F,
				Inst: x86asm.Inst{
					Op:   x86asm.PTEST,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  5,
				},
			},
			contains: []string{"ctx->zf = ((a.u64[0] & b.u64[0]) | (a.u64[1] & b.u64[1])) == 0;"},
		},
		{
			name: "CWD",
			inst: disasm.Instruction{
				Address: 0x1094,
				Inst:    x86asm.Inst{Op: x86asm.CWD, Len: 1},
			},
			contains: []string{"(int16_t)ctx->rax < 0"},
		},
		{
			name: "VCVTTPS2DQ",
			inst: disasm.Instruction{
				Address: 0x10A0,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTTPS2DQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"(int32_t)truncf(src.f32[0])"},
		},
		{
			name: "VPMOVZXWD",
			inst: disasm.Instruction{
				Address: 0x10A4,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMOVZXWD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  5,
				},
			},
			contains: []string{"ctx->xmm[0].u32[0] = src.u16[0];"},
		},
		{
			name: "VPMULLD",
			inst: disasm.Instruction{
				Address: 0x10A9,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMULLD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: []string{"ctx->xmm[0] = ctx->xmm[1];", "ctx->xmm[0].s32[0] *= src.s32[0];"},
		},
		{
			name: "VSQRTPS",
			inst: disasm.Instruction{
				Address: 0x1095,
				Inst: x86asm.Inst{
					Op:   x86asm.VSQRTPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{"ctx->xmm[0].f32[0] = sqrtf(src.f32[0]);", "ctx->xmm[0].f32[3] = sqrtf(src.f32[3]);"},
		},
		{
			name: "VMOVAPS_YMM_store",
			inst: disasm.Instruction{
				Address: 0x854ee,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVAPS,
					Args: x86asm.Args{x86asm.Mem{Base: x86asm.RSP, Disp: 0x7b80}, x86asm.Y5},
					Len:  9,
				},
			},
			contains: []string{
				"memcpy(ctx->mem_base + (ctx->rsp + 0x7b80ULL), &ctx->xmm[5], 16);",
				"memcpy(ctx->mem_base + (ctx->rsp + 0x7b80ULL) + 16, &ctx->ymmh[5], 16);",
			},
		},
		{
			name: "VMOVAPS_YMM_load",
			inst: disasm.Instruction{
				Address: 0x85998,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVAPS,
					Args: x86asm.Args{x86asm.Y6, x86asm.Mem{Base: x86asm.RSP, Disp: 0x7ba0}},
					Len:  9,
				},
			},
			contains: []string{
				"memcpy(&ctx->xmm[6], ctx->mem_base + (ctx->rsp + 0x7ba0ULL), 16);",
				"memcpy(&ctx->ymmh[6], ctx->mem_base + (ctx->rsp + 0x7ba0ULL) + 16, 16);",
			},
		},
		{
			name: "VMOVAPS_YMM_reg",
			inst: disasm.Instruction{
				Address: 0x85990,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVAPS,
					Args: x86asm.Args{x86asm.Y1, x86asm.Y2},
					Len:  4,
				},
			},
			contains: []string{
				"ctx->xmm[1] = ctx->xmm[2];",
				"ctx->ymmh[1] = ctx->ymmh[2];",
			},
		},
		{
			name: "MOVQ_XMM_XMM",
			inst: disasm.Instruction{
				Address: 0x85994,
				Inst: x86asm.Inst{
					Op:   x86asm.MOVQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: []string{
				"uint64_t val = ctx->xmm[1].u64[0];",
				"memset(&ctx->xmm[0], 0, 16);",
				"ctx->xmm[0].u64[0] = val;",
			},
		},
		{
			name: "MOVD_XMM_XMM",
			inst: disasm.Instruction{
				Address: 0x85998,
				Inst: x86asm.Inst{
					Op:   x86asm.MOVD,
					Args: x86asm.Args{x86asm.X2, x86asm.X3},
					Len:  4,
				},
			},
			contains: []string{
				"uint32_t val = ctx->xmm[3].u32[0];",
				"memset(&ctx->xmm[2], 0, 16);",
				"ctx->xmm[2].u32[0] = val;",
			},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			lines, err := l.LiftInstruction(tc.inst, tc.inst.Address+uint64(tc.inst.Inst.Len), nil)
			if err != nil {
				t.Fatalf("failed to lift %s: %v", tc.name, err)
			}
			code := strings.Join(lines, "\n")
			for _, expected := range tc.contains {
				if !strings.Contains(code, expected) {
					t.Errorf("%s code does not contain %q:\n%s", tc.name, expected, code)
				}
			}
		})
	}
}
