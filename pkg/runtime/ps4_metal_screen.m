#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "ps4_metal_screen.h"

static NSWindow *g_window = nil;
static CAMetalLayer *g_metalLayer = nil;
static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_commandQueue = nil;
static id<MTLTexture> g_screenTexture = nil;
static int g_width = 1920;
static int g_height = 1080;
static BOOL g_inited = NO;

int ps4_metal_screen_init(int width, int height, const char *title) {
    if (g_inited) return 0;
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;
    g_width = width;
    g_height = height;

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

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

        NSView *view = [[NSView alloc] initWithFrame:frame];
        [view setWantsLayer:YES];

        g_metalLayer = [CAMetalLayer layer];
        g_metalLayer.device = g_device;
        g_metalLayer.pixelFormat = MTLPixelFormatRGBA8Unorm;
        g_metalLayer.framebufferOnly = NO;
        g_metalLayer.drawableSize = CGSizeMake(g_width, g_height);

        [view setLayer:g_metalLayer];
        [g_window setContentView:view];
        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        // Create the backing 2D screen texture
        MTLTextureDescriptor *texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
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

int ps4_metal_screen_present_frame(const void *pixels, size_t pitch, int width, int height) {
    if (!g_inited) {
        if (ps4_metal_screen_init(width, height, "PS4Recomp - Native Metal Display") != 0) {
            return -1;
        }
    }

    @autoreleasepool {
        if (pixels && g_screenTexture) {
            MTLRegion region = MTLRegionMake2D(0, 0, width > 0 ? width : g_width, height > 0 ? height : g_height);
            [g_screenTexture replaceRegion:region
                               mipmapLevel:0
                                 withBytes:pixels
                               bytesPerRow:pitch > 0 ? pitch : (g_width * 4)];
        }

        id<CAMetalDrawable> drawable = [g_metalLayer nextDrawable];
        if (drawable) {
            id<MTLCommandBuffer> commandBuffer = [g_commandQueue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
            [blit copyFromTexture:g_screenTexture
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(g_width, g_height, 1)
                        toTexture:drawable.texture
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];

            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
            [commandBuffer waitUntilScheduled];
        }
    }

    ps4_metal_screen_pump_events();
    return 0;
}

void ps4_metal_screen_pump_events(void) {
    @autoreleasepool {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
    }
}

void ps4_metal_screen_destroy(void) {
    @autoreleasepool {
        if (g_window) {
            [g_window close];
            g_window = nil;
        }
        g_screenTexture = nil;
        g_commandQueue = nil;
        g_metalLayer = nil;
        g_device = nil;
        g_inited = NO;
    }
}
