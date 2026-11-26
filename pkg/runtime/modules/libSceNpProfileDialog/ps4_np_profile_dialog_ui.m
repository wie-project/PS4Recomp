// SPDX-License-Identifier: GPL-2.0-or-later

// The NpProfileDialog window: a non-modal AppKit panel over the game window, laid out like
// shadPS4's ImGui card (np_profile_dialog_ui.cpp): the mode title, a placeholder avatar, the
// target, the mode subtitle, the profile message and Close. The game keeps running and polls
// sceNpProfileDialogUpdateStatus while it is up.
//
// The profile card cannot load without an NP server sign-in (ps4_np_profile_dialog.c), so the
// window shows shadPS4's load-failure message and no mode action.

#import <Cocoa/Cocoa.h>
#include "ps4_np_profile_dialog.h"
#include <stdlib.h>

@interface NpProfileDialogPanelController : NSObject <NSWindowDelegate>
@property(nonatomic) NSPanel *panel;
@property(nonatomic) uint32_t serial;
@property(nonatomic) BOOL done;
@end

static NpProfileDialogPanelController *g_controller;

static NSString *mode_title(uint32_t mode) {
    switch (mode) {
    case NP_PROFILE_DIALOG_MODE_FRIEND_REQUEST:
        return @"Friend Request";
    case NP_PROFILE_DIALOG_MODE_ADD_TO_BLOCK_LIST:
        return @"Block List";
    case NP_PROFILE_DIALOG_MODE_GRIEF_REPORT:
        return @"Report";
    default:
        return @"Profile";
    }
}

static NSString *mode_subtitle(uint32_t mode) {
    switch (mode) {
    case NP_PROFILE_DIALOG_MODE_FRIEND_REQUEST:
        return @"Send Friend Request";
    case NP_PROFILE_DIALOG_MODE_ADD_TO_BLOCK_LIST:
        return @"Add to Block List";
    case NP_PROFILE_DIALOG_MODE_GRIEF_REPORT:
        return @"Report Player";
    default:
        return @"Player Profile";
    }
}

@implementation NpProfileDialogPanelController

- (void)closeDialog:(id)sender {
    if (self.done) return;
    self.done = YES;
    [self.panel orderOut:nil];
    if (g_controller == self) g_controller = nil;
    ps4_np_profile_dialog_finish(self.serial, NP_PROFILE_DIALOG_RESULT_USER_CANCELED);
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    [self closeDialog:sender];
    return NO;
}

- (void)show:(const NpProfileDialogView *)view {
    self.panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 480, 350)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    self.panel.title = mode_title(view->mode);
    self.panel.delegate = self;
    self.panel.releasedWhenClosed = NO;
    self.panel.level = NSFloatingWindowLevel;
    self.panel.hidesOnDeactivate = NO;

    NSImage *person = [NSImage imageWithSystemSymbolName:@"person.crop.circle.fill" accessibilityDescription:@"Avatar"];
    NSImageView *avatar = [NSImageView imageViewWithImage:person];
    avatar.symbolConfiguration = [NSImageSymbolConfiguration configurationWithPointSize:80 weight:NSFontWeightRegular];
    avatar.contentTintColor = NSColor.secondaryLabelColor;

    NSTextField *target = [NSTextField labelWithString:[NSString stringWithUTF8String:view->target] ?: @""];
    target.font = [NSFont boldSystemFontOfSize:16];
    target.selectable = YES;
    NSTextField *subtitle = [NSTextField labelWithString:mode_subtitle(view->mode)];
    subtitle.textColor = NSColor.secondaryLabelColor;
    NSTextField *message = [NSTextField labelWithString:@"This player's profile could not be loaded."];
    message.textColor = NSColor.systemRedColor;

    NSButton *close = [NSButton buttonWithTitle:@"Close" target:self action:@selector(closeDialog:)];
    close.keyEquivalent = @"\r";
    [close.widthAnchor constraintEqualToConstant:110].active = YES;

    NSStackView *card = [NSStackView stackViewWithViews:@[ avatar, target, subtitle ]];
    card.orientation = NSUserInterfaceLayoutOrientationVertical;
    card.alignment = NSLayoutAttributeCenterX;
    card.spacing = 6;

    NSView *content = self.panel.contentView;
    for (NSView *v in @[ card, message, close ]) {
        v.translatesAutoresizingMaskIntoConstraints = NO;
        [content addSubview:v];
    }
    [NSLayoutConstraint activateConstraints:@[
        [card.centerXAnchor constraintEqualToAnchor:content.centerXAnchor],
        [card.topAnchor constraintEqualToAnchor:content.topAnchor constant:20],
        [message.centerXAnchor constraintEqualToAnchor:content.centerXAnchor],
        [message.bottomAnchor constraintEqualToAnchor:close.topAnchor constant:-20],
        [close.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-16],
        [close.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12],
    ]];
    [self.panel center];
    [self.panel makeKeyAndOrderFront:nil];
    [self.panel makeFirstResponder:close];
    [NSApp activateIgnoringOtherApps:YES];
}

@end

bool ps4_np_profile_dialog_ui_show(const NpProfileDialogView *view) {
    if (getenv("PS4_RECOMP_HEADLESS") || !NSApp) return false;
    NpProfileDialogView *copy = malloc(sizeof(*copy));
    if (!copy) return false;
    *copy = *view;
    dispatch_async(dispatch_get_main_queue(), ^{
        NpProfileDialogPanelController *old = g_controller; // a window the game left open: close it
        old.done = YES;
        [old.panel orderOut:nil];
        g_controller = [NpProfileDialogPanelController new];
        g_controller.serial = copy->serial;
        [g_controller show:copy];
        free(copy);
    });
    return true;
}

void ps4_np_profile_dialog_ui_dismiss(void) {
    if (!NSApp) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        NpProfileDialogPanelController *c = g_controller;
        g_controller = nil;
        c.done = YES; // the game already finished the dialog
        [c.panel orderOut:nil];
    });
}
