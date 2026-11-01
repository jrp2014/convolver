// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: Convolver's window, laid out as jrp approved on 1 November 2026
// (docs/use/plugins.md#the-audio-units-window): the status as a symbol, a word and what
// plays, why and what to change; a form with the Filter (its file name and
// Choose…), Gain and Mix (slider, typed value, unit; Option-click resets) and
// Latency (menu and the time it adds); the Audio facts in plain words; the
// file, software and log lines folded away; and Copy Diagnostics and Show
// Log. A filter file can also be dropped on it. Native AppKit controls in
// stack views (auto layout), so screen readers, keyboard use, dark mode and
// text scaling come from the system. It reads and sets everything through
// blocks a format supplies (the Audio Unit's custom properties and
// parameters), on the main thread, and never touches the processor or the
// audio thread (docs/project/plans/2026-10-24-plugin-editor.md#threads).
// Every word it shows comes from the shared report (portable/host_report),
// so the VST3 windows (VST3-11 to VST3-13) can show the same. VST3-11 is to
// reuse this view, moving it to a folder both formats share.
#pragma once
#import <AppKit/AppKit.h>

// Objective-C classes share one namespace per process, so the build names
// this class with Convolver's version (ConvolverDiagnosticView_0_6_0): two
// Convolver builds in one host each keep their own window.
#ifndef CONVOLVER_DIAGNOSTIC_VIEW
#error "CONVOLVER_DIAGNOSTIC_VIEW names the view class; plugins/au/CMakeLists.txt defines it."
#endif

NS_ASSUME_NONNULL_BEGIN

// report_fields() (portable/host_report.hpp) as a dictionary, or nil when the
// plug-in cannot give it.
typedef NSDictionary<NSString*, NSString*>* _Nullable (^ConvolverReportSource)(void);
// diagnostics_text(), or nil.
typedef NSString* _Nullable (^ConvolverDiagnosticsSource)(void);
// A setting's current value (NAN when the plug-in cannot give it), and a
// change the user made, which the format passes to the host.
typedef double (^ConvolverSettingGetter)(void);
typedef void (^ConvolverSettingSetter)(double value);
// Loads the filter file at `path` (a .cfg, a configuration list or a sound
// file); returns whether the plug-in accepted it. Either way the report then
// says what plays and why.
typedef BOOL (^ConvolverFileLoader)(NSString* path);
// One setting the window shows as a slider is a dictionary with the keys
// @"name" (NSString), @"minimum", @"maximum" and @"default" (NSNumber),
// @"unit" (NSString, as @" dB") for writing its value, @"get"
// (ConvolverSettingGetter) and @"set" (ConvolverSettingSetter); optionally
// @"low" and @"high" (NSString), words under the slider's ends (Mix: Dry and
// Wet). With @"choices" (NSArray<NSString*>) it is a menu instead, whose value
// is the chosen item's index (Latency); @"tip" (NSString), if present, is the
// control's tooltip.

@interface CONVOLVER_DIAGNOSTIC_VIEW : NSView
// `settings` (Gain, Mix and Latency) appear as rows of the form under the
// Filter row: a host that shows this window shows it instead of its generic
// controls.
- (instancetype)initWithFrame:(NSRect)frame
                       report:(ConvolverReportSource)report
                  diagnostics:(ConvolverDiagnosticsSource)diagnostics
                     settings:(NSArray<NSDictionary<NSString*, id>*>*)settings
                       loader:(nullable ConvolverFileLoader)loader;
// What Choose… and a drop do with the file: loads it and rereads the
// report. Returns whether the plug-in accepted it.
- (BOOL)loadFile:(NSString*)path;
// Reads the report now.
- (void)refresh;
// What the view does twice a second while it is in a window, which catches
// faults the audio thread signals without telling anyone: reads the report
// only while the window is on screen, so a host that keeps a closed or
// hidden window does not have it polled, and may dispose of the unit then.
// Returns whether it read.
- (BOOL)refreshIfShown;
// Choose… in two steps, so the tests can drive it without a dialog:
// startChoosing records a request (and forgets any earlier one), and
// finishChoosing:URL: loads its file only while that request is still the
// pending one and the view is in a window. Closing the window, or the view
// leaving it, forgets the request and cancels its dialog: the host may then
// dispose of the unit while the dialog is still open. Returns whether the
// file was loaded.
- (NSUInteger)startChoosing;
- (BOOL)finishChoosing:(NSUInteger)request URL:(nullable NSURL*)url;
// Setting `i` back to its default, as Option-click on its slider does.
- (void)resetSetting:(NSUInteger)i;
// Shows or hides the file, software and log lines, as their disclosure
// button does.
- (void)showReference:(BOOL)shown;
// What a user reads and presses, for the tests.
@property(readonly, nonatomic) NSTextField* titleField;
@property(readonly, nonatomic) NSImageView* statusSymbol;
@property(readonly, nonatomic) NSTextField* statusField;
// What plays, why and what to change: the report's headline.
@property(readonly, nonatomic) NSTextView* explanationView;
@property(readonly, nonatomic) NSTextField* filterNameField;
@property(readonly, nonatomic) NSTextField* filterNoteField;
@property(readonly, nonatomic) NSButton* chooseButton;
// Each setting's slider or menu, and its value: an editable number field for
// a slider, the time it adds for the Latency menu.
@property(readonly, nonatomic) NSArray<NSSlider*>* settingSliders;
@property(readonly, nonatomic) NSArray<NSPopUpButton*>* settingMenus;
@property(readonly, nonatomic) NSArray<NSTextField*>* settingValues;
// The Audio section: one label and one value (value and note) per fact.
@property(readonly, nonatomic) NSArray<NSTextField*>* audioLabels;
@property(readonly, nonatomic) NSArray<NSTextField*>* audioValues;
// The file, software and log lines and their disclosure button.
@property(readonly, nonatomic) NSButton* referenceButton;
@property(readonly, nonatomic) NSTextField* referenceField;
@property(readonly, nonatomic) NSButton* diagnosticsButton;
@property(readonly, nonatomic) NSButton* logButton;
// Where Copy Diagnostics puts its text: the general pasteboard, unless a test
// gives another.
@property(strong, nonatomic) NSPasteboard* pasteboard;
@end

NS_ASSUME_NONNULL_END
