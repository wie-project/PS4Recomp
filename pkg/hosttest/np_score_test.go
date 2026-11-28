// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpScore runs testdata/np_score/main.c: libSceNpScore against shadPS4's np_score.cpp
// without a shadNet sign-in: title contexts and requests, the argument checks of every
// operation in shadPS4's order, the signed-out completion of server operations, RecordGameData
// chunk buffering, the local comment operations, WaitAsync across threads, and the shims'
// register and stack arguments; and every server operation against a local shadNet server
// through the NP handler (online).
func TestNpScore(t *testing.T) {
	bin := buildHostCFlags(t, "np_score", []string{kernelInclude, userServiceInclude, shadNetInclude},
		append([]string{"../runtime/modules/libSceNpScore/ps4_np_score.c"}, npHandlerSources...)...)
	runHostCases(t, bin, []string{"contexts", "operations", "game-data", "comments", "wait", "shim-args", shadNetOnlineCase})
}
