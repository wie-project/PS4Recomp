package ps4pkg

import (
	"bytes"
	"compress/flate"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
)

const pfscMagic = 0x43534650 // "PFSC"

type pfscReader struct {
	inner       io.ReaderAt
	blockSize   int
	dataLength  int64
	sectorMap   []int64
	cachedIdx   int
	cached      []byte
}

func newPFSCReader(inner io.ReaderAt) (*pfscReader, error) {
	var hdr [0x30]byte
	if _, err := inner.ReadAt(hdr[:], 0); err != nil {
		return nil, fmt.Errorf("failed to read PFSC header: %w", err)
	}
	magic := binary.LittleEndian.Uint32(hdr[0:4])
	if magic != pfscMagic {
		return nil, errors.New("not a PFSC image")
	}
	blockSz := int(binary.LittleEndian.Uint32(hdr[0x0C:]))
	blockSz2 := int64(binary.LittleEndian.Uint64(hdr[0x10:]))
	blockOffsets := int64(binary.LittleEndian.Uint64(hdr[0x18:]))
	dataLength := int64(binary.LittleEndian.Uint64(hdr[0x28:]))
	if blockSz == 0 || int64(blockSz) != blockSz2 {
		return nil, fmt.Errorf("PFSC block size mismatch (%d vs %d)", blockSz, blockSz2)
	}
	if dataLength < 0 || blockSz2 == 0 {
		return nil, errors.New("invalid PFSC data length")
	}
	numBlocks := int(dataLength / blockSz2)
	mapBytes := make([]byte, (numBlocks+1)*8)
	if _, err := inner.ReadAt(mapBytes, blockOffsets); err != nil && err != io.EOF {
		return nil, fmt.Errorf("failed to read PFSC sector map: %w", err)
	}
	sectorMap := make([]int64, numBlocks+1)
	for i := 0; i < numBlocks+1; i++ {
		sectorMap[i] = int64(binary.LittleEndian.Uint64(mapBytes[i*8:]))
	}
	return &pfscReader{
		inner:      inner,
		blockSize:  blockSz,
		dataLength: dataLength,
		sectorMap:  sectorMap,
		cachedIdx:  -1,
		cached:     make([]byte, blockSz),
	}, nil
}

func (p *pfscReader) ReadAt(b []byte, off int64) (int, error) {
	if off < 0 {
		return 0, errors.New("negative offset")
	}
	if off >= p.dataLength {
		return 0, io.EOF
	}
	n := 0
	for n < len(b) && off < p.dataLength {
		idx := int(off / int64(p.blockSize))
		into := int(off % int64(p.blockSize))
		if err := p.fill(idx); err != nil {
			if n > 0 {
				return n, err
			}
			return 0, err
		}
		remain := p.blockSize - into
		if remain > len(b)-n {
			remain = len(b) - n
		}
		if off+int64(remain) > p.dataLength {
			remain = int(p.dataLength - off)
		}
		copy(b[n:n+remain], p.cached[into:into+remain])
		n += remain
		off += int64(remain)
	}
	if n < len(b) {
		return n, io.EOF
	}
	return n, nil
}

func (p *pfscReader) fill(idx int) error {
	if p.cachedIdx == idx {
		return nil
	}
	if idx < 0 || idx >= len(p.sectorMap)-1 {
		return fmt.Errorf("PFSC sector index %d out of range", idx)
	}
	off := p.sectorMap[idx]
	size := p.sectorMap[idx+1] - off
	if size == int64(p.blockSize) {
		if _, err := p.inner.ReadAt(p.cached, off); err != nil && err != io.EOF {
			return err
		}
	} else if size > int64(p.blockSize) {
		for i := range p.cached {
			p.cached[i] = 0
		}
	} else {
		if size < 2 {
			return fmt.Errorf("compressed PFSC sector %d too small (%d)", idx, size)
		}
		comp := make([]byte, size-2)
		if _, err := p.inner.ReadAt(comp, off+2); err != nil && err != io.EOF {
			return err
		}
		fr := flate.NewReader(bytes.NewReader(comp))
		defer fr.Close()
		got, err := io.ReadFull(fr, p.cached)
		if err != nil && err != io.ErrUnexpectedEOF && err != io.EOF {
			return fmt.Errorf("PFSC inflate sector %d: %w", idx, err)
		}
		if got < p.blockSize {
			for i := got; i < p.blockSize; i++ {
				p.cached[i] = 0
			}
		}
	}
	p.cachedIdx = idx
	return nil
}
