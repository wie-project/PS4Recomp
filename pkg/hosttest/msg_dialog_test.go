// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import (
	"os/exec"
	"strings"
	"testing"
)

// TestMsgDialog runs testdata/msgdialog/main.c: the libSceMsgDialog state machine and error
// codes, the copy of the guest parameter, the progress bar calls, the result of each button and
// the headless answers, against shadPS4's msgdialog.cpp / msgdialog_ui.cpp.
func TestMsgDialog(t *testing.T) {
	bin := buildHostC(t, "msgdialog",
		"../runtime/modules/libSceMsgDialog/ps4_common_dialog.c",
		"../runtime/modules/libSceMsgDialog/ps4_msg_dialog.c",
	)
	runHostCases(t, bin, []string{
		"state",
		"user-view",
		"progress-bar",
		"system-msg",
		"headless",
	})
	// Parameters shadPS4 asserts on stop the process instead of returning.
	for _, c := range []struct{ name, msg string }{
		{"bad-size", "param->size is not sizeof(OrbisParam)"},
		{"bad-mode", "unknown dialog mode"},
		{"bad-sysmsg", "unknown system message type"},
	} {
		t.Run(c.name, func(t *testing.T) {
			out, err := exec.Command(bin, c.name).CombinedOutput()
			if err == nil || strings.Contains(string(out), "sceMsgDialogOpen returned") ||
				!strings.Contains(string(out), c.msg) {
				t.Fatalf("want an abort with %q, got err=%v\n%s", c.msg, err, out)
			}
		})
	}
}
