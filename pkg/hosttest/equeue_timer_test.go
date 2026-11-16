// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestEqueueTimer runs testdata/equeue_timer/main.c: verifies equeue timer creation,
// wait deadlines, event delivery, and cancellation.
func TestEqueueTimer(t *testing.T) {
	bin := buildHostC(t, "equeue_timer",
		"../runtime/kernel/equeue/ps4_equeue.c",
	)
	runHostCases(t, bin, []string{"create-delete", "timer-event", "hrtimer-event", "delete-timer"})
}
