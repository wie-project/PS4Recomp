// SPDX-License-Identifier: GPL-2.0-or-later

package cli

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

// homebrewFreeType is the Homebrew prefix of FreeType on Apple Silicon.
const homebrewFreeType = "/opt/homebrew/opt/freetype"

// homebrewFFmpeg is the Homebrew prefix of FFmpeg on Apple Silicon.
const homebrewFFmpeg = "/opt/homebrew/opt/ffmpeg"

// exeDirs returns the directories the bundled tools are searched from: the directory of the
// running executable, and the directory it resolves to when reached through a symlink.
func exeDirs() []string {
	exe, err := os.Executable()
	if err != nil {
		return nil
	}
	dirs := []string{filepath.Dir(exe)}
	if real, err := filepath.EvalSymlinks(exe); err == nil && filepath.Dir(real) != dirs[0] {
		dirs = append(dirs, filepath.Dir(real))
	}
	return dirs
}

// findNinja locates ninja: next to the engine (bin/ninja), then on PATH. It returns "" when
// there is none.
func findNinja() string { return ninjaIn(exeDirs(), exec.LookPath) }

func ninjaIn(dirs []string, lookPath func(string) (string, error)) string {
	for _, d := range dirs {
		for _, p := range []string{filepath.Join(d, "bin", "ninja"), filepath.Join(d, "..", "bin", "ninja")} {
			if st, err := os.Stat(p); err == nil && st.Mode().IsRegular() && st.Mode().Perm()&0o111 != 0 {
				return p
			}
		}
	}
	if p, err := lookPath("ninja"); err == nil {
		return p
	}
	return ""
}

// findFreeType returns the compiler and linker flags for FreeType: a copy bundled next to the
// engine, then pkg-config, then Homebrew. It returns nil, nil when none is found.
func findFreeType() (cflags, libs []string) {
	return freeTypeIn(exeDirs(), func(args ...string) ([]byte, error) {
		return exec.Command("pkg-config", args...).Output()
	}, homebrewFreeType)
}

func freeTypeIn(dirs []string, pkgConfig func(args ...string) ([]byte, error), brewDir string) (cflags, libs []string) {
	for _, d := range dirs {
		for _, ft := range []string{filepath.Join(d, "3rdparty", "freetype"), filepath.Join(d, "..", "3rdparty", "freetype")} {
			if !fileExists(filepath.Join(ft, "include", "freetype2", "ft2build.h")) || !fileExists(filepath.Join(ft, "lib", "libfreetype.a")) {
				continue
			}
			libs = []string{filepath.Join(ft, "lib", "libfreetype.a")}
			if png := filepath.Join(ft, "lib", "libpng16.a"); fileExists(png) {
				libs = append(libs, png)
			}
			return []string{"-I" + filepath.Join(ft, "include", "freetype2")}, append(libs, "-lz", "-lbz2")
		}
	}
	if out, err := pkgConfig("--cflags", "freetype2"); err == nil {
		cflags = strings.Fields(string(out))
	} else if fileExists(filepath.Join(brewDir, "include", "freetype2")) {
		cflags = []string{"-I" + filepath.Join(brewDir, "include", "freetype2")}
	}
	if out, err := pkgConfig("--libs", "freetype2"); err == nil {
		libs = strings.Fields(string(out))
	} else if fileExists(filepath.Join(brewDir, "lib")) {
		libs = []string{"-L" + filepath.Join(brewDir, "lib"), "-lfreetype"}
	}
	return cflags, libs
}

func fileExists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}

// findFFmpeg returns the compiler and linker flags for FFmpeg: pkg-config, then Homebrew.
// It returns nil, nil when none is found.
func findFFmpeg() (cflags, libs []string) {
	return ffmpegIn(exeDirs(), func(args ...string) ([]byte, error) {
		return exec.Command("pkg-config", args...).Output()
	}, homebrewFFmpeg)
}

func ffmpegIn(dirs []string, pkgConfig func(args ...string) ([]byte, error), brewDir string) (cflags, libs []string) {
	modules := []string{"libavformat", "libavcodec", "libswscale", "libswresample", "libavutil"}
	if out, err := pkgConfig(append([]string{"--cflags"}, modules...)...); err == nil {
		cflags = strings.Fields(string(out))
	} else if fileExists(filepath.Join(brewDir, "include")) {
		cflags = []string{"-I" + filepath.Join(brewDir, "include")}
	} else if fileExists("/opt/homebrew/include/libavcodec/avcodec.h") {
		cflags = []string{"-I/opt/homebrew/include"}
	}
	if out, err := pkgConfig(append([]string{"--libs"}, modules...)...); err == nil {
		libs = strings.Fields(string(out))
	} else if fileExists(filepath.Join(brewDir, "lib")) {
		libs = []string{"-L" + filepath.Join(brewDir, "lib"), "-lavformat", "-lavcodec", "-lswscale", "-lswresample", "-lavutil"}
	} else if fileExists("/opt/homebrew/lib/libavcodec.dylib") {
		libs = []string{"-L/opt/homebrew/lib", "-lavformat", "-lavcodec", "-lswscale", "-lswresample", "-lavutil"}
	}
	return cflags, libs
}
