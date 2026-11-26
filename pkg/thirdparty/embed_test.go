// SPDX-License-Identifier: GPL-2.0-or-later

package thirdparty

import "testing"

func TestThirdPartyExtract(t *testing.T) {
	tempDir := t.TempDir()

	if err := Extract(tempDir); err != nil {
		t.Fatalf("Extract failed: %v", err)
	}

	_ = HasEmbedded()
}
