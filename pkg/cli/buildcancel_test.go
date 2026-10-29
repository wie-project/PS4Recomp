// SPDX-License-Identifier: GPL-2.0-or-later

package cli

import (
	"context"
	"errors"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

// fakeTool writes an executable shell script name into a new directory and returns the
// directory. The script starts a long sleep in the background, writes its pid to
// $FAKE_PIDFILE, prints line and waits: a child that keeps running until it is killed.
func fakeTool(t *testing.T, name, line string) string {
	t.Helper()
	dir := t.TempDir()
	script := "#!/bin/sh\nsleep 60 &\necho $! > \"$FAKE_PIDFILE.tmp\"\nmv \"$FAKE_PIDFILE.tmp\" \"$FAKE_PIDFILE\"\necho '" + line + "'\nwait\n"
	if err := os.WriteFile(filepath.Join(dir, name), []byte(script), 0o755); err != nil {
		t.Fatal(err)
	}
	return dir
}

// waitPid returns the pid the fake tool wrote to pidfile.
func waitPid(t *testing.T, pidfile string) int {
	t.Helper()
	for deadline := time.Now().Add(10 * time.Second); time.Now().Before(deadline); time.Sleep(20 * time.Millisecond) {
		if b, err := os.ReadFile(pidfile); err == nil {
			pid, err := strconv.Atoi(strings.TrimSpace(string(b)))
			if err != nil {
				t.Fatal(err)
			}
			return pid
		}
	}
	t.Fatal("the fake tool did not start")
	return 0
}

// assertGone checks that process pid ends soon.
func assertGone(t *testing.T, pid int) {
	t.Helper()
	for deadline := time.Now().Add(5 * time.Second); time.Now().Before(deadline); time.Sleep(20 * time.Millisecond) {
		if errors.Is(syscall.Kill(pid, 0), syscall.ESRCH) {
			return
		}
	}
	syscall.Kill(pid, syscall.SIGKILL)
	t.Errorf("grandchild %d still runs after the cancel", pid)
}

// A cancel stops ninja and what ninja started: its whole process group.
func TestRunNinjaCancelStopsProcessGroup(t *testing.T) {
	pidfile := filepath.Join(t.TempDir(), "pid")
	t.Setenv("FAKE_PIDFILE", pidfile)
	ninja := filepath.Join(fakeTool(t, "ninja", "[1/10] compiling"), "ninja")
	oldLog := logw
	logw = io.Discard
	defer func() { logw = oldLog }()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	errc := make(chan error, 1)
	go func() { errc <- runNinja(ctx, ninja, t.TempDir(), 1, nil) }()
	pid := waitPid(t, pidfile)
	start := time.Now()
	cancel()
	select {
	case err := <-errc:
		if !errors.Is(err, errBuildCancelled) {
			t.Errorf("err = %v", err)
		}
	case <-time.After(groupKillDelay + 5*time.Second):
		t.Fatal("runNinja did not return after the cancel")
	}
	if d := time.Since(start); d > 3*time.Second {
		t.Errorf("runNinja took %v to stop", d)
	}
	assertGone(t, pid)
}

// Without ninja the clang workers are stopped the same way, and no link is attempted.
func TestCompileParallelCancelStopsClang(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("macOS build path")
	}
	pidfile := filepath.Join(t.TempDir(), "pid")
	t.Setenv("FAKE_PIDFILE", pidfile)
	t.Setenv("PATH", fakeTool(t, "clang", "compiling")+":/bin:/usr/bin") // no ninja there
	out := t.TempDir()
	src := filepath.Join(out, "a.c")
	if err := os.WriteFile(src, []byte("int a;\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	errc := make(chan error, 1)
	go func() {
		errc <- compileParallel(ctx, []string{src}, out, nil, filepath.Join(out, "ps4_app"), 1, "0", false, nil)
	}()
	pid := waitPid(t, pidfile)
	cancel()
	select {
	case err := <-errc:
		if !errors.Is(err, errBuildCancelled) {
			t.Errorf("err = %v", err)
		}
	case <-time.After(groupKillDelay + 5*time.Second):
		t.Fatal("compileParallel did not return after the cancel")
	}
	assertGone(t, pid)
}

// SIGTERM cancels the build context instead of killing the process.
func TestBuildSignalContext(t *testing.T) {
	ctx, stop := buildSignalContext()
	defer stop()
	if err := syscall.Kill(os.Getpid(), syscall.SIGTERM); err != nil {
		t.Fatal(err)
	}
	select {
	case <-ctx.Done():
	case <-time.After(5 * time.Second):
		t.Fatal("SIGTERM did not cancel the build context")
	}
}

// A failed codesign fails the bundle step, so the build ends with build_failed.
func TestPackageAppBundleFailsWhenSigningFails(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("app bundles are macOS only")
	}
	fake := t.TempDir()
	if err := os.WriteFile(filepath.Join(fake, "codesign"), []byte("#!/bin/sh\necho 'fake codesign failure' >&2\nexit 1\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	t.Setenv("PATH", fake+":/usr/bin:/bin")
	out := t.TempDir()
	bin := filepath.Join(out, "ps4_app")
	if err := copyFile("/usr/bin/true", bin); err != nil {
		t.Fatal(err)
	}
	oldLog := logw
	logw = io.Discard
	defer func() { logw = oldLog }()
	if _, err := packageAppBundle(&Config{OutDir: out}, "eboot", bin); err == nil || !strings.Contains(err.Error(), "fake codesign failure") {
		t.Errorf("packageAppBundle with a failing codesign: %v", err)
	}
}
