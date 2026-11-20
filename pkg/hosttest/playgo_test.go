// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestPlayGo runs testdata/playgo/main.c: verifies libScePlayGo ToDoList handling
// and PlayGoDialog common dialog lifecycle.
func TestPlayGo(t *testing.T) {
	bin := buildHostC(t, "playgo",
		"../runtime/modules/libScePlayGo/ps4_playgo.c",
	)
	runHostCases(t, bin, []string{
		"todo-list",
		"dialog-lifecycle",
	})
}
