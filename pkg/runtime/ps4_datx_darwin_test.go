//go:build darwin

// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"bytes"
	"crypto/md5"
	"encoding/binary"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"testing"
)

func TestDriveClubDATXFilesThroughVFS(t *testing.T) {
	t.Setenv("PS4_APP_DIR", "")
	clang, err := exec.LookPath("clang")
	if err != nil {
		t.Skip("clang is required for the native DATX VFS test")
	}

	root := t.TempDir()
	writeDATXFixture(t, root)
	harness := filepath.Join(t.TempDir(), "datx_vfs_test.c")
	const source = `
#include "ps4_vfs.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    ps4_vfs_init(argv[1], true);

    if (argc == 5 && strcmp(argv[2], "--extract") == 0) {
        int fd = ps4_vfs_open(argv[3], O_RDONLY, 0);
        if (fd < 0) return 30;
        struct stat file_stat;
        if (ps4_vfs_fstat(fd, &file_stat) != 0) return 31;
        FILE *out = fopen(argv[4], "wb");
        if (!out) return 32;
        unsigned char buffer[65536];
        off_t total = 0;
        while (total < file_stat.st_size) {
            size_t amount = sizeof(buffer);
            if ((off_t)amount > file_stat.st_size - total)
                amount = (size_t)(file_stat.st_size - total);
            ssize_t got = ps4_vfs_read(fd, buffer, amount);
            if (got <= 0 || fwrite(buffer, 1, (size_t)got, out) != (size_t)got)
                return 33;
            total += got;
        }
        if (fclose(out) != 0 || ps4_vfs_close(fd) != 0) return 34;
        ps4_vfs_destroy();
        return 0;
    }

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            int fd = ps4_vfs_open(argv[i], O_RDONLY, 0);
            if (fd < 0) {
                fprintf(stderr, "PROBE_MISSING %s errno=%d\n", argv[i], errno);
                continue;
            }
            struct stat file_stat;
            if (ps4_vfs_fstat(fd, &file_stat) != 0) return 21;
            unsigned char header[8];
            ssize_t got = ps4_vfs_pread(fd, header, sizeof(header), 0);
            if (got < 0) return 22;
            printf("PROBE %s size=%lld first_bytes=", argv[i],
                   (long long)file_stat.st_size);
            for (ssize_t j = 0; j < got; j++) printf("%02x", header[j]);
            if (file_stat.st_size <= 4 * 1024 * 1024) {
                unsigned char buffer[65536];
                off_t position = 0;
                while (position < file_stat.st_size) {
                    size_t amount = sizeof(buffer);
                    if ((off_t)amount > file_stat.st_size - position)
                        amount = (size_t)(file_stat.st_size - position);
                    ssize_t read_size = ps4_vfs_pread(fd, buffer, amount, position);
                    if (read_size <= 0) return 24;
                    position += read_size;
                }
                void *mapped = ps4_vfs_mmap(NULL, (size_t)file_stat.st_size,
                                            PROT_READ, MAP_PRIVATE, fd, 0);
                if (mapped == MAP_FAILED ||
                    memcmp(mapped, header, sizeof(header)) != 0) return 25;
                munmap(mapped, (size_t)file_stat.st_size);
                printf(" verified_read=%lld mmap=ok", (long long)position);
            }
            printf("\n");
            if (ps4_vfs_close(fd) != 0) return 23;
        }
        ps4_vfs_destroy();
        return 0;
    }

    struct stat st;
    if (ps4_vfs_stat("/app0/newui", &st) != 0 || !S_ISDIR(st.st_mode)) return 2;
    if (ps4_vfs_stat("/app0/newui/main_menu.rpk", &st) != 0 || st.st_size != 8) return 3;
    if (ps4_vfs_access("/app0/newui/main_menu.rpk", F_OK) != 0) return 4;

    int fd = ps4_vfs_open("/app0/NEWUI\\main_menu.rpk", O_RDONLY, 0);
    if (fd < 0 || !ps4_vfs_is_virtual_fd(fd)) return 5;
    char header[8];
    if (ps4_vfs_read(fd, header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "EVOSLITL", sizeof(header)) != 0) return 6;
    if (ps4_vfs_lseek(fd, 0, SEEK_SET) != 0 ||
        ps4_vfs_pread(fd, header, sizeof(header), 0) != sizeof(header) ||
        memcmp(header, "EVOSLITL", sizeof(header)) != 0) return 7;
    if (ps4_vfs_close(fd) != 0) return 8;

    fd = ps4_vfs_open("/app0/boot.ctl", O_RDONLY, 0);
    if (fd < 0) return 9;
    char zeros[16];
    memset(zeros, 0xff, sizeof(zeros));
    if (ps4_vfs_read(fd, zeros, sizeof(zeros)) != sizeof(zeros)) return 10;
    for (size_t i = 0; i < sizeof(zeros); i++) if (zeros[i] != 0) return 11;
    if (ps4_vfs_close(fd) != 0) return 12;

    ps4_vfs_destroy();
    return 0;
}
`
	if err := os.WriteFile(harness, []byte(source), 0o600); err != nil {
		t.Fatal(err)
	}
	bin := filepath.Join(t.TempDir(), "datx_vfs_test")
	cmd := exec.Command(clang, "-std=gnu11", "-pthread", "-Ikernel/vfs",
		harness, "kernel/vfs/ps4_vfs.c", "kernel/vfs/ps4_pak.c",
		"kernel/vfs/ps4_datx.c", "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("build DATX VFS test: %v\n%s", err, out)
	}
	run := exec.Command(bin, root)
	if out, err := run.CombinedOutput(); err != nil {
		t.Fatalf("run DATX VFS test: %v\n%s", err, out)
	}

	if installedRoot := os.Getenv("PS4_DATX_TEST_ROOT"); installedRoot != "" {
		probe := exec.Command(bin, installedRoot,
			"/app0/newui/main_menu.rpk",
			"/app0/newui/art/main_menu.rpk",
			"/app0/boot.ctl",
			"/app0/newui/pages/boot.ctl",
			"/app0/newui/art/dc_attract_video_30fps_11.v3_256.mp4")
		if out, err := probe.CombinedOutput(); err != nil {
			t.Fatalf("probe installed DriveClub DATX archive: %v\n%s", err, out)
		} else {
			t.Logf("installed archive probe:\n%s", out)
		}
		extracted := filepath.Join(t.TempDir(), "main_menu.rpk")
		extract := exec.Command(bin, installedRoot, "--extract",
			"/app0/newui/art/main_menu.rpk", extracted)
		if out, err := extract.CombinedOutput(); err != nil {
			t.Fatalf("read full main-menu resource: %v\n%s", err, out)
		}
		data, err := os.ReadFile(extracted)
		if err != nil {
			t.Fatal(err)
		}
		if got, want := md5.Sum(data), datxRecordMD5(t, installedRoot, "newui/art/main_menu.rpk"); !bytes.Equal(got[:], want) {
			t.Fatalf("main-menu resource MD5 mismatch: got %x want %x", got, want)
		}
	}
}

func writeDATXFixture(t *testing.T, root string) {
	t.Helper()
	const (
		chunkSize = 0x10000
		filetime  = uint64(0x01d215ac2250c60e)
	)

	names := []string{"newui/main_menu.rpk", "boot.ctl"}
	entries := []struct {
		name   string
		offset uint64
		data   []byte
	}{
		{name: names[0], offset: 0, data: []byte("EVOSLITL")},
		{name: names[1], offset: chunkSize, data: make([]byte, 16)},
	}

	chunk0 := make([]byte, chunkSize)
	copy(chunk0, entries[0].data)
	compressedChunk1 := encodeZeroLZ4Chunk()

	volume := make([]byte, 36)
	copy(volume, "DATF")
	binary.LittleEndian.PutUint32(volume[4:], 4300)
	binary.LittleEndian.PutUint64(volume[8:], filetime)
	binary.LittleEndian.PutUint32(volume[24:], 2)
	binary.LittleEndian.PutUint32(volume[28:], chunkSize)
	copy(volume[32:], "CHNK")
	var sizeBytes [4]byte
	binary.LittleEndian.PutUint32(sizeBytes[:], chunkSize)
	volume = append(volume, sizeBytes[:]...)
	binary.LittleEndian.PutUint32(sizeBytes[:], uint32(4+len(compressedChunk1)))
	volume = append(volume, sizeBytes[:]...)
	volume = append(volume, "DATA"...)
	volume = append(volume, chunk0...)
	var decodedSize [4]byte
	binary.LittleEndian.PutUint32(decodedSize[:], chunkSize)
	volume = append(volume, decodedSize[:]...)
	volume = append(volume, compressedChunk1...)
	if err := os.WriteFile(filepath.Join(root, "game000.dat"), volume, 0o600); err != nil {
		t.Fatal(err)
	}

	hashes := make(map[string]uint32, len(names))
	for _, name := range names {
		hashes[name] = driveClubNameHash(name, 13, 1)
	}
	sort.Slice(entries, func(i, j int) bool {
		return hashes[entries[i].name] < hashes[entries[j].name]
	})

	var encodedNames []byte
	for _, name := range names {
		encodedNames = append(encodedNames, []byte(name)...)
		encodedNames = append(encodedNames, 0)
	}
	index := make([]byte, 0x3d+256)
	copy(index, "DATX")
	binary.LittleEndian.PutUint32(index[4:], 4300)
	binary.LittleEndian.PutUint64(index[8:], filetime)
	binary.LittleEndian.PutUint64(index[16:], uint64(2*chunkSize))
	binary.LittleEndian.PutUint32(index[24:], 13)
	binary.LittleEndian.PutUint32(index[28:], 1)
	binary.LittleEndian.PutUint32(index[32:], 7)
	binary.LittleEndian.PutUint32(index[36:], chunkSize)
	binary.LittleEndian.PutUint32(index[40:], 0x60)
	binary.LittleEndian.PutUint32(index[44:], 2)
	binary.LittleEndian.PutUint32(index[48:], 1)
	binary.LittleEndian.PutUint32(index[52:], 2)
	binary.LittleEndian.PutUint32(index[56:], uint32(len(entries)))
	index[60] = 128
	var namesSize [4]byte
	binary.LittleEndian.PutUint32(namesSize[:], uint32(len(encodedNames)))
	index = append(index, namesSize[:]...)
	index = append(index, encodedNames...)
	var marker [4]byte
	binary.LittleEndian.PutUint32(marker[:], 0x12345678)
	index = append(index, marker[:]...)
	for _, entry := range entries {
		var record [32]byte
		packedLocation := entry.offset << 16
		binary.LittleEndian.PutUint64(record[:], packedLocation)
		binary.LittleEndian.PutUint32(record[8:], uint32(len(entry.data)))
		binary.LittleEndian.PutUint32(record[12:], hashes[entry.name])
		index = append(index, record[:]...)
	}
	index = append(index, marker[:]...)
	var volumeTime [8]byte
	binary.LittleEndian.PutUint64(volumeTime[:], filetime)
	index = append(index, volumeTime[:]...)
	var finalTag [8]byte
	binary.LittleEndian.PutUint64(finalTag[:], uint64(len(index)))
	index = append(index, finalTag[:]...)
	if err := os.WriteFile(filepath.Join(root, "game.ndx"), index, 0o600); err != nil {
		t.Fatal(err)
	}
}

func driveClubNameHash(name string, seedA, seedB uint32) uint32 {
	var hash uint32
	for i := 0; i < len(name); i++ {
		c := name[i]
		if c == '\\' {
			c = '/'
		}
		if c >= 'a' && c <= 'z' {
			c -= 'a' - 'A'
		}
		seedA *= seedB
		seedA %= 0x7ffffffe
		hash = (hash*seedA + uint32(c)) % 0x7fffffff
	}
	return hash
}

func datxRecordMD5(t *testing.T, root, name string) []byte {
	t.Helper()
	index, err := os.ReadFile(filepath.Join(root, "game.ndx"))
	if err != nil {
		t.Fatal(err)
	}
	fileCount := binary.LittleEndian.Uint32(index[56:])
	namesSize := binary.LittleEndian.Uint32(index[317:])
	records := 321 + int(namesSize) + 4
	wantHash := driveClubNameHash(name, 13, 1)
	for i := uint32(0); i < fileCount; i++ {
		record := index[records+int(i)*32 : records+(int(i)+1)*32]
		if binary.LittleEndian.Uint32(record[12:]) == wantHash {
			return append([]byte(nil), record[16:32]...)
		}
	}
	t.Fatalf("no DATX record for %q", name)
	return nil
}

func encodeZeroLZ4Chunk() []byte {
	out := []byte{0x1f, 0x00, 0x01, 0x00}
	remainingMatch := 65530 - 19
	for remainingMatch >= 255 {
		out = append(out, 255)
		remainingMatch -= 255
	}
	out = append(out, byte(remainingMatch))
	out = append(out, 0x50, 0, 0, 0, 0, 0)
	return out
}
