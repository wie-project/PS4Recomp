// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestAppContent runs testdata/app_content/main.c: libSceAppContent's initialization checks,
// add-on content per service label (SERVICE_ID_ADDCONT_ADD_<n>) with entitlement keys from
// license.info and license.dat, license-only entitlements from a license container, the
// EntitlementUpdate system service event, add-on mounting and temporary data, as in shadPS4.
func TestAppContent(t *testing.T) {
	bin := buildHostCFlags(t, "app_content",
		[]string{kernelInclude},
		"../runtime/core/ps4_sfo.c",
		"../runtime/modules/libSceAppContent/ps4_app_content.c",
		"../runtime/modules/libSceNpEntitlementAccess/ps4_np_entitlement_access.c",
		"../runtime/modules/libSceSystemService/ps4_system_service.c",
	)
	runHostCases(t, bin, []string{"not-initialized", "addcont", "temp-data"})
}
