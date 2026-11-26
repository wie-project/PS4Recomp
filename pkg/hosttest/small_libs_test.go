// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

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
		[]string{"-framework", "AudioToolbox"},
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
		[]string{"-framework", "ImageIO", "-framework", "CoreGraphics", "-framework", "CoreFoundation"},
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
		"lifecycle",
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
		[]string{linkZlib, "-framework", "ImageIO", "-framework", "CoreGraphics", "-framework", "CoreFoundation"},
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
	runHostCases(t, bin, []string{"lifecycle", "busy"})
}

// TestFiber runs testdata/fiber/main.c: libSceFiber argument checks in shadPS4's order, the opt
// param magic, context size checking and the margin sceFiberGetInfo measures, attaching a
// context, the FPU control words a fiber starts with, coroutine Run / Switch / ReturnToThread,
// and the stack overflow check on a switch.
func TestFiber(t *testing.T) {
	bin := buildHostCFlags(t, "fiber",
		[]string{"-I../runtime/kernel/threading"},
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
