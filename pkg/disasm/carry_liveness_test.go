package disasm

import (
	"encoding/hex"
	"sort"
	"strings"
	"testing"

	"golang.org/x/arch/x86/x86asm"
)

// testBlock is one block of a synthetic function: a label and its instructions. An instruction
// is the hex encoding of a non-branch instruction or a branch written as
//
//	"jcc:OP:LABEL"   conditional branch (Jcc, LOOP) to a block of the function
//	"jmp:LABEL"      direct jump; LABEL "out" is a function outside (a tail call), "nofn" an
//	                 address outside that is no function entry
//	"call:out"       direct call outside the function
type testBlock struct {
	label string
	insts []string
}

var testOps = map[string]x86asm.Op{"JE": x86asm.JE, "JNE": x86asm.JNE, "JB": x86asm.JB, "LOOP": x86asm.LOOP}

// testOutside is a function entry outside the function under test, testNotFunc an address
// outside it that is no function entry (testIsFunc).
const (
	testOutside = 0x900000
	testNotFunc = 0x800000
)

func testIsFunc(addr uint64) bool { return addr == testOutside }

// buildFunction lays the blocks out contiguously from 0x1000 and returns the function and the
// address of each block. jumpTables maps the label of a block ending in an indirect jump to the
// labels of its table targets.
func buildFunction(t *testing.T, blocks []testBlock, jumpTables map[string][]string) (*Function, map[string]uint64) {
	t.Helper()
	addrs := map[string]uint64{"out": testOutside, "nofn": testNotFunc}
	type pending struct {
		block, idx int
		label      string
	}
	var fixups []pending
	fn := &Function{EntryAddr: 0x1000, Blocks: map[uint64]*BasicBlock{}, JumpTables: map[uint64][]uint64{}}
	pc := uint64(0x1000)
	var made []*BasicBlock
	for _, tb := range blocks {
		addrs[tb.label] = pc
		b := &BasicBlock{StartAddr: pc}
		for _, s := range tb.insts {
			var inst x86asm.Inst
			label := ""
			switch {
			case strings.HasPrefix(s, "jcc:"):
				f := strings.Split(s, ":")
				inst = x86asm.Inst{Op: testOps[f[1]], Args: x86asm.Args{x86asm.Rel(0)}, Len: 6}
				label = f[2]
			case strings.HasPrefix(s, "jmp:"):
				inst = x86asm.Inst{Op: x86asm.JMP, Args: x86asm.Args{x86asm.Rel(0)}, Len: 5}
				label = s[4:]
			case strings.HasPrefix(s, "call:"):
				inst = x86asm.Inst{Op: x86asm.CALL, Args: x86asm.Args{x86asm.Rel(0)}, Len: 5}
				label = s[5:]
			default:
				by, err := hex.DecodeString(s)
				if err != nil {
					t.Fatalf("bad hex %q", s)
				}
				inst, err = x86asm.Decode(by, 64)
				if err != nil {
					t.Fatalf("decode %s: %v", s, err)
				}
			}
			b.Insts = append(b.Insts, Instruction{Address: pc, Inst: inst})
			if label != "" {
				fixups = append(fixups, pending{len(made), len(b.Insts) - 1, label})
			}
			pc += uint64(inst.Len)
		}
		b.EndAddr = pc
		made = append(made, b)
	}
	for _, b := range made {
		fn.Blocks[b.StartAddr] = b
		fn.BlockOrder = append(fn.BlockOrder, b.StartAddr)
	}
	for _, f := range fixups {
		target, ok := addrs[f.label]
		if !ok {
			t.Fatalf("unknown label %q", f.label)
		}
		in := &made[f.block].Insts[f.idx]
		next := in.Address + uint64(in.Inst.Len)
		in.Inst.Args[0] = x86asm.Rel(int64(target) - int64(next))
	}
	for from, tos := range jumpTables {
		b := fn.Blocks[addrs[from]]
		last := b.Insts[len(b.Insts)-1].Address
		for _, to := range tos {
			fn.JumpTables[last] = append(fn.JumpTables[last], addrs[to])
		}
	}
	sort.Slice(fn.BlockOrder, func(i, j int) bool { return fn.BlockOrder[i] < fn.BlockOrder[j] })
	fn.EndAddr = pc
	return fn, addrs
}

// want is the expected CF/OF liveness after one instruction: block label, index in the block.
// checkOver also compares OverflowDead.
type want struct {
	label     string
	idx       int
	carryDead bool
	overDead  bool
	checkOver bool
}

func TestCarryOverflowLiveness(t *testing.T) {
	const (
		clc       = "f8"
		adcMem    = "481306"   // adc rax, [rsi]
		adcRR     = "4811d8"   // adc rax, rbx
		adcImm    = "4883d100" // adc rcx, 0
		leaRsi    = "488d7608" // lea rsi, [rsi+8]
		decRcx    = "48ffc9"   // dec rcx
		cmpRR     = "4839d8"   // cmp rax, rbx
		addRR     = "4801d8"   // add rax, rbx
		xorEax    = "31c0"     // xor eax, eax
		ret       = "c3"
		jmpRax    = "ffe0"     // jmp rax
		shlCl     = "48d3e2"   // shl rdx, cl
		shlImm    = "48c1e203" // shl rdx, 3
		shl1      = "48d1e0"   // shl rax, 1
		rcl1      = "48d1d2"   // rcl rdx, 1
		syscall   = "0f05"     // not classified: treated as reading CF/OF
		fcmovb    = "dac1"     // fcmovb st0, st1: reads CF
		addEcx1   = "83c101"   // add ecx, 1
		movRaxRb  = "4889d8"   // mov rax, rbx
		repeCmpsb = "f3a6"     // repe cmpsb
		cmpsb     = "a6"       // cmpsb
		int41     = "cd41"     // int 0x41: no-op, falls through
		int44     = "cd44"     // int 0x44: traps, no successor
	)
	for _, tc := range []struct {
		name   string
		blocks []testBlock
		tables map[string][]string
		want   []want
	}{
		{"adc chain over a dec/jnz back edge", []testBlock{
			{"entry", []string{clc}},
			{"loop", []string{adcMem, leaRsi, decRcx, "jcc:JNE:loop"}},
			{"exit", []string{ret}},
		}, nil, []want{
			{label: "entry", idx: 0, carryDead: false},
			{label: "loop", idx: 0, carryDead: false, overDead: true, checkOver: true},
			{label: "loop", idx: 2, carryDead: false, overDead: true, checkOver: true},
			{label: "loop", idx: 3, carryDead: false},
		}},
		{"Jcc: the fall-through edge reads CF", []testBlock{
			{"entry", []string{cmpRR, "jcc:JE:taken"}},
			{"fall", []string{adcImm, ret}},
			{"taken", []string{xorEax, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}, {label: "fall", idx: 0, carryDead: true}}},
		{"Jcc: the taken edge reads CF", []testBlock{
			{"entry", []string{cmpRR, "jcc:JE:taken"}},
			{"fall", []string{xorEax, ret}},
			{"taken", []string{adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"Jcc: no edge reads CF", []testBlock{
			{"entry", []string{cmpRR, "jcc:JE:taken"}},
			{"fall", []string{xorEax, ret}},
			{"taken", []string{xorEax, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: true}, {label: "entry", idx: 1, carryDead: true}}},
		{"local JMP to a CF reader", []testBlock{
			{"entry", []string{addRR, "jmp:reader"}},
			{"other", []string{ret}},
			{"reader", []string{adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}, {label: "entry", idx: 1, carryDead: false}}},
		{"tail jump out of the function", []testBlock{
			{"entry", []string{addRR, "jmp:out"}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: true}}},
		{"jump out of the function to a non-function target: unknown", []testBlock{
			{"entry", []string{addRR, "jmp:nofn"}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false, overDead: false, checkOver: true}}},
		{"Jcc out of the function to a non-function target: unknown", []testBlock{
			{"entry", []string{addRR, "jcc:JE:nofn"}},
			{"fall", []string{xorEax, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"Jcc out of the function to a function: tail call", []testBlock{
			{"entry", []string{addRR, "jcc:JE:out"}},
			{"fall", []string{xorEax, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: true}}},
		{"jump table with a CF reader", []testBlock{
			{"entry", []string{addRR, jmpRax}},
			{"t1", []string{ret}},
			{"t2", []string{adcImm, ret}},
		}, map[string][]string{"entry": {"t1", "t2"}}, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"jump table without CF readers", []testBlock{
			{"entry", []string{addRR, jmpRax}},
			{"t1", []string{ret}},
			{"t2", []string{xorEax, ret}},
		}, map[string][]string{"entry": {"t1", "t2"}}, []want{{label: "entry", idx: 0, carryDead: true}}},
		{"indirect jump without a table: unknown successors", []testBlock{
			{"entry", []string{addRR, jmpRax}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false, overDead: false, checkOver: true}}},
		{"CALL kills CF and OF", []testBlock{
			{"entry", []string{addRR, "call:out", adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: true, overDead: true, checkOver: true}}},
		{"INT 0x41 at a block end falls through to a CF reader", []testBlock{
			{"entry", []string{addRR, int41}},
			{"next", []string{adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"INT 0x41 falling off the function: unknown successor", []testBlock{
			{"entry", []string{addRR, int41}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"other INT at a block end has no successor", []testBlock{
			{"entry", []string{addRR, int44}},
			{"next", []string{adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: true}}},
		{"falling off the function: unknown successor", []testBlock{
			{"entry", []string{addRR, movRaxRb}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"shift by CL passes CF through (count 0)", []testBlock{
			{"entry", []string{addRR, shlCl, adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"shift by a non-zero immediate defines CF", []testBlock{
			{"entry", []string{addRR, shlImm, adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: true}, {label: "entry", idx: 1, carryDead: false}}},
		{"RCL reads the CF of a SHL", []testBlock{
			{"entry", []string{shl1, rcl1, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}, {label: "entry", idx: 1, carryDead: true}}},
		{"an unknown instruction reads CF and OF", []testBlock{
			{"entry", []string{addRR, syscall, xorEax, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false, overDead: false, checkOver: true}}},
		{"FCMOVB reads CF", []testBlock{
			{"entry", []string{addRR, fcmovb, xorEax, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"REPE CMPSB passes CF through (RCX = 0)", []testBlock{
			{"entry", []string{addRR, repeCmpsb, adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}}},
		{"CMPSB without REP defines CF", []testBlock{
			{"entry", []string{addRR, cmpsb, adcImm, ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: true}}},
		{"LOOP is a conditional branch", []testBlock{
			{"entry", []string{clc}},
			{"loop", []string{adcRR, "jcc:LOOP:loop"}},
			{"exit", []string{ret}},
		}, nil, []want{{label: "entry", idx: 0, carryDead: false}, {label: "loop", idx: 0, carryDead: false}}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			fn, addrs := buildFunction(t, tc.blocks, tc.tables)
			analyzeCarryOverflowLiveness(fn, testIsFunc)
			for _, w := range tc.want {
				in := fn.Blocks[addrs[w.label]].Insts[w.idx]
				if in.CarryDead != w.carryDead {
					t.Errorf("%s[%d] %v: CarryDead = %v, want %v", w.label, w.idx, in.Inst, in.CarryDead, w.carryDead)
				}
				if w.checkOver && in.OverflowDead != w.overDead {
					t.Errorf("%s[%d] %v: OverflowDead = %v, want %v", w.label, w.idx, in.Inst, in.OverflowDead, w.overDead)
				}
			}
		})
	}

	// The per-block SkipFlags pass: an instruction whose flags a later instruction of the block
	// can read must keep them.
	for _, tc := range []struct {
		name  string
		insts []string
		idx   int
		skip  bool
	}{
		{"RCL reads the CF of a SHL", []string{shl1, rcl1, addEcx1}, 0, false},
		{"shift by CL may keep the flags of a CMP", []string{cmpRR, shlCl, "jcc:JB:out"}, 0, false},
		{"an unknown instruction may read the flags", []string{cmpRR, syscall, addEcx1}, 0, false},
		{"FCMOVB reads CF", []string{cmpRR, fcmovb, addEcx1}, 0, false},
		{"flags overwritten before any reader", []string{cmpRR, movRaxRb, addEcx1}, 0, true},
	} {
		t.Run("SkipFlags: "+tc.name, func(t *testing.T) {
			fn, addrs := buildFunction(t, []testBlock{{"entry", tc.insts}}, nil)
			(&Disassembler{}).analyzeBlockFlagLiveness(fn)
			in := fn.Blocks[addrs["entry"]].Insts[tc.idx]
			if in.SkipFlags != tc.skip {
				t.Errorf("%v: SkipFlags = %v, want %v", in.Inst, in.SkipFlags, tc.skip)
			}
		})
	}
}
