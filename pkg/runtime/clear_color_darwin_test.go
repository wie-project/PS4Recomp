// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os/exec"
	"path/filepath"
	"testing"
)

// TestMetalClearColorSRGB: a fast clear of an 8-bit sRGB render target (DRIVECLUB's sun shadow
// mask) must be decoded, or the clear is consumed without being applied.
func TestMetalClearColorSRGB(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native Metal regression")
	}
	source := metalTestSource(t, "clear_color.mm")
	bin := filepath.Join(t.TempDir(), "clear-color")
	cmd := exec.Command(clang, "-std=c++17", "-fobjc-arc", "-O2", "-Imodules/libSceGnmDriver", source,
		"-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native Metal regression: %v\n%s", err, out)
	}
	runMetalTestBinary(t, bin)
}
