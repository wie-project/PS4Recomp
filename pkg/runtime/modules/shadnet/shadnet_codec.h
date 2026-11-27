// SPDX-License-Identifier: GPL-2.0-or-later

// Generic protobuf (proto3) codec for the shadNet messages. cmd/shadnet-protogen generates a
// C struct and a descriptor table per message of shadPS4's shadnet.proto (shadnet_pb.h); this
// codec encodes and decodes any of them through its descriptor.
//
// Field representation:
//   bool / int32 / int64 / uint32 / uint64 / enum  the C scalar (enum as int32_t)
//   string / bytes                                  ShadPbBytes
//   message                                         pointer to its struct, NULL when absent
//   repeated T                                      T *name plus uint32_t name_count
//
// Encoding follows proto3: zero scalars and empty strings of singular fields are omitted,
// repeated numeric fields are packed, a present message field is always written. Decoding
// accepts packed and unpacked repeated numeric fields, skips unknown fields, and keeps the last
// occurrence of a singular field (shadNet does not send a field twice). Decoded strings, bytes,
// arrays and sub-messages live in a ShadPbArena; decoded strings are NUL-terminated.

#ifndef SHADNET_CODEC_H
#define SHADNET_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShadPbBytes {
    const uint8_t *data;
    uint32_t len;
} ShadPbBytes;

// Field kinds (pkg/shadnetproto Kind).
enum {
    SHADPB_BOOL = 0,
    SHADPB_INT32,
    SHADPB_INT64,
    SHADPB_UINT32,
    SHADPB_UINT64,
    SHADPB_STRING,
    SHADPB_BYTES,
    SHADPB_ENUM,
    SHADPB_MESSAGE,
};

typedef struct ShadPbMessageDesc ShadPbMessageDesc;

typedef struct ShadPbField {
    uint32_t number;
    uint8_t kind;
    bool repeated;
    uint16_t offset;       // of the value, or of the array pointer when repeated
    uint16_t count_offset; // of the uint32_t count when repeated
    uint16_t elem_size;    // size of one value / element
    const ShadPbMessageDesc *sub;
} ShadPbField;

struct ShadPbMessageDesc {
    const char *name;
    uint32_t size;
    uint32_t field_count;
    const ShadPbField *fields;
};

// Memory for decoded messages, released at once.
typedef struct ShadPbArenaBlock ShadPbArenaBlock;
typedef struct ShadPbArena {
    ShadPbArenaBlock *head;
} ShadPbArena;

// Zeroed, 8-byte aligned memory from the arena; NULL when out of memory.
void *shadpb_arena_alloc(ShadPbArena *arena, size_t size);
void shadpb_arena_free(ShadPbArena *arena);

// A ShadPbBytes for a C string (NULL gives an empty one).
ShadPbBytes shadpb_str(const char *s);
// A ShadPbBytes for a buffer.
ShadPbBytes shadpb_bytes(const void *data, size_t len);

// Size of msg's encoding.
size_t shadpb_encoded_size(const ShadPbMessageDesc *desc, const void *msg);
// Writes msg's encoding (shadpb_encoded_size bytes) to out; returns the size written.
size_t shadpb_encode(const ShadPbMessageDesc *desc, const void *msg, uint8_t *out);
// Decodes buf into msg (desc->size bytes, cleared first), allocating from arena. False for a
// malformed encoding or when out of memory; msg is then only partly filled.
bool shadpb_decode(const ShadPbMessageDesc *desc, const uint8_t *buf, size_t len, void *msg, ShadPbArena *arena);

#ifdef __cplusplus
}
#endif

#endif
