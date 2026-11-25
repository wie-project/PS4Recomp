// SPDX-License-Identifier: GPL-2.0-or-later

// The GameCustomDataDialog window: a non-modal AppKit panel over the game window, laid out like
// shadPS4's ImGui dialog (game_custom_data_dialog_ui.cpp). The game keeps running and polls
// sceGameCustomDataDialogUpdateStatus while it is up. Without an online service the player can
// read the request and edit the message, but sending or listing data reports the missing service.

#import <Cocoa/Cocoa.h>
#include "ps4_game_custom_data_dialog.h"
#include <stdlib.h>

static NSString *const kNoService = @"no online service is available (PlayStation Network is not emulated).";

// "never", "in 2 days", "in 3 hours", "in 45 minutes" (shadPS4 DescribeExpiry).
static NSString *describe_expiry(int32_t minutes) {
    if (minutes <= 0) return @"never";
    const char *unit = "minute";
    int32_t n = minutes;
    if (minutes % (24 * 60) == 0) {
        n = minutes / (24 * 60);
        unit = "day";
    } else if (minutes % 60 == 0) {
        n = minutes / 60;
        unit = "hour";
    }
    return [NSString stringWithFormat:@"in %d %s%s", n, unit, n == 1 ? "" : "s"];
}

static NSString *str(const char *s) {
    NSString *v = s ? [NSString stringWithUTF8String:s] : nil;
    return v ? v : @"";
}

// What the panel shows, copied from the dialog state on the caller's thread.
@interface GcddContent : NSObject
@property(nonatomic) uint32_t serial;
@property(nonatomic) int32_t mode;
@property(nonatomic, copy) NSString *name, *detail, *message, *info, *recipients;
@property(nonatomic) BOOL userEditable;
@property(nonatomic) NSData *thumbnail;
@end
@implementation GcddContent
@end

@interface GcddPanelController : NSObject <NSWindowDelegate, NSTextViewDelegate>
@property(nonatomic) NSPanel *panel;
@property(nonatomic) NSTextField *errorLabel;
@property(nonatomic) BOOL done;
@property(nonatomic) uint32_t serial;
@end

static GcddPanelController *g_controller;

static NSTextField *label(NSString *text, NSFont *font, NSColor *color) {
    NSTextField *f = [NSTextField wrappingLabelWithString:text];
    f.font = font;
    if (color) f.textColor = color;
    f.selectable = NO;
    return f;
}

@implementation GcddPanelController

- (void)finish:(int32_t)result {
    if (self.done) return;
    self.done = YES;
    ps4_gcdd_finish(self.serial, result);
    [self.panel orderOut:nil];
    if (g_controller == self) g_controller = nil;
}

- (void)cancel:(id)sender {
    [self finish:GCDD_RESULT_USER_CANCELED];
}

- (void)send:(id)sender {
    self.errorLabel.stringValue = [@"Couldn't send: " stringByAppendingString:kNoService];
    self.errorLabel.hidden = NO;
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    [self finish:GCDD_RESULT_USER_CANCELED];
    return NO;
}

// The message is limited to GCDD_MAX_USER_MSG_SIZE characters, as on the console.
- (BOOL)textView:(NSTextView *)view shouldChangeTextInRange:(NSRange)range replacementString:(NSString *)text {
    if (!text) return YES;
    const NSUInteger chars = view.string.length - range.length + text.length;
    return chars <= GCDD_MAX_USER_MSG_SIZE;
}

- (NSView *)buildSend:(GcddContent *)c {
    NSStackView *top = [NSStackView stackViewWithViews:@[]];
    top.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    top.alignment = NSLayoutAttributeTop;
    top.spacing = 12;
    NSImage *image = c.thumbnail ? [[NSImage alloc] initWithData:c.thumbnail] : nil;
    if (image) {
        NSImageView *iv = [NSImageView imageViewWithImage:image];
        iv.imageScaling = NSImageScaleProportionallyDown;
        [iv.widthAnchor constraintEqualToConstant:192].active = YES;
        [iv.heightAnchor constraintEqualToConstant:108].active = YES;
        [top addArrangedSubview:iv];
    }
    NSStackView *text = [NSStackView stackViewWithViews:@[
        label(c.name, [NSFont boldSystemFontOfSize:15], nil),
        label(c.detail, [NSFont systemFontOfSize:12], nil),
        label(c.info, [NSFont systemFontOfSize:11], NSColor.secondaryLabelColor),
    ]];
    text.orientation = NSUserInterfaceLayoutOrientationVertical;
    text.alignment = NSLayoutAttributeLeading;
    [top addArrangedSubview:text];

    NSScrollView *scroll = [NSTextView scrollableTextView];
    NSTextView *msg = scroll.documentView;
    msg.string = c.message;
    msg.font = [NSFont systemFontOfSize:12];
    msg.richText = NO;
    msg.delegate = self;
    scroll.borderType = NSBezelBorder;
    [scroll.heightAnchor constraintEqualToConstant:70].active = YES;

    NSStackView *v = [NSStackView stackViewWithViews:@[
        top, label(@"Message", [NSFont boldSystemFontOfSize:12], nil), scroll,
        label(c.recipients, [NSFont systemFontOfSize:12], c.userEditable ? NSColor.secondaryLabelColor : nil),
    ]];
    v.orientation = NSUserInterfaceLayoutOrientationVertical;
    v.alignment = NSLayoutAttributeLeading;
    [scroll.widthAnchor constraintEqualToAnchor:v.widthAnchor].active = YES;
    return v;
}

- (void)showContent:(GcddContent *)c {
    const BOOL receive = c.mode == GCDD_MODE_RECV;
    self.serial = c.serial;
    self.panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 560, receive ? 200 : 380)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    self.panel.title = receive ? @"Received Game Data" : @"Send Game Data";
    self.panel.delegate = self;
    self.panel.releasedWhenClosed = NO;
    self.panel.level = NSFloatingWindowLevel;
    self.panel.hidesOnDeactivate = NO;

    NSView *body;
    if (receive) {
        body = label([@"Couldn't get game data: " stringByAppendingString:kNoService], [NSFont systemFontOfSize:12],
                     NSColor.systemRedColor);
    } else {
        body = [self buildSend:c];
    }
    self.errorLabel = label(@"", [NSFont systemFontOfSize:12], NSColor.systemRedColor);
    self.errorLabel.hidden = YES;

    NSButton *primary = [NSButton buttonWithTitle:receive ? @"Use" : @"Send" target:self action:@selector(send:)];
    primary.keyEquivalent = @"\r";
    // Nothing can be received, and a player-chosen recipient list has no friends to choose from.
    primary.enabled = !receive && !c.userEditable;
    NSButton *cancel = [NSButton buttonWithTitle:receive ? @"Close" : @"Cancel" target:self action:@selector(cancel:)];
    cancel.keyEquivalent = @"\033";
    NSStackView *buttons = [NSStackView stackViewWithViews:@[ primary, cancel ]];
    buttons.spacing = 20;

    NSStackView *root = [NSStackView stackViewWithViews:@[ body, self.errorLabel, buttons ]];
    root.orientation = NSUserInterfaceLayoutOrientationVertical;
    root.alignment = NSLayoutAttributeCenterX;
    root.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
    root.spacing = 10;
    root.translatesAutoresizingMaskIntoConstraints = NO;
    [body.widthAnchor constraintEqualToConstant:528].active = YES;
    [self.panel.contentView addSubview:root];
    [NSLayoutConstraint activateConstraints:@[
        [root.leadingAnchor constraintEqualToAnchor:self.panel.contentView.leadingAnchor],
        [root.trailingAnchor constraintEqualToAnchor:self.panel.contentView.trailingAnchor],
        [root.topAnchor constraintEqualToAnchor:self.panel.contentView.topAnchor],
        [root.bottomAnchor constraintEqualToAnchor:self.panel.contentView.bottomAnchor],
    ]];
    [self.panel center];
    [self.panel makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

@end

static GcddContent *content_of(const GcddView *v) {
    GcddContent *c = [GcddContent new];
    c.serial = v->serial;
    c.mode = v->mode;
    if (v->mode == GCDD_MODE_RECV) return c;
    c.name = str(v->data_name);
    c.detail = str(v->data_detail);
    c.message = str(v->message);
    if (v->thumbnail) c.thumbnail = [NSData dataWithBytes:v->thumbnail length:v->thumbnail_size];

    NSMutableArray<NSString *> *info = [NSMutableArray array];
    if (v->mode == GCDD_MODE_SEND_URL) {
        [info addObject:[NSString stringWithFormat:@"Link: %@%@", str(v->data_url), v->with_item_id ? @" (+ item ID)" : @""]];
    } else {
        [info addObject:[NSString stringWithFormat:@"Attached game data: %u bytes", v->data_size]];
    }
    [info addObject:[@"Expires: " stringByAppendingString:describe_expiry(v->expire_minutes)]];
    NSMutableArray<NSString *> *platforms = [NSMutableArray array];
    for (uint32_t i = 0; i < v->platform_count; i++) [platforms addObject:str(v->platforms[i])];
    if (platforms.count) [info addObject:[@"Platforms: " stringByAppendingString:[platforms componentsJoinedByString:@", "]]];
    c.info = [info componentsJoinedByString:@"\n"];

    c.userEditable = v->user_editable;
    if (v->user_editable) {
        c.recipients = [NSString stringWithFormat:@"To: choose up to %u friend%s. No friends available: %@", v->user_max_count,
                                                  v->user_max_count == 1 ? "" : "s", kNoService];
    } else {
        NSMutableArray<NSString *> *to = [NSMutableArray array];
        for (uint32_t i = 0; i < v->recipient_count; i++) [to addObject:str(v->recipients[i])];
        c.recipients = [@"To: " stringByAppendingString:[to componentsJoinedByString:@", "]];
    }
    return c;
}

bool ps4_gcdd_ui_show(const GcddView *view) {
    if (getenv("PS4_RECOMP_HEADLESS") || !NSApp) return false;
    GcddContent *content = content_of(view);
    dispatch_async(dispatch_get_main_queue(), ^{
        GcddPanelController *old = g_controller; // a panel the game left open: close it
        old.done = YES;
        [old.panel orderOut:nil];
        g_controller = [GcddPanelController new];
        [g_controller showContent:content];
    });
    return true;
}

void ps4_gcdd_ui_dismiss(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        GcddPanelController *c = g_controller;
        g_controller = nil;
        c.done = YES; // the game already finished the dialog
        [c.panel orderOut:nil];
    });
}
