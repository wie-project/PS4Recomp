package lifter

import (
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

func TestArm64LifterBasic(t *testing.T) {
	lifter := NewArm64Lifter(map[uint64]bool{0x1000: true, 0x2000: true})
	lifter.SetTSO(false) // these cases check the plain lowerings; TestTsoOrdering checks the ordering

	tests := []struct {
		name     string
		bytes    []byte
		pc       uint64
		expected []string
	}{
		{
			name:     "mov eax, ebx (32-bit zero extend)",
			bytes:    []byte{0x89, 0xd8}, // mov eax, ebx
			pc:       0x1000,
			expected: []string{"mov w19, w20"},
		},
		{
			name:     "mov rax, rbx (64-bit)",
			bytes:    []byte{0x48, 0x89, 0xd8}, // mov rax, rbx
			pc:       0x1000,
			expected: []string{"mov x19, x20"},
		},
		{
			name:     "mov dh, 0xcb (high-8 register immediate)",
			bytes:    []byte{0xb6, 0xcb}, // mov dh, 0xcb
			pc:       0x1000,
			expected: []string{"mov w16, #0xcb", "bfi x22, x16, #8, #8"},
		},
		{
			name:     "movzx r9l, bl",
			bytes:    []byte{0x44, 0x0f, 0xb6, 0xcb}, // movzx r9l, bl
			pc:       0x1000,
			expected: []string{"uxtb w9, w20"},
		},
		{
			name:     "movsx rdx, ax",
			bytes:    []byte{0x48, 0x0f, 0xbf, 0xd0}, // movsx rdx, ax
			pc:       0x1000,
			expected: []string{"sxth x22, w19"},
		},
		{
			name:     "movzx ecx, word ptr [rdi]",
			bytes:    []byte{0x0f, 0xb7, 0x0f}, // movzx ecx, word ptr [rdi]
			pc:       0x1000,
			expected: []string{"ldrh w21, [x28, x16]"},
		},
		{
			name:     "movzx ecx, byte ptr [rdi]",
			bytes:    []byte{0x0f, 0xb6, 0x0f}, // movzx ecx, byte ptr [rdi]
			pc:       0x1000,
			expected: []string{"ldrb w21, [x28, x16]"},
		},
		{
			name:     "mov byte ptr [r13 + 0x411d], 0 (large disp store without clobbering w17)",
			bytes:    []byte{0x41, 0xc6, 0x85, 0x1d, 0x41, 0x00, 0x00, 0x00}, // mov byte ptr [r13+0x411d], 0
			pc:       0x1000,
			expected: []string{"mov w17, #0x0", "mov x16, x13", "movz x7, #0x411d", "add x16, x16, x7", "strb w17, [x28, x16]"},
		},
		{
			name:     "movsx rcx, word ptr [rdi]",
			bytes:    []byte{0x48, 0x0f, 0xbf, 0x0f}, // movsx rcx, word ptr [rdi]
			pc:       0x1000,
			expected: []string{"ldrsh x21, [x28, x16]"},
		},
		{
			name:     "add rax, rcx",
			bytes:    []byte{0x48, 0x01, 0xc8}, // add rax, rcx
			pc:       0x1000,
			expected: []string{"adds x19, x19, x21"},
		},
		{
			name:     "sub rax, 42",
			bytes:    []byte{0x48, 0x83, 0xe8, 0x2a}, // sub rax, 42
			pc:       0x1000,
			expected: []string{"subs x19, x19, #42"},
		},
		{
			name:     "cmp rax, rbx",
			bytes:    []byte{0x48, 0x39, 0xd8}, // cmp rax, rbx
			pc:       0x1000,
			expected: []string{"cmp x19, x20"},
		},
		{
			name:     "sete al",
			bytes:    []byte{0x0f, 0x94, 0xc0}, // sete al
			pc:       0x1000,
			expected: []string{"cset w16, eq", "bfi x19, x16, #0, #8"},
		},
		{
			name:     "cmove rax, rbx",
			bytes:    []byte{0x48, 0x0f, 0x44, 0xc3}, // cmove rax, rbx
			pc:       0x1000,
			expected: []string{"csel x19, x20, x19, eq"},
		},
		{
			name:     "imul eax, ebx",
			bytes:    []byte{0x0f, 0xaf, 0xc3}, // imul eax, ebx
			pc:       0x1000,
			expected: []string{"smull x0, w19, w20", "cmp x0, w0, sxtw", "mov w19, w0", "rmif x2, #0, #3"},
		},
		{
			name:     "bswap rax",
			bytes:    []byte{0x48, 0x0f, 0xc8}, // bswap rax
			pc:       0x1000,
			expected: []string{"rev x19, x19"},
		},
		{
			name:     "xorps xmm0, xmm1",
			bytes:    []byte{0x0f, 0x57, 0xc1}, // xorps xmm0, xmm1
			pc:       0x1000,
			expected: []string{"eor v0.16b, v0.16b, v1.16b"},
		},
		{
			name:     "ret",
			bytes:    []byte{0xc3}, // ret
			pc:       0x1000,
			expected: []string{"ldr x16, [x28, x26]", "add x26, x26, #8", "ret"},
		},
		{
			name:     "sbb eax, eax",
			bytes:    []byte{0x19, 0xc0}, // sbb eax, eax
			pc:       0x1000,
			expected: []string{"sbcs w19, w19, w19"},
		},
		{
			name:     "adc rax, rbx",
			bytes:    []byte{0x48, 0x11, 0xd8}, // adc rax, rbx
			pc:       0x1000,
			expected: []string{"adcs x19, x19, x20"},
		},
		{
			name:     "adc r14, 0",
			bytes:    []byte{0x49, 0x83, 0xd6, 0x00}, // adc r14, 0
			pc:       0x1000,
			expected: []string{"cfinv", "adcs x14, x14, xzr"},
		},
		{
			name:     "adc eax, 0",
			bytes:    []byte{0x83, 0xd0, 0x00}, // adc eax, 0
			pc:       0x1000,
			expected: []string{"cfinv", "adcs w19, w19, wzr"},
		},
		{
			name:     "bt eax, 13",
			bytes:    []byte{0x0f, 0xba, 0xe0, 0x0d}, // bt eax, 13
			pc:       0x1000,
			expected: []string{"ubfx x2, x19, #13, #1", "rmif x2, #63, #2"},
		},
		{
			name:     "cvtss2sd xmm0, xmm1",
			bytes:    []byte{0xf3, 0x0f, 0x5a, 0xc1}, // cvtss2sd xmm0, xmm1
			pc:       0x1000,
			expected: []string{"fcvt d30, s1", "mov v0.d[0], v30.d[0]"},
		},
		{
			name:     "pandn xmm0, xmm1",
			bytes:    []byte{0x66, 0x0f, 0xdf, 0xc1}, // pandn xmm0, xmm1
			pc:       0x1000,
			expected: []string{"bic v0.16b, v1.16b, v0.16b"},
		},
		{
			name:     "pshufd xmm0, xmm1, 0",
			bytes:    []byte{0x66, 0x0f, 0x70, 0xc1, 0x00}, // pshufd xmm0, xmm1, 0
			pc:       0x1000,
			expected: []string{"dup v0.4s, v1.s[0]"},
		},
		{
			name:     "punpckldq xmm0, xmm1",
			bytes:    []byte{0x66, 0x0f, 0x62, 0xc1}, // punpckldq xmm0, xmm1
			pc:       0x1000,
			expected: []string{"zip1 v0.4s, v0.4s, v1.4s"},
		},
		{
			name:     "pcmpgtd xmm0, xmm1",
			bytes:    []byte{0x66, 0x0f, 0x66, 0xc1}, // pcmpgtd xmm0, xmm1
			pc:       0x1000,
			expected: []string{"cmgt v0.4s, v0.4s, v1.4s"},
		},
		{
			name:     "pmuludq xmm0, xmm1",
			bytes:    []byte{0x66, 0x0f, 0xf4, 0xc1}, // pmuludq xmm0, xmm1
			pc:       0x1000,
			expected: []string{"umull v0.2d, v0.2s, v1.2s"},
		},
		{
			name:     "vmovddup xmm1, xmm2",
			bytes:    []byte{0xc5, 0xfb, 0x12, 0xca}, // vmovddup xmm1, xmm2
			pc:       0x1000,
			expected: []string{"dup v1.2d, v2.d[0]", "movi v17.16b, #0"},
		},
		{
			name:     "vmovq rbx, xmm0",
			bytes:    []byte{0xc4, 0xe1, 0xf9, 0x7e, 0xc3}, // vmovq rbx, xmm0
			pc:       0x1000,
			expected: []string{"fmov x20, d0"},
		},
		{
			name:     "vpshufd xmm0, xmm4, 0xee",
			bytes:    []byte{0xc5, 0xf9, 0x70, 0xc4, 0xee}, // vpshufd xmm0, xmm4, 0xee
			pc:       0x1000,
			expected: []string{"tbl v0.16b, {v4.16b}, v29.16b", "movi v16.16b, #0"},
		},
		{
			name:     "vpcmpeqd x7, x7, x7",
			bytes:    []byte{0xc5, 0xc1, 0x76, 0xff}, // vpcmpeqd x7, x7, x7
			pc:       0x1000,
			expected: []string{"cmeq v7.4s, v7.4s, v7.4s", "movi v23.16b, #0"},
		},
		{
			name:     "vpaddq x3, x3, x7",
			bytes:    []byte{0xc5, 0xe1, 0xd4, 0xdf}, // vpaddq x3, x3, x7
			pc:       0x1000,
			expected: []string{"add v3.2d, v3.2d, v7.2d", "movi v19.16b, #0"},
		},
		{
			name:     "bextr ecx, ecx, r8d",
			bytes:    []byte{0xc4, 0xe2, 0x38, 0xf7, 0xc9}, // bextr ecx, ecx, r8d
			pc:       0x1000,
			expected: []string{"ubfx w1, w8, #0, #8", "ubfx w2, w8, #8, #8", "lsr w0, w0, w1", "csel w0, wzr, w0, hs", "csinv w3, w3, wzr, lo", "tst w0, w0", "mov w21, w0"},
		},
		{
			name:     "vcvttsd2si rax, xmm0",
			bytes:    []byte{0xc4, 0xe1, 0xfb, 0x2c, 0xc0}, // vcvttsd2si rax, xmm0
			pc:       0x1000,
			expected: []string{"fcvtzs x19, d0"},
		},
		{
			name:     "vbroadcastf128 ymm0, [rip + 0x10]",
			bytes:    []byte{0xc4, 0xe2, 0x7d, 0x1a, 0x05, 0x10, 0x00, 0x00, 0x00},
			pc:       0x1000,
			expected: []string{"ldr q0, [x28, x16]", "mov v16.16b, v0.16b"},
		},
		{
			name:     "vbroadcastsd ymm1, [rip + 0x10]",
			bytes:    []byte{0xc4, 0xe2, 0x7d, 0x19, 0x0d, 0x10, 0x00, 0x00, 0x00},
			pc:       0x1000,
			expected: []string{"ldr d1, [x28, x16]", "dup v1.2d, v1.d[0]", "mov v17.16b, v1.16b"},
		},
		{
			name:     "vextractf128 xmm0, ymm1, 1",
			bytes:    []byte{0xc4, 0xe3, 0x7d, 0x19, 0xc8, 0x01},
			pc:       0x1000,
			expected: []string{"mov v0.16b, v17.16b", "movi v16.16b, #0"},
		},
		{
			name:     "vinsertf128 ymm0, ymm1, xmm2, 1",
			bytes:    []byte{0xc4, 0xe3, 0x75, 0x18, 0xc2, 0x01},
			pc:       0x1000,
			expected: []string{"mov v31.16b, v2.16b", "mov v0.16b, v1.16b", "mov v16.16b, v31.16b"},
		},
		{
			name:     "vextractps eax, xmm0, 1",
			bytes:    []byte{0xc4, 0xe3, 0x79, 0x17, 0xc0, 0x01},
			pc:       0x1000,
			expected: []string{"umov w16, v0.s[1]", "mov w19, w16"},
		},
		{
			name:     "vinsertps xmm0, xmm1, xmm2, 0x10",
			bytes:    []byte{0xc4, 0xe3, 0x71, 0x21, 0xc2, 0x10},
			pc:       0x1000,
			expected: []string{"mov v31.s[0], v2.s[0]", "mov v0.s[1], v31.s[0]", "movi v16.16b, #0"},
		},
		{
			name:     "vperm2f128 ymm0, ymm1, ymm2, 0x20",
			bytes:    []byte{0xc4, 0xe3, 0x75, 0x06, 0xc2, 0x20},
			pc:       0x1000,
			expected: []string{"mov v31.16b, v1.16b", "mov v19.16b, v2.16b", "mov v0.16b, v31.16b", "mov v16.16b, v19.16b"},
		},
		{
			name:     "vpermilps ymm0, ymm1, 0x1b",
			bytes:    []byte{0xc4, 0xe3, 0x7d, 0x04, 0xc1, 0x1b},
			pc:       0x1000,
			expected: []string{"mov v18.s[0], v1.s[3]", "mov v18.s[1], v1.s[2]", "mov v18.s[2], v1.s[1]", "mov v18.s[3], v1.s[0]", "mov v0.16b, v18.16b"},
		},
		{
			name:     "vpermilpd ymm0, ymm1, 0x01",
			bytes:    []byte{0xc4, 0xe3, 0x7d, 0x05, 0xc1, 0x01},
			pc:       0x1000,
			expected: []string{"mov v18.d[0], v1.d[1]", "mov v18.d[1], v1.d[0]", "mov v0.16b, v18.16b"},
		},
		{
			name:     "cpuid",
			bytes:    []byte{0x0f, 0xa2}, // cpuid
			pc:       0x1000,
			expected: []string{"// cpuid", "cbz w16", "cmp w16, #1"},
		},
		{
			name:     "xgetbv",
			bytes:    []byte{0x0f, 0x01, 0xd0}, // xgetbv
			pc:       0x1000,
			expected: []string{"// xgetbv", "mov x19, #7", "mov x22, #0"},
		},
		{
			name:     "vpinsrb x0, x0, r9l, 0x1",
			bytes:    []byte{0xc4, 0xc3, 0x79, 0x20, 0xc1, 0x01},
			pc:       0x1000,
			expected: []string{"mov w16, w9", "mov v0.b[1], w16", "movi v16.16b, #0"},
		},
		{
			name:     "vpmovmskb edx, x1",
			bytes:    []byte{0xc5, 0xf9, 0xd7, 0xd1},
			pc:       0x1000,
			expected: []string{"sshr v16.16b, v1.16b, #7", "and v16.16b, v16.16b, v17.16b", "addp v16.16b", "umov w16, v16.h[0]", "mov w22, w16"},
		},
		{
			name:     "vdpps x3, x3, x2, 0x7f",
			bytes:    []byte{0xc4, 0xe3, 0x61, 0x40, 0xda, 0x7f},
			pc:       0x1000,
			expected: []string{"fmul v16.4s, v3.4s, v2.4s", "faddp v17.4s, v16.4s, v16.4s", "faddp s17, v17.2s", "mov v3.s[0], v17.s[0]", "movi v19.16b, #0"},
		},
		{
			name:     "vucomiss x3, [rax]",
			bytes:    []byte{0xc5, 0xf8, 0x2e, 0x18},
			pc:       0x1000,
			expected: []string{"ldr s16, [x28, x16]", "fcmp s3, s16"},
		},
		{
			name:     "vucomisd x2, [rax+0x10]",
			bytes:    []byte{0xc5, 0xf9, 0x2e, 0x50, 0x10},
			pc:       0x1000,
			expected: []string{"ldr d16, [x28, x16]", "fcmp d2, d16"},
		},
		{
			name:     "vunpcklps x2, x0, x4",
			bytes:    []byte{0xc5, 0xf8, 0x14, 0xd4},
			pc:       0x1000,
			expected: []string{"zip1 v2.4s, v0.4s, v4.4s", "movi v18.16b, #0"},
		},
		{
			name:     "vunpcklps y2, y0, y4",
			bytes:    []byte{0xc5, 0xfc, 0x14, 0xd4},
			pc:       0x1000,
			expected: []string{"zip1 v2.4s, v0.4s, v4.4s", "zip1 v18.4s, v16.4s, v20.4s"},
		},
		{
			name:     "unpcklps x2, x4",
			bytes:    []byte{0x0f, 0x14, 0xd4},
			pc:       0x1000,
			expected: []string{"zip1 v2.4s, v2.4s, v4.4s"},
		},
		{
			name:     "vblendps x0, x1, x0, 0x3",
			bytes:    []byte{0xc4, 0xe3, 0x71, 0x0c, 0xc0, 0x03},
			pc:       0x1000,
			expected: []string{"bsl v28.16b, v0.16b, v1.16b", "mov v0.16b, v28.16b", "movi v16.16b, #0"},
		},
		{
			name:     "vmovshdup x1, x11",
			bytes:    []byte{0xc4, 0xc1, 0x7a, 0x16, 0xcb},
			pc:       0x1000,
			expected: []string{"trn2 v1.4s, v11.4s, v11.4s", "movi v17.16b, #0"},
		},
		{
			name:     "vmovsldup x1, x11",
			bytes:    []byte{0xc4, 0xc1, 0x7a, 0x12, 0xcb},
			pc:       0x1000,
			expected: []string{"trn1 v1.4s, v11.4s, v11.4s", "movi v17.16b, #0"},
		},
		{
			name:     "andn eax, ebx, ecx",
			bytes:    []byte{0xc4, 0xe2, 0x60, 0xf2, 0xc1},
			pc:       0x1000,
			expected: []string{"bics w19, w21, w20"},
		},
		{
			name:     "vandnps x0, x1, x2",
			bytes:    []byte{0xc5, 0xf0, 0x55, 0xc2},
			pc:       0x1000,
			expected: []string{"bic v0.16b, v2.16b, v1.16b", "movi v16.16b, #0"},
		},
		{
			name:     "vpshufb x0, x1, x2",
			bytes:    []byte{0xc4, 0xe2, 0x71, 0x00, 0xc2},
			pc:       0x1000,
			expected: []string{"tbl v0.16b, {v1.16b}, v29.16b", "movi v16.16b, #0"},
		},
		{
			name:     "vcmpps x0, x1, x2, 0",
			bytes:    []byte{0xc5, 0xf0, 0xc2, 0xc2, 0x00},
			pc:       0x1000,
			expected: []string{"fcmeq v0.4s, v1.4s, v2.4s", "movi v16.16b, #0"},
		},
		{
			name:     "vcvtdq2ps x0, x1",
			bytes:    []byte{0xc5, 0xf8, 0x5b, 0xc1},
			pc:       0x1000,
			expected: []string{"scvtf v0.4s, v1.4s", "movi v16.16b, #0"},
		},
		{
			name:     "vpslld x1, x1, 4",
			bytes:    []byte{0xc5, 0xf1, 0x72, 0xf1, 0x04},
			pc:       0x1000,
			expected: []string{"shl v1.4s, v1.4s, #4", "movi v17.16b, #0"},
		},
		{
			name:     "push qword [rax]",
			bytes:    []byte{0xff, 0x30}, // push [rax]
			pc:       0x1000,
			expected: []string{"ldr x16, [x28, x16]", "sub x26, x26, #8", "str x16, [x28, x26]"},
		},
		{
			name:     "pop qword [rax]",
			bytes:    []byte{0x8f, 0x00}, // pop [rax]
			pc:       0x1000,
			expected: []string{"ldr x16, [x28, x26]", "add x26, x26, #8", "str x16, [x28, x17]"},
		},
		{
			name:     "div dword [rcx]",
			bytes:    []byte{0xf7, 0x31}, // div dword [rcx]
			pc:       0x1000,
			expected: []string{"ldr w17, [x28, x16]", "udiv w16, w19, w17", "msub w22, w16, w17, w19"},
		},
		{
			name:     "mul qword [rcx]",
			bytes:    []byte{0x48, 0xf7, 0x21}, // mul qword [rcx]
			pc:       0x1000,
			expected: []string{"ldr x1, [x28, x16]", "mul x0, x19, x1", "umulh x22, x19, x1", "mov x19, x0"},
		},
		{
			name:     "mul r8b",
			bytes:    []byte{0x41, 0xf6, 0xe0}, // mul r8b
			pc:       0x1000,
			expected: []string{"and w1, w8, #0xff", "and w0, w19, #0xff", "mul w0, w0, w1", "bfi x19, x0, #0, #16"},
		},
		{
			name:     "not qword [rdx]",
			bytes:    []byte{0x48, 0xf7, 0x12}, // not qword [rdx]
			pc:       0x1000,
			expected: []string{"ldr x17, [x28, x16]", "mvn x17, x17", "str x17, [x28, x16]"},
		},
		{
			name:     "bts rax, 5",
			bytes:    []byte{0x48, 0x0f, 0xba, 0xe8, 0x05}, // bts rax, 5
			pc:       0x1000,
			expected: []string{"ubfx x2, x19, #5, #1", "rmif x2, #63, #2", "orr x19, x19, #0x20"},
		},
		{
			name:     "btr rax, 5",
			bytes:    []byte{0x48, 0x0f, 0xba, 0xf0, 0x05}, // btr rax, 5
			pc:       0x1000,
			expected: []string{"ubfx x2, x19, #5, #1", "rmif x2, #63, #2", "and x19, x19, #0xffffffffffffffdf"},
		},
		{
			name:     "blsi eax, ebx",
			bytes:    []byte{0xc4, 0xe2, 0x78, 0xf3, 0xdb}, // blsi eax, ebx
			pc:       0x1000,
			expected: []string{"neg w1, w0", "and w1, w0, w1", "tst w1, w1", "rmif x2, #63, #2", "mov w19, w1"},
		},
		{
			name:     "shlx eax, ebx, ecx",
			bytes:    []byte{0xc4, 0xe2, 0x69, 0xf7, 0xc3}, // shlx eax, ebx, ecx
			pc:       0x1000,
			expected: []string{"lsl w19, w16, w17"},
		},
		{
			name:     "rorx rax, rbx, 4",
			bytes:    []byte{0xc4, 0xe3, 0xfb, 0xf0, 0xc3, 0x04}, // rorx rax, rbx, 4
			pc:       0x1000,
			expected: []string{"ror x19, x16, #4"},
		},
		{
			name:     "vptest ymm0, ymm1",
			bytes:    []byte{0xc4, 0xe2, 0x7d, 0x17, 0xc1}, // vptest ymm0, ymm1
			pc:       0x1000,
			expected: []string{"and v31.16b, v1.16b, v0.16b", "bic v30.16b, v1.16b, v0.16b", "and v18.16b, v17.16b, v16.16b", "msr nzcv"},
		},
		{
			name:     "vpmulld xmm0, xmm1, xmm2",
			bytes:    []byte{0xc4, 0xe2, 0x71, 0x40, 0xc2}, // vpmulld xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"mul v0.4s, v1.4s, v2.4s", "movi v16.16b, #0"},
		},
		{
			name:     "vpmaxud xmm0, xmm1, xmm2",
			bytes:    []byte{0xc4, 0xe2, 0x71, 0x3f, 0xc2}, // vpmaxud xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"umax v0.4s, v1.4s, v2.4s", "movi v16.16b, #0"},
		},
		{
			name:     "vpmovzxbd ymm0, xmm1",
			bytes:    []byte{0xc4, 0xe2, 0x7d, 0x31, 0xc1}, // vpmovzxbd ymm0, xmm1
			pc:       0x1000,
			expected: []string{"uxtl v31.8h, v1.8b", "uxtl v0.4s, v31.4h", "uxtl2 v16.4s, v31.8h"},
		},
		{
			name:     "vroundps ymm0, ymm1, 1",
			bytes:    []byte{0xc4, 0xe3, 0x7d, 0x08, 0xc1, 0x01}, // vroundps ymm0, ymm1, 1 (floor)
			pc:       0x1000,
			expected: []string{"frintm v0.4s, v1.4s", "frintm v16.4s, v17.4s"},
		},
		{
			name:     "vhsubps xmm0, xmm1, xmm2",
			bytes:    []byte{0xc5, 0xf3, 0x7d, 0xc2}, // vhsubps xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"uzp1 v28.4s, v1.4s, v2.4s", "uzp2 v29.4s, v1.4s, v2.4s", "fsub v0.4s, v28.4s, v29.4s"},
		},
		{
			name:     "vphaddd xmm0, xmm1, xmm2",
			bytes:    []byte{0xc4, 0xe2, 0x71, 0x02, 0xc2}, // vphaddd xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"addp v0.4s, v1.4s, v2.4s", "movi v16.16b, #0"},
		},
		{
			name:     "vrcpps ymm0, ymm1",
			bytes:    []byte{0xc4, 0xe1, 0x7c, 0x53, 0xc1}, // vrcpps ymm0, ymm1
			pc:       0x1000,
			expected: []string{"frecpe v27.4s, v24.4s", "frecps v26.4s, v24.4s, v27.4s", "and v0.16b, v27.16b, v25.16b", "and v16.16b, v27.16b, v25.16b"},
		},
		{
			name:     "vmovlpd [rax], xmm0",
			bytes:    []byte{0xc5, 0xf9, 0x13, 0x00}, // vmovlpd [rax], xmm0
			pc:       0x1000,
			expected: []string{"str d0, [x28, x16]"},
		},
		{
			name:     "vpslldq xmm1, xmm1, 4",
			bytes:    []byte{0xc5, 0xf1, 0x73, 0xf9, 0x04}, // vpslldq xmm1, xmm1, 4
			pc:       0x1000,
			expected: []string{"movi v31.16b, #0", "ext v1.16b, v31.16b, v1.16b, #12", "movi v17.16b, #0"},
		},
		{
			name:     "vpalignr xmm0, xmm1, xmm2, 4",
			bytes:    []byte{0xc4, 0xe3, 0x71, 0x0f, 0xc2, 0x04}, // vpalignr xmm0, xmm1, xmm2, 4
			pc:       0x1000,
			expected: []string{"ext v0.16b, v2.16b, v1.16b, #4", "movi v16.16b, #0"},
		},
		{
			name:     "vmovntps [rax], ymm0",
			bytes:    []byte{0xc5, 0xfc, 0x2b, 0x00}, // vmovntps [rax], ymm0
			pc:       0x1000,
			expected: []string{"str q0, [x28, x16]", "str q16, [x28, x16]"},
		},
		{
			name:     "vaddsubps xmm0, xmm1, xmm2",
			bytes:    []byte{0xc5, 0xf3, 0xd0, 0xc2}, // vaddsubps xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"fsub v28.4s, v1.4s, v2.4s", "fadd v29.4s, v1.4s, v2.4s", "mov v0.s[1], v29.s[1]", "mov v0.s[3], v29.s[3]"},
		},
		{
			name:     "vcvtps2pd ymm0, xmm1",
			bytes:    []byte{0xc5, 0xfd, 0x5a, 0xc1}, // vcvtpd2ps xmm0, ymm1
			pc:       0x1000,
			expected: []string{"fcvtn v0.2s, v1.2d", "fcvtn2 v0.4s, v17.2d"},
		},
		{
			name:     "vcvttpd2dq xmm0, ymm1",
			bytes:    []byte{0xc5, 0xfd, 0xe6, 0xc1}, // vcvttpd2dq xmm0, ymm1
			pc:       0x1000,
			expected: []string{"mov d29, v1.d[0]", "fcvtzs x3, d29", "mov v28.s[3], w3", "mov v0.16b, v28.16b", "movi v16.16b, #0"},
		},
		{
			name:     "cmpxchg16b [rax]",
			bytes:    []byte{0x48, 0x0f, 0xc7, 0x08}, // cmpxchg16b [rax]
			pc:       0x1000,
			expected: []string{"caspal x0, x1, x2, x3, [x16]", "msr nzcv, x5"},
		},
		{
			name:     "clc",
			bytes:    []byte{0xf8}, // clc
			pc:       0x1000,
			expected: []string{"rmif xzr, #0, #2"},
		},
		{
			name:     "stc",
			bytes:    []byte{0xf9}, // stc
			pc:       0x1000,
			expected: []string{"rmif xzr, #0, #2"},
		},
		{
			name:     "leave",
			bytes:    []byte{0xc9}, // leave
			pc:       0x1000,
			expected: []string{"mov x26, x25", "ldr x25, [x28, x26]", "add x26, x26, #8"},
		},
		{
			name:     "shld eax, ebx, 4",
			bytes:    []byte{0x0f, 0xa4, 0xd8, 0x04}, // shld eax, ebx, 4
			pc:       0x1000,
			expected: []string{"orr x4, x0, x3, lsl #32", "lsl x5, x4, x1", "lsr x5, x5, #32", "mov w19, w5"},
		},
		{
			name:     "vpslld xmm0, xmm1, xmm2",
			bytes:    []byte{0xc5, 0xf1, 0xf2, 0xc2}, // vpslld xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"fmov x16, d2", "ushl v0.4s, v1.4s, v31.4s", "movi v16.16b, #0"},
		},
		{
			name:     "vpavgw xmm0, xmm1, xmm2",
			bytes:    []byte{0xc5, 0xf1, 0xe3, 0xc2}, // vpavgw xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"urhadd v0.8h, v1.8h, v2.8h", "movi v16.16b, #0"},
		},
		{
			name:     "vpabsd xmm0, xmm1",
			bytes:    []byte{0xc4, 0xe2, 0x79, 0x1e, 0xc1}, // vpabsd xmm0, xmm1
			pc:       0x1000,
			expected: []string{"abs v0.4s, v1.4s", "movi v16.16b, #0"},
		},
		{
			name:     "vpsadbw xmm0, xmm1, xmm2",
			bytes:    []byte{0xc5, 0xf1, 0xf6, 0xc2}, // vpsadbw xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"uabd v28.16b, v1.16b, v2.16b", "uaddlp v29.8h, v28.16b", "uaddlp v28.4s, v29.8h", "uaddlp v29.2d, v28.4s"},
		},
		{
			name:     "vpmaddwd xmm0, xmm1, xmm2",
			bytes:    []byte{0xc5, 0xf1, 0xf5, 0xc2}, // vpmaddwd xmm0, xmm1, xmm2
			pc:       0x1000,
			expected: []string{"smull v28.4s, v1.4h, v2.4h", "smull2 v29.4s, v1.8h, v2.8h", "addp v28.4s, v28.4s, v28.4s"},
		},
		{
			name:     "fldz",
			bytes:    []byte{0xd9, 0xee}, // fldz
			pc:       0x1000,
			expected: []string{"fmov d28, xzr", "add  x16, x27, #0x70", "str  w17, [x16, #0x2e0]"},
		},
		{
			name:     "fld1",
			bytes:    []byte{0xd9, 0xe8}, // fld1
			pc:       0x1000,
			expected: []string{"fmov d28, #1.0", "add  x16, x27, #0x70"},
		},
		{
			name:     "fstp st(0)",
			bytes:    []byte{0xdd, 0xd8}, // fstp st(0)
			pc:       0x1000,
			expected: []string{"add  w17, w17, #1", "str  w17, [x16, #0x2e0]"},
		},
		{
			name:     "fxch st(1)",
			bytes:    []byte{0xd9, 0xc9}, // fxch st(1)
			pc:       0x1000,
			expected: []string{"ldr  w17, [x16, #0x2e0]", "add  w17, w17, #1"},
		},
		{
			name:     "faddp st(1), st(0)",
			bytes:    []byte{0xde, 0xc1}, // faddp st(1), st(0)
			pc:       0x1000,
			expected: []string{"fadd d28, d28, d29", "add  w17, w17, #1"},
		},
		{
			name:     "fchs",
			bytes:    []byte{0xd9, 0xe0}, // fchs
			pc:       0x1000,
			expected: []string{"fneg d28, d28"},
		},
		{
			name:     "pxor mm0, mm0",
			bytes:    []byte{0x0f, 0xef, 0xc0},
			pc:       0x1000,
			expected: []string{"str xzr, [x5, #752]"},
		},
		{
			name:     "movq mm1, [rsi]",
			bytes:    []byte{0x0f, 0x6f, 0x0e},
			pc:       0x1000,
			expected: []string{"ldr x17, [x28, x16]", "str x17, [x5, #760]"},
		},
		{
			name:     "pmullw mm1, mm2",
			bytes:    []byte{0x0f, 0xd5, 0xca},
			pc:       0x1000,
			expected: []string{"ldr d16, [x5, #760]", "ldr d17, [x5, #768]", "mul v16.4h, v16.4h, v17.4h"},
		},
		{
			name:     "movsd string",
			bytes:    []byte{0xa5},
			pc:       0x1000,
			expected: []string{"ldr w16, [x28, x23]", "str w16, [x28, x24]"},
		},
		{
			name:     "cmpsd string",
			bytes:    []byte{0xa7},
			pc:       0x1000,
			expected: []string{"ldr w16, [x28, x23]", "ldr w17, [x28, x24]", "cmp w16, w17"},
		},
		{
			name:     "rep movsb",
			bytes:    []byte{0xf3, 0xa4},
			pc:       0x1000,
			expected: []string{"cbz x21", "ldrb w16, [x28, x23]", "strb w16, [x28, x24]"},
		},
		{
			name:     "rep stosd",
			bytes:    []byte{0xf3, 0xab},
			pc:       0x1000,
			expected: []string{"cbz x21", "str w19, [x28, x24]"},
		},
		{
			name:     "int 3",
			bytes:    []byte{0xcc},
			pc:       0x1000,
			expected: []string{"brk #0"},
		},
		{
			name:     "int 0x80",
			bytes:    []byte{0xcd, 0x80},
			pc:       0x1000,
			expected: []string{"brk #0"},
		},
		{
			name:     "emms",
			bytes:    []byte{0x0f, 0x77},
			pc:       0x1000,
			expected: []string{"// emms"},
		},
		{
			name:     "xorps xmm0, [rcx]",
			bytes:    []byte{0x0f, 0x57, 0x01},
			pc:       0x1000,
			expected: []string{"ldr q16, [x28, x16]", "eor v0.16b, v0.16b, v16.16b"},
		},
		{
			name:     "vpermilps ymm0, ymm1, ymm2",
			bytes:    []byte{0xc4, 0xe2, 0x75, 0x0c, 0xc2},
			pc:       0x1000,
			expected: []string{"tbl v0.16b, {v1.16b}, v23.16b", "tbl v16.16b, {v17.16b}, v23.16b"},
		},
		{
			name:     "ldmxcsr [rcx]",
			bytes:    []byte{0x0f, 0xae, 0x11},
			pc:       0x1000,
			expected: []string{"ldr w0, [x28, x16]", "str w0, [x17, #0x2e8]", "msr fpcr, x1"},
		},
		{
			name:     "stmxcsr [rdx]",
			bytes:    []byte{0x0f, 0xae, 0x1a},
			pc:       0x1000,
			expected: []string{"ldr w0, [x17, #0x2e8]", "str w0, [x28, x16]"},
		},
		{
			name:     "pmaddubsw x0, x5",
			bytes:    []byte{0x66, 0x0f, 0x38, 0x04, 0xc5},
			pc:       0x1000,
			expected: []string{"uxtl v20.8h, v0.8b", "sxtl v21.8h, v5.8b", "sqadd v25.4h"},
		},
		{
			name:     "pmuldq x0, x3",
			bytes:    []byte{0x66, 0x0f, 0x38, 0x28, 0xc3},
			pc:       0x1000,
			expected: []string{"smull v0.2d, v20.2s, v21.2s"},
		},
		{
			name:     "pmulhw x1, x4",
			bytes:    []byte{0x66, 0x0f, 0xe5, 0xcc},
			pc:       0x1000,
			expected: []string{"smull  v20.4s, v1.4h, v4.4h", "uzp2   v1.8h, v20.8h, v21.8h"},
		},
		{
			name:     "pushfq",
			bytes:    []byte{0x9c},
			pc:       0x1000,
			expected: []string{"cset w2, cc", "sub x26, x26, #8", "str x0, [x28, x26]"},
		},
		{
			name:     "popfq",
			bytes:    []byte{0x9d},
			pc:       0x1000,
			expected: []string{"ldr x0, [x28, x26]", "add x26, x26, #8", "rmif x0, #4, #12", "rmif x0, #63, #2"},
		},
		{
			name:     "xlatb",
			bytes:    []byte{0xd7},
			pc:       0x1000,
			expected: []string{"uxtb w16, w19", "add  x16, x20, x16", "ldrb w16, [x28, x16]"},
		},
		{
			name:     "lret",
			bytes:    []byte{0xcb},
			pc:       0x1000,
			expected: []string{"ldr x30, [sp], #16", "ret"},
		},
		{
			name:     "fscale",
			bytes:    []byte{0xd9, 0xfd},
			pc:       0x1000,
			expected: []string{"frintz d31, d29", "fcvtzs x2, d31", "fmul d28, d28, d31", "msr  nzcv, x7"},
		},
		{
			name:     "fprem1",
			bytes:    []byte{0xd9, 0xf5},
			pc:       0x1000,
			expected: []string{"subs x1, x2, x3", "lsl  x2, x6, #1", "scvtf d28, x6", "msr  nzcv, x7"},
		},
		{
			name:     "vaesenc x0, x0, x1",
			bytes:    []byte{0xc4, 0xe2, 0x79, 0xdc, 0xc1},
			pc:       0x1000,
			expected: []string{"aese  v21.16b, v20.16b", "aesmc v21.16b, v21.16b", "eor   v0.16b, v21.16b, v1.16b"},
		},
		{
			name:     "vaesenclast x0, x0, x1",
			bytes:    []byte{0xc4, 0xe2, 0x79, 0xdd, 0xc1},
			pc:       0x1000,
			expected: []string{"aese  v21.16b, v20.16b", "eor   v0.16b, v21.16b, v1.16b"},
		},
		{
			name:     "cwd",
			bytes:    []byte{0x66, 0x99},
			pc:       0x1000,
			expected: []string{"sxth w16, w19", "asr  w16, w16, #16", "bfi  x22, x16, #0, #16"},
		},
		{
			name:     "enter 0, 0",
			bytes:    []byte{0xc8, 0x00, 0x00, 0x00},
			pc:       0x1000,
			expected: []string{"sub  x26, x26, #8", "str  x25, [x28, x26]", "mov  x25, x26"},
		},
		{
			name:     "crc32 rax, rbx",
			bytes:    []byte{0xf2, 0x48, 0x0f, 0x38, 0xf1, 0xc3},
			pc:       0x1000,
			expected: []string{"crc32cx w19, w17, x16"},
		},
		{
			name:     "rcl rdi, 1",
			bytes:    []byte{0x48, 0xd1, 0xd7},
			pc:       0x1000,
			expected: []string{"cset w2, cc", "lsl x5, x3, x1", "rmif x4, #0, #3", "mov x24, x5"},
		},
		{
			name:     "fcos",
			bytes:    []byte{0xd9, 0xff},
			pc:       0x1000,
			expected: []string{"b _recomp_unsupported_instruction"},
		},
		{
			name:     "frndint",
			bytes:    []byte{0xd9, 0xfc},
			pc:       0x1000,
			expected: []string{"msr  fpcr, x7", "frintx d28, d28", "msr  fpcr, x5"},
		},
		{
			name:  "vpclmulqdq x0, x1, x2, 0",
			bytes: []byte{0xc4, 0xe3, 0x71, 0x44, 0xc2, 0x00},
			pc:    0x1000,
		},
		{
			name:     "sar al, 7",
			bytes:    []byte{0xc0, 0xf8, 0x07}, // sar al, 7
			pc:       0x1000,
			expected: []string{"sxtb x4, w17", "asr x0, x4, #7", "cmn wzr, w0, lsl #24", "bfi x19, x0, #0, #8"},
		},
		{
			name:  "vpcmpistri x1, x0, 0",
			bytes: []byte{0xc4, 0xe3, 0x79, 0x63, 0xc8, 0x00},
			pc:    0x1000,
			expected: []string{
				"stp  x8,  x9,  [sp, #32]",
				"stp  x10, x11, [sp, #48]",
				"stp  x12, x13, [sp, #64]",
				"stp  x14, x15, [sp, #80]",
				"bl   _recomp_vpcmpistri",
				"ldp  x8,  x9,  [sp, #32]",
				"ldp  x10, x11, [sp, #48]",
				"ldp  x12, x13, [sp, #64]",
				"ldp  x14, x15, [sp, #80]",
			},
		},
		{
			name:  "setae byte [rbp - 0x10]",
			bytes: []byte{0x0f, 0x93, 0x45, 0xf0},
			pc:    0x1000,
			expected: []string{
				"cset w16, cs",
				"sub x17, x17, #16",
				"strb w16, [x28, x17]",
			},
		},
		{
			name:     "or al, [rbp - 0x10]",
			bytes:    []byte{0x0a, 0x45, 0xf0},
			pc:       0x1000,
			expected: []string{"lsl w17, w17, #24", "orr w17, w17, w16, lsl #24", "tst w17, w17", "lsr w17, w17, #24", "bfi x19, x17, #0, #8"},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			lifter.ResetFlags()
			inst, err := x86asm.Decode(tc.bytes, 64)
			if err != nil {
				t.Fatalf("failed to decode: %v", err)
			}
			dInst := disasm.Instruction{Address: tc.pc, Inst: inst}
			nextPC := tc.pc + uint64(inst.Len)
			lines, err := lifter.LiftInstructionToBuf(dInst, nextPC, nil, nil)
			if err != nil {
				t.Fatalf("LiftInstructionToBuf failed: %v", err)
			}
			joined := strings.Join(lines, "\n")
			for _, exp := range tc.expected {
				if !strings.Contains(joined, exp) {
					t.Errorf("expected %q in output, got:\n%s", exp, joined)
				}
			}
		})
	}
}

func TestAddJccCarryPolarity(t *testing.T) {
	lifter := NewArm64Lifter(nil)
	fn := &disasm.Function{
		EntryAddr: 0x1000,
		EndAddr:   0x2000,
		Blocks: map[uint64]*disasm.BasicBlock{
			0x1000: {StartAddr: 0x1000},
			0x1012: {StartAddr: 0x1012},
		},
	}

	// 1. ADD r15, rax: 49 01 c7
	addInst, err := x86asm.Decode([]byte{0x49, 0x01, 0xc7}, 64)
	if err != nil {
		t.Fatalf("decode add failed: %v", err)
	}
	dAdd := disasm.Instruction{Address: 0x1000, Inst: addInst}
	_, err = lifter.LiftInstructionToBuf(dAdd, 0x1003, fn, nil)
	if err != nil {
		t.Fatalf("lift add failed: %v", err)
	}

	// 2. JAE +13: 73 0d -> after ADD, no carry (CF=0) must be b.cc, NOT b.cs!
	jaeInst, err := x86asm.Decode([]byte{0x73, 0x0d}, 64)
	if err != nil {
		t.Fatalf("decode jae failed: %v", err)
	}
	// CF dead after the branch: JAE reads C in the ADD sense (C = CF).
	dJae := disasm.Instruction{Address: 0x1003, Inst: jaeInst, CarryDead: true}
	lines, err := lifter.LiftInstructionToBuf(dJae, 0x1005, fn, nil)
	if err != nil {
		t.Fatalf("lift jae failed: %v", err)
	}
	joined := strings.Join(lines, "\n")
	if !strings.Contains(joined, "b.cc") {
		t.Errorf("expected b.cc for JAE following ADD, got:\n%s", joined)
	}
	if strings.Contains(joined, "b.cs") || strings.Contains(joined, "cfinv") {
		t.Errorf("unexpected b.cs/cfinv for JAE following ADD with CF dead, got:\n%s", joined)
	}

	// CF live after the branch: C is first brought into the block-boundary sense (C = !CF).
	lifter.ResetFlags()
	if _, err := lifter.LiftInstructionToBuf(dAdd, 0x1003, fn, nil); err != nil {
		t.Fatalf("lift add failed: %v", err)
	}
	dJae.CarryDead = false
	lines, err = lifter.LiftInstructionToBuf(dJae, 0x1005, fn, nil)
	if err != nil {
		t.Fatalf("lift jae failed: %v", err)
	}
	joined = strings.Join(lines, "\n")
	if !strings.Contains(joined, "cfinv") || !strings.Contains(joined, "b.cs") {
		t.Errorf("expected cfinv + b.cs for JAE following ADD with CF live, got:\n%s", joined)
	}
	dJae.CarryDead = true

	// 3. Reset and lift CMP followed by JAE -> after CMP, A >= B (no borrow) must be b.cs!
	lifter.ResetFlags()
	cmpInst, err := x86asm.Decode([]byte{0x49, 0x39, 0xc7}, 64) // cmp r15, rax
	if err != nil {
		t.Fatalf("decode cmp failed: %v", err)
	}
	dCmp := disasm.Instruction{Address: 0x1000, Inst: cmpInst}
	_, err = lifter.LiftInstructionToBuf(dCmp, 0x1003, fn, nil)
	if err != nil {
		t.Fatalf("lift cmp failed: %v", err)
	}

	linesCmp, err := lifter.LiftInstructionToBuf(dJae, 0x1005, fn, nil)
	if err != nil {
		t.Fatalf("lift jae after cmp failed: %v", err)
	}
	joinedCmp := strings.Join(linesCmp, "\n")
	if !strings.Contains(joinedCmp, "b.cs") {
		t.Errorf("expected b.cs for JAE following CMP, got:\n%s", joinedCmp)
	}
}

func TestRolRorVariableAndImmediate(t *testing.T) {
	lifter := NewArm64Lifter(map[uint64]bool{0x1000: true})
	fn := &disasm.Function{EntryAddr: 0x1000}

	tests := []struct {
		name     string
		bytes    []byte
		contains []string
	}{
		{
			name:     "rol r10d, cl (variable 32-bit)",
			bytes:    []byte{0x41, 0xd3, 0xc2}, // rol r10d, cl
			contains: []string{"neg w5, w21", "ror w10, w10, w5"},
		},
		{
			name:     "rol r9d, cl (variable 32-bit)",
			bytes:    []byte{0x41, 0xd3, 0xc1}, // rol r9d, cl
			contains: []string{"neg w5, w21", "ror w9, w9, w5"},
		},
		{
			name:     "rol rax, cl (variable 64-bit)",
			bytes:    []byte{0x48, 0xd3, 0xc0}, // rol rax, cl
			contains: []string{"neg x5, x21", "ror x19, x19, x5"},
		},
		{
			name:     "ror r10d, cl (variable 32-bit)",
			bytes:    []byte{0x41, 0xd3, 0xca}, // ror r10d, cl
			contains: []string{"ror w10, w10, w21"},
		},
		{
			name:     "rol r10d, 5 (imm 32-bit)",
			bytes:    []byte{0x41, 0xc1, 0xc2, 0x05}, // rol r10d, 5
			contains: []string{"ror w10, w10, #27"},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			inst, err := x86asm.Decode(tc.bytes, 64)
			if err != nil {
				t.Fatalf("decode failed: %v", err)
			}
			// CF/OF dead: rotates then only change the value.
			dInst := disasm.Instruction{Address: 0x1000, Inst: inst, CarryDead: true, OverflowDead: true}
			lines, err := lifter.LiftInstructionToBuf(dInst, 0x1004, fn, nil)
			if err != nil {
				t.Fatalf("lift failed: %v", err)
			}
			joined := strings.Join(lines, "\n")
			for _, expected := range tc.contains {
				if !strings.Contains(joined, expected) {
					t.Errorf("expected %q in output:\n%s", expected, joined)
				}
			}
		})
	}
}

func TestShiftFlagUpdates(t *testing.T) {
	lifter := NewArm64Lifter(map[uint64]bool{0x1000: true})
	fn := &disasm.Function{
		EntryAddr: 0x1000,
		EndAddr:   0x2000,
		Blocks: map[uint64]*disasm.BasicBlock{
			0x1000: {StartAddr: 0x1000},
			0x1016: {StartAddr: 0x1016},
		},
	}

	// 1. SHR rdx, 0x20: 48 c1 ea 20
	shrInst, err := x86asm.Decode([]byte{0x48, 0xc1, 0xea, 0x20}, 64)
	if err != nil {
		t.Fatalf("decode shr failed: %v", err)
	}
	// ZF is live (JNE), CF/OF are dead: the shift updates SF/ZF with TST.
	dShr := disasm.Instruction{Address: 0x1000, Inst: shrInst, SkipFlags: false, CarryDead: true, OverflowDead: true}
	linesShr, err := lifter.LiftInstructionToBuf(dShr, 0x1004, fn, nil)
	if err != nil {
		t.Fatalf("lift shr failed: %v", err)
	}
	joinedShr := strings.Join(linesShr, "\n")
	if !strings.Contains(joinedShr, "lsr x22, x22, #32") {
		t.Errorf("expected lsr x22, x22, #32, got:\n%s", joinedShr)
	}
	if !strings.Contains(joinedShr, "tst x22, x22") {
		t.Errorf("expected tst x22, x22, got:\n%s", joinedShr)
	}

	// 2. JNE +16: 75 10
	jneInst, err := x86asm.Decode([]byte{0x75, 0x10}, 64)
	if err != nil {
		t.Fatalf("decode jne failed: %v", err)
	}
	dJne := disasm.Instruction{Address: 0x1004, Inst: jneInst}
	linesJne, err := lifter.LiftInstructionToBuf(dJne, 0x1006, fn, nil)
	if err != nil {
		t.Fatalf("lift jne failed: %v", err)
	}
	joinedJne := strings.Join(linesJne, "\n")
	if !strings.Contains(joinedJne, "b.ne") {
		t.Errorf("expected b.ne for JNE following SHR, got:\n%s", joinedJne)
	}
}

func TestTzcntLzcntBsfBsrFlags(t *testing.T) {
	lifter := NewArm64Lifter(map[uint64]bool{0x1000: true})
	fn := &disasm.Function{
		EntryAddr: 0x1000,
		EndAddr:   0x2000,
		Blocks: map[uint64]*disasm.BasicBlock{
			0x1000: {StartAddr: 0x1000},
			0x1016: {StartAddr: 0x1016},
		},
	}

	// 1. TZCNT R8L, EDX: f3 44 0f bc c2
	tzcntInst, err := x86asm.Decode([]byte{0xf3, 0x44, 0x0f, 0xbc, 0xc2}, 64)
	if err != nil {
		t.Fatalf("decode tzcnt failed: %v", err)
	}
	dTzcnt := disasm.Instruction{Address: 0x1000, Inst: tzcntInst, SkipFlags: false}
	linesTzcnt, err := lifter.LiftInstructionToBuf(dTzcnt, 0x1005, fn, nil)
	if err != nil {
		t.Fatalf("lift tzcnt failed: %v", err)
	}
	joinedTzcnt := strings.Join(linesTzcnt, "\n")
	// CF = (src == 0), ZF = (result == 0): CMP src then CCMP result.
	if !strings.Contains(joinedTzcnt, "cmp w0, #0") || !strings.Contains(joinedTzcnt, "ccmp w1, #0, #0, ne") {
		t.Errorf("expected 'cmp w0, #0' + 'ccmp w1, #0, #0, ne' in TZCNT output, got:\n%s", joinedTzcnt)
	}

	// 2. CMOVAE ECX, EAX: 0f 43 c8
	cmovaeInst, err := x86asm.Decode([]byte{0x0f, 0x43, 0xc8}, 64)
	if err != nil {
		t.Fatalf("decode cmovae failed: %v", err)
	}
	dCmovae := disasm.Instruction{Address: 0x1005, Inst: cmovaeInst}
	linesCmovae, err := lifter.LiftInstructionToBuf(dCmovae, 0x1008, fn, nil)
	if err != nil {
		t.Fatalf("lift cmovae failed: %v", err)
	}
	joinedCmovae := strings.Join(linesCmovae, "\n")
	if !strings.Contains(joinedCmovae, "csel w21, w19, w21, cs") {
		t.Errorf("expected 'csel w21, w19, w21, cs' for CMOVAE after TZCNT, got:\n%s", joinedCmovae)
	}

	// 3. LZCNT R8L, EDX: f3 44 0f bd c2
	lzcntInst, err := x86asm.Decode([]byte{0xf3, 0x44, 0x0f, 0xbd, 0xc2}, 64)
	if err != nil {
		t.Fatalf("decode lzcnt failed: %v", err)
	}
	dLzcnt := disasm.Instruction{Address: 0x1008, Inst: lzcntInst, SkipFlags: false}
	linesLzcnt, err := lifter.LiftInstructionToBuf(dLzcnt, 0x100d, fn, nil)
	if err != nil {
		t.Fatalf("lift lzcnt failed: %v", err)
	}
	joinedLzcnt := strings.Join(linesLzcnt, "\n")
	if !strings.Contains(joinedLzcnt, "cmp w0, #0") || !strings.Contains(joinedLzcnt, "ccmp w1, #0, #0, ne") {
		t.Errorf("expected 'cmp w0, #0' + 'ccmp w1, #0, #0, ne' in LZCNT output, got:\n%s", joinedLzcnt)
	}

	// 4. BSF EAX, EDX: 0f bc c2
	bsfInst, err := x86asm.Decode([]byte{0x0f, 0xbc, 0xc2}, 64)
	if err != nil {
		t.Fatalf("decode bsf failed: %v", err)
	}
	dBsf := disasm.Instruction{Address: 0x100d, Inst: bsfInst, SkipFlags: false}
	linesBsf, err := lifter.LiftInstructionToBuf(dBsf, 0x1010, fn, nil)
	if err != nil {
		t.Fatalf("lift bsf failed: %v", err)
	}
	joinedBsf := strings.Join(linesBsf, "\n")
	if !strings.Contains(joinedBsf, "cmp w0, #0") {
		t.Errorf("expected 'cmp w0, #0' in BSF output, got:\n%s", joinedBsf)
	}
}

func TestVectorShiftPreservesFlags(t *testing.T) {
	lifter := NewArm64Lifter(map[uint64]bool{0x1000: true})
	fn := &disasm.Function{
		EntryAddr: 0x1000,
		EndAddr:   0x2000,
		Blocks: map[uint64]*disasm.BasicBlock{
			0x1000: {StartAddr: 0x1000},
		},
	}

	// VPSRLQ X0, X1, X2: c5 f1 d3 c2
	vpsrlqInst, err := x86asm.Decode([]byte{0xc5, 0xf1, 0xd3, 0xc2}, 64)
	if err != nil {
		t.Fatalf("decode vpsrlq failed: %v", err)
	}
	dVpsrlq := disasm.Instruction{Address: 0x1000, Inst: vpsrlqInst}
	lines, err := lifter.LiftInstructionToBuf(dVpsrlq, 0x1004, fn, nil)
	if err != nil {
		t.Fatalf("lift vpsrlq failed: %v", err)
	}
	joined := strings.Join(lines, "\n")
	if !strings.Contains(joined, "mrs x17, nzcv") || !strings.Contains(joined, "msr nzcv, x17") {
		t.Errorf("expected VPSRLQ to preserve NZCV, got:\n%s", joined)
	}

	// PSRLQ X0, X1: 66 0f d3 c1
	psrlqInst, err := x86asm.Decode([]byte{0x66, 0x0f, 0xd3, 0xc1}, 64)
	if err != nil {
		t.Fatalf("decode psrlq failed: %v", err)
	}
	dPsrlq := disasm.Instruction{Address: 0x1004, Inst: psrlqInst}
	linesPsrlq, err := lifter.LiftInstructionToBuf(dPsrlq, 0x1008, fn, nil)
	if err != nil {
		t.Fatalf("lift psrlq failed: %v", err)
	}
	joinedPsrlq := strings.Join(linesPsrlq, "\n")
	if !strings.Contains(joinedPsrlq, "mrs x17, nzcv") || !strings.Contains(joinedPsrlq, "msr nzcv, x17") {
		t.Errorf("expected PSRLQ to preserve NZCV, got:\n%s", joinedPsrlq)
	}
}
