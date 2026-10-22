// lifter-difftest-asm: differential testing of the x86-64 -> ARM64 assembly lifter
// (lifter.Arm64Lifter) against real x86 semantics.
//
// It samples instruction encodings from a guest image (addresses listed in a disassembly
// listing), keeps one representative per (opcode, operand shape, immediate, register aliasing)
// signature, and generates two programs that execute every sampled instruction from identical
// pseudo-random register, flag and memory states:
//
//   - native_tests.s: the raw instruction bytes wrapped in x86-64 load/store stubs (Rosetta 2)
//   - asm_tests.s:    the Arm64Lifter's translation of the same instruction, wrapped in an arm64
//     prologue that loads the pinned guest registers (x19-x25, x8-x15, x26 = RSP, x27 = FS,
//     x28 = mem_base = 0), v0-v31 (XMM low halves and YMM upper halves) and NZCV from the state
//     block, and an epilogue that stores them back.
//
// Flags are read back the way lifted code consumes them: the epilogue lifts SETB/SETE/SETS/SETO/
// SETP AL with the same lifter instance, so the CF polarity tracking (lastFlagOp) and the PF
// mapping are exactly those a following Jcc/SETcc/CMOVcc would use.
//
// Memory operands are redirected into a scratch area mapped at the same fixed address in both
// programs. run.sh/harness/compare.py compare the final states.
//
// Usage: lifter-difftest-asm -image guest_image.bin -dis listing.txt -out DIR [-max-per-op N] [-tso=false]
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/lifter"

	"golang.org/x/arch/x86/x86asm"
)

const (
	scratchBase = 0x800000000
	scratchSize = 0x100000
	memTarget   = scratchBase + 0x80000
	indexValue  = 0x40
)

// GPR index in GuestContext order.
var gprIndex = map[x86asm.Reg]int{}

func init() {
	fam := [][]x86asm.Reg{
		{x86asm.RAX, x86asm.EAX, x86asm.AX, x86asm.AL, x86asm.AH},
		{x86asm.RCX, x86asm.ECX, x86asm.CX, x86asm.CL, x86asm.CH},
		{x86asm.RDX, x86asm.EDX, x86asm.DX, x86asm.DL, x86asm.DH},
		{x86asm.RBX, x86asm.EBX, x86asm.BX, x86asm.BL, x86asm.BH},
		{x86asm.RSP, x86asm.ESP, x86asm.SP, x86asm.SPB},
		{x86asm.RBP, x86asm.EBP, x86asm.BP, x86asm.BPB},
		{x86asm.RSI, x86asm.ESI, x86asm.SI, x86asm.SIB},
		{x86asm.RDI, x86asm.EDI, x86asm.DI, x86asm.DIB},
		{x86asm.R8, x86asm.R8L, x86asm.R8W, x86asm.R8B},
		{x86asm.R9, x86asm.R9L, x86asm.R9W, x86asm.R9B},
		{x86asm.R10, x86asm.R10L, x86asm.R10W, x86asm.R10B},
		{x86asm.R11, x86asm.R11L, x86asm.R11W, x86asm.R11B},
		{x86asm.R12, x86asm.R12L, x86asm.R12W, x86asm.R12B},
		{x86asm.R13, x86asm.R13L, x86asm.R13W, x86asm.R13B},
		{x86asm.R14, x86asm.R14L, x86asm.R14W, x86asm.R14B},
		{x86asm.R15, x86asm.R15L, x86asm.R15W, x86asm.R15B},
	}
	for i, f := range fam {
		for _, r := range f {
			gprIndex[r] = i
		}
	}
}

func regClass(r x86asm.Reg) string {
	switch {
	case r >= x86asm.AL && r <= x86asm.R15B:
		return "r8"
	case r >= x86asm.AX && r <= x86asm.R15W:
		return "r16"
	case r >= x86asm.EAX && r <= x86asm.R15L:
		return "r32"
	case r >= x86asm.RAX && r <= x86asm.R15:
		return "r64"
	case r >= x86asm.X0 && r <= x86asm.X15:
		return "x"
	case r >= x86asm.Y0 && r <= x86asm.Y15:
		return "y"
	case r >= x86asm.M0 && r <= x86asm.M7:
		return "mm"
	case r >= x86asm.F0 && r <= x86asm.F7:
		return "st"
	}
	return "other:" + r.String()
}

// entryMode is the lifter state a test instruction is lifted in.
type entryMode int

const (
	// modeBlockStart: start of a basic block; the lifter tracks C = !CF (carrySub) and NZCV
	// holds the input flags that way.
	modeBlockStart entryMode = iota
	// modeAfterAdd: lifted right after an ADD, so the lifter tracks C = CF (carryAdd) and NZCV
	// holds C = CF.
	modeAfterAdd
	// modeAfterLogic: lifted right after a TEST, so the lifter tracks C = 0 = CF (carryClear);
	// the input flags have CF = OF = 0 and NZCV holds C = V = 0.
	modeAfterLogic
	// modeNoFlags: the instruction's flags are dead (disasm sets SkipFlags); flags not compared.
	modeNoFlags
	// modeDeadCO: CF and OF are dead after the instruction (disasm sets CarryDead and
	// OverflowDead); CF and OF are not compared.
	modeDeadCO
	// modeCrossBlock: the instruction ends a block whose successor reads the flags: the lifter
	// emits its block-end code (EndBlock with CF live) and the flags are read back as the next
	// block would, from the block-boundary carry sense (C = !CF).
	modeCrossBlock
	// modeFpCmp: the input flags are those of an FP compare (ZF/PF/CF in {000, 001, 100, 111},
	// OF = SF = AF = 0) and NZCV holds them the way the lifter's COMIS lowering leaves them
	// (Z = ZF, C = !CF, V = PF, N = 0). The lifter models PF only there, so the PF readers
	// (SETP/SETNP/CMOVP/CMOVNP) are tested in this mode instead of modeBlockStart.
	modeFpCmp
)

// Bits of a test's regset mask (word 0 of nt_regsets/lt_regsets) that constrain the input
// flags dt_make_state generates: FP-compare results (modeFpCmp), CF = OF = 0 (modeAfterLogic).
const (
	regsetFpCmp = 1 << 63
	regsetLogic = 1 << 62
	// regsetMxcsr: the memory operand holds a valid MXCSR value (default exception masks,
	// random rounding mode, FZ, DAZ and exception flags). A random value would have reserved
	// bits set, which x86 rejects with #GP and Rosetta 2 silently masks.
	regsetMxcsr = 1 << 61
	// regsetShiftCL: a shift or rotate by CL; trials 0 and 1 run with CL = 0 and CL = 1.
	regsetShiftCL = 1 << 60
	// regsetFcw: the memory operand holds a valid x87 control word (for FLDCW).
	regsetFcw = 1 << 59
	// regsetPopf: [stackTop] holds a valid RFLAGS image for POPFQ (arithmetic flags random,
	// IF and bit 1 set, TF/DF/AC clear).
	regsetPopf = 1 << 58
	// regsetX87Full: the instruction names ST(7); all eight x87 registers hold values (by
	// default ST(7) is empty so that a push does not overflow).
	regsetX87Full = 1 << 57
	// regsetCasEqual: CMPXCHG/CMPXCHG16B; in trials 4-7 the memory operand holds RAX (and RDX
	// at +8), so that the compare succeeds and the store path runs.
	regsetCasEqual = 1 << 56
	// regsetCas8b: CMPXCHG8B; in trials 4-7 the memory operand holds EDX:EAX.
	regsetCas8b = 1 << 55
)

// readsPF reports whether the instruction's result depends on PF.
func readsPF(op x86asm.Op) bool {
	switch op {
	case x86asm.SETP, x86asm.SETNP, x86asm.CMOVP, x86asm.CMOVNP, x86asm.FCMOVU, x86asm.FCMOVNU:
		return true
	}
	return false
}

type test struct {
	op      x86asm.Op
	addr    uint64
	bytes   []byte
	text    string
	sig     string
	setRegs map[int]uint64 // GPR overrides for memory addressing
	lifted  []string
	readout []string // lifted SETcc readout sequence
	mode    entryMode
	flags   string // flag rule passed to compare.py
	vregs   string // vector registers named by the instruction (comma-separated indexes)
	vdest   string // vector register the instruction writes ("-" if none)
	clShift bool   // shift or rotate by CL (CL = 0 and 1 are forced in two trials)
	stackOp bool   // PUSHFQ/POPFQ: runs on a stack in the compared memory window (RSP = stackTop)
}

// stackTop is RSP for PUSHFQ/POPFQ tests: PUSHFQ writes [stackTop-8, stackTop) and POPFQ reads
// [stackTop, stackTop+8), both inside the compared memory window.
const stackTop = memTarget + 64

// skipClass returns the filtered class of an opcode the harness does not test, or "". Every
// filtered listing site is counted per class in skipped.txt.
func skipClass(inst x86asm.Inst) string {
	op := inst.Op
	s := op.String()
	switch op {
	case x86asm.CALL, x86asm.RET, x86asm.JMP:
		return "control flow: CALL/RET/JMP/Jcc/JrCXZ/LOOP"
	case x86asm.PUSH, x86asm.POP, x86asm.LEAVE, x86asm.ENTER:
		return "stack: PUSH/POP/LEAVE/ENTER (the native stub keeps its frame in RSP)"
	case x86asm.DIV, x86asm.IDIV:
		return "DIV/IDIV (random operands fault: #DE)"
	case x86asm.VZEROUPPER, x86asm.VZEROALL:
		return "VZEROUPPER/VZEROALL"
	case x86asm.FXSAVE, x86asm.FXRSTOR, x86asm.FXSAVE64, x86asm.FXRSTOR64, x86asm.XSAVE, x86asm.XRSTOR,
		x86asm.XSAVE64, x86asm.XRSTOR64:
		return "FXSAVE/FXRSTOR/XSAVE/XRSTOR"
	case x86asm.EMMS:
		return "MMX (EMMS)"
	case x86asm.INT, x86asm.UD2, x86asm.HLT, x86asm.CPUID, x86asm.RDTSC, x86asm.RDTSCP, x86asm.XGETBV,
		x86asm.SYSCALL, x86asm.RDRAND, x86asm.PAUSE:
		return "system and timing: INT/UD2/HLT/CPUID/RDTSC(P)/XGETBV/SYSCALL/RDRAND/PAUSE"
	case x86asm.LFENCE, x86asm.MFENCE, x86asm.SFENCE:
		return "fences and prefetches"
	case x86asm.MOVSB, x86asm.MOVSW, x86asm.MOVSD, x86asm.MOVSQ, x86asm.STOSB, x86asm.STOSW, x86asm.STOSD,
		x86asm.STOSQ, x86asm.LODSB, x86asm.LODSW, x86asm.LODSD, x86asm.LODSQ, x86asm.SCASB, x86asm.SCASW,
		x86asm.SCASD, x86asm.SCASQ, x86asm.CMPSB, x86asm.CMPSW, x86asm.CMPSD, x86asm.CMPSQ, x86asm.INSB,
		x86asm.INSW, x86asm.INSD, x86asm.OUTSB, x86asm.OUTSW, x86asm.OUTSD, x86asm.XLATB:
		return "string instructions (Task 6)"
	case x86asm.FNSTENV, x86asm.FLDENV, x86asm.FNSAVE, x86asm.FRSTOR:
		return "x87 environment: tag word, FIP/FDP and exception flags are not modeled (Task 5)"
	case x86asm.FBLD, x86asm.FBSTP:
		return "x87 m80 and BCD forms (Task 5)"
	}
	if strings.HasPrefix(s, "J") || strings.HasPrefix(s, "LOOP") {
		return "control flow: CALL/RET/JMP/Jcc/JrCXZ/LOOP"
	}
	if strings.HasPrefix(s, "PREFETCH") {
		return "fences and prefetches"
	}
	// FLD m80 / FSTP m80 (DB /5, DB /7) are lifted (pkg/lifter x87LoadM80/x87StoreM80) and tested.
	m80LoadStore := (op == x86asm.FLD || op == x86asm.FSTP) && inst.Opcode>>24 == 0xdb
	if strings.HasPrefix(s, "F") && (inst.MemBytes == 10 || inst.MemBytes == 0) && !m80LoadStore {
		for _, a := range inst.Args {
			if _, ok := a.(x86asm.Mem); ok { // x86asm reports m80 operands with MemBytes 0
				return "x87 m80 and BCD forms (Task 5)"
			}
		}
	}
	return ""
}

// shiftByCL reports shifts and rotates whose count is CL.
func shiftByCL(inst x86asm.Inst) bool {
	switch inst.Op {
	case x86asm.SHL, x86asm.SHR, x86asm.SAR, x86asm.ROL, x86asm.ROR, x86asm.RCL, x86asm.RCR,
		x86asm.SHLD, x86asm.SHRD:
		for _, a := range inst.Args[1:] {
			if a == x86asm.CL {
				return true
			}
		}
	}
	return false
}

// readsFlags reports whether the x86 instruction consumes or preserves CF in a way that depends
// on the lifter's carry sense. Such instructions are also tested after an ADD and after a TEST,
// where the lifter tracks CF in the other two senses.
func readsFlags(inst x86asm.Inst) bool {
	op := inst.Op
	switch op {
	case x86asm.ADC, x86asm.SBB, x86asm.RCL, x86asm.RCR, x86asm.CMC, x86asm.LAHF, x86asm.PUSHFQ,
		x86asm.INC, x86asm.DEC, x86asm.SAHF,
		x86asm.FCMOVB, x86asm.FCMOVNB, x86asm.FCMOVE, x86asm.FCMOVNE, x86asm.FCMOVBE, x86asm.FCMOVNBE,
		x86asm.FCMOVU, x86asm.FCMOVNU:
		return true
	case x86asm.SHL, x86asm.SHR, x86asm.SAR, x86asm.ROL, x86asm.ROR, x86asm.SHLD, x86asm.SHRD:
		// A count of 0 keeps the flags.
		for _, a := range inst.Args[1:] {
			if a == x86asm.CL {
				return true
			}
		}
		return false
	}
	s := op.String()
	return strings.HasPrefix(s, "SET") || strings.HasPrefix(s, "CMOV")
}

// flagRule returns the rule compare.py uses to decide which of CF/ZF/SF/OF/PF to compare.
// Letters list the flags compared; "-" means none. Shift and rotate forms are written as
// "shift:OP" and resolved per trial from the masked count (count 0 leaves all flags unchanged).
// AF is never compared: the lifter does not model it (LAHF/PUSHFQ store 0). PF is compared only
// for the FP compares, the one place the lifter models it (as ARM V, read by JP/SETP/CMOVP).
// Flags an instruction leaves unchanged are compared (they must survive the lowering).
func flagRule(inst x86asm.Inst) string {
	op := inst.Op
	switch op {
	case x86asm.COMISS, x86asm.UCOMISS, x86asm.COMISD, x86asm.UCOMISD,
		x86asm.VCOMISS, x86asm.VUCOMISS, x86asm.VCOMISD, x86asm.VUCOMISD,
		x86asm.FCOMI, x86asm.FCOMIP, x86asm.FUCOMI, x86asm.FUCOMIP:
		// OF (= 0) is not compared: after an FP compare the lifter keeps PF in ARM V, the
		// register that holds OF everywhere else, so JO/SETO there read PF (model limit).
		return "CZSP"
	case x86asm.PTEST, x86asm.VPTEST, x86asm.VTESTPS, x86asm.VTESTPD:
		// x86 clears OF/SF/AF/PF; Rosetta 2 leaves them unchanged (checked with a native test),
		// so the reference is only valid for CF and ZF.
		return "CZ"
	case x86asm.BSF, x86asm.BSR:
		return "Z" // CF/OF/SF undefined
	case x86asm.TZCNT, x86asm.LZCNT:
		return "CZ" // OF/SF undefined
	case x86asm.BEXTR:
		return "CZO" // SF undefined
	case x86asm.BT, x86asm.BTS, x86asm.BTR, x86asm.BTC:
		return "CZ" // OF/SF undefined, ZF unchanged
	case x86asm.IMUL, x86asm.MUL:
		return "CO" // ZF/SF undefined
	case x86asm.SHL, x86asm.SHR, x86asm.SAR, x86asm.ROL, x86asm.ROR, x86asm.RCL, x86asm.RCR,
		x86asm.SHLD, x86asm.SHRD:
		// shift:OP:SIZE:COUNT, COUNT = immediate or "cl"
		size := inst.DataSize
		if r, ok := inst.Args[0].(x86asm.Reg); ok {
			switch regClass(r) {
			case "r8":
				size = 8
			case "r16":
				size = 16
			case "r32":
				size = 32
			case "r64":
				size = 64
			}
		} else if inst.MemBytes > 0 {
			size = inst.MemBytes * 8
		}
		count := "1"
		for _, a := range inst.Args[1:] {
			if a == nil {
				break
			}
			if v, ok := a.(x86asm.Imm); ok {
				count = strconv.FormatUint(uint64(v), 10)
			} else if a == x86asm.CL {
				count = "cl"
			}
		}
		return fmt.Sprintf("shift:%s:%d:%s", op, size, count)
	}
	return "CZSO"
}

// vectorDest returns the index of the XMM/YMM register the instruction writes, or "-".
func vectorDest(inst x86asm.Inst) string {
	switch inst.Op {
	case x86asm.COMISS, x86asm.UCOMISS, x86asm.COMISD, x86asm.UCOMISD, x86asm.VCOMISS, x86asm.VUCOMISS,
		x86asm.VCOMISD, x86asm.VUCOMISD, x86asm.PTEST, x86asm.VPTEST, x86asm.VTESTPS, x86asm.VTESTPD:
		return "-"
	}
	if r, ok := inst.Args[0].(x86asm.Reg); ok {
		if r >= x86asm.X0 && r <= x86asm.X15 {
			return strconv.Itoa(int(r - x86asm.X0))
		}
		if r >= x86asm.Y0 && r <= x86asm.Y15 {
			return strconv.Itoa(int(r - x86asm.Y0))
		}
	}
	return "-"
}

// vectorRegs lists the XMM/YMM register indexes the instruction names explicitly.
func vectorRegs(inst x86asm.Inst) string {
	var out []string
	seen := map[int]bool{}
	for _, a := range inst.Args {
		if a == nil {
			break
		}
		if r, ok := a.(x86asm.Reg); ok {
			idx := -1
			if r >= x86asm.X0 && r <= x86asm.X15 {
				idx = int(r - x86asm.X0)
			} else if r >= x86asm.Y0 && r <= x86asm.Y15 {
				idx = int(r - x86asm.Y0)
			}
			if idx >= 0 && !seen[idx] {
				seen[idx] = true
				out = append(out, strconv.Itoa(idx))
			}
		}
	}
	if len(out) == 0 {
		return "-"
	}
	return strings.Join(out, ",")
}

var (
	branchRe = regexp.MustCompile(`^(b|bl|b\.[a-z]+|cbz|cbnz|tbz|tbnz)\s+(?:[wx]\d+,\s*)?(?:#\d+,\s*)?([A-Za-z_.$][\w.$]*)\s*(?://.*)?$`)
	labelRe  = regexp.MustCompile(`^\s*([A-Za-z_.$][\w.$]*):`)
)

// selfContained reports whether the lifted lines can run standalone: no calls into the runtime,
// no branches out of the instruction, no unsupported-instruction stubs. It returns a reason
// when they cannot.
func selfContained(lines []string) (bool, string) {
	defined := map[string]bool{}
	for _, ln := range lines {
		if m := labelRe.FindStringSubmatch(ln); m != nil {
			defined[m[1]] = true
		}
	}
	for _, ln := range lines {
		t := strings.TrimSpace(ln)
		if strings.HasPrefix(t, "//") || t == "" {
			continue
		}
		if strings.Contains(t, "UNSUPPORTED") {
			return false, "unsupported"
		}
		f := strings.Fields(t)
		switch f[0] {
		case "brk":
			return false, "brk"
		case "blr", "br", "ret", "svc":
			return false, "control:" + f[0]
		}
		if m := branchRe.FindStringSubmatch(t); m != nil {
			if m[1] == "bl" {
				return false, "call:" + m[2]
			}
			if !defined[m[2]] {
				return false, "branch:" + m[2]
			}
		}
	}
	return true, ""
}

// regArgsOK reports whether the register operands of a (re-encoded) instruction can be tested:
// no RSP (the stubs keep their frame there), no RIP, no x87/MMX/segment registers. The
// synthesized VEX aliasing variants can turn a GPR operand into RSP (vmovq rsp, xmm4), which
// would leave the native stub without a stack.
func regArgsOK(inst x86asm.Inst) bool {
	for _, a := range inst.Args {
		if a == nil {
			break
		}
		if r, ok := a.(x86asm.Reg); ok {
			if idx, ok := gprIndex[r]; ok && idx == 4 {
				return false
			}
			cl := regClass(r)
			if r == x86asm.RIP || strings.HasPrefix(cl, "other") || cl == "mm" {
				return false
			}
		}
	}
	return true
}

type skipInfo struct {
	count   int      // distinct signatures
	sites   int      // listing sites of those signatures
	keys    []string // the signatures
	example string
}

type classInfo struct {
	sites   int
	ops     map[string]int
	example string
}

func main() {
	imgPath := flag.String("image", "", "guest memory image (file offset == guest address)")
	disPath := flag.String("dis", "", "disassembly listing (lines 'ADDR: ...', ADDR in hex, optional 0x)")
	outDir := flag.String("out", "difftest", "output directory")
	maxPerOp := flag.Int("max-per-op", 40, "max distinct signatures per opcode")
	tso := flag.Bool("tso", true, "lift with x86 memory ordering (acquire/release for non-stack memory), as ps4-recomp -tso")
	flag.Parse()

	img, err := os.ReadFile(*imgPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	f, err := os.Open(*disPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer f.Close()

	l := lifter.NewArm64Lifter(nil)
	l.SetTSO(*tso)
	// UsesYmm: lift in the mode that keeps every YMM upper half intact (the strict superset).
	fn := &disasm.Function{EntryAddr: 0x1000, Blocks: make(map[uint64]*disasm.BasicBlock), UsesYmm: true}
	lift := func(inst x86asm.Inst, addr uint64, mode entryMode) ([]string, []string, error) {
		l.ResetFlags()
		var pre x86asm.Inst
		switch mode {
		case modeAfterAdd:
			pre = x86asm.Inst{Op: x86asm.ADD, Args: x86asm.Args{x86asm.RAX, x86asm.RBX}, Len: 3, DataSize: 64}
		case modeAfterLogic:
			pre = x86asm.Inst{Op: x86asm.TEST, Args: x86asm.Args{x86asm.RAX, x86asm.RBX}, Len: 3, DataSize: 64}
		}
		if pre.Op != 0 {
			// Lift (and discard) it so the lifter tracks the carry sense it leaves.
			if _, err := l.LiftInstructionToBuf(disasm.Instruction{Address: addr, Inst: pre}, addr+3, fn, nil); err != nil {
				return nil, nil, err
			}
		}
		dead := mode == modeDeadCO
		dInst := disasm.Instruction{Address: addr, Inst: inst, SkipFlags: mode == modeNoFlags,
			CarryDead: dead, OverflowDead: dead}
		lines, err := l.LiftInstructionToBuf(dInst, addr+uint64(inst.Len), fn, nil)
		if err != nil {
			return nil, nil, err
		}
		if mode == modeCrossBlock {
			lines = append(lines, l.EndBlock(dInst)...)
			l.ResetFlags()
		}
		var readout []string
		for k, op := range readoutOps {
			set := x86asm.Inst{Op: op, Args: x86asm.Args{x86asm.AL}, Len: 3, DataSize: 8}
			r, err := l.LiftInstructionToBuf(disasm.Instruction{Address: addr, Inst: set}, addr+3, fn, nil)
			if err != nil {
				return nil, nil, err
			}
			readout = append(readout, r...)
			readout = append(readout, fmt.Sprintf("    strb w19, [sp, #%d]", frameFlags+k))
		}
		return lines, readout, nil
	}

	seen := map[string]bool{}
	perOp := map[x86asm.Op]int{}
	skipped := map[string]*skipInfo{}
	noteSkip := func(reason string, inst x86asm.Inst, addr uint64, sigKey string) {
		key := inst.Op.String() + "\t" + reason
		s := skipped[key]
		if s == nil {
			s = &skipInfo{example: fmt.Sprintf("0x%x %s", addr, safeText(inst, addr))}
			skipped[key] = s
		}
		s.count++
		s.keys = append(s.keys, sigKey)
	}
	var tests []*test
	add := func(in x86asm.Inst, b []byte, addr uint64, key string, setRegs map[int]uint64) {
		base := test{op: in.Op, addr: addr, bytes: b, text: safeText(in, addr), sig: key, setRegs: setRegs,
			flags: flagRule(in), vregs: vectorRegs(in), vdest: vectorDest(in), clShift: shiftByCL(in),
			stackOp: in.Op == x86asm.PUSHFQ || in.Op == x86asm.POPFQ}
		ln, ro, err := lift(in, addr, modeBlockStart)
		if err != nil {
			return
		}
		if ok, _ := selfContained(ln); !ok {
			return
		}
		t := base
		t.lifted, t.readout, t.mode = ln, ro, modeBlockStart
		if readsPF(in.Op) {
			// OF = 0 in an FP-compare state while ARM V holds PF: OF is not compared.
			t.mode, t.flags = modeFpCmp, "CZSP"
			t.sig += " [fpcmp]"
		}
		tests = append(tests, &t)
		variant := func(mode entryMode, tag, rule string, needDiff bool) {
			lnV, roV, err := lift(in, addr, mode)
			if err != nil {
				return
			}
			if ok, _ := selfContained(lnV); !ok {
				return
			}
			if needDiff && strings.Join(lnV[1:], "\n") == strings.Join(ln[1:], "\n") {
				return // same code as the block-start test
			}
			tv := base
			tv.sig += " [" + tag + "]"
			tv.lifted, tv.readout, tv.mode = lnV, roV, mode
			if rule != "" {
				tv.flags = rule
			}
			if mode == modeNoFlags {
				tv.readout = nil
			}
			tests = append(tests, &tv)
		}
		if readsFlags(in) && !readsPF(in.Op) {
			variant(modeAfterAdd, "after-add", "", false)
			variant(modeAfterLogic, "after-logic", "", false)
		}
		variant(modeNoFlags, "noflags", "-", true)
		variant(modeDeadCO, "dead-cf-of", "deadco:"+base.flags, true)
		if !readsPF(in.Op) {
			variant(modeCrossBlock, "cross-block", "", true)
		}
	}

	// Filtered listing sites per class, and listing sites per signature (sites of the sampled
	// signatures that are not tested are reported with the reason).
	classes := map[string]*classInfo{}
	filter := func(class string, inst x86asm.Inst, addr uint64) {
		c := classes[class]
		if c == nil {
			c = &classInfo{ops: map[string]int{}, example: fmt.Sprintf("0x%x %s", addr, safeText(inst, addr))}
			classes[class] = c
		}
		c.sites++
		c.ops[inst.Op.String()]++
	}
	siteCount := map[string]int{}
	sampledOut := 0
	// process samples one instruction encoding (a listing site, or a synthesized one: force skips
	// the per-opcode limit).
	process := func(inst x86asm.Inst, code []byte, addr uint64, force bool) {
		switch code[0] {
		case 0x62:
			filter("prefix: EVEX", inst, addr)
			return
		case 0x64, 0x65:
			filter("prefix: FS/GS segment", inst, addr)
			return
		}
		if c := skipClass(inst); c != "" {
			filter(c, inst, addr)
			return
		}
		// operand signature; RIP/RSP use and other untestable operands are filtered
		var sig []string
		bad := ""
		var mem *x86asm.Mem
		for _, a := range inst.Args {
			if a == nil {
				break
			}
			switch v := a.(type) {
			case x86asm.Reg:
				cl := regClass(v)
				if idx, ok := gprIndex[v]; ok && idx == 4 {
					bad = "register operand: RSP (the native stub keeps its frame there)"
				} else if v == x86asm.RIP {
					bad = "register operand: RIP"
				} else if cl == "mm" {
					bad = "MMX register operand"
				} else if strings.HasPrefix(cl, "other") {
					bad = "register operand: segment/control/other"
				}
				sig = append(sig, cl)
			case x86asm.Mem:
				switch {
				case v.Base == x86asm.RIP:
					bad = "memory: RIP-relative"
				case v.Segment != 0:
					bad = "memory: segment override"
				case v.Base == x86asm.RSP || v.Base == x86asm.ESP:
					bad = "memory: RSP base (the native stub keeps its frame there)"
				case v.Base == 0 && v.Index == 0 && v.Scale == 0:
					// x86asm's VEX decoder reports RIP-relative operands without a base register.
					bad = "memory: RIP-relative"
				case v.Base == 0 && v.Index == 0:
					bad = "memory: absolute address"
				case v.Index >= x86asm.X0 && v.Index <= x86asm.Y15:
					bad = "memory: VSIB (gathers)"
				}
				vv := v
				mem = &vv
				sig = append(sig, fmt.Sprintf("m%d", inst.MemBytes))
			case x86asm.Imm:
				sig = append(sig, fmt.Sprintf("i%x", uint64(v)))
			case x86asm.Rel:
				bad = "branch target operand"
			default:
				bad = "other operand type"
			}
			if bad != "" {
				break
			}
		}
		if bad != "" {
			filter(bad, inst, addr)
			return
		}
		if mem != nil && mem.Base >= x86asm.EAX && mem.Base <= x86asm.R15L {
			filter("memory: 32-bit address (addr32)", inst, addr)
			return
		}
		if mem != nil && mem.Base == 0 {
			filter("memory: index without base", inst, addr)
			return
		}
		// register aliasing pattern (dest == src2 etc. exercises copy-order bugs)
		var regs []x86asm.Reg
		alias := ""
		for _, a := range inst.Args {
			if r, ok := a.(x86asm.Reg); ok {
				for k, pr := range regs {
					if pr == r || (gprIndex[pr] == gprIndex[r] && regClass(pr)[0] == 'r' && regClass(r)[0] == 'r' && gprIndex[r] != 0) {
						alias += fmt.Sprintf("=%d%d", k, len(regs))
					}
				}
				regs = append(regs, r)
			} else if m, ok := a.(x86asm.Mem); ok {
				for k, pr := range regs {
					if pr == m.Base || pr == m.Index {
						alias += fmt.Sprintf("@%d", k)
					}
				}
			}
		}
		key := inst.Op.String() + " " + strings.Join(sig, ",") + alias
		if lifter.HasLockPrefix(inst) {
			key = "LOCK " + key // sampled apart from the plain form: a different lowering
		}
		if force && seen[key] {
			key += " {" + safeText(inst, addr) + "}" // a synthesized form: keep AH-DH and aliasing apart
		}
		siteCount[key]++
		if seen[key] || (!force && perOp[inst.Op] >= *maxPerOp) {
			sampledOut++
			return
		}
		setRegs := map[int]uint64{}
		if inst.Op == x86asm.PUSHFQ || inst.Op == x86asm.POPFQ {
			setRegs[4] = stackTop
		}
		if mem != nil {
			bi, hasB := gprIndex[mem.Base]
			ii, hasI := gprIndex[mem.Index]
			scale := uint64(mem.Scale)
			if scale == 0 {
				scale = 1
			}
			// x86asm keeps a disp32 zero-extended; the CPU sign-extends it.
			disp := uint64(int64(int32(mem.Disp)))
			if hasB && hasI && bi == ii {
				v := (memTarget - disp) / (1 + scale)
				v &^= 63
				setRegs[bi] = v
			} else {
				iv := uint64(0)
				if hasI {
					iv = indexValue
					setRegs[ii] = iv
				}
				setRegs[bi] = memTarget - disp - iv*scale
			}
			switch inst.Op {
			case x86asm.BT, x86asm.BTS, x86asm.BTR, x86asm.BTC:
				if r, ok := inst.Args[1].(x86asm.Reg); ok {
					if i, ok := gprIndex[r]; ok {
						if _, used := setRegs[i]; !used {
							setRegs[i] = btOffset(r)
						}
					}
				}
			}
		}
		lines, _, err := lift(inst, addr, modeBlockStart)
		if err != nil {
			noteSkip("lift error: "+err.Error(), inst, addr, key)
			return
		}
		seen[key] = true
		if ok, why := selfContained(lines); !ok {
			noteSkip(why, inst, addr, key)
			return
		}
		perOp[inst.Op]++
		add(inst, append([]byte(nil), code...), addr, key, setRegs)
		// Synthesized aliasing variants of VEX register forms: src2 = dst, src1 = dst.
		for _, v := range vexAliasVariants(code) {
			vi, err := x86asm.Decode(v, 64)
			if err != nil || vi.Op != inst.Op || vi.Len != len(v) || !regArgsOK(vi) {
				continue
			}
			if safeText(vi, addr) == safeText(inst, addr) {
				continue // VEX.vvvv set on an instruction without that operand: #UD, nothing new
			}
			vk := "VAR " + safeText(vi, addr)
			if seen[vk] {
				continue
			}
			seen[vk] = true
			add(vi, v, addr, key+" [synth]", setRegs)
		}
	}

	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	seenAddr := map[uint64]bool{}
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		c := strings.IndexByte(line, ':')
		if c <= 0 {
			continue
		}
		addr, err := strconv.ParseUint(strings.TrimPrefix(line[:c], "0x"), 16, 64)
		if err != nil || addr >= uint64(len(img)) || seenAddr[addr] {
			continue
		}
		seenAddr[addr] = true
		inst, err := x86asm.Decode(img[addr:], 64)
		if err != nil || inst.Len == 0 {
			filter("decode error", x86asm.Inst{}, addr)
			continue
		}
		process(inst, img[addr:addr+uint64(inst.Len)], addr, false)
	}
	// Every LOCK form and operand size, also those neither eboot uses: the atomic lowerings of
	// the forms TLOU and U4 lack would otherwise never run.
	for i, code := range lockForms {
		inst, err := x86asm.Decode(code, 64)
		if err != nil || inst.Len != len(code) {
			fmt.Fprintf(os.Stderr, "lock form %x: bad encoding\n", code)
			os.Exit(1)
		}
		process(inst, code, synthLockAddr+uint64(16*i), true)
	}
	sort.SliceStable(tests, func(i, j int) bool { return tests[i].sig < tests[j].sig })
	if err := os.MkdirAll(*outDir, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	writeNative(filepath.Join(*outDir, "native_tests.s"), tests)
	writeAsm(filepath.Join(*outDir, "asm_tests.s"), tests)
	writeList(filepath.Join(*outDir, "tests.txt"), tests)
	for _, sk := range skipped {
		for _, k := range sk.keys {
			sk.sites += siteCount[k]
		}
	}
	writeSkipped(filepath.Join(*outDir, "skipped.txt"), classes, skipped, sampledOut)
	nf := 0
	for _, c := range classes {
		nf += c.sites
	}
	fmt.Printf("%d tests written to %s (%d listing sites filtered in %d classes, %d opcode/reason pairs not "+
		"testable after lifting; see skipped.txt)\n", len(tests), *outDir, nf, len(classes), len(skipped))
}

func writeList(path string, tests []*test) {
	var b strings.Builder
	for i, t := range tests {
		fmt.Fprintf(&b, "%d\t0x%x\t%s\t%s\t%s\t%s\t%s\n", i, t.addr, t.sig, t.text, t.flags, t.vregs, t.vdest)
	}
	os.WriteFile(path, []byte(b.String()), 0o644)
}

func writeSkipped(path string, classes map[string]*classInfo, skipped map[string]*skipInfo, sampledOut int) {
	var b strings.Builder
	b.WriteString("# Filtered listing sites (never lifted for a test), by class\n# class\tsites\topcodes (sites)\texample\n")
	names := make([]string, 0, len(classes))
	for k := range classes {
		names = append(names, k)
	}
	sort.Slice(names, func(i, j int) bool { return classes[names[i]].sites > classes[names[j]].sites })
	for _, k := range names {
		c := classes[k]
		ops := make([]string, 0, len(c.ops))
		for op := range c.ops {
			ops = append(ops, op)
		}
		sort.Slice(ops, func(i, j int) bool { return c.ops[ops[i]] > c.ops[ops[j]] })
		var parts []string
		for _, op := range ops {
			parts = append(parts, fmt.Sprintf("%s %d", op, c.ops[op]))
		}
		fmt.Fprintf(&b, "%s\t%d\t%s\t%s\n", k, c.sites, strings.Join(parts, ", "), c.example)
	}
	fmt.Fprintf(&b, "\n# Sampled out (signature already tested or %s per opcode reached): %d sites\n", "max-per-op", sampledOut)
	keys := make([]string, 0, len(skipped))
	for k := range skipped {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	b.WriteString("\n# Not testable after lifting (runtime helper call, branch out, unsupported)\n" +
		"# opcode\treason\tdistinct signatures\tsites\texample\n")
	for _, k := range keys {
		fmt.Fprintf(&b, "%s\t%d\t%d\t%s\n", k, skipped[k].count, skipped[k].sites, skipped[k].example)
	}
	os.WriteFile(path, []byte(b.String()), 0o644)
}

// State layout (shared with harness.h): u64 gpr[16]; u64 flags; u32 mxcsr; u32 pad; u8 ymm[16][32]
const (
	offGpr   = 0
	offFlags = 128
	offMxcsr = 136
	offYmm   = 144
)

var gprNames = []string{"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"}

// arm64 register pinned to each GuestContext-order GPR (see arm64RegMap in pkg/lifter).
var armGpr = []string{"x19", "x21", "x22", "x20", "x26", "x25", "x23", "x24", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15"}

func writeNative(path string, tests []*test) {
	var b strings.Builder
	b.WriteString(".intel_syntax noprefix\n.text\n")
	b.WriteString(".globl _g_state\n.zerofill __DATA,__bss,_g_state,8,3\n")
	b.WriteString(".zerofill __DATA,__bss,_g_saved_rsp,8,3\n.zerofill __DATA,__bss,_g_tmp,8,3\n")
	b.WriteString(".zerofill __DATA,__bss,_g_host_mxcsr,8,3\n.zerofill __DATA,__bss,_g_test_rsp,8,3\n")
	for i, t := range tests {
		fmt.Fprintf(&b, "\n.globl _nt_%d\n.p2align 4\n_nt_%d:\n", i, i)
		b.WriteString("  push rbx\n  push rbp\n  push r12\n  push r13\n  push r14\n  push r15\n")
		b.WriteString("  mov [rip+_g_saved_rsp], rsp\n  mov [rip+_g_state], rdi\n")
		// The host MXCSR is restored on return: a test's (V)LDMXCSR must not change how the C
		// driver rounds while it builds the next inputs.
		b.WriteString("  stmxcsr [rip+_g_host_mxcsr]\n")
		for r := 0; r < 16; r++ {
			fmt.Fprintf(&b, "  vmovdqu ymm%d, [rdi+%d]\n", r, offYmm+32*r)
		}
		fmt.Fprintf(&b, "  ldmxcsr [rdi+%d]\n", offMxcsr)
		fmt.Fprintf(&b, "  push qword ptr [rdi+%d]\n  popfq\n", offFlags)
		for r := 0; r < 16; r++ {
			if r == 4 || r == 7 {
				continue
			}
			fmt.Fprintf(&b, "  mov %s, [rdi+%d]\n", gprNames[r], offGpr+8*r)
		}
		if t.stackOp {
			fmt.Fprintf(&b, "  mov rsp, [rdi+%d]\n", offGpr+8*4)
		}
		fmt.Fprintf(&b, "  mov rdi, [rdi+%d]\n", offGpr+8*7)
		b.WriteString("  .byte ")
		for k, by := range t.bytes {
			if k > 0 {
				b.WriteString(",")
			}
			fmt.Fprintf(&b, "0x%02x", by)
		}
		b.WriteString("\n")
		if t.stackOp { // back to the host stack before anything else uses it (MOV keeps the flags)
			b.WriteString("  mov [rip+_g_test_rsp], rsp\n  mov rsp, [rip+_g_saved_rsp]\n")
		}
		b.WriteString("  mov [rip+_g_tmp], rax\n  pushfq\n  pop rax\n")
		b.WriteString("  push rcx\n  mov rcx, [rip+_g_state]\n")
		fmt.Fprintf(&b, "  mov [rcx+%d], rax\n", offFlags)
		b.WriteString("  pop rax\n") // rax = original rcx
		fmt.Fprintf(&b, "  mov [rcx+%d], rax\n", offGpr+8*1)
		b.WriteString("  mov rax, [rip+_g_tmp]\n")
		fmt.Fprintf(&b, "  mov [rcx+%d], rax\n", offGpr)
		for r := 2; r < 16; r++ {
			if r == 4 {
				continue
			}
			fmt.Fprintf(&b, "  mov [rcx+%d], %s\n", offGpr+8*r, gprNames[r])
		}
		if t.stackOp {
			fmt.Fprintf(&b, "  mov rax, [rip+_g_test_rsp]\n  mov [rcx+%d], rax\n", offGpr+8*4)
		}
		for r := 0; r < 16; r++ {
			fmt.Fprintf(&b, "  vmovdqu [rcx+%d], ymm%d\n", offYmm+32*r, r)
		}
		fmt.Fprintf(&b, "  stmxcsr [rcx+%d]\n", offMxcsr)
		b.WriteString("  mov rsp, [rip+_g_saved_rsp]\n  vzeroupper\n  ldmxcsr [rip+_g_host_mxcsr]\n")
		b.WriteString("  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rbp\n  pop rbx\n  ret\n")
	}
	fmt.Fprintf(&b, "\n.data\n.globl _nt_count\n_nt_count: .quad %d\n.globl _nt_table\n.p2align 3\n_nt_table:\n", len(tests))
	for i := range tests {
		fmt.Fprintf(&b, "  .quad _nt_%d\n", i)
	}
	b.WriteString(".globl _nt_regsets\n_nt_regsets:\n")
	writeRegsets(&b, tests)
	os.WriteFile(path, []byte(b.String()), 0o644)
}

func writeRegsets(b *strings.Builder, tests []*test) {
	for _, t := range tests {
		// per test: mask of overridden GPRs, then 16 values
		var mask uint64
		vals := make([]uint64, 16)
		for r, v := range t.setRegs {
			mask |= 1 << uint(r)
			vals[r] = v
		}
		switch t.mode {
		case modeFpCmp:
			mask |= regsetFpCmp
		case modeAfterLogic:
			mask |= regsetLogic
		}
		if strings.HasPrefix(t.sig, "LDMXCSR ") || strings.HasPrefix(t.sig, "VLDMXCSR ") {
			mask |= regsetMxcsr
		}
		if t.clShift {
			mask |= regsetShiftCL
		}
		if strings.HasPrefix(t.sig, "FLDCW ") {
			mask |= regsetFcw
		}
		if strings.HasPrefix(t.sig, "POPFQ") {
			mask |= regsetPopf
		}
		if strings.Contains(t.text, "st7") {
			mask |= regsetX87Full
		}
		switch t.op {
		case x86asm.CMPXCHG, x86asm.CMPXCHG16B:
			mask |= regsetCasEqual
		case x86asm.CMPXCHG8B:
			mask |= regsetCas8b
		}
		fmt.Fprintf(b, "  .quad 0x%x", mask)
		for _, v := range vals {
			fmt.Fprintf(b, ", 0x%x", v)
		}
		b.WriteString("\n")
	}
}

// readoutOps are lifted after the test instruction to read the flags back; byte k of the
// readout area holds the value of condition k. The x86 condition encoding order (O, NO, B, AE,
// E, NE, BE, A, S, NS, P, NP, L, GE, LE, G) is kept so compare.py can index by cc number.
var readoutOps = []x86asm.Op{
	x86asm.SETO, x86asm.SETNO, x86asm.SETB, x86asm.SETAE, x86asm.SETE, x86asm.SETNE, x86asm.SETBE, x86asm.SETA,
	x86asm.SETS, x86asm.SETNS, x86asm.SETP, x86asm.SETNP, x86asm.SETL, x86asm.SETGE, x86asm.SETLE, x86asm.SETG,
}

// Stack frame of an asm test function (bytes from sp after the prologue).
const (
	frameSize  = 208
	frameState = 160 // DtState *
	frameRax   = 168 // guest RAX while the SETcc readout uses AL
	frameFlags = 176 // 16 bytes: SETcc readout, x86 cc order
	frameX18   = 192 // host x18 (platform register), restored before returning
	frameFpcr  = 200 // host FPCR (LDMXCSR changes the rounding mode), restored before returning
)

// writeAsm emits the arm64 test functions: void lt_N(DtState *st, uint64_t fs_base).
func writeAsm(path string, tests []*test) {
	var b strings.Builder
	b.WriteString("// Generated by lifter-difftest-asm. One function per test: void lt_N(DtState *st, uint64_t fs_base).\n")
	b.WriteString(".text\n.p2align 2\n")
	pairs := [][2]string{{"x19", "x20"}, {"x21", "x22"}, {"x23", "x24"}, {"x25", "x26"}, {"x27", "x28"}}
	for i, t := range tests {
		fmt.Fprintf(&b, "\n// %s  [%s]\n.globl _lt_%d\n.p2align 2\n_lt_%d:\n", t.text, t.sig, i, i)
		// 1. Save host callee-saved registers (x19-x28, x29/x30, d8-d15) and x18.
		fmt.Fprintf(&b, "    stp x29, x30, [sp, #-%d]!\n    mov x29, sp\n", frameSize)
		for k, r := range pairs {
			fmt.Fprintf(&b, "    stp %s, %s, [sp, #%d]\n", r[0], r[1], 16+16*k)
		}
		for k := 0; k < 4; k++ {
			fmt.Fprintf(&b, "    stp d%d, d%d, [sp, #%d]\n", 8+2*k, 9+2*k, 96+16*k)
		}
		fmt.Fprintf(&b, "    str x0, [sp, #%d]\n    str x18, [sp, #%d]\n", frameState, frameX18)
		fmt.Fprintf(&b, "    mrs x2, fpcr\n    str x2, [sp, #%d]\n", frameFpcr)
		// 2. Load the guest state: vectors, NZCV, pinned GPRs, FS, mem_base.
		for r := 0; r < 16; r++ {
			fmt.Fprintf(&b, "    ldr q%d, [x0, #%d]\n    ldr q%d, [x0, #%d]\n", r, offYmm+32*r, r+16, offYmm+32*r+16)
		}
		fmt.Fprintf(&b, "    ldr x2, [x0, #%d]\n", offFlags)
		b.WriteString("    ubfx x3, x2, #7, #1\n    lsl x4, x3, #31\n")         // SF -> N
		b.WriteString("    ubfx x3, x2, #6, #1\n    orr x4, x4, x3, lsl #30\n") // ZF -> Z
		b.WriteString("    ubfx x3, x2, #0, #1\n")
		if t.mode != modeAfterAdd && t.mode != modeAfterLogic {
			b.WriteString("    eor x3, x3, #1\n") // C = !CF (carrySub)
		}
		b.WriteString("    orr x4, x4, x3, lsl #29\n")
		if t.mode == modeFpCmp {
			b.WriteString("    ubfx x3, x2, #2, #1\n    orr x4, x4, x3, lsl #28\n") // PF -> V
		} else {
			b.WriteString("    ubfx x3, x2, #11, #1\n    orr x4, x4, x3, lsl #28\n") // OF -> V
		}
		b.WriteString("    msr nzcv, x4\n    msr fpcr, xzr\n")
		b.WriteString("    mov x27, x1\n    mov x28, #0\n")
		for r := 0; r < 16; r++ {
			fmt.Fprintf(&b, "    ldr %s, [x0, #%d]\n", armGpr[r], offGpr+8*r)
		}
		// Poison the scratch registers so lowerings that read them before writing show up.
		for _, r := range []string{"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x16", "x17", "x18"} {
			fmt.Fprintf(&b, "    movz %s, #0xdead, lsl #48\n    movk %s, #0xbeef, lsl #16\n", r, r)
		}
		// 3. The lifted instruction.
		for _, ln := range t.lifted {
			b.WriteString(ln)
			b.WriteString("\n")
		}
		// 4. Read the flags back through the lifter's own SETcc lowering, then store the state.
		fmt.Fprintf(&b, "    str x19, [sp, #%d]\n", frameRax)
		if t.mode == modeNoFlags {
			fmt.Fprintf(&b, "    stp xzr, xzr, [sp, #%d]\n", frameFlags)
		} else {
			for _, ln := range t.readout {
				b.WriteString(ln)
				b.WriteString("\n")
			}
		}
		fmt.Fprintf(&b, "    ldr x19, [sp, #%d]\n    ldr x16, [sp, #%d]\n", frameRax, frameState)
		for r := 0; r < 16; r++ {
			fmt.Fprintf(&b, "    str %s, [x16, #%d]\n", armGpr[r], offGpr+8*r)
		}
		for r := 0; r < 16; r++ {
			fmt.Fprintf(&b, "    str q%d, [x16, #%d]\n    str q%d, [x16, #%d]\n", r, offYmm+32*r, r+16, offYmm+32*r+16)
		}
		// Flags word: CF/PF/ZF/SF/OF in their EFLAGS bits (from SETB/SETP/SETE/SETS/SETO) and all
		// 16 condition readouts in bits 32-47.
		b.WriteString("    mov x5, #0x202\n")
		for k := range readoutOps {
			fmt.Fprintf(&b, "    ldrb w0, [sp, #%d]\n    orr x5, x5, x0, lsl #%d\n", frameFlags+k, 32+k)
		}
		for _, f := range [][2]int{{2, 0}, {10, 2}, {4, 6}, {8, 7}, {0, 11}} { // B->CF P->PF E->ZF S->SF O->OF
			fmt.Fprintf(&b, "    ldrb w0, [sp, #%d]\n    orr x5, x5, x0, lsl #%d\n", frameFlags+f[0], f[1])
		}
		fmt.Fprintf(&b, "    str x5, [x16, #%d]\n", offFlags)
		// 5. Restore host registers.
		fmt.Fprintf(&b, "    ldr x18, [sp, #%d]\n", frameX18)
		fmt.Fprintf(&b, "    ldr x2, [sp, #%d]\n    msr fpcr, x2\n", frameFpcr)
		for k, r := range pairs {
			fmt.Fprintf(&b, "    ldp %s, %s, [sp, #%d]\n", r[0], r[1], 16+16*k)
		}
		for k := 0; k < 4; k++ {
			fmt.Fprintf(&b, "    ldp d%d, d%d, [sp, #%d]\n", 8+2*k, 9+2*k, 96+16*k)
		}
		fmt.Fprintf(&b, "    ldp x29, x30, [sp], #%d\n    ret\n", frameSize)
	}
	fmt.Fprintf(&b, "\n.data\n.globl _lt_count\n.p2align 3\n_lt_count: .quad %d\n.globl _lt_table\n_lt_table:\n", len(tests))
	for i := range tests {
		fmt.Fprintf(&b, "  .quad _lt_%d\n", i)
	}
	b.WriteString(".globl _lt_regsets\n_lt_regsets:\n")
	writeRegsets(&b, tests)
	os.WriteFile(path, []byte(b.String()), 0o644)
}

func safeText(inst x86asm.Inst, addr uint64) (out string) {
	defer func() {
		if recover() != nil {
			out = inst.String()
		}
	}()
	return x86asm.IntelSyntax(inst, addr, nil)
}

// vexAliasVariants returns re-encodings of a VEX register-form instruction where the second
// source (ModRM.rm) and/or the first source (VEX.vvvv) name the destination (ModRM.reg).
func vexAliasVariants(b []byte) [][]byte {
	if len(b) < 4 {
		return nil
	}
	var modrmAt int
	var reg int
	switch b[0] {
	case 0xC5:
		modrmAt = 3
		if len(b) <= modrmAt {
			return nil
		}
		reg = int((b[3]>>3)&7) | int((^b[1]>>7)&1)<<3
	case 0xC4:
		if m := b[1] & 0x1f; m != 1 && m != 2 && m != 3 {
			return nil
		}
		modrmAt = 4
		if len(b) <= modrmAt {
			return nil
		}
		reg = int((b[4]>>3)&7) | int((^b[1]>>7)&1)<<3
	default:
		return nil
	}
	if b[modrmAt]>>6 != 3 {
		return nil
	}
	var out [][]byte
	// src2 = dst
	v := append([]byte(nil), b...)
	v[modrmAt] = (v[modrmAt] &^ 7) | byte(reg&7)
	if b[0] == 0xC4 {
		if reg >= 8 {
			v[1] &^= 0x20 // B = ~1
		} else {
			v[1] |= 0x20
		}
		out = append(out, v)
	} else if reg < 8 {
		out = append(out, v)
	}
	// src1 = dst (vvvv = ~reg)
	w := append([]byte(nil), b...)
	vb := 2
	if b[0] == 0xC5 {
		vb = 1
	}
	w[vb] = (w[vb] &^ 0x78) | byte((^reg&0xf)<<3)
	out = append(out, w)
	return out
}

// synthLockAddr is the (fake) address of the synthesized LOCK forms, outside any guest image.
const synthLockAddr = 0xf0000000

// lockForms: every LOCK read-modify-write form in every operand size, with register and
// immediate sources, AH-DH operands and base/source aliasing, plus XCHG with memory (implicitly
// locked) and CMPXCHG8B/16B. The memory operand is [rdi] unless noted.
var lockForms = [][]byte{
	// ADD: byte al, byte ah, word, dword, qword, byte imm, dword imm8, qword imm8, qword imm32
	{0xf0, 0x00, 0x07}, {0xf0, 0x00, 0x27}, {0x66, 0xf0, 0x01, 0x07}, {0xf0, 0x01, 0x07}, {0xf0, 0x48, 0x01, 0x07},
	{0xf0, 0x80, 0x07, 0x7f}, {0xf0, 0x83, 0x07, 0x80}, {0xf0, 0x48, 0x83, 0x07, 0x01},
	{0xf0, 0x48, 0x81, 0x07, 0x78, 0x56, 0x34, 0x12},
	// SUB
	{0xf0, 0x28, 0x07}, {0x66, 0xf0, 0x29, 0x07}, {0xf0, 0x29, 0x07}, {0xf0, 0x48, 0x29, 0x07},
	{0xf0, 0x83, 0x2f, 0x05}, {0xf0, 0x48, 0x83, 0x2f, 0xff}, {0x66, 0xf0, 0x83, 0x2f, 0x01},
	// AND
	{0xf0, 0x20, 0x07}, {0x66, 0xf0, 0x21, 0x07}, {0xf0, 0x21, 0x07}, {0xf0, 0x48, 0x21, 0x07},
	{0xf0, 0x83, 0x27, 0xf0}, {0xf0, 0x80, 0x27, 0x0f},
	// OR
	{0xf0, 0x08, 0x07}, {0x66, 0xf0, 0x09, 0x07}, {0xf0, 0x09, 0x07}, {0xf0, 0x48, 0x09, 0x07},
	{0xf0, 0x83, 0x0f, 0x01},
	// XOR
	{0xf0, 0x30, 0x07}, {0x66, 0xf0, 0x31, 0x07}, {0xf0, 0x31, 0x07}, {0xf0, 0x48, 0x31, 0x07},
	{0xf0, 0x83, 0x37, 0xff},
	// INC, DEC (also [rdi+8])
	{0xf0, 0xfe, 0x07}, {0x66, 0xf0, 0xff, 0x07}, {0xf0, 0xff, 0x07}, {0xf0, 0x48, 0xff, 0x07},
	{0xf0, 0xfe, 0x0f}, {0x66, 0xf0, 0xff, 0x0f}, {0xf0, 0xff, 0x0f}, {0xf0, 0x48, 0xff, 0x0f},
	{0xf0, 0x48, 0xff, 0x4f, 0x08},
	// NOT, NEG
	{0xf0, 0xf6, 0x17}, {0x66, 0xf0, 0xf7, 0x17}, {0xf0, 0xf7, 0x17}, {0xf0, 0x48, 0xf7, 0x17},
	{0xf0, 0xf6, 0x1f}, {0x66, 0xf0, 0xf7, 0x1f}, {0xf0, 0xf7, 0x1f}, {0xf0, 0x48, 0xf7, 0x1f},
	// ADC, SBB (byte ah, imm8)
	{0xf0, 0x10, 0x07}, {0x66, 0xf0, 0x11, 0x07}, {0xf0, 0x11, 0x07}, {0xf0, 0x48, 0x11, 0x07},
	{0xf0, 0x83, 0x17, 0x01},
	{0xf0, 0x18, 0x07}, {0xf0, 0x18, 0x27}, {0x66, 0xf0, 0x19, 0x07}, {0xf0, 0x19, 0x07}, {0xf0, 0x48, 0x19, 0x07},
	{0xf0, 0x83, 0x1f, 0x01},
	// BTS, BTR, BTC: register offsets (eax/ecx, see btOffset) and immediates
	{0xf0, 0x0f, 0xab, 0x07}, {0x66, 0xf0, 0x0f, 0xab, 0x0f}, {0xf0, 0x48, 0x0f, 0xab, 0x0f},
	{0xf0, 0x0f, 0xba, 0x2f, 0x1f}, {0xf0, 0x48, 0x0f, 0xba, 0x2f, 0x3f}, {0x66, 0xf0, 0x0f, 0xba, 0x2f, 0x13},
	{0xf0, 0x0f, 0xb3, 0x0f}, {0x66, 0xf0, 0x0f, 0xb3, 0x07}, {0xf0, 0x48, 0x0f, 0xb3, 0x07},
	{0xf0, 0x0f, 0xba, 0x37, 0x03}, {0xf0, 0x48, 0x0f, 0xba, 0x37, 0x21},
	{0xf0, 0x0f, 0xbb, 0x07}, {0x66, 0xf0, 0x0f, 0xbb, 0x0f}, {0xf0, 0x48, 0x0f, 0xbb, 0x0f},
	{0xf0, 0x0f, 0xba, 0x3f, 0x07},
	// XADD: byte al, byte ah, word, dword, qword, qword [rdi], rdi
	{0xf0, 0x0f, 0xc0, 0x07}, {0xf0, 0x0f, 0xc0, 0x27}, {0x66, 0xf0, 0x0f, 0xc1, 0x07}, {0xf0, 0x0f, 0xc1, 0x07},
	{0xf0, 0x48, 0x0f, 0xc1, 0x07}, {0xf0, 0x48, 0x0f, 0xc1, 0x3f},
	// XCHG with memory, with and without an explicit LOCK
	{0x86, 0x07}, {0x86, 0x27}, {0x66, 0x87, 0x07}, {0x87, 0x07}, {0x48, 0x87, 0x07}, {0xf0, 0x48, 0x87, 0x37},
	// CMPXCHG: byte cl, byte ch, word, dword, qword, source = accumulator, [rax] base
	{0xf0, 0x0f, 0xb0, 0x0f}, {0xf0, 0x0f, 0xb0, 0x2f}, {0x66, 0xf0, 0x0f, 0xb1, 0x0f}, {0xf0, 0x0f, 0xb1, 0x0f},
	{0xf0, 0x48, 0x0f, 0xb1, 0x0f}, {0xf0, 0x0f, 0xb1, 0x07}, {0xf0, 0x48, 0x0f, 0xb1, 0x07},
	{0xf0, 0x48, 0x0f, 0xb1, 0x08},
	// CMPXCHG8B, CMPXCHG16B
	{0xf0, 0x0f, 0xc7, 0x0f}, {0x0f, 0xc7, 0x0f}, {0xf0, 0x48, 0x0f, 0xc7, 0x0f},
}

// btOffset is the bit offset register value of BT/BTS/BTR/BTC with a memory operand: a random
// offset would address far outside the scratch area. The EAX family gets a positive offset into
// the next operand, every other register a negative one (the offset is signed).
func btOffset(r x86asm.Reg) uint64 {
	if gprIndex[r] == 0 {
		return 77
	}
	return uint64(0xffffffffffffffd3) // -45
}
