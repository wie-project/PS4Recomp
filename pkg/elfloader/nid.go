package elfloader

import (
	"bufio"
	"bytes"
	"compress/gzip"
	"crypto/sha1"
	_ "embed"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"strings"
	"sync"
)

//go:embed known_names.txt.gz
var knownNamesGz []byte

var (
	nidDBOnce sync.Once
	nidToName map[string]string
)

func initNIDDB() {
	nidToName = make(map[string]string, 45000)
	if len(knownNamesGz) == 0 {
		return
	}
	zr, err := gzip.NewReader(bytes.NewReader(knownNamesGz))
	if err != nil {
		return
	}
	defer zr.Close()

	scanner := bufio.NewScanner(zr)
	for scanner.Scan() {
		name := strings.TrimSpace(scanner.Text())
		if name == "" {
			continue
		}
		nid := CalculateNID(name)
		if nid != "" {
			nidToName[nid] = name
		}
		nidUnder := CalculateNID("_" + name)
		if nidUnder != "" {
			if _, exists := nidToName[nidUnder]; !exists {
				nidToName[nidUnder] = "_" + name
			}
		}
	}
}

// specialNIDs maps well-known Sony symbol hashes (such as libc globals)
// to their canonical plaintext names.
var specialNIDs = map[string]string{
	"f7uOxY9mM1U": "__stack_chk_guard",
}

// ResolveNID returns the human-readable function name for a Sony NID if known.
func ResolveNID(nidOrSym string) (string, bool) {
	prefix := NIDPrefix(nidOrSym)
	if name, ok := specialNIDs[prefix]; ok {
		return name, true
	}
	nidDBOnce.Do(initNIDDB)
	name, ok := nidToName[prefix]
	return name, ok
}


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
