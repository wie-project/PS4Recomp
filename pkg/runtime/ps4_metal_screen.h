#ifndef PS4_METAL_SCREEN_H
#define PS4_METAL_SCREEN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ps4_metal_screen_init(int width, int height, const char *title);
int ps4_metal_screen_present_frame(const void *pixels, size_t pitch, int width, int height);
void ps4_metal_screen_pump_events(void);
void ps4_metal_screen_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_METAL_SCREEN_H
