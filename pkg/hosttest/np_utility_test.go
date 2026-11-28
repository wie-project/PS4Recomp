// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpUtility runs testdata/np_utility/main.c: libSceNpUtility against shadPS4's
// np_utility.cpp without a shadNet sign-in: NpLookup and NpWordFilter title contexts and
// requests, the signed-out lookup, local word filtering, SetTimeout checks, WaitAsync across
// threads, and the shims; and sceNpLookupNpId resolved through the NP handler by a local
// shadNet server (online).
func TestNpUtility(t *testing.T) {
	bin := buildHostCFlags(t, "np_utility",
		[]string{userServiceInclude, kernelInclude, shadNetInclude},
		append([]string{
			"../runtime/modules/libSceNpUtility/ps4_np_utility.c",
		}, npHandlerSources...)...,
	)
	runHostCases(t, bin, []string{"lookup", "timeouts", "word-filter", "wait", "shims", shadNetOnlineCase})
}
