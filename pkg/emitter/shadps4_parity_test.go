// SPDX-License-Identifier: GPL-2.0-or-later

package emitter

import (
	"os"
	"path/filepath"
	"regexp"
	"testing"
)

// shadps4Libraries is the shadPS4 reference tree (tools/shadps4) the runtime modules follow.
const shadps4Libraries = "../../tools/shadps4/src/core/libraries"

// auditedShadPS4Libraries are the shadPS4 library directories whose runtime modules were
// compared with shadPS4 function by function: every function shadPS4 registers has a shim.
var auditedShadPS4Libraries = []string{
	"companion",
	"content_export",
	"disc_map",
	"fiber", //nolint:goconst // a shadPS4 directory name, as every entry here
	"game_custom_data_dialog",
	"game_live_streaming",
	"jpeg",
	"keyboard",
	"libpng",
	"mouse",
	"move",
	"playgo",
	"random",
	"razor_cpu",
	"remote_play",
	"rtc",
	"rudp",
	"screenshot",
	"share_play",
	"signin_dialog",
	"sysmodule",
	"system_gesture",
	"ulobjmgr",
	"video_recording",
	"voice",
	"vr_tracker",
	"web_browser_dialog",
	"zlib",
}

var libFunctionRe = regexp.MustCompile(`LIB_FUNCTION\(\s*"([^"]+)",\s*"([^"]+)",\s*\d+,\s*"[^"]+",\s*([A-Za-z0-9_]+)`)

// TestAuditedLibrariesCoverShadPS4 reads the LIB_FUNCTION registrations of the audited
// shadPS4 libraries and checks that each NID resolves to a runtime shim.
func TestAuditedLibrariesCoverShadPS4(t *testing.T) {
	if _, err := os.Stat(shadps4Libraries); err != nil {
		t.Skipf("shadPS4 reference tree not present: %v", err)
	}
	for _, lib := range auditedShadPS4Libraries {
		files, err := filepath.Glob(filepath.Join(shadps4Libraries, lib, "*.cpp"))
		if err != nil || len(files) == 0 {
			t.Fatalf("%s: no shadPS4 sources (%v)", lib, err)
		}
		registered := 0
		for _, f := range files {
			src, err := os.ReadFile(f)
			if err != nil {
				t.Fatal(err)
			}
			for _, m := range libFunctionRe.FindAllSubmatch(src, -1) {
				registered++
				nid := string(m[1])
				if _, ok := LookupShim(nid + "#A#A"); !ok {
					t.Errorf("%s: %s (%s, %s) has no shim", lib, m[3], nid, m[2])
				}
			}
		}
		if registered == 0 {
			t.Errorf("%s: no LIB_FUNCTION registrations found", lib)
		}
	}
}
