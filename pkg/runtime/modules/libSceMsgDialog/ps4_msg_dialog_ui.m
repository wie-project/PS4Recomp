// SPDX-License-Identifier: GPL-2.0-or-later

// The MsgDialog window: a non-modal AppKit panel over the game window, laid out like shadPS4's
// ImGui dialog (msgdialog_ui.cpp): a centered message, then the buttons (the first one at the
// right) or the progress bar. The game keeps running and polls sceMsgDialogUpdateStatus while
// it is up; the panel has no close box, as the console dialog cannot be dismissed without a
// button.

#import <Cocoa/Cocoa.h>
#include "ps4_msg_dialog.h"
#include <stdlib.h>

// Button labels per ButtonType (shadPS4 user_button_texts); NULL label = no button.
static const struct {
    const char *text1;
    const char *text2;
} k_button_texts[] = {
    {"OK", NULL},       // OK
    {"Yes", "No"},      // YESNO
    {NULL, NULL},       // NONE
    {"OK", "Cancel"},   // OK_CANCEL
    {NULL, NULL},       // 4: undefined
    {"Wait", NULL},     // WAIT
    {"Wait", "Cancel"}, // WAIT_CANCEL
    {"Yes", "No"},      // YESNO_FOCUS_NO
    {"OK", "Cancel"},   // OK_CANCEL_FOCUS_CANCEL
    {NULL, NULL},       // 2BUTTONS: the game's labels
};

// shadPS4 system_message_texts, by SystemMessageType.
static const char *const k_system_texts[] = {
    "No product available in the store.",
    "PSN chat restriction.",
    "User-generated Media restriction",
    NULL,
    "Camera not connected.",
    "Warning: profile picture and name are not set",
};

static const CGFloat kButtonWidth = 100;

@interface MsgDialogPanelController : NSObject
@property(nonatomic) NSPanel *panel;
@property(nonatomic) NSTextField *text;
@property(nonatomic) NSProgressIndicator *bar;
@property(nonatomic) uint32_t serial;
@property(nonatomic) uint32_t mode;
@property(nonatomic) uint32_t type;
@property(nonatomic) BOOL done;
@end

static MsgDialogPanelController *g_controller;

@implementation MsgDialogPanelController

- (void)finishWithButton:(uint32_t)buttonId result:(int32_t)result {
    if (self.done) return;
    self.done = YES;
    ps4_msg_dialog_finish(self.serial, buttonId, result);
    [self.panel orderOut:nil];
    if (g_controller == self) g_controller = nil;
}

// The first (right) button: OK, Yes, Wait, BUTTON1, or OK of a system message.
- (void)pressFirst:(id)sender {
    [self finishWithButton:MSG_DIALOG_BUTTON_ID_BUTTON1 result:MSG_DIALOG_RESULT_OK];
}

// The second (left) button: a cancel button cancels, No and BUTTON2 report themselves.
- (void)pressSecond:(id)sender {
    switch (self.type) {
    case MSG_DIALOG_BUTTON_TYPE_OK_CANCEL:
    case MSG_DIALOG_BUTTON_TYPE_WAIT_CANCEL:
    case MSG_DIALOG_BUTTON_TYPE_OK_CANCEL_FOCUS_CANCEL:
        [self finishWithButton:MSG_DIALOG_BUTTON_ID_INVALID result:MSG_DIALOG_RESULT_USER_CANCELED];
        break;
    default:
        [self finishWithButton:MSG_DIALOG_BUTTON_ID_BUTTON2 result:MSG_DIALOG_RESULT_OK];
        break;
    }
}

// The Cancel button of a PERCENTAGE_CANCEL progress bar.
- (void)cancelProgress:(id)sender {
    [self finishWithButton:MSG_DIALOG_BUTTON_ID_INVALID result:MSG_DIALOG_RESULT_USER_CANCELED];
}

static NSButton *make_button(NSString *title, id target, SEL action) {
    NSButton *b = [NSButton buttonWithTitle:title target:target action:action];
    [b.widthAnchor constraintGreaterThanOrEqualToConstant:kButtonWidth].active = YES;
    return b;
}

static NSString *ns_string(const char *s) {
    NSString *str = s ? [NSString stringWithUTF8String:s] : nil;
    return str ? str : @"";
}

- (void)setProgress:(uint32_t)value {
    self.bar.doubleValue = value > 100 ? 100 : value;
}

- (void)showView:(const MsgDialogView *)v {
    self.serial = v->serial;
    self.mode = v->mode;
    self.type = v->type;

    self.panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 500, 300)
                                            styleMask:NSWindowStyleMaskTitled
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    self.panel.title = @"Message";
    self.panel.releasedWhenClosed = NO;
    self.panel.level = NSFloatingWindowLevel;
    self.panel.hidesOnDeactivate = NO;

    NSString *message = v->mode == MSG_DIALOG_MODE_SYSTEM_MSG ? ns_string(k_system_texts[v->type]) : ns_string(v->msg);
    self.text = [NSTextField wrappingLabelWithString:message];
    self.text.font = [NSFont systemFontOfSize:17];
    self.text.alignment = NSTextAlignmentCenter;
    self.text.selectable = YES;

    NSMutableArray<NSView *> *row = [NSMutableArray array];
    NSButton *focus = nil;
    if (v->mode == MSG_DIALOG_MODE_USER_MSG) {
        NSString *t1 = nil, *t2 = nil;
        if (v->type == MSG_DIALOG_BUTTON_TYPE_2BUTTONS) {
            t1 = ns_string(v->button1);
            t2 = ns_string(v->button2);
        } else {
            if (k_button_texts[v->type].text1) t1 = ns_string(k_button_texts[v->type].text1);
            if (k_button_texts[v->type].text2) t2 = ns_string(k_button_texts[v->type].text2);
        }
        NSButton *b1 = t1 ? make_button(t1, self, @selector(pressFirst:)) : nil;
        NSButton *b2 = t2 ? make_button(t2, self, @selector(pressSecond:)) : nil;
        if (b2) [row addObject:b2]; // the first button sits at the right
        if (b1) [row addObject:b1];
        const BOOL focus_first = v->type < MSG_DIALOG_BUTTON_TYPE_YESNO_FOCUS_NO;
        focus = (focus_first || !b2) ? b1 : b2;
    } else if (v->mode == MSG_DIALOG_MODE_PROGRESS_BAR) {
        self.bar = [[NSProgressIndicator alloc] init];
        self.bar.style = NSProgressIndicatorStyleBar;
        self.bar.indeterminate = NO;
        self.bar.minValue = 0;
        self.bar.maxValue = 100;
        [self.bar.widthAnchor constraintEqualToConstant:400].active = YES;
        [row addObject:self.bar];
        if (v->type == MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE_CANCEL) {
            focus = make_button(@"Cancel", self, @selector(cancelProgress:));
            [row addObject:focus];
        }
    } else {
        focus = make_button(@"OK", self, @selector(pressFirst:));
        [row addObject:focus];
    }
    focus.keyEquivalent = @"\r";

    NSStackView *buttons = [NSStackView stackViewWithViews:row];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    NSStackView *root = [NSStackView stackViewWithViews:@[ self.text, buttons ]];
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
        [self.text.widthAnchor constraintLessThanOrEqualToConstant:460],
    ]];
    [self.panel center];
    [self.panel makeKeyAndOrderFront:nil];
    if (focus) [self.panel makeFirstResponder:focus];
    [NSApp activateIgnoringOtherApps:YES];
}

@end

// Closes a window the game left behind (main queue only).
static void drop_controller(void) {
    MsgDialogPanelController *c = g_controller;
    g_controller = nil;
    c.done = YES;
    [c.panel orderOut:nil];
}

bool ps4_msg_dialog_ui_show(const MsgDialogView *view) {
    if (getenv("PS4_RECOMP_HEADLESS") || !NSApp) return false;
    // The view's strings belong to the library; take copies before leaving this thread.
    MsgDialogView v = *view;
    NSString *msg = view->msg ? [NSString stringWithUTF8String:view->msg] : nil;
    NSString *b1 = view->button1 ? [NSString stringWithUTF8String:view->button1] : nil;
    NSString *b2 = view->button2 ? [NSString stringWithUTF8String:view->button2] : nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        drop_controller();
        MsgDialogView local = v;
        local.msg = msg.UTF8String;
        local.button1 = b1.UTF8String;
        local.button2 = b2.UTF8String;
        g_controller = [MsgDialogPanelController new];
        [g_controller showView:&local];
    });
    return true;
}

void ps4_msg_dialog_ui_progress(uint32_t serial, const char *msg, uint32_t value) {
    if (!NSApp) return;
    NSString *text = ns_string(msg);
    dispatch_async(dispatch_get_main_queue(), ^{
        MsgDialogPanelController *c = g_controller;
        if (!c || c.serial != serial || c.done) return;
        c.text.stringValue = text;
        [c setProgress:value];
    });
}

void ps4_msg_dialog_ui_dismiss(void) {
    if (!NSApp) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        drop_controller();
    });
}
