// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_handle_table.h"

#include <stdlib.h>

uint32_t ps4_handle_insert(Ps4HandleTable *t, Ps4HandleObject *obj) {
    obj->refs = 1;
    os_unfair_lock_lock(&t->lock);
    if (!t->slots) {
        t->slots = calloc(t->max, sizeof(*t->slots));
    }
    uint32_t id = 0;
    for (uint32_t n = 0; t->slots && n < t->max; n++) {
        const uint32_t i = (t->hint + n) % t->max;
        if (!t->slots[i]) {
            t->slots[i] = obj;
            t->hint = (i + 1) % t->max;
            id = i + 1;
            break;
        }
    }
    os_unfair_lock_unlock(&t->lock);
    return id;
}

Ps4HandleObject *ps4_handle_get(Ps4HandleTable *t, uint32_t id) {
    Ps4HandleObject *obj = NULL;
    os_unfair_lock_lock(&t->lock);
    if (t->slots && id >= 1 && id <= t->max) {
        obj = t->slots[id - 1];
        if (obj) {
            obj->refs++;
        }
    }
    os_unfair_lock_unlock(&t->lock);
    return obj;
}

Ps4HandleObject *ps4_handle_remove(Ps4HandleTable *t, uint32_t id) {
    Ps4HandleObject *obj = NULL;
    os_unfair_lock_lock(&t->lock);
    if (t->slots && id >= 1 && id <= t->max) {
        obj = t->slots[id - 1];
        t->slots[id - 1] = NULL;
    }
    os_unfair_lock_unlock(&t->lock);
    return obj;
}

void ps4_handle_put(Ps4HandleTable *t, Ps4HandleObject *obj) {
    os_unfair_lock_lock(&t->lock);
    const bool last = --obj->refs == 0;
    os_unfair_lock_unlock(&t->lock);
    if (last) {
        obj->destroy(obj);
    }
}
