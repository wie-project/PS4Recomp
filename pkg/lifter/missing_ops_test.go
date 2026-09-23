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
