// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: Convolver's window and the properties it reads, through the Audio
// Unit API with the unit registered in-process: the report and Copy
// diagnostics properties, their change notifications, and the AppKit view
// that the factory makes, attached to an off-screen window, showing the
// status and the full explanation, with working Copy diagnostics and Show
// log buttons. jrp's first GarageBand run is the main case: a stereo preset
// on a mono track plays silence, and the window must say why.
#import <AppKit/AppKit.h>
#if __has_include(<AudioToolbox/AUCocoaUIView.h>)
#import <AudioToolbox/AUCocoaUIView.h>
#else
#import <AudioUnit/AUCocoaUIView.h>
#endif
#import "diagnostic_view.h"
#include "au/au_host.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#define CONVOLVER_TEXT_OF(x) #x
#define CONVOLVER_TEXT(x) CONVOLVER_TEXT_OF(x)

using namespace convolver;
using au_test::check;
using au_test::Unit;

namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string text(NSString* s) {
    return s ? std::string(s.UTF8String) : std::string();
}
bool contains(NSString* s, const char* part) {
    return s && [s rangeOfString:@(part)].location != NSNotFound;
}

Configuration identity(int rate, std::size_t channels) {
    Configuration c{rate, channels, channels, 0, {}, {}, {}};
    for (std::size_t ch = 0; ch < channels; ++ch)
        c.paths.push_back({{1.f, .5f, .25f}, {{ch, 1}}, {{ch, 1}}});
    return c;
}
HostState state_of(const Configuration& c) {
    HostState s;
    s.configuration = c;
    return s;
}

NSDictionary<NSString*, NSString*>* report(Unit& unit) {
    CFTypeRef value = nullptr;
    UInt32 size = sizeof value;
    check(AudioUnitGetProperty(unit.get(), au::report_property, kAudioUnitScope_Global, 0, &value,
                               &size),
          "report property");
    id report = CFBridgingRelease(value);
    require([report isKindOfClass:[NSDictionary class]], "the report is not a dictionary");
    return report;
}
NSString* diagnostics(Unit& unit) {
    CFTypeRef value = nullptr;
    UInt32 size = sizeof value;
    check(AudioUnitGetProperty(unit.get(), au::diagnostics_property, kAudioUnitScope_Global, 0,
                               &value, &size),
          "diagnostics property");
    id text = CFBridgingRelease(value);
    require([text isKindOfClass:[NSString class]], "the diagnostics are not a string");
    return text;
}

// The Convolver log goes to a file of this process's own while it exists.
class PrivateLog {
  public:
    PrivateLog()
        : path_(std::filesystem::temp_directory_path() /
                ("convolver-au-editor-" + std::to_string(std::random_device{}()) + ".log")) {
        setenv("CONVOLVER_LOG", path_.c_str(), 1);
    }
    PrivateLog(const PrivateLog&) = delete;
    PrivateLog& operator=(const PrivateLog&) = delete;
    ~PrivateLog() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    std::string path() const {
        return path_.string();
    }

  private:
    std::filesystem::path path_;
};

std::atomic<int> report_changes{0};
void report_changed(void*, AudioUnit, AudioUnitPropertyID id, AudioUnitScope, AudioUnitElement) {
    if (id == au::report_property)
        ++report_changes;
}

// The properties: types, read-only, notifications, and what they say for a
// stereo preset on a mono track.
void properties(const PrivateLog& log) {
    Unit unit;
    for (const auto id : {au::report_property, au::diagnostics_property}) {
        UInt32 size = 0;
        Boolean writable = true;
        check(AudioUnitGetPropertyInfo(unit.get(), id, kAudioUnitScope_Global, 0, &size, &writable),
              "property info " + std::to_string(id));
        require(size == sizeof(CFTypeRef) && !writable, "property info " + std::to_string(id));
    }
    // Registered without its bundle, the unit offers no window rather than a
    // bundle it cannot name; auval checks the installed component's.
    UInt32 size = 0;
    require(AudioUnitGetPropertyInfo(unit.get(), kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global,
                                     0, &size, nullptr) != noErr,
            "a window offered without the component bundle");

    auto r = report(unit);
    require([r[@"status name"] isEqualToString:@"No filter"], "empty: " + text(r[@"status name"]));
    require(contains(r[@"explanation"], "load a Convolver preset"),
            "empty: " + text(r[@"explanation"]));
    require(contains(r[@"title"], "Audio Unit #"), "title: " + text(r[@"title"]));

    check(AudioUnitAddPropertyListener(unit.get(), au::report_property, report_changed, nullptr),
          "report listener");
    // A stereo preset, restored before the host sets a mono track's formats,
    // as GarageBand does.
    check(unit.load(state_of(identity(44100, 2))), "stereo preset");
    require(report_changes.load() > 0, "no report change after a load");
    r = report(unit);
    require([r[@"status name"] isEqualToString:@"Not checked yet"],
            "before start: " + text(r[@"status name"]));
    check(unit.configure(44100, 1, 1, 512), "mono formats");
    const auto before = report_changes.load();
    check(unit.initialize(), "mono initialization");
    require(report_changes.load() > before, "no report change after initialization");
    r = report(unit);
    require([r[@"status"] isEqualToString:@"2"] && [r[@"status name"] isEqualToString:@"Load failed"],
            "mono track: " + text(r[@"status name"]));
    for (const char* part :
         {"plays silence", "Give the plug-in 2 inputs and 2 outputs", "put Convolver on a stereo track"})
        require(contains(r[@"explanation"], part), "mono track explanation lacks \"" +
                                                       std::string(part) +
                                                       "\": " + text(r[@"explanation"]));
    require(contains(r[@"details"], "Host: 44100 Hz, 1 in, 1 out, blocks of up to 512 frames"),
            "mono track details: " + text(r[@"details"]));
    require(r[@"summary"].length > 0, "no summary");
    require([r[@"log path"] isEqualToString:@(log.path().c_str())], "log path " + text(r[@"log path"]));

    // Copy diagnostics: the report and this instance's log lines, including
    // the start line that explains the silence.
    const auto copied = diagnostics(unit);
    require(contains(copied, "Convolver diagnostics") &&
                contains(copied, "put Convolver on a stereo track") &&
                contains(copied, "Recent Convolver log lines for Audio Unit #") &&
                contains(copied, "Started at 44100 Hz, 1 in, 1 out"),
            "diagnostics:\n" + text(copied));
    AudioUnitRemovePropertyListenerWithUserData(unit.get(), au::report_property, report_changed,
                                                nullptr);
    unit.uninitialize();
}

// The window the factory makes, as a host would ask for it.
void window() {
    Unit unit;
    Class factory_class = NSClassFromString(@CONVOLVER_TEXT(CONVOLVER_AU_VIEW_FACTORY));
    require(factory_class != nil, "no view factory class " CONVOLVER_TEXT(CONVOLVER_AU_VIEW_FACTORY));
    require([factory_class conformsToProtocol:@protocol(AUCocoaUIBase)],
            "the factory is not an AUCocoaUIBase");
    id<AUCocoaUIBase> factory = [[factory_class alloc] init];
    NSView* made = [factory uiViewForAudioUnit:unit.get() withSize:NSMakeSize(0, 0)];
    require([made isKindOfClass:[CONVOLVER_DIAGNOSTIC_VIEW class]], "no Convolver view");
    CONVOLVER_DIAGNOSTIC_VIEW* view = (CONVOLVER_DIAGNOSTIC_VIEW*)made;
    require(NSWidth(view.frame) >= 400 && NSHeight(view.frame) >= 300, "window too small");

    NSWindow* host = [[NSWindow alloc] initWithContentRect:view.frame
                                                 styleMask:NSWindowStyleMaskTitled
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    host.releasedWhenClosed = NO;
    [host.contentView addSubview:view];

    // Empty, then the mono-track failure, then a mono filter that plays.
    require([view.statusField.stringValue isEqualToString:@"No filter"],
            "window status " + text(view.statusField.stringValue));
    check(unit.load(state_of(identity(44100, 2))), "stereo preset");
    check(unit.configure(44100, 1, 1, 512), "mono formats");
    check(unit.initialize(), "mono initialization");
    [view refresh];
    const auto r = report(unit);
    require([view.statusField.stringValue isEqualToString:@"Load failed"],
            "window status " + text(view.statusField.stringValue));
    require(contains(view.explanationView.string, text(r[@"explanation"]).c_str()) &&
                contains(view.explanationView.string, "put Convolver on a stereo track") &&
                contains(view.explanationView.string, "Host: 44100 Hz, 1 in, 1 out"),
            "window text:\n" + text(view.explanationView.string));
    require(contains(view.titleField.stringValue, "Audio Unit #"),
            "window title " + text(view.titleField.stringValue));

    // Copy diagnostics, to a pasteboard of the test's own.
    NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
    view.pasteboard = board;
    require(view.diagnosticsButton.enabled, "Copy diagnostics disabled");
    [view.diagnosticsButton performClick:nil];
    NSString* pasted = [board stringForType:NSPasteboardTypeString];
    require(contains(pasted, "Convolver diagnostics") &&
                contains(pasted, "put Convolver on a stereo track") &&
                contains(pasted, "Recent Convolver log lines"),
            "copied:\n" + text(pasted));
    [board releaseGlobally];
    require(view.logButton.enabled, "Show log disabled with a log file");

    // Accessibility: named controls with their roles (VoiceOver reads them).
    require([view.statusField.accessibilityLabel isEqualToString:@"Status"], "status label");
    require([view.diagnosticsButton.accessibilityRole isEqualToString:NSAccessibilityButtonRole] &&
                [view.logButton.accessibilityRole isEqualToString:NSAccessibilityButtonRole],
            "button roles");
    require(view.explanationView.accessibilityLabel.length > 0 && !view.explanationView.editable &&
                view.explanationView.selectable,
            "explanation text");

    // Corrected: a mono filter plays.
    unit.uninitialize();
    check(unit.load(state_of(identity(44100, 1))), "mono preset");
    check(unit.initialize(), "mono initialization with a mono filter");
    [view refresh];
    require([view.statusField.stringValue isEqualToString:@"Playing"],
            "window status after the fix " + text(view.statusField.stringValue));

    // Closed before the unit goes, as hosts do; the view stops reading it.
    [view removeFromSuperview];
    [host close];
    unit.uninitialize();
}
} // namespace

int main() {
    @autoreleasepool {
        try {
            [NSApplication sharedApplication];
            PrivateLog log;
            properties(log);
            window();
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    std::cout << "Audio Unit window checks passed\n";
    return 0;
}
