// SPDX-License-Identifier: GPL-2.0-or-later

package shadnetproto

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const repoRoot = "../.."

// TestGeneratedFilesUpToDate fails when the committed shadnet_pb.h / shadnet_pb.c differ from
// what shadPS4's current shadnet.proto generates: run `go run ./cmd/shadnet-protogen`.
func TestGeneratedFilesUpToDate(t *testing.T) {
	proto := filepath.Join(repoRoot, DefaultProto)
	if _, err := os.Stat(proto); err != nil {
		t.Skipf("shadPS4 reference tree not present: %v", err)
	}
	h, c, err := GenerateFile(proto)
	if err != nil {
		t.Fatal(err)
	}
	for name, want := range map[string][]byte{HeaderName: h, SourceName: c} {
		got, err := os.ReadFile(filepath.Join(repoRoot, DefaultOutDir, name))
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(got, want) {
			t.Errorf("%s is stale: run `go run ./cmd/shadnet-protogen` from the repository root", name)
		}
	}
}

func TestParse(t *testing.T) {
	src := []byte(`syntax = "proto3";
package p; // comment
option optimize_for = LITE_RUNTIME;
enum Kind { A = 0; B = 1; }
/* block */
message Inner { uint32 x = 1; }
message Outer {
    string name = 1;   // trailing
    repeated uint64 ids = 2;
    Inner inner = 3;
    repeated Inner list = 4;
    Kind kind = 5;
    bytes blob = 6;
}`)
	f, err := Parse(src)
	if err != nil {
		t.Fatal(err)
	}
	if f.Package != "p" || len(f.Enums) != 1 || len(f.Messages) != 2 {
		t.Fatalf("parsed %+v", f)
	}
	want := []Field{
		{Name: "name", Number: 1, Kind: KindString},
		{Name: "ids", Number: 2, Repeated: true, Kind: KindUint64},
		{Name: "inner", Number: 3, Kind: KindMessage, TypeName: "Inner"},
		{Name: "list", Number: 4, Repeated: true, Kind: KindMessage, TypeName: "Inner"},
		{Name: "kind", Number: 5, Kind: KindEnum, TypeName: "Kind"},
		{Name: "blob", Number: 6, Kind: KindBytes},
	}
	got := f.Messages[1].Fields
	if len(got) != len(want) {
		t.Fatalf("fields %+v", got)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("field %d: got %+v, want %+v", i, got[i], want[i])
		}
	}
}

func TestParseRejectsUnsupported(t *testing.T) {
	for _, src := range []string{
		`syntax = "proto2";`,
		`syntax = "proto3"; message M { map<string, string> m = 1; }`,
		`syntax = "proto3"; message M { Missing m = 1; }`,
		`syntax = "proto3"; message M { uint32 a = 1; uint32 b = 1; }`,
		`syntax = "proto3"; service S {}`,
	} {
		if _, err := Parse([]byte(src)); err == nil {
			t.Errorf("accepted %q", src)
		} else if strings.TrimSpace(err.Error()) == "" {
			t.Errorf("empty error for %q", src)
		}
	}
}
