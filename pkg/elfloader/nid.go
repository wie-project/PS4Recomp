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
