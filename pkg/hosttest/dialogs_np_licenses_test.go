// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"strings"
	"testing"
)

// TestDialogsNpLicenses runs testdata/dialogs_np_licenses/main.c: the libSceErrorDialog state
// machine and message, libSceHmdSetupDialog and libSceVrServiceDialog on a host without a
// headset, the libSceNpPartner001 subscription query, and libSceNpEntitlementAccess over license
// files and a param.sfo written by the cases.
func TestDialogsNpLicenses(t *testing.T) {
	bin := buildHostCFlags(t, "dialogs_np_licenses",
		[]string{"-I../runtime/modules/libSceMsgDialog"},
		"../runtime/core/ps4_sfo.c",
		"../runtime/modules/libSceErrorDialog/ps4_error_dialog.c",
		"../runtime/modules/libSceHmdSetupDialog/ps4_hmd_setup_dialog.c",
		"../runtime/modules/libSceVrServiceDialog/ps4_vr_service_dialog.c",
		"../runtime/modules/libSceNpPartner/ps4_np_partner.c",
		"../runtime/modules/libSceNpEntitlementAccess/ps4_np_entitlement_access.c",
	)
	runHostCases(t, bin, []string{
		"error-dialog",
		"hmd-vr-dialogs",
		"np-partner",
		"entitlement-requests",
		"entitlement-licenses",
		"entitlement-single-rif",
		"entitlement-title-id",
		"entitlement-game-app",
		"entitlement-no-licenses",
	})
	// sceErrorDialogOpen with a parameter of the wrong size stops the process instead of returning.
	t.Run("error-dialog-bad-size", func(t *testing.T) {
		out, err := exec.Command(bin, "error-dialog-bad-size").CombinedOutput()
		if err == nil || strings.Contains(string(out), "sceErrorDialogOpen returned") ||
			!strings.Contains(string(out), "param size 8") {
			t.Fatalf("want an abort with the size message, got err=%v\n%s", err, out)
		}
	})
}
