// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpStorage runs testdata/np_storage/main.c: verifies libSceNp TSS/TUS storage
// validation and module lifecycle.
func TestNpStorage(t *testing.T) {
	bin := buildHostC(t, "np_storage",
		"../runtime/modules/libSceNp/ps4_np.c",
	)
	runHostCases(t, bin, []string{
		"np-storage",
		"module-lifecycle",
	})
}
