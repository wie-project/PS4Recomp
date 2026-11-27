// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

const (
	frameworkFlag  = "-framework"
	coreFoundation = "CoreFoundation"
	// caseLifecycle is the host test case of a library's initialize / terminate rules.
	caseLifecycle = "lifecycle"
)

// TestMouse runs testdata/mouse/main.c: libSceMouse handle rules, error codes and the input
// report queue fed by the host pointer.
func TestMouse(t *testing.T) {
	bin := buildHostCFlags(t, "mouse",
		[]string{userServiceInclude},
		"../runtime/modules/libSceMouse/ps4_mouse.c",
	)
	runHostCases(t, bin, []string{
		"open-close",
		"read-disabled",
		"read-host",
		"report-before-init",
	})
}

// TestAudioIn runs testdata/audioin/main.c: libSceAudioIn argument validation, handle format,
// port limit and reads from a port without a capture device (PS4_AUDIO_IN=none).
func TestAudioIn(t *testing.T) {
	bin := buildHostCFlags(t, "audioin",
		[]string{frameworkFlag, "AudioToolbox"},
		"../runtime/modules/libSceAudioIn/ps4_audioin.c",
	)
	runHostCases(t, bin, []string{
		"open-validate",
		"input-null-device",
	})
}

// TestRandomSharePlay runs testdata/random_shareplay/main.c: sceRandomGetRandomNumber limits and
// the Share Play connection info queries.
func TestRandomSharePlay(t *testing.T) {
	bin := buildHostC(t, "random_shareplay",
		"../runtime/modules/libSceRandom/ps4_random.c",
		"../runtime/modules/libSceSharePlay/ps4_shareplay.c",
	)
	runHostCases(t, bin, []string{
		"random",
		"shareplay-info",
	})
}

// TestRtc runs testdata/rtc/main.c: libSceRtc ticks since year 1, calendar arithmetic, the
// time_t / FILETIME / DOS conversions, RFC 3339 and RFC 2822 formatting and parsing.
func TestRtc(t *testing.T) {
	bin := buildHostC(t, "rtc", "../runtime/modules/libSceRtc/ps4_rtc.c")
	runHostCases(t, bin, []string{
		"ticks",
		"validation",
		"tick-add",
		"conversions",
		"format-parse",
		"set-clock",
	})
}

// TestJpeg runs testdata/jpeg/main.c: libSceJpegEnc and libSceJpegDec parameter checks and
// handles, and encode -> parse header -> decode round trips in every supported pixel format.
func TestJpeg(t *testing.T) {
	bin := buildHostCFlags(t, "jpeg",
		[]string{frameworkFlag, "ImageIO", frameworkFlag, "CoreGraphics", frameworkFlag, coreFoundation},
		"../runtime/modules/libSceJpegDec/ps4_jpegdec.c",
		"../runtime/modules/libSceJpegEnc/ps4_jpegenc.c",
	)
	runHostCases(t, bin, []string{
		"enc-checks",
		"dec-checks",
		"round-trip",
		"enc-formats",
	})
}

// TestGameCustomDataDialog runs testdata/gcdd/main.c: the common dialog system rules, the
// parameter validation, the request the window shows, and the results. The AppKit window is
// replaced by recording hooks.
func TestGameCustomDataDialog(t *testing.T) {
	bin := buildHostCFlags(t, "gcdd",
		[]string{userServiceInclude},
		"../runtime/modules/libSceMsgDialog/ps4_common_dialog.c",
		"../runtime/modules/libSceGameCustomDataDialog/ps4_game_custom_data_dialog.c",
	)
	runHostCases(t, bin, []string{
		caseLifecycle,
		"validation",
		"send-cancel",
		"close-ok",
		"headless",
	})
}

// TestSysmodule runs testdata/sysmodule/main.c: libSceSysmodule over the real module loader —
// ID checks, dependencies and load counts, game modules started with their initializers, the
// libkernel preload and the module names hidden from unwinding.
func TestSysmodule(t *testing.T) {
	bin := buildHostCFlags(t, "sysmodule", []string{kernelInclude},
		"../runtime/modules/libkernel/ps4_sysmodule_lib.c",
		"../runtime/modules/libkernel/ps4_sysmodule.c",
	)
	runHostCases(t, bin, []string{
		"ids",
		"deps",
		"game",
		"preload",
		"hidden-names",
	})
}

// TestPngDec runs testdata/pngdec/main.c: libScePngDec on PNGs of every color type and bit
// depth, plain and Adam7-interlaced (pixel-exact against the written samples, the writer
// checked against ImageIO), output layout and the error cases; and libScePngEnc output read back
// by ImageIO and the decoder, with the row filters each filter mask allows.
func TestPngDec(t *testing.T) {
	bin := buildHostCFlags(t, "pngdec",
		[]string{linkZlib, frameworkFlag, "ImageIO", frameworkFlag, "CoreGraphics", frameworkFlag, coreFoundation},
		"../runtime/modules/libScePngDec/ps4_pngdec.c",
	)
	runHostCases(t, bin, []string{
		"create",
		"header",
		"writer",
		"formats",
		"layout",
		"encode-round-trip",
	})
}

// TestVoice runs testdata/voice/main.c: libSceVoice initialization, the 64-port table, port
// settings and attributes, and the data calls of a library without voice chat.
func TestVoice(t *testing.T) {
	bin := buildHostCFlags(t, "voice", []string{"-I../runtime/modules/libSceAudioIn"},
		"../runtime/modules/libSceVoice/ps4_voice.c",
	)
	runHostCases(t, bin, []string{
		"init",
		"ports",
		"settings",
		"attrs",
		"data",
	})
}

// TestWebBrowserDialog runs testdata/webbrowserdialog/main.c: the libSceWebBrowserDialog
// lifecycle on the CommonDialog system and the dialog result.
func TestWebBrowserDialog(t *testing.T) {
	bin := buildHostC(t, "webbrowserdialog",
		"../runtime/modules/libSceWebBrowserDialog/ps4_web_browser_dialog.c",
		"../runtime/modules/libSceMsgDialog/ps4_common_dialog.c",
	)
	runHostCases(t, bin, []string{caseLifecycle, "busy"})
}

// TestFiber runs testdata/fiber/main.c: libSceFiber argument checks in shadPS4's order, the opt
// param magic, context size checking and the margin sceFiberGetInfo measures, attaching a
// context, the FPU control words a fiber starts with, coroutine Run / Switch / ReturnToThread,
// and the stack overflow check on a switch.
func TestFiber(t *testing.T) {
	bin := buildHostCFlags(t, "fiber",
		[]string{threadingInclude},
		"../runtime/modules/libSceFiber/ps4_fiber.c",
	)
	runHostCases(t, bin, []string{
		"init-checks",
		"context-size-check",
		"attach",
		"run-switch",
		"overflow",
	})
}

// TestVideoRecordingRudp runs testdata/videorecording_rudp/main.c: the info values
// sceVideoRecordingSetInfo accepts, the VideoRecording functions shadPS4 leaves to its
// unresolved stub, and the libSceRudp stubs.
func TestVideoRecordingRudp(t *testing.T) {
	bin := buildHostC(t, "videorecording_rudp",
		"../runtime/modules/libSceVideoRecording/ps4_video_recording.c",
		"../runtime/modules/libSceRudp/ps4_rudp.c",
	)
	runHostCases(t, bin, []string{
		"video-recording",
		"rudp",
	})
}

// TestHmd runs testdata/hmd/main.c: libSceHmd on a host without a PlayStation VR headset, as
// in shadPS4 (state machine, firmware 4.50 answers, memory requirements as return values).
func TestHmd(t *testing.T) {
	bin := buildHostC(t, "hmd", "../runtime/modules/libSceHmd/ps4_hmd.c")
	runHostCases(t, bin, []string{
		"state-machine",
		"queries",
		"shims",
	})
}

// TestNetCtl runs testdata/netctl/main.c: libSceNetCtl as in shadPS4, disconnected by default
// and connected with PS4_NETWORK=1 (host address, netmask, gateway), callback ids and the state
// delivered once per change by sceNetCtlCheckCallback.
func TestNetCtl(t *testing.T) {
	bin := buildHostC(t, "netctl", "../runtime/modules/libSceNetCtl/ps4_netctl.c")
	runHostCases(t, bin, []string{
		"disconnected",
		"connected",
	})
}

// TestVideoCoredump runs testdata/video_coredump/main.c: libSceCoredump handler registration,
// libSceVideoCoreServerInterface resource bookkeeping, the libSceVideodec / libSceVideodec2
// argument, struct size and handle checks and memory queries, and the blocking libSceVdecsw
// SyncDecodeInput / SyncDecodeOutput calls and picture info entry points.
func TestVideoCoredump(t *testing.T) {
	bin := buildHostCFlags(t, "video_coredump",
		[]string{frameworkFlag, "VideoToolbox", frameworkFlag, "CoreMedia", frameworkFlag, "CoreVideo",
			frameworkFlag, coreFoundation},
		"../runtime/modules/libSceCoredump/ps4_coredump.c",
		"../runtime/modules/libSceVideoCoreServerInterface/ps4_videocore_server.c",
		"../runtime/modules/libSceVideodec/ps4_videodec.c",
		"../runtime/modules/libSceVideodec/ps4_h264_vt.c",
		"../runtime/modules/libSceVideodec/ps4_hevc_parse.c",
		"../runtime/modules/libSceVideodec2/ps4_videodec2.c",
		"../runtime/modules/libSceVdecsw/ps4_vdecsw.c",
	)
	runHostCases(t, bin, []string{
		"coredump",
		"videocore-server",
		"videodec",
		"videodec2",
		"vdecsw-sync",
	})
}

// TestSaveDataDialog runs testdata/savedata_dialog/main.c: libSceSaveDataDialog's common dialog
// system rules, the check order of Open and GetResult, Close and the progress bar calls, with
// automatic answers instead of the AppKit window.
func TestSaveDataDialog(t *testing.T) {
	bin := buildHostCFlags(t, "savedata_dialog",
		[]string{"-I../runtime/modules/libSceSaveData", "-fobjc-arc", frameworkFlag, "Cocoa"},
		"../runtime/modules/libSceMsgDialog/ps4_common_dialog.c",
		"../runtime/modules/libSceSaveDataDialog/ps4_savedata_dialog.m",
	)
	runHostCases(t, bin, []string{
		caseLifecycle,
		"open-result-order",
		"progress-bar",
	})
}

// TestHevcDecode runs testdata/hevc_decode/main.c: an HEVC stream made with VideoToolbox's
// encoder (B-frames, conformance window) through the HEVC parser, libSceVideodec2,
// libSceVdecsw and libSceVdecCore, and an H.264 one through libSceVdecCore: pictures in display
// order with the right content, the picture info, and VdecCore's frame-buffer slots and errors.
func TestHevcDecode(t *testing.T) {
	bin := buildHostCFlags(t, "hevc_decode",
		[]string{frameworkFlag, "VideoToolbox", frameworkFlag, "CoreMedia", frameworkFlag, "CoreVideo",
			frameworkFlag, coreFoundation},
		"../runtime/modules/libSceVideodec/ps4_h264_vt.c",
		"../runtime/modules/libSceVideodec/ps4_hevc_parse.c",
		"../runtime/modules/libSceVideodec2/ps4_videodec2.c",
		"../runtime/modules/libSceVdecsw/ps4_vdecsw.c",
		"../runtime/modules/libSceVdecCore/ps4_vdeccore.c",
	)
	runHostCases(t, bin, []string{
		"parse",
		"videodec2",
		"vdecsw",
		"vdeccore-hevc",
		"vdeccore-avc",
	})
}
