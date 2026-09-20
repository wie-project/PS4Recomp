package ps4pkg

import (
	"bytes"
	"encoding/binary"
	"os"
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

func TestLiveHogwartsLegacyPKGs(t *testing.T) {
	testDir := "/Volumes/Samsung T7/Hogwarts Legacy Deluxe Edition"
	if _, err := os.ReadDir(testDir); err != nil {
		t.Skipf("Live test path %s not accessible (sandbox or unmounted), skipping: %v", testDir, err)
	}

	mgr := NewMultiPKGManager()
	defer mgr.Close()

	if err := mgr.ScanDirectory(testDir); err != nil {
		t.Fatalf("ScanDirectory failed: %v", err)
	}

	if len(mgr.Packages) == 0 {
		t.Fatalf("Expected packages to be found in %s", testDir)
	}

	set, ok := mgr.GameSets["CUSA12771"]
	if !ok {
		t.Fatalf("Expected CUSA12771 to be found in GameSets")
	}

	if set.Title == "" {
		t.Errorf("Expected non-empty Title for CUSA12771")
	}
	if len(set.DLCs) == 0 {
		t.Errorf("Expected DLCs to be detected for CUSA12771, got 0")
	}

	t.Logf("\n--- Multi-PKG Manager Report ---\n%s", set.SummaryString())
}
