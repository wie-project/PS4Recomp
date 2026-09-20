package ps4pkg

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"strings"
)

// PSFMagic is the standard magic bytes for Sony SFO files ("\x00PSF").
var PSFMagic = [4]byte{0x00, 'P', 'S', 'F'}

// SFOParamFormat defines the data format of an SFO parameter.
type SFOParamFormat uint16

const (
	// SFOFormatBinary represents binary data.
	SFOFormatBinary SFOParamFormat = 0x0004
	// SFOFormatString represents a null-terminated UTF-8 string.
	SFOFormatString SFOParamFormat = 0x0204
	// SFOFormatStringAlt represents an alternative string format.
	SFOFormatStringAlt SFOParamFormat = 0x0002
	// SFOFormatInteger represents a 32-bit unsigned little-endian integer.
	SFOFormatInteger SFOParamFormat = 0x0404
)

// SFOHeader represents the 20-byte header of a param.sfo file.
type SFOHeader struct {
	Magic           [4]byte
	Version         uint32
	KeyTableOffset  uint32
	DataTableOffset uint32
	EntryCount      uint32
}

// SFOEntryRecord represents the 16-byte metadata of each key-value pair in SFO.
type SFOEntryRecord struct {
	KeyOffset   uint16
	ParamFormat SFOParamFormat
	ParamLength uint32
	ParamMaxLen uint32
	DataOffset  uint32
}

// SFOValue represents an individual parsed parameter from an SFO file.
type SFOValue struct {
	Key         string
	Format      SFOParamFormat
	StringVal   string
	IntegerVal  uint32
	BinaryVal   []byte
}

// SFO represents a fully parsed Sony PlayStation SFO file.
type SFO struct {
	Header  SFOHeader
	Entries map[string]SFOValue
	Keys    []string // preserves order of entries
}

// ParseSFO parses raw SFO bytes into an SFO struct.
func ParseSFO(data []byte) (*SFO, error) {
	if len(data) < 20 {
		return nil, errors.New("SFO data too short for header")
	}

	r := bytes.NewReader(data)
	var hdr SFOHeader
	if err := binary.Read(r, binary.LittleEndian, &hdr); err != nil {
		return nil, fmt.Errorf("failed to read SFO header: %w", err)
	}

	if hdr.Magic != PSFMagic {
		return nil, fmt.Errorf("invalid SFO magic: expected %v, got %v", PSFMagic, hdr.Magic)
	}

	if hdr.KeyTableOffset > uint32(len(data)) || hdr.DataTableOffset > uint32(len(data)) {
		return nil, errors.New("invalid SFO table offsets")
	}

	records := make([]SFOEntryRecord, hdr.EntryCount)
	for i := uint32(0); i < hdr.EntryCount; i++ {
		if err := binary.Read(r, binary.LittleEndian, &records[i]); err != nil {
			return nil, fmt.Errorf("failed to read SFO entry %d: %w", i, err)
		}
	}

	keyTable := data[hdr.KeyTableOffset:]
	dataTable := data[hdr.DataTableOffset:]

	sfo := &SFO{
		Header:  hdr,
		Entries: make(map[string]SFOValue, hdr.EntryCount),
		Keys:    make([]string, 0, hdr.EntryCount),
	}

	for _, rec := range records {
		// Extract key string
		if int(rec.KeyOffset) >= len(keyTable) {
			continue
		}
		keyEnd := bytes.IndexByte(keyTable[rec.KeyOffset:], 0)
		var key string
		if keyEnd >= 0 {
			key = string(keyTable[rec.KeyOffset : int(rec.KeyOffset)+keyEnd])
		} else {
			key = string(keyTable[rec.KeyOffset:])
		}

		// Extract value
		valOffset := int(rec.DataOffset)
		valLen := int(rec.ParamLength)
		if valOffset+valLen > len(dataTable) {
			continue
		}
		rawVal := dataTable[valOffset : valOffset+valLen]

		sfoVal := SFOValue{
			Key:    key,
			Format: rec.ParamFormat,
		}

		switch rec.ParamFormat {
		case SFOFormatString, SFOFormatStringAlt:
			s := strings.TrimRight(string(rawVal), "\x00")
			sfoVal.StringVal = s
		case SFOFormatInteger:
			if len(rawVal) >= 4 {
				sfoVal.IntegerVal = binary.LittleEndian.Uint32(rawVal)
			}
		default:
			sfoVal.BinaryVal = rawVal
		}

		sfo.Entries[key] = sfoVal
		sfo.Keys = append(sfo.Keys, key)
	}

	return sfo, nil
}

// ReadSFO reads and parses an SFO from an io.ReaderAt.
func ReadSFO(r io.ReaderAt, offset int64, size int64) (*SFO, error) {
	buf := make([]byte, size)
	if _, err := r.ReadAt(buf, offset); err != nil {
		return nil, fmt.Errorf("failed to read SFO data: %w", err)
	}
	return ParseSFO(buf)
}

// GetString returns the string value for key, or empty string if not found.
func (s *SFO) GetString(key string) string {
	if v, ok := s.Entries[key]; ok {
		if v.Format == SFOFormatString || v.Format == SFOFormatStringAlt {
			return v.StringVal
		}
		if v.StringVal != "" {
			return v.StringVal
		}
	}
	return ""
}

// GetInt returns the integer value for key, or 0 if not found.
func (s *SFO) GetInt(key string) uint32 {
	if v, ok := s.Entries[key]; ok {
		return v.IntegerVal
	}
	return 0
}

// Title returns the game/app title.
func (s *SFO) Title() string {
	if t := s.GetString("TITLE"); t != "" {
		return t
	}
	return s.GetString("TITLE_00")
}

// TitleID returns the PS4 Title ID (e.g., CUSA12771).
func (s *SFO) TitleID() string {
	return s.GetString("TITLE_ID")
}

// AppVersion returns the application patch/update version (e.g., 01.04).
func (s *SFO) AppVersion() string {
	if v := s.GetString("APP_VER"); v != "" {
		return v
	}
	return s.GetString("VERSION")
}

// Version returns the base application version.
func (s *SFO) Version() string {
	return s.GetString("VERSION")
}

// Category returns the application category (e.g., gd, gp, ac).
func (s *SFO) Category() string {
	return s.GetString("CATEGORY")
}

// ContentID returns the Sony Content ID (e.g., EP1018-CUSA12771_00-HOGWARTSLEGACY01).
func (s *SFO) ContentID() string {
	return s.GetString("CONTENT_ID")
}

// SystemVersion returns the minimum system firmware version required.
func (s *SFO) SystemVersion() uint32 {
	return s.GetInt("SYSTEM_VER")
}
