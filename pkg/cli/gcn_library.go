// SPDX-License-Identifier: GPL-2.0-or-later

package cli

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// The shader translator is linked from a prebuilt 3rdparty/gcn-recompiler/libgcn_recompiler.a
// (README: built with CMake, then copied there). A library built from other sources than the
// headers the runtime is compiled against links without complaint and translates differently
// (a library older than the inline fetch shader translation: every draw whose vertex buffer
// Metal cannot fetch was skipped). The library carries its build ID (GCN_BUILD_ID, a hash of
// the sources, cmake/gcn_build_id.cmake) in its shader cache ID string "<id>-f<format>";
// checkGcnLibrary recomputes it from the sources next to the library and refuses a mismatch.

// gcnBuildIDInputs lists the files cmake/gcn_build_id.cmake hashes, as the GCN_ID_INPUTS globs of
// 3rdparty/gcn-recompiler/CMakeLists.txt find them, by their path relative to the source dir
// (CMake sorts the absolute paths, which share that prefix: the same order). A missing directory
// is an empty glob.
func gcnBuildIDInputs(dir string) ([]string, error) {
	dir, err := realDir(dir)
	if err != nil {
		return nil, err
	}
	var out []string
	walk := func(sub string, exts ...string) error {
		root := filepath.Join(dir, sub)
		if _, err := os.Stat(root); os.IsNotExist(err) {
			return nil
		}
		return filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if d.IsDir() {
				return nil
			}
			for _, e := range exts {
				if strings.HasSuffix(path, e) {
					rel, err := filepath.Rel(dir, path)
					if err != nil {
						return err
					}
					out = append(out, filepath.ToSlash(rel))
					break
				}
			}
			return nil
		})
	}
	if err := walk("src", ".cpp", ".h"); err != nil {
		return nil, err
	}
	if err := walk("include", ".h"); err != nil {
		return nil, err
	}
	if err := walk("externals/sirit/src", ".cpp", ".h"); err != nil {
		return nil, err
	}
	if err := walk("externals/sirit/include", ".h"); err != nil {
		return nil, err
	}
	// GCN_SPIRV_CROSS_DIR is ${CMAKE_CURRENT_SOURCE_DIR}/../spirv-cross (not cleaned: the
	// paths sort before the externals; the OS resolves ".." from the real directory, as dir is)
	ents, err := os.ReadDir(filepath.Join(dir, "..", "spirv-cross"))
	if err != nil && !os.IsNotExist(err) {
		return nil, err
	}
	for _, e := range ents {
		n := e.Name()
		if e.IsDir() {
			continue
		}
		if n == "libspirv-cross.a" ||
			(strings.HasPrefix(n, "spirv_") && (strings.HasSuffix(n, ".cpp") || strings.HasSuffix(n, ".hpp"))) {
			out = append(out, "../spirv-cross/"+n)
		}
	}
	sort.Strings(out)
	return out, nil
}

// realDir is dir with its symlinks resolved: ".." in "../spirv-cross" is taken from the directory
// the link points to (the OS does so for CMake's glob), not lexically from the link.
func realDir(dir string) (string, error) {
	abs, err := filepath.Abs(dir)
	if err != nil {
		return "", err
	}
	return filepath.EvalSymlinks(abs)
}

// gcnBuildID is GCN_BUILD_ID for these files (paths relative to dir, sorted): the first 32 hex
// digits of the SHA-256 of the concatenated lowercase hex SHA-256 digests of their contents.
func gcnBuildID(dir string, files []string) (string, error) {
	dir, err := realDir(dir)
	if err != nil {
		return "", err
	}
	acc := sha256.New()
	buf := make([]byte, 64<<10)
	var sum [sha256.Size]byte
	var hexSum [2 * sha256.Size]byte
	h := sha256.New()
	for _, f := range files {
		r, err := os.Open(filepath.Join(dir, filepath.FromSlash(f)))
		if err != nil {
			return "", err
		}
		h.Reset()
		_, err = io.CopyBuffer(h, r, buf)
		r.Close()
		if err != nil {
			return "", err
		}
		hex.Encode(hexSum[:], h.Sum(sum[:0]))
		acc.Write(hexSum[:])
	}
	return hex.EncodeToString(acc.Sum(nil))[:32], nil
}

// libraryBuildID finds "<32 lowercase hex digits>-f<digits>\x00" (gcn_shader_cache.cpp kCacheId)
// in the library's bytes.
func libraryBuildID(lib []byte) (string, bool) {
	isHex := func(c byte) bool { return c >= '0' && c <= '9' || c >= 'a' && c <= 'f' }
	for i := 0; ; {
		j := bytes.Index(lib[i:], []byte("-f"))
		if j < 0 {
			return "", false
		}
		at := i + j
		i = at + 2
		if at < 32 || (at > 32 && isHex(lib[at-33])) {
			continue
		}
		ok := true
		for k := at - 32; k < at && ok; k++ {
			ok = isHex(lib[k])
		}
		end := at + 2
		for end < len(lib) && lib[end] >= '0' && lib[end] <= '9' {
			end++
		}
		if ok && end > at+2 && end < len(lib) && lib[end] == 0 {
			return string(lib[at-32 : at]), true
		}
	}
}

// checkGcnLibrary refuses a translator library that was not built from the sources in its
// directory (src/ next to it). Without sources (a packaged library) there is nothing to check.
func checkGcnLibrary(libPath string) error {
	dir := filepath.Dir(libPath)
	if _, err := os.Stat(filepath.Join(dir, "src")); err != nil {
		return nil
	}
	files, err := gcnBuildIDInputs(dir)
	if err != nil {
		return fmt.Errorf("gcn-recompiler sources: %w", err)
	}
	want, err := gcnBuildID(dir, files)
	if err != nil {
		return fmt.Errorf("gcn-recompiler sources: %w", err)
	}
	lib, err := os.ReadFile(libPath)
	if err != nil {
		return err
	}
	have, ok := libraryBuildID(lib)
	if !ok {
		return fmt.Errorf("%s has no build ID: it predates the sources in %s; %s", libPath, dir, rebuildSteps(dir))
	}
	if have != want {
		return fmt.Errorf("%s was built from other sources (build ID %s, the sources in %s are %s); %s",
			libPath, have, dir, want, rebuildSteps(dir))
	}
	return nil
}

// rebuildSteps says how to make the library match the sources (README: gcn-recompiler).
func rebuildSteps(dir string) string {
	return "rebuild it with `ninja -C <gcn-build>` (or `cmake --build <gcn-build>`; configure once with " +
		"`cmake -S " + dir + " -B <gcn-build> -G Ninja`) and copy it: `cp <gcn-build>/libgcn_recompiler.a " + dir + "/`. " +
		"A library configured with -DGCN_SPIRV_CROSS_DIR=<another dir> hashes that SPIRV-Cross and is always refused " +
		"(this check reads " + filepath.Join(dir, "..", "spirv-cross") + ")"
}
