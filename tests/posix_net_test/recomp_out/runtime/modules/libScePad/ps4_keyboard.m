#import <Foundation/Foundation.h>
#include "ps4_keyboard.h"
#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>

static int g_keyboard_inited = 0;
static int g_keyboard_opened = 0;
static pthread_mutex_t g_keyboard_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint16_t g_active_keys[32];
static int g_num_active_keys = 0;
static uint32_t g_locks = 0;
static uint32_t g_mods = 0;

// Mapping macOS virtual key codes (Carbon / HIToolbox) to USB HID key codes
static uint16_t mac_keycode_to_hid(unsigned short macCode) {
    switch (macCode) {
        // Letters A-Z
        case 0x00: return 0x04; // A
        case 0x0B: return 0x05; // B
        case 0x08: return 0x06; // C
        case 0x02: return 0x07; // D
        case 0x0E: return 0x08; // E
        case 0x03: return 0x09; // F
        case 0x05: return 0x0A; // G
        case 0x04: return 0x0B; // H
        case 0x22: return 0x0C; // I
        case 0x26: return 0x0D; // J
        case 0x28: return 0x0E; // K
        case 0x25: return 0x0F; // L
        case 0x2E: return 0x10; // M
        case 0x2D: return 0x11; // N
        case 0x1F: return 0x12; // O
        case 0x23: return 0x13; // P
        case 0x0C: return 0x14; // Q
        case 0x0F: return 0x15; // R
        case 0x01: return 0x16; // S
        case 0x11: return 0x17; // T
        case 0x20: return 0x18; // U
        case 0x09: return 0x19; // V
        case 0x0D: return 0x1A; // W
        case 0x07: return 0x1B; // X
        case 0x10: return 0x1C; // Y
        case 0x06: return 0x1D; // Z

        // Numbers 1-0
        case 0x12: return 0x1E; // 1
        case 0x13: return 0x1F; // 2
        case 0x14: return 0x20; // 3
        case 0x15: return 0x21; // 4
        case 0x17: return 0x22; // 5
        case 0x16: return 0x23; // 6
        case 0x1A: return 0x24; // 7
        case 0x1C: return 0x25; // 8
        case 0x19: return 0x26; // 9
        case 0x1D: return 0x27; // 0

        // Whitespace and Control
        case 0x24: return 0x28; // Return / Enter
        case 0x35: return 0x29; // Escape
        case 0x33: return 0x2A; // Backspace (Delete)
        case 0x30: return 0x2B; // Tab
        case 0x31: return 0x2C; // Space

        // Symbols
        case 0x1B: return 0x2D; // -
        case 0x18: return 0x2E; // =
        case 0x21: return 0x2F; // [
        case 0x1E: return 0x30; // ]
        case 0x2A: return 0x31; // \
        case 0x29: return 0x33; // ;
        case 0x27: return 0x34; // '
        case 0x32: return 0x35; // `
        case 0x2B: return 0x36; // ,
        case 0x2F: return 0x37; // .
        case 0x2C: return 0x38; // /

        // Function keys F1-F12
        case 0x7A: return 0x3A; // F1
        case 0x78: return 0x3B; // F2
        case 0x63: return 0x3C; // F3
        case 0x76: return 0x3D; // F4
        case 0x60: return 0x3E; // F5
        case 0x61: return 0x3F; // F6
        case 0x62: return 0x40; // F7
        case 0x64: return 0x41; // F8
        case 0x65: return 0x42; // F9
        case 0x6D: return 0x43; // F10
        case 0x67: return 0x44; // F11
        case 0x6F: return 0x45; // F12

        // Navigation
        case 0x72: return 0x49; // Insert (Help)
        case 0x73: return 0x4A; // Home
        case 0x74: return 0x4B; // PageUp
        case 0x75: return 0x4C; // Forward Delete
        case 0x77: return 0x4D; // End
        case 0x79: return 0x4E; // PageDown
        case 0x7C: return 0x4F; // Right Arrow
        case 0x7B: return 0x50; // Left Arrow
        case 0x7D: return 0x51; // Down Arrow
        case 0x7E: return 0x52; // Up Arrow

        // Keypad
        case 0x47: return 0x53; // NumLock / Clear
        case 0x4B: return 0x54; // Keypad /
        case 0x43: return 0x55; // Keypad *
        case 0x4E: return 0x56; // Keypad -
        case 0x45: return 0x57; // Keypad +
        case 0x4C: return 0x58; // Keypad Enter
        case 0x53: return 0x59; // Keypad 1
        case 0x54: return 0x5A; // Keypad 2
        case 0x55: return 0x5B; // Keypad 3
        case 0x56: return 0x5C; // Keypad 4
        case 0x57: return 0x5D; // Keypad 5
        case 0x58: return 0x5E; // Keypad 6
        case 0x59: return 0x5F; // Keypad 7
        case 0x5B: return 0x60; // Keypad 8
        case 0x5C: return 0x61; // Keypad 9
        case 0x52: return 0x62; // Keypad 0
        case 0x41: return 0x63; // Keypad .

        // Modifiers as keycodes
        case 0x39: return 0x39; // CapsLock
        case 0x38: return 0xE1; // Left Shift
        case 0x3C: return 0xE5; // Right Shift
        case 0x3B: return 0xE0; // Left Control
        case 0x3E: return 0xE4; // Right Control
        case 0x3A: return 0xE2; // Left Alt / Option
        case 0x3D: return 0xE6; // Right Alt / Option
        case 0x37: return 0xE3; // Left Command / Meta
        case 0x36: return 0xE7; // Right Command / Meta

        default: return 0;
    }
}

void ps4_keyboard_handle_key(unsigned short macCode, int isDown, uintptr_t modifierFlags) {
    pthread_mutex_lock(&g_keyboard_mutex);

    // Update modifier states based on keycode
    switch (macCode) {
        case 0x38: // Left Shift
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_LEFT_SHIFT;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_LEFT_SHIFT;
            break;
        case 0x3C: // Right Shift
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_RIGHT_SHIFT;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_RIGHT_SHIFT;
            break;
        case 0x3B: // Left Ctrl
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_LEFT_CTRL;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_LEFT_CTRL;
            break;
        case 0x3E: // Right Ctrl
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_RIGHT_CTRL;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_RIGHT_CTRL;
            break;
        case 0x3A: // Left Alt / Option
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_LEFT_ALT;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_LEFT_ALT;
            break;
        case 0x3D: // Right Alt / Option
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_RIGHT_ALT;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_RIGHT_ALT;
            break;
        case 0x37: // Left Command / Meta
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_LEFT_META;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_LEFT_META;
            break;
        case 0x36: // Right Command / Meta
            if (isDown) g_mods |= ORBIS_KEYBOARD_MOD_RIGHT_META;
            else g_mods &= ~ORBIS_KEYBOARD_MOD_RIGHT_META;
            break;
        default:
            break;
    }

    // Update locks
    if (modifierFlags & (1 << 16)) { // NSEventModifierFlagCapsLock
        g_locks |= ORBIS_KEYBOARD_CAPS_LOCK;
    } else {
        g_locks &= ~ORBIS_KEYBOARD_CAPS_LOCK;
    }

    uint16_t hid = mac_keycode_to_hid(macCode);
    if (hid != 0) {
        if (isDown) {
            // Check if already in active keys
            int found = 0;
            for (int i = 0; i < g_num_active_keys; i++) {
                if (g_active_keys[i] == hid) {
                    found = 1;
                    break;
                }
            }
            if (!found && g_num_active_keys < 32) {
                g_active_keys[g_num_active_keys++] = hid;
            }
        } else {
            // Remove from active keys
            for (int i = 0; i < g_num_active_keys; i++) {
                if (g_active_keys[i] == hid) {
                    for (int j = i; j < g_num_active_keys - 1; j++) {
                        g_active_keys[j] = g_active_keys[j + 1];
                    }
                    g_num_active_keys--;
                    g_active_keys[g_num_active_keys] = 0;
                    break;
                }
            }
        }
    }

    pthread_mutex_unlock(&g_keyboard_mutex);
}

void ps4_keyboard_handle_flags(uintptr_t modifierFlags) {
    pthread_mutex_lock(&g_keyboard_mutex);

    // Caps Lock
    if (modifierFlags & (1 << 16)) { // NSEventModifierFlagCapsLock
        g_locks |= ORBIS_KEYBOARD_CAPS_LOCK;
    } else {
        g_locks &= ~ORBIS_KEYBOARD_CAPS_LOCK;
    }

    // Shift
    if (modifierFlags & (1 << 17)) { // NSEventModifierFlagShift
        if (!(g_mods & (ORBIS_KEYBOARD_MOD_LEFT_SHIFT | ORBIS_KEYBOARD_MOD_RIGHT_SHIFT))) {
            g_mods |= ORBIS_KEYBOARD_MOD_LEFT_SHIFT;
        }
    } else {
        g_mods &= ~(ORBIS_KEYBOARD_MOD_LEFT_SHIFT | ORBIS_KEYBOARD_MOD_RIGHT_SHIFT);
    }

    // Control
    if (modifierFlags & (1 << 18)) { // NSEventModifierFlagControl
        if (!(g_mods & (ORBIS_KEYBOARD_MOD_LEFT_CTRL | ORBIS_KEYBOARD_MOD_RIGHT_CTRL))) {
            g_mods |= ORBIS_KEYBOARD_MOD_LEFT_CTRL;
        }
    } else {
        g_mods &= ~(ORBIS_KEYBOARD_MOD_LEFT_CTRL | ORBIS_KEYBOARD_MOD_RIGHT_CTRL);
    }

    // Option / Alt
    if (modifierFlags & (1 << 19)) { // NSEventModifierFlagOption
        if (!(g_mods & (ORBIS_KEYBOARD_MOD_LEFT_ALT | ORBIS_KEYBOARD_MOD_RIGHT_ALT))) {
            g_mods |= ORBIS_KEYBOARD_MOD_LEFT_ALT;
        }
    } else {
        g_mods &= ~(ORBIS_KEYBOARD_MOD_LEFT_ALT | ORBIS_KEYBOARD_MOD_RIGHT_ALT);
    }

    // Command / Meta
    if (modifierFlags & (1 << 20)) { // NSEventModifierFlagCommand
        if (!(g_mods & (ORBIS_KEYBOARD_MOD_LEFT_META | ORBIS_KEYBOARD_MOD_RIGHT_META))) {
            g_mods |= ORBIS_KEYBOARD_MOD_LEFT_META;
        }
    } else {
        g_mods &= ~(ORBIS_KEYBOARD_MOD_LEFT_META | ORBIS_KEYBOARD_MOD_RIGHT_META);
    }

    pthread_mutex_unlock(&g_keyboard_mutex);
}

int32_t sceKeyboardInit(void) {
    pthread_mutex_lock(&g_keyboard_mutex);
    g_keyboard_inited = 1;
    pthread_mutex_unlock(&g_keyboard_mutex);
    return 0;
}

int32_t sceKeyboardOpen(int32_t userID, int32_t type, int32_t index, void* param) {
    (void)userID; (void)type; (void)index; (void)param;
    pthread_mutex_lock(&g_keyboard_mutex);
    g_keyboard_opened = 1;
    pthread_mutex_unlock(&g_keyboard_mutex);
    return 1; // Keyboard handle 1
}

int32_t sceKeyboardClose(int32_t handle) {
    (void)handle;
    pthread_mutex_lock(&g_keyboard_mutex);
    g_keyboard_opened = 0;
    pthread_mutex_unlock(&g_keyboard_mutex);
    return 0;
}

int32_t sceKeyboardGetHandle(int32_t userID, int32_t type, int32_t index) {
    (void)userID; (void)type; (void)index;
    pthread_mutex_lock(&g_keyboard_mutex);
    int opened = g_keyboard_opened;
    pthread_mutex_unlock(&g_keyboard_mutex);
    return opened ? 1 : -1;
}

int32_t sceKeyboardReadState(int32_t handle, OrbisKeyboardData* data) {
    if (!data) return -1;
    (void)handle;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t ts = (uint32_t)(tv.tv_sec * 1000000 + tv.tv_usec);

    pthread_mutex_lock(&g_keyboard_mutex);
    data->timestamp = ts;
    memset(data->padding, 0, sizeof(data->padding));
    data->unk1 = 1;
    data->nkeys = g_num_active_keys;
    data->locks = g_locks;
    data->mods = g_mods;
    memset(data->keycodes, 0, sizeof(data->keycodes));
    for (int i = 0; i < g_num_active_keys && i < 32; i++) {
        data->keycodes[i] = g_active_keys[i];
    }
    pthread_mutex_unlock(&g_keyboard_mutex);

    return 0;
}

int32_t sceKeyboardGetKey2Char(int32_t handle, bool unknown, int32_t locks, int32_t mods, int32_t keycode, OrbisKeyboardKey2Char* data) {
    (void)handle; (void)unknown;
    if (!data) return -1;

    data->ok = 0;
    data->ok2 = 0;
    data->keycode = 0;
    memset(data->unk, 0, sizeof(data->unk));

    bool shift = (mods & (ORBIS_KEYBOARD_MOD_LEFT_SHIFT | ORBIS_KEYBOARD_MOD_RIGHT_SHIFT)) != 0;
    bool caps = (locks & ORBIS_KEYBOARD_CAPS_LOCK) != 0;
    bool ctrl = (mods & (ORBIS_KEYBOARD_MOD_LEFT_CTRL | ORBIS_KEYBOARD_MOD_RIGHT_CTRL)) != 0;

    int ch = -1;

    // A-Z: USB HID 0x04 - 0x1D
    if (keycode >= 0x04 && keycode <= 0x1D) {
        int base = 'a' + (keycode - 0x04);
        if (ctrl) {
            ch = keycode - 0x04 + 1; // Ctrl+A = 1
        } else if (shift ^ caps) {
            ch = 'A' + (keycode - 0x04);
        } else {
            ch = base;
        }
    }
    // 1-0: USB HID 0x1E - 0x27
    else if (keycode >= 0x1E && keycode <= 0x27) {
        static const char unshifted_digits[] = "1234567890";
        static const char shifted_digits[]   = "!@#$%^&*()";
        int idx = keycode - 0x1E;
        ch = shift ? shifted_digits[idx] : unshifted_digits[idx];
    }
    // Return, Escape, Backspace, Tab, Space
    else if (keycode == 0x28) ch = '\n';
    else if (keycode == 0x29) ch = 27; // ESC
    else if (keycode == 0x2A) ch = '\b';
    else if (keycode == 0x2B) ch = '\t';
    else if (keycode == 0x2C) ch = ' ';
    // Symbols
    else if (keycode == 0x2D) ch = shift ? '_' : '-';
    else if (keycode == 0x2E) ch = shift ? '+' : '=';
    else if (keycode == 0x2F) ch = shift ? '{' : '[';
    else if (keycode == 0x30) ch = shift ? '}' : ']';
    else if (keycode == 0x31) ch = shift ? '|' : '\\';
    else if (keycode == 0x33) ch = shift ? ':' : ';';
    else if (keycode == 0x34) ch = shift ? '"' : '\'';
    else if (keycode == 0x35) ch = shift ? '~' : '`';
    else if (keycode == 0x36) ch = shift ? '<' : ',';
    else if (keycode == 0x37) ch = shift ? '>' : '.';
    else if (keycode == 0x38) ch = shift ? '?' : '/';
    // Keypad numbers
    else if (keycode >= 0x59 && keycode <= 0x61) {
        ch = '1' + (keycode - 0x59);
    } else if (keycode == 0x62) {
        ch = '0';
    } else if (keycode == 0x54) ch = '/';
    else if (keycode == 0x55) ch = '*';
    else if (keycode == 0x56) ch = '-';
    else if (keycode == 0x57) ch = '+';
    else if (keycode == 0x58) ch = '\n';
    else if (keycode == 0x63) ch = '.';

    if (ch >= 0) {
        data->ok = 1;
        data->keycode = ch;
    }

    return 0;
}

void ps4_keyboard_destroy(void) {
    pthread_mutex_lock(&g_keyboard_mutex);
    g_keyboard_inited = 0;
    g_keyboard_opened = 0;
    g_num_active_keys = 0;
    memset(g_active_keys, 0, sizeof(g_active_keys));
    g_locks = 0;
    g_mods = 0;
    pthread_mutex_unlock(&g_keyboard_mutex);
}

// Shims
void shim_sceKeyboardInit(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKeyboardInit();
    SHIM_RETURN();
}

void shim_sceKeyboardOpen(GuestContext *ctx) {
    void *param = ctx->rcx ? (void *)(ctx->mem_base + ctx->rcx) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceKeyboardOpen((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx, param);
    SHIM_RETURN();
}

void shim_sceKeyboardClose(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKeyboardClose((int32_t)ctx->rdi);
    SHIM_RETURN();
}

void shim_sceKeyboardReadState(GuestContext *ctx) {
    OrbisKeyboardData *data = ctx->rsi ? (OrbisKeyboardData *)(ctx->mem_base + ctx->rsi) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceKeyboardReadState((int32_t)ctx->rdi, data);
    SHIM_RETURN();
}

void shim_sceKeyboardGetKey2Char(GuestContext *ctx) {
    OrbisKeyboardKey2Char *data = ctx->r9 ? (OrbisKeyboardKey2Char *)(ctx->mem_base + ctx->r9) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceKeyboardGetKey2Char((int32_t)ctx->rdi, (bool)ctx->rsi, (int32_t)ctx->rdx, (int32_t)ctx->rcx, (int32_t)ctx->r8, data);
    SHIM_RETURN();
}

void shim_sceKeyboardGetHandle(GuestContext *ctx) {
    ctx->rax = (uint64_t)(int64_t)sceKeyboardGetHandle((int32_t)ctx->rdi, (int32_t)ctx->rsi, (int32_t)ctx->rdx);
    SHIM_RETURN();
}
