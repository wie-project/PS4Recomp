// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"strings"
	"testing"
)

// TestCamera runs testdata/camera/main.c: libSceCamera handles and open state, the parameter
// checks and defaults of the attribute, config and calibration calls, and frame delivery with
// the YUY2 -> RAW16 / RAW8 conversion from a fake capture device, against shadPS4's camera
// library.
func TestCamera(t *testing.T) {
	bin := buildHostC(t, "camera", "../runtime/modules/libSceCamera/ps4_camera.c")
	runHostCases(t, bin, []string{
		"open-close",
		"not-open",
		"attributes",
		"config",
		"calibration",
		"frames",
		"no-device",
		"missing-device",
	})
	// An output format shadPS4 does not handle stops the process instead of returning.
	t.Run("bad-format", func(t *testing.T) {
		out, err := exec.Command(bin, "bad-format").CombinedOutput()
		if err == nil || strings.Contains(string(out), "sceCameraGetFrameData returned") ||
			!strings.Contains(string(out), "unhandled base format") {
			t.Fatalf("want an abort with the format message, got err=%v\n%s", err, out)
		}
	})
}
