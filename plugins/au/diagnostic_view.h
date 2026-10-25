// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: Convolver's first window, the smallest useful one: the status, the
// full explanation of what plays and what to change, the details, and the
// Copy diagnostics and Show log buttons. Native AppKit controls only, so
// screen readers, keyboard use, dark mode and text scaling come from the
// system. It reads everything through two sources a format supplies (the
// Audio Unit's custom properties), on the main thread, and never touches
// the processor or the audio thread
// (docs/project/plans/2026-10-24-plugin-editor.md#threads). VST3-11 is to
// reuse it, moving it to a folder both formats share.
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

@interface CONVOLVER_DIAGNOSTIC_VIEW : NSView
- (instancetype)initWithFrame:(NSRect)frame
                       report:(ConvolverReportSource)report
                  diagnostics:(ConvolverDiagnosticsSource)diagnostics;
// Reads the report now. The view also rereads it twice a second while it is
// in a window, which catches faults the audio thread signals without telling
// anyone.
- (void)refresh;
// What a user reads and presses, for the tests.
@property(readonly, nonatomic) NSTextField* titleField;
@property(readonly, nonatomic) NSTextField* statusField;
@property(readonly, nonatomic) NSTextView* explanationView;
@property(readonly, nonatomic) NSButton* copyButton;
@property(readonly, nonatomic) NSButton* logButton;
// Where Copy diagnostics puts its text: the general pasteboard, unless a test
// gives another.
@property(strong, nonatomic) NSPasteboard* pasteboard;
@end

NS_ASSUME_NONNULL_END
