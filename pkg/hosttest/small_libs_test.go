// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestMouse runs testdata/mouse/main.c: libSceMouse handle rules, error codes and the input
// report queue fed by the host pointer.
func TestMouse(t *testing.T) {
	bin := buildHostCFlags(t, "mouse",
		[]string{"-I../runtime/modules/libSceUserService"},
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
		[]string{"-I../runtime/modules/libSceUserService"},
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
