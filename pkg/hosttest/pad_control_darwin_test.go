// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"path/filepath"
	"testing"
)

func TestPadControl(t *testing.T) {
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	bin := filepath.Join(t.TempDir(), "pad-control")
	const pad = "../runtime/modules/libScePad"
	cmd := exec.Command("clang", "-O2", "-std=gnu11", "-fobjc-arc",
		"-I../runtime/core", "-I"+pad, "-framework", "Cocoa", "-framework", "GameController",
		"testdata/pad_control/main.c", pad+"/ps4_pad.m", pad+"/ps4_pad_motion.c", pad+"/ps4_pad_touch.c", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("building pad control regression: %v\n%s", err, out)
	}
	if out, err := exec.Command(bin, filepath.Join(t.TempDir(), "pad.ctl")).CombinedOutput(); err != nil {
		t.Fatalf("pad control regression: %v\n%s", err, out)
	}
}
