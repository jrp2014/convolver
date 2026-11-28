// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11, VST3-12: what the window tests (editor_tests.mm on macOS,
// editor_win_tests.cpp on Windows) play the host with: a component handler
// that records the edits, a frame that records resize requests, and a
// filter file to load, and the plug-in set up as the Mix-note cases need.
#pragma once
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace convolver::vst3::test {
using namespace Steinberg;
using namespace Steinberg::Vst;

inline void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
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
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        QUERY_INTERFACE(_iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(_iid, obj, IComponentHandler::iid, IComponentHandler)
        QUERY_INTERFACE(_iid, obj, IComponentHandler2::iid, IComponentHandler2)
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
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        QUERY_INTERFACE(_iid, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(_iid, obj, IPlugFrame::iid, IPlugFrame)
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

// PLUG-22 with PLUG-23, CI-67: the host side of both windows' Mix-note
// case. A six-channel (5.1) track restores a saved 2->6 crossover and
// activates it, so the filter leaves no dry signal and the report dims Mix.
// Each window test then attaches its own view to `frame` and inspects its
// own controls and note: a passing Cocoa test says nothing about a Windows
// control. close() deactivates, disconnects and terminates, as a host
// does, once the test has removed its view; a failed check skips it, as
// the tests always have.
struct SurroundCrossover {
    SurroundCrossover() {
        require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                    c.setComponentHandler(&handler) == kResultOk && p.connect(&c) == kResultOk &&
                    c.connect(&p) == kResultOk,
                "mix note initialize");
        SpeakerArrangement six = SpeakerArr::k51;
        require(p.setBusArrangements(&six, 1, &six, 1) == kResultOk, "mix note buses");
        p.activateBus(kAudio, kInput, 0, true);
        p.activateBus(kAudio, kOutput, 0, true);
        HostState saved;
        Configuration x{48000, 2, 6, 0, {}, {}, {}};
        for (std::size_t o = 0; o < 6; ++o)
            x.paths.push_back({{1.f}, {{o % 2, 1}}, {{o, 1}}});
        saved.configuration = x;
        MemoryStream state;
        require(write_state(&state, saved), "mix note state");
        state.seek(0, IBStream::kIBSeekSet, nullptr);
        ProcessSetup setup{kRealtime, kSample32, 512, 48000};
        require(p.setState(&state) == kResultOk && p.setupProcessing(setup) == kResultOk &&
                    p.setActive(true) == kResultOk,
                "mix note activate");
    }
    SurroundCrossover(const SurroundCrossover&) = delete;
    SurroundCrossover& operator=(const SurroundCrossover&) = delete;
    void close() {
        p.setActive(false);
        p.disconnect(&c);
        c.disconnect(&p);
        c.terminate();
        p.terminate();
    }
    HostApplication context;
    Handler handler;
    Frame frame;
    Processor p;
    Controller c;
};

// A 16-bit mono WAV at 48 kHz with an impulse of one half.
inline void write_wav(const std::filesystem::path& path) {
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
} // namespace convolver::vst3::test
