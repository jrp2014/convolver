// SPDX-License-Identifier: GPL-2.0-or-later
#import "diagnostic_view.h"
#import "file_watch.h"
#include "recent_files.hpp"
#import "view_layout.h"
#include <cmath>

using namespace convolver::view;

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
constexpr CGFloat fact_label = 120; // "Host sample rate" and the other Audio labels.
constexpr CGFloat icon = 18;        // The document symbol before the filter's name.
constexpr CGFloat padding = 10;     // Inside the box round a problem.
constexpr CGFloat text_width = inner - 2 * padding;
constexpr CGFloat most_text =
    200; // A longer explanation scrolls (a list .cfg explains each entry).
constexpr NSTimeInterval refresh_interval = 0.5;
constexpr unsigned ticks_per_check = 2; // Files are checked once a second.

NSString* field(NSDictionary<NSString*, NSString*>* report, NSString* key) {
    NSString* value = report[key];
    return [value isKindOfClass:[NSString class]] ? value : @"";
}

// The status as a symbol and a colour; the status word is always written
// beside them, so neither is the only signal. Values are HostStatus numbers.
NSColor* status_colour(NSInteger status) {
    switch (status) {
    case 0: // Active
        return [NSColor systemGreenColor];
    case 2: // Load failed
        return [NSColor systemRedColor];
    case 4: // Fault
        return [NSColor systemOrangeColor];
    default: // No filter, Not checked yet, Bypassed
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
    case 5:
        return @"pause.circle";
    default:
        return @"circle.dashed";
    }
}
// Whether the status has a colour of its own (the others are drawn in the
// secondary colour).
bool coloured(NSInteger status) {
    return status == 0 || status == 2 || status == 4;
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

// DIAG-2: the message under the status word (docs/use/plugins.md#convolvers-window):
// the problem in semibold, then what plays, then the cause and the fix as rows
// whose labels sit right-aligned in the form's label column, as Filter: and
// Gain: do, the Fix label in bold as the part to act on. Empty parts are left out.
NSAttributedString* message_text(NSString* problem, NSString* plays, NSString* cause,
                                 NSString* fix) {
    NSMutableParagraphStyle* plain = [[NSMutableParagraphStyle alloc] init];
    plain.paragraphSpacing = 4;
    NSMutableParagraphStyle* row = [[NSMutableParagraphStyle alloc] init];
    row.tabStops = @[
        [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentRight
                                        location:form_label
                                         options:@{}],
        [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentLeft
                                        location:form_label + 10
                                         options:@{}]
    ];
    row.headIndent = form_label + 10;
    row.paragraphSpacing = 4;
    NSFont* semibold = [NSFont systemFontOfSize:[NSFont systemFontSize]
                                         weight:NSFontWeightSemibold];
    NSMutableAttributedString* text = [[NSMutableAttributedString alloc] init];
    const auto add = [&](NSString* part, NSFont* font, NSColor* colour, NSParagraphStyle* style) {
        [text appendAttributedString:[[NSAttributedString alloc]
                                         initWithString:part
                                             attributes:@{
                                                 NSFontAttributeName : font,
                                                 NSForegroundColorAttributeName : colour,
                                                 NSParagraphStyleAttributeName : style
                                             }]];
    };
    const auto end_paragraph = [&](NSParagraphStyle* style) {
        if (text.length)
            add(@"\n", body_font(), [NSColor labelColor], style);
    };
    if (problem.length)
        add(problem, semibold, [NSColor labelColor], plain);
    if (plays.length) {
        if (problem.length)
            add(@" ", body_font(), [NSColor labelColor], plain);
        add(plays, body_font(), [NSColor labelColor], plain);
    }
    if (cause.length) {
        end_paragraph(row);
        add(@"\tCause:\t", body_font(), [NSColor secondaryLabelColor], row);
        add(cause, body_font(), [NSColor labelColor], row);
    }
    if (fix.length) {
        end_paragraph(row);
        add(@"\tFix:\t", [NSFont boldSystemFontOfSize:[NSFont systemFontSize]],
            [NSColor labelColor], row);
        add(fix, body_font(), [NSColor labelColor], row);
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

// The Recent menu asks the view to fill it as it opens.
@interface CONVOLVER_DIAGNOSTIC_VIEW () <NSMenuDelegate>
@end

@implementation CONVOLVER_DIAGNOSTIC_VIEW {
    ConvolverReportSource report_;
    ConvolverDiagnosticsSource diagnostics_;
    ConvolverFileLoader loader_;
    NSDictionary<NSString*, id>* bypass_; // The format's bypass (diagnostic_view.h), or nil.
    NSStackView* stack_;                  // Everything, top to bottom.
    NSScrollView* scroll_;
    NSLayoutConstraint* scrollHeight_;
    NSStackView* audio_;        // The Audio facts' rows, rebuilt when they change.
    NSBox* message_;            // The message, in a box when it is a problem; hidden when empty.
    NSStackView* messageStack_; // Its text, button and note.
    NSStackView* factsRow_;     // The facts under the name, hidden while they are not known.
    NSStackView* noteRow_;      // "Note:" and the note, hidden without one.
    NSStackView* actionRow_;    // The fix's button, hidden without one.
    NSStackView* mixRow_;       // Mix's row, whose label dims when Mix has no effect.
    NSStackView* mixNoteRow_;   // The mix note's row, hidden while Mix applies.
    NSUInteger mix_;            // Mix's index among the settings (NSNotFound: none).
    NSTimer* timer_;
    NSString* shown_; // The text last shown, so a refresh keeps a selection.
    NSString* logPath_;
    CONVOLVER_SETTING_ROWS* settings_; // Gain, Mix and Latency, and what the user does to them.
    NSUInteger choosing_,
        requests_;       // The pending Choose… request (0: none), the last number.
    NSOpenPanel* panel_; // Its dialog, while one is open.
    // Reload: the playing filter's file and the files it was read from
    // (file_watch.h); timer ticks, for the checks.
    FileWatch watch_;
    unsigned ticks_;
}

- (instancetype)initWithFrame:(NSRect)frame
                       report:(ConvolverReportSource)report
                  diagnostics:(ConvolverDiagnosticsSource)diagnostics
                     settings:(NSArray<NSDictionary<NSString*, id>*>*)settings
                       bypass:(NSDictionary<NSString*, id>*)bypass
                       loader:(ConvolverFileLoader)loader {
    // The view's own size: the frame gives only its position.
    self = [super initWithFrame:NSMakeRect(NSMinX(frame), NSMinY(frame), width, 400)];
    if (!self)
        return nil;
    _pasteboard = [NSPasteboard generalPasteboard];
    report_ = [report copy];
    diagnostics_ = [diagnostics copy];
    loader_ = [loader copy];
    bypass_ = [bypass copy];

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

    // Which instance this is, with Bypass at the trailing end of the same
    // line (jrp, 5 November 2026: nothing on the window's bottom edge).
    _titleField = label(small_font(), [NSColor secondaryLabelColor]);
    stretches(_titleField);
    _bypassButton = [NSButton checkboxWithTitle:@"Bypass"
                                         target:self
                                         action:@selector(bypassClicked:)];
    _bypassButton.toolTip = @"Lets the audio through without the filter, as the host's own "
                            @"bypass does; the host records the change like any other.";
    _bypassButton.hidden = bypass_ == nil;
    NSStackView* title_row = stack(@[], NSUserInterfaceLayoutOrientationHorizontal, 8);
    [title_row addView:_titleField inGravity:NSStackViewGravityLeading];
    [title_row addView:_bypassButton inGravity:NSStackViewGravityTrailing];
    fixed_width(title_row, inner);
    [self add:title_row after:6];

    // PLUG-22 (jrp, 6 November 2026): the heading is the filter's file name,
    // with its extension, and beside it the status as a symbol and a word
    // (Active, Bypassed, Load failed...). A long name loses its middle, so
    // the extension stays; its tooltip has all of it.
    NSImageView* document = [[NSImageView alloc] init];
    document.image = [NSImage imageWithSystemSymbolName:@"doc" accessibilityDescription:nil];
    document.contentTintColor = [NSColor secondaryLabelColor];
    document.imageScaling = NSImageScaleProportionallyUpOrDown;
    document.accessibilityElement = NO;
    fixed_width(document, icon);
    [document.heightAnchor constraintEqualToConstant:icon].active = YES;
    _filterNameField = label([NSFont boldSystemFontOfSize:17], [NSColor labelColor]);
    _filterNameField.lineBreakMode = NSLineBreakByTruncatingMiddle;
    _filterNameField.accessibilityLabel = @"Filter";
    [_filterNameField
        setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                 forOrientation:NSLayoutConstraintOrientationHorizontal];
    _statusSymbol = [[NSImageView alloc] init];
    _statusSymbol.translatesAutoresizingMaskIntoConstraints = NO;
    _statusSymbol.imageScaling = NSImageScaleProportionallyUpOrDown;
    _statusSymbol.accessibilityElement = NO; // The status word says it.
    [NSLayoutConstraint activateConstraints:@[
        [_statusSymbol.widthAnchor constraintEqualToConstant:16],
        [_statusSymbol.heightAnchor constraintEqualToConstant:16]
    ]];
    _statusField = label([NSFont systemFontOfSize:[NSFont systemFontSize]
                                           weight:NSFontWeightSemibold],
                         [NSColor labelColor]);
    _statusField.accessibilityLabel = @"Status";
    [_statusField setContentCompressionResistancePriority:NSLayoutPriorityRequired
                                           forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView* badge =
        stack(@[ _statusSymbol, _statusField ], NSUserInterfaceLayoutOrientationHorizontal, 4);
    NSStackView* name_row = stack(@[], NSUserInterfaceLayoutOrientationHorizontal, 6);
    [name_row addView:document inGravity:NSStackViewGravityLeading];
    [name_row addView:_filterNameField inGravity:NSStackViewGravityLeading];
    [name_row addView:badge inGravity:NSStackViewGravityTrailing];
    fixed_width(name_row, inner);
    [self add:name_row after:4];

    // jrp, 7 November 2026: the filter's own facts under its name, as
    // labelled columns (Sample rate, Channels, Length, Speakers) that start
    // at the name's text, rather than a run of values with gaps; the Audio
    // section gives the host's, which need not match. The speaker list, the
    // one long value, comes last and loses its end when it does not fit;
    // its tooltip has all of it.
    NSMutableArray<NSView*>* columns = [NSMutableArray array];
    NSMutableArray<NSTextField*>* values = [NSMutableArray array];
    NSView* facts_indent = [[NSView alloc] init];
    fixed_width(facts_indent, icon);
    [facts_indent.heightAnchor constraintEqualToConstant:1].active = YES;
    [columns addObject:facts_indent];
    for (NSString* title in @[ @"Sample rate", @"Channels", @"Length", @"Speakers" ]) {
        NSTextField* heading = label([NSFont systemFontOfSize:[NSFont smallSystemFontSize] - 1],
                                     [NSColor secondaryLabelColor]);
        heading.stringValue = title;
        heading.selectable = NO;
        heading.accessibilityElement = NO; // The value carries it.
        NSTextField* value =
            label([NSFont monospacedDigitSystemFontOfSize:[NSFont smallSystemFontSize]
                                                   weight:NSFontWeightRegular],
                  [NSColor labelColor]);
        value.accessibilityLabel = title;
        NSStackView* column =
            stack(@[ heading, value ], NSUserInterfaceLayoutOrientationVertical, 1);
        if ([title isEqualToString:@"Speakers"]) {
            stretches(value);
            stretches(column);
        }
        [columns addObject:column];
        [values addObject:value];
    }
    factsRow_ = stack(columns, NSUserInterfaceLayoutOrientationHorizontal, 22);
    factsRow_.alignment = NSLayoutAttributeTop;
    [factsRow_ setCustomSpacing:6 afterView:facts_indent];
    factsRow_.distribution = NSStackViewDistributionFill;
    fixed_width(factsRow_, inner);
    _glanceValues = values;
    [self add:factsRow_ after:6];
    // Under the name, a refused file and the drop hint, in the small font.
    _filterNoteField = wrapping(small_font(), [NSColor secondaryLabelColor], inner - icon - 6);
    NSView* note_indent = [[NSView alloc] init];
    fixed_width(note_indent, icon);
    [note_indent.heightAnchor constraintEqualToConstant:1].active = YES;
    [self add:stack(@[ note_indent, _filterNoteField ], NSUserInterfaceLayoutOrientationHorizontal,
                    6)
        after:10];

    // DIAG-2's message, shown only when there is something to say: the
    // problem, what plays, its cause and fix, the button that carries out
    // the fix and a note. A problem sits in a box tinted with the status
    // colour; Bypassed's line and a note are plain text. No scroller unless
    // it is longer than most_text.
    scroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, text_width, 40)];
    scroll_.hasVerticalScroller = YES;
    scroll_.autohidesScrollers = YES;
    scroll_.borderType = NSNoBorder;
    scroll_.drawsBackground = NO;
    fixed_width(scroll_, text_width);
    scrollHeight_ = [scroll_.heightAnchor constraintEqualToConstant:40];
    scrollHeight_.active = YES;
    _explanationView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, text_width, 40)];
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
    _explanationView.textContainer.containerSize = NSMakeSize(text_width, CGFLOAT_MAX);
    _explanationView.textContainer.widthTracksTextView = YES;
    _explanationView.accessibilityLabel = @"What Convolver plays, why, and what to do";
    // A file dropped on the text goes to the window, not into the text.
    [_explanationView unregisterDraggedTypes];
    scroll_.documentView = _explanationView;

    // The one button that carries out the fix, when the window can (Choose
    // Filter… or Copy Diagnostics), aligned with the fix's text; an
    // ordinary push button, so Return still goes to the host. Then the
    // note, in the small secondary font.
    _actionButton = [NSButton buttonWithTitle:@"" target:self action:nil];
    _actionButton.hidden = YES;
    NSView* action_indent = [[NSView alloc] init];
    fixed_width(action_indent, form_label);
    [action_indent.heightAnchor constraintEqualToConstant:1].active = YES;
    actionRow_ =
        stack(@[ action_indent, _actionButton ], NSUserInterfaceLayoutOrientationHorizontal, 10);
    actionRow_.hidden = YES;
    NSTextField* note_label = label(small_font(), [NSColor secondaryLabelColor]);
    note_label.stringValue = @"Note:";
    note_label.alignment = NSTextAlignmentRight;
    note_label.selectable = NO;
    fixed_width(note_label, form_label);
    _noteField =
        wrapping(small_font(), [NSColor secondaryLabelColor], text_width - form_label - 10);
    _noteField.accessibilityLabel = @"Note";
    stretches(_noteField);
    noteRow_ = stack(@[ note_label, _noteField ], NSUserInterfaceLayoutOrientationHorizontal, 10);
    noteRow_.alignment = NSLayoutAttributeFirstBaseline;
    noteRow_.distribution = NSStackViewDistributionFill;
    noteRow_.hidden = YES;
    fixed_width(noteRow_, text_width);
    NSStackView* message =
        stack(@[ scroll_, actionRow_, noteRow_ ], NSUserInterfaceLayoutOrientationVertical, 8);
    messageStack_ = message;
    message_ = [[NSBox alloc] init];
    message_.boxType = NSBoxCustom;
    message_.titlePosition = NSNoTitle;
    message_.cornerRadius = 6;
    message_.contentViewMargins = NSZeroSize;
    message_.translatesAutoresizingMaskIntoConstraints = NO;
    [message_.contentView addSubview:message];
    [NSLayoutConstraint activateConstraints:@[
        [message.topAnchor constraintEqualToAnchor:message_.contentView.topAnchor],
        [message.bottomAnchor constraintEqualToAnchor:message_.contentView.bottomAnchor],
        [message.leadingAnchor constraintEqualToAnchor:message_.contentView.leadingAnchor],
        [message.trailingAnchor constraintEqualToAnchor:message_.contentView.trailingAnchor]
    ]];
    fixed_width(message_, inner);
    [self add:message_ after:10];

    // Choose…, the recent files, Reload, and reloading when the file
    // changes (for someone refining a filter in another program), under the
    // heading; a file dropped anywhere on the window does what Choose… does.
    _chooseButton = [NSButton buttonWithTitle:@"Choose…"
                                       target:self
                                       action:@selector(chooseFilter:)];
    _chooseButton.accessibilityLabel = @"Choose Filter";
    _chooseButton.toolTip =
        @"Loads a Convolver configuration (.cfg) or a sound file as the filter; you can also "
        @"drop the file on this window. The project keeps the filter itself and its file's "
        @"name, as a preset does, so moving the file later does not change it.";
    _recentButton = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:YES];
    _recentButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_recentButton addItemWithTitle:@"Recent"];
    _recentButton.menu.delegate = self;
    _recentButton.menu.autoenablesItems = NO; // updateRecentMenu enables each item itself.
    _recentButton.accessibilityLabel = @"Recent Filters";
    _recentButton.toolTip = @"Filter files loaded recently in a Convolver window, in any host.";
    _reloadButton = [NSButton buttonWithTitle:@"Reload"
                                       target:self
                                       action:@selector(reloadFilter:)];
    _reloadButton.accessibilityLabel = @"Reload Filter";
    _reloadButton.toolTip = @"Reads the filter file again, for example after changing it in "
                            @"another program. Available for a file loaded in this window, "
                            @"not for a filter from the saved project or a preset.";
    _watchButton = [NSButton checkboxWithTitle:@"Reload when the file changes"
                                        target:self
                                        action:@selector(watchToggled:)];
    _watchButton.state = NSControlStateValueOn;
    _watchButton.toolTip = @"While this window is open, reads the filter again when its file, "
                           @"or a file a .cfg names, changes.";
    if (loader_) {
        NSStackView* file_row =
            stack(@[ _chooseButton, _recentButton, _reloadButton, _watchButton ],
                  NSUserInterfaceLayoutOrientationHorizontal, 8);
        [self add:[self formRow:@"" content:file_row] after:12];
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    [self updateRecentMenu];
    if (settings.count)
        [self add:separator() after:12];

    // The settings (setting_rows.h), each a row of the form.
    __weak CONVOLVER_DIAGNOSTIC_VIEW* weakSelf = self;
    settings_ = [[CONVOLVER_SETTING_ROWS alloc] initWithSettings:settings
                                                          chosen:^{
                                                            [weakSelf refresh];
                                                          }];
    mix_ = NSNotFound;
    _mixNoteField = wrapping(small_font(), [NSColor secondaryLabelColor], inner - form_label - 10);
    for (NSUInteger i = 0; i < settings_.rows.count; ++i) {
        NSStackView* row = [self formRow:settings[i][@"name"] content:settings_.rows[i]];
        [self add:row after:12];
        if ([settings[i][@"name"] isEqualToString:@"Mix"]) {
            // PLUG-23: with no dry signal, a note under Mix says why it has
            // no effect.
            mix_ = i;
            mixRow_ = row;
            mixNoteRow_ = [self formRow:@"" content:_mixNoteField];
            mixNoteRow_.hidden = YES;
            [stack_ setCustomSpacing:4 afterView:row];
            [self add:mixNoteRow_ after:12];
        }
    }
    _settingSliders = settings_.sliders;
    _settingMenus = settings_.menus;
    _settingValues = settings_.values;
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
    // Copy Diagnostics and Show Log on the same row as the section they
    // belong with, Show Log rightmost, rather than in a bar along the bottom.
    _diagnosticsButton = [NSButton buttonWithTitle:@"Copy Diagnostics"
                                            target:self
                                            action:@selector(putDiagnosticsOnPasteboard:)];
    _diagnosticsButton.toolTip =
        @"Copies this report and Convolver's recent log lines, ready to paste "
        @"into a problem report.";
    _logButton = [NSButton buttonWithTitle:@"Show Log" target:self action:@selector(showLog:)];
    _logButton.toolTip = @"Shows the Convolver log file in the Finder.";
    NSStackView* disclosure = stack(@[], NSUserInterfaceLayoutOrientationHorizontal, 4);
    [disclosure addView:_referenceButton inGravity:NSStackViewGravityLeading];
    [disclosure addView:caption inGravity:NSStackViewGravityLeading];
    [disclosure addView:_diagnosticsButton inGravity:NSStackViewGravityTrailing];
    [disclosure addView:_logButton inGravity:NSStackViewGravityTrailing];
    [disclosure setCustomSpacing:12 afterView:_diagnosticsButton];
    fixed_width(disclosure, inner);
    [self add:disclosure after:8];
    _referenceField = wrapping(small_font(), [NSColor secondaryLabelColor], inner);
    _referenceField.allowsEditingTextAttributes = YES; // Keeps the tab stops when selected.
    _referenceField.hidden = YES;
    fixed_width(_referenceField, inner);
    [self add:_referenceField after:0];

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
    title.stringValue = name.length ? [name stringByAppendingString:@":"] : @"";
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
    // Release the rows before NSView releases their controls, so nothing a
    // control does while it goes reaches them (their target is weak).
    settings_ = nil;
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
    // A file chosen for another window, or none, is not loaded, and a drag
    // the window no longer sees ends its edit.
    [self stopChoosing];
    [settings_ endGesture];
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
                                        [weakSelf tick];
                                      }];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
    [self refresh];
}

- (BOOL)shown {
    NSWindow* window = self.window;
    return window.visible && (window.occlusionState & NSWindowOcclusionStateVisible);
}

// The report while the window is on screen; the files once a second while
// it is open, even behind another window, since the program that changes
// them is then in front.
- (void)tick {
    [self refreshIfShown];
    if (++ticks_ % ticks_per_check == 0 && self.window.visible)
        [self checkForChanges];
}

- (BOOL)refreshIfShown {
    if (![self shown])
        return NO;
    [self refresh];
    return YES;
}

- (void)resetSetting:(NSUInteger)i {
    [settings_ resetSetting:i];
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
    NSString *problem, *plays, *cause, *fix, *advice, *action, *audio, *reference, *name, *note;
    NSInteger status = -1;
    if (!report) {
        _titleField.stringValue = @"Convolver";
        _statusField.stringValue = @"Not available";
        _statusField.textColor = [NSColor labelColor];
        _statusSymbol.image = nil;
        problem = @"Convolver could not read its report from the plug-in.";
        plays = cause = advice = action = @"";
        fix = @"Close this window and open it again. If it keeps happening, the Convolver log "
              @"has the details.";
        audio = reference = name = note = @"";
        logPath_ = nil;
    } else {
        status = field(report, @"status").integerValue;
        _titleField.stringValue = field(report, @"title");
        _statusField.stringValue = field(report, @"status name");
        _statusField.textColor = status_colour(status);
        _statusSymbol.image = [NSImage imageWithSystemSymbolName:status_symbol(status)
                                        accessibilityDescription:nil];
        _statusSymbol.contentTintColor =
            coloured(status) ? status_colour(status) : [NSColor secondaryLabelColor];
        problem = field(report, @"problem");
        plays = field(report, @"plays");
        cause = field(report, @"cause");
        fix = field(report, @"fix");
        advice = field(report, @"note");
        action = field(report, @"action");
        audio = field(report, @"audio");
        reference = field(report, @"reference");
        name = field(report, @"filter name");
        note = field(report, @"filter note");
        logPath_ = field(report, @"log path");
    }
    // The Latency row shows the time a choice adds at the host's rate, and
    // whether the chosen Latency is in use yet.
    [settings_ readWithSampleRate:field(report, @"sample rate").intValue
                     latencyWaits:[field(report, @"latency waits") isEqualToString:@"yes"]];
    _logButton.enabled = logPath_.length > 0;
    [self readBypass];
    watch_.follow(report ? field(report, @"filter path") : @"",
                  report ? field(report, @"filter files") : @"");
    _reloadButton.enabled = watch_.path().length > 0;
    _watchButton.enabled = watch_.path().length > 0;
    if (loader_)
        note = note.length ? [note stringByAppendingString:@" Or drop a file on this window."]
                           : @"Or drop a .cfg or sound file on this window.";
    // Choose Filter… only where the window can choose a file.
    if ([action isEqualToString:@"choose filter"] && !loader_)
        action = @"";
    NSString* mixNote = field(report, @"mix note");
    NSString* text = [@[
        @(status).stringValue, problem, plays, cause, fix, advice, action, name, note, audio,
        reference, field(report, @"filter rate"), field(report, @"filter channels"),
        field(report, @"filter length"), field(report, @"filter speakers"), mixNote
    ] componentsJoinedByString:@"\v"];
    if ([text isEqualToString:shown_])
        return;
    shown_ = text;
    [_explanationView.textStorage setAttributedString:message_text(problem, plays, cause, fix)];
    [self showAction:action];
    _noteField.stringValue = advice;
    noteRow_.hidden = advice.length == 0;
    [self showMessage:problem status:status];
    _filterNameField.stringValue = name;
    _filterNameField.toolTip = name.length ? name : nil;
    _filterNoteField.stringValue = note;
    [self showAudio:audio];
    [self showGlance:report];
    [self showMixNote:mixNote];
    _referenceField.attributedStringValue = reference_text(reference);
    [self fit];
}

// PLUG-23: Mix has no effect without a dry signal: its label and controls
// dim and the note under it says why (the mockup's Wider input state).
- (void)showMixNote:(NSString*)mixNote {
    if (mix_ == NSNotFound)
        return;
    const BOOL applies = mixNote.length == 0;
    _mixNoteField.stringValue = mixNote;
    mixNoteRow_.hidden = applies;
    for (NSSlider* slider in settings_.sliders)
        if ([slider isDescendantOf:settings_.rows[mix_]])
            slider.enabled = applies;
    settings_.values[mix_].enabled = applies;
    NSTextField* title = (NSTextField*)mixRow_.arrangedSubviews.firstObject;
    title.textColor = applies ? [NSColor labelColor] : [NSColor secondaryLabelColor];
}

// The facts under the name: the filter's own rate, channels, length and
// the speakers its outputs are labelled for (jrp, 7 November 2026), which
// the Audio section's host values need not match. Shown while a filter is
// loaded; the speakers column only when the filter names them.
- (void)showGlance:(NSDictionary<NSString*, NSString*>*)report {
    NSString* speakers = field(report, @"filter speakers");
    factsRow_.hidden = field(report, @"filter rate").length == 0;
    _glanceValues[0].stringValue = field(report, @"filter rate");
    _glanceValues[1].stringValue = field(report, @"filter channels");
    _glanceValues[2].stringValue = field(report, @"filter length");
    _glanceValues[2].toolTip =
        field(report, @"filter taps").length ? field(report, @"filter taps") : nil;
    _glanceValues[3].stringValue = speakers;
    _glanceValues[3].toolTip = speakers.length ? speakers : nil;
    _glanceValues[3].superview.hidden = speakers.length == 0;
}

// The message under the heading: hidden when there is nothing to say, in a
// box tinted with the status colour when it states a problem, and plain text
// otherwise (Bypassed's line, a note).
- (void)showMessage:(NSString*)problem status:(NSInteger)status {
    const BOOL text = _explanationView.string.length > 0;
    scroll_.hidden = !text;
    message_.hidden = !text && actionRow_.hidden && noteRow_.hidden;
    const BOOL boxed = problem.length > 0;
    NSColor* tint = coloured(status) ? status_colour(status) : [NSColor secondaryLabelColor];
    message_.fillColor = boxed ? [tint colorWithAlphaComponent:0.08] : [NSColor clearColor];
    message_.borderColor = boxed ? [tint colorWithAlphaComponent:0.4] : [NSColor clearColor];
    message_.borderWidth = boxed ? 1 : 0;
    messageStack_.edgeInsets =
        boxed ? NSEdgeInsetsMake(padding, padding, padding, padding) : NSEdgeInsetsMake(0, 0, 0, 0);
}

// The fix's button: Choose Filter… (as Choose…) or Copy Diagnostics, or none.
- (void)showAction:(NSString*)action {
    if ([action isEqualToString:@"choose filter"]) {
        _actionButton.title = @"Choose Filter…";
        _actionButton.action = @selector(chooseFilter:);
        _actionButton.toolTip = _chooseButton.toolTip;
    } else if ([action isEqualToString:@"copy diagnostics"]) {
        _actionButton.title = @"Copy Diagnostics";
        _actionButton.action = @selector(putDiagnosticsOnPasteboard:);
        _actionButton.toolTip = _diagnosticsButton.toolTip;
    }
    _actionButton.hidden = !([action isEqualToString:@"choose filter"] ||
                             [action isEqualToString:@"copy diagnostics"]);
    actionRow_.hidden = _actionButton.hidden;
}

- (BOOL)checkForChanges {
    if (!loader_ || _watchButton.state != NSControlStateValueOn || !watch_.due())
        return NO;
    // The host may have replaced the filter (a preset, an undo) while the
    // window was covered and not reading the report: reload only the file
    // that still plays.
    NSString* path = watch_.path();
    NSString* files = watch_.files();
    [self refresh];
    if (![watch_.path() isEqualToString:path] || ![watch_.files() isEqualToString:files])
        return NO;
    [self loadFile:path];
    return YES;
}

- (void)watchToggled:(NSButton*)button {
    static_cast<void>(button);
    watch_.settle(); // Changes made while it was off are not reloaded.
}

- (void)reloadFilter:(id)sender {
    static_cast<void>(sender);
    NSString* path = watch_.path();
    if (path.length == 0 || ![self loadFile:path])
        NSBeep();
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    if (menu == _recentButton.menu)
        [self updateRecentMenu];
}

// "room.cfg — Filters": the file's name and its folder's, since several
// folders often hold a filter.cfg; the full path is the item's tooltip.
- (void)updateRecentMenu {
    NSMenu* menu = _recentButton.menu;
    while (menu.numberOfItems > 1) // The first item is the button's title.
        [menu removeItemAtIndex:1];
    NSFileManager* files = [NSFileManager defaultManager];
    BOOL any = NO;
    for (const auto& file : convolver::recent_files()) {
        NSString* path = [NSString stringWithUTF8String:file.c_str()];
        if (!path || ![files fileExistsAtPath:path])
            continue;
        NSString* folder = path.stringByDeletingLastPathComponent.lastPathComponent;
        NSString* title =
            folder.length && ![folder isEqualToString:@"/"]
                ? [NSString stringWithFormat:@"%@ — %@", path.lastPathComponent, folder]
                : path.lastPathComponent;
        NSMenuItem* item = [menu addItemWithTitle:title
                                           action:@selector(recentChosen:)
                                    keyEquivalent:@""];
        item.target = self;
        item.representedObject = path;
        item.toolTip = path;
        any = YES;
    }
    if (!any) {
        [menu addItemWithTitle:@"No Recent Filters" action:nil keyEquivalent:@""].enabled = NO;
        return;
    }
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Clear Menu" action:@selector(clearRecent:) keyEquivalent:@""].target =
        self;
}

- (void)recentChosen:(NSMenuItem*)item {
    NSString* path = item.representedObject;
    if (![path isKindOfClass:[NSString class]] || ![self loadFile:path])
        NSBeep();
}

- (void)clearRecent:(id)sender {
    static_cast<void>(sender);
    convolver::forget_recent_files();
    [self updateRecentMenu];
}

// Shows the format's bypass as the host has it now (its own button,
// automation); disabled when the plug-in cannot say.
- (void)readBypass {
    if (!bypass_)
        return;
    ConvolverSettingGetter get = bypass_[@"get"];
    const double value = get ? get() : NAN;
    _bypassButton.enabled = !std::isnan(value);
    _bypassButton.state = value >= 0.5 ? NSControlStateValueOn : NSControlStateValueOff;
}

// One click is one edit: begin, set, end, as the VST3 SDK asks of an editor.
- (void)bypassClicked:(NSButton*)button {
    ConvolverSettingGesture begin = bypass_[@"begin"];
    ConvolverSettingSetter set = bypass_[@"set"];
    ConvolverSettingGesture end = bypass_[@"end"];
    if (begin)
        begin();
    if (set)
        set(button.state == NSControlStateValueOn ? 1.0 : 0.0);
    if (end)
        end();
    [self refresh];
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
    if (accepted && path.UTF8String)
        convolver::remember_recent_file(path.UTF8String);
    [self refresh]; // The report says what plays now, and why a refused file was refused.
    if (accepted)
        watch_.settle(); // What was read is what plays: nothing to reload.
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
    // The button pressed (this one or the fix's) says so for two seconds.
    NSButton* pressed = sender == _actionButton ? _actionButton : _diagnosticsButton;
    pressed.title = @"Copied";
    __weak NSButton* weakPressed = pressed;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(2 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     NSButton* button = weakPressed;
                     if ([button.title isEqualToString:@"Copied"])
                         button.title = @"Copy Diagnostics";
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
