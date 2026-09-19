package cli

import (
	"errors"
	"flag"
	"testing"
)

func TestParseArgs(t *testing.T) {
	tests := []struct {
		name        string
		args        []string
		wantElf     string
		wantOut     string
		wantCompile bool
		wantRun     bool
		wantTimeout int
		wantOpt     string
		wantErr     bool
		errIs       error
	}{
		{
			name:        "Positional ELF path",
			args:        []string{"game.elf"},
			wantElf:     "game.elf",
			wantOut:     "build",
			wantCompile: true,
			wantRun:     false,
			wantOpt:     "2",
		},
		{
			name:        "Positional ELF with following flags",
			args:        []string{"game.elf", "-o", "custom_dir", "-r", "-t", "7"},
			wantElf:     "game.elf",
			wantOut:     "custom_dir",
			wantCompile: true,
			wantRun:     true,
			wantTimeout: 7,
			wantOpt:     "2",
		},
		{
			name:        "Flag ELF and Out",
			args:        []string{"-elf", "app.elf", "-out", "custom_build", "-r", "-t", "5", "-O", "3"},
			wantElf:     "app.elf",
			wantOut:     "custom_build",
			wantCompile: true,
			wantRun:     true,
			wantTimeout: 5,
			wantOpt:     "3",
		},
		{
			name:        "Short flags",
			args:        []string{"-e", "short.elf", "-o", "bin_out", "-c=false"},
			wantElf:     "short.elf",
			wantOut:     "bin_out",
			wantCompile: false,
			wantRun:     false,
			wantOpt:     "2",
		},
		{
			name:    "Missing ELF argument",
			args:    []string{"-o", "build"},
			wantErr: true,
		},
		{
			name:    "Help flag",
			args:    []string{"--help"},
			wantErr: true,
			errIs:   flag.ErrHelp,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			cfg, err := ParseArgs(tt.args)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("expected error, got nil")
				}
				if tt.errIs != nil && !errors.Is(err, tt.errIs) {
					t.Fatalf("expected error %v, got %v", tt.errIs, err)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if cfg.ElfPath != tt.wantElf {
				t.Errorf("ElfPath = %s, want %s", cfg.ElfPath, tt.wantElf)
			}
			if cfg.OutDir != tt.wantOut {
				t.Errorf("OutDir = %s, want %s", cfg.OutDir, tt.wantOut)
			}
			if cfg.Compile != tt.wantCompile {
				t.Errorf("Compile = %v, want %v", cfg.Compile, tt.wantCompile)
			}
			if cfg.Run != tt.wantRun {
				t.Errorf("Run = %v, want %v", cfg.Run, tt.wantRun)
			}
			if cfg.TimeoutSec != tt.wantTimeout {
				t.Errorf("TimeoutSec = %d, want %d", cfg.TimeoutSec, tt.wantTimeout)
			}
			if cfg.OptLevel != tt.wantOpt {
				t.Errorf("OptLevel = %s, want %s", cfg.OptLevel, tt.wantOpt)
			}
		})
	}
}
