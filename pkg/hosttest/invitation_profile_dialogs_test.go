// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestInvitationProfileDialogs runs testdata/invitation_profile_dialogs/main.c: the
// libSceInvitationDialog and libSceNpProfileDialog state machines, error order, results and the
// requests their windows show, on a host without an NP sign-in. The AppKit windows are replaced
// by recording hooks.
func TestInvitationProfileDialogs(t *testing.T) {
	bin := buildHostCFlags(t, "invitation_profile_dialogs",
		[]string{"-I../runtime/modules/libSceMsgDialog"},
		"../runtime/modules/libSceMsgDialog/ps4_common_dialog.c",
		"../runtime/modules/libSceInvitationDialog/ps4_invitation_dialog.c",
		"../runtime/modules/libSceNpProfileDialog/ps4_np_profile_dialog.c",
	)
	runHostCases(t, bin, []string{
		"invitation-lifecycle",
		"invitation-send",
		"profile-lifecycle",
		"profile-grief-report",
	})
}
