package emitter

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"testing"
)

// contextHeaderOffsets parses the RECOMP_CTX_OFFSET asserts and RECOMP_TCB_CTX_SLOT from
// recomp_context.h: the GuestContext offsets the C compiler checks against the struct.
func contextHeaderOffsets(t *testing.T) (fields map[string]uint64, tcbSlot uint64) {
	t.Helper()
	src, err := os.ReadFile(filepath.Join("..", "runtime", "core", "recomp_context.h"))
	if err != nil {
		t.Fatal(err)
	}
	fields = map[string]uint64{}
	for _, m := range regexp.MustCompile(`(?m)^RECOMP_CTX_OFFSET\(([a-z_0-9]+)(?:\[(\d+)\])?, (0x[0-9a-f]+)\);`).FindAllStringSubmatch(string(src), -1) {
		off, _ := strconv.ParseUint(m[3], 0, 64)
		name := m[1]
		if m[2] != "" {
			name += "[" + m[2] + "]"
		}
		fields[name] = off
	}
	m := regexp.MustCompile(`(?m)^#define RECOMP_TCB_CTX_SLOT (0x[0-9a-f]+)ULL`).FindStringSubmatch(string(src))
	if m == nil {
		t.Fatal("RECOMP_TCB_CTX_SLOT not found in recomp_context.h")
	}
	tcbSlot, _ = strconv.ParseUint(m[1], 0, 64)
	return fields, tcbSlot
}

// TestContextOffsetsMatchHeader checks the GuestContext offsets hardcoded in the trampoline and
// the lifter's assembly against the offsets recomp_context.h asserts for the C struct: every
// offset literal annotated with a field name (`[x0, #0x2e8] // mxcsr`) must equal the header's
// offset for that field, and every TCB access through x27 must use the header's slot.
func TestContextOffsetsMatchHeader(t *testing.T) {
	fields, tcbSlot := contextHeaderOffsets(t)
	for name, off := range map[string]uint64{
		"rax": 0x00, "rdx": 0x10, "rsp": 0x20, "rsi": 0x30, "r8": 0x40, "r10": 0x50, "r12": 0x60, "r14": 0x70,
		"rip": 0x80, "fs_base": 0x88, "cf": 0x98, "zf": 0x99, "sf": 0x9a, "of": 0x9b,
		"xmm[0]": 0xa0, "xmm[2]": 0xc0, "xmm[4]": 0xe0, "xmm[6]": 0x100,
		"fpu_stack": 0x2a0, "fpu_top": 0x2e0, "fpu_cw": 0x2e4, "fpu_sw": 0x2e6, "mxcsr": 0x2e8, "mem_base": 0x330,
	} {
		if got, ok := fields[name]; !ok || got != off {
			t.Errorf("recomp_context.h asserts %s at %#x (present %v), the assembly uses %#x", name, got, ok, off)
		}
	}
	if tcbSlot != 0x70 {
		t.Errorf("RECOMP_TCB_CTX_SLOT = %#x, the assembly uses 0x70", tcbSlot)
	}

	sources := map[string]string{"asm_trampoline.go:trampolineAsm": trampolineAsm}
	lifterFiles, _ := filepath.Glob(filepath.Join("..", "lifter", "*.go"))
	for _, f := range lifterFiles {
		if matched, _ := filepath.Match("*_test.go", filepath.Base(f)); matched {
			continue
		}
		b, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		sources[f] = string(b)
	}
	annotated := regexp.MustCompile(`#(0x[0-9a-f]+)\]?[",]*[ \t]*//[ \t]*(?:[wx]\d+ = &?)?([a-z_0-9]+)`)
	tcb := regexp.MustCompile(`x27, #(0x[0-9a-f]+)`)
	checked := 0
	for name, src := range sources {
		for _, m := range annotated.FindAllStringSubmatch(src, -1) {
			want, ok := fields[m[2]]
			if !ok {
				continue // a comment that does not name a GuestContext field
			}
			got, _ := strconv.ParseUint(m[1], 0, 64)
			if got != want {
				t.Errorf("%s: %q uses %#x for %s, recomp_context.h asserts %#x", name, m[0], got, m[2], want)
			}
			checked++
		}
		for _, m := range tcb.FindAllStringSubmatch(src, -1) {
			if got, _ := strconv.ParseUint(m[1], 0, 64); got != tcbSlot {
				t.Errorf("%s: %q: TCB slot %#x, recomp_context.h has %#x", name, m[0], got, tcbSlot)
			}
			checked++
		}
	}
	if checked < 30 {
		t.Errorf("only %d annotated offsets found; the scanner no longer matches the assembly", checked)
	}
}
