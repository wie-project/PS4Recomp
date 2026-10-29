#include <stdatomic.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <ImageIO/ImageIO.h>
#import <MetalFX/MetalFX.h>
#include <math.h>
#include <pthread.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include "ps4_metal_screen.h"
#include "ps4_output_gamma.h"
#include "ps4_present_queue.h"
#include "ps4_present_stats.h"
#include "ps4_screenshot_cadence.h"
#include "recomp_runtime.h"

@interface PS4ScreenDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate>
@end

@implementation PS4ScreenDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    ps4_host_quit(0);
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    ps4_host_quit(0);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    ps4_host_quit(0);
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    ps4_host_quit(0);
    return NSTerminateNow;
}

@end

@interface PS4MetalView : NSView
@end

@implementation PS4MetalView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
- (void)keyDown:(NSEvent *)event { (void)event; }
- (void)keyUp:(NSEvent *)event { (void)event; }
@end

static PS4ScreenDelegate *g_screenDelegate = nil;
static NSWindow *g_window = nil;
static PS4MetalView *g_view = nil;
static CAMetalLayer *g_metalLayer = nil;
static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_commandQueue = nil;
static id<MTLTexture> g_screenTexture = nil;
static int g_width = 1920;
static int g_height = 1080;
static BOOL g_inited = NO;

// AppKit must be driven from the host main thread. Guest code runs on other
// host threads (see ps4_host_run_main_loop), so marshal AppKit work there.
static void run_on_main(dispatch_block_t block) {
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
}

void ps4_host_run_main_loop(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
    for (;;) {
        CFRunLoopRun();
    }
}

static int metal_screen_init_main(int width, int height, const char *title);

int ps4_metal_screen_init(int width, int height, const char *title) {
    if (g_inited) return 0;
    __block int ret = 0;
    run_on_main(^{
        ret = metal_screen_init_main(width, height, title);
    });
    return ret;
}

static int metal_screen_init_main(int width, int height, const char *title) {
    if (g_inited) return 0;
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;
    g_width = width;
    g_height = height;

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // Opt out of App Nap and timer coalescing for the life of the process;
        // the game's frame deadlines are far shorter than the coalescing slack.
        // PS4_NO_LATENCY_ACTIVITY=1 disables this.
        static id g_latencyActivity;
        const char *noAct = getenv("PS4_NO_LATENCY_ACTIVITY");
        if (!g_latencyActivity && !(noAct && noAct[0] == '1')) {
            g_latencyActivity = [[NSProcessInfo processInfo]
                beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                                  reason:@"game"];
        }

        if (!g_screenDelegate) {
            g_screenDelegate = [[PS4ScreenDelegate alloc] init];
        }
        [NSApp setDelegate:g_screenDelegate];

        // Standard macOS Menu Bar with Quit (Cmd+Q) and Close Window (Cmd+W)
        NSMenu *menubar = [[NSMenu alloc] init];
        NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
        [menubar addItem:appMenuItem];
        [NSApp setMainMenu:menubar];

        NSMenu *appMenu = [[NSMenu alloc] init];
        NSString *appName = [[NSProcessInfo processInfo] processName];
        if (!appName || [appName length] == 0) {
            appName = @"PS4Recomp";
        }
        NSString *quitTitle = [@"Quit " stringByAppendingString:appName];
        NSMenuItem *quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                              action:@selector(terminate:)
                                                       keyEquivalent:@"q"];
        [quitMenuItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [appMenu addItem:quitMenuItem];

        NSMenuItem *closeMenuItem = [[NSMenuItem alloc] initWithTitle:@"Close Window"
                                                               action:@selector(performClose:)
                                                        keyEquivalent:@"w"];
        [closeMenuItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
        [appMenu addItem:closeMenuItem];

        [appMenuItem setSubmenu:appMenu];

        g_device = MTLCreateSystemDefaultDevice();
        if (!g_device) {
            fprintf(stderr, "[ps4-metal] Error: Metal is not supported on this device\n");
            return -1;
        }

        g_commandQueue = [g_device newCommandQueue];

        NSRect frame = NSMakeRect(100, 100, g_width / 1.5, g_height / 1.5);
        NSUInteger styleMask = NSWindowStyleMaskTitled |
                               NSWindowStyleMaskClosable |
                               NSWindowStyleMaskMiniaturizable |
                               NSWindowStyleMaskResizable;

        g_window = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:styleMask
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        NSString *titleStr = title ? [NSString stringWithUTF8String:title] : @"PS4Recomp - Display";
        [g_window setTitle:titleStr];
        [g_window setDelegate:g_screenDelegate];
        // tools/tlou_color_run.sh captures this window with `screencapture -l <id>`
        fprintf(stderr, "[metal-screen] window id=%ld\n", (long)[g_window windowNumber]);

        g_view = [[PS4MetalView alloc] initWithFrame:frame];
        [g_view setWantsLayer:YES];

        g_metalLayer = [CAMetalLayer layer];
        g_metalLayer.device = g_device;
        g_metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_metalLayer.framebufferOnly = NO;
        // The frame holds sRGB values; the tag declares that, so macOS colour-matches the layer to
        // the panel (P3 on Mac displays). Untagged, the values go to the panel unmatched. The
        // on-screen effect is expected, not measured: window captures (screencapture) come out the
        // same either way (docs/color-measurement.md). PS4_COLORSPACE=native keeps it untagged.
        const char *cs_env = getenv("PS4_COLORSPACE");
        if (!(cs_env && strcmp(cs_env, "native") == 0)) {
            CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            g_metalLayer.colorspace = srgb;
            CGColorSpaceRelease(srgb);
        }
        // The FPS display is the presenter's own overlay (hud_texture); the system Metal HUD
        // still appears when MTL_HUD_ENABLED=1 is set in the environment at launch.
        g_metalLayer.drawableSize = CGSizeMake(g_width, g_height);
        // Drawable policy (docs/present-pacing.md): three drawables (one on screen, one waiting
        // for its refresh, one being filled) and vsync; the presenter thread is the only GPU-path
        // caller of nextDrawable, so waiting for one never stalls the renderer.
        g_metalLayer.maximumDrawableCount = 3;
        g_metalLayer.displaySyncEnabled = YES;

        [g_view setLayer:g_metalLayer];
        [g_window setContentView:g_view];
        [g_window makeFirstResponder:g_view];
        if (getenv("PS4_BACKGROUND_WINDOW")) {
            // automated test runs: keep the window at desktop level behind everything and do not
            // take focus, so it does not cover what the user is doing
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            [g_window setLevel:kCGDesktopWindowLevel];
            [g_window orderBack:nil];
        } else {
            [g_window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }

        // Create the backing 2D screen texture
        MTLTextureDescriptor *texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                           width:g_width
                                                                                          height:g_height
                                                                                       mipmapped:NO];
        texDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        texDesc.storageMode = MTLStorageModeShared;
        g_screenTexture = [g_device newTextureWithDescriptor:texDesc];

        g_inited = YES;
    }

    ps4_metal_screen_pump_events();
    return 0;
}

// Present statistics: drawable waits, submission spacing and the real on-screen times from the
// drawables' presented handlers; one "[metal-screen] present:" line per window of frames.
static ps4_present_stats g_pstats;
static dispatch_once_t g_pstats_once;

static ps4_present_stats *pstats(void) {
    dispatch_once(&g_pstats_once, ^{
        ps4_present_stats_init(&g_pstats);
    });
    return &g_pstats;
}

static void pstats_presented(id<MTLDrawable> d) {
    ps4_present_stats *s = pstats();
    ps4_present_stats_on_presented(s, d.presentedTime);
    char line[768];
    if (ps4_present_stats_report(s, line, sizeof(line))) fprintf(stderr, "[metal-screen] present: %s\n", line);
}

// -[CAMetalLayer nextDrawable] blocks while every drawable is queued for the display.
static id<CAMetalDrawable> next_drawable_timed(void) {
    const double t0 = CACurrentMediaTime();
    id<CAMetalDrawable> d = [g_metalLayer nextDrawable];
    const double t1 = CACurrentMediaTime();
    ps4_present_stats_on_acquire(pstats(), (t1 - t0) * 1000.0);
    if (d) {
        [d addPresentedHandler:^(id<MTLDrawable> shown) {
            pstats_presented(shown);
        }];
    }
    return d;
}

// PS4_SCREENSHOT_DIR=<dir> [PS4_SCREENSHOT_EVERY=<frames>, default 600]: save
// the presented image as PNG periodically (frame_<n>.png), for headless runs.
// Returns the index of the frame being presented if it is to be saved, else -1.
// Set by debug tooling (PS4_PAD_CTL "shot") to save the next presented frame.
volatile int g_ps4_screenshot_request;

// Called once per flip, in flip order, from the renderer (for presented, held and dropped
// frames) and from the CPU present paths: the counter and the debug request are atomic.
// PS4_SCREENSHOT_EVERY_SEC=<s> (time mode, s a whole number of seconds): the first flip at or after
// each multiple of s seconds of wall time since the first flip is saved as sec_<seconds>.png,
// named by the last multiple it has passed (ps4_screenshot_cadence.h), and a debug request as
// sec_<seconds>_req.png; the index is then the milliseconds since the first flip, so the saved
// line shows how late a shot is. Otherwise every PS4_SCREENSHOT_EVERY-th flip (default 600) and a
// debug request are saved as frame_<flip>.png.
#define PS4_SHOT_REQ_BIT (1L << 40) // in a time-mode index: a debug request, not a cadence shot
static _Atomic bool g_shot_time_mode;
static _Atomic long g_shot_flip;   // flips counted by the frame cadence
static long g_shot_every_sec;      // the time cadence's period (set once, before any time-mode index)
static double g_shot_process_start; // CLOCK_MONOTONIC at load, for the first flip's offset

static double shot_clock(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

__attribute__((constructor)) static void shot_note_start(void) { g_shot_process_start = shot_clock(); }

static long screenshot_due(void) {
    static dispatch_once_t once;
    static const char *dir;
    static long every = 600;
    static ps4_shot_cadence cadence;
    static pthread_mutex_t cadence_mu = PTHREAD_MUTEX_INITIALIZER;
    dispatch_once(&once, ^{
        dir = getenv("PS4_SCREENSHOT_DIR");
        const char *e = getenv("PS4_SCREENSHOT_EVERY");
        if (e && atol(e) > 0) every = atol(e);
        const char *es = getenv("PS4_SCREENSHOT_EVERY_SEC");
        if (es && atol(es) > 0) {
            cadence.every = g_shot_every_sec = atol(es);
            atomic_store(&g_shot_time_mode, true);
        }
    });
    const long n = atomic_fetch_add(&g_shot_flip, 1);
    if (!dir) return -1;
    if (atomic_load_explicit(&g_shot_time_mode, memory_order_relaxed)) {
        const double now = shot_clock();
        pthread_mutex_lock(&cadence_mu); // flips come in order, but from more than one thread
        const bool first = !cadence.started;
        long ms = ps4_shot_cadence_due(&cadence, now);
        if (ms < 0 && __atomic_exchange_n(&g_ps4_screenshot_request, 0, __ATOMIC_ACQ_REL))
            ms = (long)floor((now - cadence.t0) * 1000.0) | PS4_SHOT_REQ_BIT;
        pthread_mutex_unlock(&cadence_mu);
        // Compare between runs: an offset here shifts every pair by the same time. `mono` is the
        // first flip's raw CLOCK_MONOTONIC time: sec_<s>.png was presented at about mono + s, the
        // clock of the other runtime traces (e.g. PS4_VIDEODEC_LOG).
        if (first)
            fprintf(stderr, "[metal-screen] time cadence: every %ld s, first flip at %.3f s after start (mono %.3f)\n",
                    g_shot_every_sec, now - g_shot_process_start, now);
        return ms;
    }
    if (__atomic_exchange_n(&g_ps4_screenshot_request, 0, __ATOMIC_ACQ_REL)) return n;
    if (n % every != 0) return -1;
    return n;
}

// A flip that is not shown (no slot, dropped): the frame cadence still counts it, so its
// numbering stays per flip; the time cadence keeps its due shot for the next flip that is shown.
static void screenshot_skip(void) {
    if (!atomic_load_explicit(&g_shot_time_mode, memory_order_relaxed)) atomic_fetch_add(&g_shot_flip, 1);
}

// Screenshots are encoded and written on a background queue: the PNG encode and the file write
// take tens of milliseconds, and on the presenter path they used to run in the completion
// handler of the frame's command buffer, ahead of the handler that frees its present slot, so
// every saved frame held up the display (and with it the renderer) for that long. At most
// kShotMaxPending images wait for the writer; a frame due while it is that far behind is not
// saved (counted and logged), so a slow disk cannot grow memory without limit.
enum { kShotMaxPending = 4 };
static _Atomic int g_shot_pending;
static _Atomic uint64_t g_shot_dropped;

static dispatch_queue_t shot_queue(void) {
    static dispatch_queue_t q;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        q = dispatch_queue_create("ps4.screenshot",
                                  dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
    });
    return q;
}

// Encodes buf (BGRA8, w x h, `row` bytes per row) as <dir>/frame_<n>.png (time mode:
// sec_<seconds>.png, n in milliseconds) and frees it.
static void write_screenshot_png(uint8_t *buf, size_t w, size_t h, size_t row, long n) {
    const char *dir = getenv("PS4_SCREENSHOT_DIR");
    @autoreleasepool {
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef bc = CGBitmapContextCreate(buf, w, h, 8, row, cs,
            kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
        CGImageRef img = bc ? CGBitmapContextCreateImage(bc) : NULL;
        if (img && dir) {
            const bool timed = atomic_load_explicit(&g_shot_time_mode, memory_order_relaxed);
            const long ms = n & ~PS4_SHOT_REQ_BIT; // time mode: milliseconds since the first flip
            NSString *path =
                !timed                   ? [NSString stringWithFormat:@"%s/frame_%06ld.png", dir, n]
                : (n & PS4_SHOT_REQ_BIT) ? [NSString stringWithFormat:@"%s/sec_%06ld_req.png", dir, ms / 1000]
                                         : [NSString stringWithFormat:@"%s/sec_%06ld.png", dir,
                                                                      ps4_shot_cadence_name(g_shot_every_sec, ms)];
            CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
                (__bridge CFURLRef)[NSURL fileURLWithPath:path], CFSTR("public.png"), 1, NULL);
            if (dst) {
                CGImageDestinationAddImage(dst, img, NULL);
                CGImageDestinationFinalize(dst);
                CFRelease(dst);
                if (timed)
                    fprintf(stderr, "[metal-screen] saved %s (%.3f s after the first flip)\n", path.UTF8String, ms / 1000.0);
                else
                    fprintf(stderr, "[metal-screen] saved %s\n", path.UTF8String);
            }
        }
        if (img) CGImageRelease(img);
        if (bc) CGContextRelease(bc);
        CGColorSpaceRelease(cs);
    }
    free(buf);
}

// Reserves a writer slot for frame n; false (frame counted as not saved) when the writer is
// kShotMaxPending images behind.
static bool shot_reserve(long n) {
    if (atomic_fetch_add(&g_shot_pending, 1) >= kShotMaxPending) {
        atomic_fetch_sub(&g_shot_pending, 1);
        const uint64_t d = atomic_fetch_add(&g_shot_dropped, 1) + 1;
        if ((d & (d - 1)) == 0)
            fprintf(stderr, "[metal-screen] screenshot writer %d images behind: frame %ld not saved (%llu not saved so far)\n",
                    kShotMaxPending, n, (unsigned long long)d);
        return false;
    }
    return true;
}

// Saves `tex` (BGRA8, CPU-readable) as frame n. copy_now: the texture is reused after this call
// (the CPU present paths' screen texture), so its bytes are read here; otherwise the caller hands
// the texture over (the presenter's per-frame capture texture) and the read happens on the writer.
static void save_screenshot(id<MTLTexture> tex, long n, bool copy_now) {
    if (!getenv("PS4_SCREENSHOT_DIR") || !tex || n < 0) return;
    if (!shot_reserve(n)) return;
    const NSUInteger w = tex.width, h = tex.height;
    const size_t row = w * 4;
    uint8_t *buf = NULL;
    if (copy_now) {
        buf = malloc(row * h);
        if (buf) [tex getBytes:buf bytesPerRow:row fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    }
    if (copy_now && !buf) {
        atomic_fetch_sub(&g_shot_pending, 1);
        return;
    }
    dispatch_async(shot_queue(), ^{
        uint8_t *b = buf;
        if (!b) {
            b = malloc(row * h);
            if (b) [tex getBytes:b bytesPerRow:row fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
        }
        if (b) write_screenshot_png(b, w, h, row, n);
        atomic_fetch_sub(&g_shot_pending, 1);
    });
}

static void maybe_save_screenshot(id<MTLTexture> tex) {
    save_screenshot(tex, screenshot_due(), true);
}

static void presenter_drain(void);

static void presented_one(void); // counts a frame put on a drawable (see the presenter)

int ps4_metal_screen_present_frame(const void *pixels, size_t pitch, int width, int height) {
    if (!g_inited) {
        if (ps4_metal_screen_init(width, height, "PS4Recomp - Native Metal Display") != 0) {
            return -1;
        }
    }
    presenter_drain();

    @autoreleasepool {
        // The game may present at a different size than the one the screen
        // was created with (e.g. 3200x1800 PS4 Pro buffers): resize the
        // staging texture and drawable instead of writing out of bounds,
        // which faults the GPU and hangs the queue.
        if (width > 0 && height > 0 && (width != g_width || height != g_height)) {
            g_width = width;
            g_height = height;
            MTLTextureDescriptor *texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                               width:g_width
                                                                                              height:g_height
                                                                                           mipmapped:NO];
            texDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            texDesc.storageMode = MTLStorageModeShared;
            g_screenTexture = [g_device newTextureWithDescriptor:texDesc];
            run_on_main(^{
                g_metalLayer.drawableSize = CGSizeMake(g_width, g_height);
            });
        }
        if (pixels && g_screenTexture) {
            MTLRegion region = MTLRegionMake2D(0, 0, g_width, g_height);
            [g_screenTexture replaceRegion:region
                               mipmapLevel:0
                                 withBytes:pixels
                               bytesPerRow:pitch > 0 ? pitch : (g_width * 4)];
        }

        id<CAMetalDrawable> drawable = next_drawable_timed();
        if (drawable) {
            id<MTLCommandBuffer> commandBuffer = [g_commandQueue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            NSUInteger cw = MIN((NSUInteger)g_width, drawable.texture.width);
            NSUInteger ch = MIN((NSUInteger)g_height, drawable.texture.height);
            [blit copyFromTexture:g_screenTexture
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(cw, ch, 1)
                        toTexture:drawable.texture
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];

            [commandBuffer presentDrawable:drawable];
            ps4_present_stats_on_submit(pstats(), CACurrentMediaTime());
            [commandBuffer commit];
            presented_one();
            [commandBuffer waitUntilScheduled];
            maybe_save_screenshot(g_screenTexture);
        }
    }

    ps4_metal_screen_pump_events();
    return 0;
}

int ps4_metal_screen_present_texture(void *texture) {
    id<MTLTexture> src = (__bridge id<MTLTexture>)texture;
    if (!src) return -1;
    const int width = (int)src.width, height = (int)src.height;
    if (!g_inited) {
        if (ps4_metal_screen_init(width, height, "PS4Recomp - Native Metal Display") != 0) return -1;
    }
    presenter_drain();
    @autoreleasepool {
        if (width != g_width || height != g_height || !g_screenTexture) {
            g_width = width;
            g_height = height;
            MTLTextureDescriptor *texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                               width:g_width
                                                                                              height:g_height
                                                                                           mipmapped:NO];
            texDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            texDesc.storageMode = MTLStorageModeShared;
            g_screenTexture = [g_device newTextureWithDescriptor:texDesc];
            run_on_main(^{
                g_metalLayer.drawableSize = CGSizeMake(g_width, g_height);
            });
        }
        id<MTLCommandBuffer> commandBuffer = [g_commandQueue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        [blit copyFromTexture:src
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(g_width, g_height, 1)
                    toTexture:g_screenTexture
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        id<CAMetalDrawable> drawable = next_drawable_timed();
        if (drawable) {
            NSUInteger cw = MIN((NSUInteger)g_width, drawable.texture.width);
            NSUInteger ch = MIN((NSUInteger)g_height, drawable.texture.height);
            [blit copyFromTexture:src
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(cw, ch, 1)
                        toTexture:drawable.texture
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        [blit endEncoding];
        if (drawable) {
            [commandBuffer presentDrawable:drawable];
            ps4_present_stats_on_submit(pstats(), CACurrentMediaTime());
        }
        [commandBuffer commit];
        if (drawable) presented_one();
        [commandBuffer waitUntilCompleted];
        maybe_save_screenshot(g_screenTexture);
    }
    ps4_metal_screen_pump_events();
    return 0;
}

// ---------------------------------------------------------------------------
// Optional MetalFX spatial upscaling of the presented frame.
//   PS4_UPSCALE=metalfx        upscale to the window's backing size (Retina pixels), aspect kept
//                              (letterboxed); PS4_UPSCALE=off (default) presents 1:1.
//   PS4_UPSCALE_FACTOR=<f>     instead of the window size, upscale by a fixed factor (1..4).
// ---------------------------------------------------------------------------
typedef struct Upscaler {
    int mode; // -1 unknown, 0 off, 1 MetalFX
    double factor;
    id<MTLFXSpatialScaler> scaler;
    id<MTLTexture> out;
    NSUInteger in_w, in_h, out_w, out_h;
    // window backing size in pixels, refreshed from the main thread
    _Atomic uint64_t backing; // w | h << 32
    _Atomic bool backing_query_queued;
    uint64_t frames;
} Upscaler;
static Upscaler g_up = {.mode = -1};

static void upscale_init(void) {
    if (g_up.mode >= 0) return;
    const char *m = getenv("PS4_UPSCALE");
    g_up.mode = 0;
    if (m && !strcasecmp(m, "metalfx")) {
        if ([MTLFXSpatialScalerDescriptor supportsDevice:g_device]) {
            g_up.mode = 1;
        } else {
            fprintf(stderr, "[metal-screen] MetalFX spatial scaling is not supported on this GPU; presenting 1:1\n");
        }
    }
    const char *f = getenv("PS4_UPSCALE_FACTOR");
    g_up.factor = f ? atof(f) : 0.0;
    if (g_up.factor != 0.0 && (g_up.factor < 1.0 || g_up.factor > 4.0)) g_up.factor = 0.0;
    if (g_up.mode == 1)
        fprintf(stderr, "[metal-screen] MetalFX spatial upscaling enabled (%s)\n",
                g_up.factor > 0 ? "fixed factor" : "to the window's backing size");
}

static uint64_t query_backing_main(void) {
    NSRect b = g_view ? [g_view bounds] : NSMakeRect(0, 0, g_width, g_height);
    const CGFloat scale = g_window ? [g_window backingScaleFactor] : 1.0;
    const uint64_t w = (uint64_t)llround(b.size.width * scale), h = (uint64_t)llround(b.size.height * scale);
    return (w ? w : 1) | ((h ? h : 1) << 32);
}

// Window backing size; refreshed asynchronously (no blocking round trip to the main thread per frame).
static void upscale_backing_size(NSUInteger *w, NSUInteger *h) {
    uint64_t v = atomic_load(&g_up.backing);
    if (!v) {
        __block uint64_t q = 0;
        run_on_main(^{
            q = query_backing_main();
        });
        atomic_store(&g_up.backing, q);
        v = q;
    } else if ((g_up.frames % 30) == 0 && !atomic_exchange(&g_up.backing_query_queued, true)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            atomic_store(&g_up.backing, query_backing_main());
            atomic_store(&g_up.backing_query_queued, false);
        });
    }
    *w = (NSUInteger)(v & 0xFFFFFFFFu);
    *h = (NSUInteger)(v >> 32);
}

// Encodes the MetalFX pass; returns the upscaled texture (or nil to present `src` 1:1).
// *dw/*dh: drawable size to use.
static id<MTLTexture> upscale_encode(id<MTLCommandBuffer> cb, id<MTLTexture> src, NSUInteger *dw, NSUInteger *dh) {
    upscale_init();
    if (g_up.mode != 1) return nil;
    g_up.frames++;
    const NSUInteger iw = src.width, ih = src.height;
    NSUInteger tw, th;
    if (g_up.factor > 0) {
        tw = (NSUInteger)llround(iw * g_up.factor);
        th = (NSUInteger)llround(ih * g_up.factor);
    } else {
        upscale_backing_size(&tw, &th);
    }
    // keep the aspect ratio inside the target
    NSUInteger ow = tw, oh = (NSUInteger)llround((double)tw * ih / iw);
    if (oh > th) {
        oh = th;
        ow = (NSUInteger)llround((double)th * iw / ih);
    }
    if (ow <= iw || oh <= ih) return nil; // no upscaling needed
    if (ow > 16384 || oh > 16384) return nil;
    if (!g_up.scaler || g_up.in_w != iw || g_up.in_h != ih || g_up.out_w != ow || g_up.out_h != oh) {
        MTLFXSpatialScalerDescriptor *d = [MTLFXSpatialScalerDescriptor new];
        d.inputWidth = iw;
        d.inputHeight = ih;
        d.outputWidth = ow;
        d.outputHeight = oh;
        d.colorTextureFormat = src.pixelFormat;
        d.outputTextureFormat = MTLPixelFormatBGRA8Unorm;
        // the display buffer holds sRGB-encoded (perceptual) values
        d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
        g_up.scaler = [d newSpatialScalerWithDevice:g_device];
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:ow
                                                                                     height:oh
                                                                                  mipmapped:NO];
        td.storageMode = MTLStorageModePrivate;
        td.usage = g_up.scaler ? g_up.scaler.outputTextureUsage | MTLTextureUsageShaderRead : MTLTextureUsageShaderRead;
        g_up.out = g_up.scaler ? [g_device newTextureWithDescriptor:td] : nil;
        g_up.in_w = iw;
        g_up.in_h = ih;
        g_up.out_w = ow;
        g_up.out_h = oh;
        if (!g_up.scaler || !g_up.out) {
            fprintf(stderr, "[metal-screen] cannot create the MetalFX scaler (%lux%lu -> %lux%lu); presenting 1:1\n",
                    (unsigned long)iw, (unsigned long)ih, (unsigned long)ow, (unsigned long)oh);
            g_up.mode = 0;
            g_up.scaler = nil;
            return nil;
        }
        fprintf(stderr, "[metal-screen] MetalFX: %lux%lu -> %lux%lu (window %lux%lu)\n", (unsigned long)iw,
                (unsigned long)ih, (unsigned long)ow, (unsigned long)oh, (unsigned long)tw, (unsigned long)th);
    }
    g_up.scaler.colorTexture = src;
    g_up.scaler.outputTexture = g_up.out;
    g_up.scaler.inputContentWidth = iw;
    g_up.scaler.inputContentHeight = ih;
    [g_up.scaler encodeToCommandBuffer:cb];
    *dw = tw;
    *dh = th;
    return g_up.out;
}

// Output gamma (sceVideoOutAdjustColor), see ps4_output_gamma.h. 1.0 presents the frame as is.
static _Atomic uint32_t g_gamma_bits = 0x3f800000u; // 1.0f
static id<MTLRenderPipelineState> g_gamma_pso;

void ps4_metal_screen_set_gamma(float gamma) {
    uint32_t bits;
    memcpy(&bits, &gamma, sizeof(bits));
    atomic_store(&g_gamma_bits, bits);
}

static float current_gamma(void) {
    const uint32_t bits = atomic_load(&g_gamma_bits);
    float g;
    memcpy(&g, &bits, sizeof(g));
    return g;
}

static id<MTLRenderPipelineState> gamma_pipeline(void) {
    if (g_gamma_pso) return g_gamma_pso;
    static NSString *const src = @"#include <metal_stdlib>\n"
                                  "using namespace metal;\n"
                                  "struct VOut { float4 pos [[position]]; };\n"
                                  "vertex VOut gamma_vs(uint vid [[vertex_id]]) {\n"
                                  "    float2 p = float2((vid << 1) & 2, vid & 2);\n"
                                  "    VOut o; o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0); return o;\n"
                                  "}\n"
                                  "// prm: xy = destination origin of the frame, z = encoding exponent (ps4_output_gamma_param), w = 1 if the source\n"
                                  "// view already decodes sRGB\n"
                                  "fragment float4 gamma_fs(VOut in [[stage_in]], texture2d<float> src [[texture(0)]],\n"
                                  "                         constant float4& prm [[buffer(0)]]) {\n"
                                  "    float3 e = src.read(uint2(in.pos.xy - prm.xy)).rgb;\n"
                                  "    float3 enc = prm.w > 0.5 ? select(1.055 * pow(e, 1.0 / 2.4) - 0.055, 12.92 * e, e <= 0.0031308) : e;\n"
                                  "    float3 o = pow(enc, prm.z);\n"
                                  "    return float4(saturate(o), 1.0);\n"
                                  "}\n";
    NSError *err = nil;
    id<MTLLibrary> lib = [g_device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        fprintf(stderr, "[metal-screen] gamma shader: %s\n", err.localizedDescription.UTF8String);
        return nil;
    }
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@"gamma_vs"];
    d.fragmentFunction = [lib newFunctionWithName:@"gamma_fs"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_gamma_pso = [g_device newRenderPipelineStateWithDescriptor:d error:&err];
    if (!g_gamma_pso) fprintf(stderr, "[metal-screen] gamma pipeline: %s\n", err.localizedDescription.UTF8String);
    return g_gamma_pso;
}

static bool is_srgb_format(MTLPixelFormat f) {
    return f == MTLPixelFormatBGRA8Unorm_sRGB || f == MTLPixelFormatRGBA8Unorm_sRGB;
}

// Performance overlay: frame rate and frame time drawn by the presenter into the corner of
// the drawable (the screenshot path copies the frame before it, so captures stay clean).
// It replaces the system Metal HUD, which does not appear for an app started from Finder.
// Enabled unless MTL_HUD_ENABLED=0 (the "FPS HUD" checkbox of the settings window).
#define HUD_SLOTS 3
static struct {
    int state; // 0 unknown, 1 on, -1 off
    double window_start, last;
    int frames;
    double max_ms;
    char text[160];
    bool dirty;
    id<MTLTexture> tex[HUD_SLOTS];
    NSUInteger slot, tex_w, tex_h, font_px;
} g_hud;

// Set by the renderer's thread once, read by the presenter thread (a static string, never freed).
static _Atomic(const char *) g_backend_label;

void ps4_metal_screen_set_backend_label(const char *label) {
    atomic_store_explicit(&g_backend_label, label, memory_order_release);
}

const char *ps4_metal_screen_backend_label(void) {
    return atomic_load_explicit(&g_backend_label, memory_order_acquire);
}

static bool hud_enabled(void) {
    if (g_hud.state == 0) {
        const char *e = getenv("MTL_HUD_ENABLED");
        g_hud.state = (e && strcmp(e, "0") == 0) ? -1 : 1;
    }
    return g_hud.state > 0;
}

// Accumulates one presented frame; refreshes the text twice a second.
static void hud_tick(NSUInteger src_w, NSUInteger src_h, NSUInteger out_w, NSUInteger out_h) {
    const double now = CACurrentMediaTime();
    if (g_hud.last > 0) {
        const double ms = (now - g_hud.last) * 1000.0;
        if (ms > g_hud.max_ms) g_hud.max_ms = ms;
        g_hud.frames++;
    } else {
        g_hud.window_start = now;
    }
    g_hud.last = now;
    const double span = now - g_hud.window_start;
    if (span < 0.5 || g_hud.frames == 0) return;
    const double fps = g_hud.frames / span;
    const char *mode = ps4_host_setting_pro_mode() ? "PS4 Pro" : "PS4";
    const char *backend = ps4_metal_screen_backend_label();
    const char *sep = backend ? "  " : "";
    if (!backend) backend = "";
    if (out_w != src_w || out_h != src_h) {
        snprintf(g_hud.text, sizeof(g_hud.text), "%.0f FPS  %.1f ms  (max %.0f)%s%s\n%s %lux%lu -> %lux%lu MetalFX",
                 fps, 1000.0 / fps, g_hud.max_ms, sep, backend, mode, (unsigned long)src_w, (unsigned long)src_h,
                 (unsigned long)out_w, (unsigned long)out_h);
    } else {
        snprintf(g_hud.text, sizeof(g_hud.text), "%.0f FPS  %.1f ms  (max %.0f)%s%s\n%s %lux%lu", fps, 1000.0 / fps,
                 g_hud.max_ms, sep, backend, mode, (unsigned long)src_w, (unsigned long)src_h);
    }
    g_hud.window_start = now;
    g_hud.frames = 0;
    g_hud.max_ms = 0;
    g_hud.dirty = true;
}

// Renders the text into the next shared texture slot (slots rotate, so the GPU never reads
// a texture that is being rewritten); returns the texture to copy into the drawable.
static id<MTLTexture> hud_texture(NSUInteger target_h) {
    if (!g_hud.text[0]) return nil;
    const NSUInteger font_px = MAX((NSUInteger)14, target_h / 48);
    if (font_px != g_hud.font_px) {
        g_hud.font_px = font_px;
        g_hud.tex_w = font_px * 26; // the first line ends with the backend ("Metal 4")
        g_hud.tex_h = font_px * 3;
        for (int i = 0; i < HUD_SLOTS; i++) g_hud.tex[i] = nil;
        g_hud.dirty = true;
    }
    if (!g_hud.dirty) return g_hud.tex[g_hud.slot];
    g_hud.dirty = false;
    g_hud.slot = (g_hud.slot + 1) % HUD_SLOTS;
    if (!g_hud.tex[g_hud.slot]) {
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:g_hud.tex_w
                                                                                     height:g_hud.tex_h
                                                                                  mipmapped:NO];
        td.storageMode = MTLStorageModeShared;
        td.usage = MTLTextureUsageShaderRead;
        g_hud.tex[g_hud.slot] = [g_device newTextureWithDescriptor:td];
    }
    id<MTLTexture> tex = g_hud.tex[g_hud.slot];
    const size_t w = g_hud.tex_w, h = g_hud.tex_h, stride = w * 4;
    void *pixels = calloc(h, stride);
    if (!pixels) return nil;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    // BGRA in memory, matching MTLPixelFormatBGRA8Unorm
    CGContextRef ctx = CGBitmapContextCreate(pixels, w, h, 8, stride, cs,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        free(pixels);
        return nil;
    }
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
    CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
    NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:gc];
    NSDictionary *attrs = @{
        NSFontAttributeName : [NSFont monospacedDigitSystemFontOfSize:(CGFloat)font_px weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName : [NSColor colorWithSRGBRed:0.55 green:1.0 blue:0.45 alpha:1.0],
    };
    NSString *str = [NSString stringWithUTF8String:g_hud.text];
    [str drawInRect:NSMakeRect(font_px * 0.5, 0, w - font_px, h - font_px * 0.3) withAttributes:attrs];
    [NSGraphicsContext restoreGraphicsState];
    CGContextFlush(ctx);
    [tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:pixels bytesPerRow:stride];
    CGContextRelease(ctx);
    free(pixels);
    return tex;
}

// PS4_SCREENSHOT_STAGE=output saves screenshots after the output gamma pass (what the window
// shows, without the HUD and the layer's colour matching); the default "render" saves the frame
// as rendered, before it.
static bool screenshot_after_gamma(void) {
    static int v = -1;
    if (v < 0) {
        const char *s = getenv("PS4_SCREENSHOT_STAGE");
        v = s && strcmp(s, "output") == 0;
    }
    return v == 1;
}

// Presenter thread: encodes the presentation of `src` into `cb`: the frame (MetalFX-upscaled if
// enabled) is copied into the window's drawable, which is presented when `cb` has executed, at
// least `min_duration` seconds after the previous frame appeared (0 = next refresh). shot_idx
// >= 0 also saves the frame as a screenshot. The caller commits.
static void present_encode(id<MTLCommandBuffer> cb, id<MTLTexture> src, long shot_idx, double min_duration,
                           id<CAMetalDrawable> drawable) {
    @autoreleasepool {
        NSUInteger dw = src.width, dh = src.height;
        id<MTLTexture> up = upscale_encode(cb, src, &dw, &dh);
        id<MTLTexture> img = up ? up : src;
        if ((int)dw != g_width || (int)dh != g_height) {
            g_width = (int)dw;
            g_height = (int)dh;
            run_on_main(^{
                g_metalLayer.drawableSize = CGSizeMake(dw, dh);
            });
        }
        const NSUInteger iw = img.width, ih = img.height;
        const float gamma = current_gamma();
        const float gamma_prm_z = ps4_output_gamma_param(gamma);
        id<MTLRenderPipelineState> gamma_pso = fabsf(gamma - 1.0f) > 0.001f ? gamma_pipeline() : nil;
        const bool shot_gamma = shot_idx >= 0 && gamma_pso && screenshot_after_gamma();
        id<MTLTexture> shot = nil;
        if (shot_idx >= 0) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                          width:iw
                                                                                         height:ih
                                                                                      mipmapped:NO];
            td.storageMode = MTLStorageModeShared;
            if (shot_gamma) td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            shot = [g_device newTextureWithDescriptor:td];
        }
        // The drawable was taken before the frame's size was known: a resize needs a new one. A nil
        // drawable (window minimized or closed; nextDrawable already waited its 1 s) is not asked
        // for again: this frame is not shown.
        if (drawable && (drawable.texture.width != dw || drawable.texture.height != dh)) {
            drawable = nil;
            drawable = next_drawable_timed();
        }
        NSUInteger ox = 0, oy = 0;
        if (drawable && gamma_pso) {
            // copy with the output gamma applied (letterbox bars cleared by the same pass)
            const NSUInteger tw = drawable.texture.width, th = drawable.texture.height;
            ox = (tw - MIN(iw, tw)) / 2;
            oy = (th - MIN(ih, th)) / 2;
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = drawable.texture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
            [enc setRenderPipelineState:gamma_pso];
            [enc setViewport:(MTLViewport){(double)ox, (double)oy, (double)MIN(iw, tw), (double)MIN(ih, th), 0.0, 1.0}];
            const float prm[4] = {(float)ox, (float)oy, gamma_prm_z, is_srgb_format(img.pixelFormat) ? 1.0f : 0.0f};
            [enc setFragmentBytes:prm length:sizeof(prm) atIndex:0];
            [enc setFragmentTexture:img atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [enc endEncoding];
        } else if (drawable) {
            const NSUInteger tw = drawable.texture.width, th = drawable.texture.height;
            if (iw < tw || ih < th) {
                // letterbox: clear the bars
                ox = (tw - MIN(iw, tw)) / 2;
                oy = (th - MIN(ih, th)) / 2;
                MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = drawable.texture;
                rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
                rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                [[cb renderCommandEncoderWithDescriptor:rp] endEncoding];
            }
        }
        if (shot_gamma) {
            MTLRenderPassDescriptor *sp = [MTLRenderPassDescriptor renderPassDescriptor];
            sp.colorAttachments[0].texture = shot;
            sp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            sp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> se = [cb renderCommandEncoderWithDescriptor:sp];
            [se setRenderPipelineState:gamma_pso];
            [se setViewport:(MTLViewport){0.0, 0.0, (double)iw, (double)ih, 0.0, 1.0}];
            const float sprm[4] = {0.0f, 0.0f, gamma_prm_z, is_srgb_format(img.pixelFormat) ? 1.0f : 0.0f};
            [se setFragmentBytes:sprm length:sizeof(sprm) atIndex:0];
            [se setFragmentTexture:img atIndex:0];
            [se drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [se endEncoding];
        }
        if (drawable || shot) {
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            if (drawable && !gamma_pso) {
                NSUInteger cw = MIN(iw, drawable.texture.width);
                NSUInteger ch = MIN(ih, drawable.texture.height);
                [blit copyFromTexture:img
                          sourceSlice:0
                          sourceLevel:0
                         sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(cw, ch, 1)
                            toTexture:drawable.texture
                     destinationSlice:0
                     destinationLevel:0
                    destinationOrigin:MTLOriginMake(ox, oy, 0)];
            }
            if (shot && !shot_gamma) [blit copyFromTexture:img toTexture:shot];
            if (drawable && hud_enabled()) {
                hud_tick(src.width, src.height, iw, ih);
                const NSUInteger tw = drawable.texture.width, th = drawable.texture.height;
                id<MTLTexture> hud = hud_texture(th);
                const NSUInteger m = g_hud.font_px / 2;
                if (hud && hud.width + m <= tw && hud.height + m <= th) {
                    [blit copyFromTexture:hud
                              sourceSlice:0
                              sourceLevel:0
                             sourceOrigin:MTLOriginMake(0, 0, 0)
                               sourceSize:MTLSizeMake(hud.width, hud.height, 1)
                                toTexture:drawable.texture
                         destinationSlice:0
                         destinationLevel:0
                        destinationOrigin:MTLOriginMake(m, m, 0)];
                }
            }
            [blit endEncoding];
        }
        if (drawable) {
            if (min_duration > 0)
                [cb presentDrawable:drawable afterMinimumDuration:min_duration];
            else
                [cb presentDrawable:drawable];
            ps4_present_stats_on_submit(pstats(), CACurrentMediaTime());
        }
        if (shot) {
            [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
                (void)done;
                save_screenshot(shot, shot_idx, false); // queued: the slot is released right after
            }];
        }
    }
}

// ---------------------------------------------------------------------------
// Presenter thread (docs/present-pacing.md). The renderer converts each flipped display buffer
// into a present slot and queues it (ps4_metal_screen_acquire_frame / _encode_present); this
// thread waits for a drawable, copies the slot into it on its own command buffer, which waits
// on the GPU for the renderer's work (shared event), and presents it.
//   PS4_PRESENT_MODE=uncapped (default) | vsync | mailbox   (see ps4_present_queue.h)
// ---------------------------------------------------------------------------
#define PRESENT_SLOTS 3

typedef struct PresentSlot {
    id<MTLTexture> tex; // the converted display buffer (BGRA8Unorm)
    uint64_t ready;     // g_pr.ready value the renderer's command buffer signals once tex is written
    long shot_idx;      // screenshot index of this frame, -1 = none
} PresentSlot;

static struct {
    dispatch_once_t once;
    _Atomic bool started;
    ps4_present_queue q;
    PresentSlot slot[PRESENT_SLOTS];
    id<MTLCommandQueue> queue; // the presenter's own: a frame waiting for its renderer work never
                               // holds up the CPU present paths on g_commandQueue
    id<MTLSharedEvent> ready;
    _Atomic uint64_t ready_value;
    _Atomic bool drain_timed_out;
    ps4_present_mode mode;
    _Atomic uint64_t display_hz; // refresh rate of the window's screen, 0 = unknown
    _Atomic bool hz_query_queued;
    _Atomic uint64_t presented; // frames the presenter has put on a drawable
} g_pr;

static _Atomic int g_guest_flip_rate = 0;
static _Atomic int g_guest_flip_mode = PS4_FLIP_MODE_VSYNC;

void ps4_metal_screen_set_guest_flip_rate(int flip_rate) {
    atomic_store(&g_guest_flip_rate, flip_rate);
}

void ps4_metal_screen_set_guest_flip_mode(int flip_mode) {
    atomic_store(&g_guest_flip_mode, flip_mode);
}

static uint64_t query_display_hz_main(void) {
    NSScreen *scr = g_window.screen ? g_window.screen : [NSScreen mainScreen];
    const NSInteger fps = scr ? scr.maximumFramesPerSecond : 0;
    return fps > 0 ? (uint64_t)fps : 0;
}

static struct {
    void (*created)(void *, void *);
    void (*released)(void *, void *);
    void *ctx;
} g_tex_hooks;

void ps4_metal_screen_set_texture_hooks(void (*created)(void *, void *), void (*released)(void *, void *), void *ctx) {
    g_tex_hooks.created = created;
    g_tex_hooks.released = released;
    g_tex_hooks.ctx = ctx;
}

// A new slot or held texture in place of old (nil the first time), reported to the hooks.
static id<MTLTexture> hooked_texture(MTLTextureDescriptor *td, id<MTLTexture> old) {
    id<MTLTexture> t = [g_device newTextureWithDescriptor:td];
    if (old && g_tex_hooks.released) g_tex_hooks.released(g_tex_hooks.ctx, (__bridge void *)old);
    if (t && g_tex_hooks.created) g_tex_hooks.created(g_tex_hooks.ctx, (__bridge void *)t);
    return t;
}

static bool slot_texture(PresentSlot *ps, NSUInteger w, NSUInteger h) {
    if (ps->tex && ps->tex.width == w && ps->tex.height == h) return true;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    ps->tex = hooked_texture(td, ps->tex);
    return ps->tex != nil;
}

// Held frames (see "Held frames" below): the presenter takes them straight from their textures.
static int held_take(void);
static void present_held(int h, id<CAMetalDrawable> drawable);
static void held_init(void);
static void presented_one(void);

static void present_slot(int s, id<CAMetalDrawable> drawable) {
    PresentSlot *ps = &g_pr.slot[s];
    id<MTLCommandBuffer> cb = [g_pr.queue commandBuffer];
    [cb encodeWaitForEvent:g_pr.ready value:ps->ready];
    const double min_duration =
        ps4_present_min_duration(g_pr.mode, atomic_load(&g_guest_flip_rate), atomic_load(&g_guest_flip_mode),
                                 (double)atomic_load(&g_pr.display_hz));
    present_encode(cb, ps->tex, ps->shot_idx, min_duration, drawable);
    [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
        (void)done;
        ps4_pq_release(&g_pr.q, s);
    }];
    [cb commit];
    presented_one();
}

static void presented_one(void) {
    if ((atomic_fetch_add(&g_pr.presented, 1) + 1) % 120 == 0 && !atomic_exchange(&g_pr.hz_query_queued, true)) {
        // the window may have moved to another screen
        dispatch_async(dispatch_get_main_queue(), ^{
            atomic_store(&g_pr.display_hz, query_display_hz_main());
            atomic_store(&g_pr.hz_query_queued, false);
        });
    }
}

static void *presenter_main(void *arg) {
    (void)arg;
    pthread_setname_np("Metal Present");
    for (;;) {
        @autoreleasepool {
            // Get the drawable first and only then let the renderer have a slot (a credit):
            // while the display is behind, the renderer waits at the same point where it waited
            // in nextDrawable before, so the thread adds no frames of latency. Mailbox never waits.
            // The drawable stays held while no frame comes: it is what backs the credit (giving it
            // back would let the renderer run ahead of the display again). That leaves 2 of the 3
            // drawables to the CPU present paths while the GPU path is idle, which is enough: the
            // display keeps releasing them.
            id<CAMetalDrawable> drawable = next_drawable_timed();
            if (g_pr.mode != PS4_PRESENT_MAILBOX) ps4_pq_grant(&g_pr.q);
            // the next frame: a queued slot (always older than any held frame), else a held frame
            // once no slot frame is ahead of it (presented straight from its texture, at the
            // point where a slot would have taken it)
            int s = -1, h = -1;
            pthread_mutex_lock(&g_pr.q.mu);
            for (;;) {
                if (g_pr.q.stop) break;
                if ((s = ps4_pq_try_take_locked(&g_pr.q)) >= 0) break;
                if ((h = held_take()) >= 0) break;
                pthread_cond_wait(&g_pr.q.cv, &g_pr.q.mu);
            }
            pthread_mutex_unlock(&g_pr.q.mu);
            if (s >= 0) present_slot(s, drawable);
            else if (h >= 0) present_held(h, drawable);
            else return NULL;
        }
    }
}

static void presenter_start(void) {
    dispatch_once(&g_pr.once, ^{
        const char *m = getenv("PS4_PRESENT_MODE");
        if (!ps4_present_parse_mode(m, &g_pr.mode))
            fprintf(stderr, "[metal-screen] unknown PS4_PRESENT_MODE=%s (uncapped, vsync, mailbox); using uncapped\n", m);
        ps4_pq_init(&g_pr.q, PRESENT_SLOTS, g_pr.mode == PS4_PRESENT_MAILBOX);
        for (int i = 0; i < PRESENT_SLOTS; i++) g_pr.slot[i].shot_idx = -1;
        g_pr.queue = [g_device newCommandQueue];
        g_pr.queue.label = @"present";
        g_pr.ready = [g_device newSharedEvent];
        held_init();
        __block uint64_t hz = 0;
        run_on_main(^{
            hz = query_display_hz_main();
        });
        atomic_store(&g_pr.display_hz, hz);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        // on the frame's critical path like the GNM encoder thread
        pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
        pthread_t th;
        const int rc = pthread_create(&th, &attr, presenter_main, NULL);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            fprintf(stderr, "[metal-screen] cannot start the presenter thread (%d)\n", rc);
            abort();
        }
        atomic_store(&g_pr.started, true);
        fprintf(stderr, "[metal-screen] presenter thread: mode %s, %d slots, %lu drawables, display %llu Hz\n",
                ps4_present_mode_name(g_pr.mode), PRESENT_SLOTS, (unsigned long)g_metalLayer.maximumDrawableCount,
                (unsigned long long)hz);
    });
}

// The synchronous CPU paths present on the screen's queue directly: let queued frames go first so
// frames stay in order. Once a drain has timed out (the renderer's GPU work does not finish) the
// CPU paths stop waiting until the presenter is idle again.
static void presenter_drain(void) {
    if (!atomic_load(&g_pr.started)) return;
    const bool idle = ps4_pq_drain(&g_pr.q, atomic_load(&g_pr.drain_timed_out) ? 0.0 : 1.0);
    if (!idle && !atomic_exchange(&g_pr.drain_timed_out, true))
        fprintf(stderr, "[metal-screen] presenter still busy after 1 s; CPU presents no longer wait for it\n");
    if (idle) atomic_store(&g_pr.drain_timed_out, false);
}

void *ps4_metal_screen_try_acquire_frame(int width, int height) {
    if (width <= 0 || height <= 0) return NULL;
    if (!g_inited && ps4_metal_screen_init(width, height, "PS4Recomp - Native Metal Display") != 0) return NULL;
    presenter_start();
    int superseded = 0;
    const int s = ps4_pq_acquire(&g_pr.q, 0, &superseded, NULL);
    if (superseded) ps4_present_stats_on_superseded(pstats());
    if (s < 0) return NULL;
    if (!slot_texture(&g_pr.slot[s], (NSUInteger)width, (NSUInteger)height)) {
        ps4_pq_cancel(&g_pr.q, s);
        return NULL;
    }
    return (__bridge void *)g_pr.slot[s].tex;
}

void ps4_metal_screen_note_backpressure(double wait_ms) {
    ps4_present_stats_on_backpressure(pstats(), wait_ms);
}

uint64_t ps4_metal_screen_presented_count(void) {
    return atomic_load(&g_pr.presented);
}

// ---------------------------------------------------------------------------
// Held frames (docs/present-pacing.md, "No waiting on the display"). A frame that finds every
// present slot taken is converted by the renderer into one of these textures; the renderer's
// command buffer signals g_held.ev once it is written, and nothing waits for the display. The
// presenter takes held frames where it would take a queued slot (after any queued slot, which is
// older), and presents straight from the held texture: its command buffer waits for g_held.ev on
// the GPU, then copies into the drawable as for a slot. The flip completes when that command
// buffer completes. At most g_hold_depth frames are held (the guest's display buffers minus one,
// at most kHeldMax): one more drops the oldest; a frame held for kHeldTimeoutS (the presenter is
// stuck) is dropped and its flip completed at once. When the renderer's GPU work is given up
// (ps4_metal_screen_release_held), every held frame's flip completes without waiting.
// ---------------------------------------------------------------------------
enum { kHeldMax = 3, kHeldTextures = kHeldMax + 1 };
static const double kHeldTimeoutS = 1.0;
enum { HELD_FREE = 0, HELD_RENDERING, HELD_QUEUED, HELD_PRESENTING, HELD_DROPPING };
static _Atomic int g_hold_depth = kHeldMax;

typedef struct HeldFrame {
    id<MTLTexture> tex;
    int state;
    uint64_t ready;  // g_held.ev value the renderer's command buffer signals once tex is written
    uint64_t order;  // queue order; identifies this use of the texture
    long shot_idx;   // screenshot index (taken when queued, in flip order)
    double since;    // when it was queued (monotonic seconds)
    bool completed;  // its flip completion (done) has run or is running
    ps4_metal_screen_held_done_fn done;
    void *arg;
} HeldFrame;

static struct {
    pthread_mutex_t mu;
    HeldFrame f[kHeldTextures];
    uint64_t next_order;
    id<MTLSharedEvent> ev;
    _Atomic uint64_t ev_value;
    MTLSharedEventListener *listener;
    dispatch_queue_t q; // drop listeners and the timeout timer; set once in held_init
    _Atomic bool timer_armed;
} g_held = {.mu = PTHREAD_MUTEX_INITIALIZER};

int ps4_metal_screen_present_begin(void *texture, ps4_metal_screen_present_op *op) {
    id<MTLTexture> src = (__bridge id<MTLTexture>)texture;
    if (!src || !op) return -1;
    if (!g_inited) {
        if (ps4_metal_screen_init((int)src.width, (int)src.height, "PS4Recomp - Native Metal Display") != 0) return -1;
    }
    presenter_start();
    int s = -1;
    pthread_mutex_lock(&g_pr.q.mu);
    for (int i = 0; i < PRESENT_SLOTS; i++)
        if (g_pr.q.state[i] == PS4_PQ_RENDERING && g_pr.slot[i].tex == src) s = i;
    pthread_mutex_unlock(&g_pr.q.mu);
    op->copy_dst = NULL;
    if (s < 0) {
        // not an acquired slot: copied into a free one behind the caller's work, without waiting
        int superseded = 0;
        double waited = 0;
        s = ps4_pq_acquire(&g_pr.q, 0, &superseded, &waited);
        if (superseded) ps4_present_stats_on_superseded(pstats());
        if (s >= 0 && (src.pixelFormat != MTLPixelFormatBGRA8Unorm || !slot_texture(&g_pr.slot[s], src.width, src.height))) {
            ps4_pq_cancel(&g_pr.q, s);
            s = -1;
        }
        if (s < 0) {
            ps4_present_stats_on_superseded(pstats()); // this frame is not shown
            screenshot_skip();                         // but keeps the per-flip numbering
            ps4_metal_screen_pump_events();
            return 1;
        }
        op->copy_dst = (__bridge void *)g_pr.slot[s].tex;
    }
    op->event = (__bridge void *)g_pr.ready;
    op->value = atomic_fetch_add(&g_pr.ready_value, 1) + 1;
    op->slot = s;
    return 0;
}

void ps4_metal_screen_present_commit(const ps4_metal_screen_present_op *op) {
    g_pr.slot[op->slot].ready = op->value;
    g_pr.slot[op->slot].shot_idx = screenshot_due();
    ps4_pq_submit(&g_pr.q, op->slot, g_pr.slot[op->slot].shot_idx >= 0);
    ps4_metal_screen_pump_events();
}

// A failed command buffer may not have reached its signal; the presenter must not wait for it
// forever. Only while value is the newest one handed out: a later command buffer's GPU signal
// could otherwise be lowered again by this CPU write.
void ps4_metal_screen_present_failed(const ps4_metal_screen_present_op *op) {
    id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)op->event;
    _Atomic uint64_t *newest = ev == g_held.ev ? &g_held.ev_value : &g_pr.ready_value;
    if (atomic_load(newest) == op->value && ev.signaledValue < op->value) ev.signaledValue = op->value;
}

int ps4_metal_screen_encode_present(void *command_buffer, void *texture) {
    id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)command_buffer;
    if (!cb) return -1;
    ps4_metal_screen_present_op op;
    const int r = ps4_metal_screen_present_begin(texture, &op);
    if (r != 0) return r < 0 ? -1 : 0;
    @autoreleasepool {
        if (op.copy_dst) {
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromTexture:(__bridge id<MTLTexture>)texture toTexture:(__bridge id<MTLTexture>)op.copy_dst];
            [blit endEncoding];
        }
        [cb encodeSignalEvent:(__bridge id<MTLSharedEvent>)op.event value:op.value];
        const ps4_metal_screen_present_op o = op;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            if (done.status == MTLCommandBufferStatusError) ps4_metal_screen_present_failed(&o);
        }];
        ps4_metal_screen_present_commit(&op);
    }
    return 0;
}


static double held_now(void) {
    return CACurrentMediaTime();
}

// presenter_start (dispatch_once): before the presenter thread or any renderer call uses it.
static void held_init(void) {
    g_held.q = dispatch_queue_create(
        "ps4.present-held", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0));
    g_held.ev = [g_device newSharedEvent];
    g_held.listener = [[MTLSharedEventListener alloc] initWithDispatchQueue:g_held.q];
}

static int held_oldest_locked(int state) {
    int best = -1;
    for (int i = 0; i < kHeldTextures; i++)
        if (g_held.f[i].state == state && (best < 0 || g_held.f[i].order < g_held.f[best].order)) best = i;
    return best;
}

// Marks frame i's flip as completing and returns its callback (NULL if that already happened or
// the texture now holds another frame). The caller runs it without g_held.mu.
static ps4_metal_screen_held_done_fn held_claim_locked(int i, uint64_t order, void **arg) {
    HeldFrame *h = &g_held.f[i];
    if (h->order != order || h->completed) return NULL;
    h->completed = true;
    *arg = h->arg;
    return h->done;
}

// Frame i (with this order) is no longer read by the GPU: its texture is free again.
static void held_free(int i, uint64_t order) {
    pthread_mutex_lock(&g_held.mu);
    if (g_held.f[i].order == order && g_held.f[i].state != HELD_RENDERING) g_held.f[i].state = HELD_FREE;
    pthread_mutex_unlock(&g_held.mu);
}

static void held_complete(int i, uint64_t order, int shown) {
    void *arg = NULL;
    pthread_mutex_lock(&g_held.mu);
    ps4_metal_screen_held_done_fn done = held_claim_locked(i, order, &arg);
    pthread_mutex_unlock(&g_held.mu);
    if (done) done(arg, shown);
}

typedef struct HeldDrop {
    int i;
    uint64_t order, ready;
    bool now; // complete the flip now (timeout), else once the renderer has written the frame
} HeldDrop;

// Drops frame i (QUEUED). The caller registers the drop with held_finish_drop after releasing
// g_held.mu: an already signalled event runs the listener's block at once.
static HeldDrop held_drop_locked(int i, bool now, const char *why) {
    HeldFrame *h = &g_held.f[i];
    h->state = HELD_DROPPING;
    ps4_present_stats_on_held_dropped(pstats());
    static _Atomic uint64_t drops;
    const uint64_t n = atomic_fetch_add(&drops, 1) + 1;
    if ((n & (n - 1)) == 0) fprintf(stderr, "[metal-screen] held frame dropped: %s (%llu so far)\n", why, (unsigned long long)n);
    return (HeldDrop){i, h->order, h->ready, now};
}

static void held_finish_drop(HeldDrop d) {
    if (d.now) held_complete(d.i, d.order, 0);
    const int i = d.i;
    const uint64_t order = d.order;
    [g_held.ev notifyListener:g_held.listener
                      atValue:d.ready
                        block:^(id<MTLSharedEvent> ev, uint64_t value) {
                            (void)ev;
                            (void)value;
                            held_complete(i, order, 0); // the renderer has written it (a no-op after a timeout)
                            held_free(i, order);
                        }];
}

// Drops the frames held longer than kHeldTimeoutS: the presenter (or the GPU) is stuck.
static void held_timeout(void) {
    HeldDrop drops[kHeldTextures];
    int n = 0;
    pthread_mutex_lock(&g_held.mu);
    atomic_store(&g_held.timer_armed, false);
    const double now = held_now();
    double next = 0;
    for (int k = 0; k < kHeldTextures; k++) {
        HeldFrame *h = &g_held.f[k];
        if (h->state != HELD_QUEUED) continue;
        if (now - h->since >= kHeldTimeoutS) drops[n++] = held_drop_locked(k, true, "the display did not take it within 1 s");
        else if (!next || h->since < next) next = h->since;
    }
    if (next && !atomic_exchange(&g_held.timer_armed, true)) {
        const double left = next + kHeldTimeoutS - now + 0.005;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(left * 1e9)), g_held.q, ^{
            held_timeout();
        });
    }
    pthread_mutex_unlock(&g_held.mu);
    for (int k = 0; k < n; k++) held_finish_drop(drops[k]);
}

// Presenter thread, under g_pr.q.mu: the oldest held frame goes to the screen, unless a slot
// frame (older: it was acquired before any frame was held) is still being rendered or queued.
static int held_take(void) {
    pthread_mutex_lock(&g_held.mu);
    int i = held_oldest_locked(HELD_QUEUED);
    if (i >= 0 && !ps4_pq_claim_drawable_locked(&g_pr.q)) i = -1;
    if (i >= 0) g_held.f[i].state = HELD_PRESENTING;
    pthread_mutex_unlock(&g_held.mu);
    return i;
}

static void present_held(int i, id<CAMetalDrawable> drawable) {
    HeldFrame *h = &g_held.f[i]; // PRESENTING: only this thread changes it now
    pthread_mutex_lock(&g_held.mu);
    id<MTLTexture> tex = h->tex;
    const uint64_t ready = h->ready, order = h->order;
    const long shot = h->shot_idx;
    const double waited = held_now() - h->since;
    pthread_mutex_unlock(&g_held.mu);
    id<MTLCommandBuffer> cb = [g_pr.queue commandBuffer];
    [cb encodeWaitForEvent:g_held.ev value:ready]; // the renderer has written the frame
    const double min_duration =
        ps4_present_min_duration(g_pr.mode, atomic_load(&g_guest_flip_rate), atomic_load(&g_guest_flip_mode),
                                 (double)atomic_load(&g_pr.display_hz));
    present_encode(cb, tex, shot, min_duration, drawable);
    [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
        (void)c;
        held_complete(i, order, 1); // the flip completes once its image is in the drawable
        held_free(i, order);
    }];
    [cb commit];
    ps4_present_stats_on_backpressure(pstats(), waited * 1000.0);
    presented_one();
}

static void presenter_wake(void) {
    pthread_mutex_lock(&g_pr.q.mu);
    pthread_cond_broadcast(&g_pr.q.cv);
    pthread_mutex_unlock(&g_pr.q.mu);
}

bool ps4_metal_screen_frames_held(void) {
    pthread_mutex_lock(&g_held.mu);
    const bool held = held_oldest_locked(HELD_QUEUED) >= 0;
    pthread_mutex_unlock(&g_held.mu);
    return held;
}

void ps4_metal_screen_set_hold_depth(int display_buffers) {
    const int d = display_buffers - 1 < 1 ? 1 : display_buffers - 1 > kHeldMax ? kHeldMax : display_buffers - 1;
    if (atomic_exchange(&g_hold_depth, d) != d)
        fprintf(stderr, "[metal-screen] up to %d frames held for the display (%d display buffers)\n", d, display_buffers);
}

void *ps4_metal_screen_hold_texture(int width, int height) {
    if (width <= 0 || height <= 0 || !g_inited) return NULL;
    presenter_start();
    HeldDrop drop = {-1, 0, 0, false};
    pthread_mutex_lock(&g_held.mu);
    int queued = 0;
    for (int k = 0; k < kHeldTextures; k++) queued += g_held.f[k].state == HELD_QUEUED;
    if (queued >= atomic_load(&g_hold_depth)) {
        // the guest flips faster than the display takes frames and does not wait for its flips
        drop = held_drop_locked(held_oldest_locked(HELD_QUEUED), false, "more frames flipped than the display can take");
    }
    const int i = held_oldest_locked(HELD_FREE);
    void *tex = NULL;
    if (i >= 0) {
        HeldFrame *h = &g_held.f[i];
        if (!h->tex || h->tex.width != (NSUInteger)width || h->tex.height != (NSUInteger)height) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                          width:(NSUInteger)width
                                                                                         height:(NSUInteger)height
                                                                                      mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            td.storageMode = MTLStorageModePrivate;
            h->tex = hooked_texture(td, h->tex);
        }
        if (h->tex) {
            h->state = HELD_RENDERING;
            tex = (__bridge void *)h->tex;
        }
    }
    pthread_mutex_unlock(&g_held.mu);
    if (drop.i >= 0) held_finish_drop(drop);
    return tex;
}

int ps4_metal_screen_held_begin(void *texture, ps4_metal_screen_held_done_fn done, void *arg,
                                ps4_metal_screen_present_op *op) {
    id<MTLTexture> tex = (__bridge id<MTLTexture>)texture;
    if (!tex || !op || !g_held.ev) return -1;
    pthread_mutex_lock(&g_held.mu);
    int i = -1;
    for (int k = 0; k < kHeldTextures; k++)
        if (g_held.f[k].state == HELD_RENDERING && g_held.f[k].tex == tex) i = k;
    if (i < 0) {
        pthread_mutex_unlock(&g_held.mu);
        return -1;
    }
    const uint64_t v = atomic_fetch_add(&g_held.ev_value, 1) + 1;
    op->event = (__bridge void *)g_held.ev;
    op->value = v;
    op->copy_dst = NULL;
    op->slot = i;
    HeldFrame *h = &g_held.f[i];
    h->state = HELD_QUEUED;
    h->ready = v;
    h->order = ++g_held.next_order;
    h->shot_idx = screenshot_due();
    h->since = held_now();
    h->completed = false;
    h->done = done;
    h->arg = arg;
    ps4_present_stats_on_held(pstats());
    const bool arm = !atomic_exchange(&g_held.timer_armed, true);
    pthread_mutex_unlock(&g_held.mu);
    if (arm)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((kHeldTimeoutS + 0.005) * 1e9)), g_held.q, ^{
            held_timeout();
        });
    presenter_wake();
    return 0;
}

int ps4_metal_screen_queue_held(void *command_buffer, void *texture, ps4_metal_screen_held_done_fn done, void *arg) {
    id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)command_buffer;
    if (!cb) return -1;
    ps4_metal_screen_present_op op;
    if (ps4_metal_screen_held_begin(texture, done, arg, &op) != 0) return -1;
    [cb encodeSignalEvent:(__bridge id<MTLSharedEvent>)op.event value:op.value];
    const ps4_metal_screen_present_op o = op;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
        if (c.status == MTLCommandBufferStatusError) ps4_metal_screen_present_failed(&o);
    }];
    return 0;
}

void ps4_metal_screen_note_held_drop(void) {
    ps4_present_stats_on_held_dropped(pstats());
    screenshot_skip(); // keeps the per-flip numbering
}

void ps4_metal_screen_release_held(void) {
    if (!g_held.ev) return; // the presenter never started: nothing waits
    typedef struct {
        ps4_metal_screen_held_done_fn done;
        void *arg;
    } Done;
    Done dones[kHeldTextures];
    HeldDrop drops[kHeldTextures];
    int nd = 0, nq = 0;
    pthread_mutex_lock(&g_held.mu);
    for (int k = 0; k < kHeldTextures; k++) {
        HeldFrame *h = &g_held.f[k];
        if (h->state == HELD_QUEUED) {
            drops[nq++] = held_drop_locked(k, false, "the renderer's GPU work was given up");
        }
        if (h->state == HELD_PRESENTING || h->state == HELD_DROPPING) {
            void *arg = NULL;
            ps4_metal_screen_held_done_fn done = held_claim_locked(k, h->order, &arg);
            if (done) dones[nd++] = (Done){done, arg};
        }
    }
    const uint64_t v = atomic_load(&g_held.ev_value);
    pthread_mutex_unlock(&g_held.mu);
    // the renderer's command buffers that would signal these values may never complete
    if (g_held.ev.signaledValue < v) g_held.ev.signaledValue = v;
    // the same for slot frames: a presenter command buffer waiting for a hung slot frame (and every
    // one queued behind it) would keep its drawable for good, and no later frame could be shown
    if (g_pr.ready) {
        const uint64_t r = atomic_load(&g_pr.ready_value);
        if (g_pr.ready.signaledValue < r) g_pr.ready.signaledValue = r;
    }
    fprintf(stderr, "[metal-screen] renderer GPU work given up: %d held frames' flips completed\n", nd);
    for (int k = 0; k < nd; k++) dones[k].done(dones[k].arg, 0); // in order through the renderer's flip sequence
    for (int k = 0; k < nq; k++) held_finish_drop(drops[k]);
}

uint64_t ps4_metal_screen_held_drop_count(void) {
    pthread_mutex_lock(&pstats()->mu);
    const uint64_t n = pstats()->total_held_dropped;
    pthread_mutex_unlock(&pstats()->mu);
    return n;
}

bool ps4_metal_screen_slot_ready(void) {
    if (!atomic_load(&g_pr.started)) return false;
    pthread_mutex_lock(&g_pr.q.mu);
    const bool ready = ps4_pq_can_acquire_locked(&g_pr.q);
    pthread_mutex_unlock(&g_pr.q.mu);
    return ready;
}

bool ps4_metal_screen_presenter_idle(void) {
    if (!atomic_load(&g_pr.started)) return true;
    pthread_mutex_lock(&g_pr.q.mu);
    const bool slots = ps4_pq_idle_locked(&g_pr.q);
    pthread_mutex_unlock(&g_pr.q.mu);
    pthread_mutex_lock(&g_held.mu);
    const bool held = held_oldest_locked(HELD_QUEUED) < 0 && held_oldest_locked(HELD_PRESENTING) < 0;
    pthread_mutex_unlock(&g_held.mu);
    return slots && held;
}

uint64_t ps4_metal_screen_held_count(void) {
    pthread_mutex_lock(&pstats()->mu);
    const uint64_t n = pstats()->total_held;
    pthread_mutex_unlock(&pstats()->mu);
    return n;
}

static void metal_screen_pump_events_main(void);

void ps4_metal_screen_pump_events(void) {
    if ([NSThread isMainThread]) {
        metal_screen_pump_events_main();
        return;
    }
    // Do not block the presenting (guest) thread on the main thread; one pending pump is enough.
    static atomic_bool queued;
    if (atomic_exchange(&queued, true)) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        atomic_store(&queued, false);
        metal_screen_pump_events_main();
    });
}

static void metal_screen_pump_events_main(void) {
    @autoreleasepool {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            if (event.type == NSEventTypeKeyDown) {
                // Cmd+Q quits; Escape is the pad's Options button (pause menu).
                if ((event.modifierFlags & NSEventModifierFlagCommand) && event.keyCode == 12) {
                    ps4_host_quit(0);
                }
                extern void ps4_pad_handle_key(unsigned short keyCode, int isDown);
                ps4_pad_handle_key(event.keyCode, 1);
                extern void ps4_keyboard_handle_key(unsigned short keyCode, int isDown, NSUInteger modifierFlags);
                ps4_keyboard_handle_key(event.keyCode, 1, event.modifierFlags);

                if (event.modifierFlags & NSEventModifierFlagCommand) {
                    [NSApp sendEvent:event];
                }
                continue;
            } else if (event.type == NSEventTypeKeyUp) {
                extern void ps4_pad_handle_key(unsigned short keyCode, int isDown);
                ps4_pad_handle_key(event.keyCode, 0);
                extern void ps4_keyboard_handle_key(unsigned short keyCode, int isDown, NSUInteger modifierFlags);
                ps4_keyboard_handle_key(event.keyCode, 0, event.modifierFlags);

                if (event.modifierFlags & NSEventModifierFlagCommand) {
                    [NSApp sendEvent:event];
                }
                continue;
            } else if (event.type == NSEventTypeFlagsChanged) {
                extern void ps4_keyboard_handle_flags(NSUInteger modifierFlags);
                ps4_keyboard_handle_flags(event.modifierFlags);
            }
            [NSApp sendEvent:event];
            [NSApp updateWindows];
        }

        // Failsafe: if window was closed or hidden and not minimized, exit cleanly
        if (g_inited && g_window && ![g_window isVisible] && ![g_window isMiniaturized]) {
            ps4_host_quit(0);
        }
    }
}

void *ps4_metal_screen_get_device(void) {
    if (!g_inited) ps4_metal_screen_init(1920, 1080, "PS4Recomp - Native Metal Display");
    return (__bridge void *)g_device;
}

void *ps4_metal_screen_get_command_queue(void) {
    if (!g_inited) ps4_metal_screen_init(1920, 1080, "PS4Recomp - Native Metal Display");
    return (__bridge void *)g_commandQueue;
}

void *ps4_metal_screen_get_layer(void) {
    if (!g_inited) ps4_metal_screen_init(1920, 1080, "PS4Recomp - Native Metal Display");
    return (__bridge void *)g_metalLayer;
}

void *ps4_metal_screen_get_texture(void) {
    if (!g_inited) ps4_metal_screen_init(1920, 1080, "PS4Recomp - Native Metal Display");
    return (__bridge void *)g_screenTexture;
}

// No caller today (the process ends with _exit). The presenter thread is stopped, not joined:
// a caller that tears down the device while frames are in flight must drain first.
void ps4_metal_screen_destroy(void) {
    if (atomic_load(&g_pr.started)) {
        presenter_drain();
        ps4_pq_stop(&g_pr.q);
    }
    @autoreleasepool {
        if (g_window) {
            NSWindow *w = g_window;
            g_window = nil;
            run_on_main(^{
                [w close];
            });
        }
        g_screenTexture = nil;
        g_commandQueue = nil;
        g_metalLayer = nil;
        g_device = nil;
        g_inited = NO;
    }
}

// Debug hooks for exit tests (PS4_PAD_CTL "close" / "terminate"): the same paths as a click on
// the window's close button and Cmd+Q / the app menu's Quit.
void ps4_metal_screen_request_close(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_window) [g_window performClose:nil];
    });
}

void ps4_metal_screen_request_terminate(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
    });
}
