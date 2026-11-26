// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS4_DISC_MAP_H
#define PS4_DISC_MAP_H

#include <stdint.h>
#include "recomp_runtime.h"

#define ORBIS_DISC_MAP_ERROR_INVALID_ARGUMENT ((int32_t)0x81100001)
#define ORBIS_DISC_MAP_ERROR_LOCATION_NOT_MAPPED ((int32_t)0x81100002)
#define ORBIS_DISC_MAP_ERROR_FILE_NOT_FOUND ((int32_t)0x81100003)
#define ORBIS_DISC_MAP_ERROR_NO_BITMAP_INFO ((int32_t)0x81100004)
#define ORBIS_DISC_MAP_ERROR_FATAL ((int32_t)0x811000FF)

void shim_sceDiscMapGetPackageSize(GuestContext *ctx);
void shim_sceDiscMapIsRequestOnHDD(GuestContext *ctx);
void shim_Func_7C980FFB0AA27E7A(GuestContext *ctx);
void shim_Func_8A828CAEE7EDD5E9(GuestContext *ctx);
void shim_Func_E7EBCE96E92F91F8(GuestContext *ctx);

#endif // PS4_DISC_MAP_H
