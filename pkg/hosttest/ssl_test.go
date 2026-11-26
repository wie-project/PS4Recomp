// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestSsl runs testdata/ssl/main.c: libSceSsl as shadPS4's ssl2.cpp, the library shadPS4
// registers for libSceSsl: context ids, the dummy CA certificate in guest memory and its
// release, and the stubs.
func TestSsl(t *testing.T) {
	bin := buildHostC(t, "ssl", "../runtime/modules/libSceSsl/ps4_ssl.c")
	runHostCases(t, bin, []string{"init", "ca-certs", "stubs"})
}
