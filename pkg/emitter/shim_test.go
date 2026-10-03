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
		{"sceKernelConfiguredFlexibleMemorySize", "n1-v6FgU7MQ", "shim_sceKernelConfiguredFlexibleMemorySize"},
		{"sceSystemServiceParamGetInt", "fZo48un7LK4", "shim_sceSystemServiceParamGetInt"},
		{"sceSystemServiceHideSplashScreen", "Vo5V8KAwCmk", "shim_sceSystemServiceHideSplashScreen"},
		{"sceAppContentInitialize", "R9lA82OraNs", "shim_sceAppContentInitialize"},
		{"sceAppContentAppParamGetInt", "99b82IKXpH4", "shim_sceAppContentAppParamGetInt"},
		{"sceAppContentTemporaryDataMount2", "buYbeLOGWmA", "shim_sceAppContentTemporaryDataMount2"},
		{"sceAppContentDownloadDataGetAvailableSpaceKb", "Gl6w5i0JokY", "shim_sceAppContentDownloadDataGetAvailableSpaceKb"},
		{"sceAppContentGetAddcontInfo", "m47juOmH0VE", "shim_sceAppContentGetAddcontInfo"},
		{"sceAppContentGetAddcontInfoList", "xnd8BJzAxmk", "shim_sceAppContentGetAddcontInfoList"},
		{"sceAppContentGetEntitlementKey", "XTWR0UXvcgs", "shim_sceAppContentGetEntitlementKey"},
		{"sceSaveDataInitialize3", "TywrFKCoLGY", "shim_sceSaveDataInitialize3"},
		{"sceSaveDataSetupSaveDataMemory2", "oQySEUfgXRA", "shim_sceSaveDataSetupSaveDataMemory2"},
		{"sceSaveDataGetSaveDataMemory2", "QwOO7vegnV8", "shim_sceSaveDataGetSaveDataMemory2"},
		{"sceSaveDataSetSaveDataMemory2", "cduy9v4YmT4", "shim_sceSaveDataSetSaveDataMemory2"},
		{"sceSaveDataSyncSaveDataMemory", "wiT9jeC7xPw", "shim_sceSaveDataSyncSaveDataMemory"},
		{"sceSigninDialogInitialize", "mlYGfmqE3fQ", "shim_sceSigninDialogInitialize"},
		{"sceSigninDialogOpen", "JlpJVoRWv7U", "shim_sceSigninDialogOpen"},
		{"sceSigninDialogUpdateStatus", "Bw31liTFT3A", "shim_sceSigninDialogUpdateStatus"},
		{"sceSigninDialogGetResult", "nqG7rqnYw1U", "shim_sceSigninDialogGetResult"},
		{"sceSigninDialogTerminate", "LXlmS6PvJdU", "shim_sceSigninDialogTerminate"},
		{"sceWebBrowserDialogInitialize", "jqb7HntFQFc", "shim_sceWebBrowserDialogInitialize"},
		{"sceWebBrowserDialogOpen", "FraP7debcdg", "shim_sceWebBrowserDialogOpen"},
		{"sceWebBrowserDialogUpdateStatus", "h1dR-t5ISgg", "shim_sceWebBrowserDialogUpdateStatus"},
		{"sceWebBrowserDialogTerminate", "ocHtyBwHfys", "shim_sceWebBrowserDialogTerminate"},
		{"sceImeKeyboardOpen", "eaFXjfJv3xs", "shim_sceImeKeyboardOpen"},
		{"sceImeKeyboardClose", "PMVehSlfZ94", "shim_sceImeKeyboardClose"},
		{"sceImeKeyboardGetInfo", "VkqLPArfFdc", "shim_sceImeKeyboardGetInfo"},
		{"sceImeKeyboardGetResourceId", "dKadqZFgKKQ", "shim_sceImeKeyboardGetResourceId"},
		{"sceImeUpdate", "-4GCfYdNF1s", "shim_sceImeUpdate"},
		{"sceNpRegisterStateCallbackForToolkit", "0c7HbXRKUt4", "shim_sceNpRegisterStateCallbackForToolkit"},
		{"sceNpUnregisterStateCallbackForToolkit", "YIvqqvJyjEc", "shim_sceNpUnregisterStateCallbackForToolkit"},
		{"sceNpCheckCallbackForLib", "JELHf4xPufo", "shim_sceNpCheckCallbackForLib"},
		{"sceNpCheckCallback", "3Zl8BePTh9Y", "shim_sceNpCheckCallback"},
		{"_ZN3sce4Json5Value11referObjectEv", "-NxEk7XLkDY", "shim__ZN3sce4Json5Value11referObjectEv"},
		{"_ZN3sce4Json5ValueC1El", "0lLK8+kDqmE", "shim__ZN3sce4Json5ValueC1El"},
		{"_ZNK3sce4Json5Value7getRealEv", "3qrge7L-AU4", "shim__ZNK3sce4Json5Value7getRealEv"},
		{"_ZN3sce4Json5ValueC1ERKNS0_6ObjectE", "3xUXnmUkXfo", "shim__ZN3sce4Json5ValueC1ERKNS0_6ObjectE"},
		{"_ZNK3sce4Json5Value10getIntegerEv", "DIxvoy7Ngvk", "shim__ZNK3sce4Json5Value10getIntegerEv"},
		{"_ZNK3sce4Json6String6lengthEv", "EUH+EmT-v9E", "shim__ZNK3sce4Json6String6lengthEv"},
		{"_ZN3sce4Json5Value3setENS0_9ValueTypeE", "IKQimvG9Wqs", "shim__ZN3sce4Json5Value3setENS0_9ValueTypeE"},
		{"_ZNK3sce4Json5Value9getObjectEv", "IlsmvBtMkak", "shim__ZNK3sce4Json5Value9getObjectEv"},
		{"_ZNK3sce4Json5Value8getArrayEv", "ONT8As5R1ug", "shim__ZNK3sce4Json5Value8getArrayEv"},
		{"_ZN3sce4Json5Value9serializeERNS0_6StringE", "R7FDWtcN6f8", "shim__ZN3sce4Json5Value9serializeERNS0_6StringE"},
		{"_ZNK3sce4Json5Value7getTypeEv", "SHtAad20YYM", "shim__ZNK3sce4Json5Value7getTypeEv"},
		{"_ZN3sce4Json5ValueC1Eb", "UeuWT+yNdCQ", "shim__ZN3sce4Json5ValueC1Eb"},
		{"_ZNK3sce4Json5ValueixEm", "XlWbvieLj2M", "shim__ZNK3sce4Json5ValueixEm"},
		{"_ZN3sce4Json5ValueC1EPKc", "b9V6fmppLXY", "shim__ZN3sce4Json5ValueC1EPKc"},
		{"_ZNK3sce4Json5Array4backEv", "bAM9Qwofus0", "shim__ZNK3sce4Json5Array4backEv"},
		{"_ZN3sce4Json6StringaSERKS1_", "cn9svYGWKDQ", "shim__ZN3sce4Json6StringaSERKS1_"},
		{"_ZN3sce4Json5Value10referArrayEv", "nM5XqdeXFPw", "shim__ZN3sce4Json5Value10referArrayEv"},
		{"_ZN3sce4Json6Object5clearEv", "oH8aBmLU+fc", "shim__ZN3sce4Json6Object5clearEv"},
		{"_ZN3sce4Json6StringC1Ev", "qSmqLXXCPas", "shim__ZN3sce4Json6StringC1Ev"},
		{"_ZNK3sce4Json5Array4sizeEv", "rQGJeNjOuUk", "shim__ZNK3sce4Json5Array4sizeEv"},
		{"_ZN3sce4Json5ValueC1Ed", "sOmU4vnx3s0", "shim__ZN3sce4Json5ValueC1Ed"},
		{"_ZN3sce4Json6ObjectaSERKS1_", "urOpESTBZmo", "shim__ZN3sce4Json6ObjectaSERKS1_"},
		{"_ZN3sce4Json5Array9push_backERKNS0_5ValueE", "zQtLRTqceMY", "shim__ZN3sce4Json5Array9push_backERKNS0_5ValueE"},
		{"sceNetEpollAbort", "w21YgGGNtBk", "shim_sceNetEpollAbort"},
		{"sceNetResolverGetError", "J5i3hiLJMPk", "shim_sceNetResolverGetError"},
		{"sceNetGetMemoryPoolStats", "rMyh97BU5pY", "shim_sceNetGetMemoryPoolStats"},
		{"sceNetCtlInit", "gky0+oaNM4k", "shim_sceNetCtlInit"},
		{"sceNetCtlGetInfo", "obuxdTiwkF8", "shim_sceNetCtlGetInfo"},
		{"sceNetCtlGetState", "uBPlr0lbuiI", "shim_sceNetCtlGetState"},
		{"sceNetCtlGetNatInfo", "JO4yuTuMoKI", "shim_sceNetCtlGetNatInfo"},
		{"sceNetCtlRegisterCallbackForNpToolkit", "wIsKy+TfeLs", "shim_sceNetCtlRegisterCallbackForNpToolkit"},
		{"sceNetCtlUnregisterCallbackForNpToolkit", "2oUqKR5odGc", "shim_sceNetCtlUnregisterCallbackForNpToolkit"},
		{"sceNetCtlCheckCallbackForNpToolkit", "u5oqtlIP+Fw", "shim_sceNetCtlCheckCallbackForNpToolkit"},
		{"sceHttpUriEscape", "YuOW3dDAKYc", "shim_sceHttpUriEscape"},
		{"sceHttpCreateConnection", "Kiwv9r4IZCc", "shim_sceHttpCreateConnection"},
		{"sceHttpCreateRequest2", "rGNm+FjIXKk", "shim_sceHttpCreateRequest2"},
		{"sceHttpSetConnectTimeOut", "0S9tTH0uqTU", "shim_sceHttpSetConnectTimeOut"},
		{"sceHttpSetSendTimeOut", "xegFfZKBVlw", "shim_sceHttpSetSendTimeOut"},
		{"sceHttpSetRecvTimeOut", "yigr4V0-HTM", "shim_sceHttpSetRecvTimeOut"},
		{"sceHttpSetRequestContentLength", "PTiFIUxCpJc", "shim_sceHttpSetRequestContentLength"},
		{"sceHttpSetChunkedTransferEnabled", "PDxS48xGQLs", "shim_sceHttpSetChunkedTransferEnabled"},
		{"sceHttpSetAuthEnabled", "qFg2SuyTJJY", "shim_sceHttpSetAuthEnabled"},
		{"sceHttpCookieFlush", "seCvUt91WHY", "shim_sceHttpCookieFlush"},
		{"sceHttpSetRedirectCallback", "h9wmFZX4i-4", "shim_sceHttpSetRedirectCallback"},
		{"sceHttpGetMemoryPoolStats", "16sMmVuOvgU", "shim_sceHttpGetMemoryPoolStats"},
		{"sceHttpsEnableOption", "f42K37mm5RM", "shim_sceHttpsEnableOption"},
		{"sceHttpsDisableOption", "mSQCxzWTwVI", "shim_sceHttpsDisableOption"},
		{"sceHttpsSetSslCallback", "htyBOoWeS58", "shim_sceHttpsSetSslCallback"},
		{"sceSslGetMemoryPoolStats", "-PoIzr3PEk0", "shim_sceSslGetMemoryPoolStats"},
		{"sceSslGetNameEntryInfo", "7RBSTKGrmDA", "shim_sceSslGetNameEntryInfo"},
		{"sceSslGetIssuerName", "7whYpYfHP74", "shim_sceSslGetIssuerName"},
		{"sceSslGetSerialNumber", "DOwXL+FQMEY", "shim_sceSslGetSerialNumber"},
		{"sceSslGetNameEntryCount", "R1ePzopYPYM", "shim_sceSslGetNameEntryCount"},
		{"sceSslFreeSslCertName", "RwXD8grHZHM", "shim_sceSslFreeSslCertName"},
		{"sceSslGetSubjectName", "dQReuBX9sD8", "shim_sceSslGetSubjectName"},
		{"sceSslGetPem", "kLB5aGoUJXg", "shim_sceSslGetPem"},
		{"sceSslFreeCaCerts", "qIvLs0gYxi0", "shim_sceSslFreeCaCerts"},
		{"_ZNK3sce4Json5Value10getBooleanEv", "zTwZdI8AZ5Y", "shim__ZNK3sce4Json5Value10getBooleanEv"},
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
	if shim, ok := LookupShim("fJgP+wqifno#A#B"); !ok || shim != "shim_Func_7C980FFB0AA27E7A" {
		t.Fatalf("LookupShim(fJgP+wqifno#A#B) = %q, %v; want shim_Func_7C980FFB0AA27E7A", shim, ok)
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

func TestLibkernelNewShims(t *testing.T) {
	cases := []struct {
		plain string
		nid   string
		shim  string
	}{
		{"sceKernelSetVirtualRangeName", "DGMG3JshrZU", "shim_sceKernelSetVirtualRangeName"},
		{"sceKernelDirectMemoryQuery", "BHouLQzh0X0", "shim_sceKernelDirectMemoryQuery"},
		{"sceKernelCheckedReleaseDirectMemory", "hwVSPCmp5tM", "shim_sceKernelCheckedReleaseDirectMemory"},
		{"sceKernelMprotect", "vSMAm3cxYTY", "shim_sceKernelMprotect"},
		{"sceKernelCreateSema", "188x57JYp0g", "shim_sceKernelCreateSema"},
		{"sceKernelDeleteSema", "R1Jvn8bSCW8", "shim_sceKernelDeleteSema"},
		{"sceKernelWaitSema", "Zxa0VhQVTsk", "shim_sceKernelWaitSema"},
		{"sceKernelSignalSema", "4czppHBiriw", "shim_sceKernelSignalSema"},
		{"scePthreadSemInit", "GEnUkDZoUwY", "shim_scePthreadSemInit"},
		{"scePthreadSemDestroy", "Vwc+L05e6oE", "shim_scePthreadSemDestroy"},
		{"scePthreadSemWait", "C36iRE0F5sE", "shim_scePthreadSemWait"},
		{"scePthreadSemTrywait", "H2a+IN9TP0E", "shim_scePthreadSemTrywait"},
		{"scePthreadSemTimedwait", "fjN6NQHhK8k", "shim_scePthreadSemTimedwait"},
		{"scePthreadSemPost", "aishVAiFaYM", "shim_scePthreadSemPost"},
		{"scePthreadAttrGetstacksize", "-fA+7ZlGDQs", "shim_scePthreadAttrGetstacksize"},
		{"scePthreadAttrSetstack", "Bvn74vj6oLo", "shim_scePthreadAttrSetstack"},
		{"scePthreadRename", "GBUY7ywdULE", "shim_scePthreadRename"},
		{"scePthreadGetschedparam", "P41kTWUS3EI", "shim_scePthreadGetschedparam"},
		{"scePthreadSetschedparam", "oIRFTjoILbg", "shim_scePthreadSetschedparam"},
		{"sceKernelCancelEventFlag", "PZku4ZrXJqg", "shim_sceKernelCancelEventFlag"},
		{"sceKernelGetEventId", "mJ7aghmgvfc", "shim_sceKernelGetEventId"},
		{"sceKernelGetEventFilter", "23CPPI1tyBY", "shim_sceKernelGetEventFilter"},
		{"sceKernelDeleteUserEvent", "LJDwdSNTnDg", "shim_sceKernelDeleteUserEvent"},
		{"sceKernelClockGettime", "QBi7HCK03hw", "shim_sceKernelClockGettime"},
		{"sceKernelUuidCreate", "Xjoosiw+XPI", "shim_sceKernelUuidCreate"},
		{"sceKernelSetGPO", "ca7v6Cxulzs", "shim_sceKernelSetGPO"},
		{"sceKernelIsProspero", "mpxAdqW7dKY", "shim_sceKernelIsProspero"},
	}
	for _, tc := range cases {
		// Test plain name resolution
		shim, ok := LookupShim(tc.plain)
		if !ok || shim != tc.shim {
			t.Errorf("LookupShim(%s)=%q, ok=%v, want %s", tc.plain, shim, ok, tc.shim)
		}
		// Test NID#lib#mod encoded resolution
		encoded := tc.nid + "#p#O"
		shimNid, okNid := LookupShim(encoded)
		if !okNid || shimNid != tc.shim {
			t.Errorf("LookupShim(%s)=%q, ok=%v, want %s", encoded, shimNid, okNid, tc.shim)
		}
	}
}

func TestBatchKernelAndPthreadShims(t *testing.T) {
	cases := []struct {
		plain string
		nid   string
		shim  string
	}{
		{"scePthreadOnce", "14bOACANTBo", "shim_scePthreadOnce"},
		{"pthread_once", "Z4QosVuAsA0", "shim_pthread_once"},
		{"scePthreadMutexTimedlock", "IafI2PxcPnQ", "shim_scePthreadMutexTimedlock"},
		{"pthread_mutex_timedlock", "Io9+nTKXZtA", "shim_pthread_mutex_timedlock"},
		{"scePthreadAttrGetaffinity", "8+s5BzZjxSg", "shim_scePthreadAttrGetaffinity"},
		{"scePthreadAttrGetdetachstate", "JaRMy+QcpeU", "shim_scePthreadAttrGetdetachstate"},
		{"scePthreadAttrGet", "x1X76arYMxU", "shim_scePthreadAttrGet"},
		{"scePthreadGetname", "How7B8Oet6k", "shim_scePthreadGetname"},
		{"pthread_cancel", "0D4-FVvEikw", "shim_pthread_cancel"},
		{"pthread_mutexattr_setprotocol", "5txKfcMUAok", "shim_pthread_mutexattr_setprotocol"},
		{"sceKernelMkdir", "1-LFLmRFxxM", "shim_mkdir"},
		{"rmdir", "c7ZnT7V1B98", "shim_rmdir"},
		{"sceKernelRename", "52NcYU9+lEo", "shim_rename"},
		{"sceKernelUnlink", "AUXVxWeJU-A", "shim_unlink"},
		{"sceKernelTruncate", "WlyEA-sLDf0", "shim_truncate"},
		{"sceKernelMlock", "3k6kx-zOOSQ", "shim_mlock"},
		{"__pthread_cxa_finalize", "kbw4UHHSYy0", "shim___pthread_cxa_finalize"},
		{"sceKernelConvertUtcToLocaltime", "-o5uEDpN+oY", "shim_sceKernelConvertUtcToLocaltime"},
		{"sceKernelConvertLocaltimeToUtc", "0NTHN1NKONI", "shim_sceKernelConvertLocaltimeToUtc"},
		{"sceKernelInternalMemoryGetModuleSegmentInfo", "-YTW+qXc3CQ", "shim_sceKernelInternalMemoryGetModuleSegmentInfo"},
		{"sceKernelGetModuleInfoForUnwind", "RpQJJVKTiFM", "shim_sceKernelGetModuleInfoForUnwind"},
		{"sceKernelGetModuleInfoFromAddr", "f7KBOafysXo", "shim_sceKernelGetModuleInfoFromAddr"},
		{"sceKernelPrintBacktraceWithModuleInfo", "Wl2o5hOVZdw", "shim_sceKernelPrintBacktraceWithModuleInfo"},
		{"sceKernelDebugRaiseException", "OMDRKKAZ8I4", "shim_sceKernelDebugRaiseException"},
		{"sceKernelDebugRaiseExceptionOnReleaseMode", "zE-wXIZjLoM", "shim_sceKernelDebugRaiseExceptionOnReleaseMode"},
		{"sceKernelRaiseException", "il03nluKfMk", "shim_sceKernelRaiseException"},
		{"sceKernelInstallExceptionHandler", "WkwEd3N7w0Y", "shim_sceKernelInstallExceptionHandler"},
		{"_sceKernelRtldThreadAtexitDecrement", "8OnWXlgQlvo", "shim__sceKernelRtldThreadAtexitDecrement"},
		{"_sceKernelRtldThreadAtexitIncrement", "Tz4RNUCBbGI", "shim__sceKernelRtldThreadAtexitIncrement"},
		{"_sceKernelSetThreadAtexitReport", "WhCc1w3EhSI", "shim__sceKernelSetThreadAtexitReport"},
		{"_sceKernelSetThreadAtexitCount", "pB-yGZ2nQ9o", "shim__sceKernelSetThreadAtexitCount"},
		{"_sceKernelSetThreadDtors", "rNhWz+lvOMU", "shim__sceKernelSetThreadDtors"},
		{"_sceKernelRtldSetApplicationHeapAPI", "p5EcQeEeJAE", "shim__sceKernelRtldSetApplicationHeapAPI"},
		{"bnZxYgAFeA0", "bnZxYgAFeA0", "shim_sceKernelGetSanitizerNewReplaceExternal"},
		{"py6L8jiVAN8", "py6L8jiVAN8", "shim_sceKernelGetSanitizerMallocReplaceExternal"},
		{"sceKernelIsAddressSanitizerEnabled", "jh+8XiK4LeE", "shim_sceKernelIsAddressSanitizerEnabled"},
		{"__elf_phdr_match_addr", "Fjc4-n1+y2g", "shim___elf_phdr_match_addr"},
		{"signal", "VADc3MNQ3cM", "shim_signal"},
		{"_is_signal_return", "crb5j7mkk1c", "shim__is_signal_return"},
		{"__progname", "djxxOmW6-aw", "shim___progname"},
		{"getargv", "FJmglmTMdr4", "shim_getargv"},
		{"getpagesize", "k+AXqu2-eBc", "shim_getpagesize"},
		{"sceDiscMapIsRequestOnHDD", "lbQKqsERhtE", "shim_sceDiscMapIsRequestOnHDD"},
		{"sceRtcGetTime_t", "BtqmpTRXHgk", "shim_sceRtcGetTime_t"},
		{"sceRtcParseDateTime", "NxEI1KByvCI", "shim_sceRtcParseDateTime"},
		{"sceRtcGetCurrentNetworkTick", "zO9UL3qIINQ", "shim_sceRtcGetCurrentNetworkTick"},
	}
	for _, tc := range cases {
		shim, ok := LookupShim(tc.plain)
		if !ok || shim != tc.shim {
			t.Errorf("LookupShim(%s)=%q, ok=%v, want %s", tc.plain, shim, ok, tc.shim)
		}
		encoded := tc.nid + "#A#B"
		shimNid, okNid := LookupShim(encoded)
		if !okNid || shimNid != tc.shim {
			t.Errorf("LookupShim(%s)=%q, ok=%v, want %s", encoded, shimNid, okNid, tc.shim)
		}
	}
}
