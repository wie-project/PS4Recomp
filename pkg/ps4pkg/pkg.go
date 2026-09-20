package ps4pkg

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
)

// PKGMagic is the 4-byte magic sequence "\x7fCNT" identifying a PS4 PKG container.
var PKGMagic = [4]byte{0x7f, 'C', 'N', 'T'}

// Known Entry IDs within PS4 PKG files.
const (
	EntryIDDigests          uint32 = 0x00000001
	EntryIDEntryKeys        uint32 = 0x00000010
	EntryIDImageKey         uint32 = 0x00000020
	EntryIDGeneralDigests   uint32 = 0x00000080
	EntryIDMetas            uint32 = 0x00000100
	EntryIDEntryNames       uint32 = 0x00000200
	EntryIDLicenseDat       uint32 = 0x00000400
	EntryIDLicenseInfo      uint32 = 0x00000401
	EntryIDParamSFO         uint32 = 0x00001000
	EntryIDPlayGoChunkDat   uint32 = 0x00001001
	EntryIDPlayGoChunkSha   uint32 = 0x00001002
	EntryIDPlayGoManifest   uint32 = 0x00001003
	EntryIDPronunciationXML uint32 = 0x00001004
	EntryIDPronunciationSig uint32 = 0x00001005
	EntryIDPic1PNG          uint32 = 0x00001006
	EntryIDPubToolInfo      uint32 = 0x00001007
	EntryIDShareParamJSON   uint32 = 0x0000100B
	EntryIDIcon0PNG         uint32 = 0x00001200
	EntryIDPic0PNG          uint32 = 0x00001220
	EntryIDSnd0AT9          uint32 = 0x00001240
	EntryIDChangeInfoXML    uint32 = 0x00001260
)

// RawHeader represents the initial binary header of a PS4 PKG.
type RawHeader struct {
	Magic          [4]byte
	PkgType        uint32
	Unknown08      uint32
	FileCount      uint32
	EntryCount     uint32
	ScEntryCount   uint16
	TotalEntries   uint16
	TableOffset    uint32
	TableSize      uint32
	BodyOffset     uint64
	BodySize       uint64
}

// RawMetaEntry represents a 32-byte descriptor entry in the PKG entry table.
type RawMetaEntry struct {
	ID              uint32
	NameTableOffset uint32
	Flags1          uint32
	Flags2          uint32
	DataOffset      uint32
	DataSize        uint32
	Padding         [8]byte
}

// Entry represents an individual item extracted from the PKG entry table.
type Entry struct {
	ID              uint32
	Name            string
	NameTableOffset uint32
	Flags1          uint32
	Flags2          uint32
	DataOffset      uint64
	DataSize        uint64
	IsEncrypted     bool
	KeyIndex        uint32
}

// PKG represents an opened PS4 PKG container.
type PKG struct {
	FilePath    string
	File        *os.File
	FileSize    int64
	RawHeader   RawHeader
	ContentID   string
	DrmType     uint32
	ContentType uint32
	Entries     []Entry
	EntryByID   map[uint32]*Entry
	EntryByName map[string]*Entry
	SFO         *SFO
}

// Open opens and parses a PS4 PKG file from disk.
func Open(filePath string) (*PKG, error) {
	f, err := os.Open(filePath)
	if err != nil {
		return nil, fmt.Errorf("failed to open PKG file: %w", err)
	}

	fi, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, fmt.Errorf("failed to stat PKG file: %w", err)
	}

	pkg, err := Parse(f, fi.Size())
	if err != nil {
		f.Close()
		return nil, err
	}

	pkg.FilePath = filePath
	pkg.File = f
	return pkg, nil
}

// Parse parses a PKG from an io.ReaderAt and size.
func Parse(r io.ReaderAt, size int64) (*PKG, error) {
	if size < 0x2000 {
		return nil, errors.New("file too small to be a valid PS4 PKG")
	}

	hdrBuf := make([]byte, 0x1000)
	if _, err := r.ReadAt(hdrBuf, 0); err != nil {
		return nil, fmt.Errorf("failed to read PKG header: %w", err)
	}

	var rawHdr RawHeader
	rdr := bytes.NewReader(hdrBuf[:0x30])
	if err := binary.Read(rdr, binary.BigEndian, &rawHdr); err != nil {
		return nil, fmt.Errorf("failed to decode PKG header: %w", err)
	}

	if rawHdr.Magic != PKGMagic {
		return nil, fmt.Errorf("invalid PKG magic: expected %v, got %v", PKGMagic, rawHdr.Magic)
	}

	// Content ID is stored at offset 0x40 (null-terminated string, max 48 bytes)
	contentIDRaw := hdrBuf[0x40:0x70]
	contentID := string(bytes.TrimRight(contentIDRaw, "\x00"))

	pkg := &PKG{
		FileSize:    size,
		RawHeader:   rawHdr,
		ContentID:   contentID,
		EntryByID:   make(map[uint32]*Entry),
		EntryByName: make(map[string]*Entry),
	}

	// Read Entry Table
	if rawHdr.TableOffset == 0 || rawHdr.TableSize == 0 {
		return nil, errors.New("PKG table offset or size is zero")
	}

	tableBuf := make([]byte, rawHdr.TableSize)
	if _, err := r.ReadAt(tableBuf, int64(rawHdr.TableOffset)); err != nil {
		return nil, fmt.Errorf("failed to read PKG entry table: %w", err)
	}

	entryCount := int(rawHdr.EntryCount)
	if entryCount*32 > len(tableBuf) {
		entryCount = len(tableBuf) / 32
	}

	pkg.Entries = make([]Entry, entryCount)
	tableRdr := bytes.NewReader(tableBuf)

	var namesEntry *Entry

	for i := 0; i < entryCount; i++ {
		var rawEntry RawMetaEntry
		if err := binary.Read(tableRdr, binary.BigEndian, &rawEntry); err != nil {
			return nil, fmt.Errorf("failed to parse entry table index %d: %w", i, err)
		}

		keyIdx := (rawEntry.Flags2 & 0xF000) >> 12
		isEnc := (rawEntry.Flags1 & 0x80000000) != 0 || (rawEntry.Flags2 & 1) != 0

		e := Entry{
			ID:              rawEntry.ID,
			NameTableOffset: rawEntry.NameTableOffset,
			Flags1:          rawEntry.Flags1,
			Flags2:          rawEntry.Flags2,
			DataOffset:      uint64(rawEntry.DataOffset),
			DataSize:        uint64(rawEntry.DataSize),
			IsEncrypted:     isEnc,
			KeyIndex:        keyIdx,
		}

		pkg.Entries[i] = e
		pkg.EntryByID[e.ID] = &pkg.Entries[i]

		if e.ID == EntryIDEntryNames {
			namesEntry = &pkg.Entries[i]
		}
	}

	// Read Name Table if available
	if namesEntry != nil && namesEntry.DataSize > 0 {
		namesBuf := make([]byte, namesEntry.DataSize)
		if _, err := r.ReadAt(namesBuf, int64(namesEntry.DataOffset)); err == nil {
			for i := range pkg.Entries {
				offset := int(pkg.Entries[i].NameTableOffset)
				if offset < len(namesBuf) {
					end := bytes.IndexByte(namesBuf[offset:], 0)
					if end >= 0 {
						pkg.Entries[i].Name = string(namesBuf[offset : offset+end])
					} else {
						pkg.Entries[i].Name = string(namesBuf[offset:])
					}
					if pkg.Entries[i].Name != "" {
						pkg.EntryByName[pkg.Entries[i].Name] = &pkg.Entries[i]
					}
				}
			}
		}
	}

	// If PARAM_SFO entry exists, parse it
	if sfoEntry, ok := pkg.EntryByID[EntryIDParamSFO]; ok {
		if sfoEntry.DataSize > 0 {
			sfo, err := ReadSFO(r, int64(sfoEntry.DataOffset), int64(sfoEntry.DataSize))
			if err == nil {
				pkg.SFO = sfo
			}
		}
	}

	return pkg, nil
}

// Close closes the underlying PKG file handle if open.
func (p *PKG) Close() error {
	if p.File != nil {
		return p.File.Close()
	}
	return nil
}

// ReadEntryData reads the raw byte content of an entry.
func (p *PKG) ReadEntryData(e *Entry) ([]byte, error) {
	if p.File == nil {
		return nil, errors.New("PKG file handle not available")
	}
	if e.DataSize == 0 {
		return []byte{}, nil
	}
	buf := make([]byte, e.DataSize)
	if _, err := p.File.ReadAt(buf, int64(e.DataOffset)); err != nil {
		return nil, fmt.Errorf("failed to read entry %d (%s): %w", e.ID, e.Name, err)
	}
	return buf, nil
}

// ExtractEntry writes an entry directly to a destination file path.
func (p *PKG) ExtractEntry(e *Entry, dstPath string) error {
	data, err := p.ReadEntryData(e)
	if err != nil {
		return err
	}
	return os.WriteFile(dstPath, data, 0644)
}

// Title returns the parsed game/app title from SFO, or fallback to Content ID.
func (p *PKG) Title() string {
	if p.SFO != nil && p.SFO.Title() != "" {
		return p.SFO.Title()
	}
	return p.ContentID
}

// TitleID returns the Title ID (e.g. CUSA12771).
func (p *PKG) TitleID() string {
	if p.SFO != nil && p.SFO.TitleID() != "" {
		return p.SFO.TitleID()
	}
	// Fallback: parse from content ID "EP1018-CUSA12771_00-..."
	parts := strings.Split(p.ContentID, "-")
	if len(parts) >= 2 {
		sub := strings.Split(parts[1], "_")
		if len(sub) >= 1 && strings.HasPrefix(sub[0], "CUSA") {
			return sub[0]
		}
	}
	return ""
}

// AppVersion returns the app or patch version (e.g. 01.04).
func (p *PKG) AppVersion() string {
	if p.SFO != nil && p.SFO.AppVersion() != "" {
		return p.SFO.AppVersion()
	}
	return "01.00"
}

// Category returns the category string (gd, gp, ac).
func (p *PKG) Category() string {
	if p.SFO != nil && p.SFO.Category() != "" {
		return p.SFO.Category()
	}
	return ""
}

// IsBaseGame returns true if the package is a full base game (gd).
func (p *PKG) IsBaseGame() bool {
	cat := strings.ToLower(p.Category())
	return cat == "gd" || cat == "gdc" || cat == "gde"
}

// IsPatch returns true if the package is a game patch/update (gp).
func (p *PKG) IsPatch() bool {
	cat := strings.ToLower(p.Category())
	return cat == "gp" || cat == "gpc" || cat == "gpe"
}

// IsDLC returns true if the package is add-on content / DLC (ac).
func (p *PKG) IsDLC() bool {
	cat := strings.ToLower(p.Category())
	return cat == "ac" || cat == "al"
}
