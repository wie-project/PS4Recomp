package emitter

import (
	"os"
	"testing"

	"ps4-recomp/pkg/elfloader"
)

func TestLookupShimByNID(t *testing.T) {
	cases := []struct {
		plain string
		nid   string
		shim  string
	}{
		{"pthread_sigmask", "JZKw5+Wrnaw", "shim_pthread_sigmask"},
		{"cpuset_getaffinity", "Pdgml4rbxYk", "shim_cpuset_getaffinity"},
		{"getrlimit", "Wh7HbV7JFqc", "shim_getrlimit"},
		{"raise", "0t0-MxQNwK4", "shim_raise"},
		{"memcpy", "Q3VBxCXhUHs", "shim_memcpy"},
		{"memset", "8zTFvBIAIN8", "shim_memset"},
		{"strlen", "j4ViWNHEgww", "shim_strlen"},
		{"strncpy", "6sJWiWSRuqk", "shim_strncpy"},
		{"sceUserServiceGetEvent", "yH17Q6NWtVg", "shim_sceUserServiceGetEvent"},
		{"scePlayGoGetProgress", "-RJWNMK3fC8", "shim_scePlayGoGetProgress"},
		{"sceRtcGetCurrentTick", "18B2NS1y9UU", "shim_sceRtcGetCurrentTick"},
		{"sceRandomGetRandomNumber", "PI7jIZj4pcE", "shim_sceRandomGetRandomNumber"},
		{"sceImeDialogInit", "NUeBrN7hzf0", "shim_sceImeDialogInit"},
		{"sceImeDialogGetResult", "x01jxu+vxlc", "shim_sceImeDialogGetResult"},
		{"sceSaveDataDialogOpen", "4tPhsP6FpDI", "shim_sceSaveDataDialogOpen"},
		{"sceErrorDialogOpen", "M2ZF-ClLhgY", "shim_sceErrorDialogOpen"},
		{"sceInvitationDialogOpenA", "sAxbHhAWMXM", "shim_sceInvitationDialogOpenA"},
		{"sceNpProfileDialogOpenA", "nrQRlLKzdwE", "shim_sceNpProfileDialogOpenA"},
		{"sceVideoRecordingOpen2", "s28dalBwp2g", "shim_sceVideoRecordingOpen2"},
		{"sceScreenShotEnable", "2xxUtuC-RzE", "shim_sceScreenShotEnable"},
		{"sceSharePlayInitialize", "isruqthpYcw", "shim_sceSharePlayInitialize"},
		{"sceMouseOpen", "RaqxZIf6DvE", "shim_sceMouseOpen"},
		{"sceHttpInit", "", "shim_sceHttpInit"},
		{"sceHttp2Init", "", "shim_sceHttp2Init"},
		{"sceSslInit", "", "shim_sceSslInit"},
		{"sceNpGetState", "", "shim_sceNpGetState"},
		{"sceNpAuthGetAuthorizationCode", "", "shim_sceNpAuthGetAuthorizationCode"},
		{"sceVoiceQoSInit", "", "shim_sceVoiceQoSInit"},
		{"_ZN3sce4Json6StringC1EPKc", "", "shim__ZN3sce4Json6StringC1EPKc"},
	}
	for _, tc := range cases {
		nid := elfloader.CalculateNID(tc.plain)
		if tc.nid != "" && nid != tc.nid {
			t.Fatalf("%s NID=%s want %s", tc.plain, nid, tc.nid)
		}
		encoded := nid + "#B#B"
		shim, ok := LookupShim(encoded)
		if !ok || shim != tc.shim {
			t.Fatalf("LookupShim(%s)=%q ok=%v want %s", encoded, shim, ok, tc.shim)
		}
	}
}

func TestPRXDynsymBindsMemcpyShim(t *testing.T) {
	path := "../../tools/OpenOrbis/PS4Toolchain/samples/using_library/sce_module/libExample.prx"
	if _, err := os.Stat(path); err != nil {
		t.Skip("libExample.prx not present")
	}
	loaded, err := elfloader.LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	e := NewCEmitter(loaded, nil, nil)
	wantNID := elfloader.CalculateNID("memcpy")
	var memcpyAddr uint64
	for _, sym := range loaded.DynSymbols {
		if elfloader.NIDPrefix(sym.Name) == wantNID && sym.Address != 0 {
			memcpyAddr = sym.Address
			break
		}
	}
	if memcpyAddr == 0 {
		t.Fatal("PRX dynsym has no memcpy")
	}
	if e.shimMap[memcpyAddr] != "shim_memcpy" {
		t.Fatalf("shimMap[0x%x]=%q want shim_memcpy", memcpyAddr, e.shimMap[memcpyAddr])
	}
}
