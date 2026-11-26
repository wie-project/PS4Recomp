// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestCompanionHttpd runs testdata/companion_httpd/main.c: the libSceCompanionHttpd lifecycle.
func TestCompanionHttpd(t *testing.T) {
	bin := buildHostC(t, "companion_httpd",
		"../runtime/modules/libSceHttp/ps4_companion_httpd.c",
	)
	runHostCases(t, bin, []string{
		"companion-lifecycle",
	})
}
