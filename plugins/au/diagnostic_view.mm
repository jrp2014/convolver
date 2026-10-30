// SPDX-License-Identifier: GPL-2.0-or-later
#import "diagnostic_view.h"
#include "host_log.hpp"
#include <cmath>

namespace {
// The usual macOS panel layout (jrp's GarageBand run: the first version
// filled whatever size the host offered and left most of it blank). A fixed
// width, a height that fits the text, label and value rows for the details,
// Choose Filter at the bottom left and the other buttons at the bottom right.
constexpr CGFloat width = 560;
constexpr CGFloat margin = 20;
constexpr CGFloat button_height = 32;
constexpr CGFloat label_column = 76; // Where the detail values start.
constexpr CGFloat settings_top = 66;
constexpr CGFloat setting_row = 28;
constexpr CGFloat value_width = 84;
constexpr CGFloat most_text = 480; // Longer text scrolls (a list .cfg explains every candidate).
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

// The explanation, then each "Label: value" line of the details as a row:
// the label in the secondary colour, the value aligned after it, wrapped
// lines indented under the value.
NSAttributedString* styled(NSString* explanation, NSString* details) {
    NSFont* font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
    NSMutableParagraphStyle* prose = [[NSMutableParagraphStyle alloc] init];
    prose.paragraphSpacing = 10;
    NSMutableAttributedString* text = [[NSMutableAttributedString alloc]
        initWithString:[explanation stringByAppendingString:@"\n"]
            attributes:@{
                NSFontAttributeName : font,
                NSForegroundColorAttributeName : [NSColor labelColor],
                NSParagraphStyleAttributeName : prose
            }];
    NSMutableParagraphStyle* row = [[NSMutableParagraphStyle alloc] init];
    row.tabStops = @[ [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentLeft
                                                      location:label_column
                                                       options:@{}] ];
    row.headIndent = label_column;
    row.paragraphSpacing = 3;
    for (NSString* line in [details componentsSeparatedByString:@"\n"]) {
        if (line.length == 0)
            continue;
        const NSRange colon = [line rangeOfString:@": "];
        NSString* name =
            colon.location == NSNotFound ? @"" : [line substringToIndex:colon.location];
        NSString* value =
            colon.location == NSNotFound ? line : [line substringFromIndex:NSMaxRange(colon)];
        [text appendAttributedString:[[NSAttributedString alloc]
                                         initWithString:[name stringByAppendingString:@"\t"]
                                             attributes:@{
                                                 NSFontAttributeName : font,
                                                 NSForegroundColorAttributeName :
                                                     [NSColor secondaryLabelColor],
                                                 NSParagraphStyleAttributeName : row
                                             }]];
        [text appendAttributedString:[[NSAttributedString alloc]
                                         initWithString:[value stringByAppendingString:@"\n"]
                                             attributes:@{
                                                 NSFontAttributeName : font,
                                                 NSForegroundColorAttributeName :
                                                     [NSColor labelColor],
                                                 NSParagraphStyleAttributeName : row
                                             }]];
    }
    return text;
}
// The first file dropped on the window, as if chosen.
NSURL* dropped_file(id<NSDraggingInfo> sender) {
    NSArray<NSURL*>* urls =
        [sender.draggingPasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                 options:@{
                                                     NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                                 }];
    return urls.firstObject;
}

} // namespace

@implementation CONVOLVER_DIAGNOSTIC_VIEW {
    ConvolverReportSource report_;
    ConvolverDiagnosticsSource diagnostics_;
    ConvolverFileLoader loader_;
    NSScrollView* scroll_;
    NSTimer* timer_;
    NSString* shown_; // The explanation and details last shown, so a refresh keeps a selection.
    NSString* logPath_;
    NSArray<NSDictionary<NSString*, id>*>* settings_;
    CGFloat textTop_; // Under the settings.
}

- (instancetype)initWithFrame:(NSRect)frame
                       report:(ConvolverReportSource)report
                  diagnostics:(ConvolverDiagnosticsSource)diagnostics
                     settings:(NSArray<NSDictionary<NSString*, id>*>*)settings
                       loader:(ConvolverFileLoader)loader {
    // The view's own size: the frame gives only its position.
    self = [super initWithFrame:NSMakeRect(NSMinX(frame), NSMinY(frame), width, 240)];
    if (!self)
        return nil;
    _pasteboard = [NSPasteboard generalPasteboard];
    report_ = [report copy];
    diagnostics_ = [diagnostics copy];
    loader_ = [loader copy];
    const CGFloat inner = width - 2 * margin;

    _titleField = label([NSFont systemFontOfSize:[NSFont smallSystemFontSize]],
                        [NSColor secondaryLabelColor]);
    _titleField.frame = NSMakeRect(margin, 14, inner, 16);
    _titleField.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    [self addSubview:_titleField];

    _statusField = label([NSFont boldSystemFontOfSize:17], [NSColor labelColor]);
    _statusField.frame = NSMakeRect(margin, 32, inner, 24);
    _statusField.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    _statusField.accessibilityLabel = @"Status";
    [self addSubview:_statusField];

    // The settings: name, slider, value. A host that shows this window shows
    // it instead of its generic controls (jrp's GarageBand run).
    settings_ = [settings copy];
    NSMutableArray<NSSlider*>* sliders = [NSMutableArray array];
    NSMutableArray<NSTextField*>* values = [NSMutableArray array];
    CGFloat y = settings_top;
    for (NSUInteger i = 0; i < settings_.count; ++i, y += setting_row) {
        NSDictionary<NSString*, id>* setting = settings_[i];
        NSTextField* name =
            label([NSFont systemFontOfSize:[NSFont systemFontSize]], [NSColor secondaryLabelColor]);
        name.stringValue = setting[@"name"];
        name.frame = NSMakeRect(margin, y + 4, label_column - 8, 18);
        name.autoresizingMask = NSViewMaxYMargin;
        [self addSubview:name];
        NSSlider* slider = [NSSlider sliderWithValue:[setting[@"minimum"] doubleValue]
                                            minValue:[setting[@"minimum"] doubleValue]
                                            maxValue:[setting[@"maximum"] doubleValue]
                                              target:self
                                              action:@selector(settingChanged:)];
        slider.continuous = YES;
        slider.tag = static_cast<NSInteger>(i);
        slider.frame =
            NSMakeRect(margin + label_column, y, inner - label_column - value_width - 8, 24);
        slider.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
        slider.accessibilityLabel = setting[@"name"];
        [self addSubview:slider];
        NSTextField* value = label([NSFont monospacedDigitSystemFontOfSize:[NSFont systemFontSize]
                                                                    weight:NSFontWeightRegular],
                                   [NSColor labelColor]);
        value.alignment = NSTextAlignmentRight;
        value.frame = NSMakeRect(width - margin - value_width, y + 4, value_width, 18);
        value.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
        [self addSubview:value];
        [sliders addObject:slider];
        [values addObject:value];
    }
    _settingSliders = sliders;
    _settingValues = values;
    textTop_ = settings_.count ? y + 10 : settings_top - 2;

    // No border or box: the text reads as part of the panel. It scrolls only
    // when it is longer than most_text.
    scroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(margin, textTop_, inner, 100)];
    scroll_.hasVerticalScroller = YES;
    scroll_.autohidesScrollers = YES;
    scroll_.borderType = NSNoBorder;
    scroll_.drawsBackground = NO;
    scroll_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _explanationView = [[NSTextView alloc] initWithFrame:scroll_.contentView.bounds];
    _explanationView.editable = NO;
    _explanationView.selectable = YES;
    _explanationView.richText = YES; // Styled details; still read-only.
    _explanationView.drawsBackground = NO;
    _explanationView.textContainerInset = NSZeroSize;
    _explanationView.textContainer.lineFragmentPadding = 0;
    // Apple's recipe for a text view in a scroll view.
    _explanationView.minSize = NSMakeSize(0, 0);
    _explanationView.maxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    _explanationView.autoresizingMask = NSViewWidthSizable;
    _explanationView.verticallyResizable = YES;
    _explanationView.horizontallyResizable = NO;
    _explanationView.textContainer.containerSize = NSMakeSize(inner, CGFLOAT_MAX);
    _explanationView.textContainer.widthTracksTextView = YES;
    _explanationView.accessibilityLabel = @"What Convolver plays, why, and what to change";
    // A file dropped on the text goes to the window, not into the text.
    [_explanationView unregisterDraggedTypes];
    scroll_.documentView = _explanationView;
    [self addSubview:scroll_];

    // Choose Filter at the bottom left, as the old Convolver's filter
    // button; a file dropped anywhere on the window does the same.
    _chooseButton = [NSButton buttonWithTitle:@"Choose Filter…"
                                       target:self
                                       action:@selector(chooseFilter:)];
    _chooseButton.toolTip =
        @"Loads a Convolver configuration (.cfg) or a sound file as the filter; you can also "
        @"drop the file on this window. The project keeps the filter itself, as a preset does, "
        @"so moving the file later does not change it.";
    _chooseButton.autoresizingMask = NSViewMinYMargin | NSViewMaxXMargin;
    _chooseButton.hidden = loader_ == nil;
    [self addSubview:_chooseButton];
    if (loader_)
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];

    // The buttons at the bottom right, Show log rightmost.
    _logButton = [NSButton buttonWithTitle:@"Show log" target:self action:@selector(showLog:)];
    _logButton.toolTip = @"Shows the Convolver log file in the Finder.";
    _diagnosticsButton = [NSButton buttonWithTitle:@"Copy diagnostics"
                                            target:self
                                            action:@selector(putDiagnosticsOnPasteboard:)];
    _diagnosticsButton.toolTip =
        @"Copies this report and Convolver's recent log lines, ready to paste "
        @"into a problem report.";
    for (NSButton* button in @[ _diagnosticsButton, _logButton ]) {
        button.autoresizingMask = NSViewMinYMargin | NSViewMinXMargin;
        [self addSubview:button];
    }

    [self refresh];
    return self;
}

// Fits the height to the text (up to most_text, then it scrolls) and puts
// the buttons under it. Hosts that follow the view's size resize the window.
- (void)fit {
    NSLayoutManager* layout = _explanationView.layoutManager;
    [layout ensureLayoutForTextContainer:_explanationView.textContainer];
    const CGFloat text =
        ceil(NSHeight([layout usedRectForTextContainer:_explanationView.textContainer]));
    const CGFloat shown = MIN(MAX(text, 40), most_text);
    const CGFloat buttons = textTop_ + shown + 16;
    const CGFloat log_width = MAX(NSWidth(_logButton.frame), 100);
    const CGFloat copy_width = MAX(NSWidth(_diagnosticsButton.frame), 150);
    // The view's size first: resizing it moves the subviews by their
    // autoresizing masks (kept for hosts that resize the window), and the
    // frames below are where they belong at this size.
    [self setFrameSize:NSMakeSize(width, buttons + button_height + margin)];
    scroll_.frame = NSMakeRect(margin, textTop_, width - 2 * margin, shown);
    _logButton.frame = NSMakeRect(width - margin - log_width, buttons, log_width, button_height);
    _diagnosticsButton.frame =
        NSMakeRect(NSMinX(_logButton.frame) - 12 - copy_width, buttons, copy_width, button_height);
    const CGFloat choose_width = MAX(NSWidth(_chooseButton.frame), 130);
    _chooseButton.frame = NSMakeRect(margin, buttons, choose_width, button_height);
}

- (BOOL)isFlipped {
    return YES; // Laid out from the top.
}

- (void)dealloc {
    [timer_ invalidate];
}

// The timer runs while the view is in a window and reads only while that
// window is on screen (refreshIfShown): the host may dispose of the unit once
// it has closed the window, which need not remove the view.
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
                                        [weakSelf refreshIfShown];
                                      }];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
    [self refresh];
}

- (BOOL)refreshIfShown {
    NSWindow* window = self.window;
    if (!window.visible || !(window.occlusionState & NSWindowOcclusionStateVisible))
        return NO;
    [self refresh];
    return YES;
}

// A setting's value as the window writes it ("-12 dB", "50% wet"): the same
// text as the report's details and the log (convolver::setting_text).
- (NSString*)describe:(double)value of:(NSDictionary<NSString*, id>*)setting {
    const auto text = convolver::setting_text(value, [setting[@"minimum"] doubleValue],
                                              [setting[@"maximum"] doubleValue]);
    return [@(text.c_str()) stringByAppendingString:setting[@"unit"]];
}

// Shows `value` beside slider `i`, and gives VoiceOver the same words.
- (void)show:(double)value at:(NSUInteger)i {
    NSString* text = [self describe:value of:settings_[i]];
    _settingValues[i].stringValue = text;
    _settingSliders[i].accessibilityValueDescription = text;
}

// The user moved a slider: the format passes the value to the host.
- (void)settingChanged:(NSSlider*)slider {
    const auto i = static_cast<NSUInteger>(slider.tag);
    if (i >= settings_.count)
        return;
    NSDictionary<NSString*, id>* setting = settings_[i];
    ConvolverSettingSetter set = setting[@"set"];
    if (set)
        set(slider.doubleValue);
    [self show:slider.doubleValue at:i];
}

// The settings as the host has them now (automation, its own controls),
// except while the user is dragging a slider.
- (void)readSettings {
    const BOOL dragging = [NSEvent pressedMouseButtons] != 0;
    for (NSUInteger i = 0; i < settings_.count; ++i) {
        ConvolverSettingGetter get = settings_[i][@"get"];
        const double value = get ? get() : NAN;
        if (std::isnan(value)) {
            _settingSliders[i].enabled = NO;
            _settingValues[i].stringValue = @"";
            continue;
        }
        _settingSliders[i].enabled = YES;
        if (!dragging && _settingSliders[i].doubleValue != value)
            _settingSliders[i].doubleValue = value;
        [self show:(dragging ? _settingSliders[i].doubleValue : value) at:i];
    }
}

- (void)refresh {
    [self readSettings];
    NSDictionary<NSString*, NSString*>* report = report_ ? report_() : nil;
    NSString *explanation, *details;
    if (!report) {
        _titleField.stringValue = @"Convolver";
        _statusField.stringValue = @"Not available";
        _statusField.textColor = [NSColor labelColor];
        explanation = @"Convolver could not read its report from the plug-in. The Convolver log "
                      @"has the details.";
        details = @"";
        logPath_ = nil;
    } else {
        _titleField.stringValue = field(report, @"title");
        _statusField.stringValue = field(report, @"status name");
        _statusField.textColor = status_colour(field(report, @"status").integerValue);
        _statusField.toolTip = field(report, @"summary");
        explanation = field(report, @"explanation");
        details = field(report, @"details");
        logPath_ = field(report, @"log path");
    }
    _logButton.enabled = logPath_.length > 0;
    NSString* text = [NSString stringWithFormat:@"%@\n\n%@", explanation, details];
    if ([text isEqualToString:shown_])
        return;
    shown_ = text;
    [_explanationView.textStorage setAttributedString:styled(explanation, details)];
    [self fit];
}

- (BOOL)loadFile:(NSString*)path {
    const BOOL accepted = loader_ && path.length > 0 && loader_(path);
    [self refresh]; // The report says what plays now, and why a refused file was refused.
    return accepted;
}

// The system's file dialog, run modally: a sheet would attach to the host's
// window, which a plug-in does not own.
- (void)chooseFilter:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message =
        @"Choose a Convolver configuration (.cfg) or a sound file to use as the filter.";
    panel.prompt = @"Load";
    // Not runModal: a modal loop inside the plug-in would stall the host's
    // own window events (plan, threads rule 4). The window may close before
    // the panel does, so the handler holds it weakly.
    __weak CONVOLVER_DIAGNOSTIC_VIEW* weakSelf = self;
    [panel beginWithCompletionHandler:^(NSModalResponse response) {
      CONVOLVER_DIAGNOSTIC_VIEW* view = weakSelf;
      if (!view || response != NSModalResponseOK || !panel.URL.fileURL)
          return;
      if (![view loadFile:panel.URL.path])
          NSBeep();
    }];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return loader_ && dropped_file(sender) ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSURL* file = dropped_file(sender);
    if (!file)
        return NO;
    if (![self loadFile:file.path])
        NSBeep();
    return YES; // Taken, even if refused: the window says why.
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
