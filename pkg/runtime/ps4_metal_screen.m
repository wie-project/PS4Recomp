#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdlib.h>
#include "ps4_metal_screen.h"

@interface PS4ScreenDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate>
@end

@implementation PS4ScreenDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    exit(0);
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    exit(0);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    exit(0);
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    exit(0);
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

int ps4_metal_screen_init(int width, int height, const char *title) {
    if (g_inited) return 0;
    if (width <= 0) width = 1920;
    if (height <= 0) height = 1080;
    g_width = width;
    g_height = height;

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

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
        g_metalLayer.drawableSize = CGSizeMake(g_width, g_height);

        [g_view setLayer:g_metalLayer];
        [g_window setContentView:g_view];
        [g_window makeFirstResponder:g_view];
        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

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
            if (event.type == NSEventTypeKeyDown) {
                if (event.keyCode == 53) { // Escape key
                    exit(0);
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
            exit(0);
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
