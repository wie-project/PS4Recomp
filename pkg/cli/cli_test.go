package cli

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"ps4-recomp/pkg/events"
)

func TestParseArgs(t *testing.T) {
	tests := []struct {
		name        string
		args        []string
		wantElf     string
		wantOut     string
		wantCompile bool
		wantRun     bool
		wantTimeout int
		wantOpt     string
		wantErr     bool
		errIs       error
	}{
		{
			name:        "Positional ELF path",
			args:        []string{"game.elf"},
			wantElf:     "game.elf",
			wantOut:     "build",
			wantCompile: true,
			wantRun:     false,
			wantOpt:     "2",
		},
		{
			name:        "Positional ELF with following flags",
			args:        []string{"game.elf", "-o", "custom_dir", "-r", "-t", "7"},
			wantElf:     "game.elf",
			wantOut:     "custom_dir",
			wantCompile: true,
			wantRun:     true,
			wantTimeout: 7,
			wantOpt:     "2",
		},
		{
			name:        "Flag ELF and Out",
			args:        []string{"-elf", "app.elf", "-out", "custom_build", "-r", "-t", "5", "-O", "3"},
			wantElf:     "app.elf",
			wantOut:     "custom_build",
			wantCompile: true,
			wantRun:     true,
			wantTimeout: 5,
			wantOpt:     "3",
		},
		{
			name:        "Short flags",
			args:        []string{"-e", "short.elf", "-o", "bin_out", "-c=false"},
			wantElf:     "short.elf",
			wantOut:     "bin_out",
			wantCompile: false,
			wantRun:     false,
			wantOpt:     "2",
		},
		{
			name:    "Missing ELF argument",
			args:    []string{"-o", "build"},
			wantErr: true,
		},
		{
			name:    "Help flag",
			args:    []string{"--help"},
			wantErr: true,
			errIs:   flag.ErrHelp,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cfg, err := ParseArgs(tt.args)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("expected error, got nil")
				}
				if tt.errIs != nil && !errors.Is(err, tt.errIs) {
					t.Fatalf("expected error %v, got %v", tt.errIs, err)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if cfg.ElfPath != tt.wantElf {
				t.Errorf("ElfPath = %s, want %s", cfg.ElfPath, tt.wantElf)
			}
			if cfg.OutDir != tt.wantOut {
				t.Errorf("OutDir = %s, want %s", cfg.OutDir, tt.wantOut)
			}
			if cfg.Compile != tt.wantCompile {
				t.Errorf("Compile = %v, want %v", cfg.Compile, tt.wantCompile)
			}
			if cfg.Run != tt.wantRun {
				t.Errorf("Run = %v, want %v", cfg.Run, tt.wantRun)
			}
			if cfg.TimeoutSec != tt.wantTimeout {
				t.Errorf("TimeoutSec = %d, want %d", cfg.TimeoutSec, tt.wantTimeout)
			}
			if cfg.OptLevel != tt.wantOpt {
				t.Errorf("OptLevel = %s, want %s", cfg.OptLevel, tt.wantOpt)
			}
		})
	}
}

func TestParseArgsTSO(t *testing.T) {
	cfg, err := ParseArgs([]string{"eboot.bin"})
	if err != nil || !cfg.TSO {
		t.Fatalf("default: TSO = %v, err %v; want on", cfg != nil && cfg.TSO, err)
	}
	cfg, err = ParseArgs([]string{"eboot.bin", "-tso=false"})
	if err != nil || cfg.TSO {
		t.Fatalf("-tso=false: TSO = %v, err %v; want off", cfg != nil && cfg.TSO, err)
	}
}

func TestParseArgsBuildJSON(t *testing.T) {
	cfg, err := ParseArgs([]string{"build", "x/eboot.bin", "--json"})
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.JSON || cfg.ElfPath != "x/eboot.bin" {
		t.Errorf("JSON = %v, ElfPath = %q", cfg.JSON, cfg.ElfPath)
	}
	if cfg, err = ParseArgs([]string{"x/eboot.bin"}); err != nil || cfg.JSON {
		t.Errorf("default: JSON = %v, err %v; want off", cfg != nil && cfg.JSON, err)
	}
}

// A build from <library>/games/<ID>/content/eboot.bin needs no --app-dir: the content dir holding
// sce_sys/param.sfo is detected.
func TestParseArgsAppDirFromContentDir(t *testing.T) {
	content := filepath.Join(t.TempDir(), "games", "CUSA00001", "content")
	if err := os.MkdirAll(filepath.Join(content, "sce_sys"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(content, "sce_sys", "param.sfo"), []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	cfg, err := ParseArgs([]string{"build", filepath.Join(content, "eboot.bin"), "--json"})
	if err != nil {
		t.Fatal(err)
	}
	want, _ := filepath.EvalSymlinks(content)
	got, _ := filepath.EvalSymlinks(cfg.AppDir)
	if got != want {
		t.Errorf("AppDir = %q, want %q", cfg.AppDir, content)
	}
}

// A failed build with --json ends the stdout stream with an error event.
func TestExecuteJSONErrorEvent(t *testing.T) {
	var buf bytes.Buffer
	old := stdout
	stdout = &buf
	defer func() { stdout = old }()

	err := Execute([]string{"build", filepath.Join(t.TempDir(), "missing", "eboot.bin"), "--json", "-o", t.TempDir()})
	if err == nil {
		t.Fatal("Execute succeeded on a missing ELF")
	}
	lines := strings.Split(strings.TrimSpace(buf.String()), "\n")
	var last map[string]any
	for _, l := range lines {
		last = nil
		if jerr := json.Unmarshal([]byte(l), &last); jerr != nil {
			t.Fatalf("stdout line is not JSON: %q", l)
		}
	}
	if last["event"] != "error" || last["code"] != "bad_input" || last["message"] == "" {
		t.Errorf("last event = %v", last)
	}
}

// The game started by --run must not write into the JSON event stream.
func TestRunBinaryStdoutGoesToLog(t *testing.T) {
	var log bytes.Buffer
	oldLog, oldOut := logw, stdout
	logw, stdout = &log, &bytes.Buffer{}
	defer func() { logw, stdout = oldLog, oldOut }()

	if err := runBinary(context.Background(), "/bin/echo", 0, ""); err != nil {
		t.Fatal(err)
	}
	if got := stdout.(*bytes.Buffer).String(); got != "" {
		t.Errorf("stdout = %q, want empty", got)
	}
}

// With --json the stream must end in "done" only when the whole command succeeded, so the
// done event follows the optional run step.
func TestFinishBuildDoneFollowsRun(t *testing.T) {
	oldLog := logw
	logw = io.Discard
	defer func() { logw = oldLog }()

	for _, tc := range []struct {
		bin      string
		wantDone bool
	}{{"/bin/echo", true}, {"/bin/false", false}} {
		var buf bytes.Buffer
		cfg := &Config{Run: true, Compile: true}
		err := finishBuild(context.Background(), events.New(&buf, "build", true), cfg, tc.bin, "")
		if (err == nil) != tc.wantDone {
			t.Errorf("%s: err = %v", tc.bin, err)
		}
		if got := strings.Contains(buf.String(), `"event":"done"`); got != tc.wantDone {
			t.Errorf("%s: stream %q, done=%v want %v", tc.bin, buf.String(), got, tc.wantDone)
		}
	}
}

// A build directory on a volume without extended attributes (exFAT, FAT, SMB) gets an
// AppleDouble "._<name>" sidecar next to each copied runtime file that has one; the sidecars
// are binary and must not be compiled.
func TestIsRuntimeSource(t *testing.T) {
	for name, want := range map[string]bool{
		"ps4_game_key.c":     true,
		"ps4_metal_screen.m": true,
		"ps4_gnm_draw.mm":    true,
		"x.cpp":              true,
		"ps4_game_key.h":     false,
		"._ps4_game_key.c":   false,
		"._ps4_gnm_draw.mm":  false,
		"notes.txt":          false,
	} {
		if got := isRuntimeSource(name); got != want {
			t.Errorf("isRuntimeSource(%q) = %v, want %v", name, got, want)
		}
	}
}

func TestBundleDylibDependenciesRpathAndLoaderPath(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("Mach-O dylib bundling is macOS-specific")
	}
	if _, err := exec.LookPath("clang"); err != nil {
		t.Skip("clang not available")
	}

	oldLog := logw
	logw = io.Discard
	defer func() { logw = oldLog }()

	tmp := t.TempDir()
	extLibDir := filepath.Join(tmp, "ext", "pkg", "lib")
	if err := os.MkdirAll(extLibDir, 0o755); err != nil {
		t.Fatal(err)
	}

	commonSrc := filepath.Join(tmp, "common.c")
	decSrc := filepath.Join(tmp, "dec.c")
	helperSrc := filepath.Join(tmp, "helper.c")
	mainSrc := filepath.Join(tmp, "main.c")

	if err := os.WriteFile(commonSrc, []byte("int common_val(void) { return 40; }\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(decSrc, []byte("int common_val(void); int dec_val(void) { return common_val() + 2; }\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(helperSrc, []byte("int common_val(void); int helper_val(void) { return common_val() + 3; }\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(mainSrc, []byte("int dec_val(void); int helper_val(void); int main(void) { return (dec_val() == 42 && helper_val() == 43) ? 0 : 1; }\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	commonDylib := filepath.Join(extLibDir, "libfakecommon.1.dylib")
	decDylib := filepath.Join(extLibDir, "libfakedec.1.dylib")
	helperDylib := filepath.Join(extLibDir, "libfakehelper.1.dylib")

	run := func(args ...string) {
		t.Helper()
		cmd := exec.Command(args[0], args[1:]...)
		if out, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("%v failed: %v\n%s", args, err, out)
		}
	}

	// Build libfakecommon with @rpath install_name (mirrors Homebrew brotli's libbrotlicommon).
	run("clang", "-shared", "-Wl,-install_name,@rpath/libfakecommon.1.dylib", commonSrc, "-o", commonDylib)
	// Build libfakedec depending on @rpath/libfakecommon.1.dylib with LC_RPATH @loader_path/../lib.
	run("clang", "-shared", "-Wl,-install_name,"+decDylib, "-Wl,-rpath,@loader_path/../lib", decSrc, commonDylib, "-o", decDylib)
	// Build libfakehelper depending directly on @loader_path/libfakecommon.1.dylib.
	run("clang", "-shared", "-Wl,-install_name,"+helperDylib, helperSrc, commonDylib, "-o", helperDylib)
	run("install_name_tool", "-change", "@rpath/libfakecommon.1.dylib", "@loader_path/libfakecommon.1.dylib", helperDylib)

	binPath := filepath.Join(tmp, "ps4_app")
	run("clang", "-Wl,-headerpad_max_install_names", mainSrc, decDylib, helperDylib, "-o", binPath)

	contentsDir := filepath.Join(tmp, "Test.app", "Contents")
	macosDir := filepath.Join(contentsDir, "MacOS")
	if err := os.MkdirAll(macosDir, 0o755); err != nil {
		t.Fatal(err)
	}

	if err := bundleDylibDependencies(contentsDir, binPath); err != nil {
		t.Fatalf("bundleDylibDependencies failed: %v", err)
	}

	bundledBin := filepath.Join(macosDir, "Test")
	if err := copyFile(binPath, bundledBin); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(bundledBin, 0o755); err != nil {
		t.Fatal(err)
	}
	run("codesign", "--force", "--sign", "-", bundledBin)

	// Remove original external libraries so dyld can only succeed using Contents/Frameworks.
	if err := os.RemoveAll(filepath.Join(tmp, "ext")); err != nil {
		t.Fatal(err)
	}

	for _, name := range []string{"libfakecommon.1.dylib", "libfakedec.1.dylib", "libfakehelper.1.dylib"} {
		if _, err := os.Stat(filepath.Join(contentsDir, "Frameworks", name)); err != nil {
			t.Errorf("expected bundled library %s in Contents/Frameworks: %v", name, err)
		}
	}

	if out, err := exec.Command(bundledBin).CombinedOutput(); err != nil {
		t.Fatalf("bundled app execution failed: %v\n%s", err, out)
	}
}

