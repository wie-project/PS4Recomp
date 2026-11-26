// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"strings"
	"testing"
)

// TestVrTrackerSigninDiscMap runs testdata/vrtracker_signin_discmap/main.c: libSceVrTracker
// memory requirements, Init checks, the device registry and the GPU calls of a host without a
// headset; the libSceSigninDialog status rules; and the libSceDiscMap answers of a title
// without a disc bitmap.
func TestVrTrackerSigninDiscMap(t *testing.T) {
	bin := buildHostCFlags(t, "vrtracker_signin_discmap",
		[]string{"-I../runtime/kernel/memory"},
		"../runtime/modules/libSceVrTracker/ps4_vr_tracker.c",
		"../runtime/modules/libSceSigninDialog/ps4_signin_dialog.c",
		"../runtime/modules/libSceDiscMap/ps4_disc_map.c",
	)
	runHostCases(t, bin, []string{
		"vr-query",
		"vr-lifecycle",
		"signin",
		"disc-map",
	})
	// sceVrTrackerInit with a memory range that is not mapped for its whole size stops the
	// process instead of returning.
	t.Run("vr-bad-mapping", func(t *testing.T) {
		out, err := exec.Command(bin, "vr-bad-mapping").CombinedOutput()
		if err == nil || strings.Contains(string(out), "sceVrTrackerInit returned") ||
			!strings.Contains(string(out), "insufficient work memory") {
			t.Fatalf("want an abort with the work memory message, got err=%v\n%s", err, out)
		}
	})
}
