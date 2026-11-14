// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"
)

// colorAliasSource locates tests/gnm_metal_test/color_alias.mm from the module root. The build
// tool copies this package into its output directory, which is still inside the module but has no
// tests/ tree, so the path cannot be relative to the package directory.
func colorAliasSource(t *testing.T) string {
	t.Helper()
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		t.Skip("the test's own source path is unknown")
	}
	for dir := filepath.Dir(file); ; dir = filepath.Dir(dir) {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			return filepath.Join(dir, "tests", "gnm_metal_test", "color_alias.mm")
		}
		if filepath.Dir(dir) == dir {
			t.Skip("no module root above the test: the native Metal regression source is not available")
		}
	}
}

func TestMetalColorTargetAlias(t *testing.T) {
	clang, err := exec.LookPath("clang++")
	if err != nil {
		t.Skip("clang++ is required for the native Metal regression")
	}
	source := colorAliasSource(t)
	if _, err := os.Stat(source); err != nil {
		t.Skipf("native Metal regression source is not present: %v", err)
	}
	bin := filepath.Join(t.TempDir(), "color-alias")
	cmd := exec.Command(clang, "-std=c++17", "-fobjc-arc", "-O2", "-Wl,-dead_strip",
		"-Imodules/libSceGnmDriver", "-Icore", source,
		"modules/libSceGnmDriver/ps4_gnm_resources.mm",
		"-framework", "Metal", "-framework", "Foundation", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build native Metal regression: %v\n%s", err, out)
	}
	if out, err := exec.Command(bin).CombinedOutput(); err != nil {
		t.Fatalf("native Metal regression: %v\n%s", err, out)
	}
}
