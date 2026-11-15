// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os/exec"
	"path/filepath"
	"testing"
)

func TestMetalColorTargetAlias(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native Metal regression")
	}
	source := metalTestSource(t, "color_alias.mm")
	bin := filepath.Join(t.TempDir(), "color-alias")
	cmd := exec.Command(clang, "-std=c++17", "-fobjc-arc", "-O2", "-Wl,-dead_strip",
		"-Imodules/libSceGnmDriver", "-Icore", source,
		"modules/libSceGnmDriver/ps4_gnm_resources.mm",
		"-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native Metal regression: %v\n%s", err, out)
	}
	runMetalTestBinary(t, bin)
}
