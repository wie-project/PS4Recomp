// SPDX-License-Identifier: GPL-2.0-or-later

package cli

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"os/signal"
	"syscall"
	"time"
)

// errBuildCancelled ends a build stopped by SIGINT or SIGTERM; with --json it is reported
// as error code "cancelled".
var errBuildCancelled = errors.New("build cancelled")

// groupKillDelay is how long the process group of a cancelled child has after SIGTERM
// before it gets SIGKILL.
const groupKillDelay = 5 * time.Second

// buildSignalContext returns a context that is cancelled on SIGINT or SIGTERM, and the
// function that stops the signal notifier (restoring the default action).
func buildSignalContext() (context.Context, context.CancelFunc) {
	return signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
}

// groupCommand is exec.CommandContext for a build child (ninja, clang): the child runs in
// a process group of its own, and when ctx ends the whole group, the child and everything
// it started, gets SIGTERM and, after groupKillDelay, SIGKILL. SIGTERM comes first so that
// ninja and clang remove the outputs they were writing; a truncated object file would
// otherwise look up to date to the next build.
func groupCommand(ctx context.Context, name string, args ...string) *exec.Cmd {
	cmd := exec.CommandContext(ctx, name, args...)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.Cancel = func() error {
		pgid := cmd.Process.Pid
		time.AfterFunc(groupKillDelay, func() { syscall.Kill(-pgid, syscall.SIGKILL) })
		return syscall.Kill(-pgid, syscall.SIGTERM)
	}
	cmd.WaitDelay = groupKillDelay + time.Second
	return cmd
}

// cancelledOr returns errBuildCancelled when ctx has ended and err otherwise: the error of
// a child killed by a cancel is the cancel.
func cancelledOr(ctx context.Context, err error) error {
	if ctx.Err() != nil {
		return errBuildCancelled
	}
	return err
}
