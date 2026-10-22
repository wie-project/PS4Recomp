#ifndef PS4_METAL_SCREEN_H
#define PS4_METAL_SCREEN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int ps4_metal_screen_init(int width, int height, const char *title);
int ps4_metal_screen_present_frame(const void *pixels, size_t pitch, int width, int height);
// Present a BGRA8Unorm MTLTexture (id<MTLTexture> bridged to void*) whose GPU work has completed.
int ps4_metal_screen_present_texture(void *texture);
// GPU presentation goes through the presenter thread (see docs/present-pacing.md):
// 1. ps4_metal_screen_acquire_frame returns a present slot texture (id<MTLTexture>, BGRA8Unorm,
//    render target) of the given size to render the frame into. It waits while every slot is
//    queued or being shown (back-pressure from the display) and returns NULL when none frees up
//    within a second.
// 2. ps4_metal_screen_encode_present queues `texture` for the presenter behind the work already
//    in `command_buffer` (id<MTLCommandBuffer>, not yet committed): the presenter's copy to the
//    window waits on the GPU for that work. No CPU wait. A texture that is not an acquired slot
//    is copied into a free slot in `command_buffer`, or dropped when none is free.
void *ps4_metal_screen_acquire_frame(int width, int height);
int ps4_metal_screen_encode_present(void *command_buffer, void *texture);
// Guest flip parameters for the vsync pacing mode: sceVideoOutSetFlipRate (0 = 60 Hz, 1 = 30 Hz,
// 2 = 20 Hz) and the flip mode of the last sceVideoOutSubmitFlip.
void ps4_metal_screen_set_guest_flip_rate(int flip_rate);
void ps4_metal_screen_set_guest_flip_mode(int flip_mode);
void ps4_metal_screen_pump_events(void);
// Output gamma from sceVideoOutAdjustColor (1.0 = plain sRGB).
void ps4_metal_screen_set_gamma(float gamma);
void *ps4_metal_screen_get_device(void);
void *ps4_metal_screen_get_command_queue(void);
void *ps4_metal_screen_get_layer(void);
void *ps4_metal_screen_get_texture(void);
void ps4_metal_screen_destroy(void);
// Debug hooks for exit tests: same paths as the window close button and Cmd+Q.
void ps4_metal_screen_request_close(void);
void ps4_metal_screen_request_terminate(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_METAL_SCREEN_H
