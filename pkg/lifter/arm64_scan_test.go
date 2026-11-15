package lifter

import (
	"regexp"
	"strconv"
	"testing"
)

// The hand-written scanners below replaced the regexp engine on the per-instruction hot path.
// These tests pin them to the patterns they implement.

var (
	refHighVecRe  = regexp.MustCompile(`\b[bhsdqv](1[6-9]|2[0-9]|3[01])\b`)
	refUpperTokRe = regexp.MustCompile(`\b([bhsdqv])U(1[6-9]|2[0-9]|3[01])\b`)
	refScratchRe  = regexp.MustCompile(`\b[wx]([0-7])\b`)
	refScalarRe   = regexp.MustCompile(`^(\s+)(ldr|ldrb|ldrh|ldrsb|ldrsh|ldrsw|str|strb|strh)\s+([wx](?:\d+|zr)),\s*\[x28,\s*(x\d+)\]\s*(//.*)?$`)
)

type span struct {
	start, end, idx int
	letter          byte
}

func scanSpans(s string, upper bool) []span {
	var out []span
	if upper {
		scanUpperTokens(s, func(start, end, idx int, letter byte) {
			out = append(out, span{start, end, idx, letter})
		})
	} else {
		scanVecRegs(s, func(start, end, idx int) {
			out = append(out, span{start, end, idx, 0})
		})
	}
	return out
}

func TestScanVecRegsMatchesRegexp(t *testing.T) {
	lines := []string{
		"    mov v16.16b, v31.16b",
		"    add x0, x1, w7",
		"xv16",
		"v16b",
		"v15 v31",
		"L_fn_0x40: qU17",
		"foo v16, v17 // v18 and v19",
		"b16 h17 s18 d19 q20 v21",
		"v163 v16",
		"V16 v16",
		"v16v17",
		"",
	}
	for _, line := range lines {
		want := refHighVecRe.FindAllStringSubmatchIndex(line, -1)
		got := scanSpans(line, false)
		if len(want) != len(got) {
			t.Fatalf("%q: got %d matches, want %d (%v)", line, len(got), len(want), got)
		}
		for i, m := range want {
			idx, err := strconv.Atoi(line[m[2]:m[3]])
			if err != nil {
				t.Fatalf("%q: reference index: %v", line, err)
			}
			if got[i].start != m[0] || got[i].end != m[1] || got[i].idx != idx {
				t.Fatalf("%q: match %d = %+v, want start=%d end=%d idx=%d", line, i, got[i], m[0], m[1], idx)
			}
		}
	}
}

func TestScanUpperTokensMatchesRegexp(t *testing.T) {
	lines := []string{
		"    stp qU16, qU17, [sp, #-32]!",
		"    mov vU31.d[0], v30.d[0]",
		"xU16",
		"vU16b",
		"vU15 vU31",
		"foo vU16 // vU17",
		"bU16 hU17 sU18 dU19 qU20 vU21",
		"v16 vU17",
		"",
	}
	for _, line := range lines {
		want := refUpperTokRe.FindAllStringSubmatchIndex(line, -1)
		got := scanSpans(line, true)
		if len(want) != len(got) {
			t.Fatalf("%q: got %d matches, want %d (%v)", line, len(got), len(want), got)
		}
		for i, m := range want {
			idx, err := strconv.Atoi(line[m[4]:m[5]])
			if err != nil {
				t.Fatalf("%q: reference index: %v", line, err)
			}
			if got[i].start != m[0] || got[i].end != m[1] || got[i].idx != idx || got[i].letter != line[m[2]] {
				t.Fatalf("%q: match %d = %+v, want start=%d end=%d idx=%d letter=%c",
					line, i, got[i], m[0], m[1], idx, line[m[2]])
			}
		}
	}
}

func TestMarkScratchUseMatchesRegexp(t *testing.T) {
	lines := []string{
		"    add x7, x1, w0",
		"    ldr x16, [x28, x70] // x0",
		"    mov x0, x7",
		"x00 w07 x0- w7, x8",
		"    b _fn_0x40",
		"",
	}
	for _, line := range lines {
		var want, got [8]bool
		for _, m := range refScratchRe.FindAllStringSubmatch(line, -1) {
			n, _ := strconv.Atoi(m[1])
			want[n] = true
		}
		markScratchUse(line, &got)
		if got != want {
			t.Fatalf("%q: got %v, want %v", line, got, want)
		}
	}
}

func TestParseScalarAccessMatchesRegexp(t *testing.T) {
	lines := []string{
		"    ldr w17, [x28, x16]",
		"    ldr x0, [x28, x26]",
		"    ldrsh x0, [x28, x16]",
		"    strb w17, [x28, x16] // comment",
		"    ldr w17,[x28,x16]",
		"    ldr wzr, [x28, x16]",
		"    ldr xzr, [x28, x16]",
		"\tldr w17, [x28, x16]",
		"    str w17, [x28, x16] ",
		"    ldr w17, [x28, x16]// tight",
		"    ldr w17, [x28, x16",
		"    ldr w17, [x29, x16]",
		"    ldr w17, [x28, x1x]",
		"    ldr w17 , [x28, x16]",
		"    ldur w17, [x28, x16]",
		"ldr w17, [x28, x16]",
		"    ldr w17, [x28, x16] extra",
		"    ldr q0, [x28, x16]",
		"",
	}
	for _, line := range lines {
		m := refScalarRe.FindStringSubmatch(line)
		got, ok := parseScalarAccess(line)
		if m == nil {
			if ok {
				t.Fatalf("%q: parsed %+v, reference does not match", line, got)
			}
			continue
		}
		if !ok {
			t.Fatalf("%q: not parsed, reference gives %q", line, m)
		}
		if got.indent != m[1] || got.op != m[2] || got.data != m[3] || got.addr != m[4] {
			t.Fatalf("%q: got %+v, want indent=%q op=%q data=%q addr=%q", line, got, m[1], m[2], m[3], m[4])
		}
	}
}
