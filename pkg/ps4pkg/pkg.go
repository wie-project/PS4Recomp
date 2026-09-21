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
	RawMeta         [32]byte
}

// PKG represents an opened PS4 PKG container.
type PKG struct {
	FilePath         string
	File             *os.File
	FileSize         int64
	RawHeader        RawHeader
	ContentID        string
	DrmType          uint32
	ContentType      uint32
	ContentFlags     uint32
	PfsImageCount    uint32
	PfsFlags         uint64
	PfsImageOffset   uint64
	PfsImageSize     uint64
	PackageSize      uint64
	PfsCacheSize     uint32
	Entries          []Entry
	EntryByID        map[uint32]*Entry
	EntryByName      map[string]*Entry
	SFO              *SFO
	entryKeysSeed    []byte
	entryKeyDigests  [7][]byte
	entryKeyBlobs    [7][]byte
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
	if len(hdrBuf) >= 0x7C {
		pkg.DrmType = binary.BigEndian.Uint32(hdrBuf[0x70:0x74])
		pkg.ContentType = binary.BigEndian.Uint32(hdrBuf[0x74:0x78])
		pkg.ContentFlags = binary.BigEndian.Uint32(hdrBuf[0x78:0x7C])
	}
	if size >= 0x440 {
		pfsHdr := make([]byte, 0x40)
		if _, err := r.ReadAt(pfsHdr, 0x400); err == nil {
			pkg.PfsImageCount = binary.BigEndian.Uint32(pfsHdr[0x04:0x08])
			pkg.PfsFlags = binary.BigEndian.Uint64(pfsHdr[0x08:0x10])
			pkg.PfsImageOffset = binary.BigEndian.Uint64(pfsHdr[0x10:0x18])
			pkg.PfsImageSize = binary.BigEndian.Uint64(pfsHdr[0x18:0x20])
			pkg.PackageSize = binary.BigEndian.Uint64(pfsHdr[0x28:0x30])
			pkg.PfsCacheSize = binary.BigEndian.Uint32(pfsHdr[0x34:0x38])
		}
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
		isEnc := (rawEntry.Flags1 & 0x80000000) != 0

		var rawMeta [32]byte
		copy(rawMeta[:], tableBuf[i*32:(i+1)*32])

		e := Entry{
			ID:              rawEntry.ID,
			NameTableOffset: rawEntry.NameTableOffset,
			Flags1:          rawEntry.Flags1,
			Flags2:          rawEntry.Flags2,
			DataOffset:      uint64(rawEntry.DataOffset),
			DataSize:        uint64(rawEntry.DataSize),
			IsEncrypted:     isEnc,
			KeyIndex:        keyIdx,
			RawMeta:         rawMeta,
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
		if sfoEntry.DataSize > 0 && !sfoEntry.IsEncrypted {
			sfo, err := ReadSFO(r, int64(sfoEntry.DataOffset), int64(sfoEntry.DataSize))
			if err == nil {
				pkg.SFO = sfo
			}
		}
	}

	if keysEntry, ok := pkg.EntryByID[EntryIDEntryKeys]; ok && keysEntry.DataSize >= 32+7*32+7*256 {
		buf := make([]byte, keysEntry.DataSize)
		if _, err := r.ReadAt(buf, int64(keysEntry.DataOffset)); err == nil {
			pkg.entryKeysSeed = append([]byte(nil), buf[:32]...)
			off := 32
			for i := 0; i < 7; i++ {
				pkg.entryKeyDigests[i] = append([]byte(nil), buf[off:off+32]...)
				off += 32
			}
			for i := 0; i < 7; i++ {
				pkg.entryKeyBlobs[i] = append([]byte(nil), buf[off:off+256]...)
				off += 256
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

// TitleIDFromContentID extracts the 9-character title ID from a Sony Content ID
// of the form "XXXXXX-TITLEID_00-CONTENTNAME" (CUSA, PLJS, PCAS, ...).
func TitleIDFromContentID(contentID string) string {
	parts := strings.Split(contentID, "-")
	if len(parts) < 2 {
		return ""
	}
	sub := strings.Split(parts[1], "_")
	id := sub[0]
	if len(id) < 8 {
		return ""
	}
	return id
}

// TitleID returns the Title ID from param.sfo, or from the Content ID.
func (p *PKG) TitleID() string {
	if p.SFO != nil && p.SFO.TitleID() != "" {
		return p.SFO.TitleID()
	}
	return TitleIDFromContentID(p.ContentID)
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
	if cat == "gd" || cat == "gdc" || cat == "gde" {
		return true
	}
	return p.ContentType == ContentTypeGD && !p.IsDLC() && !p.IsPatch()
}

// IsPatch returns true if the package is a game patch/update (gp).
func (p *PKG) IsPatch() bool {
	cat := strings.ToLower(p.Category())
	return cat == "gp" || cat == "gpc" || cat == "gpe"
}

// IsDLC returns true if the package is add-on content / DLC (ac).
func (p *PKG) IsDLC() bool {
	cat := strings.ToLower(p.Category())
	if cat == "ac" || cat == "al" {
		return true
	}
	return p.ContentType == ContentTypeAC || p.ContentType == ContentTypeAL
}

const (
	ContentTypeGD uint32 = 0x1A
	ContentTypeAC uint32 = 0x1B
	ContentTypeAL uint32 = 0x1C
	ContentTypeDP uint32 = 0x1E
)

// CheckPasscode reports whether passcode produces the digest stored in ENTRY_KEYS slot 0.
func (p *PKG) CheckPasscode(passcode string) bool {
	if len(passcode) != 32 || p.entryKeyDigests[0] == nil {
		return false
	}
	dk0, err := ComputeKeys(p.ContentID, passcode, 0)
	if err != nil {
		return false
	}
	digest := xorBytes(sha256Sum(dk0), dk0)
	return bytes.Equal(digest, p.entryKeyDigests[0])
}

// GetEkpfs decrypts the EKPFS of a fake (non-finalized) PKG using the published fake RSA keyset.
func (p *PKG) GetEkpfs() ([]byte, error) {
	imageKey, ok := p.EntryByID[EntryIDImageKey]
	if !ok || imageKey.DataSize < 256 {
		return nil, errors.New("PKG has no IMAGE_KEY entry")
	}
	if p.entryKeyBlobs[3] == nil {
		return nil, errors.New("PKG has no ENTRY_KEYS slot 3")
	}
	dk3Raw, err := rsaPKCS1Decrypt(pkgDerivedKey3RSA, p.entryKeyBlobs[3])
	if err != nil {
		return nil, fmt.Errorf("failed to decrypt derived key 3 (not a fake PKG?): %w", err)
	}
	dk3 := takeKey32(dk3Raw)
	seed := make([]byte, 64)
	copy(seed, imageKey.RawMeta[:])
	copy(seed[32:], dk3)
	ivKey := sha256Sum(seed)

	ciphertext, err := p.readRaw(imageKey)
	if err != nil {
		return nil, err
	}
	if len(ciphertext) < 256 {
		return nil, errors.New("IMAGE_KEY is shorter than 256 bytes")
	}
	plaintext, err := aesCBCDecrypt(ciphertext[:256], ivKey[16:32], ivKey[0:16])
	if err != nil {
		return nil, fmt.Errorf("failed to AES-decrypt IMAGE_KEY: %w", err)
	}
	ekpfsRaw, err := rsaPKCS1Decrypt(fakeRSAKey, plaintext)
	if err != nil {
		return nil, fmt.Errorf("failed to RSA-decrypt IMAGE_KEY (not a fake PKG?): %w", err)
	}
	if len(ekpfsRaw) == 0 {
		return nil, errors.New("RSA-decrypted IMAGE_KEY was empty")
	}
	candidates := make([][]byte, 0, 2)
	candidates = append(candidates, takeKey32(ekpfsRaw))
	if len(ekpfsRaw) > 32 {
		candidates = append(candidates, append([]byte(nil), ekpfsRaw[:32]...))
	}

	for _, ekpfs := range candidates {
		if p.entryKeyDigests[1] == nil {
			return ekpfs, nil
		}
		digest := xorBytes(sha256Sum(ekpfs), ekpfs)
		if bytes.Equal(digest, p.entryKeyDigests[1]) {
			return ekpfs, nil
		}
	}
	// Some merged / patched dumps encrypt IMAGE_KEY with the fake RSA keyset
	// while ENTRY_KEYS slot 1 stores the passcode-derived digest. The 32-byte
	// RSA plaintext is still the EKPFS used for the PFS image.
	return candidates[0], nil
}

// ResolveEKPFS returns the PFS encryption key using passcode, or the fake-PKG IMAGE_KEY path.
func (p *PKG) ResolveEKPFS(passcode string) ([]byte, error) {
	if passcode != "" {
		if !p.CheckPasscode(passcode) {
			return nil, errors.New("invalid PKG passcode")
		}
		return ComputeKeys(p.ContentID, passcode, 1)
	}
	if ekpfs, err := p.GetEkpfs(); err == nil {
		return ekpfs, nil
	} else {
		lastErr := err
		zero := "00000000000000000000000000000000"
		if p.CheckPasscode(zero) {
			return ComputeKeys(p.ContentID, zero, 1)
		}
		return nil, lastErr
	}
}

func (p *PKG) readRaw(e *Entry) ([]byte, error) {
	if e.DataSize == 0 {
		return []byte{}, nil
	}
	buf := make([]byte, e.DataSize)
	r := p.reader()
	if r == nil {
		return nil, errors.New("PKG reader not available")
	}
	if _, err := r.ReadAt(buf, int64(e.DataOffset)); err != nil {
		return nil, fmt.Errorf("failed to read entry 0x%x (%s): %w", e.ID, e.Name, err)
	}
	return buf, nil
}

func (p *PKG) reader() io.ReaderAt {
	if p.File != nil {
		return p.File
	}
	return nil
}

// DecryptEntry returns plaintext bytes of a PKG table entry.
func (p *PKG) DecryptEntry(e *Entry, passcode string) ([]byte, error) {
	raw, err := p.readRaw(e)
	if err != nil {
		return nil, err
	}
	if !e.IsEncrypted {
		return raw, nil
	}
	var keySeed []byte
	if e.KeyIndex == 3 && p.entryKeyBlobs[3] != nil {
		dk3, err := rsaPKCS1Decrypt(pkgDerivedKey3RSA, p.entryKeyBlobs[3])
		if err != nil {
			return nil, err
		}
		keySeed = takeKey32(dk3)
	} else if passcode != "" {
		keySeed, err = ComputeKeys(p.ContentID, passcode, e.KeyIndex)
		if err != nil {
			return nil, err
		}
	} else {
		return nil, fmt.Errorf("entry 0x%x is encrypted with key index %d and cannot be decrypted without a passcode", e.ID, e.KeyIndex)
	}
	seed := make([]byte, 32+len(keySeed))
	copy(seed, e.RawMeta[:])
	copy(seed[32:], keySeed)
	ivKey := sha256Sum(seed)
	return aesCBCDecrypt(raw, ivKey[16:32], ivKey[0:16])
}

func inodeLooksSane(in pfsInode) bool {
	start := in.Direct[0]
	return start >= 1 && int64(start) < maxPFSBlocks && int64(in.Blocks) >= 1 && int64(in.Blocks) < maxPFSBlocks
}

func (p *PKG) pfsSource() (*offsetReader, error) {
	if p.PfsImageSize == 0 {
		return nil, errors.New("PKG has no PFS image")
	}
	r := p.reader()
	if r == nil {
		return nil, errors.New("PKG file handle not available")
	}
	return &offsetReader{r: r, base: int64(p.PfsImageOffset), length: int64(p.PfsImageSize)}, nil
}

func tryOpenPFS(outer io.ReaderAt, flags uint64, ekpfs []byte) (*PfsImage, error) {
	img, err := OpenPFS(outer, flags, ekpfs)
	if err != nil {
		return nil, err
	}
	if len(img.inodes) == 0 || !inodeLooksSane(img.inodes[0]) {
		return nil, errors.New("PFS inodes are corrupt (wrong encryption key)")
	}
	return img, nil
}

func (p *PKG) ekpfsCandidates(passcode string) [][]byte {
	var out [][]byte
	seen := map[string]bool{}
	add := func(k []byte) {
		if len(k) != 32 || seen[string(k)] {
			return
		}
		seen[string(k)] = true
		out = append(out, k)
	}
	if passcode != "" {
		if k, err := ComputeKeys(p.ContentID, passcode, 1); err == nil {
			add(k)
		}
	}
	if k, err := p.GetEkpfs(); err == nil {
		add(k)
	}
	zero := "00000000000000000000000000000000"
	if k, err := ComputeKeys(p.ContentID, zero, 1); err == nil {
		add(k)
	}
	return out
}

// OpenOuterPFS opens the encrypted outer PFS image inside the PKG.
func (p *PKG) OpenOuterPFS(passcode string) (*PfsImage, error) {
	outer, err := p.pfsSource()
	if err != nil {
		return nil, err
	}
	keys := p.ekpfsCandidates(passcode)
	if len(keys) == 0 {
		return nil, errors.New("unable to derive EKPFS for this PKG")
	}
	flagSets := []uint64{p.PfsFlags, p.PfsFlags ^ pfsFlagNewCrypt}
	var last error
	for _, ekpfs := range keys {
		for _, flags := range flagSets {
			img, err := tryOpenPFS(outer, flags, ekpfs)
			if err == nil {
				return img, nil
			}
			last = err
		}
	}
	if last == nil {
		last = errors.New("unable to open outer PFS")
	}
	return nil, last
}

// OpenGameFS opens the inner (game) PFS, unwrapping nested PFSC when present.
func (p *PKG) OpenGameFS(passcode string) (*PfsImage, error) {
	outer, err := p.OpenOuterPFS(passcode)
	if err != nil {
		return nil, err
	}
	innerFile := outer.Lookup("pfs_image.dat")
	if innerFile == nil || innerFile.IsDir {
		return outer, nil
	}
	view, size := innerFile.Reader(outer)
	_ = size
	inner, err := OpenPFS(view, 0, nil)
	if err != nil {
		// Outer file is typically a PFSC-compressed inner PFS; try wrapping explicitly.
		pr, perr := newPFSCReader(view)
		if perr != nil {
			return nil, fmt.Errorf("failed to open inner PFS: %w", err)
		}
		inner, err = OpenPFS(pr, 0, nil)
		if err != nil {
			return nil, fmt.Errorf("failed to open inner PFS from PFSC: %w", err)
		}
	}
	return inner, nil
}
