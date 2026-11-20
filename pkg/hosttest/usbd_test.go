// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestUsbd runs testdata/usbd/main.c: verifies libSceUsbd initialization,
// device list queries, device operations, control and interrupt transfer setups.
func TestUsbd(t *testing.T) {
	bin := buildHostC(t, "usbd",
		"../runtime/modules/libSceUsbd/ps4_usbd.c",
	)
	runHostCases(t, bin, []string{
		"init-exit",
		"device-list",
		"device-ops",
		"control-transfer",
		"interrupt-transfer",
		"handle-events",
	})
}
