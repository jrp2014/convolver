// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6, VST3-11: the settings in Convolver's window (Gain, Mix and Latency), apart
// from the window's layout (AU-10, jrp's review of 1 November 2026). Each
// setting is a full control, because a host that shows the window shows it
// instead of its generic controls: a slider with its value typed beside it
// and its unit (Apple's generic view, Logic), or a menu with the time it
// adds (Logic's buffer size). This object builds those rows and handles
// everything the user does with them: dragging, Option-click reset, typing
// in a box and choosing from the menu. The window places the rows in its
// form and asks them to read the host's values on each refresh.
#pragma once
#import <AppKit/AppKit.h>

// Objective-C classes share one namespace per process, so the build names
// this class with Convolver's version, as it does the view.
#ifndef CONVOLVER_SETTING_ROWS
#error                                                                                             \
    "CONVOLVER_SETTING_ROWS names the setting rows' class; each format's CMakeLists.txt defines it."
#endif

NS_ASSUME_NONNULL_BEGIN

// A setting's current value (NAN when the plug-in cannot give it), and a
// change the user made, which the format passes to the host.
typedef double (^ConvolverSettingGetter)(void);
typedef void (^ConvolverSettingSetter)(double value);
// The start or end of one edit (VST3's beginEdit and endEdit), so a host
// records a drag as one gesture.
typedef void (^ConvolverSettingGesture)(void);
// One setting the window shows as a slider is a dictionary with the keys
// @"name" (NSString), @"minimum", @"maximum" and @"default" (NSNumber),
// @"unit" (NSString, as @" dB") for writing its value, @"get"
// (ConvolverSettingGetter) and @"set" (ConvolverSettingSetter); optionally
// @"low" and @"high" (NSString), words under the slider's ends (Mix: Dry and
// Wet). With @"choices" (NSArray<NSString*>) it is a menu instead, whose value
// is the chosen item's index (Latency); @"tip" (NSString), if present, is the
// control's tooltip. Optionally @"begin" and @"end" (ConvolverSettingGesture)
// bracket each edit: a slider calls begin when the mouse goes down and end
// when it comes up, and every other change (typing, a menu choice,
// Option-click, a key) calls begin, set and end, as the VST3 SDK asks of an
// editor. Without them, changes are only set.

@interface CONVOLVER_SETTING_ROWS : NSObject
// `chosen` runs after the user chooses from a menu, so the window can read
// the report again (it says when a new Latency takes effect).
- (instancetype)initWithSettings:(NSArray<NSDictionary<NSString*, id>*>*)settings
                          chosen:(void (^)(void))chosen NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;
// Shows the settings as the host has them now (automation, its own
// controls), except the slider the user is dragging. `sampleRate` (0 before
// the host starts) gives the time a Latency choice adds; `latencyWaits`
// says the chosen Latency is not in use yet.
- (void)readWithSampleRate:(int)sampleRate latencyWaits:(BOOL)latencyWaits;
// Setting `i` back to its default, as Option-click on its slider does.
- (void)resetSetting:(NSUInteger)i;
// Ends an edit still open (a drag the window stopped seeing), as when the
// window closes.
- (void)endGesture;
// Each setting's row, without its "Name:" label, in the settings' order.
@property(readonly, nonatomic) NSArray<NSView*>* rows;
// Each setting's slider or menu, and its value: an editable number field for
// a slider, the time it adds for the Latency menu.
@property(readonly, nonatomic) NSArray<NSSlider*>* sliders;
@property(readonly, nonatomic) NSArray<NSPopUpButton*>* menus;
@property(readonly, nonatomic) NSArray<NSTextField*>* values;
@end

NS_ASSUME_NONNULL_END
