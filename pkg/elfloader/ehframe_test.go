package elfloader

import (
	"encoding/binary"
	"os"
	"testing"
)

func TestHelloWorldUnwindBounds(t *testing.T) {
	loaded, err := LoadELF("../../hello_world.elf")
	if err != nil {
		t.Fatal(err)
	}
	if len(loaded.FuncBounds) < 4000 {
		t.Fatalf("unwind functions = %d, want thousands", len(loaded.FuncBounds))
	}
	if !loaded.IsFuncEntry(0x20) {
		t.Fatal("0x20 is the first FDE entry")
	}
	if got := loaded.CodeCeiling(0x20); got != 0x51 {
		t.Fatalf("ceiling(0x20) = %#x, want %#x", got, uint64(0x51))
	}
	if loaded.InData(0x20) {
		t.Fatal("function entry marked as unwind data")
	}
	if !loaded.InData(0xd1178) {
		t.Fatal(".eh_frame was not marked as data")
	}
	// A gap between extents stops at the next function, not at the RX end.
	if ceil := loaded.CodeCeiling(0x51); ceil == 0 || ceil > loaded.FuncBounds[0].End+0x100000 {
		t.Fatalf("gap ceiling = %#x", ceil)
	}
}

func TestExceptionsLSDABounds(t *testing.T) {
	loaded, err := LoadELF("../../exceptions.elf")
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("FuncBounds: %d, LSDABounds: %d", len(loaded.FuncBounds), len(loaded.LSDABounds))
	if len(loaded.FuncBounds) == 0 {
		t.Fatal("expected FuncBounds > 0")
	}
	if len(loaded.LSDABounds) == 0 {
		t.Fatal("expected LSDABounds > 0")
	}
	if len(loaded.LSDABounds) >= len(loaded.FuncBounds) {
		t.Fatalf("expected LSDABounds (%d) < FuncBounds (%d)", len(loaded.LSDABounds), len(loaded.FuncBounds))
	}
}

func TestSyntheticEHFrame(t *testing.T) {
	const (
		fn      = 0x40
		fnSize  = 3
		frame   = 0x100
		hdr     = 0x200
		hdrSize = 16
	)
	img := make([]byte, 0x300)
	// CIE: length covers id..encoding.
	cie := []byte{
		0, 0, 0, 0, // length patched
		0, 0, 0, 0, // CIE id
		1,
		'z', 'R', 0,
		1,    // code alignment
		0x78, // data alignment -8
		16,   // return register
		1,    // augmentation length
		0x1b, // pcrel | sdata4
	}
	binary.LittleEndian.PutUint32(cie[0:], uint32(len(cie)-4))
	copy(img[frame:], cie)
	fdeAt := frame + len(cie)
	fde := []byte{
		0, 0, 0, 0, // length patched
		0, 0, 0, 0, // CIE pointer patched
		0, 0, 0, 0, // pc_begin
		fnSize, 0, 0, 0,
		0, // augmentation length
	}
	binary.LittleEndian.PutUint32(fde[0:], uint32(len(fde)-4))
	ciePtrField := fdeAt + 4
	binary.LittleEndian.PutUint32(fde[4:], uint32(ciePtrField-frame))
	pcField := fdeAt + 8
	pcRel := int32(fn - pcField)
	binary.LittleEndian.PutUint32(fde[8:], uint32(pcRel))
	copy(img[fdeAt:], fde)
	// terminator
	// already zero

	img[hdr] = 1
	img[hdr+1] = 0x1b
	img[hdr+2] = 0x03
	img[hdr+3] = 0x3b
	frameRel := int32(frame - (hdr + 4))
	binary.LittleEndian.PutUint32(img[hdr+4:], uint32(frameRel))
	binary.LittleEndian.PutUint32(img[hdr+8:], 1)

	loaded := &LoadedELF{MemoryImage: img}
	if !parseEHFrameHdr(loaded, hdr, hdrSize, 0) {
		t.Fatal("parse failed")
	}
	if len(loaded.FuncBounds) != 1 {
		t.Fatalf("bounds = %+v", loaded.FuncBounds)
	}
	if loaded.FuncBounds[0].Start != fn || loaded.FuncBounds[0].End != fn+fnSize {
		t.Fatalf("bound = %#x-%#x", loaded.FuncBounds[0].Start, loaded.FuncBounds[0].End)
	}
	if !loaded.InData(frame) || !loaded.InData(hdr) || loaded.InData(fn) {
		t.Fatalf("data ranges = %+v", loaded.DataRanges)
	}
}

func TestUnwindOnEbootFromEnv(t *testing.T) {
	path := os.Getenv("PS4RECOMP_EBOOT")
	if path == "" {
		t.Skip("PS4RECOMP_EBOOT not set")
	}
	loaded, err := LoadELF(path)
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("entry=%#x unwind=%d data=%d exec=%d image=%dMB",
		loaded.EntryPoint, len(loaded.FuncBounds), len(loaded.DataRanges), len(loaded.ExecRanges),
		len(loaded.MemoryImage)>>20)
	for i, r := range loaded.DataRanges {
		t.Logf("data[%d] %#x-%#x (%dKB)", i, r.Start, r.End, (r.End-r.Start)>>10)
	}
	if len(loaded.FuncBounds) < 100000 {
		t.Fatalf("unwind functions = %d", len(loaded.FuncBounds))
	}
	if loaded.InData(loaded.EntryPoint) {
		t.Fatal("entry point falls in unwind data")
	}
	if !loaded.IsFuncEntry(loaded.EntryPoint) && loaded.CodeCeiling(loaded.EntryPoint) == 0 {
		t.Fatalf("entry %#x is not covered by an unwind extent", loaded.EntryPoint)
	}
	t.Logf("entry ceiling=%#x first=%#x-%#x last=%#x-%#x",
		loaded.CodeCeiling(loaded.EntryPoint),
		loaded.FuncBounds[0].Start, loaded.FuncBounds[0].End,
		loaded.FuncBounds[len(loaded.FuncBounds)-1].Start, loaded.FuncBounds[len(loaded.FuncBounds)-1].End)
}

func TestApplyBiasShiftsUnwind(t *testing.T) {
	l := &LoadedELF{
		MemoryImage: make([]byte, 0x2000),
		MinVAddr:    0x1000,
		MaxVAddr:    0x2000,
		FuncBounds:  []AddrRange{{Start: 0x1000, End: 0x1010}},
		DataRanges:  []AddrRange{{Start: 0x1800, End: 0x1900}},
	}
	if err := l.ApplyBias(0x5000); err != nil {
		t.Fatal(err)
	}
	if l.FuncBounds[0].Start != 0x6000 || l.FuncBounds[0].End != 0x6010 {
		t.Fatalf("bounds = %+v", l.FuncBounds)
	}
	if l.DataRanges[0].Start != 0x6800 || l.DataRanges[0].End != 0x6900 {
		t.Fatalf("data = %+v", l.DataRanges)
	}
}
