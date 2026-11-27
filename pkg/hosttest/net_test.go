// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNet runs testdata/net/main.c: libSceNet against shadPS4's libraries/net and core/net on
// loopback sockets: the error convention and per-thread sceNetErrnoLoc, the helpers and memory
// pools, TCP and UDP with the PS4 socket options, sceNetSocketAbort and close cutting blocking
// calls short, epolls, resolvers (an asynchronous lookup as an epoll event), the libkernel
// socket calls with kernel descriptor routing, sceNetThreadCreate / sceNetThreadJoin, and P2P
// sockets (DGRAM_P2P and TCP over the P2P transport, keys, crypto, epolls, loss recovery).
func TestNet(t *testing.T) {
	bin := buildHostCFlags(t, "net",
		[]string{kernelInclude, threadingInclude, syncInclude,
			"-I../runtime/modules/libSceNetCtl"},
		"../runtime/modules/libSceNet/ps4_net.c",
		"../runtime/modules/libSceNet/ps4_net_core.c",
		"../runtime/modules/libSceNet/ps4_net_host.c",
		"../runtime/modules/libSceNet/ps4_net_p2p.c",
		"../runtime/modules/libSceNet/ps4_net_p2p_crypto.c",
		"../runtime/modules/libSceNet/ps4_net_p2p_tcp.c",
		"../runtime/modules/libSceNet/ps4_net_p2p_transport.c",
		"../runtime/modules/libSceNet/ps4_net_resolver.c",
		"../runtime/modules/libSceNet/ps4_net_translate.c",
		"../runtime/modules/libSceNetCtl/ps4_netctl.c",
	)
	runHostCases(t, bin, []string{"basics", "tcp", "abort", "epoll", "udp", "resolver", "kernel", "thread", "p2p"})
}
