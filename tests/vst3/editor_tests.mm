// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11: Convolver's window in VST3 on macOS, as a host opens it: the
// controller's createView("editor") gives an IPlugView for NSView, which is
// sized (getSize), attached to an off-screen window's content view and
// removed again. Checks that it is the shared Convolver view, that it shows
// the processor's report, that its settings edit the parameters as the SDK
// asks (beginEdit, performEdit, endEdit), Bypass too, that a Latency choice reaches the
// processor and asks the host to restart, that a chosen file loads, marks
// the project changed, joins the Recent menu and is reloaded by hand and
// when it or the chosen list entry's filter changes (not when the filter was
// replaced meanwhile), and that a taller view asks the host to resize.
#import "diagnostic_view.h"
#include "processor.hpp"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "recent_files.hpp"
#import <AppKit/AppKit.h>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::vst3;

namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string text(NSString* s) {
    return s ? std::string(s.UTF8String) : std::string();
}

// What a host's component handler receives from the controller.
class Handler final : public IComponentHandler, public IComponentHandler2 {
  public:
    tresult PLUGIN_API beginEdit(ParamID id) override {
        edits.push_back({'b', id, 0});
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        edits.push_back({'p', id, value});
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override {
        edits.push_back({'e', id, 0});
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        if (flags & kLatencyChanged)
            ++latency_restarts;
        return kResultOk;
    }
    tresult PLUGIN_API setDirty(TBool state) override {
        dirty += state ? 1 : 0;
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString) override {
        return kNotImplemented;
    }
    tresult PLUGIN_API startGroupEdit() override {
        return kNotImplemented;
    }
    tresult PLUGIN_API finishGroupEdit() override {
        return kNotImplemented;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler2::iid, IComponentHandler2)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return 1;
    }
    uint32 PLUGIN_API release() override {
        return 1;
    }
    struct Edit {
        char kind;
        ParamID id;
        ParamValue value;
    };
    // The edits of one parameter as the kinds in order ("bpe").
    std::string kinds(ParamID id) const {
        std::string out;
        for (const auto& e : edits)
            if (e.id == id)
                out += e.kind;
        return out;
    }
    std::vector<Edit> edits;
    int latency_restarts = 0;
    int dirty = 0;
};

// The host's window frame: records the sizes the view asks for and, as hosts
// do, gives the view its new size (onSize).
class Frame final : public IPlugFrame {
  public:
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override {
        if (!view || !size)
            return kInvalidArgument;
        asked.push_back(*size);
        return view->onSize(size);
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return 1;
    }
    uint32 PLUGIN_API release() override {
        return 1;
    }
    std::vector<ViewRect> asked;
};

// A 16-bit mono WAV at 48 kHz with an impulse of one half.
void write_wav(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    const auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.put(static_cast<char>((value >> (8 * i)) & 0xff));
    };
    constexpr std::uint32_t frames = 8, data = frames * 2;
    out.write("RIFF", 4);
    put(36 + data, 4);
    out.write("WAVEfmt ", 8);
    put(16, 4);
    put(1, 2);
    put(1, 2);
    put(48000, 4);
    put(96000, 4);
    put(2, 2);
    put(16, 2);
    out.write("data", 4);
    put(data, 4);
    for (std::uint32_t f = 0; f < frames; ++f)
        put(f == 0 ? 16384 : 0, 2);
    if (!out)
        throw std::runtime_error("could not write " + path.string());
}

CONVOLVER_DIAGNOSTIC_VIEW* find_view(NSView* parent) {
    for (NSView* child in parent.subviews)
        if ([child isKindOfClass:[CONVOLVER_DIAGNOSTIC_VIEW class]])
            return static_cast<CONVOLVER_DIAGNOSTIC_VIEW*>(child);
    return nil;
}

void window() {
    HostApplication context;
    Handler handler; // Outlive the controller and the view, which hold them.
    Frame frame;
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                c.setComponentHandler(&handler) == kResultOk,
            "initialize");
    require(p.connect(&c) == kResultOk && c.connect(&p) == kResultOk, "connect");
    SpeakerArrangement mono = SpeakerArr::kMono;
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk, "activate");

    // The recent files beside this test's log (CMake sets CONVOLVER_LOG).
    convolver::forget_recent_files();
    require(c.createView("other") == nullptr, "a view for another name");
    IPlugView* view = c.createView(ViewType::kEditor);
    require(view != nullptr, "no editor view");
    require(view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue &&
                view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue,
            "platform types");
    require(view->canResize() != kResultTrue, "the view says it is resizable");
    ViewRect size;
    require(view->getSize(&size) == kResultTrue && size.getWidth() == 560 &&
                size.getHeight() > 100 && size.getHeight() < 1000,
            "view size " + std::to_string(size.getWidth()) + " x " +
                std::to_string(size.getHeight()));
    require(view->setFrame(&frame) == kResultTrue, "set frame");

    NSWindow* host =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.getWidth(), size.getHeight())
                                    styleMask:NSWindowStyleMaskTitled
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    host.releasedWhenClosed = NO;
    require(view->attached((__bridge void*)host.contentView, kPlatformTypeNSView) == kResultOk,
            "attach");
    CONVOLVER_DIAGNOSTIC_VIEW* shown = find_view(host.contentView);
    require(shown != nil, "the Convolver view is not in the host's view");
    require(text(NSStringFromClass([shown class])).rfind("ConvolverVST3View_", 0) == 0,
            "view class " + text(NSStringFromClass([shown class])));
    [shown refresh];
    require([shown.statusField.stringValue isEqualToString:@"No filter"],
            "status " + text(shown.statusField.stringValue));
    require([shown.titleField.stringValue rangeOfString:@"VST3 #"].location != NSNotFound,
            "title " + text(shown.titleField.stringValue));
    require(shown.settingSliders.count == 2 && shown.settingMenus.count == 1 &&
                shown.settingMenus[0].numberOfItems == 7 &&
                [shown.settingMenus[0].titleOfSelectedItem isEqualToString:@"256 frames"],
            "settings rows");

    // A Gain the host set shows in the window; Option-click's reset is one
    // edit of the parameter, as the SDK asks of an editor.
    c.setParamNormalized(gain_id, normalized_gain(-6));
    [shown refresh];
    require(std::abs(shown.settingSliders[0].doubleValue + 6) < 1e-6 &&
                [shown.settingValues[0].stringValue isEqualToString:@"-6"],
            "Gain from the host: " + text(shown.settingValues[0].stringValue));
    handler.edits.clear();
    [shown resetSetting:0];
    require(handler.kinds(gain_id) == "bpe" &&
                std::abs(handler.edits[1].value - normalized_gain(0)) < 1e-12 &&
                std::abs(c.getParamNormalized(gain_id) - normalized_gain(0)) < 1e-12,
            "Gain reset edits: " + handler.kinds(gain_id));

    // A Latency choice: one edit, the processor reports it, the host is
    // asked to restart, and the window says it is not in use yet.
    handler.edits.clear();
    NSPopUpButton* latency = shown.settingMenus[0];
    [latency selectItemAtIndex:4];
    [latency sendAction:latency.action to:latency.target];
    require(handler.kinds(latency_id) == "bpe" && p.settings().latency == 1024 &&
                p.getLatencySamples() == 256 && handler.latency_restarts == 1,
            "Latency choice: " + handler.kinds(latency_id));
    [shown refresh];
    require([shown.settingValues[2].stringValue rangeOfString:@"not in use yet"].location !=
                NSNotFound,
            "Latency note " + text(shown.settingValues[2].stringValue));
    require(p.setActive(false) == kResultOk && p.setActive(true) == kResultOk &&
                p.getLatencySamples() == 1024,
            "restart");
    [shown refresh];
    require([shown.settingValues[2].stringValue isEqualToString:@"21.3 ms"],
            "Latency note after the restart " + text(shown.settingValues[2].stringValue));

    // Bypass: a click is one edit of the bypass parameter, and the host's
    // own bypass shows in the checkbox.
    require(!shown.bypassButton.hidden && shown.bypassButton.enabled &&
                shown.bypassButton.state == NSControlStateValueOff &&
                [shown.bypassButton.title isEqualToString:@"Bypass"],
            "Bypass checkbox");
    handler.edits.clear();
    [shown.bypassButton performClick:nil];
    require(handler.kinds(bypass_id) == "bpe" && handler.edits[1].value == 1.0 &&
                c.getParamNormalized(bypass_id) == 1.0 &&
                shown.bypassButton.state == NSControlStateValueOn,
            "Bypass click edits: " + handler.kinds(bypass_id));
    c.setParamNormalized(bypass_id, 0.0);
    [shown refresh];
    require(shown.bypassButton.state == NSControlStateValueOff, "Bypass from the host");

    // A file chosen in the window loads in the processor and marks the
    // project changed.
    const auto folder = std::filesystem::temp_directory_path() /
                        ("convolver-vst3-editor-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(folder);
    const auto file = folder / "room.wav";
    write_wav(file);
    require([shown loadFile:@(file.c_str())] && handler.dirty == 1, "file not loaded");
    require([shown.statusField.stringValue isEqualToString:@"Playing"] &&
                [shown.filterNameField.stringValue isEqualToString:@"room.wav"],
            "after loading: " + text(shown.statusField.stringValue) + ", " +
                text(shown.filterNameField.stringValue));

    // The file is now first in the Recent menu, and Reload reads it again.
    [shown updateRecentMenu];
    require(shown.recentButton.numberOfItems >= 2 &&
                [shown.recentButton.itemArray[1].title hasPrefix:@"room.wav — "] &&
                [shown.recentButton.itemArray[1].toolTip isEqualToString:@(file.c_str())],
            "Recent menu: " + text(shown.recentButton.itemArray.lastObject.title));
    require(shown.reloadButton.enabled && shown.watchButton.enabled &&
                shown.watchButton.state == NSControlStateValueOn,
            "Reload controls");
    [shown.reloadButton performClick:nil];
    require(handler.dirty == 2, "Reload did not load the file again");

    // A change to the file reloads it once the change has settled for one
    // check; a missing file does not.
    NSString* room = @(file.c_str());
    NSFileManager* files = [NSFileManager defaultManager];
    require([files
                setAttributes:@{NSFileModificationDate : [NSDate dateWithTimeIntervalSinceNow:60]}
                 ofItemAtPath:room
                        error:nil],
            "touch the file");
    require(![shown checkForChanges] && [shown checkForChanges] && handler.dirty == 3,
            "a changed file was not reloaded");
    require(![shown checkForChanges], "reloaded again without a change");
    [shown.watchButton performClick:nil]; // Off.
    require([files
                setAttributes:@{NSFileModificationDate : [NSDate dateWithTimeIntervalSinceNow:120]}
                 ofItemAtPath:room
                        error:nil] &&
                ![shown checkForChanges] && ![shown checkForChanges],
            "reloaded with the box unticked");
    [shown.watchButton performClick:nil]; // On again: the change made while off is not reloaded.
    std::filesystem::remove(file);
    require(![shown checkForChanges] && ![shown checkForChanges] && handler.dirty == 3,
            "a missing file was reloaded");

    // A list whose unused entry names a filter that is not installed (jrp's
    // review of fb0f549): a change to the entry that plays still reloads it.
    write_wav(file);
    {
        std::ofstream slow(folder / "slow.cfg");
        slow << "44100 1 1 0\n0\n0\nmissing.wav\n0\n0\n0\n";
        std::ofstream room_cfg(folder / "room.cfg");
        room_cfg << "48000 1 1 0\n0\n0\nroom.wav\n0\n0\n0\n";
        std::ofstream list(folder / "list.txt");
        list << "slow.cfg\nroom.cfg\n";
    }
    const int listed = handler.dirty;
    require([shown loadFile:@((folder / "list.txt").c_str())] && handler.dirty == listed + 1,
            "list not loaded: " + text(shown.explanationView.string));
    require([files
                setAttributes:@{NSFileModificationDate : [NSDate dateWithTimeIntervalSinceNow:180]}
                 ofItemAtPath:room
                        error:nil] &&
                ![shown checkForChanges] && [shown checkForChanges] && handler.dirty == listed + 2,
            "a list with a missing unused filter was not reloaded");

    // The filter replaced without the window reading the report (a preset
    // restored while the window was covered): a change to the file the
    // window loaded does not bring that file back.
    const auto other = folder / "other.wav";
    write_wav(other);
    require(c.load_file(other.string()), "other.wav not loaded");
    const int replaced = handler.dirty;
    require([files
                setAttributes:@{NSFileModificationDate : [NSDate dateWithTimeIntervalSinceNow:240]}
                 ofItemAtPath:room
                        error:nil] &&
                ![shown checkForChanges] && ![shown checkForChanges] && handler.dirty == replaced &&
                [shown.filterNameField.stringValue isEqualToString:@"other.wav"],
            "a change to the old file replaced the newer filter: " +
                text(shown.filterNameField.stringValue));
    std::filesystem::remove(other);
    std::filesystem::remove(file);
    std::filesystem::remove(folder / "list.txt");

    [shown updateRecentMenu];
    require([shown.recentButton.itemArray[1].title isEqualToString:@"No Recent Filters"] &&
                !shown.recentButton.itemArray[1].enabled,
            "a missing file in the Recent menu");
    std::filesystem::remove_all(folder);

    // Copy Diagnostics puts the report and the log lines on the pasteboard.
    shown.pasteboard = [NSPasteboard pasteboardWithUniqueName];
    [shown.diagnosticsButton performClick:nil];
    NSString* copied = [shown.pasteboard stringForType:NSPasteboardTypeString];
    require([copied rangeOfString:@"Recent Convolver log lines for VST3 #"].location != NSNotFound,
            "diagnostics: " + text(copied));

    // Showing the reference lines makes the view taller: it asks the host
    // for the new size, and the host's onSize keeps it in place.
    frame.asked.clear();
    [shown showReference:YES];
    require(!frame.asked.empty() &&
                frame.asked.back().getHeight() ==
                    static_cast<int32>(std::lround(NSHeight(shown.frame))) &&
                frame.asked.back().getWidth() == 560,
            "no resize request for a taller view");
    ViewRect now;
    require(view->getSize(&now) == kResultTrue && now.getHeight() == frame.asked.back().getHeight(),
            "the host's size was not kept");

    require(view->removed() == kResultOk && find_view(host.contentView) == nil, "remove");
    view->setFrame(nullptr);
    view->release();
    [host close];
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}
} // namespace

int main() {
    @autoreleasepool {
        try {
            [NSApplication sharedApplication];
            window();
        } catch (const std::exception& e) {
            std::cerr << e.what() << '\n';
            return 1;
        }
    }
    std::cout << "VST3 window checks passed\n";
    return 0;
}
