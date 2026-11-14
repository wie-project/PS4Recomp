// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os/exec"
	"path/filepath"
	"testing"
)

func TestMetalDepthCompanionHazards(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native Metal regression")
	}
	bin := filepath.Join(t.TempDir(), "depth-companion")
	cmd := exec.Command(clang, "-std=c++17", "-fobjc-arc", "-O2",
		"-Imodules/libSceGnmDriver",
		"../../tests/gnm_metal_test/depth_companion.mm",
		"-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native Metal regression: %v\n%s", err, out)
	}
	if out, err := exec.Command(bin).CombinedOutput(); err != nil {
		t.Fatalf("native Metal regression: %v\n%s", err, out)
	}
}
