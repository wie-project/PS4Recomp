// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestPngEnc runs testdata/pngenc/main.c: verifies libScePngEnc lifecycle,
// parameter validation, and real PNG generation with zlib compression.
func TestPngEnc(t *testing.T) {
	bin := buildHostC(t, "pngenc",
		"-lz",
		"../runtime/modules/libScePngDec/ps4_pngdec.c",
	)
	runHostCases(t, bin, []string{
		"pngenc-lifecycle",
		"pngenc-encode",
	})
}
