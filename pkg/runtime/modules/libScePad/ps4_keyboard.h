#ifndef PS4_KEYBOARD_H
#define PS4_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "recomp_runtime.h"

#define ORBIS_KEYBOARD_NUM_LOCK    1
#define ORBIS_KEYBOARD_CAPS_LOCK   2
#define ORBIS_KEYBOARD_SCROLL_LOCK 4

#define ORBIS_KEYBOARD_MOD_LEFT_CTRL   1
#define ORBIS_KEYBOARD_MOD_LEFT_SHIFT  2
#define ORBIS_KEYBOARD_MOD_LEFT_ALT    4
#define ORBIS_KEYBOARD_MOD_LEFT_META   8
#define ORBIS_KEYBOARD_MOD_RIGHT_CTRL  16
#define ORBIS_KEYBOARD_MOD_RIGHT_SHIFT 32
#define ORBIS_KEYBOARD_MOD_RIGHT_ALT   64
#define ORBIS_KEYBOARD_MOD_RIGHT_META  128

typedef struct OrbisKeyboardData {
    uint32_t timestamp;
    uint8_t padding[12];
    int32_t unk1;
    int32_t nkeys;
    uint32_t locks;
    uint32_t mods;
    uint16_t keycodes[32];
} OrbisKeyboardData;

typedef struct OrbisKeyboardKey2Char {
    int32_t ok;
    int32_t ok2;
    int32_t keycode;
    char unk[8];
} OrbisKeyboardKey2Char;

#ifdef __cplusplus
extern "C" {
#endif

int32_t sceKeyboardInit(void);
int32_t sceKeyboardOpen(int32_t userID, int32_t type, int32_t index, void* param);
int32_t sceKeyboardClose(int32_t handle);
int32_t sceKeyboardReadState(int32_t handle, OrbisKeyboardData* data);
int32_t sceKeyboardGetKey2Char(int32_t handle, bool unknown, int32_t locks, int32_t mods, int32_t keycode, OrbisKeyboardKey2Char* data);
int32_t sceKeyboardGetHandle(int32_t userID, int32_t type, int32_t index);

void ps4_keyboard_handle_key(unsigned short keyCode, int isDown, uintptr_t modifierFlags);
void ps4_keyboard_handle_flags(uintptr_t modifierFlags);
void ps4_keyboard_destroy(void);

// Shims
void shim_sceKeyboardInit(GuestContext *ctx);
void shim_sceKeyboardOpen(GuestContext *ctx);
void shim_sceKeyboardClose(GuestContext *ctx);
void shim_sceKeyboardReadState(GuestContext *ctx);
void shim_sceKeyboardGetKey2Char(GuestContext *ctx);
void shim_sceKeyboardGetHandle(GuestContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // PS4_KEYBOARD_H
