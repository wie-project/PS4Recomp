// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestCompanionHttpd runs testdata/companion_httpd/main.c: verifies libSceCompanionHttpd
// lifecycle and libSceHttp option/CA list handling.
func TestCompanionHttpd(t *testing.T) {
	bin := buildHostC(t, "companion_httpd",
		"../runtime/modules/libSceHttp/ps4_http.c",
	)
	runHostCases(t, bin, []string{
		"companion-lifecycle",
		"http-options",
	})
}
