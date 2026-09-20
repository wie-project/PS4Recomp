// Copyright 2026 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package x86asm

const (
	_ Op = iota + maxOp
	ANDN
	BEXTR
	BLSI
	BLSMSK
	BLSR
	BZHI
	MULX
	RORX
	SARX
	SHLX
	SHRX
)

func init() {
	bmiNames := map[Op]string{
		ANDN:   "ANDN",
		BEXTR:  "BEXTR",
		BLSI:   "BLSI",
		BLSMSK: "BLSMSK",
		BLSR:   "BLSR",
		BZHI:   "BZHI",
		MULX:   "MULX",
		RORX:   "RORX",
		SARX:   "SARX",
		SHLX:   "SHLX",
		SHRX:   "SHRX",
	}

	for op, name := range bmiNames {
		idx := int(op)
		for len(avxOpNames) <= idx {
			avxOpNames = append(avxOpNames, "")
		}
		avxOpNames[idx] = name
	}

	// 0F38 Table entries:
	// 0xF2: ANDN (vexP: 0)
	avxMap0F38[0xF2] = append(avxMap0F38[0xF2],
		&avxOptab{op: ANDN, args: [6]argType{argGPR32_R, argGPR32_N, argGPR32_B}, vexP: 0, vexW: 0, opdigit: -1},
		&avxOptab{op: ANDN, args: [6]argType{argGPR32_R, argGPR32_N, argM}, vexP: 0, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: ANDN, args: [6]argType{argGPR64_R, argGPR64_N, argGPR64_B}, vexP: 0, vexW: 1, opdigit: -1},
		&avxOptab{op: ANDN, args: [6]argType{argGPR64_R, argGPR64_N, argM}, vexP: 0, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},
	)

	// 0xF3: BLSR (opdigit 1), BLSMSK (opdigit 2), BLSI (opdigit 3) (vexP: 0)
	avxMap0F38[0xF3] = append(avxMap0F38[0xF3],
		// BLSR
		&avxOptab{op: BLSR, args: [6]argType{argGPR32_N, argGPR32_B}, vexP: 0, vexW: 0, opdigit: 1},
		&avxOptab{op: BLSR, args: [6]argType{argGPR32_N, argM}, vexP: 0, vexW: 0, opdigit: 1, ismem: 1, memBytes: 4},
		&avxOptab{op: BLSR, args: [6]argType{argGPR64_N, argGPR64_B}, vexP: 0, vexW: 1, opdigit: 1},
		&avxOptab{op: BLSR, args: [6]argType{argGPR64_N, argM}, vexP: 0, vexW: 1, opdigit: 1, ismem: 1, memBytes: 8},
		// BLSMSK
		&avxOptab{op: BLSMSK, args: [6]argType{argGPR32_N, argGPR32_B}, vexP: 0, vexW: 0, opdigit: 2},
		&avxOptab{op: BLSMSK, args: [6]argType{argGPR32_N, argM}, vexP: 0, vexW: 0, opdigit: 2, ismem: 1, memBytes: 4},
		&avxOptab{op: BLSMSK, args: [6]argType{argGPR64_N, argGPR64_B}, vexP: 0, vexW: 1, opdigit: 2},
		&avxOptab{op: BLSMSK, args: [6]argType{argGPR64_N, argM}, vexP: 0, vexW: 1, opdigit: 2, ismem: 1, memBytes: 8},
		// BLSI
		&avxOptab{op: BLSI, args: [6]argType{argGPR32_N, argGPR32_B}, vexP: 0, vexW: 0, opdigit: 3},
		&avxOptab{op: BLSI, args: [6]argType{argGPR32_N, argM}, vexP: 0, vexW: 0, opdigit: 3, ismem: 1, memBytes: 4},
		&avxOptab{op: BLSI, args: [6]argType{argGPR64_N, argGPR64_B}, vexP: 0, vexW: 1, opdigit: 3},
		&avxOptab{op: BLSI, args: [6]argType{argGPR64_N, argM}, vexP: 0, vexW: 1, opdigit: 3, ismem: 1, memBytes: 8},
	)

	// 0xF5: BZHI (vexP: 0)
	avxMap0F38[0xF5] = append(avxMap0F38[0xF5],
		&avxOptab{op: BZHI, args: [6]argType{argGPR32_R, argGPR32_B, argGPR32_N}, vexP: 0, vexW: 0, opdigit: -1},
		&avxOptab{op: BZHI, args: [6]argType{argGPR32_R, argM, argGPR32_N}, vexP: 0, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: BZHI, args: [6]argType{argGPR64_R, argGPR64_B, argGPR64_N}, vexP: 0, vexW: 1, opdigit: -1},
		&avxOptab{op: BZHI, args: [6]argType{argGPR64_R, argM, argGPR64_N}, vexP: 0, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},
	)

	// 0xF6: MULX (vexP: 2, F2 prefix)
	avxMap0F38[0xF6] = append(avxMap0F38[0xF6],
		&avxOptab{op: MULX, args: [6]argType{argGPR32_R, argGPR32_N, argGPR32_B}, vexP: 2, vexW: 0, opdigit: -1},
		&avxOptab{op: MULX, args: [6]argType{argGPR32_R, argGPR32_N, argM}, vexP: 2, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: MULX, args: [6]argType{argGPR64_R, argGPR64_N, argGPR64_B}, vexP: 2, vexW: 1, opdigit: -1},
		&avxOptab{op: MULX, args: [6]argType{argGPR64_R, argGPR64_N, argM}, vexP: 2, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},
	)

	// 0xF7: BEXTR (vexP: 0), SHLX (vexP: 1, 66), SHRX (vexP: 2, F2), SARX (vexP: 3, F3)
	avxMap0F38[0xF7] = append(avxMap0F38[0xF7],
		// BEXTR (no prefix -> vexP: 0)
		&avxOptab{op: BEXTR, args: [6]argType{argGPR32_R, argGPR32_B, argGPR32_N}, vexP: 0, vexW: 0, opdigit: -1},
		&avxOptab{op: BEXTR, args: [6]argType{argGPR32_R, argM, argGPR32_N}, vexP: 0, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: BEXTR, args: [6]argType{argGPR64_R, argGPR64_B, argGPR64_N}, vexP: 0, vexW: 1, opdigit: -1},
		&avxOptab{op: BEXTR, args: [6]argType{argGPR64_R, argM, argGPR64_N}, vexP: 0, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},

		// SHLX (prefix 66 -> vexP: 1)
		&avxOptab{op: SHLX, args: [6]argType{argGPR32_R, argGPR32_B, argGPR32_N}, vexP: 1, vexW: 0, opdigit: -1},
		&avxOptab{op: SHLX, args: [6]argType{argGPR32_R, argM, argGPR32_N}, vexP: 1, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: SHLX, args: [6]argType{argGPR64_R, argGPR64_B, argGPR64_N}, vexP: 1, vexW: 1, opdigit: -1},
		&avxOptab{op: SHLX, args: [6]argType{argGPR64_R, argM, argGPR64_N}, vexP: 1, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},

		// SHRX (prefix F2 -> vexP: 2)
		&avxOptab{op: SHRX, args: [6]argType{argGPR32_R, argGPR32_B, argGPR32_N}, vexP: 2, vexW: 0, opdigit: -1},
		&avxOptab{op: SHRX, args: [6]argType{argGPR32_R, argM, argGPR32_N}, vexP: 2, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: SHRX, args: [6]argType{argGPR64_R, argGPR64_B, argGPR64_N}, vexP: 2, vexW: 1, opdigit: -1},
		&avxOptab{op: SHRX, args: [6]argType{argGPR64_R, argM, argGPR64_N}, vexP: 2, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},

		// SARX (prefix F3 -> vexP: 3)
		&avxOptab{op: SARX, args: [6]argType{argGPR32_R, argGPR32_B, argGPR32_N}, vexP: 3, vexW: 0, opdigit: -1},
		&avxOptab{op: SARX, args: [6]argType{argGPR32_R, argM, argGPR32_N}, vexP: 3, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: SARX, args: [6]argType{argGPR64_R, argGPR64_B, argGPR64_N}, vexP: 3, vexW: 1, opdigit: -1},
		&avxOptab{op: SARX, args: [6]argType{argGPR64_R, argM, argGPR64_N}, vexP: 3, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},
	)

	// 0F3A Table entries:
	// 0xF0: RORX (vexP: 2, F2 prefix)
	avxMap0F3A[0xF0] = append(avxMap0F3A[0xF0],
		&avxOptab{op: RORX, args: [6]argType{argGPR32_R, argGPR32_B, argImm8u}, vexP: 2, vexW: 0, opdigit: -1},
		&avxOptab{op: RORX, args: [6]argType{argGPR32_R, argM, argImm8u}, vexP: 2, vexW: 0, opdigit: -1, ismem: 1, memBytes: 4},
		&avxOptab{op: RORX, args: [6]argType{argGPR64_R, argGPR64_B, argImm8u}, vexP: 2, vexW: 1, opdigit: -1},
		&avxOptab{op: RORX, args: [6]argType{argGPR64_R, argM, argImm8u}, vexP: 2, vexW: 1, opdigit: -1, ismem: 1, memBytes: 8},
	)
}
