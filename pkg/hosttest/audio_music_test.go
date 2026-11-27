// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestAudio3d runs testdata/audio3d/main.c: the libSceAudio3d entry points added from shadPS4's
// audio3d.cpp (PortCreate, ObjectSetAttribute, AudioOutOutput) over a recording AudioOut, and
// the functions shadPS4 stubs.
func TestAudio3d(t *testing.T) {
	bin := buildHostCFlags(t, "audio3d",
		[]string{"-I../runtime/modules/libSceAudioOut"},
		"../runtime/modules/libSceAudio3d/ps4_audio3d.c",
		"../runtime/modules/libSceAudio3d/ps4_audio3d_mix.c",
	)
	runHostCases(t, bin, []string{
		"port-create",
		"object-set-attribute",
		"audioout-output",
		"stubs",
	})
}

// TestMusicPlayerService runs testdata/music_player/main.c: libSceMusicPlayerService decoding
// FLAC and AAC files written by AudioToolbox, the track list and transaction rules, the status
// queries, and playback through the headless output (PS4_AUDIO_NULL): URIs with schemes,
// escapes and USB serials, auto-advance, repeat, pause, seek and the end of the list.
func TestMusicPlayerService(t *testing.T) {
	bin := buildHostCFlags(t, "music_player",
		[]string{frameworkFlag, "AudioToolbox", frameworkFlag, coreFoundation},
		"../runtime/modules/libSceMusicPlayerService/ps4_music_player_service.c",
		"../runtime/modules/libSceMusicPlayerService/ps4_music_player_audio.c",
	)
	runHostCases(t, bin, []string{
		"decode",
		"lists",
		"status",
		"playback",
	})
}
