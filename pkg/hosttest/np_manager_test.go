// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpManager runs testdata/np_manager/main.c: libSceNpManager requests and the request limit,
// argument checks in shadPS4's order, the answers without a sign-in, the SDK version rules for an
// invalid user id, and the NP state that a libSceUserService login event queues and
// sceNpCheckCallback delivers to the state and reachability callbacks, as shadPS4 without
// shadNet; and signed in through the NP handler to a local shadNet server (online).
func TestNpManager(t *testing.T) {
	bin := buildHostCFlags(t, "np_manager",
		[]string{userServiceInclude, kernelInclude, shadNetInclude},
		append([]string{
			"../runtime/modules/libSceNp/ps4_np_manager.c",
			"../runtime/modules/libSceUserService/ps4_user_service.c",
		}, npHandlerSources...)...,
	)
	runHostCases(t, bin, []string{"requests", "queries", "state-callbacks", "online"})
}
