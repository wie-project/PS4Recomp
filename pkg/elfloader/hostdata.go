// SPDX-License-Identifier: GPL-2.0-or-later

package elfloader

import "encoding/binary"

// The host data page holds the data symbols the runtime's HLE libraries export (shadPS4's
// LIB_OBJ): the stack canary, and libSceNet's in6addr constants. The page sits right after the
// image, in the writable guest image like any other data, so each export is the guest's own
// writable copy (shadPS4 gives every LIB_OBJ its own object too). GLOB_DAT and R_X86_64_64
// import slots of these symbols get the address of their copy at build time.

// hostDataPageSize is the size of the host data page.
const hostDataPageSize = 4096

// stackCanaryValue is the __stack_chk_guard value.
const stackCanaryValue = uint64(0x595e9fbd94fda766)

type hostDataSymbol struct {
	nid    string // import NID
	name   string // plaintext name, matched too (for images that import by name)
	offset uint64 // offset in the host data page
	value  []byte
}

func in6Addr(first, second, last byte) []byte {
	a := make([]byte, 16)
	a[0], a[1], a[15] = first, second, last
	return a
}

func canaryBytes() []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, stackCanaryValue)
	return b
}

// hostDataSymbols lists the page's contents. The canary stays at offset 0.
var hostDataSymbols = []hostDataSymbol{
	{nid: "f7uOxY9mM1U", name: "__stack_chk_guard", offset: 0x00, value: canaryBytes()},
	// libSceNet (shadPS4 libraries/net/net.cpp RegisterLib).
	{nid: "ZRAJo-A-ukc", name: "in6addr_any", offset: 0x10, value: in6Addr(0, 0, 0)},
	{nid: "XCuA-GqjA-k", name: "in6addr_loopback", offset: 0x20, value: in6Addr(0, 0, 1)},
	{nid: "VZgoeBxPXUQ", name: "sce_net_dummy", offset: 0x30, value: make([]byte, 4)},
	{nid: "GAtITrgxKDE", name: "sce_net_in6addr_any", offset: 0x40, value: in6Addr(0, 0, 0)},
	{nid: "84MgU4MMTLQ", name: "sce_net_in6addr_linklocal_allnodes", offset: 0x50, value: in6Addr(0xff, 2, 1)},
	{nid: "2uSWyOKYc1M", name: "sce_net_in6addr_linklocal_allrouters", offset: 0x60, value: in6Addr(0xff, 2, 2)},
	{nid: "P3AeWBvPrkg", name: "sce_net_in6addr_loopback", offset: 0x70, value: in6Addr(0, 0, 1)},
	{nid: "PgNI+j4zxzM", name: "sce_net_in6addr_nodelocal_allnodes", offset: 0x80, value: in6Addr(0xff, 1, 1)},
}

// HostDataOffset returns the offset in the host data page of the data symbol an import names
// (an NID-encoded dynsym name, a bare NID or a plaintext name; the NIDs are the names' hashes).
func HostDataOffset(symName string) (uint64, bool) {
	if symName == "" {
		return 0, false
	}
	nid := NIDPrefix(symName)
	for i := range hostDataSymbols {
		s := &hostDataSymbols[i]
		if symName == s.name || nid == s.nid {
			return s.offset, true
		}
	}
	return 0, false
}

// IsHostDataSymbol reports whether an import is served by the host data page.
func IsHostDataSymbol(symName string) bool {
	_, ok := HostDataOffset(symName)
	return ok
}

// writeHostData writes the page's contents at page (an offset into image).
func writeHostData(image []byte, page uint64) {
	if page+hostDataPageSize > uint64(len(image)) {
		return
	}
	for i := range hostDataSymbols {
		s := &hostDataSymbols[i]
		copy(image[page+s.offset:], s.value)
	}
}

// bindHostDataSlot points the import slot at offset to its symbol's copy in the page at page.
// It returns false when symName is not a host data symbol.
func bindHostDataSlot(image []byte, page, offset uint64, symName string, addend int64) bool {
	off, ok := HostDataOffset(symName)
	if !ok || page == 0 {
		return ok
	}
	if offset+8 <= uint64(len(image)) {
		binary.LittleEndian.PutUint64(image[offset:offset+8], page+off+uint64(addend))
	}
	return true
}
