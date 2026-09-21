package ps4pkg

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"strings"
)

const (
	pfsMagic             int64  = 20130315
	pfsModeSigned        uint16 = 0x1
	pfsModeEncrypted     uint16 = 0x4
	inodeFlagCompressed  uint32 = 0x1
	direntFile           int32  = 2
	direntDirectory      int32  = 3
	maxPFSBlocks         int64  = 100_000_000
)

// PfsHeader is the little-endian PFS superblock.
type PfsHeader struct {
	Version          int64
	Magic            int64
	Id               int64
	Mode             uint16
	BlockSize        uint32
	DinodeCount      int64
	Ndblock          int64
	DinodeBlockCount int64
	IndirectCount    int
	Seed             []byte
}

// PfsNode is a file or directory in a PFS image.
type PfsNode struct {
	Name           string
	Path           string
	IsDir          bool
	Size           int64
	CompressedSize int64
	Compressed     bool
	ino            uint32
	startBlock     int32
	blocks         []int32
	blockSize      int
	children       []*PfsNode
}

// PfsImage is a parsed (and optionally decrypted) PFS filesystem.
type PfsImage struct {
	Header PfsHeader
	reader io.ReaderAt
	inodes []pfsInode
	Root   *PfsNode
	URoot  *PfsNode
}

type pfsInode struct {
	Mode           uint16
	Nlink          uint16
	Flags          uint32
	Size           int64
	SizeCompressed int64
	Blocks         uint32
	Direct         [12]int32
	Indirect       [5]int32
}

type offsetReader struct {
	r      io.ReaderAt
	base   int64
	length int64
}

func (o *offsetReader) ReadAt(p []byte, off int64) (int, error) {
	if off < 0 {
		return 0, errors.New("negative offset")
	}
	if o.length >= 0 && off >= o.length {
		return 0, io.EOF
	}
	want := len(p)
	if o.length >= 0 && off+int64(want) > o.length {
		want = int(o.length - off)
	}
	n, err := o.r.ReadAt(p[:want], o.base+off)
	if n < len(p) && err == nil {
		err = io.EOF
	}
	return n, err
}

type chunkedReader struct {
	r         io.ReaderAt
	blockSize int
	blocks    []int32
	length    int64
}

func (c *chunkedReader) ReadAt(p []byte, off int64) (int, error) {
	if off < 0 {
		return 0, errors.New("negative offset")
	}
	if off >= c.length {
		return 0, io.EOF
	}
	n := 0
	for n < len(p) && off < c.length {
		idx := int(off) / c.blockSize
		if idx >= len(c.blocks) {
			return n, io.EOF
		}
		into := int(off) % c.blockSize
		remain := c.blockSize - into
		if remain > len(p)-n {
			remain = len(p) - n
		}
		if off+int64(remain) > c.length {
			remain = int(c.length - off)
		}
		phys := int64(c.blocks[idx]) * int64(c.blockSize) + int64(into)
		got, err := c.r.ReadAt(p[n:n+remain], phys)
		n += got
		off += int64(got)
		if err != nil {
			if n > 0 && err == io.EOF {
				return n, nil
			}
			return n, err
		}
		if got < remain {
			return n, io.EOF
		}
	}
	if n < len(p) {
		return n, io.EOF
	}
	return n, nil
}

func parsePfsHeader(buf []byte) (PfsHeader, error) {
	var h PfsHeader
	if len(buf) < 0x380 {
		return h, errors.New("PFS superblock too short")
	}
	h.Version = int64(binary.LittleEndian.Uint64(buf[0x00:]))
	h.Magic = int64(binary.LittleEndian.Uint64(buf[0x08:]))
	h.Id = int64(binary.LittleEndian.Uint64(buf[0x10:]))
	h.Mode = binary.LittleEndian.Uint16(buf[0x1C:])
	h.BlockSize = binary.LittleEndian.Uint32(buf[0x20:])
	h.DinodeCount = int64(binary.LittleEndian.Uint64(buf[0x30:]))
	h.Ndblock = int64(binary.LittleEndian.Uint64(buf[0x38:]))
	h.DinodeBlockCount = int64(binary.LittleEndian.Uint64(buf[0x40:]))
	if h.Version != 1 || h.Magic != pfsMagic {
		return h, fmt.Errorf("invalid PFS superblock (version=%d magic=%d)", h.Version, h.Magic)
	}
	if h.BlockSize == 0 || h.BlockSize > 0x100000 {
		return h, fmt.Errorf("invalid PFS block size %d", h.BlockSize)
	}
	// InodeBlockSig is a signed 64-bit inode at 0x50. Indirect blocks start after
	// 12 direct (32-byte sig + 8-byte block) entries following the 0x68-byte header of that inode.
	// DinodeS64: 0x50 + 0x68 (up to Blocks as int64 + 4 pad) = 0x50+0x68 = 0xB8 for first direct.
	// Direct[i] block field at 0x50 + 0x68 + i*40 + 32
	const dinodeS64Prefix = 0x68
	indirectBase := 0x50 + dinodeS64Prefix + 12*40
	for i := 0; i < 5; i++ {
		off := indirectBase + i*40 + 32
		if off+8 > len(buf) {
			break
		}
		blk := int64(binary.LittleEndian.Uint64(buf[off:]))
		if blk > 0 {
			h.IndirectCount++
		}
	}
	h.Seed = make([]byte, 16)
	copy(h.Seed, buf[0x370:0x380])
	return h, nil
}

func readDinodeD32(b []byte) pfsInode {
	var in pfsInode
	in.Mode = binary.LittleEndian.Uint16(b[0:])
	in.Nlink = binary.LittleEndian.Uint16(b[2:])
	in.Flags = binary.LittleEndian.Uint32(b[4:])
	in.Size = int64(binary.LittleEndian.Uint64(b[8:]))
	in.SizeCompressed = int64(binary.LittleEndian.Uint64(b[16:]))
	in.Blocks = binary.LittleEndian.Uint32(b[96:])
	for i := 0; i < 12; i++ {
		in.Direct[i] = int32(binary.LittleEndian.Uint32(b[100+i*4:]))
	}
	for i := 0; i < 5; i++ {
		in.Indirect[i] = int32(binary.LittleEndian.Uint32(b[148+i*4:]))
	}
	return in
}

func readDinodeS32(b []byte) pfsInode {
	var in pfsInode
	in.Mode = binary.LittleEndian.Uint16(b[0:])
	in.Nlink = binary.LittleEndian.Uint16(b[2:])
	in.Flags = binary.LittleEndian.Uint32(b[4:])
	in.Size = int64(binary.LittleEndian.Uint64(b[8:]))
	in.SizeCompressed = int64(binary.LittleEndian.Uint64(b[16:]))
	in.Blocks = binary.LittleEndian.Uint32(b[96:])
	for i := 0; i < 12; i++ {
		in.Direct[i] = int32(binary.LittleEndian.Uint32(b[100+i*36+32:]))
	}
	for i := 0; i < 5; i++ {
		in.Indirect[i] = int32(binary.LittleEndian.Uint32(b[100+12*36+i*36+32:]))
	}
	return in
}

// OpenPFS parses a PFS image from r. ekpfs is required when the image is encrypted.
func OpenPFS(r io.ReaderAt, pfsFlags uint64, ekpfs []byte) (*PfsImage, error) {
	hdrBuf := make([]byte, 0x400)
	if _, err := r.ReadAt(hdrBuf, 0); err != nil && err != io.EOF {
		return nil, fmt.Errorf("failed to read PFS superblock: %w", err)
	}
	hdr, err := parsePfsHeader(hdrBuf)
	if err != nil {
		return nil, err
	}

	reader := r
	if hdr.Mode&pfsModeEncrypted != 0 {
		if len(ekpfs) != 32 {
			return nil, errors.New("PFS image is encrypted but EKPFS was not provided")
		}
		tweak, data := PfsGenEncKey(ekpfs, hdr.Seed, (pfsFlags&pfsFlagNewCrypt) != 0)
		start := uint64(hdr.BlockSize) / xtsSectorSize
		xr, err := newXTSReader(r, data, tweak, start)
		if err != nil {
			return nil, err
		}
		reader = xr
	}

	signed := hdr.Mode&pfsModeSigned != 0
	dinodeSize := 0xA8
	if signed {
		dinodeSize = 0x2C8
	}

	img := &PfsImage{
		Header: hdr,
		reader: reader,
		inodes: make([]pfsInode, hdr.DinodeCount),
	}

	maxPerSector := int(hdr.BlockSize) / dinodeSize
	sector := make([]byte, hdr.BlockSize)
	dinodeStart := int64(hdr.BlockSize) * int64(1+hdr.IndirectCount)
	total := 0
	for i := int64(0); i < hdr.DinodeBlockCount && total < int(hdr.DinodeCount); i++ {
		pos := dinodeStart + int64(hdr.BlockSize)*i
		if _, err := reader.ReadAt(sector, pos); err != nil && err != io.EOF {
			return nil, fmt.Errorf("failed to read inode block %d: %w", i, err)
		}
		for j := 0; j < maxPerSector && total < int(hdr.DinodeCount); j++ {
			raw := sector[j*dinodeSize : (j+1)*dinodeSize]
			if signed {
				img.inodes[total] = readDinodeS32(raw)
			} else {
				img.inodes[total] = readDinodeD32(raw)
			}
			total++
		}
	}

	if len(img.inodes) == 0 {
		return nil, errors.New("PFS image contains no inodes")
	}
	if img.inodes[0].Direct[0] < 1 || int64(img.inodes[0].Direct[0]) > maxPFSBlocks ||
		int64(img.inodes[0].Blocks) < 1 || int64(img.inodes[0].Blocks) > maxPFSBlocks {
		return nil, fmt.Errorf("inode 0 is corrupt (start=%d blocks=%d)", img.inodes[0].Direct[0], img.inodes[0].Blocks)
	}

	root, err := img.loadDir(0, "")
	if err != nil {
		return nil, err
	}
	img.Root = root
	for _, c := range root.children {
		if c.Name == "uroot" && c.IsDir {
			c.Path = "uroot"
			img.URoot = c
			break
		}
	}
	if img.URoot == nil {
		return nil, errors.New("invalid PFS image: missing uroot")
	}
	return img, nil
}

func (img *PfsImage) loadDir(dinode uint32, path string) (*PfsNode, error) {
	if int(dinode) >= len(img.inodes) {
		return nil, fmt.Errorf("inode %d out of range", dinode)
	}
	ino := img.inodes[dinode]
	start := ino.Direct[0]
	blocks := int64(ino.Blocks)
	if blocks < 1 || start < 1 || int64(start) > maxPFSBlocks || blocks > maxPFSBlocks {
		return nil, fmt.Errorf("inode %d is corrupt (start=%d blocks=%d)", dinode, start, blocks)
	}
	dir := &PfsNode{
		Name:      pathBase(path),
		Path:      path,
		IsDir:     true,
		ino:       dinode,
		blockSize: int(img.Header.BlockSize),
	}
	sector := make([]byte, img.Header.BlockSize)
	var pending []struct {
		ino  uint32
		name string
	}
	for b := int32(0); b < int32(blocks); b++ {
		blk := start + b
		if _, err := img.reader.ReadAt(sector, int64(blk)*int64(img.Header.BlockSize)); err != nil && err != io.EOF {
			return nil, fmt.Errorf("failed to read dirent block %d: %w", blk, err)
		}
		off := 0
		for off+16 <= len(sector) {
			entSize := int(binary.LittleEndian.Uint32(sector[off+12:]))
			if entSize <= 0 || off+entSize > len(sector) {
				break
			}
			inoNum := binary.LittleEndian.Uint32(sector[off:])
			typ := int32(binary.LittleEndian.Uint32(sector[off+4:]))
			nameLen := int(binary.LittleEndian.Uint32(sector[off+8:]))
			if nameLen < 0 || 16+nameLen > entSize {
				break
			}
			name := string(bytes.TrimRight(sector[off+16:off+16+nameLen], "\x00"))
			switch typ {
			case direntFile:
				f, err := img.loadFile(inoNum, joinPFS(path, name))
				if err != nil {
					return nil, err
				}
				dir.children = append(dir.children, f)
			case direntDirectory:
				pending = append(pending, struct {
					ino  uint32
					name string
				}{inoNum, name})
			}
			off += entSize
		}
	}
	for _, p := range pending {
		child, err := img.loadDir(p.ino, joinPFS(path, p.name))
		if err != nil {
			return nil, err
		}
		dir.children = append(dir.children, child)
	}
	return dir, nil
}

func (img *PfsImage) loadFile(dinode uint32, path string) (*PfsNode, error) {
	if int(dinode) >= len(img.inodes) {
		return nil, fmt.Errorf("inode %d out of range", dinode)
	}
	ino := img.inodes[dinode]
	n := &PfsNode{
		Name:           pathBase(path),
		Path:           path,
		Size:           ino.Size,
		CompressedSize: ino.SizeCompressed,
		Compressed:     ino.Flags&inodeFlagCompressed != 0,
		ino:            dinode,
		startBlock:     ino.Direct[0],
		blockSize:      int(img.Header.BlockSize),
	}
	if ino.Blocks > 1 && ino.Direct[1] != -1 && img.Header.Mode&pfsModeSigned != 0 {
		blocks, err := img.collectBlocks(ino)
		if err != nil {
			return nil, err
		}
		if !contiguous(blocks) {
			n.blocks = blocks
		}
	}
	return n, nil
}

func (img *PfsImage) collectBlocks(ino pfsInode) ([]int32, error) {
	blocks := make([]int32, ino.Blocks)
	remaining := int64(ino.Blocks)
	copied := 0
	for i := 0; i < 12 && copied < int(remaining); i++ {
		blocks[copied] = ino.Direct[i]
		copied++
	}
	remaining -= int64(copied)
	if remaining <= 0 {
		return blocks, nil
	}
	sigsPerBlock := int(img.Header.BlockSize) / 36
	buf := make([]byte, 4)
	readBlk := func(phys int64) (int32, error) {
		if _, err := img.reader.ReadAt(buf, phys); err != nil {
			return 0, err
		}
		return int32(binary.LittleEndian.Uint32(buf)), nil
	}
	// single-indirect
	for i := 0; i < int(remaining) && i < sigsPerBlock; i++ {
		v, err := readBlk(int64(ino.Indirect[0])*int64(img.Header.BlockSize) + int64(i*36+32))
		if err != nil {
			return nil, err
		}
		blocks[copied] = v
		copied++
	}
	remaining -= int64(min(int(remaining), sigsPerBlock))
	if remaining <= 0 {
		return blocks, nil
	}
	// double-indirect
	for j := 0; remaining > 0; j++ {
		indirectOff, err := readBlk(int64(ino.Indirect[1])*int64(img.Header.BlockSize) + int64(j*36+32))
		if err != nil {
			return nil, err
		}
		for i := 0; i < sigsPerBlock && remaining > 0; i++ {
			v, err := readBlk(int64(indirectOff)*int64(img.Header.BlockSize) + int64(i*36+32))
			if err != nil {
				return nil, err
			}
			blocks[copied] = v
			copied++
			remaining--
		}
	}
	return blocks, nil
}

func contiguous(blocks []int32) bool {
	if len(blocks) < 2 {
		return true
	}
	for i := 1; i < len(blocks); i++ {
		if blocks[i] != blocks[i-1]+1 {
			return false
		}
	}
	return true
}

func (n *PfsNode) Reader(img *PfsImage) (io.ReaderAt, int64) {
	size := n.Size
	var r io.ReaderAt
	if len(n.blocks) > 0 {
		r = &chunkedReader{r: img.reader, blockSize: n.blockSize, blocks: n.blocks, length: size}
	} else {
		r = &offsetReader{
			r:      img.reader,
			base:   int64(n.startBlock) * int64(n.blockSize),
			length: size,
		}
	}
	if n.Compressed {
		pr, err := newPFSCReader(r)
		if err == nil {
			return pr, pr.dataLength
		}
	}
	return r, size
}

// Lookup finds a node under uroot by relative path (forward slashes).
func (img *PfsImage) Lookup(rel string) *PfsNode {
	rel = strings.TrimPrefix(rel, "/")
	if rel == "" || rel == "uroot" {
		return img.URoot
	}
	n := img.URoot
	for _, part := range strings.Split(rel, "/") {
		if part == "" || n == nil {
			return nil
		}
		var next *PfsNode
		for _, c := range n.children {
			if c.Name == part {
				next = c
				break
			}
		}
		n = next
	}
	return n
}

// AllFiles returns every file under uroot.
func (img *PfsImage) AllFiles() []*PfsNode {
	var out []*PfsNode
	var walk func(*PfsNode)
	walk = func(n *PfsNode) {
		if n == nil {
			return
		}
		if !n.IsDir {
			out = append(out, n)
			return
		}
		for _, c := range n.children {
			walk(c)
		}
	}
	walk(img.URoot)
	return out
}

func joinPFS(dir, name string) string {
	if dir == "" {
		return name
	}
	return dir + "/" + name
}

func pathBase(p string) string {
	if i := strings.LastIndexByte(p, '/'); i >= 0 {
		return p[i+1:]
	}
	return p
}
