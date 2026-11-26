// SPDX-License-Identifier: GPL-2.0-or-later

// The ErrorDialog window: a non-modal AppKit panel over the game window, laid out like shadPS4's
// ImGui dialog (centered message, one OK button). The game keeps running and polls
// sceErrorDialogUpdateStatus while it is up.

#import <Cocoa/Cocoa.h>
#include "ps4_error_dialog.h"
#include <stdlib.h>

@interface ErrorDialogPanelController : NSObject <NSWindowDelegate>
@property(nonatomic) NSPanel *panel;
@property(nonatomic) uint32_t serial;
@property(nonatomic) BOOL done;
@end

static ErrorDialogPanelController *g_controller;

@implementation ErrorDialogPanelController

- (void)finish {
    if (self.done) return;
    self.done = YES;
    ps4_error_dialog_finish(self.serial);
    [self.panel orderOut:nil];
    if (g_controller == self) g_controller = nil;
}

- (void)ok:(id)sender {
    [self finish];
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    [self finish];
    return NO;
}

- (void)showMessage:(NSString *)message {
    self.panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 500, 300)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    self.panel.title = @"Error";
    self.panel.delegate = self;
    self.panel.releasedWhenClosed = NO;
    self.panel.level = NSFloatingWindowLevel;
    self.panel.hidesOnDeactivate = NO;

    NSTextField *text = [NSTextField wrappingLabelWithString:message];
    text.font = [NSFont systemFontOfSize:17];
    text.alignment = NSTextAlignmentCenter;
    text.selectable = YES;

    NSButton *ok = [NSButton buttonWithTitle:@"OK" target:self action:@selector(ok:)];
    ok.keyEquivalent = @"\r";
    [ok.widthAnchor constraintEqualToConstant:100].active = YES;

    NSStackView *root = [NSStackView stackViewWithViews:@[ text, ok ]];
    root.orientation = NSUserInterfaceLayoutOrientationVertical;
    root.alignment = NSLayoutAttributeCenterX;
    root.distribution = NSStackViewDistributionEqualCentering;
    root.edgeInsets = NSEdgeInsetsMake(20, 20, 10, 20);
    root.translatesAutoresizingMaskIntoConstraints = NO;
    [self.panel.contentView addSubview:root];
    [NSLayoutConstraint activateConstraints:@[
        [root.leadingAnchor constraintEqualToAnchor:self.panel.contentView.leadingAnchor],
        [root.trailingAnchor constraintEqualToAnchor:self.panel.contentView.trailingAnchor],
        [root.topAnchor constraintEqualToAnchor:self.panel.contentView.topAnchor],
        [root.bottomAnchor constraintEqualToAnchor:self.panel.contentView.bottomAnchor],
        [text.widthAnchor constraintLessThanOrEqualToConstant:460],
    ]];
    [self.panel center];
    [self.panel makeKeyAndOrderFront:nil];
    [self.panel makeFirstResponder:ok];
    [NSApp activateIgnoringOtherApps:YES];
}

@end

bool ps4_error_dialog_ui_show(uint32_t serial, const char *message) {
    if (getenv("PS4_RECOMP_HEADLESS") || !NSApp) return false;
    NSString *text = [NSString stringWithUTF8String:message];
    dispatch_async(dispatch_get_main_queue(), ^{
        ErrorDialogPanelController *old = g_controller; // a window the game left open: close it
        old.done = YES;
        [old.panel orderOut:nil];
        g_controller = [ErrorDialogPanelController new];
        g_controller.serial = serial;
        [g_controller showMessage:text];
    });
    return true;
}

void ps4_error_dialog_ui_dismiss(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        ErrorDialogPanelController *c = g_controller;
        g_controller = nil;
        c.done = YES; // the game already finished the dialog
        [c.panel orderOut:nil];
    });
}
