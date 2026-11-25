// SPDX-License-Identifier: GPL-2.0-or-later

package cli

import (
	"os"
	"path/filepath"
	"testing"
	"testing/fstest"
)

// TestPruneStaleRuntime: a runtime source removed since the last build of an output directory
// disappears from it together with its object, while current sources, their objects and
// unrelated files stay.
func TestPruneStaleRuntime(t *testing.T) {
	src := fstest.MapFS{
		"modules/libSceRtc/ps4_rtc.c":      {Data: []byte("x")},
		"modules/libSceRtc/ps4_rtc.h":      {Data: []byte("x")},
		"modules/libScePad/ps4_keyboard.m": {Data: []byte("x")},
	}
	dst := t.TempDir()
	files := map[string]bool{ // path -> kept
		"modules/libSceRtc/ps4_rtc.c":          true,
		"modules/libSceRtc/ps4_rtc.h":          true,
		"modules/libSceRtc/ps4_rtc.o":          true,
		"modules/libSceRtc/ps4_rtc.o.d":        true,
		"modules/libScePad/ps4_keyboard.o":     true,
		"modules/libSceRtc/notes.txt":          true,
		"modules/libSceRtc/ps4_rtc_extras.c":   false,
		"modules/libSceRtc/ps4_rtc_extras.o":   false,
		"modules/libSceRtc/ps4_rtc_extras.o.d": false,
		"modules/libSceOld/ps4_old.h":          false,
	}
	for p := range files {
		full := filepath.Join(dst, p)
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte("x"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	if err := pruneStaleRuntime(dst, src); err != nil {
		t.Fatal(err)
	}
	for p, kept := range files {
		_, err := os.Stat(filepath.Join(dst, p))
		if exists := err == nil; exists != kept {
			t.Errorf("%s: exists=%v, want %v", p, exists, kept)
		}
	}
	// A directory that does not exist yet (first build) is fine.
	if err := pruneStaleRuntime(filepath.Join(dst, "missing"), src); err != nil {
		t.Fatal(err)
	}
}
