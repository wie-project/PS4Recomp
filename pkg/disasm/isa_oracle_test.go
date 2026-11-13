package disasm_test

import (
	"bufio"
	"encoding/hex"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
)

// isaOps maps the mnemonics of testdata/gr_isa_sites.txt to the decoder's opcodes.
var isaOps = map[string]x86asm.Op{
	"andn": x86asm.ANDN, "bextr": x86asm.BEXTR, "blsi": x86asm.BLSI, "blsmsk": x86asm.BLSMSK,
	"blsr": x86asm.BLSR, "tzcnt": x86asm.TZCNT, "lzcnt": x86asm.LZCNT, "movbe": x86asm.MOVBE,
}

func argText(inst x86asm.Inst) string {
	var a []string
	for _, arg := range inst.Args {
		if arg == nil {
			break
		}
		a = append(a, arg.String())
	}
	return strings.Join(a, ", ")
}

// liftTraps reports whether the lifter lowers inst to an unsupported-instruction trap.
func liftTraps(t *testing.T, inst x86asm.Inst, addr uint64) bool {
	t.Helper()
	fn := &disasm.Function{EntryAddr: addr, Blocks: map[uint64]*disasm.BasicBlock{}}
	lines, err := lifter.NewArm64Lifter(nil).LiftInstructionToBuf(disasm.Instruction{Address: addr, Inst: inst},
		addr+uint64(inst.Len), fn, nil)
	if err != nil {
		t.Fatalf("lift %v: %v", inst, err)
	}
	for _, ln := range lines {
		if strings.Contains(ln, "_recomp_unsupported_instruction") || strings.Contains(ln, "brk #0") {
			return true
		}
	}
	return false
}

// TestIsaForms decodes every form of the BMI1, LZCNT and MOVBE instructions (operand sizes,
// register and memory operands, REX and VEX register extensions) and the encodings next to
// them that are other instructions: without the F3 prefix 0F BC/BD are BSF/BSR, and of F2 and
// F3 the last one decides (F3 F2 0F BD is BSR); F2 0F 38 F0/F1 is CRC32, not MOVBE. Each
// decoded form must also lift without an unsupported-instruction trap.
func TestIsaForms(t *testing.T) {
	cases := []struct {
		hex  string
		op   x86asm.Op
		args string
	}{
		// TZCNT / LZCNT: 16, 32, 64 bits, register and memory, REX.B, both orders of 66 and F3.
		{"66f30fbcc1", x86asm.TZCNT, "AX, CX"},
		{"f3660fbcc1", x86asm.TZCNT, "AX, CX"},
		{"f30fbcc1", x86asm.TZCNT, "EAX, ECX"},
		{"f3480fbcc1", x86asm.TZCNT, "RAX, RCX"},
		{"f3490fbc07", x86asm.TZCNT, "RAX, [R15]"},
		{"f3410fbcc0", x86asm.TZCNT, "EAX, R8L"},
		{"f30fbc4708", x86asm.TZCNT, "EAX, [RDI+0x8]"},
		{"66f30fbdc1", x86asm.LZCNT, "AX, CX"},
		{"f30fbdc1", x86asm.LZCNT, "EAX, ECX"},
		{"f34c0fbdc7", x86asm.LZCNT, "R8, RDI"},
		{"f3480fbd07", x86asm.LZCNT, "RAX, [RDI]"},
		{"f2f30fbdc1", x86asm.LZCNT, "EAX, ECX"}, // F3 last
		// Not TZCNT/LZCNT: no F3, or F2 last.
		{"0fbcc1", x86asm.BSF, "EAX, ECX"},
		{"480fbdc1", x86asm.BSR, "RAX, RCX"},
		{"660fbdc1", x86asm.BSR, "AX, CX"},
		{"f3f20fbdc1", x86asm.BSR, "EAX, ECX"},
		{"f20fbcc1", x86asm.BSF, "EAX, ECX"},
		// ANDN, BEXTR (VEX.0F38), 32 and 64 bits, register and memory, VEX.R/B and vvvv.
		{"c4e278f2c1", x86asm.ANDN, "EAX, EAX, ECX"},
		{"c4e2f0f2c2", x86asm.ANDN, "RAX, RCX, RDX"},
		{"c462b8f207", x86asm.ANDN, "R8, R8, [RDI]"},
		{"c4e278f7c1", x86asm.BEXTR, "EAX, ECX, EAX"},
		{"c4e2f8f707", x86asm.BEXTR, "RAX, [RDI], RAX"},
		{"c4c2b0f7c0", x86asm.BEXTR, "RAX, R8, R9"},
		// BLSR /1, BLSMSK /2, BLSI /3 (VEX.vvvv is the destination).
		{"c4e278f3cf", x86asm.BLSR, "EAX, EDI"},
		{"c4e2f0f30f", x86asm.BLSR, "RCX, [RDI]"},
		{"c4e278f3d7", x86asm.BLSMSK, "EAX, EDI"},
		{"c4c2b8f3d1", x86asm.BLSMSK, "R8, R9"},
		{"c4e2f8f3df", x86asm.BLSI, "RAX, RDI"},
		{"c4e270f31e", x86asm.BLSI, "ECX, [RSI]"},
		// MOVBE: load F0 and store F1, 16, 32, 64 bits.
		{"660f38f007", x86asm.MOVBE, "AX, [RDI]"},
		{"0f38f007", x86asm.MOVBE, "EAX, [RDI]"},
		{"480f38f04708", x86asm.MOVBE, "RAX, [RDI+0x8]"},
		{"660f38f107", x86asm.MOVBE, "[RDI], AX"},
		{"0f38f10e", x86asm.MOVBE, "[RSI], ECX"},
		{"4c0f38f107", x86asm.MOVBE, "[RDI], R8"},
		// Not MOVBE: F2 0F 38 F0/F1 is CRC32.
		{"f20f38f0c1", x86asm.CRC32, "EAX, CL"},
		{"f2480f38f1c1", x86asm.CRC32, "RAX, RCX"},
	}
	for _, c := range cases {
		b, err := hex.DecodeString(c.hex)
		if err != nil {
			t.Fatal(err)
		}
		inst, err := disasm.Decode(b)
		if err != nil {
			t.Errorf("%s: %v", c.hex, err)
			continue
		}
		if inst.Op != c.op || inst.Len != len(b) || argText(inst) != c.args {
			t.Errorf("%s: decoded %v %s (%d bytes), want %v %s (%d bytes)", c.hex, inst.Op, argText(inst), inst.Len,
				c.op, c.args, len(b))
			continue
		}
		if liftTraps(t, inst, 0x1000) {
			t.Errorf("%s (%v %s): lifted to an unsupported-instruction trap", c.hex, inst.Op, argText(inst))
		}
	}
}

// TestGravityRushIsaSites decodes every BMI1, LZCNT and MOVBE instruction of Gravity Rush
// Remastered's eboot.bin, libc.prx and libSceFios2.prx (testdata/gr_isa_sites.txt, found with
// llvm-objdump by the gravityrush_pc port) and requires the same mnemonic and length, and a
// lowering without an unsupported-instruction trap. PS4_GR_EBOOT is the path of the decrypted
// eboot.bin; the modules are read from sce_module next to it.
func TestGravityRushIsaSites(t *testing.T) {
	eboot := os.Getenv("PS4_GR_EBOOT")
	if eboot == "" {
		t.Skip("PS4_GR_EBOOT is not set (path of Gravity Rush Remastered CUSA01113 eboot.bin); " +
			"TestIsaForms covers the instruction forms without the game")
	}
	f, err := os.Open(filepath.Join("testdata", "gr_isa_sites.txt"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	images := map[string][]byte{}
	image := func(name string) []byte {
		if img, ok := images[name]; ok {
			return img
		}
		path := eboot
		if name != "eboot.bin" {
			path = filepath.Join(filepath.Dir(eboot), "sce_module", name)
		}
		loaded, err := elfloader.LoadELF(path)
		if err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		images[name] = loaded.MemoryImage
		return loaded.MemoryImage
	}
	n, bad := 0, 0
	perOp := map[x86asm.Op]int{}
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		if line == "" || line[0] == '#' {
			continue
		}
		fl := strings.Fields(line)
		if len(fl) != 4 {
			t.Fatalf("bad line %q", line)
		}
		off, err1 := strconv.ParseUint(fl[1], 0, 64)
		length, err2 := strconv.Atoi(fl[2])
		want, ok := isaOps[fl[3]]
		if err1 != nil || err2 != nil || !ok {
			t.Fatalf("bad line %q", line)
		}
		img := image(fl[0])
		if off+uint64(length) > uint64(len(img)) {
			t.Fatalf("%s: %s+%#x beyond the image (%#x bytes)", line, fl[0], off, len(img))
		}
		n++
		inst, err := disasm.Decode(img[off:])
		switch {
		case err != nil:
			t.Errorf("%s %#x: %v", fl[0], off, err)
		case inst.Op != want || inst.Len != length:
			t.Errorf("%s %#x (% x): decoded %v %s (%d bytes), want %s (%d bytes)", fl[0], off, img[off:off+uint64(length)],
				inst.Op, argText(inst), inst.Len, fl[3], length)
		case liftTraps(t, inst, off):
			t.Errorf("%s %#x: %v %s lifted to an unsupported-instruction trap", fl[0], off, inst.Op, argText(inst))
		default:
			perOp[inst.Op]++
			continue
		}
		bad++
	}
	if err := sc.Err(); err != nil {
		t.Fatal(err)
	}
	if n != 1236 {
		t.Errorf("%d sites read, the list has 1236", n)
	}
	t.Logf("%d sites, %d mismatches; decoded: %v", n, bad, perOp)
}
