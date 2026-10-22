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
#include "ps4_present_queue.h"
#include "ps4_present_stats.h"
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

        g_view = [[PS4MetalView alloc] initWithFrame:frame];
        [g_view setWantsLayer:YES];

        g_metalLayer = [CAMetalLayer layer];
        g_metalLayer.device = g_device;
        g_metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_metalLayer.framebufferOnly = NO;
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
    char line[512];
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

static long screenshot_due(void) {
    static int init;
    static const char *dir;
    static long every = 600;
    static long frame;
    if (!init) {
        init = 1;
        dir = getenv("PS4_SCREENSHOT_DIR");
        const char *e = getenv("PS4_SCREENSHOT_EVERY");
        if (e && atol(e) > 0) every = atol(e);
    }
    long n = frame++;
    if (dir && g_ps4_screenshot_request) {
        g_ps4_screenshot_request = 0;
        return n;
    }
    if (!dir || n % every != 0) return -1;
    return n;
}

static void save_screenshot(id<MTLTexture> tex, long n) {
    const char *dir = getenv("PS4_SCREENSHOT_DIR");
    if (!dir || !tex || n < 0) return;
    NSUInteger w = tex.width, h = tex.height;
    size_t row = w * 4;
    uint8_t *buf = malloc(row * h);
    if (!buf) return;
    [tex getBytes:buf bytesPerRow:row fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bc = CGBitmapContextCreate(buf, w, h, 8, row, cs,
        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef img = bc ? CGBitmapContextCreateImage(bc) : NULL;
    if (img) {
        NSString *path = [NSString stringWithFormat:@"%s/frame_%06ld.png", dir, n];
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
            (__bridge CFURLRef)[NSURL fileURLWithPath:path], CFSTR("public.png"), 1, NULL);
        if (dst) {
            CGImageDestinationAddImage(dst, img, NULL);
            CGImageDestinationFinalize(dst);
            CFRelease(dst);
            fprintf(stderr, "[metal-screen] saved %s\n", path.UTF8String);
        }
        CGImageRelease(img);
    }
    if (bc) CGContextRelease(bc);
    CGColorSpaceRelease(cs);
    free(buf);
}

static void maybe_save_screenshot(id<MTLTexture> tex) {
    save_screenshot(tex, screenshot_due());
}

static void presenter_drain(void);

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

// Output gamma (sceVideoOutAdjustColor). 1.0 presents the frame as is; other values re-encode
// it like the PS4 video output does: the sRGB-encoded frame is decoded to linear and encoded
// again with the exponent 1 / (3.4 - gamma), so gamma > 1 brightens the image.
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
                                  "// prm: xy = destination origin of the frame, z = gamma, w = 1 if the source\n"
                                  "// view already decodes sRGB\n"
                                  "fragment float4 gamma_fs(VOut in [[stage_in]], texture2d<float> src [[texture(0)]],\n"
                                  "                         constant float4& prm [[buffer(0)]]) {\n"
                                  "    float3 e = src.read(uint2(in.pos.xy - prm.xy)).rgb;\n"
                                  "    float3 lin = prm.w > 0.5 ? e : select(pow((e + 0.055) / 1.055, 2.4), e / 12.92, e <= 0.04045);\n"
                                  "    float k = 1.0 / (3.4 - prm.z);\n"
                                  "    float3 o = select(1.055 * pow(lin, k) - 0.055, 12.92 * lin, lin <= 0.0031308);\n"
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
    if (out_w != src_w || out_h != src_h) {
        snprintf(g_hud.text, sizeof(g_hud.text), "%.0f FPS  %.1f ms  (max %.0f)\n%s %lux%lu -> %lux%lu MetalFX", fps,
                 1000.0 / fps, g_hud.max_ms, mode, (unsigned long)src_w, (unsigned long)src_h, (unsigned long)out_w,
                 (unsigned long)out_h);
    } else {
        snprintf(g_hud.text, sizeof(g_hud.text), "%.0f FPS  %.1f ms  (max %.0f)\n%s %lux%lu", fps, 1000.0 / fps,
                 g_hud.max_ms, mode, (unsigned long)src_w, (unsigned long)src_h);
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
        g_hud.tex_w = font_px * 22;
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
        id<MTLTexture> shot = nil;
        if (shot_idx >= 0) {
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                          width:iw
                                                                                         height:ih
                                                                                      mipmapped:NO];
            td.storageMode = MTLStorageModeShared;
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
        const float gamma = current_gamma();
        id<MTLRenderPipelineState> gamma_pso = fabsf(gamma - 1.0f) > 0.001f ? gamma_pipeline() : nil;
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
            const float prm[4] = {(float)ox, (float)oy, gamma, is_srgb_format(img.pixelFormat) ? 1.0f : 0.0f};
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
            if (shot) [blit copyFromTexture:img toTexture:shot];
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
                save_screenshot(shot, shot_idx);
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
    uint64_t presented; // presenter thread only
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

static bool slot_texture(PresentSlot *ps, NSUInteger w, NSUInteger h) {
    if (ps->tex && ps->tex.width == w && ps->tex.height == h) return true;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    ps->tex = [g_device newTextureWithDescriptor:td];
    return ps->tex != nil;
}

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
    if ((++g_pr.presented % 120) == 0 && !atomic_exchange(&g_pr.hz_query_queued, true)) {
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
            const int s = ps4_pq_take(&g_pr.q);
            if (s < 0) return NULL;
            present_slot(s, drawable);
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

void *ps4_metal_screen_acquire_frame(int width, int height) {
    if (width <= 0 || height <= 0) return NULL;
    if (!g_inited && ps4_metal_screen_init(width, height, "PS4Recomp - Native Metal Display") != 0) return NULL;
    presenter_start();
    int superseded = 0;
    double waited = 0;
    const int s = ps4_pq_acquire(&g_pr.q, 1.0, &superseded, &waited);
    ps4_present_stats_on_backpressure(pstats(), waited * 1000.0);
    if (superseded) ps4_present_stats_on_superseded(pstats());
    if (s < 0) {
        static _Atomic bool logged;
        if (!atomic_exchange(&logged, true))
            fprintf(stderr, "[metal-screen] no present slot became free within 1 s (presenter or GPU stuck)\n");
        return NULL;
    }
    if (!slot_texture(&g_pr.slot[s], (NSUInteger)width, (NSUInteger)height)) {
        ps4_pq_cancel(&g_pr.q, s);
        return NULL;
    }
    return (__bridge void *)g_pr.slot[s].tex;
}

int ps4_metal_screen_encode_present(void *command_buffer, void *texture) {
    id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)command_buffer;
    id<MTLTexture> src = (__bridge id<MTLTexture>)texture;
    if (!cb || !src) return -1;
    if (!g_inited) {
        if (ps4_metal_screen_init((int)src.width, (int)src.height, "PS4Recomp - Native Metal Display") != 0) return -1;
    }
    presenter_start();
    int s = -1;
    pthread_mutex_lock(&g_pr.q.mu);
    for (int i = 0; i < PRESENT_SLOTS; i++)
        if (g_pr.q.state[i] == PS4_PQ_RENDERING && g_pr.slot[i].tex == src) s = i;
    pthread_mutex_unlock(&g_pr.q.mu);
    @autoreleasepool {
        if (s < 0) {
            // not an acquired slot: copy it into a free one behind the caller's work, without waiting
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
                (void)screenshot_due();                    // but keeps the per-flip numbering
                ps4_metal_screen_pump_events();
                return 0;
            }
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromTexture:src toTexture:g_pr.slot[s].tex];
            [blit endEncoding];
        }
        const uint64_t v = atomic_fetch_add(&g_pr.ready_value, 1) + 1;
        [cb encodeSignalEvent:g_pr.ready value:v];
        id<MTLSharedEvent> ev = g_pr.ready;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            // A failed command buffer may not have reached its signal; the presenter must not
            // wait for it forever. Only while v is the newest value handed out: a later command
            // buffer's GPU signal could otherwise be lowered again by this CPU write.
            if (done.status == MTLCommandBufferStatusError && atomic_load(&g_pr.ready_value) == v &&
                ev.signaledValue < v)
                ev.signaledValue = v;
        }];
        g_pr.slot[s].ready = v;
        g_pr.slot[s].shot_idx = screenshot_due();
        ps4_pq_submit(&g_pr.q, s, g_pr.slot[s].shot_idx >= 0);
    }
    ps4_metal_screen_pump_events();
    return 0;
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
