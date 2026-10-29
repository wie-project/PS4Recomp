// SPDX-License-Identifier: GPL-2.0-or-later

// Package compat holds the embedded game support database and turns a game
// content directory into an analysis with a support verdict.
package compat

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"ps4-recomp/pkg/analyzer"
	"ps4-recomp/pkg/sfo"
)

// Support statuses. StatusUnsupportedInput is decided by callers, never by Lookup.
const (
	StatusPlayable         = "playable"
	StatusInProgress       = "in_progress"
	StatusOtherVersion     = "other_version"
	StatusExperimental     = "experimental"
	StatusUnsupportedInput = "unsupported_input"
)

//go:embed compat.json
var embedded []byte

type entry struct {
	TitleID     string            `json:"title_id"`
	Name        string            `json:"name"`
	Versions    map[string]string `json:"versions"`
	Recommended map[string]int    `json:"recommended"`
	KnownIssues []string          `json:"known_issues"`
}

// DB is a parsed support database.
type DB struct {
	games map[string]entry
}

// Verdict is the support answer for one title and version.
type Verdict struct {
	Status         string
	Name           string
	TestedVersions []string
	Recommended    map[string]int
	KnownIssues    []string
}

// Parse reads and validates a database document.
func Parse(b []byte) (*DB, error) {
	var doc struct {
		Version int     `json:"version"`
		Games   []entry `json:"games"`
	}
	if err := json.Unmarshal(b, &doc); err != nil {
		return nil, fmt.Errorf("compat: %w", err)
	}
	if doc.Version != 1 {
		return nil, fmt.Errorf("compat: unsupported database version %d", doc.Version)
	}
	db := &DB{games: make(map[string]entry, len(doc.Games))}
	for _, g := range doc.Games {
		if g.TitleID == "" {
			return nil, errors.New("compat: game without title_id")
		}
		for ver, st := range g.Versions {
			if st != StatusPlayable && st != StatusInProgress {
				return nil, fmt.Errorf("compat: %s version %s: unknown status %q", g.TitleID, ver, st)
			}
		}
		db.games[g.TitleID] = g
	}
	return db, nil
}

// Embedded returns the database compiled into the binary.
func Embedded() *DB {
	db, err := Parse(embedded)
	if err != nil {
		panic(err) // the embedded file is checked by tests
	}
	return db
}

// Lookup returns the verdict for a title and application version.
func (db *DB) Lookup(titleID, appVer string) Verdict {
	g, ok := db.games[titleID]
	if !ok {
		return Verdict{Status: StatusExperimental}
	}
	v := Verdict{Name: g.Name, Recommended: g.Recommended, KnownIssues: g.KnownIssues}
	for ver := range g.Versions {
		v.TestedVersions = append(v.TestedVersions, ver)
	}
	sort.Strings(v.TestedVersions)
	if st, ok := g.Versions[appVer]; ok {
		v.Status = st
	} else {
		v.Status = StatusOtherVersion
	}
	return v
}

// GameAnalysis is the result of analysing a game content directory.
type GameAnalysis struct {
	TitleID, Title, AppVer, Category string
	Verdict                          Verdict
	Coverage                         float64
	Unresolved                       int
	MissingOps                       int
	Modules                          []string
}

// readParam fills the identity fields from sce_sys/param.sfo. Real dumps often
// lack APP_VER, so VERSION is used then.
func readParam(contentDir string) (*GameAnalysis, error) {
	f, err := sfo.Load(filepath.Join(contentDir, "sce_sys", "param.sfo"))
	if err != nil {
		return nil, err
	}
	g := &GameAnalysis{}
	g.TitleID, _ = f.String("TITLE_ID")
	g.Title, _ = f.String("TITLE")
	g.Category, _ = f.String("CATEGORY")
	if v, ok := f.String("APP_VER"); ok && v != "" {
		g.AppVer = v
	} else {
		g.AppVer, _ = f.String("VERSION")
	}
	return g, nil
}

func listModules(contentDir string) []string {
	var mods []string
	ents, err := os.ReadDir(filepath.Join(contentDir, "sce_module"))
	if err != nil {
		return nil
	}
	for _, e := range ents {
		// Hidden files, such as the "._" AppleDouble companions macOS leaves on non-APFS
		// volumes, are not modules.
		if strings.HasPrefix(e.Name(), ".") {
			continue
		}
		n := strings.ToLower(e.Name())
		if !e.IsDir() && (strings.HasSuffix(n, ".prx") || strings.HasSuffix(n, ".sprx")) {
			mods = append(mods, e.Name())
		}
	}
	sort.Strings(mods)
	return mods
}

// AnalyzeGame reads sce_sys/param.sfo, analyses eboot.bin and looks the game
// up in the embedded database.
func AnalyzeGame(contentDir string) (*GameAnalysis, error) {
	g, err := readParam(contentDir)
	if err != nil {
		return nil, err
	}
	rep, err := analyzer.AnalyzeBinary(filepath.Join(contentDir, "eboot.bin"))
	if err != nil {
		return nil, err
	}
	g.Coverage = rep.CoveragePercent
	g.MissingOps = rep.MissingUniqueOps
	if rep.HLE != nil {
		g.Unresolved = rep.HLE.UnresolvedCount
	}
	g.Modules = listModules(contentDir)
	g.Verdict = Embedded().Lookup(g.TitleID, g.AppVer)
	return g, nil
}
