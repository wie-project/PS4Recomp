// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestGnmPassWrites runs testdata/gnm_pass_writes/main.cpp: the attachments of its render pass a
// draw writes (ps4_gnm_pass_writes.h), which decide whether a later draw that samples one of them
// may continue the pass.
func TestGnmPassWrites(t *testing.T) {
	bin := buildHostCxx(t, "gnm_pass_writes", "../runtime/modules/libSceGnmDriver/ps4_gnm_pass_writes.h")
	runHostCases(t, bin, []string{"depth", "stencil", "clear", "draw-writes"})
}
