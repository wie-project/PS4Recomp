// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestNpCommerceParty runs testdata/np_commerce_party/main.c: the libSceCommonDialog system state
// and stubs, libSceNpParty offline answers, and libSceNpCommerce's state machine, results, the
// request its window shows and the store icon. The AppKit windows are replaced by recording hooks.
func TestNpCommerceParty(t *testing.T) {
	bin := buildHostCFlags(t, "np_commerce_party",
		[]string{userServiceInclude},
		"../runtime/modules/libSceMsgDialog/ps4_common_dialog.c",
		"../runtime/modules/libSceNp/ps4_np_party.c",
		"../runtime/modules/libSceNpCommerce/ps4_np_commerce.c",
	)
	runHostCases(t, bin, []string{
		"common-dialog",
		"np-party",
		"commerce-lifecycle",
		"commerce-view",
		"commerce-icon",
	})
}
