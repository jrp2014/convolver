// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6, VST3-11: the settings in Convolver's window (Gain, Gain mode, Mix and Latency), apart
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
// is the chosen item's index (Gain mode, Latency); beside it, the time a
// Latency choice adds. With @"note" @"gain" (Gain mode) the menu leads the
// slider row before it (PLUG-50): in any mode but Manual that slider dims and
// its box shows the gain the mode plays, and a note under the row says what
// the box cannot. With @"taper" @"gain" (Gain, PLUG-52) the slider runs from
// 0 to 1 along gain_position() in window_model.hpp, so most of its travel is
// where gains are set; the value, its box and VoiceOver stay in the setting's
// own unit. A menu's tooltip is choice_help() of its name (PLUG-65); a slider
// with a @"default" gets slider_help() (PLUG-52), or "Option-click to reset."
// when that has nothing (both in window_model.hpp, as the Windows window uses
// them). Optionally @"begin" and @"end" (ConvolverSettingGesture) bracket each
// edit: a slider calls begin when the mouse goes down and end when it comes up,
// and every other change (typing, a menu choice, Option-click, a key) calls
// begin, set and end, as the VST3 SDK asks of an editor. Without them, changes
// are only set.

@interface CONVOLVER_SETTING_ROWS : NSObject
// `chosen` runs after the user chooses from a menu, so the window can read
// the report again (it says when a new Latency takes effect).
- (instancetype)initWithSettings:(NSArray<NSDictionary<NSString*, id>*>*)settings
                          chosen:(void (^)(void))chosen NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;
// Shows the settings as the host has them now (automation, its own
// controls), except the slider the user is dragging. `sampleRate` (0 before
// the host starts) gives the time a Latency choice adds; `latencyWaits`
// says the chosen Latency is not in use yet, and `latencyNextStart` that
// it is selected for the next start (the host has the plug-in stopped);
// `gainPlayed` is the gain Estimated or Clip-safe plays, for the Gain box,
// and `gainNote` what that box cannot say (the report's gain_played and
// gain_note, both empty in Manual mode); `gainPlayedDb` is that gain as a
// number, for the dimmed slider, NAN when there is none (PLUG-55).
- (void)readWithSampleRate:(int)sampleRate
              latencyWaits:(BOOL)latencyWaits
          latencyNextStart:(BOOL)latencyNextStart
                  gainNote:(NSString*)gainNote
                gainPlayed:(NSString*)gainPlayed
              gainPlayedDb:(double)gainPlayedDb;
// PLUG-50: whether setting `i` (Gain mode) sits in the row before it, which
// `rows` then holds at both indexes; the window shows that row once.
- (BOOL)joinsRowBefore:(NSUInteger)i;
// Setting `i` back to its default, as Option-click on its slider does.
- (void)resetSetting:(NSUInteger)i;
// Ends an edit still open (a drag the window stopped seeing), as when the
// window closes.
- (void)endGesture;
// Each setting's row, without its "Name:" label, in the settings' order
// (a joined setting's entry is the row it sits in).
@property(readonly, nonatomic) NSArray<NSView*>* rows;
// Each setting's slider or menu, and its value: an editable number field for
// a slider, the time it adds for the Latency menu, and for the Gain mode
// menu the note under the Gain row.
@property(readonly, nonatomic) NSArray<NSSlider*>* sliders;
@property(readonly, nonatomic) NSArray<NSPopUpButton*>* menus;
@property(readonly, nonatomic) NSArray<NSTextField*>* values;
@end

NS_ASSUME_NONNULL_END
