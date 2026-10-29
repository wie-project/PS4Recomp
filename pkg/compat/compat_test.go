// SPDX-License-Identifier: GPL-2.0-or-later

package compat

import (
	"os"
	"path/filepath"
	"testing"

	"ps4-recomp/pkg/sfo"
)

const testDB = `{"version":1,"games":[{"title_id":"CUSA04529","name":"Uncharted 4","versions":{"01.32":"in_progress"},"recommended":{"pro":0},"known_issues":["fog"]}]}`

func TestLookupRules(t *testing.T) {
	db, err := Parse([]byte(testDB))
	if err != nil {
		t.Fatal(err)
	}
	cases := []struct{ id, ver, want string }{
		{"CUSA04529", "01.32", "in_progress"},
		{"CUSA04529", "01.00", "other_version"},
		{"CUSA99999", "01.00", "experimental"},
	}
	for _, c := range cases {
		v := db.Lookup(c.id, c.ver)
		if v.Status != c.want {
			t.Errorf("%s %s = %s, want %s", c.id, c.ver, v.Status, c.want)
		}
	}
	v := db.Lookup("CUSA04529", "01.00")
	if len(v.TestedVersions) != 1 || v.TestedVersions[0] != "01.32" || v.Recommended["pro"] != 0 || v.Name != "Uncharted 4" {
		t.Errorf("verdict = %+v", v)
	}
}

func TestEmbeddedHasBothGames(t *testing.T) {
	db := Embedded()
	for _, id := range []string{"CUSA00557", "CUSA04529"} {
		if v := db.Lookup(id, ""); v.Status != "other_version" || v.Name == "" {
			t.Errorf("%s: %+v", id, v)
		}
	}
	if v := db.Lookup("CUSA00557", "01.00"); v.Status != "playable" {
		t.Errorf("TLOU 01.00 = %+v", v)
	}
}

func TestParseRejectsBadStatus(t *testing.T) {
	if _, err := Parse([]byte(`{"version":1,"games":[{"title_id":"X","versions":{"1":"great"}}]}`)); err == nil {
		t.Error("unknown status accepted")
	}
}

func writeSFO(t *testing.T, dir string, entries []sfo.Entry) {
	t.Helper()
	if err := os.MkdirAll(filepath.Join(dir, "sce_sys"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "sce_sys", "param.sfo"), sfo.Build(entries), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestAnalyzeGameNeedsSFOAndEboot(t *testing.T) {
	dir := t.TempDir()
	if _, err := AnalyzeGame(dir); err == nil {
		t.Error("no error without sce_sys/param.sfo")
	}
	writeSFO(t, dir, []sfo.Entry{{Key: "TITLE_ID", Str: "CUSA00557"}, {Key: "APP_VER", Str: "01.00"}})
	if _, err := AnalyzeGame(dir); err == nil {
		t.Error("no error without eboot.bin")
	}
}

func TestReadParamAppVerFallback(t *testing.T) {
	dir := t.TempDir()
	writeSFO(t, dir, []sfo.Entry{{Key: "TITLE_ID", Str: "CUSA00557"}, {Key: "VERSION", Str: "01.05"}, {Key: "CATEGORY", Str: "gd"}, {Key: "TITLE", Str: "T"}})
	g, err := readParam(dir)
	if err != nil {
		t.Fatal(err)
	}
	if g.AppVer != "01.05" || g.Category != "gd" || g.Title != "T" {
		t.Errorf("fallback: %+v", g)
	}
	writeSFO(t, dir, []sfo.Entry{{Key: "TITLE_ID", Str: "CUSA00557"}, {Key: "APP_VER", Str: "01.09"}, {Key: "VERSION", Str: "01.05"}})
	g, err = readParam(dir)
	if err != nil {
		t.Fatal(err)
	}
	if g.AppVer != "01.09" {
		t.Errorf("APP_VER should win: %+v", g)
	}
}

func TestModulesListed(t *testing.T) {
	dir := t.TempDir()
	mod := filepath.Join(dir, "sce_module")
	os.MkdirAll(mod, 0o755)
	for _, n := range []string{"libc.prx", "libfios2.sprx", "readme.txt", "._libc.prx", ".hidden.prx"} {
		os.WriteFile(filepath.Join(mod, n), nil, 0o644)
	}
	got := listModules(dir)
	if len(got) != 2 || got[0] != "libc.prx" || got[1] != "libfios2.sprx" {
		t.Errorf("modules = %v", got)
	}
}
