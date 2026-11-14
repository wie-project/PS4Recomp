// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestVideoOutPacing runs testdata/videoout_pacing/main.c: the presenter's guest flip pacing
// against model displays (60, 59.94, 120, 240, 72, 75, 144, 165 Hz, ProMotion) with an injected
// clock, a guest that waits for its flips, the real-time sleep of the pacing threads, and the
// guest vblank timeline arithmetic.
func TestVideoOutPacing(t *testing.T) {
	bin := buildHostC(t, "videoout_pacing",
		"../runtime/modules/libSceVideoOut/ps4_present_queue.c",
		"../runtime/modules/libSceVideoOut/ps4_vblank_clock.c")
	runHostCases(t, bin, []string{
		"vblank-timeline", "paces-guest", "60hz", "120hz", "72hz", "144hz", "promotion",
		"flip-rate-30", "no-drift", "late-frame", "unpaced", "wait-until",
		"guest-waits-for-flip", "realtime-wait",
	})
}
