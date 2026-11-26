// SPDX-License-Identifier: GPL-2.0-or-later

// The NpCommerce windows, laid out after shadPS4's ImGui layers (np_commerce_ui.cpp):
//  * the store dialog: a non-modal AppKit panel over the game window with the mode's title and
//    description, the user, the service label or Plus feature, the target labels (CHECKOUT and
//    DOWNLOADLIST split "service:product-sku" labels), the mode's action button and Close. The
//    game keeps running and polls sceNpCommerceDialogUpdateStatus while it is up.
//  * the store icon: a 52x52 rounded badge near the bottom edge of the game window (left, center
//    or right, inset by the layout's safe-area scale), drawn by a borderless child window that
//    ignores the mouse and follows the game window.

#import <Cocoa/Cocoa.h>
#include "ps4_np_commerce.h"
#include "ps4_metal_screen.h"
#include <stdlib.h>

static NSString *mode_title(int32_t mode) {
    switch (mode) {
    case NP_COMMERCE_MODE_PRODUCT: return @"Product";
    case NP_COMMERCE_MODE_PRODUCT_CODE: return @"Redeem Code";
    case NP_COMMERCE_MODE_CHECKOUT: return @"Purchase Confirmation";
    case NP_COMMERCE_MODE_DOWNLOADLIST: return @"My Downloads";
    case NP_COMMERCE_MODE_PLUS: return @"Plus Membership";
    default: return @"Store";
    }
}

// nil: the mode has no action (DOWNLOADLIST), only Close.
static NSString *mode_action(int32_t mode) {
    switch (mode) {
    case NP_COMMERCE_MODE_CATEGORY:
    case NP_COMMERCE_MODE_PRODUCT:
    case NP_COMMERCE_MODE_CHECKOUT: return @"Simulate purchase";
    case NP_COMMERCE_MODE_PRODUCT_CODE: return @"Redeem code";
    case NP_COMMERCE_MODE_PLUS: return @"Join Plus";
    default: return nil;
    }
}

static NSString *mode_empty_text(int32_t mode) {
    switch (mode) {
    case NP_COMMERCE_MODE_CATEGORY: return @"Top store category.";
    case NP_COMMERCE_MODE_PRODUCT_CODE: return @"Enter a promotion code.";
    case NP_COMMERCE_MODE_DOWNLOADLIST: return @"All downloadable content.";
    case NP_COMMERCE_MODE_PLUS: return @"Join Plus.";
    default: return @"No product specified.";
    }
}

static NSString *mode_description(int32_t mode) {
    switch (mode) {
    case NP_COMMERCE_MODE_CATEGORY: return @"Browsing a store category.";
    case NP_COMMERCE_MODE_PRODUCT: return @"Viewing a product.";
    case NP_COMMERCE_MODE_PRODUCT_CODE: return @"Redeeming a promotion code.";
    case NP_COMMERCE_MODE_CHECKOUT: return @"Confirming a purchase.";
    case NP_COMMERCE_MODE_DOWNLOADLIST: return @"Viewing owned downloads.";
    case NP_COMMERCE_MODE_PLUS: return @"Joining Plus.";
    default: return @"";
    }
}

static NSString *describe_features(uint64_t features) {
    if (features == 0) return @"none";
    if (features & 0x1) return @"Realtime Multiplay"; // ORBIS_NP_PLUS_FEATURE_REALTIME_MULTIPLAY
    return @"unknown";
}

static NSTextField *label(NSString *text, NSFont *font) {
    NSTextField *t = [NSTextField wrappingLabelWithString:text];
    t.font = font;
    t.selectable = YES;
    return t;
}

// The lines shown for the targets: "service:product-sku" labels are split in CHECKOUT and
// DOWNLOADLIST modes, other labels are shown as they are.
static NSString *targets_text(const NpCommerceView *v) {
    NSMutableString *out = [NSMutableString string];
    const BOOL checkout_form = v->mode == NP_COMMERCE_MODE_CHECKOUT || v->mode == NP_COMMERCE_MODE_DOWNLOADLIST;
    for (uint32_t i = 0; i < v->targetCount; i++) {
        NSString *t = [NSString stringWithUTF8String:v->targets[i]] ?: @"";
        if (checkout_form) {
            NSString *service = @"", *product = t, *sku = @"";
            NSRange colon = [product rangeOfString:@":"];
            if (colon.location != NSNotFound) {
                service = [product substringToIndex:colon.location];
                product = [product substringFromIndex:colon.location + 1];
            }
            NSRange dash = [product rangeOfString:@"-"];
            if (dash.location != NSNotFound) {
                sku = [product substringFromIndex:dash.location + 1];
                product = [product substringToIndex:dash.location];
            }
            if (sku.length || service.length) {
                [out appendFormat:@"• Product %@\n", product];
                if (sku.length) [out appendFormat:@"    SKU: %@\n", sku];
                if (service.length) [out appendFormat:@"    Service: %@\n", service];
                continue;
            }
        }
        [out appendFormat:@"• %@\n", t];
    }
    if (out.length) [out deleteCharactersInRange:NSMakeRange(out.length - 1, 1)];
    return out;
}

@interface NpCommercePanelController : NSObject <NSWindowDelegate>
@property(nonatomic) NSPanel *panel;
@property(nonatomic) uint32_t serial;
@property(nonatomic) BOOL done;
@end

static NpCommercePanelController *g_controller;

@implementation NpCommercePanelController

- (void)finishWith:(int32_t)result {
    if (self.done) return;
    self.done = YES;
    ps4_np_commerce_finish(self.serial, result);
    [self.panel orderOut:nil];
    if (g_controller == self) g_controller = nil;
}

- (void)action:(id)sender {
    [self finishWith:NP_COMMERCE_RESULT_PURCHASED];
}

- (void)close:(id)sender {
    [self finishWith:NP_COMMERCE_RESULT_USER_CANCELED];
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    [self finishWith:NP_COMMERCE_RESULT_USER_CANCELED];
    return NO;
}

- (void)show:(const NpCommerceView *)v {
    self.serial = v->serial;
    self.panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 380)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    NSString *title = mode_title(v->mode);
    if (v->targetCount == 1 && (v->mode == NP_COMMERCE_MODE_PRODUCT || v->mode == NP_COMMERCE_MODE_CATEGORY)) {
        title = [NSString stringWithFormat:@"%@: %s", title, v->targets[0]];
    }
    self.panel.title = title;
    self.panel.delegate = self;
    self.panel.releasedWhenClosed = NO;
    self.panel.level = NSFloatingWindowLevel;
    self.panel.hidesOnDeactivate = NO;

    NSFont *body = [NSFont systemFontOfSize:13];
    NSMutableArray<NSView *> *rows = [NSMutableArray array];
    [rows addObject:label(mode_description(v->mode), body)];
    [rows addObject:label(v->userName[0] ? [NSString stringWithFormat:@"User: %s (id %d)", v->userName, v->userId]
                                         : [NSString stringWithFormat:@"User id: %d", v->userId],
                          body)];
    if (v->mode != NP_COMMERCE_MODE_PRODUCT_CODE && v->mode != NP_COMMERCE_MODE_PLUS) {
        [rows addObject:label([NSString stringWithFormat:@"Service label: %u", v->serviceLabel], body)];
    }
    if (v->mode == NP_COMMERCE_MODE_PLUS) {
        [rows addObject:label([@"Plus feature: " stringByAppendingString:describe_features(v->features)], body)];
    }
    if (v->targetCount == 0) {
        [rows addObject:label(mode_empty_text(v->mode), body)];
    } else {
        NSTextField *heading = label(v->targetCount == 1 ? @"Target:" : @"Targets:", body);
        heading.textColor = NSColor.secondaryLabelColor;
        [rows addObject:heading];
        [rows addObject:label(targets_text(v), body)];
    }
    NSStackView *info = [NSStackView stackViewWithViews:rows];
    info.orientation = NSUserInterfaceLayoutOrientationVertical;
    info.alignment = NSLayoutAttributeLeading;
    info.spacing = 6;

    NSMutableArray<NSView *> *buttons = [NSMutableArray array];
    NSButton *close = [NSButton buttonWithTitle:@"Close" target:self action:@selector(close:)];
    close.keyEquivalent = @"\033";
    NSString *action = mode_action(v->mode);
    NSButton *first = close;
    if (action) {
        NSButton *act = [NSButton buttonWithTitle:action target:self action:@selector(action:)];
        act.keyEquivalent = @"\r";
        [act.widthAnchor constraintEqualToConstant:160].active = YES;
        [buttons addObject:act];
        first = act;
    }
    [close.widthAnchor constraintEqualToConstant:160].active = YES;
    [buttons addObject:close];
    NSStackView *row = [NSStackView stackViewWithViews:buttons];
    row.spacing = 10;

    NSStackView *root = [NSStackView stackViewWithViews:@[ info, row ]];
    root.orientation = NSUserInterfaceLayoutOrientationVertical;
    root.alignment = NSLayoutAttributeCenterX;
    root.distribution = NSStackViewDistributionEqualSpacing;
    root.edgeInsets = NSEdgeInsetsMake(16, 16, 10, 16);
    root.translatesAutoresizingMaskIntoConstraints = NO;
    [self.panel.contentView addSubview:root];
    [NSLayoutConstraint activateConstraints:@[
        [root.leadingAnchor constraintEqualToAnchor:self.panel.contentView.leadingAnchor],
        [root.trailingAnchor constraintEqualToAnchor:self.panel.contentView.trailingAnchor],
        [root.topAnchor constraintEqualToAnchor:self.panel.contentView.topAnchor],
        [root.bottomAnchor constraintEqualToAnchor:self.panel.contentView.bottomAnchor],
        [info.widthAnchor constraintEqualToConstant:488],
    ]];
    [self.panel center];
    [self.panel makeKeyAndOrderFront:nil];
    [self.panel makeFirstResponder:first];
    [NSApp activateIgnoringOtherApps:YES];
}

@end

bool ps4_np_commerce_ui_show(const NpCommerceView *view) {
    if (getenv("PS4_RECOMP_HEADLESS") || !NSApp) return false;
    NpCommerceView *copy = malloc(sizeof(*copy));
    if (!copy) return false;
    *copy = *view;
    dispatch_async(dispatch_get_main_queue(), ^{
        NpCommercePanelController *old = g_controller; // a window the game left open: close it
        old.done = YES;
        [old.panel orderOut:nil];
        g_controller = [NpCommercePanelController new];
        [g_controller show:copy];
        free(copy);
    });
    return true;
}

void ps4_np_commerce_ui_dismiss(void) {
    if (!NSApp) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        NpCommercePanelController *c = g_controller;
        g_controller = nil;
        c.done = YES; // the game already finished the dialog
        [c.panel orderOut:nil];
    });
}

// --- Store icon ---------------------------------------------------------------------------

static const CGFloat kIconSize = 52, kIconPad = 16;

@interface NpCommerceIconView : NSView
@end

@implementation NpCommerceIconView

- (void)drawRect:(NSRect)dirty {
    NSRect r = NSInsetRect(self.bounds, 1, 1);
    NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:r xRadius:10 yRadius:10];
    [[NSColor colorWithSRGBRed:0 green:55 / 255.0 blue:145 / 255.0 alpha:230 / 255.0] setFill];
    [shape fill];
    shape.lineWidth = 2;
    [[NSColor colorWithWhite:1 alpha:210 / 255.0] setStroke];
    [shape stroke];
    NSImageSymbolConfiguration *cfg = [[NSImageSymbolConfiguration configurationWithPointSize:22
                                                                                       weight:NSFontWeightSemibold]
        configurationByApplyingConfiguration:[NSImageSymbolConfiguration
                                                 configurationWithPaletteColors:@[ NSColor.whiteColor ]]];
    NSImage *bag = [[NSImage imageWithSystemSymbolName:@"bag.fill" accessibilityDescription:@"Store"]
        imageWithSymbolConfiguration:cfg];
    const NSSize s = bag.size;
    [bag drawInRect:NSMakeRect(NSMidX(self.bounds) - s.width / 2, NSMidY(self.bounds) - s.height / 2, s.width,
                               s.height)];
}

@end

static NSPanel *g_icon;
static int32_t g_icon_pos_ui, g_icon_layout_ui;
static id g_icon_observer;

static CGFloat layout_scale(int32_t layout) {
    switch (layout) {
    case NP_COMMERCE_ICON_LAYOUT_FIXED_90: return 0.90;
    case NP_COMMERCE_ICON_LAYOUT_FOLLOW_SAFE_AREA: return 0.95;
    default: return 1.0;
    }
}

// Places the icon over the game window's content, in screen coordinates.
static void icon_place(NSWindow *game) {
    const NSRect content = [game contentRectForFrameRect:game.frame];
    const CGFloat scale = layout_scale(g_icon_layout_ui);
    const CGFloat inset_x = content.size.width * (1 - scale) * 0.5;
    const CGFloat inset_y = content.size.height * (1 - scale) * 0.5;
    CGFloat x;
    switch (g_icon_pos_ui) {
    case NP_COMMERCE_ICON_POS_LEFT: x = inset_x + kIconPad; break;
    case NP_COMMERCE_ICON_POS_RIGHT: x = content.size.width - inset_x - kIconSize - kIconPad; break;
    default: x = (content.size.width - kIconSize) * 0.5; break;
    }
    const CGFloat y = inset_y + kIconPad; // bottom edge (AppKit's origin is bottom-left)
    [g_icon setFrame:NSMakeRect(content.origin.x + x, content.origin.y + y, kIconSize, kIconSize) display:YES];
}

static void icon_hide(void) {
    if (!g_icon) return;
    if (g_icon_observer) [[NSNotificationCenter defaultCenter] removeObserver:g_icon_observer];
    g_icon_observer = nil;
    [g_icon.parentWindow removeChildWindow:g_icon];
    [g_icon orderOut:nil];
    g_icon = nil;
}

static void icon_show(void) {
    NSWindow *game = (__bridge NSWindow *)ps4_metal_screen_get_window();
    if (!game) return; // no game window yet: nothing to draw over
    if (!g_icon) {
        g_icon = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, kIconSize, kIconSize)
                                            styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
        g_icon.opaque = NO;
        g_icon.backgroundColor = NSColor.clearColor;
        g_icon.hasShadow = NO;
        g_icon.ignoresMouseEvents = YES;
        g_icon.releasedWhenClosed = NO;
        g_icon.hidesOnDeactivate = NO;
        g_icon.contentView = [[NpCommerceIconView alloc] initWithFrame:NSMakeRect(0, 0, kIconSize, kIconSize)];
        [game addChildWindow:g_icon ordered:NSWindowAbove];
        g_icon_observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidResizeNotification
                                                                            object:game
                                                                             queue:nil
                                                                        usingBlock:^(NSNotification *n) {
                                                                            if (g_icon) icon_place(game);
                                                                        }];
    }
    icon_place(game);
    [g_icon orderFront:nil];
}

void ps4_np_commerce_ui_icon(bool shown, int32_t pos, int32_t layout) {
    if (getenv("PS4_RECOMP_HEADLESS") || !NSApp) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        g_icon_pos_ui = pos;
        g_icon_layout_ui = layout;
        if (shown) {
            icon_show();
        } else {
            icon_hide();
        }
    });
}
