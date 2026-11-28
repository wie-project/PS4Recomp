// SPDX-License-Identifier: GPL-2.0-or-later

// npbind.dat parsing, following shadPS4 src/core/file_format/npbind.cpp. The file is a 0x80-byte
// big-endian header (magic, version, file size, entry size, entry count, padding) and then one
// body per entry: four type-length-value records (NP communication id, trophy, two unknown),
// each a big-endian u16 type and u16 size followed by size bytes, then 0x98 bytes of padding
// that the last body may omit.

#include "ps4_npbind.h"
#include <string.h>

#define NPBIND_HEADER_SIZE 0x80
#define NPBIND_BODY_PADDING 0x98

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p) {
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

int ps4_npbind_comm_ids(const uint8_t *data, size_t size, char (*ids)[PS4_NPBIND_COMM_ID_LEN], int cap) {
    if (size < NPBIND_HEADER_SIZE || be32(data) != PS4_NPBIND_MAGIC) return -1;
    const uint64_t num_entries = be64(data + 24);
    size_t offset = NPBIND_HEADER_SIZE;
    int count = 0;
    for (uint64_t bi = 0; bi < num_entries; bi++) {
        if (offset + 4 * 4 > size) return -1;
        for (int e = 0; e < 4; e++) {
            if (offset + 4 > size) return -1;
            const size_t len = (size_t)data[offset + 2] << 8 | data[offset + 3];
            offset += 4;
            if (offset + len > size) return -1;
            // The first record is the NP communication id; an empty one is not listed.
            if (e == 0 && len > 0) {
                if (count < cap) {
                    memset(ids[count], 0, PS4_NPBIND_COMM_ID_LEN);
                    memcpy(ids[count], data + offset, len < PS4_NPBIND_COMM_ID_LEN ? len : PS4_NPBIND_COMM_ID_LEN);
                }
                count++;
            }
            offset += len;
        }
        offset = offset + NPBIND_BODY_PADDING <= size ? offset + NPBIND_BODY_PADDING : size;
    }
    return count;
}
