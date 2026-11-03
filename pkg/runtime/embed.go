// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"embed"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
)

// FS embeds the complete runtime source code, headers, and modules.
//
//go:embed all:core all:kernel all:modules
var FS embed.FS

// Extract extracts the embedded runtime tree into destDir.
func Extract(destDir string) error {
	return fs.WalkDir(FS, ".", func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if path == "." || path == "embed.go" {
			return nil
		}
		targetPath := filepath.Join(destDir, path)
		if d.IsDir() {
			return os.MkdirAll(targetPath, 0o755)
		}
		if err := os.MkdirAll(filepath.Dir(targetPath), 0o755); err != nil {
			return err
		}
		srcFile, err := FS.Open(path)
		if err != nil {
			return fmt.Errorf("open embedded runtime file %s: %w", path, err)
		}
		defer srcFile.Close()

		dstFile, err := os.OpenFile(targetPath, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
		if err != nil {
			return fmt.Errorf("create runtime file %s: %w", targetPath, err)
		}
		defer dstFile.Close()

		if _, err := io.Copy(dstFile, srcFile); err != nil {
			return fmt.Errorf("copy %s to %s: %w", path, targetPath, err)
		}
		return nil
	})
}
