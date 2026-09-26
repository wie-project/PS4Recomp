package lifter

import (
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

func TestMissingOpsLifting(t *testing.T) {
	l := NewLifter(nil)
	fn := &disasm.Function{
		EntryAddr: 0x1000,
		Blocks:    make(map[uint64]*disasm.BasicBlock),
	}

	testCases := []struct {
		name     string
		inst     disasm.Instruction
		contains string
	}{
		{
			name: "VMOVLPD_load",
			inst: disasm.Instruction{
				Address: 0x1000,
				Inst: x86asm.Inst{
					Op:   x86asm.VMOVLPD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.Mem{Base: x86asm.RAX}},
					Len:  4,
				},
			},
			contains: "tmp.u64[0] = val",
		},
		{
			name: "VPBLENDW",
			inst: disasm.Instruction{
				Address: 0x1004,
				Inst: x86asm.Inst{
					Op:   x86asm.VPBLENDW,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2, x86asm.Imm(0b00000001)},
					Len:  6,
				},
			},
			contains: "res.u16[0] = s2.u16[0]",
		},
		{
			name: "VPMOVSXDQ",
			inst: disasm.Instruction{
				Address: 0x100a,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMOVSXDQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  5,
				},
			},
			contains: "ctx->xmm[0].s64[0] = src.s32[0]",
		},
		{
			name: "VPMOVZXBD",
			inst: disasm.Instruction{
				Address: 0x1010,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMOVZXBD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  5,
				},
			},
			contains: "ctx->xmm[0].u32[0] = src.u8[0]",
		},
		{
			name: "VROUNDPD",
			inst: disasm.Instruction{
				Address: 0x1016,
				Inst: x86asm.Inst{
					Op:   x86asm.VROUNDPD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.Imm(1)},
					Len:  6,
				},
			},
			contains: "floor(src.f64[0])",
		},
		{
			name: "VDPPS",
			inst: disasm.Instruction{
				Address: 0x101c,
				Inst: x86asm.Inst{
					Op:   x86asm.VDPPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2, x86asm.Imm(0xF1)},
					Len:  6,
				},
			},
			contains: "sum = p0 + p1 + p2 + p3",
		},
		{
			name: "VRCPPS",
			inst: disasm.Instruction{
				Address: 0x1022,
				Inst: x86asm.Inst{
					Op:   x86asm.VRCPPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  4,
				},
			},
			contains: "1.0f / s.f32[0]",
		},
		{
			name: "VPHADDD",
			inst: disasm.Instruction{
				Address: 0x1026,
				Inst: x86asm.Inst{
					Op:   x86asm.VPHADDD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: "res.s32[0] = s1.s32[0] + s1.s32[1]",
		},
		{
			name: "VHSUBPS",
			inst: disasm.Instruction{
				Address: 0x102b,
				Inst: x86asm.Inst{
					Op:   x86asm.VHSUBPS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: "res.f32[0] = s1.f32[0] - s1.f32[1]",
		},
		{
			name: "VPMAXUW",
			inst: disasm.Instruction{
				Address: 0x1030,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMAXUW,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: "(s1.u16[0] > s2.u16[0])",
		},
		{
			name: "VPMADDWD",
			inst: disasm.Instruction{
				Address: 0x1035,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMADDWD,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: "res.s32[0] = (int32_t)s1.s16[0] * (int32_t)s2.s16[0]",
		},
		{
			name: "VPMOVMSKB",
			inst: disasm.Instruction{
				Address: 0x103a,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMOVMSKB,
					Args: x86asm.Args{x86asm.EAX, x86asm.X0},
					Len:  5,
				},
			},
			contains: "mask |= (1 << i)",
		},
		{
			name: "VPCMPEQQ",
			inst: disasm.Instruction{
				Address: 0x1040,
				Inst: x86asm.Inst{
					Op:   x86asm.VPCMPEQQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: "s1.u64[0] == s2.u64[0]",
		},
		{
			name: "VPMULUDQ",
			inst: disasm.Instruction{
				Address: 0x1045,
				Inst: x86asm.Inst{
					Op:   x86asm.VPMULUDQ,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.X2},
					Len:  5,
				},
			},
			contains: "(uint64_t)s1.u32[0] * (uint64_t)s2.u32[0]",
		},
		{
			name: "VCVTPH2PS",
			inst: disasm.Instruction{
				Address: 0x104a,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTPH2PS,
					Args: x86asm.Args{x86asm.X0, x86asm.X1},
					Len:  6,
				},
			},
			contains: "f_bits",
		},
		{
			name: "LRET",
			inst: disasm.Instruction{
				Address: 0x1050,
				Inst: x86asm.Inst{
					Op:  x86asm.LRET,
					Len: 1,
				},
			},
			contains: "ctx->rsp += 16;",
		},
		{
			name: "LOOP",
			inst: disasm.Instruction{
				Address: 0x1052,
				Inst: x86asm.Inst{
					Op:   x86asm.LOOP,
					Args: x86asm.Args{x86asm.Rel(-10)},
					Len:  2,
				},
			},
			contains: "ctx->rcx--;",
		},
		{
			name: "CMC",
			inst: disasm.Instruction{
				Address: 0x1054,
				Inst: x86asm.Inst{
					Op:  x86asm.CMC,
					Len: 1,
				},
			},
			contains: "ctx->cf = !ctx->cf;",
		},
		{
			name: "XLATB",
			inst: disasm.Instruction{
				Address: 0x1055,
				Inst: x86asm.Inst{
					Op:  x86asm.XLATB,
					Len: 1,
				},
			},
			contains: "ctx->rax & ~0xFFULL",
		},
		{
			name: "RDTSC",
			inst: disasm.Instruction{
				Address: 0x1056,
				Inst: x86asm.Inst{
					Op:  x86asm.RDTSC,
					Len: 2,
				},
			},
			contains: "recomp_rdtsc()",
		},
		{
			name: "FISTTP",
			inst: disasm.Instruction{
				Address: 0x1058,
				Inst: x86asm.Inst{
					Op:       x86asm.FISTTP,
					Args:     x86asm.Args{x86asm.Mem{Base: x86asm.RAX}},
					MemBytes: 8,
					Len:      3,
				},
			},
			contains: "trunc(fpu_pop(ctx))",
		},
		{
			name: "FCOMP",
			inst: disasm.Instruction{
				Address: 0x105c,
				Inst: x86asm.Inst{
					Op:   x86asm.FCOMP,
					Args: x86asm.Args{x86asm.F1},
					Len:  2,
				},
			},
			contains: "fpu_pop(ctx);",
		},
		{
			name: "FLDENV",
			inst: disasm.Instruction{
				Address: 0xb25f767,
				Inst: x86asm.Inst{
					Op:       x86asm.FLDENV,
					Args:     x86asm.Args{x86asm.Mem{Base: x86asm.RBP, Disp: -0x28}},
					MemBytes: 28,
					Len:      3,
				},
			},
			contains: "ctx->fpu_cw = *(const uint16_t*)(ptr);",
		},
		{
			name: "CMPXCHG16B",
			inst: disasm.Instruction{
				Address: 0x1060,
				Inst: x86asm.Inst{
					Op:       x86asm.CMPXCHG16B,
					Args:     x86asm.Args{x86asm.Mem{Base: x86asm.RAX}},
					MemBytes: 16,
					Len:      3,
				},
			},
			contains: "__sync_val_compare_and_swap",
		},
		{
			name: "CRC32",
			inst: disasm.Instruction{
				Address: 0x1065,
				Inst: x86asm.Inst{
					Op:   x86asm.CRC32,
					Args: x86asm.Args{x86asm.EAX, x86asm.EBX},
					Len:  4,
				},
			},
			contains: "recomp_crc32_u32",
		},
		{
			name: "VHADDPS_YMM",
			inst: disasm.Instruction{
				Address: 0x1070,
				Inst: x86asm.Inst{
					Op:   x86asm.VHADDPS,
					Args: x86asm.Args{x86asm.Y0, x86asm.Y1, x86asm.Y2},
					Len:  4,
				},
			},
			contains: "res_hi.f32[3] = s2_hi.f32[2] + s2_hi.f32[3]",
		},
		{
			name: "SHUFPS_YMM",
			inst: disasm.Instruction{
				Address: 0x1075,
				Inst: x86asm.Inst{
					Op:   x86asm.VSHUFPS,
					Args: x86asm.Args{x86asm.Y0, x86asm.Y1, x86asm.Y2, x86asm.Imm(0xE4)},
					Len:  5,
				},
			},
			contains: "ctx->ymmh[0].f32[0] = s1_hi.f32[0]",
		},
		{
			name: "VROUNDPD_YMM",
			inst: disasm.Instruction{
				Address: 0x1080,
				Inst: x86asm.Inst{
					Op:   x86asm.VROUNDPD,
					Args: x86asm.Args{x86asm.Y0, x86asm.Y1, x86asm.Imm(1)},
					Len:  5,
				},
			},
			contains: "floor(src_hi.f64[0])",
		},
		{
			name: "VCVTPD2PS_YMM",
			inst: disasm.Instruction{
				Address: 0x1085,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTPD2PS,
					Args: x86asm.Args{x86asm.X0, x86asm.Y1},
					Len:  4,
				},
			},
			contains: "(float)ctx->ymmh[1].f64[1]",
		},
		{
			name: "VCVTDQ2PD_YMM",
			inst: disasm.Instruction{
				Address: 0x1090,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTDQ2PD,
					Args: x86asm.Args{x86asm.Y0, x86asm.X1},
					Len:  4,
				},
			},
			contains: "ctx->ymmh[0].f64[0] = (double)s.s32[2]",
		},
		{
			name: "VCVTPH2PS_YMM",
			inst: disasm.Instruction{
				Address: 0x1095,
				Inst: x86asm.Inst{
					Op:   x86asm.VCVTPH2PS,
					Args: x86asm.Args{x86asm.Y0, x86asm.X1},
					Len:  5,
				},
			},
			contains: "memcpy(&ctx->ymmh[0], &res_arr[4], 16)",
		},
		{
			name: "VPCMPESTRI",
			inst: disasm.Instruction{
				Address: 0x10a0,
				Inst: x86asm.Inst{
					Op:   x86asm.VPCMPESTRI,
					Args: x86asm.Args{x86asm.X0, x86asm.X1, x86asm.Imm(0)},
					Len:  5,
				},
			},
			contains: "recomp_vpcmpe_stri",
		},
		{
			name: "MMX_PADDW",
			inst: disasm.Instruction{
				Address: 0x10a5,
				Inst: x86asm.Inst{
					Op:   x86asm.PADDW,
					Args: x86asm.Args{x86asm.M0, x86asm.M1},
					Len:  3,
				},
			},
			contains: "ctx->mmx[0] = d",
		},
		{
			name: "MMX_PSUBD",
			inst: disasm.Instruction{
				Address: 0x10aa,
				Inst: x86asm.Inst{
					Op:   x86asm.PSUBD,
					Args: x86asm.Args{x86asm.M2, x86asm.M3},
					Len:  3,
				},
			},
			contains: "ctx->mmx[2] = d",
		},
		{
			name: "DIV_8bit",
			inst: disasm.Instruction{
				Address: 0x10b0,
				Inst: x86asm.Inst{
					Op:   x86asm.DIV,
					Args: x86asm.Args{x86asm.BL},
					Len:  2,
				},
			},
			contains: "uint16_t dividend = (uint16_t)ctx->rax",
		},
		{
			name: "IDIV_16bit",
			inst: disasm.Instruction{
				Address: 0x10b5,
				Inst: x86asm.Inst{
					Op:   x86asm.IDIV,
					Args: x86asm.Args{x86asm.BX},
					Len:  3,
				},
			},
			contains: "int32_t dividend = ((int32_t)(int16_t)ctx->rdx << 16)",
		},
		{
			name: "Segment_FS_read",
			inst: disasm.Instruction{
				Address: 0x10c0,
				Inst: x86asm.Inst{
					Op:   x86asm.MOV,
					Args: x86asm.Args{x86asm.RAX, x86asm.FS},
					Len:  2,
				},
			},
			contains: "((uint16_t)ctx->fs_base)",
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			lines, err := l.LiftInstruction(tc.inst, tc.inst.Address+uint64(tc.inst.Inst.Len), fn)
			if err != nil {
				t.Fatalf("LiftInstruction failed: %v", err)
			}
			code := strings.Join(lines, "\n")
			if !strings.Contains(code, tc.contains) {
				t.Errorf("Expected code to contain %q, got:\n%s", tc.contains, code)
			}
		})
	}
}
