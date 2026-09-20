package ps4pkg

import (
	"fmt"
	"io/fs"
	"path/filepath"
	"sort"
	"strings"
)

// GamePackageSet groups all packages (Base, Patches, DLCs) belonging to the same PS4 game.
type GamePackageSet struct {
	TitleID   string
	Title     string
	BaseGame  *PKG
	Patches   []*PKG
	DLCs      []*PKG
	Other     []*PKG
	TotalSize int64
}

// MultiPKGManager manages discovery, classification, and grouping of multiple PKG files.
type MultiPKGManager struct {
	Packages []*PKG
	GameSets map[string]*GamePackageSet
}

// NewMultiPKGManager creates a new MultiPKGManager.
func NewMultiPKGManager() *MultiPKGManager {
	return &MultiPKGManager{
		Packages: make([]*PKG, 0),
		GameSets: make(map[string]*GamePackageSet),
	}
}

// ScanDirectory searches a directory recursively for .pkg files and organizes them.
func (m *MultiPKGManager) ScanDirectory(dirPath string) error {
	var pkgPaths []string
	err := filepath.WalkDir(dirPath, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if !d.IsDir() && strings.EqualFold(filepath.Ext(path), ".pkg") {
			pkgPaths = append(pkgPaths, path)
		}
		return nil
	})
	if err != nil {
		return fmt.Errorf("failed to scan directory %s: %w", dirPath, err)
	}

	for _, path := range pkgPaths {
		pkg, err := Open(path)
		if err != nil {
			// Skip corrupted or unreadable files
			continue
		}
		m.AddPackage(pkg)
	}

	return nil
}

// AddPackage registers an opened PKG into the manager and associates it with its GamePackageSet.
func (m *MultiPKGManager) AddPackage(pkg *PKG) {
	m.Packages = append(m.Packages, pkg)

	titleID := pkg.TitleID()
	if titleID == "" {
		titleID = "UNKNOWN"
	}

	set, ok := m.GameSets[titleID]
	if !ok {
		set = &GamePackageSet{
			TitleID: titleID,
			Title:   pkg.Title(),
			Patches: make([]*PKG, 0),
			DLCs:    make([]*PKG, 0),
			Other:   make([]*PKG, 0),
		}
		m.GameSets[titleID] = set
	}

	set.TotalSize += pkg.FileSize

	if set.Title == "" || set.Title == titleID || strings.HasPrefix(set.Title, "EP") {
		if t := pkg.Title(); t != "" && !strings.HasPrefix(t, "EP") {
			set.Title = t
		}
	}

	if pkg.IsDLC() {
		set.DLCs = append(set.DLCs, pkg)
	} else if pkg.IsPatch() {
		set.Patches = append(set.Patches, pkg)
		if set.Title == "" || strings.Contains(set.Title, ":") {
			if t := pkg.Title(); t != "" && !strings.HasPrefix(t, "EP") {
				set.Title = t
			}
		}
	} else if pkg.IsBaseGame() {
		if set.BaseGame == nil || pkg.FileSize > set.BaseGame.FileSize {
			if set.BaseGame != nil {
				set.Other = append(set.Other, set.BaseGame)
			}
			set.BaseGame = pkg
		} else {
			set.Other = append(set.Other, pkg)
		}
		if t := pkg.Title(); t != "" && !strings.HasPrefix(t, "EP") {
			set.Title = t
		}
	} else {
		// Heuristic: if file is large (> 1GB) and no base game, classify as base
		if set.BaseGame == nil && pkg.FileSize > 1024*1024*1024 {
			set.BaseGame = pkg
			if t := pkg.Title(); t != "" && !strings.HasPrefix(t, "EP") {
				set.Title = t
			}
		} else {
			set.Other = append(set.Other, pkg)
		}
	}

	// Sort patches by AppVersion ascending
	sort.Slice(set.Patches, func(i, j int) bool {
		return set.Patches[i].AppVersion() < set.Patches[j].AppVersion()
	})

	// Sort DLCs by Title ascending
	sort.Slice(set.DLCs, func(i, j int) bool {
		return set.DLCs[i].Title() < set.DLCs[j].Title()
	})
}

// Close closes all underlying open file handles.
func (m *MultiPKGManager) Close() {
	for _, p := range m.Packages {
		_ = p.Close()
	}
}

// FormatSize formats byte counts into human readable strings (GB, MB, KB).
func FormatSize(bytes int64) string {
	const unit = 1024
	if bytes < unit {
		return fmt.Sprintf("%d B", bytes)
	}
	div, exp := int64(unit), 0
	for n := bytes / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.2f %cB", float64(bytes)/float64(div), "KMGTPE"[exp])
}

// SummaryString returns a human-readable multi-line report for a game package set.
func (set *GamePackageSet) SummaryString() string {
	var sb strings.Builder
	sb.WriteString(fmt.Sprintf("Game Title:       %s\n", set.Title))
	sb.WriteString(fmt.Sprintf("Title ID:         %s\n", set.TitleID))
	sb.WriteString(fmt.Sprintf("Total Set Size:   %s\n", FormatSize(set.TotalSize)))

	if set.BaseGame != nil {
		sb.WriteString(fmt.Sprintf("Base Game PKG:    %s (%s, AppVer: %s, ContentID: %s)\n",
			filepath.Base(set.BaseGame.FilePath),
			FormatSize(set.BaseGame.FileSize),
			set.BaseGame.AppVersion(),
			set.BaseGame.ContentID,
		))
	} else {
		sb.WriteString("Base Game PKG:    (None detected)\n")
	}

	if len(set.Patches) > 0 {
		sb.WriteString(fmt.Sprintf("Patches/Updates:  %d detected\n", len(set.Patches)))
		for i, p := range set.Patches {
			sb.WriteString(fmt.Sprintf("  [%d] %s (%s, AppVer: %s)\n",
				i+1, filepath.Base(p.FilePath), FormatSize(p.FileSize), p.AppVersion()))
		}
	} else {
		sb.WriteString("Patches/Updates:  None\n")
	}

	if len(set.DLCs) > 0 {
		sb.WriteString(fmt.Sprintf("Add-ons / DLCs:   %d detected\n", len(set.DLCs)))
		for i, d := range set.DLCs {
			name := d.Title()
			if name == "" || name == set.Title {
				name = filepath.Base(d.FilePath)
			}
			sb.WriteString(fmt.Sprintf("  [%d] %s (%s, ContentID: %s)\n",
				i+1, name, FormatSize(d.FileSize), d.ContentID))
		}
	} else {
		sb.WriteString("Add-ons / DLCs:   None\n")
	}

	if len(set.Other) > 0 {
		sb.WriteString(fmt.Sprintf("Other Packages:   %d detected\n", len(set.Other)))
		for i, o := range set.Other {
			sb.WriteString(fmt.Sprintf("  [%d] %s (%s, Category: %s)\n",
				i+1, filepath.Base(o.FilePath), FormatSize(o.FileSize), o.Category()))
		}
	}

	return sb.String()
}
