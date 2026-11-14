package recompruntime

import (
	"encoding/binary"
	"math"
	"math/rand/v2"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"
)

// TestX87FcosMatchesX86 runs recomp_x87_fcos_impl (core/recomp_x87.c, built as the game build
// compiles the runtime) and the x86 FCOS instruction under Rosetta 2 on the same operands.
//
// C2 (out of range) and the result of every special operand (zeros, NaN, infinities, the
// 2^63 boundary, operands at or above it) must be identical bit for bit. A finite in-range
// result may be one double ulp away: x87 evaluates cos to 64 bits and rounds once, the C
// implementation's libm sin/cos are faithful but not correctly rounded (the 1-ulp x87
// tolerance of the lifter-difftest-asm harness). The argument reduction is checked by operands
// next to multiples of pi/2 up to 2^62, where a reduction with another value of pi than the
// x87's 66-bit one is off by many ulps or by orders of magnitude.
func TestX87FcosMatchesX86(t *testing.T) {
	if runtime.GOOS != "darwin" || runtime.GOARCH != "arm64" {
		t.Skip("needs an arm64 Mac with Rosetta 2 (x86 reference)")
	}
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	if err := exec.Command("arch", "-x86_64", "/usr/bin/true").Run(); err != nil {
		t.Skip("Rosetta 2 not installed")
	}
	special := []uint64{
		0, 1 << 63, // +-0
		0x7ff0000000000000, 0xfff0000000000000, // +-inf: the real indefinite
		0x7ff8000000000000, 0xfff8000000000000, 0x7ff8000000000123, // quiet NaNs pass through
		0x0000000000000001, 0x800fffffffffffff, // denormals
		0x43dfffffffffffff, 0xc3dfffffffffffff, // the largest operands below 2^63
		0x43e0000000000000, 0xc3e0000000000000, // +-2^63: out of range (C2 = 1)
		0x43e0000000000001, 0x7e37e43c8800759c, 0x7fefffffffffffff, // above 2^63
		0x3ff921fb54442d18, 0x400921fb54442d18, // the doubles nearest pi/2 and pi
	}
	var in []float64
	for _, b := range special {
		in = append(in, math.Float64frombits(b))
	}
	r := rand.New(rand.NewPCG(1, 2))
	for range 20000 {
		// Random magnitudes from 2^-70 to 2^63.
		in = append(in, math.Ldexp(r.Float64(), r.IntN(134)-70)*float64(1-2*r.IntN(2)))
	}
	for range 20000 {
		// Next to k * pi/2: the result is the reduction error.
		k := math.Ldexp(float64(r.Uint64()>>11), -r.IntN(53))
		x := k * (math.Pi / 2)
		for range r.IntN(3) {
			x = math.Nextafter(x, math.Inf(1-2*r.IntN(2)))
		}
		in = append(in, x)
	}

	dir := t.TempDir()
	core, err := filepath.Abs("core")
	if err != nil {
		t.Fatal(err)
	}
	write := func(name, s string) string {
		p := filepath.Join(dir, name)
		if err := os.WriteFile(p, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
		return p
	}
	// Both drivers: read doubles from argv[1], write (result, FSW) per operand to argv[2].
	native := write("native.c", `#include <stdint.h>
#include <stdio.h>
int main(int argc, char **argv) {
  FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
  double x;
  while (fread(&x, 8, 1, in) == 1) {
    double r;
    uint16_t sw;
    __asm__ volatile("fninit\n\tfldl %2\n\tfcos\n\tfnstsw %1\n\tfstpl %0" : "=m"(r), "=m"(sw) : "m"(x));
    fwrite(&r, 8, 1, out);
    fwrite(&sw, 2, 1, out);
  }
  return fclose(out);
}
`)
	host := write("host.c", `#include "recomp_context.h"
#include <stdio.h>
static GuestContext ctx;
int main(int argc, char **argv) {
  FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "wb");
  double x;
  for (int top = 0; fread(&x, 8, 1, in) == 1; top = (top + 3) & 7) {
    ctx.fpu_top = top;
    ctx.fpu_stack[top] = x;
    ctx.fpu_sw = 0x0600; /* C1, C2 set: FCOS clears both (or sets C2) */
    recomp_x87_fcos_impl(&ctx);
    fwrite(&ctx.fpu_stack[top], 8, 1, out);
    fwrite(&ctx.fpu_sw, 2, 1, out);
  }
  return fclose(out);
}
`)
	build := func(out string, args ...string) string {
		bin := filepath.Join(dir, out)
		args = append(args, "-o", bin)
		if o, err := exec.Command("clang", args...).CombinedOutput(); err != nil {
			t.Fatalf("clang %v: %v\n%s", args, err, o)
		}
		return bin
	}
	nativeBin := build("native", "-arch", "x86_64", "-O1", native)
	hostBin := build("host", "-arch", "arm64", "-O2", "-ffp-contract=off", "-I", core, host,
		filepath.Join(core, "recomp_x87.c"))

	buf := make([]byte, 8*len(in))
	for i, x := range in {
		binary.LittleEndian.PutUint64(buf[8*i:], math.Float64bits(x))
	}
	inPath := write("in.bin", string(buf))
	run := func(name string, cmd ...string) []byte {
		p := filepath.Join(dir, name)
		cmd = append(cmd, inPath, p)
		if o, err := exec.Command(cmd[0], cmd[1:]...).CombinedOutput(); err != nil {
			t.Fatalf("%v: %v\n%s", cmd, err, o)
		}
		b, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		if len(b) != 10*len(in) {
			t.Fatalf("%s: %d bytes, want %d", name, len(b), 10*len(in))
		}
		return b
	}
	want := run("native.out", "arch", "-x86_64", nativeBin)
	got := run("host.out", hostBin)

	const c1, c2 = 0x0200, 0x0400
	ulp, bad := 0, 0
	for i, x := range in {
		w := binary.LittleEndian.Uint64(want[10*i:])
		g := binary.LittleEndian.Uint64(got[10*i:])
		wsw := binary.LittleEndian.Uint16(want[10*i+8:])
		gsw := binary.LittleEndian.Uint16(got[10*i+8:])
		fail := func(what string) {
			bad++
			if bad <= 20 {
				t.Errorf("fcos(%v = %#016x): %s: got %#016x FSW %#04x, x86 %#016x FSW %#04x",
					x, math.Float64bits(x), what, g, gsw, w, wsw)
			}
		}
		if gsw&c2 != wsw&c2 {
			fail("C2")
			continue
		}
		if gsw&c1 != 0 {
			fail("C1 not cleared")
			continue
		}
		if g == w {
			continue
		}
		if i < len(special) || wsw&c2 != 0 || math.IsNaN(math.Float64frombits(w)) {
			fail("special operand")
			continue
		}
		if d := int64(g - w); g>>63 != w>>63 || d < -1 || d > 1 {
			fail("more than one ulp")
			continue
		}
		ulp++
	}
	t.Logf("%d operands: %d one ulp from the x87 result, %d failures", len(in), ulp, bad)
}
