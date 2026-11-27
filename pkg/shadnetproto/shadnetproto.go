// SPDX-License-Identifier: GPL-2.0-or-later

// Package shadnetproto turns shadPS4's shadNet protocol definition (src/shadnet/shadnet.proto)
// into C for the runtime: a struct per message and a descriptor table per message that the
// runtime's generic protobuf codec (modules/shadnet/shadnet_codec.c) encodes and decodes.
//
// It accepts the proto3 subset shadnet.proto uses: top-level enums and messages, singular and
// repeated fields of the scalar types bool, int32, int64, uint32, uint64, string and bytes,
// enum fields and message fields. Anything else is an error, so a protocol change that needs
// more is noticed when the files are regenerated.
package shadnetproto

import (
	"bytes"
	"fmt"
	"os"
	"strconv"
	"strings"
)

// Kind is the wire representation of a field.
type Kind int

// Field kinds, in the order the C codec numbers them (SHADPB_* in shadnet_codec.h).
const (
	KindBool Kind = iota
	KindInt32
	KindInt64
	KindUint32
	KindUint64
	KindString
	KindBytes
	KindEnum
	KindMessage
)

var scalarKinds = map[string]Kind{
	"bool":   KindBool,
	"int32":  KindInt32,
	"int64":  KindInt64,
	"uint32": KindUint32,
	"uint64": KindUint64,
	"string": KindString,
	"bytes":  KindBytes,
}

// Field is one field of a message.
type Field struct {
	Name     string
	Number   uint32
	Repeated bool
	Kind     Kind
	TypeName string // the enum or message name for KindEnum and KindMessage
}

// Message is a protobuf message.
type Message struct {
	Name   string
	Fields []Field
}

// EnumValue is one value of an enum.
type EnumValue struct {
	Name  string
	Value int32
}

// Enum is a protobuf enum.
type Enum struct {
	Name   string
	Values []EnumValue
}

// File is a parsed .proto file.
type File struct {
	Package  string
	Enums    []Enum
	Messages []Message
}

// tokenize splits proto source into tokens, dropping // and /* */ comments.
func tokenize(src []byte) ([]string, error) {
	var toks []string
	for i := 0; i < len(src); {
		c := src[i]
		switch {
		case c == ' ' || c == '\t' || c == '\n' || c == '\r':
			i++
		case c == '/' && i+1 < len(src) && src[i+1] == '/':
			for i < len(src) && src[i] != '\n' {
				i++
			}
		case c == '/' && i+1 < len(src) && src[i+1] == '*':
			end := bytes.Index(src[i+2:], []byte("*/"))
			if end < 0 {
				return nil, fmt.Errorf("unterminated comment")
			}
			i += end + 4
		case c == '"':
			end := bytes.IndexByte(src[i+1:], '"')
			if end < 0 {
				return nil, fmt.Errorf("unterminated string")
			}
			toks = append(toks, string(src[i:i+end+2]))
			i += end + 2
		case strings.IndexByte("{}=;[]", c) >= 0:
			toks = append(toks, string(c))
			i++
		default:
			j := i
			for j < len(src) && strings.IndexByte(" \t\r\n{}=;[]\"/", src[j]) < 0 {
				j++
			}
			if j == i {
				return nil, fmt.Errorf("unexpected character %q", c)
			}
			toks = append(toks, string(src[i:j]))
			i = j
		}
	}
	return toks, nil
}

type parser struct {
	toks []string
	pos  int
}

func (p *parser) next() (string, error) {
	if p.pos >= len(p.toks) {
		return "", fmt.Errorf("unexpected end of file")
	}
	t := p.toks[p.pos]
	p.pos++
	return t, nil
}

func (p *parser) expect(want string) error {
	t, err := p.next()
	if err != nil {
		return err
	}
	if t != want {
		return fmt.Errorf("expected %q, got %q", want, t)
	}
	return nil
}

// statement reads tokens up to and including ";".
func (p *parser) statement() ([]string, error) {
	var out []string
	for {
		t, err := p.next()
		if err != nil {
			return nil, err
		}
		if t == ";" {
			return out, nil
		}
		out = append(out, t)
	}
}

// Parse parses shadnet.proto.
func Parse(src []byte) (*File, error) {
	toks, err := tokenize(src)
	if err != nil {
		return nil, err
	}
	p := &parser{toks: toks}
	f := &File{}
	for p.pos < len(p.toks) {
		t, _ := p.next()
		switch t {
		case "syntax":
			st, err := p.statement()
			if err != nil {
				return nil, err
			}
			if len(st) != 2 || st[1] != `"proto3"` {
				return nil, fmt.Errorf("unsupported syntax %v", st)
			}
		case "package":
			st, err := p.statement()
			if err != nil {
				return nil, err
			}
			if len(st) != 1 {
				return nil, fmt.Errorf("bad package statement %v", st)
			}
			f.Package = st[0]
		case "option":
			if _, err := p.statement(); err != nil {
				return nil, err
			}
		case "enum":
			e, err := p.enum()
			if err != nil {
				return nil, err
			}
			f.Enums = append(f.Enums, e)
		case "message":
			m, err := p.message()
			if err != nil {
				return nil, err
			}
			f.Messages = append(f.Messages, m)
		default:
			return nil, fmt.Errorf("unsupported top-level token %q", t)
		}
	}
	return f, f.resolve()
}

func (p *parser) enum() (Enum, error) {
	name, err := p.next()
	if err != nil {
		return Enum{}, err
	}
	if err := p.expect("{"); err != nil {
		return Enum{}, err
	}
	e := Enum{Name: name}
	for {
		t, err := p.next()
		if err != nil {
			return Enum{}, err
		}
		if t == "}" {
			return e, nil
		}
		p.pos--
		st, err := p.statement()
		if err != nil {
			return Enum{}, err
		}
		if len(st) != 3 || st[1] != "=" {
			return Enum{}, fmt.Errorf("enum %s: bad value %v", name, st)
		}
		v, err := strconv.ParseInt(st[2], 0, 32)
		if err != nil {
			return Enum{}, fmt.Errorf("enum %s: %w", name, err)
		}
		e.Values = append(e.Values, EnumValue{Name: st[0], Value: int32(v)})
	}
}

func (p *parser) message() (Message, error) {
	name, err := p.next()
	if err != nil {
		return Message{}, err
	}
	if err := p.expect("{"); err != nil {
		return Message{}, err
	}
	m := Message{Name: name}
	seen := map[uint32]bool{}
	for {
		t, err := p.next()
		if err != nil {
			return Message{}, err
		}
		if t == "}" {
			return m, nil
		}
		p.pos--
		st, err := p.statement()
		if err != nil {
			return Message{}, err
		}
		fd := Field{}
		if len(st) > 0 && st[0] == "repeated" {
			fd.Repeated = true
			st = st[1:]
		}
		if len(st) != 4 || st[2] != "=" {
			return Message{}, fmt.Errorf("message %s: unsupported field %v", name, st)
		}
		n, err := strconv.ParseUint(st[3], 10, 29)
		if err != nil || n == 0 {
			return Message{}, fmt.Errorf("message %s: bad field number %q", name, st[3])
		}
		if seen[uint32(n)] {
			return Message{}, fmt.Errorf("message %s: duplicate field number %d", name, n)
		}
		seen[uint32(n)] = true
		fd.Name, fd.Number, fd.TypeName = st[1], uint32(n), st[0]
		if k, ok := scalarKinds[st[0]]; ok {
			fd.Kind, fd.TypeName = k, ""
		}
		m.Fields = append(m.Fields, fd)
	}
}

// resolve classifies the named field types as enums or messages.
func (f *File) resolve() error {
	enums := map[string]bool{}
	for _, e := range f.Enums {
		enums[e.Name] = true
	}
	msgs := map[string]bool{}
	for _, m := range f.Messages {
		msgs[m.Name] = true
	}
	for mi := range f.Messages {
		m := &f.Messages[mi]
		for fi := range m.Fields {
			fd := &m.Fields[fi]
			if fd.TypeName == "" {
				continue
			}
			switch {
			case enums[fd.TypeName]:
				fd.Kind = KindEnum
			case msgs[fd.TypeName]:
				fd.Kind = KindMessage
			default:
				return fmt.Errorf("message %s: unknown type %q", m.Name, fd.TypeName)
			}
		}
	}
	return nil
}

var kindC = map[Kind]string{
	KindBool:   "bool",
	KindInt32:  "int32_t",
	KindInt64:  "int64_t",
	KindUint32: "uint32_t",
	KindUint64: "uint64_t",
	KindString: "ShadPbBytes",
	KindBytes:  "ShadPbBytes",
	KindEnum:   "int32_t",
}

var kindConst = map[Kind]string{
	KindBool:    "SHADPB_BOOL",
	KindInt32:   "SHADPB_INT32",
	KindInt64:   "SHADPB_INT64",
	KindUint32:  "SHADPB_UINT32",
	KindUint64:  "SHADPB_UINT64",
	KindString:  "SHADPB_STRING",
	KindBytes:   "SHADPB_BYTES",
	KindEnum:    "SHADPB_ENUM",
	KindMessage: "SHADPB_MESSAGE",
}

// cType is the C type of a field's element.
func cType(fd Field) string {
	if fd.Kind == KindMessage {
		return "ShadPb_" + fd.TypeName
	}
	return kindC[fd.Kind]
}

// emitter accumulates generated text; writes to a bytes.Buffer cannot fail.
type emitter struct {
	bytes.Buffer
}

func (e *emitter) printf(format string, args ...any) {
	_, _ = fmt.Fprintf(&e.Buffer, format, args...) // a bytes.Buffer write cannot fail
}

// GenerateC returns the generated header and source. headerName is the header's file name,
// which the source includes; source names the .proto in the banner.
func GenerateC(f *File, headerName, source string) (header, csrc []byte) {
	guard := "SHADNET_PB_H"
	var h emitter
	w := &h
	w.printf("// SPDX-License-Identifier: GPL-2.0-or-later\n\n")
	w.printf("// Code generated by cmd/shadnet-protogen from %s. DO NOT EDIT.\n\n", source)
	w.printf("#ifndef %s\n#define %s\n\n#include \"shadnet_codec.h\"\n\n", guard, guard)
	for _, e := range f.Enums {
		for _, v := range e.Values {
			w.printf("#define SHADPB_%s %d\n", v.Name, v.Value)
		}
		w.printf("\n")
	}
	for _, m := range f.Messages {
		w.printf("typedef struct ShadPb_%s ShadPb_%s;\n", m.Name, m.Name)
	}
	w.printf("\n")
	// Structs: a message field embeds a pointer, so declaration order does not matter.
	for _, m := range f.Messages {
		w.printf("struct ShadPb_%s {\n", m.Name)
		if len(m.Fields) == 0 {
			w.printf("    uint8_t unused_;\n")
		}
		for _, fd := range m.Fields {
			switch {
			case fd.Repeated:
				w.printf("    %s *%s;\n    uint32_t %s_count;\n", cType(fd), fd.Name, fd.Name)
			case fd.Kind == KindMessage:
				w.printf("    %s *%s; // NULL when absent\n", cType(fd), fd.Name)
			default:
				w.printf("    %s %s;\n", cType(fd), fd.Name)
			}
		}
		w.printf("};\nextern const ShadPbMessageDesc shadpb_%s_desc;\n\n", m.Name)
	}
	w.printf("#endif\n")

	var c emitter
	w = &c
	w.printf("// SPDX-License-Identifier: GPL-2.0-or-later\n\n")
	w.printf("// Code generated by cmd/shadnet-protogen from %s. DO NOT EDIT.\n\n", source)
	w.printf("#include \"%s\"\n#include <stddef.h>\n\n", headerName)
	for _, m := range f.Messages {
		if len(m.Fields) == 0 {
			w.printf("const ShadPbMessageDesc shadpb_%s_desc = {\"%s\", sizeof(ShadPb_%s), 0, NULL};\n\n",
				m.Name, m.Name, m.Name)
			continue
		}
		w.printf("static const ShadPbField shadpb_%s_fields[] = {\n", m.Name)
		for _, fd := range m.Fields {
			sub, count := "NULL", "0"
			if fd.Kind == KindMessage {
				sub = "&shadpb_" + fd.TypeName + "_desc"
			}
			if fd.Repeated {
				count = fmt.Sprintf("offsetof(ShadPb_%s, %s_count)", m.Name, fd.Name)
			}
			w.printf("    {%d, %s, %t, offsetof(ShadPb_%s, %s), %s, sizeof(%s), %s},\n",
				fd.Number, kindConst[fd.Kind], fd.Repeated, m.Name, fd.Name, count, cType(fd), sub)
		}
		w.printf("};\nconst ShadPbMessageDesc shadpb_%s_desc = {\"%s\", sizeof(ShadPb_%s), %d, shadpb_%s_fields};\n\n",
			m.Name, m.Name, m.Name, len(m.Fields), m.Name)
	}
	// The tables refer to other messages' descriptors through the header's declarations.
	return h.Bytes(), c.Bytes()
}

// Paths relative to the repository root, and the generated files' names.
const (
	DefaultProto  = "tools/shadps4/src/shadnet/shadnet.proto"
	DefaultOutDir = "pkg/runtime/modules/shadnet"
	HeaderName    = "shadnet_pb.h"
	SourceName    = "shadnet_pb.c"
	sourceBanner  = "shadPS4 src/shadnet/shadnet.proto"
)

// GenerateFile reads a .proto file and returns the generated header and source.
func GenerateFile(protoPath string) (header, source []byte, err error) {
	src, err := os.ReadFile(protoPath)
	if err != nil {
		return nil, nil, err
	}
	f, err := Parse(src)
	if err != nil {
		return nil, nil, fmt.Errorf("%s: %w", protoPath, err)
	}
	header, source = GenerateC(f, HeaderName, sourceBanner)
	return header, source, nil
}
