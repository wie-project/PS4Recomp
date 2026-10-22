package lifter

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"

	"golang.org/x/arch/x86/x86asm"
)

// Execution tests: a short x86 instruction sequence runs natively under Rosetta 2 and, lifted,
// on the host, from the same registers, flags and memory; the GPRs, CF/ZF/SF/OF and a memory
// window must come out identical. This covers what the lifter-difftest-asm harness filters out
// (string instructions, stack operations, x87 environment and control word, instruction
// sequences).
//
// Both sides map the window at the same fixed address, so guest addresses are host addresses
// (the lifted side runs with x28, the memory base, at 0). The lifted side's FS base (x27)
// points at a TCB whose +0x70 slot holds a fresh GuestContext (x87 stack empty, FCW 0x37f,
// MXCSR 0x1f80), the native side starts after FNINIT with MXCSR 0x1f80.

const (
	execMemBase = 0x7000000000 // window: [execMemBase, execMemBase+execMemSize)
	execMemSize = 0x2000
	execCtx     = execMemBase + 0x2000 // GuestContext (lifted side only; not compared)
	execTcb     = execMemBase + 0x3000
	execMapSize = 0x4000
	execRSP     = execMemBase + 0x1800 // default RSP
)

// x86 RFLAGS bits.
const (
	fCF = 1 << 0
	fZF = 1 << 6
	fSF = 1 << 7
	fOF = 1 << 11
)

// x86 GPR order of the state block.
var execRegs = []x86asm.Reg{x86asm.RAX, x86asm.RCX, x86asm.RDX, x86asm.RBX, x86asm.RSP, x86asm.RBP,
	x86asm.RSI, x86asm.RDI, x86asm.R8, x86asm.R9, x86asm.R10, x86asm.R11, x86asm.R12, x86asm.R13,
	x86asm.R14, x86asm.R15}

type execCase struct {
	name  string
	code  []string              // x86 instructions, hex, lifted as one block
	regs  map[x86asm.Reg]uint64 // initial GPRs (0 if absent; RSP defaults to execRSP)
	flags uint64                // initial RFLAGS (CF/ZF/SF/OF)
	mem   map[int][]byte        // initial window bytes by offset (the rest is 0)
	// cmpFlags selects the flags compared after the sequence (0: CF|ZF|SF|OF); noFlags
	// compares none (flags x86 leaves undefined).
	cmpFlags uint64
	noFlags  bool
	// prep, when set, adjusts the lifted instruction (liveness bits the disasm pass would set).
	prep func(i int, in *disasm.Instruction)
}

// execState mirrors struct st in the drivers.
const (
	stRegs   = 0   // 16 x u64
	stFlags  = 128 // u64 x86 RFLAGS
	stTcb    = 136 // u64 FS base (lifted)
	stNzcv   = 144 // u64 initial NZCV (lifted)
	stOut    = 152 // u8 OF, CF, ZF, SF read back by SETcc (lifted)
	stSize   = 160
	readOps  = "0f90c0 0f92c0 0f94c0 0f98c0" // seto al, setb al, sete al, sets al
	readMask = fOF | fCF | fZF | fSF
)

var readFlagBits = []uint64{fOF, fCF, fZF, fSF}

func execSkip(t *testing.T) {
	t.Helper()
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		t.Skip("needs an arm64 Mac (host lowering) with Rosetta 2 (x86 reference)")
	}
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	if err := exec.Command("arch", "-x86_64", "/usr/bin/true").Run(); err != nil {
		t.Skip("Rosetta 2 not installed")
	}
}

// liftSeq lifts the instructions of one case at pc with l (one lifter for all cases keeps the
// generated labels unique) and returns the lowering and the function (the readout continues in
// it, from the carry sense the sequence left).
func liftSeq(t *testing.T, l *Arm64Lifter, c execCase, pc uint64) ([]string, *disasm.Function) {
	t.Helper()
	fn := &disasm.Function{EntryAddr: pc, EndAddr: pc + 0x100,
		Blocks: map[uint64]*disasm.BasicBlock{pc: {StartAddr: pc}}, BlockOrder: []uint64{pc}}
	var out []string
	for i, h := range c.code {
		b, err := hex.DecodeString(h)
		if err != nil {
			t.Fatalf("%s: bad hex %q", c.name, h)
		}
		inst, err := x86asm.Decode(b, 64)
		if err != nil {
			t.Fatalf("%s: decode %s: %v", c.name, h, err)
		}
		di := disasm.Instruction{Address: pc, Inst: inst}
		if c.prep != nil {
			c.prep(i, &di)
		}
		next := pc + uint64(inst.Len)
		out, err = l.LiftInstructionToBuf(di, next, fn, out)
		if err != nil {
			t.Fatalf("%s: lift %v: %v", c.name, inst, err)
		}
		pc = next
	}
	return out, fn
}

func runExecCases(t *testing.T, cases []execCase) {
	t.Helper()
	execSkip(t)
	dir := t.TempDir()
	if k := os.Getenv("EXEC_KEEP"); k != "" {
		dir = k
		os.MkdirAll(dir, 0o755)
	}

	var host, native, data strings.Builder
	host.WriteString(".text\n")
	native.WriteString(".intel_syntax noprefix\n.text\n")
	data.WriteString("#include <stdint.h>\nstruct init { int off, len; const uint8_t *b; };\n")
	l := NewArm64Lifter(nil)
	for i, c := range cases {
		pc := uint64(0x10000 + i*0x1000)
		lines, fn := liftSeq(t, l, c, pc)
		for _, ln := range lines {
			if strings.Contains(ln, "_recomp_unsupported") {
				t.Fatalf("%s: not lifted:\n%s", c.name, strings.Join(lines, "\n"))
			}
		}
		// Host function.
		fmt.Fprintf(&host, ".globl _lifted_%d\n.p2align 2\n_lifted_%d:\n", i, i)
		host.WriteString("    stp x29, x30, [sp, #-128]!\n    mov x29, sp\n" +
			"    stp x19, x20, [sp, #16]\n    stp x21, x22, [sp, #32]\n    stp x23, x24, [sp, #48]\n" +
			"    stp x25, x26, [sp, #64]\n    stp x27, x28, [sp, #80]\n    str x0, [sp, #96]\n" +
			"    mrs x1, fpcr\n    str x1, [sp, #104]\n")
		for k, r := range execRegs {
			fmt.Fprintf(&host, "    ldr %s, [x0, #%d]\n", arm64RegMap[r].X, stRegs+8*k)
		}
		fmt.Fprintf(&host, "    ldr x27, [x0, #%d]\n    mov x28, #0\n    ldr x1, [x0, #%d]\n    msr nzcv, x1\n", stTcb, stNzcv)
		for _, ln := range lines {
			host.WriteString(ln + "\n")
		}
		host.WriteString("    ldr x16, [sp, #96]\n")
		for k, r := range execRegs {
			fmt.Fprintf(&host, "    str %s, [x16, #%d]\n", arm64RegMap[r].X, stRegs+8*k)
		}
		rpc := pc + 0x800
		for k, h := range strings.Fields(readOps) {
			b, _ := hex.DecodeString(h)
			inst, _ := x86asm.Decode(b, 64)
			r, err := l.LiftInstructionToBuf(disasm.Instruction{Address: rpc, Inst: inst}, rpc+3, fn, nil)
			if err != nil {
				t.Fatal(err)
			}
			for _, ln := range r {
				host.WriteString(ln + "\n")
			}
			fmt.Fprintf(&host, "    ldr x16, [sp, #96]\n    strb w19, [x16, #%d]\n", stOut+k)
			rpc += 3
		}
		host.WriteString("    ldr x1, [sp, #104]\n    msr fpcr, x1\n" +
			"    ldp x19, x20, [sp, #16]\n    ldp x21, x22, [sp, #32]\n    ldp x23, x24, [sp, #48]\n" +
			"    ldp x25, x26, [sp, #64]\n    ldp x27, x28, [sp, #80]\n    ldp x29, x30, [sp], #128\n    ret\n")

		// Native function: rdi = state.
		fmt.Fprintf(&native, ".globl _native_%d\n_native_%d:\n", i, i)
		native.WriteString("    push rbx\n    push rbp\n    push r12\n    push r13\n    push r14\n    push r15\n" +
			"    mov [rip+_g_state], rdi\n    mov [rip+_g_rsp], rsp\n    fninit\n    ldmxcsr [rip+_g_mxcsr]\n" +
			"    push qword ptr [rdi+128]\n    popfq\n")
		for k, r := range execRegs {
			if r != x86asm.RDI {
				fmt.Fprintf(&native, "    mov %s, [rdi+%d]\n", strings.ToLower(r.String()), stRegs+8*k)
			}
		}
		native.WriteString("    mov rdi, [rdi+56]\n")
		for _, h := range c.code {
			b, _ := hex.DecodeString(h)
			var bs []string
			for _, x := range b {
				bs = append(bs, fmt.Sprintf("0x%02x", x))
			}
			native.WriteString("    .byte " + strings.Join(bs, ", ") + "\n")
		}
		native.WriteString("    mov [rip+_g_tmp], rax\n    mov rax, [rip+_g_state]\n")
		for k, r := range execRegs {
			if r != x86asm.RAX {
				fmt.Fprintf(&native, "    mov [rax+%d], %s\n", stRegs+8*k, strings.ToLower(r.String()))
			}
		}
		native.WriteString("    mov rcx, [rip+_g_tmp]\n    mov [rax], rcx\n    mov rsp, [rip+_g_rsp]\n" +
			"    pushfq\n    pop rcx\n    mov [rax+128], rcx\n    fninit\n" +
			"    pop r15\n    pop r14\n    pop r13\n    pop r12\n    pop rbp\n    pop rbx\n    ret\n")

		// Initial memory.
		fmt.Fprintf(&data, "static const struct init init_%d[] = {", i)
		for off, b := range c.mem {
			var bs []string
			for _, x := range b {
				bs = append(bs, fmt.Sprintf("%d", x))
			}
			fmt.Fprintf(&data, "{%d, %d, (const uint8_t[]){%s}}, ", off, len(b), strings.Join(bs, ","))
		}
		data.WriteString("{-1, 0, 0}};\n")
	}
	native.WriteString(".data\n.p2align 3\n_g_state: .quad 0\n_g_rsp: .quad 0\n_g_tmp: .quad 0\n_g_mxcsr: .long 0x1f80\n")

	// Drivers: read the initial states from in.bin, run each case, write state + window.
	n := len(cases)
	data.WriteString("static const struct init *inits[] = {")
	for i := 0; i < n; i++ {
		fmt.Fprintf(&data, "init_%d, ", i)
	}
	data.WriteString("};\n")
	driver := func(prefix string, lifted bool) string {
		var d strings.Builder
		d.WriteString(`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "data.h"
`)
		for i := 0; i < n; i++ {
			fmt.Fprintf(&d, "void %s_%d(uint8_t *st);\n", prefix, i)
		}
		fmt.Fprintf(&d, "static void (*fns[])(uint8_t *) = {")
		for i := 0; i < n; i++ {
			fmt.Fprintf(&d, "%s_%d, ", prefix, i)
		}
		fmt.Fprintf(&d, `};
int main(int argc, char **argv) {
    uint8_t *m = mmap((void *)0x%xULL, 0x%x, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (m == MAP_FAILED || m != (uint8_t *)0x%xULL) { perror("mmap"); return 1; }
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    for (int i = 0; i < %d; i++) {
        uint8_t st[%d];
        if (fread(st, sizeof st, 1, in) != 1) return 2;
        memset(m, 0, 0x%x);
        for (const struct init *p = inits[i]; p->off >= 0; p++) memcpy(m + p->off, p->b, p->len);
`, execMemBase, execMapSize, execMemBase, n, stSize, execMapSize)
		if lifted {
			fmt.Fprintf(&d, `        uint8_t *ctx = m + 0x%x, *tcb = m + 0x%x;
        uint64_t c = (uint64_t)ctx; memcpy(tcb + 0x70, &c, 8);
        uint16_t cw = 0x37f; memcpy(ctx + 0x2e4, &cw, 2);
        uint32_t mx = 0x1f80; memcpy(ctx + 0x2e8, &mx, 4);
        uint64_t fs = (uint64_t)tcb; memcpy(st + %d, &fs, 8);
`, execCtx-execMemBase, execTcb-execMemBase, stTcb)
		}
		d.WriteString(`        fns[i](st);
        fwrite(st, sizeof st, 1, out);
        fwrite(m, 0x` + fmt.Sprintf("%x", execMemSize) + `, 1, out);
    }
    fclose(out);
    return 0;
}
`)
		return d.String()
	}

	write := func(name, s string) string {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
		return p
	}
	write("data.h", data.String())
	build := func(arch, out string, srcs ...string) string {
		bin := filepath.Join(dir, out)
		args := append([]string{"-arch", arch, "-O1", "-w", "-I", dir, "-o", bin}, srcs...)
		if o, err := exec.Command("clang", args...).CombinedOutput(); err != nil {
			t.Fatalf("clang %v: %v\n%s", args, err, o)
		}
		return bin
	}
	hostBin := build("arm64", "host", write("host.c", driver("lifted", true)), write("lifted.s", host.String()))
	x86Bin := build("x86_64", "x86", write("x86.c", driver("native", false)), write("native.s", native.String()))

	var in bytes.Buffer
	for _, c := range cases {
		st := make([]byte, stSize)
		for k, r := range execRegs {
			v, ok := c.regs[r]
			if !ok && r == x86asm.RSP {
				v = execRSP
			}
			binary.LittleEndian.PutUint64(st[stRegs+8*k:], v)
		}
		binary.LittleEndian.PutUint64(st[stFlags:], c.flags|0x202)
		// Block-boundary sense: N = SF, Z = ZF, C = !CF, V = OF.
		var nzcv uint64
		if c.flags&fSF != 0 {
			nzcv |= 1 << 31
		}
		if c.flags&fZF != 0 {
			nzcv |= 1 << 30
		}
		if c.flags&fCF == 0 {
			nzcv |= 1 << 29
		}
		if c.flags&fOF != 0 {
			nzcv |= 1 << 28
		}
		binary.LittleEndian.PutUint64(st[stNzcv:], nzcv)
		in.Write(st)
	}
	inPath := write("in.bin", in.String())
	run := func(out string, cmd ...string) []byte {
		p := filepath.Join(dir, out)
		cmd = append(cmd, inPath, p)
		if o, err := exec.Command(cmd[0], cmd[1:]...).CombinedOutput(); err != nil {
			t.Fatalf("%v: %v\n%s", cmd, err, o)
		}
		b, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	got := run("host.out", hostBin)
	want := run("x86.out", "arch", "-x86_64", x86Bin)
	rec := stSize + execMemSize
	if len(got) != n*rec || len(want) != n*rec {
		t.Fatalf("output sizes: host %d, x86 %d, want %d", len(got), len(want), n*rec)
	}
	for i, c := range cases {
		g, w := got[i*rec:(i+1)*rec], want[i*rec:(i+1)*rec]
		for k, r := range execRegs {
			gv := binary.LittleEndian.Uint64(g[stRegs+8*k:])
			wv := binary.LittleEndian.Uint64(w[stRegs+8*k:])
			if gv != wv {
				t.Errorf("%s: %v = %#x, x86 %#x", c.name, r, gv, wv)
			}
		}
		if !c.noFlags {
			mask := c.cmpFlags
			if mask == 0 {
				mask = readMask
			}
			wf := binary.LittleEndian.Uint64(w[stFlags:])
			for k, bit := range readFlagBits {
				if mask&bit == 0 {
					continue
				}
				gb := g[stOut+k] != 0
				wb := wf&bit != 0
				if gb != wb {
					t.Errorf("%s: flag %s = %v, x86 %v", c.name, map[uint64]string{fOF: "OF", fCF: "CF", fZF: "ZF", fSF: "SF"}[bit], gb, wb)
				}
			}
		}
		gm, wm := g[stSize:], w[stSize:]
		if !bytes.Equal(gm, wm) {
			for off := 0; off < execMemSize; off++ {
				if gm[off] != wm[off] {
					end := off + 16
					if end > execMemSize {
						end = execMemSize
					}
					t.Errorf("%s: memory differs at +%#x: lifted % x, x86 % x", c.name, off, gm[off:end], wm[off:end])
					break
				}
			}
		}
	}
}

// le64 encodes a little-endian u64.
func le64(v uint64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, v)
	return b
}
