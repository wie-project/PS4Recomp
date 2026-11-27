// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpCommon runs testdata/np_common/main.c: libSceNpCommon against shadPS4's np_common.cpp:
// NpId / online id comparison and ordering, platform tags, online id validation, the SDK
// version string, the mutex and condition wrappers with their NP error codes, sceNpCreateThread
// and sceNpJoinThread, and callout contexts running guest handlers in deadline order.
func TestNpCommon(t *testing.T) {
	bin := buildHostCFlags(t, "np_common",
		[]string{syncInclude, threadingInclude, kernelInclude},
		"../runtime/modules/libSceNp/ps4_np_common.c",
	)
	runHostCases(t, bin, []string{"id-helpers", "mutex", "thread", "callout"})
}
