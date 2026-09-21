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
