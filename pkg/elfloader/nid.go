package elfloader

import (
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"strings"
)

// nidSuffixKey is the OpenOrbis / Sony suffix mixed into symbol-name SHA-1 hashes.
const nidSuffixKey = "518D64A635DED8C1E6B039B1C3E55230"

// CalculateNID returns the 11-character Sony NID for a plaintext symbol name.
func CalculateNID(symbolName string) string {
	suffix, err := hex.DecodeString(nidSuffixKey)
	if err != nil {
		return ""
	}
	sum := sha1.Sum(append([]byte(symbolName), suffix...))
	var hashBytes [8]byte
	binary.LittleEndian.PutUint64(hashBytes[:], binary.BigEndian.Uint64(sum[:8]))
	nid := base64.StdEncoding.EncodeToString(hashBytes[:])
	nid = strings.TrimRight(nid, "=")
	return strings.ReplaceAll(nid, "/", "-")
}

// NIDPrefix returns the NID portion of a dynsym name ("hash#lib#mod").
func NIDPrefix(encoded string) string {
	if i := strings.IndexByte(encoded, '#'); i >= 0 {
		return encoded[:i]
	}
	return encoded
}

// Sony base64 alphabet used for library/module IDs appended to NIDs.
const sceIDAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"

func decodeSCEID(enc string) (uint16, bool) {
	if enc == "" {
		return 0, false
	}
	n := 0
	for i := 0; i < len(enc); i++ {
		idx := strings.IndexByte(sceIDAlphabet, enc[i])
		if idx < 0 {
			return 0, false
		}
		n = n*64 + idx
	}
	if n > 0xffff {
		return 0, false
	}
	return uint16(n), true
}

// LibraryForNID maps a dynsym name ("hash#libid#modid" or plaintext) onto the
// imported library name from DT_SCE_IMPORT_LIB when that table is present.
func (l *LoadedELF) LibraryForNID(name string) string {
	parts := strings.Split(name, "#")
	if len(parts) < 2 || parts[1] == "" {
		return "plaintext"
	}
	if l == nil || len(l.ImportLibs) == 0 {
		return parts[1]
	}
	id, ok := decodeSCEID(parts[1])
	if !ok {
		return parts[1]
	}
	if lib, ok := l.ImportLibs[id]; ok {
		return lib
	}
	return parts[1]
}
