// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os/exec"
	"path/filepath"
	"testing"
)

// TestMetalComputeTessellator draws patches with Metal 3's hardware tessellation and with the
// compute tessellator the renderer uses without it (Metal 4: also through Metal4Backend), and
// requires identical images.
func TestMetalComputeTessellator(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native Metal regression")
	}
	bin := filepath.Join(t.TempDir(), "tessellator")
	cmd := exec.Command(clang, "-std=c++17", "-fobjc-arc", "-O2", "-Wl,-dead_strip",
		"-Imodules/libSceGnmDriver", "-Icore", metalTestSource(t, "tessellator.mm"),
		"modules/libSceGnmDriver/ps4_gnm_tessellator.mm", "modules/libSceGnmDriver/ps4_gnm_backend.mm",
		"modules/libSceGnmDriver/ps4_gnm_backend_metal4.mm", "modules/libSceGnmDriver/ps4_gnm_residency.mm",
		"-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native Metal regression: %v\n%s", err, out)
	}
	runMetalTestBinary(t, bin)
}
