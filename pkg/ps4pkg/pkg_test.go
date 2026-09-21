package ps4pkg

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// buildSyntheticSFO generates a valid binary SFO buffer with specified key-values.
func buildSyntheticSFO(t *testing.T) []byte {
	// Keys: "TITLE", "TITLE_ID", "APP_VER", "SYSTEM_VER"
	keys := []string{"APP_VER", "SYSTEM_VER", "TITLE", "TITLE_ID"}
	strVals := map[string]string{
		"APP_VER":  "01.04",
		"TITLE":    "Test Game",
		"TITLE_ID": "CUSA99999",
	}
	intVals := map[string]uint32{
		"SYSTEM_VER": 0x05050000,
	}

	var keyTable bytes.Buffer
	keyOffsets := make(map[string]uint16)
	for _, k := range keys {
		keyOffsets[k] = uint16(keyTable.Len())
		keyTable.WriteString(k)
		keyTable.WriteByte(0)
	}

	var dataTable bytes.Buffer
	type entryData struct {
		offset uint32
		length uint32
		maxLen uint32
		format SFOParamFormat
	}
	entriesData := make(map[string]entryData)

	for _, k := range keys {
		offset := uint32(dataTable.Len())
		if s, ok := strVals[k]; ok {
			b := []byte(s)
			b = append(b, 0)
			dataTable.Write(b)
			entriesData[k] = entryData{
				offset: offset,
				length: uint32(len(b)),
				maxLen: uint32(len(b)),
				format: SFOFormatString,
			}
		} else if v, ok := intVals[k]; ok {
			var ibuf [4]byte
			binary.LittleEndian.PutUint32(ibuf[:], v)
			dataTable.Write(ibuf[:])
			entriesData[k] = entryData{
				offset: offset,
				length: 4,
				maxLen: 4,
				format: SFOFormatInteger,
			}
		}
	}

	headerSize := uint32(20)
	entryTableSize := uint32(len(keys) * 16)
	keyTableOffset := headerSize + entryTableSize
	dataTableOffset := keyTableOffset + uint32(keyTable.Len())

	var out bytes.Buffer
	// SFO Header
	hdr := SFOHeader{
		Magic:           PSFMagic,
		Version:         0x00000101,
		KeyTableOffset:  keyTableOffset,
		DataTableOffset: dataTableOffset,
		EntryCount:      uint32(len(keys)),
	}
	_ = binary.Write(&out, binary.LittleEndian, &hdr)

	// Entry records
	for _, k := range keys {
		ed := entriesData[k]
		rec := SFOEntryRecord{
			KeyOffset:   keyOffsets[k],
			ParamFormat: ed.format,
			ParamLength: ed.length,
			ParamMaxLen: ed.maxLen,
			DataOffset:  ed.offset,
		}
		_ = binary.Write(&out, binary.LittleEndian, &rec)
	}

	out.Write(keyTable.Bytes())
	out.Write(dataTable.Bytes())

	return out.Bytes()
}

func TestSFOParser(t *testing.T) {
	data := buildSyntheticSFO(t)
	sfo, err := ParseSFO(data)
	if err != nil {
		t.Fatalf("ParseSFO failed: %v", err)
	}

	if sfo.Title() != "Test Game" {
		t.Errorf("Expected Title 'Test Game', got '%s'", sfo.Title())
	}
	if sfo.TitleID() != "CUSA99999" {
		t.Errorf("Expected TitleID 'CUSA99999', got '%s'", sfo.TitleID())
	}
	if sfo.AppVersion() != "01.04" {
		t.Errorf("Expected AppVersion '01.04', got '%s'", sfo.AppVersion())
	}
	if sfo.SystemVersion() != 0x05050000 {
		t.Errorf("Expected SystemVersion 0x05050000, got 0x%08x", sfo.SystemVersion())
	}
}

func TestSyntheticPKGParser(t *testing.T) {
	sfoData := buildSyntheticSFO(t)

	// Build synthetic PKG container:
	// 0x0000 - 0x1000: Header
	// 0x2000 - 0x2040: Entry Table (2 entries: PARAM_SFO and ENTRY_NAMES)
	// 0x3000: ENTRY_NAMES data
	// 0x4000: PARAM_SFO data
	totalSize := int64(0x6000)
	pkgBuf := make([]byte, totalSize)

	// Magic \x7fCNT
	copy(pkgBuf[0:4], PKGMagic[:])

	// TableOffset = 0x2000, TableSize = 0x40, EntryCount = 2
	binary.BigEndian.PutUint32(pkgBuf[0x10:0x14], 2)      // EntryCount
	binary.BigEndian.PutUint32(pkgBuf[0x18:0x1c], 0x2000) // TableOffset
	binary.BigEndian.PutUint32(pkgBuf[0x1c:0x20], 0x40)   // TableSize

	// Content ID at 0x40
	copy(pkgBuf[0x40:], []byte("EP1018-CUSA99999_00-TESTGAME00000001\x00"))

	// Entry 0: ENTRY_NAMES (ID=0x200)
	binary.BigEndian.PutUint32(pkgBuf[0x2000:], EntryIDEntryNames)
	binary.BigEndian.PutUint32(pkgBuf[0x2010:], 0x3000) // DataOffset
	binary.BigEndian.PutUint32(pkgBuf[0x2014:], 0x20)   // DataSize

	// Entry 1: PARAM_SFO (ID=0x1000)
	binary.BigEndian.PutUint32(pkgBuf[0x2020:], EntryIDParamSFO)
	binary.BigEndian.PutUint32(pkgBuf[0x2030:], 0x4000) // DataOffset
	binary.BigEndian.PutUint32(pkgBuf[0x2034:], uint32(len(sfoData)))

	// Name table data at 0x3000
	copy(pkgBuf[0x3000:], []byte("entry_names\x00param.sfo\x00"))

	// SFO data at 0x4000
	copy(pkgBuf[0x4000:], sfoData)

	rdr := bytes.NewReader(pkgBuf)
	pkg, err := Parse(rdr, totalSize)
	if err != nil {
		t.Fatalf("Parse PKG failed: %v", err)
	}

	if pkg.ContentID != "EP1018-CUSA99999_00-TESTGAME00000001" {
		t.Errorf("Unexpected ContentID: %s", pkg.ContentID)
	}
	if pkg.TitleID() != "CUSA99999" {
		t.Errorf("Unexpected TitleID: %s", pkg.TitleID())
	}
	if pkg.Title() != "Test Game" {
		t.Errorf("Unexpected Title: %s", pkg.Title())
	}
	if len(pkg.Entries) != 2 {
		t.Errorf("Expected 2 entries, got %d", len(pkg.Entries))
	}
}

func TestTitleIDFromContentID(t *testing.T) {
	cases := []struct {
		id   string
		want string
	}{
		{"EP1018-CUSA99999_00-TESTGAME00000001", "CUSA99999"},
		{"JP0001-PLJS36001_00-JAPANESEGAME0001", "PLJS36001"},
		{"HP0001-PCAS00001_00-ASIAGAME00000001", "PCAS00001"},
		{"invalid", ""},
		{"", ""},
	}
	for _, tc := range cases {
		if got := TitleIDFromContentID(tc.id); got != tc.want {
			t.Errorf("TitleIDFromContentID(%q)=%q, want %q", tc.id, got, tc.want)
		}
	}
}

func TestComputeKeysLength(t *testing.T) {
	k, err := ComputeKeys("EP0000-CUSA99999_00-TESTGAME00000001", "00000000000000000000000000000000", 1)
	if err != nil {
		t.Fatalf("ComputeKeys: %v", err)
	}
	if len(k) != 32 {
		t.Fatalf("expected 32-byte EKPFS, got %d", len(k))
	}
}

func TestXTSRoundTrip(t *testing.T) {
	dataKey := bytes.Repeat([]byte{0x11}, 16)
	tweakKey := bytes.Repeat([]byte{0x22}, 16)
	plain := bytes.Repeat([]byte{0xAB}, xtsSectorSize)
	plain[0], plain[100], plain[4095] = 1, 2, 3
	enc := append([]byte(nil), plain...)

	data, err := aes.NewCipher(dataKey)
	if err != nil {
		t.Fatal(err)
	}
	tweak, err := aes.NewCipher(tweakKey)
	if err != nil {
		t.Fatal(err)
	}
	var tb, et [16]byte
	encryptXTSSector(data, tweak, enc, 16, &tb, &et)
	if bytes.Equal(enc, plain) {
		t.Fatal("XTS encrypt left plaintext unchanged")
	}
	decryptXTSSector(data, tweak, enc, 16, &tb, &et)
	if !bytes.Equal(enc, plain) {
		t.Fatal("XTS decrypt did not restore plaintext")
	}
}

func encryptXTSSector(data, tweak cipher.Block, sector []byte, sectorNum uint64, tweakBuf, encTweak *[16]byte) {
	for i := range tweakBuf {
		tweakBuf[i] = 0
	}
	binary.LittleEndian.PutUint64(tweakBuf[:8], sectorNum)
	tweak.Encrypt(encTweak[:], tweakBuf[:])
	var block [16]byte
	for off := 0; off < len(sector); off += 16 {
		for i := 0; i < 16; i++ {
			block[i] = sector[off+i] ^ encTweak[i]
		}
		data.Encrypt(block[:], block[:])
		for i := 0; i < 16; i++ {
			sector[off+i] = block[i] ^ encTweak[i]
		}
		gf128Double(encTweak)
	}
}

func livePKGDir(t *testing.T) string {
	t.Helper()
	testDir := os.Getenv("PS4RECOMP_PKG_DIR")
	if testDir == "" {
		t.Skip("PS4RECOMP_PKG_DIR is not set; skipping live PKG tests")
	}
	if _, err := os.ReadDir(testDir); err != nil {
		t.Skipf("PS4RECOMP_PKG_DIR %s is not accessible: %v", testDir, err)
	}
	return testDir
}

func TestLiveMultiPKGScan(t *testing.T) {
	testDir := livePKGDir(t)

	mgr := NewMultiPKGManager()
	defer mgr.Close()

	if err := mgr.ScanDirectory(testDir); err != nil {
		t.Fatalf("ScanDirectory failed: %v", err)
	}

	if len(mgr.Packages) == 0 {
		t.Fatalf("Expected packages to be found in %s", testDir)
	}

	if len(mgr.GameSets) == 0 {
		t.Fatalf("Expected at least one title in GameSets")
	}

	var set *GamePackageSet
	for _, s := range mgr.GameSets {
		set = s
		break
	}
	if set.TitleID == "" {
		t.Errorf("Expected non-empty TitleID")
	}
	if set.Title == "" {
		t.Errorf("Expected non-empty Title for %s", set.TitleID)
	}

	t.Logf("\n--- Multi-PKG Manager Report ---\n%s", set.SummaryString())
}

func TestLivePKGUnpackAndEboot(t *testing.T) {
	testDir := livePKGDir(t)

	var pkgPath string
	err := filepath.Walk(testDir, func(path string, info os.FileInfo, err error) error {
		if err != nil || info == nil || info.IsDir() {
			return err
		}
		if strings.HasSuffix(strings.ToLower(info.Name()), ".pkg") {
			pkgPath = path
			return filepath.SkipAll
		}
		return nil
	})
	if err != nil {
		t.Fatalf("walk: %v", err)
	}
	if pkgPath == "" {
		t.Skip("no .pkg files under PS4RECOMP_PKG_DIR")
	}

	pkg, err := Open(pkgPath)
	if err != nil {
		t.Fatalf("Open PKG %s: %v", pkgPath, err)
	}
	defer pkg.Close()

	ekpfs, err := pkg.GetEkpfs()
	if err != nil {
		t.Fatalf("GetEkpfs: %v", err)
	}
	if len(ekpfs) != 32 {
		t.Fatalf("EKPFS length %d", len(ekpfs))
	}

	if pkg.PfsImageSize == 0 {
		t.Skip("PKG has no inner PFS image")
	}
	t.Logf("PKG title=%q titleID=%s app=%s pfs_off=0x%x pfs_size=%s",
		pkg.Title(), pkg.TitleID(), pkg.AppVersion(), pkg.PfsImageOffset, FormatSize(int64(pkg.PfsImageSize)))

	gameFS, err := pkg.OpenGameFS("")
	if err != nil {
		t.Fatalf("OpenGameFS: %v", err)
	}
	eboot := gameFS.Lookup("eboot.bin")
	if eboot == nil || eboot.IsDir {
		for _, f := range gameFS.AllFiles() {
			if strings.EqualFold(f.Name, "eboot.bin") {
				eboot = f
				break
			}
		}
	}
	if eboot == nil {
		var sample []string
		for i, f := range gameFS.AllFiles() {
			if i >= 30 {
				break
			}
			sample = append(sample, f.Path)
		}
		t.Skipf("eboot.bin not in this PKG; sample files: %s", strings.Join(sample, ", "))
	}
	t.Logf("Found eboot.bin path=%s size=%s", eboot.Path, FormatSize(eboot.Size))
	if eboot.Size < 1024 {
		t.Fatalf("eboot.bin too small: %d", eboot.Size)
	}

	outDir := t.TempDir()
	dst := filepath.Join(outDir, "eboot.bin")
	rel := strings.TrimPrefix(eboot.Path, "uroot/")
	if err := pkg.ExtractFile("", rel, dst); err != nil {
		t.Fatalf("ExtractFile eboot.bin: %v", err)
	}
	fi, err := os.Stat(dst)
	if err != nil {
		t.Fatalf("stat extracted eboot: %v", err)
	}
	if fi.Size() != eboot.Size {
		t.Fatalf("extracted size %d != pfs size %d", fi.Size(), eboot.Size)
	}
	hdr := make([]byte, 4)
	f, err := os.Open(dst)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if _, err := f.Read(hdr); err != nil {
		t.Fatal(err)
	}
	if string(hdr) != "\x7fELF" && (hdr[0] != 0x4F || hdr[1] != 0x15 || hdr[2] != 0x3D || hdr[3] != 0x1D) {
		t.Fatalf("extracted eboot.bin has unexpected magic %x", hdr)
	}
	t.Logf("Extracted eboot.bin magic=%x size=%d", hdr, fi.Size())
}
