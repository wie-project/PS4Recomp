// SPDX-License-Identifier: GPL-2.0-or-later

package thirdparty

import (
	"os"
	"testing"
)

func TestThirdPartyExtract(t *testing.T) {
	tempDir, err := os.MkdirTemp("", "thirdparty_test_*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tempDir)

	if err := Extract(tempDir); err != nil {
		t.Fatalf("Extract failed: %v", err)
	}

	_ = HasEmbedded()
}
