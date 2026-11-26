// SPDX-License-Identifier: GPL-2.0-or-later
#import "setting_rows.h"
#include "host_log.hpp"
#import "view_layout.h"
#include "window_model.hpp"
#include <cmath>

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
    NSInteger dragging_;    // The slider the user is dragging, or -1.
    NSInteger typed_;       // The box whose text the user has changed, or -1.
    NSInteger gesture_;     // The setting whose edit is open (its begin called), or -1.
    NSInteger resetting_;   // PLUG-56: the slider an Option-click is resetting, or -1.
    int sampleRate_;        // The host's, for the Latency row; 0 before it starts.
    BOOL latencyWaits_;     // The chosen Latency is not in use yet.
    BOOL latencyNextStart_; // It is selected for the next start (AU-19).
    NSString* gainNote_;    // PLUG-50: what the Gain row's box cannot say (the gain note).
    NSString* gainPlayed_;  // PLUG-50: the gain Estimated or Clip-safe plays, for the box.
    NSUInteger joined_;     // PLUG-50: the Gain mode menu, in the row before it (or NSNotFound).
    NSMutableIndexSet* tapered_; // PLUG-52: the sliders with Gain's taper (0 to 1 on the track).
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
    resetting_ = -1;
    joined_ = NSNotFound;
    tapered_ = [NSMutableIndexSet indexSet];
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
            // A choice from a list (Gain mode, Latency): the item's index is the value.
            NSPopUpButton* menu = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
            [menu addItemsWithTitles:choices];
            menu.target = self;
            menu.action = @selector(choiceMade:);
            menu.translatesAutoresizingMaskIntoConstraints = NO;
            control = menu;
            [menus addObject:menu];
            auto* before = i > 0 && [controls.lastObject isKindOfClass:[NSSlider class]]
                               ? static_cast<NSStackView*>(rows.lastObject)
                               : nil;
            if ([setting[@"note"] isEqual:@"gain"] && before) {
                // PLUG-50: the Gain mode menu leads the Gain row, so one row
                // says which gain plays and no "Gain mode:" label is cut
                // short (jrp's run of 0.18.10). What the box cannot say
                // goes under the row, hidden while empty.
                joined_ = i;
                [before insertArrangedSubview:menu atIndex:0];
                [before setCustomSpacing:8 afterView:menu];
                // The form's content width (diagnostic_view.mm: inner -
                // form_label - 10), as for the note under Mix.
                value = wrapping(small_font(), secondary_text(), 446);
                value.hidden = YES;
                NSStackView* column =
                    stack(@[ before, value ], NSUserInterfaceLayoutOrientationVertical, 4);
                column.alignment = NSLayoutAttributeLeading;
                [NSLayoutConstraint
                    activateConstraints:@[ [before.widthAnchor
                                            constraintEqualToAnchor:column.widthAnchor] ]];
                stretches(column);
                rows[rows.count - 1] = column;
                content = column;
            } else {
                value = label(body_font(), secondary_text());
                stretches(value);
                NSStackView* row =
                    stack(@[ menu, value ], NSUserInterfaceLayoutOrientationHorizontal, 12);
                row.distribution = NSStackViewDistributionFill;
                content = row;
            }
        } else {
            // PLUG-52: Gain's slider runs from 0 to 1 along its taper
            // (gain_position()); the setting itself stays in decibels.
            const BOOL taper = [setting[@"taper"] isEqual:@"gain"];
            if (taper)
                [tapered_ addIndex:i];
            NSSlider* slider =
                [NSSlider sliderWithValue:taper ? 0.0 : [setting[@"minimum"] doubleValue]
                                 minValue:taper ? 0.0 : [setting[@"minimum"] doubleValue]
                                 maxValue:taper ? 1.0 : [setting[@"maximum"] doubleValue]
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
                NSTextField* left = label([NSFont systemFontOfSize:10], secondary_text());
                NSTextField* right = label([NSFont systemFontOfSize:10], secondary_text());
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
            value.toolTip = @(value_box_help); // PLUG-52.
            fixed_width(value, value_field);
            NSTextField* unit = label(body_font(), secondary_text());
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
        else if (setting[@"default"]) {
            // PLUG-52: what the slider does, then how to reset it.
            NSString* name = setting[@"name"];
            const auto help = slider_help(name.length ? name.UTF8String : "", "Option-click");
            control.toolTip = help.empty() ? @"Option-click to reset." : @(help.c_str());
        }
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

- (BOOL)joinsRowBefore:(NSUInteger)i {
    return i == joined_;
}

// PLUG-52: where slider `i` sits for a setting's `value`, and the value at
// the slider's position: through Gain's taper, or the value itself.
- (double)position:(double)value at:(NSUInteger)i {
    return [tapered_ containsIndex:i] ? convolver::view::gain_position(value) : value;
}
- (double)sliderValue:(NSUInteger)i {
    const double at = controls_[i].doubleValue;
    return [tapered_ containsIndex:i] ? convolver::view::gain_at_position(at) : at;
}

// A setting's value as the window writes it ("-12", "50"): the same text as
// the report's settings and the log (convolver::setting_text).
- (NSString*)number:(double)value of:(NSDictionary<NSString*, id>*)setting {
    const auto text = convolver::setting_text(value, [setting[@"minimum"] doubleValue],
                                              [setting[@"maximum"] doubleValue]);
    return @(text.c_str());
}

// The time a Latency choice adds at the host's rate ("adds 5.3 ms"), and
// whether it is not in use yet; the item titles begin with the frames ("256
// frames"). The Audio section does not repeat it (PLUG-22).
- (NSString*)latencyNote:(NSPopUpButton*)menu {
    const auto note = convolver::view::latency_note(menu.titleOfSelectedItem.doubleValue,
                                                    sampleRate_, latencyWaits_, latencyNextStart_);
    return @(note.c_str());
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
        // PLUG-50: under the Gain row, what its box cannot say.
        if (i == joined_) {
            _values[i].stringValue = gainNote_ ? gainNote_ : @"";
            _values[i].hidden = _values[i].stringValue.length == 0;
        } else {
            _values[i].stringValue = [self latencyNote:menu];
        }
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
    controls_[i].doubleValue = [self position:initial.doubleValue at:i];
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
// view. PLUG-56 (jrp's review of 7bd024f): the click that went down with
// Option is a reset to its release, whatever Option does meanwhile: the
// default is set once, as one edit, and the knob stays at it however the
// mouse moves, as Windows' Ctrl-click does.
- (void)settingChanged:(NSSlider*)slider {
    const auto i = static_cast<NSUInteger>(slider.tag);
    if (i >= settings_.count)
        return;
    NSEvent* event = NSApp.currentEvent;
    const NSEventType type = event.type;
    const BOOL mouse = type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseDragged ||
                       type == NSEventTypeLeftMouseUp;
    const BOOL option = (event.modifierFlags & NSEventModifierFlagOption) != 0;
    // Each click starts afresh: a reset whose mouse-up AppKit never delivered
    // must not swallow the next click.
    if (type == NSEventTypeLeftMouseDown)
        resetting_ = -1;
    // A click that goes down with Option resets. AppKit may send no action
    // as the mouse goes down on the knob itself, so the first action of a
    // click that is neither a reset nor a drag yet counts too.
    if (mouse && option &&
        (type == NSEventTypeLeftMouseDown ||
         (resetting_ != slider.tag && dragging_ != slider.tag))) {
        // An edit a drag left open (AppKit can end one with no final
        // action) is closed first, so the host never sees it still open.
        [self endGesture];
        resetting_ = type == NSEventTypeLeftMouseUp ? -1 : slider.tag;
        dragging_ = -1;
        [self resetSetting:i];
        return;
    }
    if (mouse && resetting_ == slider.tag) {
        // The rest of the resetting click: the knob goes back to the default.
        NSNumber* initial = settings_[i][@"default"];
        if ([initial isKindOfClass:[NSNumber class]])
            slider.doubleValue = [self position:initial.doubleValue at:i];
        if (type == NSEventTypeLeftMouseUp)
            resetting_ = -1;
        return;
    }
    resetting_ = -1;
    dragging_ =
        type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseDragged ? slider.tag : -1;
    // A drag is one edit, from the mouse going down to it coming up.
    if (mouse && type != NSEventTypeLeftMouseUp)
        [self beginGesture:i];
    [self dropTyping:i];
    [self set:[self sliderValue:i] at:i];
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
    const double minimum = [settings_[i][@"minimum"] doubleValue];
    const double maximum = [settings_[i][@"maximum"] doubleValue];
    if (typed_ == value.tag) {
        typed_ = -1;
        // A decimal comma reads as a point ("-6,5") and a unit typed after the
        // number ("-6 dB") is ignored, whatever the host's locale (the shared
        // window model, portable/window_model.hpp).
        const char* chars = value.stringValue.UTF8String;
        const auto number =
            chars ? convolver::view::typed_value(chars, minimum, maximum) : std::nullopt;
        if (!number) {
            NSBeep();
        } else {
            slider.doubleValue = [self position:*number at:i];
            [self set:*number at:i];
        }
    }
    value.stringValue = [self number:[self sliderValue:i] of:settings_[i]];
}

// A drag ends when no mouse button is down (jrp's review: a button held
// anywhere else must not freeze the sliders).
- (void)readWithSampleRate:(int)sampleRate
              latencyWaits:(BOOL)latencyWaits
          latencyNextStart:(BOOL)latencyNextStart
                  gainNote:(NSString*)gainNote
                gainPlayed:(NSString*)gainPlayed
              gainPlayedDb:(double)gainPlayedDb {
    sampleRate_ = sampleRate;
    latencyWaits_ = latencyWaits;
    latencyNextStart_ = latencyNextStart;
    gainNote_ = [gainNote copy];
    gainPlayed_ = [gainPlayed copy];
    // The Gain mode chosen now: anything but Manual (0) plays its own gain.
    BOOL moded = NO;
    if (joined_ != NSNotFound) {
        ConvolverSettingGetter mode = settings_[joined_][@"get"];
        const double index = mode ? mode() : NAN;
        // Only once the report says what the mode plays: until the plug-in
        // has applied a new mode, Gain stays as it was.
        moded = !std::isnan(index) && std::lround(index) != 0 &&
                (gainPlayed_.length > 0 || gainNote_.length > 0);
    }
    if (dragging_ >= 0 && [NSEvent pressedMouseButtons] == 0) {
        dragging_ = -1; // The drag ended without a final action.
        [self endGesture];
    }
    if (resetting_ >= 0 && [NSEvent pressedMouseButtons] == 0)
        resetting_ = -1; // So did the resetting click.
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
        if (moded && joined_ == i + 1) {
            // PLUG-50: Estimated and Clip-safe play their own gain: the
            // Gain slider dims and its box shows that gain, greyed. The
            // Gain setting itself is kept for Manual.
            if (dragging)
                dragging_ = -1;
            if (gesture_ == static_cast<NSInteger>(i))
                [self endGesture];
            [self dropTyping:i];
            controls_[i].enabled = NO;
            _values[i].enabled = NO;
            _values[i].stringValue = gainPlayed_ ? gainPlayed_ : @"";
            // The number, not the box's text: NSString's doubleValue reads
            // only a decimal point (PLUG-55).
            if (!std::isnan(gainPlayedDb))
                controls_[i].doubleValue = [self position:gainPlayedDb at:i];
            controls_[i].accessibilityValueDescription =
                gainPlayed_.length ? [gainPlayed_ stringByAppendingString:settings_[i][@"unit"]]
                                   : gainNote_;
            continue;
        }
        controls_[i].enabled = YES;
        _values[i].enabled = YES;
        if ([controls_[i] isKindOfClass:[NSPopUpButton class]]) {
            [self show:value at:i];
            continue;
        }
        if (!dragging && controls_[i].doubleValue != [self position:value at:i])
            controls_[i].doubleValue = [self position:value at:i];
        [self show:(dragging ? [self sliderValue:i] : value) at:i];
    }
}

@end
