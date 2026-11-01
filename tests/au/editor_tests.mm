// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: Convolver's window and the properties it reads, through the Audio
// Unit API with the unit registered in-process: the report, Copy
// diagnostics and filter file properties, their change notifications, a
// report read that does not wait for a slow load, and the AppKit view
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
#include "filter_file.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
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

// A 16-bit WAV with an impulse of one half on every channel.
void write_wav(const std::filesystem::path& path, int rate, int channels, int frames) {
    std::ofstream out(path, std::ios::binary);
    const auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.put(static_cast<char>((value >> (8 * i)) & 0xff));
    };
    const auto data = static_cast<std::uint32_t>(frames * channels * 2);
    out.write("RIFF", 4);
    put(36 + data, 4);
    out.write("WAVEfmt ", 8);
    put(16, 4);
    put(1, 2); // PCM
    put(static_cast<std::uint32_t>(channels), 2);
    put(static_cast<std::uint32_t>(rate), 4);
    put(static_cast<std::uint32_t>(rate * channels * 2), 4);
    put(static_cast<std::uint32_t>(channels * 2), 2);
    put(16, 2);
    out.write("data", 4);
    put(data, 4);
    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
            put(f == 0 ? 16384 : 0, 2);
    if (!out)
        throw std::runtime_error("could not write " + path.string());
}

// A folder of this process's own for filter files, removed afterwards.
class TemporaryFolder {
  public:
    TemporaryFolder()
        : path_(std::filesystem::temp_directory_path() /
                ("convolver-au-files-" + std::to_string(std::random_device{}()))) {
        std::filesystem::create_directories(path_);
    }
    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
    ~TemporaryFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    std::filesystem::path operator/(const char* name) const {
        return path_ / name;
    }

  private:
    std::filesystem::path path_;
};

std::string loaded_path(Unit& unit) {
    CFStringRef value = nullptr;
    UInt32 size = sizeof value;
    check(AudioUnitGetProperty(unit.get(), au::load_property, kAudioUnitScope_Global, 0, &value,
                               &size),
          "filter file property");
    NSString* path = CFBridgingRelease(value);
    return text(path);
}

// The reader the unit was built with (CONVOLVER_AU_FILE_READER).
const detail::SoundFiles& unit_reader() {
#if defined(CONVOLVER_AU_READER_LIBSNDFILE)
    return detail::libsndfile_files();
#else
    return detail::audio_toolbox_files();
#endif
}

// A file chosen as the window chooses one, without the window.
OSStatus choose(Unit& unit, const std::string& path) {
    NSString* name = @(path.c_str()); // Held for the call.
    CFStringRef value = (__bridge CFStringRef)name;
    return AudioUnitSetProperty(unit.get(), au::load_property, kAudioUnitScope_Global, 0, &value,
                                sizeof value);
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

// The state notifications hosts may use to mark the project changed.
std::atomic<int> class_info_changes{0}, preset_changes{0};
void state_changed(void*, AudioUnit, AudioUnitPropertyID id, AudioUnitScope, AudioUnitElement) {
    if (id == kAudioUnitProperty_ClassInfo)
        ++class_info_changes;
    if (id == kAudioUnitProperty_PresentPreset)
        ++preset_changes;
}
std::string present_preset(Unit& unit) {
    AUPreset preset{};
    UInt32 size = sizeof preset;
    check(AudioUnitGetProperty(unit.get(), kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global,
                               0, &preset, &size),
          "present preset");
    NSString* name = CFBridgingRelease(preset.presetName);
    return preset.presetNumber == -1 ? text(name) : "factory preset " + text(name);
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
    {
        UInt32 size = 0;
        Boolean writable = false;
        check(AudioUnitGetPropertyInfo(unit.get(), au::load_property, kAudioUnitScope_Global, 0,
                                       &size, &writable),
              "filter file property info");
        require(size == sizeof(CFStringRef) && writable, "filter file property info");
        require(loaded_path(unit).empty(), "a filter file before any was chosen");
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
    require(contains(r[@"audio"], "Channels\t1 in, 1 out\tthe filter has 2 in, 2 out\tproblem\t") &&
                contains(r[@"audio"], "Host blocks\tup to 512 frames\t"),
            "mono track Audio facts: " + text(r[@"audio"]));
    require(contains(r[@"reference"], "Software: Convolver "),
            "mono track reference: " + text(r[@"reference"]));
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
    // Fitted to its content, whatever size the host offers (jrp's GarageBand
    // run: the first version filled a large window and left it mostly blank).
    // The bounds allow the form (Filter, Gain, Mix, Latency) and the Audio
    // facts of an empty unit, and keep the window within a 13-inch screen
    // with the host's own title bar and controls.
    require(NSWidth(view.frame) == 560 && NSHeight(view.frame) >= 300 &&
                NSHeight(view.frame) <= 680,
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
    require([view.explanationView.string isEqualToString:r[@"headline"]] &&
                contains(view.explanationView.string, text(r[@"explanation"]).c_str()) &&
                contains(view.explanationView.string, "put Convolver on a stereo track"),
            "window text:\n" + text(view.explanationView.string));
    // The Audio facts in plain words, the mismatch marked in words too. The
    // sign is U+26A0 with its text-style selector U+FE0E: NSString matches
    // whole composed characters, so the sign alone would not be found.
    const auto fact = [&](const char* name) -> NSTextField* {
        for (NSUInteger i = 0; i < view.audioLabels.count; ++i)
            if ([view.audioLabels[i].stringValue isEqualToString:@(name)])
                return view.audioValues[i];
        return nil;
    };
    require(
        view.audioLabels.count >= 6 &&
            [view.audioLabels[0].stringValue isEqualToString:@"Sample rate"] &&
            [fact("Sample rate").stringValue hasPrefix:@"44100 Hz"] &&
            contains(fact("Channels").stringValue, "1 in, 1 out") &&
            contains(fact("Channels").stringValue, "\u26A0\uFE0E") &&
            contains(fact("Channels").stringValue, "the filter has 2 in, 2 out") &&
            fact("Channels").toolTip.length > 0 && fact("Routing") != nil &&
            fact("Added delay") != nil,
        "Audio facts: " +
            text([[view.audioLabels valueForKey:@"stringValue"] componentsJoinedByString:@", "]) +
            "\n" +
            text([[view.audioValues valueForKey:@"stringValue"] componentsJoinedByString:@"\n"]));
    require([view.filterNameField.stringValue isEqualToString:r[@"filter name"]] &&
                [view.filterNameField.stringValue
                    isEqualToString:@"From the saved project or preset"] &&
                contains(view.filterNoteField.stringValue, "drop"),
            "Filter row: " + text(view.filterNameField.stringValue) + " / " +
                text(view.filterNoteField.stringValue));
    require(contains(view.titleField.stringValue, "Audio Unit #"),
            "window title " + text(view.titleField.stringValue));
    // Everything sits inside the view, top to bottom: the explanation, the
    // Audio facts, and the buttons at the bottom right, the view ending just
    // under them (the height fits the content). Frames in the view's own
    // (flipped) coordinates, as the controls sit in stack views.
    const auto in_view = [&](NSView* v) { return [view convertRect:v.bounds fromView:v]; };
    const NSRect copy = in_view(view.diagnosticsButton), log_button = in_view(view.logButton),
                 explained = in_view(view.explanationView.enclosingScrollView),
                 audio = in_view(view.audioValues.lastObject);
    const auto frames = [&] {
        return "view " + text(NSStringFromRect(view.frame)) + ", text " +
               text(NSStringFromRect(explained)) + ", last fact " + text(NSStringFromRect(audio)) +
               ", Copy Diagnostics " + text(NSStringFromRect(copy)) + ", Show Log " +
               text(NSStringFromRect(log_button));
    };
    require(view.isFlipped && NSMaxX(log_button) <= NSWidth(view.frame) &&
                NSMinX(copy) > NSWidth(view.frame) / 3 && NSMaxY(copy) <= NSHeight(view.frame) &&
                NSHeight(view.frame) - NSMaxY(copy) <= 24 && NSMinY(explained) >= 0 &&
                NSMinY(audio) > NSMaxY(explained) && NSMinY(copy) > NSMaxY(audio),
            "layout: " + frames());
    // The file, software and log lines are folded away, and opening them
    // makes the window taller by their height, closing returns it.
    const auto folded = NSHeight(view.frame);
    require(view.referenceField.hidden && view.referenceButton.state == NSControlStateValueOff,
            "the reference lines are not folded away");
    [view showReference:YES];
    require(!view.referenceField.hidden && NSHeight(view.frame) > folded + 20 &&
                contains(view.referenceField.stringValue, "Software\tConvolver ") &&
                contains(view.referenceField.stringValue, "Log\t"),
            "opened reference: " + std::to_string(NSHeight(view.frame)) + ": " +
                text(view.referenceField.stringValue));
    [view showReference:NO];
    require(NSHeight(view.frame) == folded, "closing the reference lines left the window " +
                                                std::to_string(NSHeight(view.frame)) + " high");

    // Accessibility: named controls (VoiceOver reads the label or title).
    // AppKit supplies the buttons' role itself, and on a CI runner with no
    // accessibility client it need not report one, so only names are checked.
    require([view.statusField.accessibilityLabel isEqualToString:@"Status"], "status label");
    require([view.diagnosticsButton.title isEqualToString:@"Copy Diagnostics"] &&
                [view.logButton.title isEqualToString:@"Show Log"],
            "button titles: " + text(view.diagnosticsButton.title) + ", " +
                text(view.logButton.title));
    require(view.explanationView.accessibilityLabel.length > 0 && !view.explanationView.editable &&
                view.explanationView.selectable,
            "explanation text");

    // Gain and Mix as sliders with their values typed beside them (jrp's
    // GarageBand run: a host that shows this window no longer shows its
    // generic sliders). Moving one or typing sets the parameter; a change
    // from the host moves the slider; resetting (Option-click) restores the
    // default.
    require(view.settingSliders.count == 2 && view.settingMenus.count == 1 &&
                view.settingValues.count == 3,
            "no setting sliders and menu");
    NSSlider* gain = view.settingSliders[0];
    NSSlider* mix = view.settingSliders[1];
    require(gain.minValue == -100 && gain.maxValue == 24 && gain.doubleValue == 0 &&
                mix.minValue == 0 && mix.maxValue == 100 && mix.doubleValue == 100,
            "slider ranges or values");
    require([view.settingValues[0].stringValue isEqualToString:@"0"] &&
                [view.settingValues[1].stringValue isEqualToString:@"100"] &&
                view.settingValues[0].editable && view.settingValues[1].editable &&
                [view.settingValues[0].accessibilityLabel isEqualToString:@"Gain"],
            "setting values " + text(view.settingValues[0].stringValue) + ", " +
                text(view.settingValues[1].stringValue));
    require([gain.accessibilityLabel isEqualToString:@"Gain"], "Gain slider label");
    gain.doubleValue = -12;
    [gain sendAction:gain.action to:gain.target];
    AudioUnitParameterValue now = 0;
    check(AudioUnitGetParameter(unit.get(), au::gain_parameter, kAudioUnitScope_Global, 0, &now),
          "Gain value");
    require(now == -12 && [view.settingValues[0].stringValue isEqualToString:@"-12"] &&
                [gain.accessibilityValueDescription isEqualToString:@"-12 dB"],
            "the Gain slider did not set Gain: " + std::to_string(now));
    // Typed, with a minus sign as the window writes it; beyond the range it
    // stops at the end; text that is not a number changes nothing.
    view.settingValues[0].stringValue = @"\u22126.5";
    [view.settingValues[0] sendAction:view.settingValues[0].action to:view.settingValues[0].target];
    check(AudioUnitGetParameter(unit.get(), au::gain_parameter, kAudioUnitScope_Global, 0, &now),
          "typed Gain");
    require(now == -6.5f && gain.doubleValue == -6.5, "typed Gain: " + std::to_string(now));
    view.settingValues[0].stringValue = @"40";
    [view.settingValues[0] sendAction:view.settingValues[0].action to:view.settingValues[0].target];
    check(AudioUnitGetParameter(unit.get(), au::gain_parameter, kAudioUnitScope_Global, 0, &now),
          "typed Gain beyond the range");
    require(now == 24, "typed Gain beyond the range: " + std::to_string(now));
    view.settingValues[0].stringValue = @"loud";
    [view.settingValues[0] sendAction:view.settingValues[0].action to:view.settingValues[0].target];
    check(AudioUnitGetParameter(unit.get(), au::gain_parameter, kAudioUnitScope_Global, 0, &now),
          "typed text");
    require(now == 24 && [view.settingValues[0].stringValue isEqualToString:@"24"],
            "typed text changed Gain: " + std::to_string(now));
    [view resetSetting:0];
    check(AudioUnitGetParameter(unit.get(), au::gain_parameter, kAudioUnitScope_Global, 0, &now),
          "reset Gain");
    require(now == 0 && gain.doubleValue == 0, "reset Gain: " + std::to_string(now));
    gain.doubleValue = -12;
    [gain sendAction:gain.action to:gain.target];
    check(AudioUnitSetParameter(unit.get(), au::mix_parameter, kAudioUnitScope_Global, 0, 50, 0),
          "Mix from the host");
    [view refresh];
    require(mix.doubleValue == 50 && [view.settingValues[1].stringValue isEqualToString:@"50"],
            "the Mix slider did not follow the host");
    require([mix.accessibilityValueDescription isEqualToString:@"50% wet"],
            "Mix slider's VoiceOver value: " + text(mix.accessibilityValueDescription));
    require(contains(diagnostics(unit), "gain -12 dB, mix 50% wet"),
            "Copy Diagnostics does not give the settings:\n" + text(diagnostics(unit)));
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

    // Latency as a menu of block sizes (PLUG-10). A choice sets the
    // parameter and plays at once, and the Added delay fact shows it.
    NSPopUpButton* latency = view.settingMenus[0];
    require(latency.numberOfItems == 7 && [latency.itemTitles[0] isEqualToString:@"64 frames"] &&
                [latency.itemTitles[6] isEqualToString:@"4096 frames"] &&
                latency.indexOfSelectedItem == 2 &&
                [latency.accessibilityLabel isEqualToString:@"Latency"] &&
                [view.settingValues[2].stringValue isEqualToString:@"5.8 ms"],
            "Latency menu: " + text([latency.itemTitles componentsJoinedByString:@", "]));
    [latency selectItemAtIndex:3];
    [latency sendAction:latency.action to:latency.target];
    check(AudioUnitGetParameter(unit.get(), au::latency_parameter, kAudioUnitScope_Global, 0, &now),
          "Latency value");
    require(now == 3, "the Latency menu did not set Latency: " + std::to_string(now));
    require(!contains(view.explanationView.string, "is chosen") &&
                [view.settingValues[2].stringValue isEqualToString:@"11.6 ms"] &&
                contains(fact("Added delay").stringValue, "512 frames"),
            "the window does not show the new Latency in use:\n" +
                text(view.explanationView.string) + "\n" + text(view.settingValues[2].stringValue) +
                "\n" + text(fact("Added delay").stringValue));
    check(AudioUnitSetParameter(unit.get(), au::latency_parameter, kAudioUnitScope_Global, 0, 2, 0),
          "Latency from the host");
    [view refresh];
    require(latency.indexOfSelectedItem == 2 &&
                !contains(view.explanationView.string, "is chosen") &&
                [view.settingValues[2].stringValue isEqualToString:@"5.8 ms"] &&
                contains(fact("Added delay").stringValue, "256 frames"),
            "the Latency menu did not follow the host");

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

// Filter files: the Choose… button and the drop registration (the
// file dialog and a real drag need a user), then the view's load step both
// use. A sound file loads through the load property with the step
// convolver-preset uses, so the saved state is the one that step gives for
// the same rate and channels; a file that does not suit the unit is refused
// with its name and the reason, the previous filter still playing, and an
// empty unit stays silent. A dialog that answers after its window closed, or
// after a newer one opened, loads nothing.
void files() {
    TemporaryFolder folder;
    const auto stereo = folder / "stereo.wav", mono = folder / "mono.wav";
    write_wav(stereo, 44100, 2, 8);
    write_wav(mono, 44100, 1, 8);
    Unit unit;
    check(unit.configure(44100, 2, 2, 512), "stereo formats");
    check(unit.initialize(), "stereo initialization");
    Class factory_class = NSClassFromString(@CONVOLVER_TEXT(CONVOLVER_AU_VIEW_FACTORY));
    id<AUCocoaUIBase> factory = [[factory_class alloc] init];
    CONVOLVER_DIAGNOSTIC_VIEW* view =
        (CONVOLVER_DIAGNOSTIC_VIEW*)[factory uiViewForAudioUnit:unit.get()
                                                       withSize:NSMakeSize(0, 0)];
    require(!view.chooseButton.hidden && [view.chooseButton.title isEqualToString:@"Choose…"] &&
                [view.chooseButton.accessibilityLabel isEqualToString:@"Choose Filter"],
            "Choose… button: " + text(view.chooseButton.title));
    require([view.registeredDraggedTypes containsObject:NSPasteboardTypeFileURL],
            "the window takes no dropped files");

    for (const auto id : {kAudioUnitProperty_ClassInfo, kAudioUnitProperty_PresentPreset})
        check(AudioUnitAddPropertyListener(unit.get(), id, state_changed, nullptr),
              "state listener");
    require([view loadFile:@(stereo.c_str())], "the stereo file was refused");
    // The project must be marked changed: GarageBand 10.4.14 ignored the
    // ClassInfo notification alone (jrp's run of d1dc0a2), so an accepted
    // file is also the present preset, a user preset named after it.
    require(class_info_changes > 0 && preset_changes > 0,
            "a chosen file sent no ClassInfo or PresentPreset notification");
    require(present_preset(unit) == "stereo", "present preset: " + present_preset(unit));
    auto r = report(unit);
    require([r[@"status name"] isEqualToString:@"Playing"] &&
                [view.statusField.stringValue isEqualToString:@"Playing"],
            "after the stereo file: " + text(r[@"status name"]) + ": " + text(r[@"explanation"]));
    require(contains(r[@"reference"], stereo.c_str()) &&
                [view.filterNameField.stringValue isEqualToString:@"stereo.wav"],
            "the window does not name the file:\n" + text(r[@"reference"]) + "\n" +
                text(view.filterNameField.stringValue));
    require(loaded_path(unit) == stereo.string(), "filter file property " + loaded_path(unit));
    require(encode_host_state(unit.saved()) ==
                encode_host_state(filter_file_state(stereo, {44100, 2, 2}, unit_reader())),
            "the saved state differs from the shared file step's (native.audio_toolbox checks "
            "that it matches convolver-preset's)");

    require(![view loadFile:@(mono.c_str())], "a mono file accepted by a stereo unit");
    r = report(unit);
    require(contains(r[@"explanation"], mono.c_str()) &&
                contains(r[@"explanation"], "could not be used"),
            "mono file refusal: " + text(r[@"explanation"]));
    require([view.filterNameField.stringValue isEqualToString:@"stereo.wav"] &&
                [view.filterNoteField.stringValue hasPrefix:@"mono.wav was refused."] &&
                contains(r[@"reference"], "Refused file: "),
            "Filter row after a refused file: " + text(view.filterNameField.stringValue) + " / " +
                text(view.filterNoteField.stringValue));
    // The short summary (Status, Copy diagnostics) gives the refused file's
    // own counts (jrp's GarageBand run: it read "0 in, 0 out"), and the
    // explanation speaks of Convolver and the host, not command-line options.
    require(contains(r[@"summary"], "the filter has 1 in, 1 out, the plug-in 2 in, 2 out") &&
                !contains(r[@"summary"], "0 in, 0 out"),
            "mono file summary: " + text(r[@"summary"]));
    require(!contains(r[@"explanation"], "--") &&
                contains(r[@"explanation"], "load it in Convolver instead"),
            "mono file advice: " + text(r[@"explanation"]));
    require(loaded_path(unit) == stereo.string(), "the refused file replaced the stereo one");
    require(present_preset(unit) == "stereo", "a refused file renamed the present preset");
    {
        // The saved state carries the preset name, and restoring it in
        // another unit (a reopened project) brings the name back.
        const auto plist = unit.class_info_now();
        const auto saved_name = static_cast<CFStringRef>(
            CFDictionaryGetValue(static_cast<CFDictionaryRef>(plist), CFSTR(kAUPresetNameKey)));
        const bool named = saved_name && CFGetTypeID(saved_name) == CFStringGetTypeID() &&
                           text((__bridge NSString*)saved_name) == "stereo";
        Unit reopened;
        check(reopened.configure(44100, 2, 2, 512), "reopened formats");
        check(reopened.initialize(), "reopened initialization");
        const auto restored = reopened.restore(plist);
        CFRelease(plist);
        require(named, "the saved state does not name the preset \"stereo\"");
        check(restored, "restoring the saved state");
        require(present_preset(reopened) == "stereo",
                "restored present preset: " + present_preset(reopened));
        reopened.uninitialize();
    }
    const auto kept = unit.failure();
    require(kept.present && !kept.silent && unit.saved().configuration,
            "after a refused file the stereo filter must still play and be saved");
    const auto missing = folder / "missing.cfg";
    require(![view loadFile:@(missing.c_str())], "a missing file accepted");
    require(contains(report(unit)[@"explanation"], missing.c_str()),
            "missing file: " + text(report(unit)[@"explanation"]));

    // Choose Filter's dialog answers later, perhaps after the window has
    // closed and the host has begun disposing of the unit (jrp's review).
    // Only the pending request loads its file, once, while the view is in
    // an open window. Driven without a dialog or the mouse.
    const auto again = folder / "again.wav", late = folder / "late.wav";
    write_wav(again, 44100, 2, 8);
    write_wav(late, 44100, 2, 8);
    const auto url = [](const std::filesystem::path& path) {
        return [NSURL fileURLWithPath:@(path.c_str())];
    };
    require(![view finishChoosing:[view startChoosing] URL:url(again)],
            "a file was loaded for a view in no window");
    NSWindow* host = [[NSWindow alloc] initWithContentRect:view.frame
                                                 styleMask:NSWindowStyleMaskTitled
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    host.releasedWhenClosed = NO;
    [host.contentView addSubview:view];
    const auto older = [view startChoosing], newer = [view startChoosing];
    require(![view finishChoosing:older URL:url(late)] && loaded_path(unit) == stereo.string(),
            "a request replaced by a newer one loaded its file");
    require([view finishChoosing:newer URL:url(again)] && loaded_path(unit) == again.string(),
            "the pending request did not load its file: " + loaded_path(unit));
    require(![view finishChoosing:newer URL:url(late)] && loaded_path(unit) == again.string(),
            "a request loaded a second file");
    require(![view finishChoosing:[view startChoosing] URL:nil], "a cancelled dialog loaded");
    const auto pending = [view startChoosing];
    [host close]; // The host keeps the closed window and the view in it.
    require(view.window == host, "the closed window no longer holds the view");
    require(![view finishChoosing:pending URL:url(late)] && loaded_path(unit) == again.string(),
            "a dialog answered after the window closed loaded its file");
    [view removeFromSuperview];
    unit.uninitialize();

    // On an empty unit a refused first file leaves it silent, as a first
    // preset that does not fit does (docs/use/plugins.md).
    Unit empty;
    check(empty.configure(44100, 2, 2, 512), "stereo formats");
    check(empty.initialize(), "stereo initialization");
    require(choose(empty, mono.string()) != noErr, "a mono file accepted by an empty unit");
    const auto first = empty.failure();
    require(first.present && first.silent && contains(report(empty)[@"explanation"], mono.c_str()),
            "a refused first file must leave the unit silent and say why: " +
                text(report(empty)[@"explanation"]));
    empty.uninitialize();
}

// TODO AU-6: a report read while a load holds the unit's control lock on
// another thread returns at once, with the last report, instead of waiting
// for the load; the window then rereads when the load is done. The load is
// held open at its start (load_started_for_tests) rather than timed, so a
// busy runner cannot fail the test (jrp's review: the timed version failed
// once at 50.3 ms against 50 ms).
std::promise<void>* load_started = nullptr;
std::shared_future<void> load_released;
void responsive() {
    Unit unit;
    check(unit.configure(44100, 2, 2, 512), "stereo formats");
    check(unit.initialize(), "stereo initialization");
    const auto before = text(report(unit)[@"status name"]); // What the window shows.
    require(before == "No filter", "before the load: " + before);

    std::promise<void> started, release;
    auto load_has_started = started.get_future();
    load_started = &started;
    load_released = release.get_future().share();
    au::load_started_for_tests = []() noexcept {
        load_started->set_value();
        load_released.wait();
    };
    const auto plist = au_test::class_info(state_of(identity(44100, 2)), "held");
    OSStatus loaded = noErr;
    std::thread loader([&] { loaded = unit.restore(plist); });
    load_has_started.wait(); // The load holds the control lock until released.
    auto reading =
        std::async(std::launch::async, [&] { return text(report(unit)[@"status name"]); });
    const bool prompt = reading.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
    release.set_value(); // Whatever happened, so that nothing is left waiting.
    loader.join();
    au::load_started_for_tests = nullptr;
    CFRelease(plist);
    const auto during = reading.get();
    check(loaded, "held load");
    require(prompt, "a report read waited for a load that holds the control lock");
    require(during == before, "a read during the load gave '" + during + "', not the last report");
    const auto after = text(report(unit)[@"status name"]);
    require(after == "Playing", "after the load: " + after);
    std::cout << "report read during a held load: '" << during << "', then '" << after << "'\n";
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
            files();
            responsive();
            faults(log);
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    std::cout << "Audio Unit window checks passed\n";
    return 0;
}
