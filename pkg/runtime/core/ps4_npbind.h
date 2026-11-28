// SPDX-License-Identifier: GPL-2.0-or-later

// npbind.dat, the title's NP binding file (shadPS4 src/core/file_format/npbind.cpp): the NP
// communication ids ("NPWRxxxxx_00") of its services, in file order.

#ifndef PS4_NPBIND_H
#define PS4_NPBIND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_NPBIND_MAGIC 0xD294A018u
// The length of an NP communication id on the wire (shadPS4 GetNpCommId's COM_ID_LEN).
#define PS4_NPBIND_COMM_ID_LEN 12

// Parses an npbind.dat image (NPBindFile::Load, then GetNpCommIds): copies the first cap NP
// communication ids into ids, each cut or NUL-padded to PS4_NPBIND_COMM_ID_LEN bytes, and
// returns how many the file has; -1 when data is not a complete npbind file.
int ps4_npbind_comm_ids(const uint8_t *data, size_t size, char (*ids)[PS4_NPBIND_COMM_ID_LEN], int cap);

#ifdef __cplusplus
}
#endif

#endif
