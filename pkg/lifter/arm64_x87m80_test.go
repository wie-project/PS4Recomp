package lifter

import (
	"bytes"
	"encoding/binary"
	"math"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// FLD m80 and FSTP m80 were lowered as their m32 forms (x86asm reports the m80 operand with
// MemBytes 0): every long double load read a float, every store wrote 4 bytes. TLOU's libc has
// 879 FLD m80 and 599 FSTP m80 sites (vfprintf printed "0000...0" for the memory statistics).
//
// The test runs the real lowerings of `fld tbyte [rax]` and `fstp tbyte [rax]` on the host and
// the instructions themselves on x86-64 under Rosetta 2 over the same inputs (special encodings,
// exponents at the double range limits, random bit patterns), and requires identical results:
// the double that FLD m80 leaves in ST(0) (what x86 FSTP m64 gives, round to nearest) and the
// ten bytes that FSTP m80 writes for a double.
func TestX87M80LoadStoreMatchesX86(t *testing.T) {
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		t.Skip("needs an arm64 Mac (host lowering) with Rosetta 2 (x86 reference)")
	}
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	if err := exec.Command("arch", "-x86_64", "/usr/bin/true").Run(); err != nil {
		t.Skip("Rosetta 2 not installed")
	}
	dir := t.TempDir()

	// Host side: the two lowerings in functions that set up the registers they use. x28 (memory
	// base) is 0, so guest addresses are host addresses; x27 (FS base) points at a block whose
	// +0x70 slot holds the GuestContext pointer, as the TCB does; x19 is RAX. NZCV is seeded from
	// the third argument and returned: the lowerings must keep it (x87 loads and stores leave
	// EFLAGS alone).
	var asm strings.Builder
	fn := func(name string, lines []string) {
		asm.WriteString(".globl _" + name + "\n.p2align 2\n_" + name + ":\n")
		asm.WriteString("    stp x19, x20, [sp, #-48]!\n    stp x27, x28, [sp, #16]\n")
		asm.WriteString("    mov x27, x0\n    mov x19, x1\n    mov x28, #0\n    msr nzcv, x2\n")
		for _, l := range lines {
			asm.WriteString(l + "\n")
		}
		asm.WriteString("    mrs x0, nzcv\n    ldp x27, x28, [sp, #16]\n    ldp x19, x20, [sp], #48\n    ret\n")
	}
	asm.WriteString(".text\n")
	fn("lifted_fld_m80", liftBlock(t, "db28")[0])  // fld tbyte [rax]
	fn("lifted_fstp_m80", liftBlock(t, "db38")[0]) // fstp tbyte [rax]
	for _, l := range strings.Split(asm.String(), "\n") {
		if strings.Contains(l, "_recomp_unsupported") {
			t.Fatalf("m80 form not lifted:\n%s", asm.String())
		}
	}
	hostDriver := `
#include <stdint.h>
#include <stdio.h>
#include <string.h>
uint64_t lifted_fld_m80(void *tcb, const void *m80, uint64_t nzcv);
uint64_t lifted_fstp_m80(void *tcb, void *m80, uint64_t nzcv);
static uint64_t seed(uint32_t i) { return (uint64_t)((i * 0x9u) & 0xf) << 28; }
static uint8_t ctx[0x400] __attribute__((aligned(16)));
static uint8_t tcb[0x100] __attribute__((aligned(16)));
int main(int argc, char **argv) {
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    void *c = ctx; memcpy(tcb + 0x70, &c, 8);
    uint32_t n80, n64; fread(&n80, 4, 1, in); fread(&n64, 4, 1, in);
    for (uint32_t i = 0; i < n80; i++) {
        uint8_t m[16] = {0}; fread(m, 10, 1, in);
        int32_t top = 0; memcpy(ctx + 0x2e0, &top, 4);
        if (lifted_fld_m80(tcb, m, seed(i)) != seed(i)) { fprintf(stderr, "fld: NZCV changed\n"); return 1; }
        memcpy(&top, ctx + 0x2e0, 4);
        fwrite(ctx + 0x2a0 + 8 * (top & 7), 8, 1, out);
    }
    for (uint32_t i = 0; i < n64; i++) {
        double d; fread(&d, 8, 1, in);
        int32_t top = 3; memcpy(ctx + 0x2e0, &top, 4); memcpy(ctx + 0x2a0 + 8 * 3, &d, 8);
        uint8_t m[16]; memset(m, 0xcc, sizeof m);
        if (lifted_fstp_m80(tcb, m, seed(i)) != seed(i)) { fprintf(stderr, "fstp: NZCV changed\n"); return 1; }
        memcpy(&top, ctx + 0x2e0, 4);
        if (top != 4) { fprintf(stderr, "fstp: TOP %d, want 4\n", top); return 1; }
        fwrite(m, 10, 1, out);
    }
    return 0;
}
`
	x86Driver := `
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
    uint32_t n80, n64; fread(&n80, 4, 1, in); fread(&n64, 4, 1, in);
    for (uint32_t i = 0; i < n80; i++) {
        uint8_t m[16] = {0}; fread(m, 10, 1, in);
        double d;
        __asm__ volatile("fninit\n fldt (%1)\n fstpl %0\n fnclex" : "=m"(d) : "r"(m) : "memory");
        fwrite(&d, 8, 1, out);
    }
    for (uint32_t i = 0; i < n64; i++) {
        double d; fread(&d, 8, 1, in);
        uint8_t m[16]; memset(m, 0xcc, sizeof m);
        __asm__ volatile("fninit\n fldl %1\n fstpt (%0)\n fnclex" : : "r"(m), "m"(d) : "memory");
        fwrite(m, 10, 1, out);
    }
    return 0;
}
`
	write := func(name, s string) string {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
		return p
	}
	build := func(arch, out string, srcs ...string) string {
		bin := filepath.Join(dir, out)
		args := append([]string{"-arch", arch, "-O1", "-o", bin}, srcs...)
		if o, err := exec.Command("clang", args...).CombinedOutput(); err != nil {
			t.Fatalf("clang %v: %v\n%s", args, err, o)
		}
		return bin
	}
	hostBin := build("arm64", "host", write("host.c", hostDriver), write("lowering.s", asm.String()))
	x86Bin := build("x86_64", "x86", write("x86.c", x86Driver))

	// Inputs.
	rng := rand.New(rand.NewSource(80))
	var m80s [][10]byte
	add80 := func(sexp uint16, mant uint64) {
		var b [10]byte
		binary.LittleEndian.PutUint64(b[:], mant)
		binary.LittleEndian.PutUint16(b[8:], sexp)
		m80s = append(m80s, b)
	}
	exps := []uint16{0, 1, 2, 0x3ffe, 0x3fff, 0x4000, 0x7ffe, 0x7fff}
	for e := 16383 - 1023 - 70; e <= 16383-1023+3; e++ { // denormal and smallest normal doubles
		exps = append(exps, uint16(e))
	}
	for e := 16383 + 1020; e <= 16383+1026; e++ { // largest doubles and overflow
		exps = append(exps, uint16(e))
	}
	mants := []uint64{0, 1, 1 << 63, 1<<63 | 1, 1<<63 | 0x400, 1<<63 | 0x7ff, 1<<63 | 0x800, 1<<63 | 0xc00,
		^uint64(0), ^uint64(0) >> 1, 1 << 62, 1<<63 | 1<<62, 1<<63 | 1<<62 | 1, 0x7ff, 0x800}
	for _, e := range exps {
		for _, m := range mants {
			add80(e, m)
			add80(e|0x8000, m)
		}
		for i := 0; i < 20; i++ {
			add80(e|uint16(rng.Intn(2))<<15, rng.Uint64()|1<<63)
		}
	}
	for i := 0; i < 20000; i++ {
		add80(uint16(rng.Uint32()), rng.Uint64())
	}
	for i := 0; i < 20000; i++ { // values inside the double range
		e := uint16(16383 - 1100 + rng.Intn(2200))
		add80(e|uint16(rng.Intn(2))<<15, rng.Uint64()|1<<63)
	}
	var f64s []uint64
	for _, v := range []float64{0, 1, 1.5, -2.25, math.Inf(1), math.Inf(-1), math.MaxFloat64, math.SmallestNonzeroFloat64,
		2.2250738585072014e-308, 2.225073858507201e-308} {
		f64s = append(f64s, math.Float64bits(v), math.Float64bits(-v))
	}
	f64s = append(f64s, 0x7ff8000000000000, 0xfff8000000000000, 0x7ff0000000000001, 0x7ff4000000000000, 0x000fffffffffffff)
	for i := 0; i < 20000; i++ {
		f64s = append(f64s, rng.Uint64())
	}

	var in bytes.Buffer
	binary.Write(&in, binary.LittleEndian, uint32(len(m80s)))
	binary.Write(&in, binary.LittleEndian, uint32(len(f64s)))
	for _, b := range m80s {
		in.Write(b[:])
	}
	for _, v := range f64s {
		binary.Write(&in, binary.LittleEndian, v)
	}
	inPath := write("in.bin", in.String())
	run := func(bin, out string, args ...string) []byte {
		p := filepath.Join(dir, out)
		cmd := exec.Command(bin, inPath, p)
		if len(args) > 0 {
			cmd = exec.Command(args[0], append(args[1:], bin, inPath, p)...)
		}
		if o, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("%s: %v\n%s", bin, err, o)
		}
		b, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	got := run(hostBin, "host.out")
	want := run(x86Bin, "x86.out", "arch", "-x86_64")
	if len(got) != len(want) || len(got) != 8*len(m80s)+10*len(f64s) {
		t.Fatalf("output sizes: host %d, x86 %d", len(got), len(want))
	}
	bad := 0
	for i, b := range m80s {
		g := binary.LittleEndian.Uint64(got[8*i:])
		w := binary.LittleEndian.Uint64(want[8*i:])
		if g != w && bad < 20 {
			t.Errorf("fld m80 %04x:%016x: lifted %016x (%g), x86 %016x (%g)", binary.LittleEndian.Uint16(b[8:]),
				binary.LittleEndian.Uint64(b[:]), g, math.Float64frombits(g), w, math.Float64frombits(w))
			bad++
		}
	}
	off := 8 * len(m80s)
	for i, v := range f64s {
		g, w := got[off+10*i:off+10*i+10], want[off+10*i:off+10*i+10]
		if !bytes.Equal(g, w) && bad < 40 {
			t.Errorf("fstp m80 %016x (%g): lifted % x, x86 % x", v, math.Float64frombits(v), g, w)
			bad++
		}
	}
}
