// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestPlayGo runs testdata/playgo/main.c: libScePlayGo over a playgo-chunk.dat in the
// console's layout (chunk sizes, loci, language mask, install speed), without PlayGo data,
// ToDoList handling and the PlayGoDialog common dialog lifecycle.
func TestPlayGo(t *testing.T) {
	bin := buildHostCFlags(t, "playgo", []string{"-I../runtime/modules/libSceSystemService"},
		"../runtime/modules/libScePlayGo/ps4_playgo.c",
	)
	runHostCases(t, bin, []string{
		"todo-list",
		"dialog-lifecycle",
		"chunk-file",
		"no-playgo",
	})
}
