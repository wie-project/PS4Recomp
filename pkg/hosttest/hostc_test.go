// SPDX-License-Identifier: GPL-2.0-or-later

// Package hosttest runs host programs that test pure C parts of the guest runtime
// (pkg/runtime/modules). The programs live here, not under pkg/runtime: the recompiler copies
// that tree into every game build and compiles each C source in it.
package hosttest

import (
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// buildHostC compiles a host test program from testdata/<name>/main.c and the given runtime
// sources (paths relative to this package, e.g. ../runtime/modules/...), with each source's
// directory on the include path.
func buildHostC(t *testing.T, name string, sources ...string) string {
	t.Helper()
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not found")
	}
	bin := filepath.Join(t.TempDir(), name)
	args := []string{"-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
		filepath.Join("testdata", name, "main.c")}
	seen := map[string]bool{}
	for _, s := range sources {
		args = append(args, s)
		if d := filepath.Dir(s); !seen[d] {
			seen[d] = true
			args = append(args, "-I"+d)
		}
	}
	args = append(args, "-o", bin)
	if out, err := exec.Command("clang", args...).CombinedOutput(); err != nil {
		t.Fatalf("building %s: %v\n%s", name, err, out)
	}
	return bin
}

// runHostCases runs each case of a host test program (its only argument) as a subtest: the case
// passes when the program exits 0 and prints "PASS <case>".
func runHostCases(t *testing.T, bin string, cases []string) {
	t.Helper()
	for _, c := range cases {
		t.Run(c, func(t *testing.T) {
			out, err := exec.Command(bin, c).CombinedOutput()
			if err != nil || !strings.Contains(string(out), "PASS "+c) {
				t.Fatalf("%s %s: %v\n%s", filepath.Base(bin), c, err, out)
			}
		})
	}
}
