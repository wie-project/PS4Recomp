// SPDX-License-Identifier: GPL-2.0-or-later

// The InvitationDialog window: a non-modal AppKit panel over the game window, laid out like
// shadPS4's ImGui dialog (invitation_dialog_ui.cpp). The game keeps running and polls
// sceInvitationDialogUpdateStatus while it is up.
//
// SEND mode shows the session and the message with Send and Cancel. With a player-editable
// recipient list (USERENABLE) the friend picker is empty, since the friend list comes from a
// signed-in NP client, so Send stays disabled as in shadPS4 without a selection. RECV mode lists
// the pending invitations, of which there are none for the same reason, so Accept and Decline
// stay disabled and only Cancel finishes the dialog.

#import <Cocoa/Cocoa.h>
#include "ps4_invitation_dialog.h"
#include <stdlib.h>

@interface InvitationDialogPanelController : NSObject <NSWindowDelegate>
@property(nonatomic) NSPanel *panel;
@property(nonatomic) uint32_t serial;
@property(nonatomic) BOOL done;
@end

static InvitationDialogPanelController *g_controller;

static NSTextField *wrapping_label(NSString *text, NSColor *color) {
    NSTextField *label = [NSTextField wrappingLabelWithString:text];
    label.textColor = color;
    [label.widthAnchor constraintEqualToConstant:460].active = YES;
    return label;
}

@implementation InvitationDialogPanelController

- (void)close {
    self.done = YES;
    [self.panel orderOut:nil];
    if (g_controller == self) g_controller = nil;
}

- (void)send:(id)sender {
    if (self.done) return;
    [self close];
    ps4_invitation_dialog_send(self.serial);
}

- (void)cancel:(id)sender {
    if (self.done) return;
    [self close];
    ps4_invitation_dialog_finish(self.serial, INVITATION_DIALOG_RESULT_USER_CANCELED);
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    [self cancel:sender];
    return NO;
}

- (NSButton *)button:(NSString *)title action:(SEL)action enabled:(BOOL)enabled {
    NSButton *b = [NSButton buttonWithTitle:title target:self action:action];
    b.enabled = enabled;
    [b.widthAnchor constraintEqualToConstant:130].active = YES;
    return b;
}

- (void)show:(const InvitationDialogView *)view {
    const BOOL send_mode = view->mode == INVITATION_DIALOG_MODE_SEND;
    self.panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 500, 320)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    self.panel.title = @"Invitation Dialog";
    self.panel.delegate = self;
    self.panel.releasedWhenClosed = NO;
    self.panel.level = NSFloatingWindowLevel;
    self.panel.hidesOnDeactivate = NO;

    NSMutableArray<NSView *> *body = [NSMutableArray array];
    NSTextField *heading = [NSTextField labelWithString:send_mode ? @"Send Invitation" : @"Invitations"];
    heading.font = [NSFont boldSystemFontOfSize:17];
    [body addObject:heading];
    NSBox *separator = [NSBox new];
    separator.boxType = NSBoxSeparator;
    [separator.widthAnchor constraintEqualToConstant:460].active = YES;
    [body addObject:separator];

    NSColor *primary = NSColor.labelColor;
    NSColor *disabled = NSColor.disabledControlTextColor;
    NSArray<NSButton *> *buttons;
    NSButton *focus;
    if (send_mode) {
        if (view->sessionId[0]) {
            NSString *sid = [NSString stringWithUTF8String:view->sessionId] ?: @"";
            [body addObject:wrapping_label([@"Session: " stringByAppendingString:sid], primary)];
        }
        if (view->userEditable) {
            [body addObject:wrapping_label(@"Select friends to invite:", primary)];
            [body addObject:wrapping_label(@"No friends available. Add friends from the friends overlay.", disabled)];
        }
        if (view->message[0]) {
            NSString *msg = [NSString stringWithUTF8String:view->message] ?: @"";
            [body addObject:wrapping_label([@"Message: " stringByAppendingString:msg], primary)];
        }
        // Send needs a recipient: with the picker, one the player selected (none can be).
        NSButton *send = [self button:@"Send" action:@selector(send:) enabled:!view->userEditable];
        NSButton *cancel = [self button:@"Cancel" action:@selector(cancel:) enabled:YES];
        buttons = @[ send, cancel ];
        focus = view->userEditable ? cancel : send;
    } else {
        [body addObject:wrapping_label(@"You have no pending invitations.", primary)];
        NSButton *accept = [self button:@"Accept" action:nil enabled:NO];
        NSButton *decline = [self button:@"Decline" action:nil enabled:NO];
        NSButton *cancel = [self button:@"Cancel" action:@selector(cancel:) enabled:YES];
        buttons = @[ accept, decline, cancel ];
        focus = cancel;
    }
    focus.keyEquivalent = @"\r";

    NSStackView *top = [NSStackView stackViewWithViews:body];
    top.orientation = NSUserInterfaceLayoutOrientationVertical;
    top.alignment = NSLayoutAttributeLeading;
    top.spacing = 8;
    NSStackView *row = [NSStackView stackViewWithViews:buttons];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.spacing = 10;

    NSView *content = self.panel.contentView;
    top.translatesAutoresizingMaskIntoConstraints = NO;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:top];
    [content addSubview:row];
    [NSLayoutConstraint activateConstraints:@[
        [top.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:20],
        [top.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-20],
        [top.topAnchor constraintEqualToAnchor:content.topAnchor constant:16],
        [row.centerXAnchor constraintEqualToAnchor:content.centerXAnchor],
        [row.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-10],
    ]];
    [self.panel center];
    [self.panel makeKeyAndOrderFront:nil];
    [self.panel makeFirstResponder:focus];
    [NSApp activateIgnoringOtherApps:YES];
}

@end

bool ps4_invitation_dialog_ui_show(const InvitationDialogView *view) {
    if (getenv("PS4_RECOMP_HEADLESS") || !NSApp) return false;
    InvitationDialogView *copy = malloc(sizeof(*copy));
    if (!copy) return false;
    *copy = *view;
    dispatch_async(dispatch_get_main_queue(), ^{
        InvitationDialogPanelController *old = g_controller; // a window the game left open: close it
        old.done = YES;
        [old.panel orderOut:nil];
        g_controller = [InvitationDialogPanelController new];
        g_controller.serial = copy->serial;
        [g_controller show:copy];
        free(copy);
    });
    return true;
}

void ps4_invitation_dialog_ui_dismiss(void) {
    if (!NSApp) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        InvitationDialogPanelController *c = g_controller;
        g_controller = nil;
        c.done = YES; // the game already finished the dialog
        [c.panel orderOut:nil];
    });
}
