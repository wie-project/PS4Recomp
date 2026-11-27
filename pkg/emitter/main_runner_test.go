// SPDX-License-Identifier: GPL-2.0-or-later

package emitter

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"ps4-recomp/pkg/elfloader"
)

func TestEmitMainRunnerPassesResourceCopyPreference(t *testing.T) {
	tests := []struct {
		name          string
		copyResources bool
		want          string
	}{
		{name: "resources not copied", want: `ps4_vfs_init("/game", true);`},
		{name: "resources copied", copyResources: true, want: `ps4_vfs_init("/game", false);`},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			e := &AsmEmitter{
				elf:           &elfloader.LoadedELF{EntryPoint: 0x1000},
				AppDir:        "/game",
				CopyResources: tt.copyResources,
			}
			path := filepath.Join(t.TempDir(), "main.c")
			if err := e.EmitMainRunner(path); err != nil {
				t.Fatal(err)
			}
			source, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			if got := strings.Count(string(source), tt.want); got != 2 {
				t.Fatalf("generated main.c contains %d calls matching %q, want 2", got, tt.want)
			}
		})
	}
}
