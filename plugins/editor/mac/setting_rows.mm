// SPDX-License-Identifier: GPL-2.0-or-later
#import "setting_rows.h"
#include "host_log.hpp"
#import "view_layout.h"
#include <algorithm>
#include <cmath>
#include <cstdlib> // Before xlocale.h, which declares strtod_l only after it.
#include <locale.h>
#include <xlocale.h>

using namespace convolver::view;

namespace {
constexpr CGFloat value_field = 56; // A setting's typed value.
} // namespace

// The typed boxes tell the rows when the user first changes their text.
@interface CONVOLVER_SETTING_ROWS () <NSTextFieldDelegate>
@end

@implementation CONVOLVER_SETTING_ROWS {
    NSArray<NSDictionary<NSString*, id>*>* settings_;
    NSArray<NSControl*>* controls_; // Each setting's slider or menu, in settings_ order.
    void (^chosen_)(void);
    NSInteger dragging_; // The slider the user is dragging, or -1.
    NSInteger typed_;    // The box whose text the user has changed, or -1.
    NSInteger gesture_;  // The setting whose edit is open (its begin called), or -1.
    int sampleRate_;     // The host's, for the Latency row; 0 before it starts.
    BOOL latencyWaits_;  // The chosen Latency is not in use yet.
}

- (instancetype)initWithSettings:(NSArray<NSDictionary<NSString*, id>*>*)settings
                          chosen:(void (^)(void))chosen {
    self = [super init];
    if (!self)
        return nil;
    settings_ = [settings copy];
    chosen_ = [chosen copy];
    dragging_ = -1;
    typed_ = -1;
    gesture_ = -1;
    NSMutableArray<NSView*>* rows = [NSMutableArray array];
    NSMutableArray<NSControl*>* controls = [NSMutableArray array];
    NSMutableArray<NSSlider*>* sliders = [NSMutableArray array];
    NSMutableArray<NSPopUpButton*>* menus = [NSMutableArray array];
    NSMutableArray<NSTextField*>* values = [NSMutableArray array];
    for (NSUInteger i = 0; i < settings_.count; ++i) {
        NSDictionary<NSString*, id>* setting = settings_[i];
        NSView* content = nil;
        NSControl* control = nil;
        NSTextField* value = nil;
        NSArray<NSString*>* choices = setting[@"choices"];
        if ([choices isKindOfClass:[NSArray class]]) {
            // A choice from a list (Latency): the item's index is the value.
            NSPopUpButton* menu = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
            [menu addItemsWithTitles:choices];
            menu.target = self;
            menu.action = @selector(choiceMade:);
            menu.translatesAutoresizingMaskIntoConstraints = NO;
            control = menu;
            [menus addObject:menu];
            value = label(body_font(), [NSColor secondaryLabelColor]);
            stretches(value);
            NSStackView* row =
                stack(@[ menu, value ], NSUserInterfaceLayoutOrientationHorizontal, 12);
            row.distribution = NSStackViewDistributionFill;
            content = row;
        } else {
            NSSlider* slider = [NSSlider sliderWithValue:[setting[@"minimum"] doubleValue]
                                                minValue:[setting[@"minimum"] doubleValue]
                                                maxValue:[setting[@"maximum"] doubleValue]
                                                  target:self
                                                  action:@selector(settingChanged:)];
            slider.continuous = YES;
            slider.translatesAutoresizingMaskIntoConstraints = NO;
            stretches(slider);
            control = slider;
            [sliders addObject:slider];
            NSView* track = slider;
            NSString* low = setting[@"low"];
            NSString* high = setting[@"high"];
            if ([low isKindOfClass:[NSString class]] && [high isKindOfClass:[NSString class]]) {
                // Words under the ends (Mix: Dry and Wet, as reverbs label it).
                NSTextField* left =
                    label([NSFont systemFontOfSize:10], [NSColor secondaryLabelColor]);
                NSTextField* right =
                    label([NSFont systemFontOfSize:10], [NSColor secondaryLabelColor]);
                left.stringValue = low;
                right.stringValue = high;
                right.alignment = NSTextAlignmentRight;
                left.selectable = right.selectable = NO;
                NSStackView* ends =
                    stack(@[ left, right ], NSUserInterfaceLayoutOrientationHorizontal, 0);
                ends.distribution = NSStackViewDistributionEqualSpacing;
                NSStackView* column =
                    stack(@[ slider, ends ], NSUserInterfaceLayoutOrientationVertical, 0);
                [NSLayoutConstraint activateConstraints:@[
                    [slider.widthAnchor constraintEqualToAnchor:column.widthAnchor],
                    [ends.widthAnchor constraintEqualToAnchor:column.widthAnchor]
                ]];
                stretches(column);
                track = column;
            }
            // The value, typed: Return or leaving the field sets it.
            value = [NSTextField textFieldWithString:@""];
            value.font = [NSFont monospacedDigitSystemFontOfSize:[NSFont systemFontSize]
                                                          weight:NSFontWeightRegular];
            value.alignment = NSTextAlignmentRight;
            value.target = self;
            value.action = @selector(valueTyped:);
            value.cell.sendsActionOnEndEditing = YES;
            value.delegate = self; // For controlTextDidBeginEditing:.
            value.tag = static_cast<NSInteger>(i);
            value.accessibilityLabel = setting[@"name"];
            fixed_width(value, value_field);
            NSTextField* unit = label(body_font(), [NSColor secondaryLabelColor]);
            unit.stringValue = [setting[@"unit"]
                stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
            unit.selectable = NO;
            fixed_width(unit, 40);
            NSStackView* row =
                stack(@[ track, value, unit ], NSUserInterfaceLayoutOrientationHorizontal, 8);
            row.distribution = NSStackViewDistributionFill;
            content = row;
        }
        control.tag = static_cast<NSInteger>(i);
        control.accessibilityLabel = setting[@"name"];
        if (NSString* tip = setting[@"tip"])
            control.toolTip = tip;
        else if (setting[@"default"])
            control.toolTip = @"Option-click to reset.";
        [rows addObject:content];
        [controls addObject:control];
        [values addObject:value];
    }
    controls_ = controls;
    _rows = rows;
    _sliders = sliders;
    _menus = menus;
    _values = values;
    return self;
}

// A setting's value as the window writes it ("-12", "50"): the same text as
// the report's settings and the log (convolver::setting_text).
- (NSString*)number:(double)value of:(NSDictionary<NSString*, id>*)setting {
    const auto text = convolver::setting_text(value, [setting[@"minimum"] doubleValue],
                                              [setting[@"maximum"] doubleValue]);
    return @(text.c_str());
}

// The time a Latency choice adds at the host's rate ("5.3 ms"), and whether
// it is not in use yet; the item titles begin with the frames ("256 frames").
- (NSString*)latencyNote:(NSPopUpButton*)menu {
    const double frames = menu.titleOfSelectedItem.doubleValue;
    NSString* time = sampleRate_ > 0 && frames > 0
                         ? [NSString stringWithFormat:@"%.1f ms", 1000 * frames / sampleRate_]
                         : @"";
    if (!latencyWaits_)
        return time;
    return time.length ? [time stringByAppendingString:@", not in use yet"] : @"Not in use yet";
}

// Shows `value` for setting `i`: in its typed field (unless the user is
// typing there) and as VoiceOver's value for the slider ("50% wet"). A menu
// shows its choice itself, with the time it adds beside it.
- (void)show:(double)value at:(NSUInteger)i {
    if ([controls_[i] isKindOfClass:[NSPopUpButton class]]) {
        auto* menu = static_cast<NSPopUpButton*>(controls_[i]);
        const auto index = static_cast<NSInteger>(std::lround(value));
        if (index >= 0 && index < menu.numberOfItems && menu.indexOfSelectedItem != index)
            [menu selectItemAtIndex:index];
        _values[i].stringValue = [self latencyNote:menu];
        return;
    }
    NSString* number = [self number:value of:settings_[i]];
    if (!_values[i].currentEditor)
        _values[i].stringValue = number;
    controls_[i].accessibilityValueDescription =
        [number stringByAppendingString:settings_[i][@"unit"]];
}

// Opens an edit of setting `i` (ending any other), and ends the open one.
- (void)beginGesture:(NSUInteger)i {
    if (gesture_ == static_cast<NSInteger>(i))
        return;
    [self endGesture];
    gesture_ = static_cast<NSInteger>(i);
    ConvolverSettingGesture begin = settings_[i][@"begin"];
    if (begin)
        begin();
}

- (void)endGesture {
    if (gesture_ < 0)
        return;
    const auto i = static_cast<NSUInteger>(gesture_);
    gesture_ = -1;
    ConvolverSettingGesture end = settings_[i][@"end"];
    if (end)
        end();
}

- (void)dealloc {
    [self endGesture];
}

// Passes `value` for setting `i` to the host and shows it: inside the open
// edit of a drag, or as an edit of its own.
- (void)set:(double)value at:(NSUInteger)i {
    const BOOL own = gesture_ != static_cast<NSInteger>(i);
    if (own)
        [self beginGesture:i];
    ConvolverSettingSetter set = settings_[i][@"set"];
    if (set)
        set(value);
    if (own)
        [self endGesture];
    [self show:value at:i];
}

// The user moved or reset slider `i`: a value half typed in its own field
// is dropped, so that leaving the field later cannot put it back (jrp's
// GarageBand run of 1 November 2026: Option-click reset Gain, then leaving
// the field restored the typed -3.5). Typing in the other box stays.
- (void)dropTyping:(NSUInteger)i {
    if (typed_ == static_cast<NSInteger>(i))
        typed_ = -1;
    NSTextField* field = _values[i];
    if (field.currentEditor)
        [field abortEditing];
}

- (void)resetSetting:(NSUInteger)i {
    if (i >= settings_.count || ![controls_[i] isKindOfClass:[NSSlider class]])
        return;
    NSNumber* initial = settings_[i][@"default"];
    if (![initial isKindOfClass:[NSNumber class]])
        return;
    [self dropTyping:i];
    controls_[i].doubleValue = initial.doubleValue;
    [self set:initial.doubleValue at:i];
}

// The user chose from a menu: the format passes the item's index to the host.
- (void)choiceMade:(NSPopUpButton*)menu {
    const auto i = static_cast<NSUInteger>(menu.tag);
    if (i >= settings_.count)
        return;
    [self set:static_cast<double>(menu.indexOfSelectedItem) at:i];
    if (chosen_)
        chosen_();
}

// The user moved a slider: the format passes the value to the host. While
// the mouse drags this slider, reading leaves it where the user puts it.
// Option-click resets it to its default, as in Logic and Apple's generic
// view (the whole click: the release would move it again).
- (void)settingChanged:(NSSlider*)slider {
    const auto i = static_cast<NSUInteger>(slider.tag);
    if (i >= settings_.count)
        return;
    NSEvent* event = NSApp.currentEvent;
    const NSEventType type = event.type;
    const BOOL mouse = type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseDragged ||
                       type == NSEventTypeLeftMouseUp;
    dragging_ =
        type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseDragged ? slider.tag : -1;
    // A drag is one edit, from the mouse going down to it coming up.
    if (mouse && type != NSEventTypeLeftMouseUp)
        [self beginGesture:i];
    if (mouse && (event.modifierFlags & NSEventModifierFlagOption))
        [self resetSetting:i];
    else {
        [self dropTyping:i];
        [self set:slider.doubleValue at:i];
    }
    if (type == NSEventTypeLeftMouseUp && gesture_ == slider.tag)
        [self endGesture];
}

// AppKit sends this at the user's first change to a box's text, not when
// the box gets focus.
- (void)controlTextDidBeginEditing:(NSNotification*)note {
    typed_ = static_cast<NSControl*>(note.object).tag;
}

// Return or leaving a box: text the user changed sets the setting, within
// the slider's range, and text that is not a number beeps. A box left
// unchanged sets nothing, since its text may be rounded ("-6.5" for
// -6.46 dB) or older than a change the host made meanwhile (automation).
// Either way the box then shows the value as the window writes it.
- (void)valueTyped:(NSTextField*)value {
    const auto i = static_cast<NSUInteger>(value.tag);
    if (i >= settings_.count || ![controls_[i] isKindOfClass:[NSSlider class]])
        return;
    auto* slider = static_cast<NSSlider*>(controls_[i]);
    if (typed_ == value.tag) {
        typed_ = -1;
        // A decimal comma reads as a point ("-6,5"), whatever the host's locale.
        NSString* typed = [[[value.stringValue stringByReplacingOccurrencesOfString:@"−"
                                                                         withString:@"-"]
            stringByReplacingOccurrencesOfString:@","
                                      withString:@"."]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        // The number at the start, read in the C locale, so a host's LC_NUMERIC
        // cannot change it; a unit typed after it ("-6 dB") is ignored.
        static const locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
        const char* chars = typed.UTF8String;
        char* end = nullptr;
        const double number = chars && c_locale ? strtod_l(chars, &end, c_locale) : 0;
        if (!chars || !c_locale || end == chars || !std::isfinite(number)) {
            NSBeep();
        } else {
            slider.doubleValue = std::clamp(number, slider.minValue, slider.maxValue);
            [self set:slider.doubleValue at:i];
        }
    }
    value.stringValue = [self number:slider.doubleValue of:settings_[i]];
}

// A drag ends when no mouse button is down (jrp's review: a button held
// anywhere else must not freeze the sliders).
- (void)readWithSampleRate:(int)sampleRate latencyWaits:(BOOL)latencyWaits {
    sampleRate_ = sampleRate;
    latencyWaits_ = latencyWaits;
    if (dragging_ >= 0 && [NSEvent pressedMouseButtons] == 0) {
        dragging_ = -1; // The drag ended without a final action.
        [self endGesture];
    }
    for (NSUInteger i = 0; i < settings_.count; ++i) {
        const BOOL dragging = dragging_ == static_cast<NSInteger>(i);
        ConvolverSettingGetter get = settings_[i][@"get"];
        const double value = get ? get() : NAN;
        if (std::isnan(value)) {
            if (typed_ == static_cast<NSInteger>(i))
                typed_ = -1; // Disabling the box ends its editing unread.
            controls_[i].enabled = NO;
            _values[i].enabled = NO;
            _values[i].stringValue = @"";
            continue;
        }
        controls_[i].enabled = YES;
        _values[i].enabled = YES;
        if ([controls_[i] isKindOfClass:[NSPopUpButton class]]) {
            [self show:value at:i];
            continue;
        }
        if (!dragging && controls_[i].doubleValue != value)
            controls_[i].doubleValue = value;
        [self show:(dragging ? controls_[i].doubleValue : value) at:i];
    }
}

@end
