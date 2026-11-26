// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestHttp runs testdata/http/main.c: libSceHttp as shadPS4's network/http.cpp (object
// lifecycle and errors, setting and epoll inheritance, offline failure, real requests against a
// local server with redirects and gzip, non-blocking requests, the URI and header parsers, host
// overrides) and libSceHttp2 as network/http2.cpp over it (the request -> connection
// bookkeeping).
func TestHttp(t *testing.T) {
	bin := buildHostCFlags(t, "http",
		[]string{kernelInclude, "-I../runtime/modules/libSceNetCtl", "-lcurl", linkZlib},
		"../runtime/modules/libSceHttp/ps4_http.c",
		"../runtime/modules/libSceHttp/ps4_http_parse.c",
		"../runtime/modules/libSceHttp/ps4_http_transport.c",
		"../runtime/modules/libSceHttp/ps4_http_overrides.c",
		"../runtime/modules/libSceHttp/ps4_http2.c",
		"../runtime/modules/libSceNetCtl/ps4_netctl.c",
	)
	runHostCases(t, bin, []string{
		"objects",
		"offline",
		"online",
		"nonblock",
		"http2",
		"parse",
		"overrides",
	})
}
