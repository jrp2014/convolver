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
#include "au/au_host.hpp"
#import "diagnostic_view.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

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

std::size_t count(const std::string& text, const std::string& part) {
    std::size_t n = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
        ++n;
    return n;
}
std::string read_file(const std::string& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
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
    require([r[@"status"] isEqualToString:@"2"] &&
                [r[@"status name"] isEqualToString:@"Load failed"],
            "mono track: " + text(r[@"status name"]));
    for (const char* part : {"plays silence", "Give the plug-in 2 inputs and 2 outputs",
                             "put Convolver on a stereo track"})
        require(contains(r[@"explanation"], part), "mono track explanation lacks \"" +
                                                       std::string(part) +
                                                       "\": " + text(r[@"explanation"]));
    require(contains(r[@"details"], "Host: 44100 Hz, 1 in, 1 out, blocks of up to 512 frames"),
            "mono track details: " + text(r[@"details"]));
    require(r[@"summary"].length > 0, "no summary");
    require([r[@"log path"] isEqualToString:@(log.path().c_str())],
            "log path " + text(r[@"log path"]));

    // Copy diagnostics: the report and this instance's log lines, including
    // the start line that explains the silence.
    const auto copied = diagnostics(unit);
    require(contains(copied, "Convolver diagnostics") &&
                contains(copied, "put Convolver on a stereo track") &&
                contains(copied, "Recent Convolver log lines for Audio Unit #") &&
                contains(copied, "Started at 44100 Hz, 1 in, 1 out"),
            "diagnostics:\n" + text(copied));
    // GarageBand restored the same preset five times in a second on a mono
    // track: the identical failure is logged once, not once per request.
    // A refused load while the saved filter is kept: "Filter not loaded; the
    // previous filter is kept. …".
    const char* refused = "Filter not loaded";
    const auto before_repeats = count(read_file(log.path()), refused);
    for (int i = 0; i < 4; ++i)
        static_cast<void>(unit.load(state_of(identity(44100, 2))));
    // Exactly one: the first repeat follows the start line, whose text
    // differs, and the rest repeat it.
    require(count(read_file(log.path()), refused) == before_repeats + 1,
            "repeated identical failures logged:\n" + read_file(log.path()));
    AudioUnitRemovePropertyListenerWithUserData(unit.get(), au::report_property, report_changed,
                                                nullptr);
    unit.uninitialize();
}

// The window the factory makes, as a host would ask for it.
void window() {
    Unit unit;
    Class factory_class = NSClassFromString(@CONVOLVER_TEXT(CONVOLVER_AU_VIEW_FACTORY));
    require(factory_class != nil,
            "no view factory class " CONVOLVER_TEXT(CONVOLVER_AU_VIEW_FACTORY));
    require([factory_class conformsToProtocol:@protocol(AUCocoaUIBase)],
            "the factory is not an AUCocoaUIBase");
    id<AUCocoaUIBase> factory = [[factory_class alloc] init];
    NSView* made = [factory uiViewForAudioUnit:unit.get() withSize:NSMakeSize(0, 0)];
    require([made isKindOfClass:[CONVOLVER_DIAGNOSTIC_VIEW class]], "no Convolver view");
    CONVOLVER_DIAGNOSTIC_VIEW* view = (CONVOLVER_DIAGNOSTIC_VIEW*)made;
    // Fitted to its text, whatever size the host offers (jrp's GarageBand
    // run: the first version filled a large window and left it mostly blank).
    require(NSWidth(view.frame) == 560 && NSHeight(view.frame) >= 150 &&
                NSHeight(view.frame) <= 460,
            "empty window size " + std::to_string(NSWidth(view.frame)) + " x " +
                std::to_string(NSHeight(view.frame)));
    NSView* offered = [factory uiViewForAudioUnit:unit.get() withSize:NSMakeSize(1270, 1120)];
    require(NSEqualSizes(offered.frame.size, view.frame.size), "the host's size was used");

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
                contains(view.explanationView.string, "Host\t44100 Hz, 1 in, 1 out"),
            "window text:\n" + text(view.explanationView.string));
    require(contains(view.titleField.stringValue, "Audio Unit #"),
            "window title " + text(view.titleField.stringValue));
    // The buttons sit under the text, at the bottom right, inside the view.
    const auto frames = [&] {
        return "view " + text(NSStringFromRect(view.frame)) + ", text " +
               text(NSStringFromRect(view.explanationView.enclosingScrollView.frame)) +
               ", Copy diagnostics " + text(NSStringFromRect(view.diagnosticsButton.frame)) +
               ", Show log " + text(NSStringFromRect(view.logButton.frame));
    };
    const auto bottom = NSMaxY(view.diagnosticsButton.frame);
    require(view.isFlipped && NSMaxX(view.logButton.frame) <= NSWidth(view.frame) &&
                NSMinX(view.diagnosticsButton.frame) > NSWidth(view.frame) / 3 &&
                bottom <= NSHeight(view.frame) && NSHeight(view.frame) - bottom <= 24 &&
                NSMinY(view.diagnosticsButton.frame) >=
                    NSMaxY(view.explanationView.enclosingScrollView.frame),
            "button layout: " + frames());

    // Accessibility: named controls (VoiceOver reads the label or title).
    // AppKit supplies the buttons' role itself, and on a CI runner with no
    // accessibility client it need not report one, so only names are checked.
    require([view.statusField.accessibilityLabel isEqualToString:@"Status"], "status label");
    require([view.diagnosticsButton.title isEqualToString:@"Copy diagnostics"] &&
                [view.logButton.title isEqualToString:@"Show log"],
            "button titles: " + text(view.diagnosticsButton.title) + ", " +
                text(view.logButton.title));
    require(view.explanationView.accessibilityLabel.length > 0 && !view.explanationView.editable &&
                view.explanationView.selectable,
            "explanation text");

    // Gain and Mix as sliders (jrp's GarageBand run: a host that shows this
    // window no longer shows its generic sliders). Moving one sets the
    // parameter; a change from the host moves the slider.
    require(view.settingSliders.count == 2 && view.settingValues.count == 2, "no setting sliders");
    NSSlider* gain = view.settingSliders[0];
    NSSlider* mix = view.settingSliders[1];
    require(gain.minValue == -100 && gain.maxValue == 24 && gain.doubleValue == 0 &&
                mix.minValue == 0 && mix.maxValue == 100 && mix.doubleValue == 100,
            "slider ranges or values");
    require([view.settingValues[0].stringValue isEqualToString:@"0 dB"] &&
                [view.settingValues[1].stringValue isEqualToString:@"100% wet"],
            "setting values " + text(view.settingValues[0].stringValue) + ", " +
                text(view.settingValues[1].stringValue));
    require([gain.accessibilityLabel isEqualToString:@"Gain"], "Gain slider label");
    gain.doubleValue = -12;
    [gain sendAction:gain.action to:gain.target];
    AudioUnitParameterValue now = 0;
    check(AudioUnitGetParameter(unit.get(), au::gain_parameter, kAudioUnitScope_Global, 0, &now),
          "Gain value");
    require(now == -12 && [view.settingValues[0].stringValue isEqualToString:@"-12 dB"],
            "the Gain slider did not set Gain: " + std::to_string(now));
    check(AudioUnitSetParameter(unit.get(), au::mix_parameter, kAudioUnitScope_Global, 0, 50, 0),
          "Mix from the host");
    [view refresh];
    require(mix.doubleValue == 50 && [view.settingValues[1].stringValue isEqualToString:@"50% wet"],
            "the Mix slider did not follow the host");
    require([mix.accessibilityValueDescription isEqualToString:@"50% wet"],
            "Mix slider's VoiceOver value: " + text(mix.accessibilityValueDescription));
    require(contains(view.explanationView.string, "gain -12 dB, mix 50% wet"),
            "the details do not show the settings:\n" + text(view.explanationView.string));
    // Near an end of the range, the text does not round onto it.
    check(
        AudioUnitSetParameter(unit.get(), au::mix_parameter, kAudioUnitScope_Global, 0, 99.96f, 0),
        "Mix near 100");
    [view refresh];
    require([view.settingValues[1].stringValue hasPrefix:@"99.9"],
            "Mix near 100 reads " + text(view.settingValues[1].stringValue));
    check(AudioUnitSetParameter(unit.get(), au::gain_parameter, kAudioUnitScope_Global, 0, 0, 0),
          "Gain back");
    check(AudioUnitSetParameter(unit.get(), au::mix_parameter, kAudioUnitScope_Global, 0, 100, 0),
          "Mix back");
    [view refresh];

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

    // Corrected: a mono filter plays.
    unit.uninitialize();
    check(unit.load(state_of(identity(44100, 1))), "mono preset");
    check(unit.initialize(), "mono initialization with a mono filter");
    [view refresh];
    require([view.statusField.stringValue isEqualToString:@"Playing"],
            "window status after the fix " + text(view.statusField.stringValue));

    // A host may close the window and keep the view in it: the twice-a-second
    // read stops, so the unit can be disposed of (the independent review).
    [host close];
    require(![view refreshIfShown], "a closed window still reads the unit");
    [view removeFromSuperview];
    unit.uninitialize();
}

AudioUnitParameterValue status_value(Unit& unit) {
    AudioUnitParameterValue value = -1;
    check(
        AudioUnitGetParameter(unit.get(), au::status_parameter, kAudioUnitScope_Global, 0, &value),
        "Status value");
    return value;
}
// Faults Render counts: the report explains them and keeps Render's Fault,
// and an open window polling it logs them once, not on every read.
void faults(const PrivateLog& log) {
    Unit unit;
    check(unit.load(state_of(identity(44100, 2))), "stereo preset");
    check(unit.configure(44100, 2, 2, 512), "stereo formats");
    check(unit.initialize(), "stereo initialization");
    std::vector<std::vector<float>> in(2, std::vector<float>(512, 0.f)),
        out(2, std::vector<float>(512, 0.f));
    in[0][0] = std::numeric_limits<float>::quiet_NaN();
    const float* ins[] = {in[0].data(), in[1].data()};
    float* outs[] = {out[0].data(), out[1].data()};
    const auto render = [&] {
        bool silent = false;
        check(unit.render(ins, outs, 512, false, silent), "invalid-input render");
    };
    const char* logged = "The host sent invalid samples";
    const auto fault = static_cast<AudioUnitParameterValue>(HostStatus::fault);
    render();
    require(status_value(unit) == fault, "Render did not signal Fault");
    auto r = report(unit);
    require([r[@"status name"] isEqualToString:@"Fault"] && contains(r[@"explanation"], logged),
            "fault report: " + text(r[@"status name"]) + ": " + text(r[@"explanation"]));
    require(status_value(unit) == fault, "reading the report cleared Fault");
    require(count(read_file(log.path()), logged) == 1, "fault not logged once");
    // Faults that go on, read as an open window does.
    for (int i = 0; i < 6; ++i) {
        render();
        r = report(unit);
    }
    require([r[@"status name"] isEqualToString:@"Fault"] && status_value(unit) == fault,
            "Fault lost while faults go on");
    require(count(read_file(log.path()), logged) == 1,
            "polling logged the faults again: " +
                std::to_string(count(read_file(log.path()), logged)));
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
            faults(log);
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    std::cout << "Audio Unit window checks passed\n";
    return 0;
}
