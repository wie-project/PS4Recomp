// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// metalTestSource locates a native Metal regression from the module root. The build tool copies
// this package into an output directory that has no tests tree, so the package-relative path is
// insufficient.
func metalTestSource(t *testing.T, name string) string {
	t.Helper()
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		t.Skip("the test's own source path is unknown")
	}
	for dir := filepath.Dir(file); ; dir = filepath.Dir(dir) {
		if _, err := os.Stat(filepath.Join(dir, "go.mod")); err == nil {
			source := filepath.Join(dir, "tests", "gnm_metal_test", name)
			if _, err := os.Stat(source); err != nil {
				t.Skipf("native Metal regression source is not present: %v", err)
			}
			return source
		}
		if filepath.Dir(dir) == dir {
			t.Skip("no module root above the test: the native Metal regression source is not available")
		}
	}
}

func runMetalTestBinary(t *testing.T, path string) {
	t.Helper()
	out, err := exec.Command(path).CombinedOutput()
	if err == nil {
		return
	}
	if strings.Contains(string(out), "Metal device unavailable") {
		t.Skipf("Metal device is unavailable: %s", out)
	}
	t.Fatalf("native Metal regression: %v\n%s", err, out)
}
