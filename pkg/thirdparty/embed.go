// SPDX-License-Identifier: GPL-2.0-or-later

package thirdparty

import (
	"embed"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
)

// FS embeds prebuilt third-party static libraries and headers when staged in assets/.
//
//go:embed all:assets
var FS embed.FS

// HasEmbedded reports whether third-party static libraries are embedded.
func HasEmbedded() bool {
	f, err := FS.Open("assets/gcn-recompiler/libgcn_recompiler.a")
	if err != nil {
		return false
	}
	_ = f.Close()
	return true
}

// Extract extracts the embedded third-party libraries and headers into destDir.
func Extract(destDir string) error {
	sub, err := fs.Sub(FS, "assets")
	if err != nil {
		return err
	}
	return fs.WalkDir(sub, ".", func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if path == "." || path == ".gitkeep" {
			return nil
		}
		targetPath := filepath.Join(destDir, path)
		if d.IsDir() {
			return os.MkdirAll(targetPath, 0o755)
		}
		if err := os.MkdirAll(filepath.Dir(targetPath), 0o755); err != nil {
			return err
		}
		srcFile, err := sub.Open(path)
		if err != nil {
			return fmt.Errorf("open embedded 3rdparty file %s: %w", path, err)
		}
		defer srcFile.Close()

		dstFile, err := os.OpenFile(targetPath, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
		if err != nil {
			return fmt.Errorf("create 3rdparty file %s: %w", targetPath, err)
		}
		defer dstFile.Close()

		if _, err := io.Copy(dstFile, srcFile); err != nil {
			return fmt.Errorf("copy %s to %s: %w", path, targetPath, err)
		}
		return nil
	})
}
