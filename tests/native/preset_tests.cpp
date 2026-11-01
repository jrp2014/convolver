// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-3: preset containers built without any plug-in SDK. The VST3 writer is
// also compared byte for byte with Steinberg's (tests/vst3/preset_format_tests.cpp),
// and tests/plugins/preset_tests.py reads all three formats independently.
#include "presets.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace convolver;
namespace {
void require(bool v, const std::string& m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> std::string rejects(F f, const std::string& what) {
    try {
        f();
    } catch (const std::exception& e) {
        return e.what();
    }
    throw std::runtime_error("accepted: " + what);
}
std::span<const std::byte> bytes(std::string_view text) {
    return std::as_bytes(std::span(text.data(), text.size()));
}
void put(std::vector<std::byte>& data, std::size_t at, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i)
        data[at + i] = static_cast<std::byte>(text[i]);
}
void put64(std::vector<std::byte>& data, std::size_t at, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        data[at + i] = std::byte((value >> (8 * i)) & 255);
}
HostState routed(std::size_t inputs, std::size_t outputs, std::uint32_t mask = 0) {
    Configuration c{48000, inputs, outputs, mask, {}, {}, {}};
    c.input_delays.assign(inputs, 0);
    c.output_delays.assign(outputs, 0);
    for (std::size_t o = 0; o < outputs; ++o)
        c.paths.push_back({{1.f, -.5f, .25f},
                           {{static_cast<std::uint32_t>(o % inputs), 1.f}},
                           {{static_cast<std::uint32_t>(o), .5f}}});
    return {std::move(c), false};
}
bool same(const HostState& a, const HostState& b) {
    return encode_host_state(a) == encode_host_state(b);
}
} // namespace

int main() {
    try {
        // RFC 4648 section 10 test vectors.
        const char* vectors[][2] = {{"", ""},
                                    {"f", "Zg=="},
                                    {"fo", "Zm8="},
                                    {"foo", "Zm9v"},
                                    {"foob", "Zm9vYg=="},
                                    {"fooba", "Zm9vYmE="},
                                    {"foobar", "Zm9vYmFy"}};
        for (auto& v : vectors)
            require(presets::base64(bytes(v[0])) == v[1], std::string("base64 of ") + v[0]);

        using presets::Format;
        require(presets::format_for("a/Room.vstpreset") == Format::vst3, "vstpreset");
        require(presets::format_for("Room.AUPRESET") == Format::au, "aupreset, any case");
        require(presets::format_for("Room.preset.lv2") == Format::lv2, "LV2 bundle");
        require(!presets::format_for("Room.cfg") && !presets::format_for("Room"), "unknown");

        // The VST3 container, field by field (vstpresetfile.h).
        const auto stereo = routed(2, 2, 3);
        const auto component = presets::vst3_component_state(stereo);
        const auto file = presets::vst3_preset(stereo);
        const auto encoded = encode_host_state(stereo);
        require(component.size() == encoded.size() + 4 &&
                    std::equal(encoded.begin(), encoded.end(), component.begin() + 4),
                "component state is a length and the encoded state");
        require(file.size() == 48 + component.size() + 28, "header, one chunk and its list");
        const auto id = presets::vst3_class_id();
        require(id.size() == 32 && std::memcmp(file.data(), "VST3\1\0\0\0", 8) == 0 &&
                    std::memcmp(file.data() + 8, id.data(), 32) == 0,
                "header and class ID");
        require(std::memcmp(file.data() + 48 + component.size(), "List\1\0\0\0Comp", 12) == 0,
                "chunk list");
        require(same(presets::read_vst3_preset(file), stereo), "VST3 round trip");

        // Hosts save other chunks too, in any order, and may pad the state;
        // a lower-case class ID is the same ID.
        auto hosted = file;
        const auto list_at = 48 + component.size();
        hosted.resize(list_at); // drop the list, append a controller chunk and a new list
        const auto cont_at = hosted.size();
        for (char c : std::string_view("controller"))
            hosted.push_back(static_cast<std::byte>(c));
        hosted.push_back(std::byte{0}); // padding after the state inside 'Comp' is ignored
        const auto new_list = hosted.size();
        hosted.resize(new_list + 8 + 2 * 20);
        put(hosted, new_list, "List");
        hosted[new_list + 4] = std::byte{2};
        put(hosted, new_list + 8, "Cont");
        put64(hosted, new_list + 12, cont_at);
        put64(hosted, new_list + 20, 10);
        put(hosted, new_list + 28, "Comp");
        put64(hosted, new_list + 32, 48);
        put64(hosted, new_list + 40, component.size() + 11);
        put64(hosted, 40, new_list);
        for (std::size_t i = 8; i < 40; ++i)
            if (auto c = static_cast<char>(hosted[i]); c >= 'A' && c <= 'F')
                hosted[i] = static_cast<std::byte>(c - 'A' + 'a');
        require(same(presets::read_vst3_preset(hosted), stereo), "host-saved VST3 preset");

        // Version-1 states are never rejected for their layout: any mask, any
        // count pairing within the limits, and the empty (identity) instance.
        for (std::uint32_t mask : {0U, 1U, 0x3FU, 0x3FFFFU, 0xFFFFFFFFU}) {
            const auto upmix = routed(2, 6, mask);
            require(same(presets::read_vst3_preset(presets::vst3_preset(upmix)), upmix),
                    "any version-1 mask");
        }
        const HostState empty{};
        require(!presets::read_vst3_preset(presets::vst3_preset(empty)).configuration,
                "empty instance");

        // Damage: every truncation, foreign plug-ins, broken lists.
        for (std::size_t n = 0; n < file.size(); ++n)
            rejects([&] { presets::read_vst3_preset(std::span(file).first(n)); }, "truncated");
        auto foreign = file;
        put(foreign, 8, "0123456789ABCDEF0123456789ABCDEF");
        require(rejects([&] { presets::read_vst3_preset(foreign); }, "foreign")
                        .find("another plug-in") != std::string::npos,
                "foreign class explained");
        auto no_comp = file;
        put(no_comp, list_at + 8, "Cont");
        rejects([&] { presets::read_vst3_preset(no_comp); }, "no component chunk");
        auto far = file;
        put64(far, list_at + 12, file.size());
        rejects([&] { presets::read_vst3_preset(far); }, "chunk past the end");
        auto huge = file;
        put64(huge, list_at + 20, ~std::uint64_t{0});
        rejects([&] { presets::read_vst3_preset(huge); }, "chunk size overflow");
        auto lost = file;
        put64(lost, 40, ~std::uint64_t{0});
        rejects([&] { presets::read_vst3_preset(lost); }, "list offset overflow");

        // AU: Apple's standard keys plus ours; the name is XML-escaped.
        const auto au = presets::au_preset(stereo, "Room <A&B>");
        for (const char* part :
             {"<key>convolver-state</key>", "<key>data</key>\n\t<data>\n\t</data>",
              "<key>version</key>\n\t<integer>0</integer>",
              "<key>type</key>\n\t<integer>1635083896</integer>",
              "<string>Room &lt;A&amp;B&gt;</string>"})
            require(au.find(part) != std::string::npos, std::string("AU preset has ") + part);

        // LV2: one preset per fixed layout, chosen by the filter's counts.
        for (const auto& layout : identity::fixed_layouts) {
            const auto preset = presets::lv2_preset(routed(layout.inputs, layout.outputs), "Room");
            require(preset.plugin_uri ==
                        std::string(identity::lv2_base_uri) + std::string(layout.id),
                    "LV2 plug-in URI");
            require(preset.manifest.find("<" + preset.plugin_uri + ">") != std::string::npos &&
                        preset.preset.find("<" + presets::lv2_state_uri() + "> \"") !=
                            std::string::npos,
                    "LV2 manifest and state key");
        }
        const auto quoted = presets::lv2_preset(routed(2, 2), "Say \"hi\" \\ there");
        require(quoted.preset.find("rdfs:label \"Say \\\"hi\\\" \\\\ there\" ;") !=
                    std::string::npos,
                "Turtle escaping");
        const auto why =
            rejects([&] { presets::check_writable(Format::lv2, routed(3, 3), "x"); }, "3->3");
        require(why.find("2->6") != std::string::npos && why.find("3 inputs") != std::string::npos,
                "LV2 layouts explained");
        rejects([&] { presets::check_writable(Format::lv2, empty, "x"); }, "LV2 empty");
        presets::check_writable(Format::au, empty, "x");
        HostState failed;
        failed.failed = true;
        for (auto format : {Format::vst3, Format::au, Format::lv2})
            require(rejects([&] { presets::check_writable(format, failed, "x"); }, "failed load")
                            .find("failed to load") != std::string::npos,
                    "a saved failed load is converted");
        // PLUG-12: one sample more than 64 MiB of saved state holds. With its
        // two delays, this mono filter takes 19 words besides its samples,
        // and PLUG-13 keeps 5 more free for a VST3 host's settings.
        HostState oversized = routed(1, 1);
        oversized.configuration->paths.front().impulse.assign(max_host_state_bytes / 4 - 23, 0.0F);
        require(encoded_host_state_size(oversized) == max_host_state_bytes - 16,
                "oversized state is not one word short of the limit without settings");
        for (auto format : {Format::vst3, Format::au, Format::lv2})
            require(rejects([&] { presets::check_writable(format, oversized, "x"); }, "oversized")
                            .find("too large to save") != std::string::npos,
                    "an oversized filter is refused and explained");
        oversized.configuration->paths.front().impulse.pop_back();
        for (auto format : {Format::vst3, Format::au, Format::lv2})
            presets::check_writable(format, oversized, "x");
        for (auto format : {Format::vst3, Format::au, Format::lv2}) {
            require(rejects([&] { presets::check_writable(format, routed(9, 9), "x"); }, "9x9")
                            .find("1 to 8") != std::string::npos,
                    "channel limit explained");
            // Empty, a tab, invalid UTF-8, an overlong '/', U+FFFE, U+FDD0 and
            // U+10FFFF: none can be stored in XML and Turtle. A .vstpreset stores
            // no name, so any file name will do for one.
            for (std::string_view name :
                 {std::string_view(""), std::string_view("a\tb"), std::string_view("\xff"),
                  std::string_view("\xc0\xaf"), std::string_view("a\xef\xbf\xbe"),
                  std::string_view("\xef\xb7\x90"), std::string_view("\xf4\x8f\xbf\xbf")}) {
                if (format == Format::vst3)
                    presets::check_writable(format, stereo, name);
                else
                    rejects([&] { presets::check_writable(format, stereo, name); }, "bad name");
            }
            presets::check_writable(format, stereo, "Raum \xc3\xbc \xf0\x9f\x8e\xa7");
        }
        std::cout << "Preset containers: VST3, AU and LV2 from one host state\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
