// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpAuth runs testdata/np_auth/main.c: libSceNpAuth request ids and the request limit,
// argument and size checks in shadPS4's order, the signed-out outcome of synchronous and
// asynchronous requests, aborting, timeouts and deleting, as shadPS4 without shadNet; and
// authorization codes from a local shadNet server through the NP handler (online).
func TestNpAuth(t *testing.T) {
	bin := buildHostCFlags(t, "np_auth",
		[]string{userServiceInclude, shadNetInclude},
		append([]string{"../runtime/modules/libSceNp/ps4_np_auth.c"}, npHandlerSources...)...,
	)
	runHostCases(t, bin, []string{"requests", "signed-out", "checks", shadNetOnlineCase})
}
