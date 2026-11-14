// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"path/filepath"
	"testing"
)

// buildHostCxx is buildHostC for a C++17 program, testdata/<name>/main.cpp, of header-only runtime
// units: only the directories of the headers go on the include path.
func buildHostCxx(t *testing.T, name string, headers ...string) string {
	t.Helper()
	if _, err := exec.LookPath("clang++"); err != nil {
		t.Skip("clang++ not found")
	}
	bin := filepath.Join(t.TempDir(), name)
	args := []string{"-O2", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
		filepath.Join("testdata", name, "main.cpp")}
	seen := map[string]bool{}
	for _, h := range headers {
		if d := filepath.Dir(h); !seen[d] {
			seen[d] = true
			args = append(args, "-I"+d)
		}
	}
	args = append(args, "-o", bin)
	if out, err := exec.Command("clang++", args...).CombinedOutput(); err != nil {
		t.Fatalf("building %s: %v\n%s", name, err, out)
	}
	return bin
}
