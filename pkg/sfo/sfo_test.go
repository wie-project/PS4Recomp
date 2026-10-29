// SPDX-License-Identifier: GPL-2.0-or-later

package sfo

import "testing"

func TestRoundTrip(t *testing.T) {
	b := Build([]Entry{
		{Key: "APP_VER", Str: "01.32"},
		{Key: "CATEGORY", Str: "gd"},
		{Key: "CONTENT_ID", Str: "UP9000-CUSA04529_00-UNCHARTED4000000"},
		{Key: "PARENTAL_LEVEL", Int: 5, IsInt: true},
		{Key: "TITLE", Str: "Uncharted 4: A Thief’s End"},
		{Key: "TITLE_ID", Str: "CUSA04529"},
	})
	f, err := Parse(b)
	if err != nil {
		t.Fatal(err)
	}
	for k, want := range map[string]string{"APP_VER": "01.32", "CATEGORY": "gd", "TITLE": "Uncharted 4: A Thief’s End", "TITLE_ID": "CUSA04529"} {
		if got, ok := f.String(k); !ok || got != want {
			t.Errorf("%s = %q, %v; want %q", k, got, ok, want)
		}
	}
	if v, ok := f.Int("PARENTAL_LEVEL"); !ok || v != 5 {
		t.Errorf("PARENTAL_LEVEL = %d, %v", v, ok)
	}
	if _, ok := f.String("PARENTAL_LEVEL"); ok {
		t.Error("an int key must not read as a string")
	}
	if _, ok := f.String("MISSING"); ok {
		t.Error("missing key found")
	}
	if len(f.Keys()) != 6 {
		t.Errorf("keys = %v", f.Keys())
	}
}

func TestRejectsBadInput(t *testing.T) {
	for name, b := range map[string][]byte{
		"empty":     {},
		"bad magic": []byte("\x00XYZ\x01\x01\x00\x00\x14\x00\x00\x00\x14\x00\x00\x00\x00\x00\x00\x00"),
		"truncated": Build([]Entry{{Key: "TITLE_ID", Str: "CUSA00557"}})[:30],
	} {
		if _, err := Parse(b); err == nil {
			t.Errorf("%s: no error", name)
		}
	}
}
