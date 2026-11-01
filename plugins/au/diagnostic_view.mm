// SPDX-License-Identifier: GPL-2.0-or-later
#import "diagnostic_view.h"
#include "host_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <locale.h>
#include <xlocale.h>

namespace {
// The usual macOS panel layout (jrp's GarageBand run: the first version
// filled whatever size the host offered and left most of it blank): a fixed
// width and a height that fits the content. One column of stack views, top
// to bottom in reading and Tab order; labels of the form right-aligned with
// a colon, as Apple's forms are.
constexpr CGFloat width = 560;
constexpr CGFloat margin = 20;
constexpr CGFloat inner = width - 2 * margin;
constexpr CGFloat form_label = 64;  // "Latency:" and the other form labels.
constexpr CGFloat fact_label = 104; // "Filter length" and the other Audio labels.
constexpr CGFloat value_field = 56; // A setting's typed value.
constexpr CGFloat most_text =
    200; // A longer explanation scrolls (a list .cfg explains each entry).
constexpr NSTimeInterval refresh_interval = 0.5;

NSString* field(NSDictionary<NSString*, NSString*>* report, NSString* key) {
    NSString* value = report[key];
    return [value isKindOfClass:[NSString class]] ? value : @"";
}

// The status as a symbol and a colour; the status word is always written
// beside them, so neither is the only signal. Values are HostStatus numbers.
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
NSString* status_symbol(NSInteger status) {
    switch (status) {
    case 0:
        return @"checkmark.circle.fill";
    case 2:
        return @"xmark.octagon.fill";
    case 3:
        return @"clock";
    case 4:
        return @"exclamationmark.triangle.fill";
    default:
        return @"circle.dashed";
    }
}

NSFont* body_font() {
    return [NSFont systemFontOfSize:[NSFont systemFontSize]];
}
NSFont* small_font() {
    return [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
}

// A one-line label the user can select (to copy a value).
NSTextField* label(NSFont* font, NSColor* colour) {
    NSTextField* text = [NSTextField labelWithString:@""];
    text.font = font;
    text.textColor = colour;
    text.selectable = YES;
    text.lineBreakMode = NSLineBreakByTruncatingTail;
    text.translatesAutoresizingMaskIntoConstraints = NO;
    return text;
}
// A label that wraps within `wide` points.
NSTextField* wrapping(NSFont* font, NSColor* colour, CGFloat wide) {
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

NSStackView* stack(NSArray<NSView*>* views, NSUserInterfaceLayoutOrientation orientation,
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
void fixed_width(NSView* view, CGFloat wide) {
    view.translatesAutoresizingMaskIntoConstraints = NO;
    [view.widthAnchor constraintEqualToConstant:wide].active = YES;
}
// A view that takes the room its row leaves.
void stretches(NSView* view) {
    [view setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    [view setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                   forOrientation:NSLayoutConstraintOrientationHorizontal];
}
NSBox* separator() {
    NSBox* line = [[NSBox alloc] init];
    line.boxType = NSBoxSeparator;
    fixed_width(line, inner);
    return line;
}

// One Audio fact's value: the value, then its note in the secondary colour,
// or a problem note in red after a warning sign, so the words say it too.
NSAttributedString* fact_text(NSString* value, NSString* note, BOOL problem) {
    NSMutableAttributedString* text = [[NSMutableAttributedString alloc]
        initWithString:value
            attributes:@{
                NSFontAttributeName : body_font(),
                NSForegroundColorAttributeName : [NSColor labelColor]
            }];
    if (note.length == 0)
        return text;
    NSString* shown =
        problem ? [@"  ⚠︎ " stringByAppendingString:note] : [@" " stringByAppendingString:note];
    [text appendAttributedString:[[NSAttributedString alloc]
                                     initWithString:shown
                                         attributes:@{
                                             NSFontAttributeName : body_font(),
                                             NSForegroundColorAttributeName : problem
                                                 ? [NSColor systemRedColor]
                                                 : [NSColor secondaryLabelColor]
                                         }]];
    return text;
}

// The file, software and log lines: each "Label: value" as a row, the label
// before a tab stop, wrapped lines indented under the value.
NSAttributedString* reference_text(NSString* lines) {
    NSMutableParagraphStyle* row = [[NSMutableParagraphStyle alloc] init];
    row.tabStops = @[ [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentLeft
                                                      location:fact_label
                                                       options:@{}] ];
    row.headIndent = fact_label;
    row.paragraphSpacing = 2;
    NSDictionary* name_style = @{
        NSFontAttributeName : small_font(),
        NSForegroundColorAttributeName : [NSColor secondaryLabelColor],
        NSParagraphStyleAttributeName : row
    };
    NSDictionary* value_style = @{
        NSFontAttributeName : small_font(),
        NSForegroundColorAttributeName : [NSColor labelColor],
        NSParagraphStyleAttributeName : row
    };
    NSMutableAttributedString* text = [[NSMutableAttributedString alloc] init];
    for (NSString* line in [lines componentsSeparatedByString:@"\n"]) {
        if (line.length == 0)
            continue;
        const NSRange colon = [line rangeOfString:@": "];
        NSString* name =
            colon.location == NSNotFound ? @"" : [line substringToIndex:colon.location];
        NSString* value =
            colon.location == NSNotFound ? line : [line substringFromIndex:NSMaxRange(colon)];
        if (text.length)
            [text appendAttributedString:[[NSAttributedString alloc] initWithString:@"\n"
                                                                         attributes:value_style]];
        [text appendAttributedString:[[NSAttributedString alloc]
                                         initWithString:[name stringByAppendingString:@"\t"]
                                             attributes:name_style]];
        [text appendAttributedString:[[NSAttributedString alloc] initWithString:value
                                                                     attributes:value_style]];
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

// The typed boxes tell the view when the user first changes their text.
@interface CONVOLVER_DIAGNOSTIC_VIEW () <NSTextFieldDelegate>
@end

@implementation CONVOLVER_DIAGNOSTIC_VIEW {
    ConvolverReportSource report_;
    ConvolverDiagnosticsSource diagnostics_;
    ConvolverFileLoader loader_;
    NSStackView* stack_; // Everything, top to bottom.
    NSScrollView* scroll_;
    NSLayoutConstraint* scrollHeight_;
    NSStackView* audio_; // The Audio facts' rows, rebuilt when they change.
    NSTimer* timer_;
    NSString* shown_; // The text last shown, so a refresh keeps a selection.
    NSString* logPath_;
    NSArray<NSDictionary<NSString*, id>*>* settings_;
    NSArray<NSControl*>* controls_; // Each setting's slider or menu, in settings_ order.
    NSInteger dragging_;            // The slider the user is dragging, or -1.
    NSInteger typed_;               // The box whose text the user has changed, or -1.
    int sampleRate_;                // The host's, for the Latency row; 0 before it starts.
    BOOL latencyWaits_;             // The chosen Latency is not in use yet.
    NSUInteger choosing_,
        requests_;       // The pending Choose… request (0: none), the last number.
    NSOpenPanel* panel_; // Its dialog, while one is open.
}

- (instancetype)initWithFrame:(NSRect)frame
                       report:(ConvolverReportSource)report
                  diagnostics:(ConvolverDiagnosticsSource)diagnostics
                     settings:(NSArray<NSDictionary<NSString*, id>*>*)settings
                       loader:(ConvolverFileLoader)loader {
    // The view's own size: the frame gives only its position.
    self = [super initWithFrame:NSMakeRect(NSMinX(frame), NSMinY(frame), width, 400)];
    if (!self)
        return nil;
    _pasteboard = [NSPasteboard generalPasteboard];
    report_ = [report copy];
    diagnostics_ = [diagnostics copy];
    loader_ = [loader copy];
    dragging_ = -1;
    typed_ = -1;

    // The stack is pinned at the top left and has the window's width; its
    // own height is the content's, and fit gives the view that height. No
    // bottom constraint, so the frame a host sets never conflicts with it.
    stack_ = stack(@[], NSUserInterfaceLayoutOrientationVertical, 8);
    stack_.edgeInsets = NSEdgeInsetsMake(14, margin, margin, margin);
    [self addSubview:stack_];
    [NSLayoutConstraint activateConstraints:@[
        [stack_.topAnchor constraintEqualToAnchor:self.topAnchor],
        [stack_.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [stack_.widthAnchor constraintEqualToConstant:width]
    ]];

    // Which instance this is, then the status: symbol, word, and under them
    // what plays, why and what to change.
    _titleField = label(small_font(), [NSColor secondaryLabelColor]);
    fixed_width(_titleField, inner);
    [self add:_titleField after:4];

    _statusSymbol = [[NSImageView alloc] init];
    _statusSymbol.translatesAutoresizingMaskIntoConstraints = NO;
    _statusSymbol.imageScaling = NSImageScaleProportionallyUpOrDown;
    _statusSymbol.accessibilityElement = NO; // The status word says it.
    [NSLayoutConstraint activateConstraints:@[
        [_statusSymbol.widthAnchor constraintEqualToConstant:22],
        [_statusSymbol.heightAnchor constraintEqualToConstant:22]
    ]];
    _statusField = label([NSFont boldSystemFontOfSize:17], [NSColor labelColor]);
    _statusField.accessibilityLabel = @"Status";
    [self add:stack(@[ _statusSymbol, _statusField ], NSUserInterfaceLayoutOrientationHorizontal, 8)
        after:4];

    // No border or box: the text reads as part of the panel. It scrolls only
    // when it is longer than most_text.
    scroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, inner, 40)];
    scroll_.hasVerticalScroller = YES;
    scroll_.autohidesScrollers = YES;
    scroll_.borderType = NSNoBorder;
    scroll_.drawsBackground = NO;
    fixed_width(scroll_, inner);
    scrollHeight_ = [scroll_.heightAnchor constraintEqualToConstant:40];
    scrollHeight_.active = YES;
    _explanationView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, inner, 40)];
    _explanationView.editable = NO;
    _explanationView.selectable = YES;
    _explanationView.richText = NO;
    _explanationView.font = body_font();
    _explanationView.textColor = [NSColor labelColor];
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
    [self add:scroll_ after:12];
    [self add:separator() after:12];

    // The form. Filter: the file's name with Choose… beside it, as
    // convolution reverbs show their impulse response; a file dropped
    // anywhere on the window does the same.
    NSImageView* document = [[NSImageView alloc] init];
    document.image = [NSImage imageWithSystemSymbolName:@"doc" accessibilityDescription:nil];
    document.contentTintColor = [NSColor secondaryLabelColor];
    document.accessibilityElement = NO;
    fixed_width(document, 16);
    _filterNameField = label([NSFont systemFontOfSize:[NSFont systemFontSize]
                                               weight:NSFontWeightSemibold],
                             [NSColor labelColor]);
    _filterNameField.accessibilityLabel = @"Filter";
    _filterNoteField = wrapping(small_font(), [NSColor secondaryLabelColor], 300);
    NSStackView* names =
        stack(@[ _filterNameField, _filterNoteField ], NSUserInterfaceLayoutOrientationVertical, 1);
    stretches(names);
    _chooseButton = [NSButton buttonWithTitle:@"Choose…"
                                       target:self
                                       action:@selector(chooseFilter:)];
    _chooseButton.accessibilityLabel = @"Choose Filter";
    _chooseButton.toolTip =
        @"Loads a Convolver configuration (.cfg) or a sound file as the filter; you can also "
        @"drop the file on this window. The project keeps the filter itself, as a preset does, "
        @"so moving the file later does not change it.";
    _chooseButton.hidden = loader_ == nil;
    NSStackView* filter_row =
        stack(@[ document, names, _chooseButton ], NSUserInterfaceLayoutOrientationHorizontal, 8);
    filter_row.distribution = NSStackViewDistributionFill;
    [self add:[self formRow:@"Filter" content:filter_row] after:12];
    if (loader_)
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];

    // The settings. A host that shows this window shows it instead of its
    // generic controls (jrp's GarageBand run), so each is a full control:
    // a slider with its value typed beside it and its unit (Apple's generic
    // view, Logic), or a menu with the time it adds (Logic's buffer size).
    settings_ = [settings copy];
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
        [self add:[self formRow:setting[@"name"] content:content] after:12];
        [controls addObject:control];
        [values addObject:value];
    }
    controls_ = controls;
    _settingSliders = sliders;
    _settingMenus = menus;
    _settingValues = values;
    [stack_ setCustomSpacing:14 afterView:stack_.arrangedSubviews.lastObject];
    [self add:separator() after:12];

    // The Audio facts: what plays, in plain words, always shown.
    NSTextField* heading = label([NSFont systemFontOfSize:[NSFont systemFontSize]
                                                   weight:NSFontWeightSemibold],
                                 [NSColor labelColor]);
    heading.stringValue = @"Audio";
    heading.selectable = NO;
    [self add:heading after:4];
    audio_ = stack(@[], NSUserInterfaceLayoutOrientationVertical, 3);
    fixed_width(audio_, inner);
    [self add:audio_ after:12];
    _audioLabels = @[];
    _audioValues = @[];

    // Reference material for problem reports, folded away and smaller.
    _referenceButton = [[NSButton alloc] initWithFrame:NSZeroRect];
    _referenceButton.bezelStyle = NSBezelStyleDisclosure;
    [_referenceButton setButtonType:NSButtonTypePushOnPushOff];
    _referenceButton.title = @"";
    _referenceButton.target = self;
    _referenceButton.action = @selector(referenceToggled:);
    _referenceButton.accessibilityLabel = @"File, software and log";
    NSTextField* caption = label(small_font(), [NSColor secondaryLabelColor]);
    caption.stringValue = @"File, software and log";
    caption.selectable = NO;
    [self add:stack(@[ _referenceButton, caption ], NSUserInterfaceLayoutOrientationHorizontal, 4)
        after:4];
    _referenceField = wrapping(small_font(), [NSColor secondaryLabelColor], inner);
    _referenceField.allowsEditingTextAttributes = YES; // Keeps the tab stops when selected.
    _referenceField.hidden = YES;
    fixed_width(_referenceField, inner);
    [self add:_referenceField after:8];

    // The buttons at the bottom right, Show Log rightmost.
    _diagnosticsButton = [NSButton buttonWithTitle:@"Copy Diagnostics"
                                            target:self
                                            action:@selector(putDiagnosticsOnPasteboard:)];
    _diagnosticsButton.toolTip =
        @"Copies this report and Convolver's recent log lines, ready to paste "
        @"into a problem report.";
    _logButton = [NSButton buttonWithTitle:@"Show Log" target:self action:@selector(showLog:)];
    _logButton.toolTip = @"Shows the Convolver log file in the Finder.";
    NSStackView* buttons = stack(@[], NSUserInterfaceLayoutOrientationHorizontal, 12);
    [buttons addView:_diagnosticsButton inGravity:NSStackViewGravityTrailing];
    [buttons addView:_logButton inGravity:NSStackViewGravityTrailing];
    fixed_width(buttons, inner);
    [self add:buttons after:0];

    [self refresh];
    return self;
}

// Adds `view` under the others, with `space` points before the next.
- (void)add:(NSView*)view after:(CGFloat)space {
    [stack_ addArrangedSubview:view];
    [stack_ setCustomSpacing:space afterView:view];
}

// A row of the form: "Name:" right-aligned, then `content` across the rest.
- (NSStackView*)formRow:(NSString*)name content:(NSView*)content {
    NSTextField* title = label(body_font(), [NSColor labelColor]);
    title.stringValue = [name stringByAppendingString:@":"];
    title.alignment = NSTextAlignmentRight;
    title.selectable = NO;
    fixed_width(title, form_label);
    stretches(content);
    NSStackView* row = stack(@[ title, content ], NSUserInterfaceLayoutOrientationHorizontal, 10);
    row.distribution = NSStackViewDistributionFill;
    fixed_width(row, inner);
    return row;
}

// Fits the height to the content (the explanation up to most_text, then it
// scrolls). Hosts that follow the view's size resize the window.
- (void)fit {
    NSLayoutManager* layout = _explanationView.layoutManager;
    [layout ensureLayoutForTextContainer:_explanationView.textContainer];
    const CGFloat text =
        ceil(NSHeight([layout usedRectForTextContainer:_explanationView.textContainer]));
    scrollHeight_.constant = MIN(MAX(text, 17), most_text);
    [stack_ layoutSubtreeIfNeeded];
    [self setFrameSize:NSMakeSize(width, ceil(stack_.fittingSize.height))];
    [self layoutSubtreeIfNeeded];
}

- (BOOL)isFlipped {
    return YES; // Laid out from the top.
}

- (void)dealloc {
    [timer_ invalidate];
    [panel_ cancel:nil];
}

// Forgets a pending Choose… request and closes its dialog.
- (void)stopChoosing {
    choosing_ = 0;
    NSOpenPanel* panel = panel_;
    panel_ = nil;
    [panel cancel:nil];
}

- (void)windowWillClose:(NSNotification*)notification {
    static_cast<void>(notification);
    [self stopChoosing];
}

// The timer runs while the view is in a window and reads only while that
// window is on screen (refreshIfShown): the host may dispose of the unit once
// it has closed the window, which need not remove the view.
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [timer_ invalidate];
    timer_ = nil;
    // A file chosen for another window, or none, is not loaded.
    [self stopChoosing];
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSWindowWillCloseNotification
                                                  object:nil];
    if (!self.window)
        return;
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(windowWillClose:)
                                                 name:NSWindowWillCloseNotification
                                               object:self.window];
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

- (BOOL)shown {
    NSWindow* window = self.window;
    return window.visible && (window.occlusionState & NSWindowOcclusionStateVisible);
}

- (BOOL)refreshIfShown {
    if (![self shown])
        return NO;
    [self refresh];
    return YES;
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
        _settingValues[i].stringValue = [self latencyNote:menu];
        return;
    }
    NSString* number = [self number:value of:settings_[i]];
    if (!_settingValues[i].currentEditor)
        _settingValues[i].stringValue = number;
    controls_[i].accessibilityValueDescription =
        [number stringByAppendingString:settings_[i][@"unit"]];
}

// Passes `value` for setting `i` to the host and shows it.
- (void)set:(double)value at:(NSUInteger)i {
    ConvolverSettingSetter set = settings_[i][@"set"];
    if (set)
        set(value);
    [self show:value at:i];
}

// The user moved or reset slider `i`: a value half typed in its field is
// dropped, so that leaving the field later cannot put it back (jrp's
// GarageBand run of 1 November 2026: Option-click reset Gain, then leaving
// the field restored the typed -3.5).
- (void)dropTyping:(NSUInteger)i {
    typed_ = -1;
    NSTextField* field = _settingValues[i];
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
    [self refresh]; // The report says when the new choice takes effect.
}

// The user moved a slider: the format passes the value to the host. While
// the mouse drags this slider, readSettings leaves it where the user puts
// it. Option-click resets it to its default, as in Logic and Apple's
// generic view (the whole click: the release would move it again).
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
    if (mouse && (event.modifierFlags & NSEventModifierFlagOption)) {
        [self resetSetting:i];
        return;
    }
    [self dropTyping:i];
    [self set:slider.doubleValue at:i];
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

// The settings as the host has them now (automation, its own controls),
// except the slider the user is dragging (jrp's review: a button held
// anywhere else must not freeze them).
- (void)readSettings {
    if (dragging_ >= 0 && [NSEvent pressedMouseButtons] == 0)
        dragging_ = -1; // The drag ended without a final action.
    for (NSUInteger i = 0; i < settings_.count; ++i) {
        const BOOL dragging = dragging_ == static_cast<NSInteger>(i);
        ConvolverSettingGetter get = settings_[i][@"get"];
        const double value = get ? get() : NAN;
        if (std::isnan(value)) {
            controls_[i].enabled = NO;
            _settingValues[i].enabled = NO;
            _settingValues[i].stringValue = @"";
            continue;
        }
        controls_[i].enabled = YES;
        _settingValues[i].enabled = YES;
        if ([controls_[i] isKindOfClass:[NSPopUpButton class]]) {
            [self show:value at:i];
            continue;
        }
        if (!dragging && controls_[i].doubleValue != value)
            controls_[i].doubleValue = value;
        [self show:(dragging ? controls_[i].doubleValue : value) at:i];
    }
}

// Rebuilds the Audio rows from the report's "audio" field: one line per
// fact, label, value, note, "problem" or empty, and help, between tabs.
- (void)showAudio:(NSString*)lines {
    for (NSView* row in [audio_.arrangedSubviews copy]) {
        [audio_ removeArrangedSubview:row];
        [row removeFromSuperview];
    }
    NSMutableArray<NSTextField*>* labels = [NSMutableArray array];
    NSMutableArray<NSTextField*>* values = [NSMutableArray array];
    for (NSString* line in [lines componentsSeparatedByString:@"\n"]) {
        NSArray<NSString*>* parts = [line componentsSeparatedByString:@"\t"];
        if (parts.count < 5)
            continue;
        NSTextField* name = label(body_font(), [NSColor secondaryLabelColor]);
        name.stringValue = parts[0];
        name.toolTip = parts[4];
        name.selectable = NO;
        fixed_width(name, fact_label);
        NSTextField* value = wrapping(body_font(), [NSColor labelColor], inner - fact_label - 10);
        value.allowsEditingTextAttributes = YES; // Keeps the note's colour when selected.
        value.attributedStringValue =
            fact_text(parts[1], parts[2], [parts[3] isEqualToString:@"problem"]);
        value.toolTip = parts[4];
        value.accessibilityLabel = parts[0];
        stretches(value);
        NSStackView* row = stack(@[ name, value ], NSUserInterfaceLayoutOrientationHorizontal, 10);
        row.alignment = NSLayoutAttributeFirstBaseline;
        row.distribution = NSStackViewDistributionFill;
        fixed_width(row, inner);
        [audio_ addArrangedSubview:row];
        [labels addObject:name];
        [values addObject:value];
    }
    _audioLabels = labels;
    _audioValues = values;
}

- (void)refresh {
    NSDictionary<NSString*, NSString*>* report = report_ ? report_() : nil;
    NSString *headline, *audio, *reference, *name, *note;
    if (!report) {
        _titleField.stringValue = @"Convolver";
        _statusField.stringValue = @"Not available";
        _statusField.textColor = [NSColor labelColor];
        _statusSymbol.image = nil;
        headline = @"Convolver could not read its report from the plug-in. The Convolver log "
                   @"has the details.";
        audio = reference = name = note = @"";
        logPath_ = nil;
        sampleRate_ = 0;
        latencyWaits_ = NO;
    } else {
        const NSInteger status = field(report, @"status").integerValue;
        _titleField.stringValue = field(report, @"title");
        _statusField.stringValue = field(report, @"status name");
        _statusField.textColor = status_colour(status);
        _statusSymbol.image = [NSImage imageWithSystemSymbolName:status_symbol(status)
                                        accessibilityDescription:nil];
        _statusSymbol.contentTintColor = status == 0 || status == 2 || status == 4
                                             ? status_colour(status)
                                             : [NSColor secondaryLabelColor];
        headline = field(report, @"headline");
        audio = field(report, @"audio");
        reference = field(report, @"reference");
        name = field(report, @"filter name");
        note = field(report, @"filter note");
        logPath_ = field(report, @"log path");
        sampleRate_ = field(report, @"sample rate").intValue;
        latencyWaits_ = [field(report, @"latency waits") isEqualToString:@"yes"];
    }
    [self readSettings]; // After the rate and Latency's wait, which its row shows.
    _logButton.enabled = logPath_.length > 0;
    if (loader_)
        note = note.length ? [note stringByAppendingString:@" Or drop a file on this window."]
                           : @"Or drop a .cfg or sound file on this window.";
    NSString* text = [@[ headline, name, note, audio, reference ] componentsJoinedByString:@"\v"];
    if ([text isEqualToString:shown_])
        return;
    shown_ = text;
    _explanationView.string = headline;
    _explanationView.font = body_font(); // Plain text: the view's own font and colour.
    _explanationView.textColor = [NSColor labelColor];
    _filterNameField.stringValue = name;
    _filterNoteField.stringValue = note;
    [self showAudio:audio];
    _referenceField.attributedStringValue = reference_text(reference);
    [self fit];
}

- (void)referenceToggled:(NSButton*)button {
    [self showReference:button.state == NSControlStateValueOn];
}

- (void)showReference:(BOOL)shown {
    _referenceButton.state = shown ? NSControlStateValueOn : NSControlStateValueOff;
    _referenceField.hidden = !shown;
    [self fit];
}

- (BOOL)loadFile:(NSString*)path {
    const BOOL accepted = loader_ && path.length > 0 && loader_(path);
    [self refresh]; // The report says what plays now, and why a refused file was refused.
    return accepted;
}

- (NSUInteger)startChoosing {
    [self stopChoosing];
    choosing_ = ++requests_;
    return choosing_;
}

- (BOOL)finishChoosing:(NSUInteger)request URL:(NSURL*)url {
    if (request == 0 || request != choosing_)
        return NO; // Forgotten: the window closed, or a newer request replaced it.
    choosing_ = 0;
    panel_ = nil;
    if (!url.fileURL || !self.window)
        return NO;
    if ([self loadFile:url.path])
        return YES;
    NSBeep();
    return NO;
}

// The system's file dialog, as a sheet on Convolver's window, presented
// asynchronously as Apple's file-panel guide describes
// (beginSheetModalForWindow:completionHandler:). In GarageBand, which runs
// the plug-in in a process of its own, a separate dialog window stayed
// behind Convolver's window and took no typing; the sheet takes the keys
// with Convolver's window (jrp's runs of bf7adb6 to abb2edf).
- (void)chooseFilter:(id)sender {
    NSWindow* own = self.window;
    if (!own)
        return; // The button is in the window; nothing to attach to otherwise.
    const NSUInteger request = [self startChoosing]; // Ends an earlier sheet.
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel_ = panel;
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message =
        @"Choose a Convolver configuration (.cfg) or a sound file to use as the filter.";
    panel.prompt = @"Load";
    // Not runModal: a modal loop inside the plug-in would stall the host's
    // own window events (plan, threads rule 4). The view may go before the
    // panel does, so the handler holds it weakly; and the window may close
    // first, which forgets the request and ends the sheet (stopChoosing).
    __weak CONVOLVER_DIAGNOSTIC_VIEW* weakSelf = self;
    __weak NSOpenPanel* weakPanel = panel;
    [panel beginSheetModalForWindow:own
                  completionHandler:^(NSModalResponse response) {
                    CONVOLVER_DIAGNOSTIC_VIEW* view = weakSelf;
                    NSURL* url = response == NSModalResponseOK ? weakPanel.URL : nil;
                    if (view)
                        [view finishChoosing:request URL:url];
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
                     weakSelf.diagnosticsButton.title = @"Copy Diagnostics";
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
