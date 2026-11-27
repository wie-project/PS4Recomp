// SPDX-License-Identifier: GPL-2.0-or-later

// Command shadnet-protogen regenerates the runtime's shadNet protobuf tables
// (pkg/runtime/modules/shadnet/shadnet_pb.h and shadnet_pb.c) from shadPS4's
// src/shadnet/shadnet.proto. Run it from the repository root after the shadPS4 tree in
// tools/shadps4 changes the protocol; pkg/shadnetproto's test fails while they differ.
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"ps4-recomp/pkg/shadnetproto"
)

func main() {
	proto := flag.String("proto", shadnetproto.DefaultProto, "shadnet.proto to read")
	out := flag.String("out", shadnetproto.DefaultOutDir, "directory for shadnet_pb.h and shadnet_pb.c")
	flag.Parse()
	if err := run(*proto, *out); err != nil {
		fmt.Fprintln(os.Stderr, "shadnet-protogen:", err)
		os.Exit(1)
	}
}

func run(proto, out string) error {
	h, c, err := shadnetproto.GenerateFile(proto)
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(out, shadnetproto.HeaderName), h, 0o644); err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(out, shadnetproto.SourceName), c, 0o644)
}
