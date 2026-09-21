package ps4pkg

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/binary"
	"fmt"
	"io"
)

const xtsSectorSize = 0x1000

// xtsReader decrypts AES-XTS-128 sectors of a PFS image on demand.
// Sectors below cryptStartSector are returned as plaintext (PFS superblock).
type xtsReader struct {
	inner           io.ReaderAt
	data            cipher.Block
	tweak           cipher.Block
	cryptStart      uint64
	cachedNum       int64
	cached          []byte
	tweakBuf        [16]byte
	encTweak        [16]byte
}

func newXTSReader(inner io.ReaderAt, dataKey, tweakKey []byte, cryptStartSector uint64) (*xtsReader, error) {
	data, err := aes.NewCipher(dataKey)
	if err != nil {
		return nil, fmt.Errorf("AES data key: %w", err)
	}
	tweak, err := aes.NewCipher(tweakKey)
	if err != nil {
		return nil, fmt.Errorf("AES tweak key: %w", err)
	}
	return &xtsReader{
		inner:      inner,
		data:       data,
		tweak:      tweak,
		cryptStart: cryptStartSector,
		cachedNum:  -1,
		cached:     make([]byte, xtsSectorSize),
	}, nil
}

func (r *xtsReader) ReadAt(p []byte, off int64) (int, error) {
	if off < 0 {
		return 0, fmt.Errorf("negative offset %d", off)
	}
	n := 0
	for n < len(p) {
		sector := uint64(off) / xtsSectorSize
		into := int(uint64(off) % xtsSectorSize)
		if err := r.fill(int64(sector)); err != nil {
			if n > 0 {
				return n, err
			}
			return 0, err
		}
		copied := copy(p[n:], r.cached[into:])
		if copied == 0 {
			return n, io.EOF
		}
		n += copied
		off += int64(copied)
	}
	return n, nil
}

func (r *xtsReader) fill(sector int64) error {
	if r.cachedNum == sector {
		return nil
	}
	_, err := r.inner.ReadAt(r.cached, sector*xtsSectorSize)
	if err != nil && err != io.EOF {
		return err
	}
	if uint64(sector) >= r.cryptStart {
		decryptXTSSector(r.data, r.tweak, r.cached, uint64(sector), &r.tweakBuf, &r.encTweak)
	}
	r.cachedNum = sector
	return nil
}

func decryptXTSSector(data, tweak cipher.Block, sector []byte, sectorNum uint64, tweakBuf, encTweak *[16]byte) {
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
		data.Decrypt(block[:], block[:])
		for i := 0; i < 16; i++ {
			sector[off+i] = block[i] ^ encTweak[i]
		}
		gf128Double(encTweak)
	}
}

// gf128Double multiplies the 16-byte little-endian GF(2^128) value by 2
// using the XTS polynomial (x^128 + x^7 + x^2 + x + 1).
func gf128Double(t *[16]byte) {
	var feedback byte
	for k := 0; k < 16; k++ {
		tmp := t[k]
		t[k] = byte(int(t[k])<<1) | feedback
		feedback = tmp >> 7
	}
	if feedback != 0 {
		t[0] ^= 0x87
	}
}
