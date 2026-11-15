// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"path/filepath"
	"testing"
)

func TestGnmWriteHistory(t *testing.T) {
	if _, err := exec.LookPath("clang++"); err != nil {
		t.Skip("clang++ not found")
	}
	bin := filepath.Join(t.TempDir(), "gnm-write-history")
	cmd := exec.Command("clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
		"-I../runtime/modules/libSceGnmDriver", "testdata/gnm_write_history/main.cpp", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("building write history regression: %v\n%s", err, out)
	}
	runHostCases(t, bin, []string{"snapshots", "queues", "randomized"})
}
