// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os/exec"
	"path/filepath"
	"testing"
)

func TestMetalSampledTextureUpload(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native Metal regression")
	}
	bin := filepath.Join(t.TempDir(), "sampled-upload")
	cmd := exec.Command(clang, "-std=c++17", "-fobjc-arc", "-O2", "-Wl,-dead_strip",
		"-Imodules/libSceGnmDriver", "-Icore", metalTestSource(t, "sampled_upload.mm"),
		"modules/libSceGnmDriver/ps4_gnm_resources.mm",
		"-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native Metal regression: %v\n%s", err, out)
	}
	runMetalTestBinary(t, bin)
}
