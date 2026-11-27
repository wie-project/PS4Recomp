// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestShadNet runs testdata/shadnet/main.c: the generated shadNet protobuf tables with the
// generic codec (against encodings worked out from the protobuf specification), and the shadNet
// client against a local server speaking shadPS4's wire protocol: login, token, server
// features, client version, notifications, an asynchronous request, a rejected login and a
// protocol version mismatch.
func TestShadNet(t *testing.T) {
	bin := buildHostCFlags(t, "shadnet", nil,
		"../runtime/modules/shadnet/shadnet_codec.c",
		"../runtime/modules/shadnet/shadnet_pb.c",
		"../runtime/modules/shadnet/shadnet_client.c")
	runHostCases(t, bin, []string{"codec", "client-login", "client-rejected", "client-version"})
}
