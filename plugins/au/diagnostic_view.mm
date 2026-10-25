// SPDX-License-Identifier: GPL-2.0-or-later
#import "diagnostic_view.h"

namespace {
constexpr CGFloat margin = 16;
constexpr CGFloat button_height = 32;
constexpr NSTimeInterval refresh_interval = 0.5;

NSString* field(NSDictionary<NSString*, NSString*>* report, NSString* key) {
    NSString* value = report[key];
    return [value isKindOfClass:[NSString class]] ? value : @"";
}

// The status word's colour; the word is always written, so colour is never
// the only signal. Values are HostStatus numbers.
NSColor* status_colour(NSInteger status) {
    switch (status) {
    case 0: // Playing
        return [NSColor systemGreenColor];
    case 2: // Load failed
        return [NSColor systemRedColor];
    case 4: // Fault
        return [NSColor systemOrangeColor];
    default: // No filter, Not checked yet
        return [NSColor labelColor];
    }
}

NSTextField* label(NSFont* font, NSColor* colour) {
    NSTextField* text = [NSTextField labelWithString:@""];
    text.font = font;
    text.textColor = colour;
    text.selectable = YES;
    text.lineBreakMode = NSLineBreakByTruncatingTail;
    return text;
}
} // namespace

@implementation CONVOLVER_DIAGNOSTIC_VIEW {
    ConvolverReportSource report_;
    ConvolverDiagnosticsSource diagnostics_;
    NSScrollView* scroll_;
    NSTimer* timer_;
    NSString* shown_; // The explanation and details last shown, so a refresh keeps a selection.
    NSString* logPath_;
}

- (instancetype)initWithFrame:(NSRect)frame
                       report:(ConvolverReportSource)report
                  diagnostics:(ConvolverDiagnosticsSource)diagnostics {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    _pasteboard = [NSPasteboard generalPasteboard];
    report_ = [report copy];
    diagnostics_ = [diagnostics copy];
    const CGFloat width = NSWidth(frame) - 2 * margin, height = NSHeight(frame);

    _titleField = label([NSFont systemFontOfSize:[NSFont smallSystemFontSize]],
                        [NSColor secondaryLabelColor]);
    _titleField.frame = NSMakeRect(margin, 12, width, 16);
    _titleField.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    [self addSubview:_titleField];

    _statusField = label([NSFont boldSystemFontOfSize:15], [NSColor labelColor]);
    _statusField.frame = NSMakeRect(margin, 32, width, 22);
    _statusField.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    _statusField.accessibilityLabel = @"Status";
    [self addSubview:_statusField];

    const CGFloat top = 62, bottom = button_height + 2 * margin;
    scroll_ = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(margin, top, width, MAX(height - top - bottom, 40))];
    scroll_.hasVerticalScroller = YES;
    scroll_.borderType = NSBezelBorder;
    scroll_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _explanationView = [[NSTextView alloc] initWithFrame:scroll_.contentView.bounds];
    _explanationView.editable = NO;
    _explanationView.selectable = YES;
    _explanationView.richText = NO;
    _explanationView.drawsBackground = YES;
    _explanationView.backgroundColor = [NSColor textBackgroundColor];
    _explanationView.textContainerInset = NSMakeSize(6, 6);
    _explanationView.autoresizingMask = NSViewWidthSizable;
    _explanationView.verticallyResizable = YES;
    _explanationView.horizontallyResizable = NO;
    _explanationView.textContainer.widthTracksTextView = YES;
    _explanationView.accessibilityLabel = @"What Convolver plays, why, and what to change";
    scroll_.documentView = _explanationView;
    [self addSubview:scroll_];

    const CGFloat buttons = height - margin - button_height;
    _diagnosticsButton = [NSButton buttonWithTitle:@"Copy diagnostics"
                                            target:self
                                            action:@selector(putDiagnosticsOnPasteboard:)];
    _diagnosticsButton.frame = NSMakeRect(margin, buttons, 160, button_height);
    _diagnosticsButton.autoresizingMask = NSViewMinYMargin | NSViewMaxXMargin;
    _diagnosticsButton.toolTip =
        @"Copies this report and Convolver's recent log lines, ready to paste "
        @"into a problem report.";
    [self addSubview:_diagnosticsButton];

    _logButton = [NSButton buttonWithTitle:@"Show log" target:self action:@selector(showLog:)];
    _logButton.frame = NSMakeRect(margin + 168, buttons, 120, button_height);
    _logButton.autoresizingMask = NSViewMinYMargin | NSViewMaxXMargin;
    _logButton.toolTip = @"Shows the Convolver log file in the Finder.";
    [self addSubview:_logButton];

    [self refresh];
    return self;
}

- (BOOL)isFlipped {
    return YES; // Laid out from the top.
}

- (void)dealloc {
    [timer_ invalidate];
}

// Rereads the report only while the view is in a window: the host may
// dispose of the unit once it has closed the window.
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [timer_ invalidate];
    timer_ = nil;
    if (!self.window)
        return;
    __weak CONVOLVER_DIAGNOSTIC_VIEW* weakSelf = self;
    timer_ = [NSTimer timerWithTimeInterval:refresh_interval
                                    repeats:YES
                                      block:^(NSTimer* timer) {
                                        static_cast<void>(timer);
                                        [weakSelf refresh];
                                      }];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
    [self refresh];
}

- (void)refresh {
    NSDictionary<NSString*, NSString*>* report = report_ ? report_() : nil;
    NSString* text;
    if (!report) {
        _titleField.stringValue = @"Convolver";
        _statusField.stringValue = @"Not available";
        _statusField.textColor = [NSColor labelColor];
        text = @"Convolver could not read its report from the plug-in. The Convolver log has the "
               @"details.";
        logPath_ = nil;
    } else {
        _titleField.stringValue = field(report, @"title");
        _statusField.stringValue = field(report, @"status name");
        _statusField.textColor = status_colour(field(report, @"status").integerValue);
        _statusField.toolTip = field(report, @"summary");
        text = [NSString
            stringWithFormat:@"%@\n\n%@", field(report, @"explanation"), field(report, @"details")];
        logPath_ = field(report, @"log path");
    }
    _logButton.enabled = logPath_.length > 0;
    if ([text isEqualToString:shown_])
        return;
    shown_ = text;
    _explanationView.string = text;
    _explanationView.font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
    _explanationView.textColor = [NSColor labelColor];
}

- (void)putDiagnosticsOnPasteboard:(id)sender {
    NSString* text = diagnostics_ ? diagnostics_() : nil;
    if (!text) {
        NSBeep();
        return;
    }
    [_pasteboard clearContents];
    [_pasteboard setString:text forType:NSPasteboardTypeString];
    _diagnosticsButton.title = @"Copied";
    __weak CONVOLVER_DIAGNOSTIC_VIEW* weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(2 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     weakSelf.diagnosticsButton.title = @"Copy diagnostics";
                   });
}

// Selects the log in the Finder, or opens its folder when the file is not
// there yet.
- (void)showLog:(id)sender {
    if (logPath_.length == 0) {
        NSBeep();
        return;
    }
    NSURL* file = [NSURL fileURLWithPath:logPath_];
    if ([[NSFileManager defaultManager] fileExistsAtPath:logPath_]) {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ file ]];
        return;
    }
    NSURL* folder = [file URLByDeletingLastPathComponent];
    if (![[NSWorkspace sharedWorkspace] openURL:folder])
        NSBeep();
}
@end
