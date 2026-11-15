// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"path/filepath"
	"testing"
)

// TestGnmCompletionFenceState checks the distinction between a submit-side fence and a fixed-value
// GPU completion label.
func TestGnmCompletionFenceState(t *testing.T) {
	if _, err := exec.LookPath("clang++"); err != nil {
		t.Skip("clang++ not found")
	}
	name := "gnm_completion_fence_state"
	bin := filepath.Join(t.TempDir(), name)
	cmd := exec.Command("clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
		"-I../runtime/modules/libSceGnmDriver",
		filepath.Join("testdata", name, "main.cpp"), "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("building %s: %v\n%s", name, err, out)
	}
	runHostCases(t, bin, []string{"initial", "completion-label", "submit-fence"})
}
