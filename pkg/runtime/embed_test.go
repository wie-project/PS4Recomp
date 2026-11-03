// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"os"
	"path/filepath"
	"testing"
)

func TestExtract(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "recompruntime_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	if err := Extract(tempDir); err != nil {
		t.Fatalf("Extract failed: %v", err)
	}

	headerPath := filepath.Join(tempDir, "core", "recomp_runtime.h")
	if _, err := os.Stat(headerPath); err != nil {
		t.Fatalf("expected extracted file %s, but stat failed: %v", headerPath, err)
	}
}
