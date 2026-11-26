// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpUtility runs testdata/np_utility/main.c: libSceNpUtility against shadPS4's
// np_utility.cpp without a shadNet sign-in: NpLookup and NpWordFilter title contexts and
// requests, the signed-out lookup, local word filtering, SetTimeout checks, WaitAsync across
// threads, and the shims.
func TestNpUtility(t *testing.T) {
	bin := buildHostC(t, "np_utility", "../runtime/modules/libSceNpUtility/ps4_np_utility.c")
	runHostCases(t, bin, []string{"lookup", "timeouts", "word-filter", "wait", "shims"})
}
