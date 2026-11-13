package elfpatch_test

import (
	"bytes"
	"debug/elf"
	"encoding/binary"
	"slices"
	"strings"
	"testing"

	"ps4-recomp/pkg/disasm"
	"ps4-recomp/pkg/elfloader"
	"ps4-recomp/pkg/elfpatch"
)

type testSeg struct {
	off, vaddr uint64
	data       []byte
	memsz      uint64
	flags      elf.ProgFlag
}

// testELF builds an x86-64 ET_SCE_DYNEXEC ELF with the given PT_LOAD segments and entry point.
func testELF(entry uint64, segs []testSeg) []byte {
	size := uint64(64 + 56*len(segs))
	for _, s := range segs {
		size = max(size, s.off+uint64(len(s.data)))
	}
	b := make([]byte, size)
	copy(b, []byte{0x7f, 'E', 'L', 'F', 2, 1, 1})
	le := binary.LittleEndian
	le.PutUint16(b[16:], 0xfe10)
	le.PutUint16(b[18:], uint16(elf.EM_X86_64))
	le.PutUint32(b[20:], 1)
	le.PutUint64(b[24:], entry)
	le.PutUint64(b[32:], 64)
	le.PutUint16(b[52:], 64)
	le.PutUint16(b[54:], 56)
	le.PutUint16(b[56:], uint16(len(segs)))
	le.PutUint16(b[58:], 64)
	for i, s := range segs {
		ph := b[64+56*i:]
		le.PutUint32(ph[0:], uint32(elf.PT_LOAD))
		le.PutUint32(ph[4:], uint32(s.flags))
		le.PutUint64(ph[8:], s.off)
		le.PutUint64(ph[16:], s.vaddr)
		le.PutUint64(ph[24:], s.vaddr)
		le.PutUint64(ph[32:], uint64(len(s.data)))
		le.PutUint64(ph[40:], s.memsz)
		le.PutUint64(ph[48:], 0x1000)
		copy(b[s.off:], s.data)
	}
	return b
}

// Code at guest 0x1000 (file 0x800): "call 0x1010; ret" and a second function "ret" at 0x1010.
// Data at guest 0x4000: 0x20 file bytes, then .bss up to 0x5000.
func gameELF() []byte {
	code := make([]byte, 0x100)
	copy(code, []byte{0xe8, 0x0b, 0, 0, 0, 0xc3})
	code[0x10] = 0xc3
	data := make([]byte, 0x20)
	for i := range data {
		data[i] = byte(i)
	}
	return testELF(0x1000, []testSeg{
		{off: 0x800, vaddr: 0x1000, data: code, memsz: 0x100, flags: elf.PF_R | elf.PF_X},
		{off: 0x1100, vaddr: 0x4000, data: data, memsz: 0x1000, flags: elf.PF_R | elf.PF_W},
	})
}

const patchXML = `<?xml version="1.0" encoding="utf-8"?>
<Patch>
  <TitleID><ID>CUSA00001</ID><ID>CUSA00002</ID></TitleID>
  <Metadata Title="T" Name="Skip Call" Author="a" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin">
    <PatchList>
      <Line Type="bytes" Address="0x00401000" Value="0f1f440000"/>
    </PatchList>
  </Metadata>
  <Metadata Title="T" Name="Data" Author="a" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin">
    <PatchList>
      <Line Type="bytes32" Address="0x00404004" Value="0xf00"/>
      <Line Type="bytes32" Address="0x00404008" Value="2160"/>
      <Line Type="bytes16" Address="0x0040400c" Value="0xbeef"/>
      <Line Type="byte" Address="0x0040400e" Value="7"/>
      <Line Type="bytes64" Address="0x00404010" Value="0x1122334455667788"/>
      <Line Type="float32" Address="0x00404018" Value="1.5"/>
      <Line Type="utf8" Address="0x0040401c" Value="ab"/>
    </PatchList>
  </Metadata>
  <Metadata Title="T" Name="Data" Author="a" PatchVer="1.0" AppVer="01.02" AppElf="eboot.bin">
    <PatchList><Line Type="bytes" Address="0x00404000" Value="ff"/></PatchList>
  </Metadata>
  <Metadata Title="T" Name="Scan" AppVer="01.00" AppElf="eboot.bin">
    <PatchList><Line Type="mask" Address="c3 ?? c3" Value="90"/></PatchList>
  </Metadata>
  <Metadata Title="T" Name="Bss" AppVer="01.00" AppElf="eboot.bin">
    <PatchList><Line Type="bytes" Address="0x00404020" Value="01"/></PatchList>
  </Metadata>
  <Metadata Title="T" Name="Prx" AppVer="01.00" AppElf="libfoo.prx">
    <PatchList><Line Type="bytes" Address="0x00404000" Value="01"/></PatchList>
  </Metadata>
  <Metadata Title="T" Name="Overlap" AppVer="01.00" AppElf="eboot.bin">
    <PatchList><Line Type="bytes" Address="0x00404006" Value="0102"/></PatchList>
  </Metadata>
</Patch>`

var target = elfpatch.Target{TitleID: "CUSA00002", AppVer: "01.00", ElfName: "EBOOT.BIN"}

func parse(t *testing.T) *elfpatch.File {
	t.Helper()
	f, err := elfpatch.Parse([]byte(patchXML))
	if err != nil {
		t.Fatal(err)
	}
	return f
}

func TestParse(t *testing.T) {
	f := parse(t)
	if !slices.Equal(f.TitleIDs, []string{"CUSA00001", "CUSA00002"}) {
		t.Fatalf("title ids %v", f.TitleIDs)
	}
	if len(f.Sets) != 7 || f.Sets[1].Name != "Data" || len(f.Sets[1].Lines) != 7 {
		t.Fatalf("sets %+v", f.Sets)
	}
	l := f.Sets[1].Lines[0]
	if l.Type != "bytes32" || l.Address != "0x00404004" || l.Value != "0xf00" {
		t.Fatalf("line %+v", l)
	}
	for _, bad := range []string{"<Patch>", "<Patch></Patch>", `<Patch><TitleID><ID>X</ID></TitleID><Metadata/></Patch>`} {
		if _, err := elfpatch.Parse([]byte(bad)); err == nil {
			t.Errorf("Parse(%q) succeeded", bad)
		}
	}
}

func TestSelectDecodesValues(t *testing.T) {
	sets, err := parse(t).Select([]string{"Data", "Skip Call"}, target)
	if err != nil {
		t.Fatal(err)
	}
	if sets[0].Name != "Data" || sets[1].Name != "Skip Call" {
		t.Fatalf("order %s, %s", sets[0].Name, sets[1].Name)
	}
	want := [][]byte{
		{0x00, 0x0f, 0, 0},
		{0x70, 0x08, 0, 0},
		{0xef, 0xbe},
		{7},
		{0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11},
		{0, 0, 0xc0, 0x3f},
		{'a', 'b', 0},
	}
	for i, w := range sets[0].Writes {
		if !bytes.Equal(w.Data, want[i]) {
			t.Errorf("line %d (%s): %x, want %x", i, w.Type, w.Data, want[i])
		}
	}
	if w := sets[1].Writes[0]; w.Address != 0x401000 || !bytes.Equal(w.Data, []byte{0x0f, 0x1f, 0x44, 0, 0}) {
		t.Fatalf("bytes line %+v", w)
	}
}

func TestSelectErrors(t *testing.T) {
	f := parse(t)
	cases := []struct {
		names []string
		t     elfpatch.Target
		want  string
	}{
		{[]string{"Data"}, elfpatch.Target{TitleID: "CUSA99999", AppVer: "01.00", ElfName: "eboot.bin"}, "the game is CUSA99999"},
		{[]string{"Data"}, elfpatch.Target{TitleID: "CUSA00001", AppVer: "01.05", ElfName: "eboot.bin"}, "app version 01.00, 01.02, the game is 01.05"},
		{[]string{"Missing"}, target, `"Missing" is not in the patch file`},
		{[]string{"Data", "Data"}, target, "requested twice"},
		{[]string{"Prx"}, target, "is for libfoo.prx"},
		{[]string{"Scan"}, target, `unsupported patch type "mask"`},
	}
	for _, c := range cases {
		_, err := f.Select(c.names, c.t)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("Select(%v): %v, want %q", c.names, err, c.want)
		}
	}
}

func TestBadValues(t *testing.T) {
	for _, line := range []string{
		`Type="bytes" Address="0x400000" Value="abc"`,
		`Type="bytes" Address="0x400000" Value="zz"`,
		`Type="bytes" Address="0x400000" Value=""`,
		`Type="bytes32" Address="0x400000" Value="0x100000000"`,
		`Type="byte" Address="0x400000" Value="256"`,
		`Type="bytes32" Address="0x400000" Value="$ff"`,
		`Type="float32" Address="0x400000" Value="x"`,
		`Type="utf16" Address="0x400000" Value="a"`,
		`Type="bytes" Address="zz" Value="00"`,
	} {
		xml := `<Patch><TitleID><ID>CUSA00002</ID></TitleID><Metadata Name="P" AppVer="01.00" AppElf="eboot.bin"><PatchList><Line ` + line + `/></PatchList></Metadata></Patch>`
		f, err := elfpatch.Parse([]byte(xml))
		if err != nil {
			t.Fatal(err)
		}
		if _, err := f.Select([]string{"P"}, target); err == nil {
			t.Errorf("line %s accepted", line)
		}
	}
}

func TestApplyMapsAddressesToFileOffsets(t *testing.T) {
	img := gameELF()
	sets, err := parse(t).Select([]string{"Skip Call", "Data"}, target)
	if err != nil {
		t.Fatal(err)
	}
	if err := elfpatch.Apply(img, sets); err != nil {
		t.Fatal(err)
	}
	code := sets[0].Writes[0]
	if code.Vaddr != 0x1000 || code.Offset != 0x800 || !bytes.Equal(code.Orig, []byte{0xe8, 0x0b, 0, 0, 0}) {
		t.Fatalf("code write %+v", code)
	}
	if !bytes.Equal(img[0x800:0x805], []byte{0x0f, 0x1f, 0x44, 0, 0}) {
		t.Fatalf("code bytes %x", img[0x800:0x806])
	}
	d := sets[1].Writes[0]
	if d.Vaddr != 0x4004 || d.Offset != 0x1104 || !bytes.Equal(d.Orig, []byte{4, 5, 6, 7}) {
		t.Fatalf("data write %+v", d)
	}
	if !bytes.Equal(img[0x1104:0x1108], []byte{0, 0x0f, 0, 0}) {
		t.Fatalf("data bytes %x", img[0x1104:0x1108])
	}
	rep := (&elfpatch.Result{Path: "p.xml", Target: target, Sets: sets}).Report()
	if !strings.Contains(rep, "## Data (7 lines, 26 bytes)") ||
		!strings.Contains(rep, "bytes    0x00401000 guest 0x1000..0x1005 file 0x800: e80b000000 -> 0f1f440000") {
		t.Fatalf("report:\n%s", rep)
	}
}

func TestApplyRejectsUnbackedAndOverlappingWrites(t *testing.T) {
	f := parse(t)
	cases := []struct {
		names []string
		want  string
	}{
		{[]string{"Bss"}, "not inside the file-backed part"},
		{[]string{"Data", "Overlap"}, "also writes"},
	}
	for _, c := range cases {
		img := gameELF()
		orig := bytes.Clone(img)
		sets, err := f.Select(c.names, target)
		if err != nil {
			t.Fatal(err)
		}
		err = elfpatch.Apply(img, sets)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("Apply(%v): %v, want %q", c.names, err, c.want)
		}
		if !bytes.Equal(img, orig) {
			t.Errorf("Apply(%v) modified the image after failing", c.names)
		}
	}
	for _, w := range []elfpatch.Write{
		{Address: 0x3fffff, Data: []byte{1}},       // below the executable base
		{Address: 0x4010fe, Data: []byte{1, 2, 3}}, // crosses the end of the code segment
		{Address: 0x402000, Data: []byte{1}},       // between segments
	} {
		img := gameELF()
		if err := elfpatch.Apply(img, []elfpatch.AppliedSet{{Name: "W", Writes: []elfpatch.Write{w}}}); err == nil {
			t.Errorf("write at 0x%x accepted", w.Address)
		}
	}
}

func TestCheckRelocations(t *testing.T) {
	sets := []elfpatch.AppliedSet{{Name: "W", Writes: []elfpatch.Write{{Vaddr: 0x4004, Data: []byte{1, 2, 3, 4}}}}}
	if err := elfpatch.CheckRelocations(sets, []elfloader.Relocation{{Offset: 0x4008, Type: uint32(elf.R_X86_64_RELATIVE)}}); err != nil {
		t.Fatalf("adjacent relocation: %v", err)
	}
	err := elfpatch.CheckRelocations(sets, []elfloader.Relocation{{Offset: 0x4000, Type: uint32(elf.R_X86_64_RELATIVE)}})
	if err == nil || !strings.Contains(err.Error(), "overlaps the target of relocation") {
		t.Fatalf("overlapping relocation: %v", err)
	}
}

// discover runs CFG discovery from the entry point the way the build does.
func discover(t *testing.T, img []byte) ([]uint64, *elfloader.LoadedELF) {
	t.Helper()
	loaded, err := elfloader.LoadELFImage("eboot.bin", img)
	if err != nil {
		t.Fatal(err)
	}
	d, err := disasm.NewDisassembler(loaded)
	if err != nil {
		t.Fatal(err)
	}
	funcs, err := d.DiscoverReachable(disasm.SeedEntryPoints(loaded, false), nil)
	if err != nil {
		t.Fatal(err)
	}
	var addrs []uint64
	for _, f := range funcs {
		addrs = append(addrs, f.Addr)
	}
	slices.Sort(addrs)
	return addrs, loaded
}

func TestAnalysisSeesPatchedBytes(t *testing.T) {
	plain, _ := discover(t, gameELF())
	if !slices.Equal(plain, []uint64{0x1000, 0x1010}) {
		t.Fatalf("unpatched functions %x", plain)
	}

	img := gameELF()
	sets, err := parse(t).Select([]string{"Skip Call", "Data"}, target)
	if err != nil {
		t.Fatal(err)
	}
	if err := elfpatch.Apply(img, sets); err != nil {
		t.Fatal(err)
	}
	patched, loaded := discover(t, img)
	if !slices.Equal(patched, []uint64{0x1000}) {
		t.Fatalf("patched functions %x: the call was replaced by a nop", patched)
	}
	// The guest image (guest_image.bin) carries the patched code and data.
	if !bytes.Equal(loaded.MemoryImage[0x1000:0x1005], []byte{0x0f, 0x1f, 0x44, 0, 0}) {
		t.Fatalf("image code %x", loaded.MemoryImage[0x1000:0x1005])
	}
	if got := binary.LittleEndian.Uint32(loaded.MemoryImage[0x4004:]); got != 0xf00 {
		t.Fatalf("image data 0x%x", got)
	}
}
