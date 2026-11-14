// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestPadMotion runs testdata/pad_motion/main.c: orientation integration, deadband, tilt
// correction and the GameController axis mapping of ScePadData's motion fields.
func TestPadMotion(t *testing.T) {
	bin := buildHostC(t, "pad_motion", "../runtime/modules/libScePad/ps4_pad_motion.c")
	runHostCases(t, bin, []string{"rest", "integrate", "deadband", "off", "tilt", "gap", "reset"})
}
