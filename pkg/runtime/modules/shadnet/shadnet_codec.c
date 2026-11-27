// SPDX-License-Identifier: GPL-2.0-or-later

// Generic proto3 codec for the generated shadNet descriptors; see shadnet_codec.h.

#include "shadnet_codec.h"
#include <stdlib.h>
#include <string.h>

// Protobuf wire types.
enum { WT_VARINT = 0, WT_I64 = 1, WT_LEN = 2, WT_I32 = 5 };

// Sub-messages nested deeper than this are rejected.
#define SHADPB_MAX_DEPTH 32

//=============================================================================
// Arena
//=============================================================================

struct ShadPbArenaBlock {
    ShadPbArenaBlock *next;
    size_t used;
    size_t cap;
    _Alignas(8) uint8_t data[];
};

#define SHADPB_ARENA_BLOCK 4096

void *shadpb_arena_alloc(ShadPbArena *arena, size_t size) {
    size = (size + 7) & ~(size_t)7;
    if (size == 0) size = 8;
    ShadPbArenaBlock *b = arena->head;
    if (!b || b->cap - b->used < size) {
        const size_t cap = size > SHADPB_ARENA_BLOCK ? size : SHADPB_ARENA_BLOCK;
        b = calloc(1, sizeof(*b) + cap);
        if (!b) return NULL;
        b->cap = cap;
        b->next = arena->head;
        arena->head = b;
    }
    void *p = b->data + b->used;
    b->used += size;
    return p;
}

void shadpb_arena_free(ShadPbArena *arena) {
    ShadPbArenaBlock *b = arena->head;
    while (b) {
        ShadPbArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    arena->head = NULL;
}

ShadPbBytes shadpb_str(const char *s) {
    ShadPbBytes b = {(const uint8_t *)s, s ? (uint32_t)strlen(s) : 0};
    return b;
}

ShadPbBytes shadpb_bytes(const void *data, size_t len) {
    ShadPbBytes b = {(const uint8_t *)data, (uint32_t)len};
    return b;
}

//=============================================================================
// Encoding
//=============================================================================

static size_t varint_size(uint64_t v) {
    size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        n++;
    }
    return n;
}

static uint8_t *put_varint(uint8_t *p, uint64_t v) {
    while (v >= 0x80) {
        *p++ = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    *p++ = (uint8_t)v;
    return p;
}

// The varint value of a numeric element (int32 and enum are sign-extended, as protobuf does).
static uint64_t numeric_value(uint8_t kind, const void *p) {
    switch (kind) {
    case SHADPB_BOOL: return *(const bool *)p ? 1 : 0;
    case SHADPB_INT32:
    case SHADPB_ENUM: return (uint64_t)(int64_t) * (const int32_t *)p;
    case SHADPB_INT64: return (uint64_t) * (const int64_t *)p;
    case SHADPB_UINT32: return *(const uint32_t *)p;
    default: return *(const uint64_t *)p; // SHADPB_UINT64
    }
}

static bool is_length_kind(uint8_t kind) {
    return kind == SHADPB_STRING || kind == SHADPB_BYTES || kind == SHADPB_MESSAGE;
}

static uint64_t tag(uint32_t number, unsigned wire_type) {
    return ((uint64_t)number << 3) | wire_type;
}

// Size of one element's payload (without tag): the varint, or the length plus the bytes.
static size_t element_size(const ShadPbField *f, const void *p) {
    if (f->kind == SHADPB_MESSAGE) {
        const size_t n = shadpb_encoded_size(f->sub, p);
        return varint_size(n) + n;
    }
    if (f->kind == SHADPB_STRING || f->kind == SHADPB_BYTES) {
        const ShadPbBytes *b = p;
        return varint_size(b->len) + b->len;
    }
    return varint_size(numeric_value(f->kind, p));
}

// Lengths of a repeated field: the packed payload size for numeric fields.
static size_t packed_payload_size(const ShadPbField *f, const uint8_t *items, uint32_t count) {
    size_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        n += varint_size(numeric_value(f->kind, items + (size_t)i * f->elem_size));
    }
    return n;
}

static bool singular_is_default(const ShadPbField *f, const void *p) {
    if (f->kind == SHADPB_MESSAGE) return *(void *const *)p == NULL;
    if (f->kind == SHADPB_STRING || f->kind == SHADPB_BYTES) return ((const ShadPbBytes *)p)->len == 0;
    return numeric_value(f->kind, p) == 0;
}

size_t shadpb_encoded_size(const ShadPbMessageDesc *desc, const void *msg) {
    const uint8_t *base = msg;
    size_t n = 0;
    for (uint32_t i = 0; i < desc->field_count; i++) {
        const ShadPbField *f = &desc->fields[i];
        const void *v = base + f->offset;
        if (f->repeated) {
            const uint32_t count = *(const uint32_t *)(base + f->count_offset);
            const uint8_t *items = *(const uint8_t *const *)v;
            if (count == 0 || !items) continue;
            if (is_length_kind(f->kind)) {
                for (uint32_t k = 0; k < count; k++) {
                    n += varint_size(tag(f->number, WT_LEN)) + element_size(f, items + (size_t)k * f->elem_size);
                }
            } else {
                const size_t payload = packed_payload_size(f, items, count);
                n += varint_size(tag(f->number, WT_LEN)) + varint_size(payload) + payload;
            }
            continue;
        }
        if (singular_is_default(f, v)) continue;
        const void *elem = f->kind == SHADPB_MESSAGE ? *(void *const *)v : v;
        n += varint_size(tag(f->number, is_length_kind(f->kind) ? WT_LEN : WT_VARINT)) + element_size(f, elem);
    }
    return n;
}

static uint8_t *put_element(const ShadPbField *f, const void *p, uint8_t *out) {
    if (f->kind == SHADPB_MESSAGE) {
        const size_t n = shadpb_encoded_size(f->sub, p);
        out = put_varint(out, n);
        return out + shadpb_encode(f->sub, p, out);
    }
    if (f->kind == SHADPB_STRING || f->kind == SHADPB_BYTES) {
        const ShadPbBytes *b = p;
        out = put_varint(out, b->len);
        if (b->len) memcpy(out, b->data, b->len);
        return out + b->len;
    }
    return put_varint(out, numeric_value(f->kind, p));
}

size_t shadpb_encode(const ShadPbMessageDesc *desc, const void *msg, uint8_t *out) {
    const uint8_t *base = msg;
    uint8_t *p = out;
    for (uint32_t i = 0; i < desc->field_count; i++) {
        const ShadPbField *f = &desc->fields[i];
        const void *v = base + f->offset;
        if (f->repeated) {
            const uint32_t count = *(const uint32_t *)(base + f->count_offset);
            const uint8_t *items = *(const uint8_t *const *)v;
            if (count == 0 || !items) continue;
            if (is_length_kind(f->kind)) {
                for (uint32_t k = 0; k < count; k++) {
                    p = put_varint(p, tag(f->number, WT_LEN));
                    p = put_element(f, items + (size_t)k * f->elem_size, p);
                }
            } else {
                p = put_varint(p, tag(f->number, WT_LEN));
                p = put_varint(p, packed_payload_size(f, items, count));
                for (uint32_t k = 0; k < count; k++) {
                    p = put_varint(p, numeric_value(f->kind, items + (size_t)k * f->elem_size));
                }
            }
            continue;
        }
        if (singular_is_default(f, v)) continue;
        p = put_varint(p, tag(f->number, is_length_kind(f->kind) ? WT_LEN : WT_VARINT));
        p = put_element(f, f->kind == SHADPB_MESSAGE ? *(void *const *)v : v, p);
    }
    return (size_t)(p - out);
}

//=============================================================================
// Decoding
//=============================================================================

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
} Reader;

static bool get_varint(Reader *r, uint64_t *out) {
    uint64_t v = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        if (r->p >= r->end) return false;
        const uint8_t b = *r->p++;
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *out = v;
            return true;
        }
    }
    return false;
}

// Reads a length-delimited payload.
static bool get_len(Reader *r, Reader *payload) {
    uint64_t n;
    if (!get_varint(r, &n) || n > (uint64_t)(r->end - r->p)) return false;
    payload->p = r->p;
    payload->end = r->p + n;
    r->p += n;
    return true;
}

static bool skip_value(Reader *r, unsigned wire_type) {
    uint64_t v;
    Reader payload;
    switch (wire_type) {
    case WT_VARINT: return get_varint(r, &v);
    case WT_I64:
        if (r->end - r->p < 8) return false;
        r->p += 8;
        return true;
    case WT_I32:
        if (r->end - r->p < 4) return false;
        r->p += 4;
        return true;
    case WT_LEN: return get_len(r, &payload);
    default: return false; // groups are not proto3
    }
}

static const ShadPbField *find_field(const ShadPbMessageDesc *desc, uint32_t number) {
    for (uint32_t i = 0; i < desc->field_count; i++) {
        if (desc->fields[i].number == number) return &desc->fields[i];
    }
    return NULL;
}

static void store_numeric(uint8_t kind, void *p, uint64_t v) {
    switch (kind) {
    case SHADPB_BOOL: *(bool *)p = v != 0; break;
    case SHADPB_INT32:
    case SHADPB_ENUM: *(int32_t *)p = (int32_t)(uint32_t)v; break;
    case SHADPB_INT64: *(int64_t *)p = (int64_t)v; break;
    case SHADPB_UINT32: *(uint32_t *)p = (uint32_t)v; break;
    default: *(uint64_t *)p = v; break;
    }
}

static bool decode_depth(const ShadPbMessageDesc *desc, const uint8_t *buf, size_t len, void *msg,
                         ShadPbArena *arena, int depth);

// Decodes one length-delimited element (string, bytes or message) into p.
static bool decode_len_element(const ShadPbField *f, const Reader *payload, void *p, ShadPbArena *arena, int depth) {
    const size_t n = (size_t)(payload->end - payload->p);
    if (f->kind == SHADPB_MESSAGE) {
        return decode_depth(f->sub, payload->p, n, p, arena, depth + 1);
    }
    uint8_t *copy = shadpb_arena_alloc(arena, n + 1);
    if (!copy) return false;
    if (n) memcpy(copy, payload->p, n);
    ShadPbBytes *b = p;
    b->data = copy;
    b->len = (uint32_t)n;
    return true;
}

static bool decode_depth(const ShadPbMessageDesc *desc, const uint8_t *buf, size_t len, void *msg,
                         ShadPbArena *arena, int depth) {
    if (depth > SHADPB_MAX_DEPTH) return false;
    uint8_t *base = msg;
    memset(base, 0, desc->size);

    // Pass 1: count the elements of each repeated field (stored in its count).
    Reader r = {buf, buf + len};
    while (r.p < r.end) {
        uint64_t key;
        if (!get_varint(&r, &key) || (key >> 3) == 0 || (key >> 3) > UINT32_MAX) return false;
        const unsigned wt = (unsigned)(key & 7);
        const ShadPbField *f = find_field(desc, (uint32_t)(key >> 3));
        const uint8_t *value_start = r.p;
        if (!skip_value(&r, wt)) return false;
        if (!f || !f->repeated) continue;
        uint32_t *count = (uint32_t *)(base + f->count_offset);
        if (is_length_kind(f->kind)) {
            if (wt != WT_LEN) return false;
            (*count)++;
        } else if (wt == WT_VARINT) {
            (*count)++;
        } else if (wt == WT_LEN) {
            Reader v = {value_start, r.p};
            Reader packed;
            if (!get_len(&v, &packed)) return false;
            uint64_t x;
            while (packed.p < packed.end) {
                if (!get_varint(&packed, &x)) return false;
                (*count)++;
            }
        } else {
            return false;
        }
    }
    for (uint32_t i = 0; i < desc->field_count; i++) {
        const ShadPbField *f = &desc->fields[i];
        if (!f->repeated) continue;
        uint32_t *count = (uint32_t *)(base + f->count_offset);
        if (*count == 0) continue;
        void *items = shadpb_arena_alloc(arena, (size_t)*count * f->elem_size);
        if (!items) return false;
        *(void **)(base + f->offset) = items;
        *count = 0; // refilled by pass 2
    }

    // Pass 2: store the values.
    r.p = buf;
    while (r.p < r.end) {
        uint64_t key;
        if (!get_varint(&r, &key)) return false; // validated by pass 1; checked again for safety
        const unsigned wt = (unsigned)(key & 7);
        const ShadPbField *f = find_field(desc, (uint32_t)(key >> 3));
        if (!f) {
            if (!skip_value(&r, wt)) return false;
            continue;
        }
        uint8_t *v = base + f->offset;
        if (f->repeated) {
            uint32_t *count = (uint32_t *)(base + f->count_offset);
            uint8_t *items = *(uint8_t **)v;
            if (is_length_kind(f->kind)) {
                Reader payload;
                if (!get_len(&r, &payload)) return false;
                if (!decode_len_element(f, &payload, items + (size_t)*count * f->elem_size, arena, depth)) return false;
                (*count)++;
            } else if (wt == WT_VARINT) {
                uint64_t x;
                if (!get_varint(&r, &x)) return false;
                store_numeric(f->kind, items + (size_t)(*count)++ * f->elem_size, x);
            } else {
                Reader packed;
                if (!get_len(&r, &packed)) return false;
                uint64_t x;
                while (packed.p < packed.end) {
                    if (!get_varint(&packed, &x)) return false;
                    store_numeric(f->kind, items + (size_t)(*count)++ * f->elem_size, x);
                }
            }
            continue;
        }
        if (is_length_kind(f->kind)) {
            if (wt != WT_LEN) return false;
            Reader payload;
            if (!get_len(&r, &payload)) return false;
            void *dst = v;
            if (f->kind == SHADPB_MESSAGE) {
                dst = shadpb_arena_alloc(arena, f->sub->size);
                if (!dst) return false;
                *(void **)v = dst;
            }
            if (!decode_len_element(f, &payload, dst, arena, depth)) return false;
        } else {
            if (wt != WT_VARINT) return false;
            uint64_t x;
            if (!get_varint(&r, &x)) return false;
            store_numeric(f->kind, v, x);
        }
    }
    return true;
}

bool shadpb_decode(const ShadPbMessageDesc *desc, const uint8_t *buf, size_t len, void *msg, ShadPbArena *arena) {
    return decode_depth(desc, buf, len, msg, arena, 0);
}
