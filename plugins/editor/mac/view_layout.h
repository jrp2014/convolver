// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: the small AppKit helpers that Convolver's window and its setting
// rows (setting_rows.h) both lay out with: the fonts, labels and stack
// views of the usual macOS panel (diagnostic_view.mm explains the layout).
#pragma once
#import <AppKit/AppKit.h>

namespace convolver::view {

inline NSFont* body_font() {
    return [NSFont systemFontOfSize:[NSFont systemFontSize]];
}
inline NSFont* small_font() {
    return [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
}

// `colour` at `alpha`, worked out again for each appearance it is drawn in
// (PLUG-67, jrp's first-pass review of 0.18.27 to 0.18.34). The view is
// made before the host puts it in its window, whose appearance may differ
// from the system's (a dark host window on a light Mac), and the user may
// switch between Light and Dark while it is open. colorWithAlphaComponent:
// on a system colour is not documented to stay dynamic, so the colour is
// resolved in the appearance it is drawn in.
inline NSColor* with_alpha(NSColor* colour, CGFloat alpha) {
    return [NSColor colorWithName:nil
                  dynamicProvider:^NSColor*(NSAppearance* appearance) {
                    __block NSColor* resolved = nil;
                    [appearance performAsCurrentDrawingAppearance:^{
                      resolved = [[colour colorUsingColorSpace:[NSColorSpace sRGBColorSpace]]
                          colorWithAlphaComponent:alpha];
                    }];
                    return resolved ? resolved : [colour colorWithAlphaComponent:alpha];
                  }];
}

// PLUG-52 (jrp's look at the window, 26 November 2026): secondary text
// (captions, labels, notes) a little darker than secondaryLabelColor, which
// measured about 3.9:1 in Light mode against Apple's 4.5:1 for small text;
// still lighter than the values beside it. Follows Light and Dark, as the
// label colour does (with_alpha()). Dimmed controls keep
// secondaryLabelColor.
inline NSColor* secondary_text() {
    return with_alpha([NSColor labelColor], 0.7);
}

// A one-line label the user can select (to copy a value).
inline NSTextField* label(NSFont* font, NSColor* colour) {
    NSTextField* text = [NSTextField labelWithString:@""];
    text.font = font;
    text.textColor = colour;
    text.selectable = YES;
    text.lineBreakMode = NSLineBreakByTruncatingTail;
    text.translatesAutoresizingMaskIntoConstraints = NO;
    return text;
}
// A label that wraps within `wide` points.
inline NSTextField* wrapping(NSFont* font, NSColor* colour, CGFloat wide) {
    NSTextField* text = [NSTextField wrappingLabelWithString:@""];
    text.font = font;
    text.textColor = colour;
    text.selectable = YES;
    text.preferredMaxLayoutWidth = wide;
    text.translatesAutoresizingMaskIntoConstraints = NO;
    [text setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                   forOrientation:NSLayoutConstraintOrientationVertical];
    return text;
}

inline NSStackView* stack(NSArray<NSView*>* views, NSUserInterfaceLayoutOrientation orientation,
                          CGFloat spacing) {
    NSStackView* s = [NSStackView stackViewWithViews:views];
    s.orientation = orientation;
    s.spacing = spacing;
    s.alignment = orientation == NSUserInterfaceLayoutOrientationVertical
                      ? NSLayoutAttributeLeading
                      : NSLayoutAttributeCenterY;
    s.translatesAutoresizingMaskIntoConstraints = NO;
    return s;
}
inline void fixed_width(NSView* view, CGFloat wide) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [view.widthAnchor constraintEqualToConstant:wide].active = YES;
}
// A view that takes the room its row leaves.
inline void stretches(NSView* view) {
    [view setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [view setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                   forOrientation:NSLayoutConstraintOrientationHorizontal];
}

} // namespace convolver::view
