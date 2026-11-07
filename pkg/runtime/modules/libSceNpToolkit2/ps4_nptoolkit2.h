#ifndef PS4_NPTOOLKIT2_H
#define PS4_NPTOOLKIT2_H

#include <stdbool.h>
#include <stdint.h>
#include "../../core/recomp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ps4_nptoolkit2_dispatch(GuestContext *ctx, const char *nid);

#ifdef __cplusplus
}
#endif

#endif // PS4_NPTOOLKIT2_H
