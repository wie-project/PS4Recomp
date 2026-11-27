// SPDX-License-Identifier: GPL-2.0-or-later

// Minimal JSON reader for the runtime's small configuration files (libSceHttp host overrides,
// the shadNet account): the members of a top-level object.

#ifndef PS4_JSON_OBJECT_H
#define PS4_JSON_OBJECT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum Ps4JsonKind {
    PS4_JSON_STRING,  // value is the decoded string (UTF-8)
    PS4_JSON_LITERAL, // value is the text of a number, true, false or null
} Ps4JsonKind;

typedef void (*Ps4JsonMemberFn)(void *user, const char *key, Ps4JsonKind kind, const char *value);

// Calls member for each string, number, true, false or null member of the top-level object
// in text, in order; object and array members are skipped. Returns the number of members
// reported, or -1 - that number when the text is not a well-formed object (the members before
// the error were reported).
int ps4_json_object_members(const char *text, size_t len, Ps4JsonMemberFn member, void *user);

#endif
