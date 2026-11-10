// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11, VST3-12: what the window tests (editor_tests.mm on macOS,
// editor_win_tests.cpp on Windows) play the host with: a component handler
// that records the edits, a frame that records resize requests, and a
// filter file to load.
#pragma once
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
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
