package ps4pkg

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

// ExtractOptions controls PFS extraction from a PKG.
type ExtractOptions struct {
	OutputDir   string
	Passcode    string
	ListOnly    bool
	Executables bool // extract eboot, PRX/SPRX/ELF, sce_sys, sce_module
	Resources   bool // also extract remaining PFS files (paks, movies, assets)
	MetaEntries bool
	OnFile      func(path string, size int64)
}

func isExecutablePath(p string) bool {
	base := strings.ToLower(filepath.Base(p))
	if base == "eboot.bin" || strings.HasSuffix(base, ".prx") || strings.HasSuffix(base, ".sprx") || strings.HasSuffix(base, ".elf") {
		return true
	}
	return strings.HasPrefix(p, "sce_sys/") || strings.HasPrefix(p, "sce_module/")
}

// ShouldExtractPath reports whether a uroot-relative PFS path is selected by opts.
// Resources selects every file. Executables (without Resources) selects eboot, PRX/SPRX/ELF,
// sce_sys and sce_module. With neither flag the caller receives the full inner PFS.
func ShouldExtractPath(rel string, opts ExtractOptions) bool {
	if opts.Resources {
		return true
	}
	if opts.Executables {
		return isExecutablePath(rel)
	}
	return true
}

// Extract writes game files from the inner PFS (and optionally PKG table entries) to OutputDir.
func (p *PKG) Extract(opts ExtractOptions) (int, error) {
	if opts.MetaEntries {
		n, err := p.extractMetaEntries(opts.OutputDir)
		if err != nil {
			return n, err
		}
		if opts.ListOnly || opts.OutputDir == "" {
			return n, nil
		}
	}

	fs, err := p.OpenGameFS(opts.Passcode)
	if err != nil {
		return 0, err
	}

	files := fs.AllFiles()
	extracted := 0
	for _, f := range files {
		rel := strings.TrimPrefix(f.Path, "uroot/")
		if rel == "" || rel == f.Path && strings.HasPrefix(f.Path, "uroot") {
			rel = strings.TrimPrefix(f.Path, "uroot")
			rel = strings.TrimPrefix(rel, "/")
		}
		if rel == "" {
			continue
		}
		if !ShouldExtractPath(rel, opts) {
			continue
		}
		if opts.OnFile != nil {
			opts.OnFile(rel, f.Size)
		}
		if opts.ListOnly {
			extracted++
			continue
		}
		if opts.OutputDir == "" {
			return extracted, errorsExtractNoOutput
		}
		dst := filepath.Join(opts.OutputDir, filepath.FromSlash(rel))
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			return extracted, err
		}
		if err := copyNodeToFile(fs, f, dst); err != nil {
			return extracted, fmt.Errorf("failed to extract %s: %w", rel, err)
		}
		extracted++
	}
	return extracted, nil
}

var errorsExtractNoOutput = fmt.Errorf("output directory is required unless --list is set")

func copyNodeToFile(img *PfsImage, n *PfsNode, dst string) error {
	r, size := n.Reader(img)
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()
	if err := out.Truncate(size); err != nil {
		return err
	}
	buf := make([]byte, 1<<16)
	var off int64
	for off < size {
		want := len(buf)
		if size-off < int64(want) {
			want = int(size - off)
		}
		got, err := r.ReadAt(buf[:want], off)
		if got > 0 {
			if _, werr := out.Write(buf[:got]); werr != nil {
				return werr
			}
			off += int64(got)
		}
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		if got == 0 {
			return io.ErrUnexpectedEOF
		}
	}
	return nil
}

func (p *PKG) extractMetaEntries(outDir string) (int, error) {
	if outDir == "" {
		return 0, nil
	}
	n := 0
	for i := range p.Entries {
		e := &p.Entries[i]
		if e.DataSize == 0 || e.IsEncrypted {
			continue
		}
		name := e.Name
		if name == "" {
			name = fmt.Sprintf("entry_0x%08x.bin", e.ID)
		}
		dst := filepath.Join(outDir, "sce_sys", name)
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			return n, err
		}
		if err := p.ExtractEntry(e, dst); err != nil {
			continue
		}
		n++
	}
	return n, nil
}

// ExtractFile writes a single PFS path (relative to uroot) to dstPath.
func (p *PKG) ExtractFile(passcode, rel, dstPath string) error {
	fs, err := p.OpenGameFS(passcode)
	if err != nil {
		return err
	}
	n := fs.Lookup(rel)
	if n == nil || n.IsDir {
		return fmt.Errorf("file %q not found in PKG", rel)
	}
	if err := os.MkdirAll(filepath.Dir(dstPath), 0o755); err != nil {
		return err
	}
	return copyNodeToFile(fs, n, dstPath)
}
