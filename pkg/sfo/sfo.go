// SPDX-License-Identifier: GPL-2.0-or-later

// Package sfo reads PS4 param.sfo files (the PSF key/value format in sce_sys/).
package sfo

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"sort"
)

const (
	fmtSpecial = 0x0004
	fmtString  = 0x0204
	fmtInt     = 0x0404
	headerSize = 20
	entrySize  = 16
)

type value struct {
	str   string
	num   uint32
	isInt bool
}

// File is a parsed param.sfo.
type File struct{ vals map[string]value }

// Entry is one key for Build.
type Entry struct {
	Key   string
	Str   string
	Int   uint32
	IsInt bool
}

// Load reads and parses a param.sfo file.
func Load(path string) (*File, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	f, err := Parse(b)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return f, nil
}

// Parse parses param.sfo bytes.
func Parse(b []byte) (*File, error) {
	if len(b) < headerSize || !bytes.Equal(b[:4], []byte("\x00PSF")) {
		return nil, errors.New("not a param.sfo (bad magic)")
	}
	le := binary.LittleEndian
	keyTab, dataTab, n := le.Uint32(b[8:]), le.Uint32(b[12:]), le.Uint32(b[16:])
	if uint64(headerSize)+uint64(n)*entrySize > uint64(len(b)) || keyTab > uint32(len(b)) || dataTab > uint32(len(b)) {
		return nil, errors.New("param.sfo truncated")
	}
	f := &File{vals: make(map[string]value, n)}
	for i := uint32(0); i < n; i++ {
		e := b[headerSize+i*entrySize:]
		keyOff := uint64(le.Uint16(e[0:])) + uint64(keyTab)
		format, length := le.Uint16(e[2:]), le.Uint32(e[4:])
		dataOff := uint64(le.Uint32(e[12:])) + uint64(dataTab)
		if keyOff >= uint64(len(b)) || dataOff+uint64(length) > uint64(len(b)) {
			return nil, fmt.Errorf("param.sfo entry %d out of range", i)
		}
		end := bytes.IndexByte(b[keyOff:], 0)
		if end < 0 {
			return nil, fmt.Errorf("param.sfo entry %d: unterminated key", i)
		}
		key := string(b[keyOff : keyOff+uint64(end)])
		data := b[dataOff : dataOff+uint64(length)]
		switch format {
		case fmtInt:
			if length < 4 {
				return nil, fmt.Errorf("param.sfo %s: short int", key)
			}
			f.vals[key] = value{num: le.Uint32(data), isInt: true}
		case fmtString, fmtSpecial:
			f.vals[key] = value{str: string(bytes.TrimRight(data, "\x00"))}
		default:
			return nil, fmt.Errorf("param.sfo %s: unknown format 0x%04x", key, format)
		}
	}
	return f, nil
}

// String returns a string value.
func (f *File) String(key string) (string, bool) {
	v, ok := f.vals[key]
	if !ok || v.isInt {
		return "", false
	}
	return v.str, true
}

// Int returns an int32 value.
func (f *File) Int(key string) (uint32, bool) {
	v, ok := f.vals[key]
	if !ok || !v.isInt {
		return 0, false
	}
	return v.num, true
}

// Keys returns the keys in sorted order.
func (f *File) Keys() []string {
	ks := make([]string, 0, len(f.vals))
	for k := range f.vals {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	return ks
}

// Build writes a param.sfo with the given entries (sorted by key, as the PS4 tools do).
func Build(entries []Entry) []byte {
	es := append([]Entry(nil), entries...)
	sort.Slice(es, func(i, j int) bool { return es[i].Key < es[j].Key })
	var keys, data bytes.Buffer
	idx := make([]byte, 0, len(es)*entrySize)
	le := binary.LittleEndian
	for _, e := range es {
		var rec [entrySize]byte
		le.PutUint16(rec[0:], uint16(keys.Len()))
		keys.WriteString(e.Key)
		keys.WriteByte(0)
		le.PutUint32(rec[12:], uint32(data.Len()))
		if e.IsInt {
			le.PutUint16(rec[2:], fmtInt)
			le.PutUint32(rec[4:], 4)
			le.PutUint32(rec[8:], 4)
			var v [4]byte
			le.PutUint32(v[:], e.Int)
			data.Write(v[:])
		} else {
			n := uint32(len(e.Str) + 1)
			maxLen := (n + 3) &^ 3
			le.PutUint16(rec[2:], fmtString)
			le.PutUint32(rec[4:], n)
			le.PutUint32(rec[8:], maxLen)
			data.WriteString(e.Str)
			data.Write(make([]byte, maxLen-n+1))
		}
		idx = append(idx, rec[:]...)
	}
	for keys.Len()%4 != 0 {
		keys.WriteByte(0)
	}
	out := make([]byte, headerSize, headerSize+len(idx)+keys.Len()+data.Len())
	copy(out, "\x00PSF")
	le.PutUint32(out[4:], 0x0101)
	le.PutUint32(out[8:], uint32(headerSize+len(idx)))
	le.PutUint32(out[12:], uint32(headerSize+len(idx)+keys.Len()))
	le.PutUint32(out[16:], uint32(len(es)))
	out = append(out, idx...)
	out = append(out, keys.Bytes()...)
	return append(out, data.Bytes()...)
}
