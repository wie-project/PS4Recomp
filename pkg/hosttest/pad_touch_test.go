// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestPadTouch runs testdata/pad_touch/main.c: GameController and keyboard touch pad input to
// ScePadData touch points, and the finger id lifecycle.
func TestPadTouch(t *testing.T) {
	bin := buildHostC(t, "pad_touch", "../runtime/modules/libScePad/ps4_pad_touch.c")
	runHostCases(t, bin, []string{"layout", "gc-mapping", "keyboard", "ids", "third-finger"})
}
