// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestUserService runs testdata/user_service/main.c: libSceUserService's boot login event and
// event queue, argument checks with shadPS4's error codes, the user name buffer check, the user
// color, and event callbacks registered, deduplicated, called and removed as shadPS4 does.
func TestUserService(t *testing.T) {
	bin := buildHostCFlags(t, "user_service",
		[]string{kernelInclude},
		"../runtime/modules/libSceUserService/ps4_user_service.c",
		"../runtime/modules/libSceNp/ps4_np_manager.c",
		"../runtime/modules/libSceUserService/ps4_user_service_stubs.c",
	)
	runHostCases(t, bin, []string{"events", "users", "callbacks"})
}
