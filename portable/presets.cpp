// SPDX-License-Identifier: GPL-2.0-or-later
#include "presets.hpp"
#include "bytes.hpp"
#include "file.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace convolver::presets {
namespace {
constexpr std::size_t vst3_header_bytes = 48; // 'VST3', version, class ID, list offset
constexpr std::string_view base64_digits =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void append(std::vector<std::byte>& out, std::string_view text) {
    for (char c : text)
        out.push_back(static_cast<std::byte>(c));
}
void append_le32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(std::byte((value >> (8 * i)) & 255));
}
void append_le64(std::vector<std::byte>& out, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        out.push_back(std::byte((value >> (8 * i)) & 255));
}
std::uint64_t read_le64(std::span<const std::byte> bytes) {
    if (bytes.size() < 8)
        throw std::invalid_argument("truncated 64-bit value");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= std::to_integer<std::uint64_t>(bytes[i]) << (8 * i);
    return value;
}
bool equal_text(std::span<const std::byte> bytes, std::string_view text) {
    return bytes.size() >= text.size() &&
           std::equal(text.begin(), text.end(), bytes.begin(),
                      [](char c, std::byte b) { return static_cast<std::byte>(c) == b; });
}
char upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t minimum = 0, code = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            extra = 1, minimum = 0x80, code = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2, minimum = 0x800, code = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3, minimum = 0x10000, code = c & 0x07;
        } else {
            return false;
        }
        if (text.size() - i <= extra)
            return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80)
                return false;
            code = (code << 6) | (next & 0x3F);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
            return false;
        // Noncharacters (U+FDD0-U+FDEF, and U+FFFE/U+FFFF in every plane) are
        // not allowed in XML, so a property list holding one would not load.
        if ((code >= 0xFDD0 && code <= 0xFDEF) || (code & 0xFFFE) == 0xFFFE)
            return false;
        i += extra + 1;
    }
    return true;
}

// Preset names appear in XML and Turtle; both escape the few special characters
// below. Control characters have no portable spelling in XML 1.0, so they are
// refused rather than dropped.
std::string xml_text(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else
            out.push_back(c);
    }
    return out;
}
std::string turtle_string(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    return out + '"';
}

std::string channels_text(std::size_t inputs, std::size_t outputs) {
    return std::to_string(inputs) + (inputs == 1 ? " input and " : " inputs and ") +
           std::to_string(outputs) + (outputs == 1 ? " output" : " outputs");
}

std::string lv2_layout_list() {
    std::string list;
    for (const auto& layout : identity::fixed_layouts) {
        if (!list.empty())
            list += ", ";
        list += std::to_string(layout.inputs) + "->" + std::to_string(layout.outputs);
    }
    return list;
}
} // namespace

std::string_view format_name(Format format) {
    switch (format) {
    case Format::vst3:
        return "VST3";
    case Format::au:
        return "AU";
    case Format::lv2:
        return "LV2";
    }
    return "?";
}

std::optional<Format> format_for(const std::filesystem::path& path) {
    auto extension = path.extension().u8string();
    std::string lower;
    for (auto c : extension)
        lower.push_back(static_cast<char>(c >= u8'A' && c <= u8'Z' ? c - u8'A' + u8'a' : c));
    if (lower == ".vstpreset")
        return Format::vst3;
    if (lower == ".aupreset")
        return Format::au;
    if (lower == ".lv2")
        return Format::lv2;
    return std::nullopt;
}

std::string lv2_state_uri() {
    return std::string(identity::lv2_base_uri) + std::string(lv2_state_suffix);
}

std::string vst3_class_id() {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string text;
    for (auto word : identity::vst3_processor_class_id)
        for (int shift = 28; shift >= 0; shift -= 4)
            text.push_back(hex[(word >> shift) & 15]);
    return text;
}

std::vector<std::byte> vst3_component_state(const HostState& state) {
    auto encoded = encode_host_state(state);
    std::vector<std::byte> out;
    out.reserve(encoded.size() + 4);
    append_le32(out, static_cast<std::uint32_t>(encoded.size()));
    out.insert(out.end(), encoded.begin(), encoded.end());
    return out;
}

std::vector<std::byte> vst3_preset(const HostState& state) {
    // Steinberg's documented layout (public.sdk/source/vst/vstpresetfile.h): a
    // header, the chunk data, then the chunk list. savePreset with only a
    // component stream writes exactly these bytes.
    const auto component = vst3_component_state(state);
    std::vector<std::byte> out;
    out.reserve(vst3_header_bytes + component.size() + 28);
    append(out, "VST3");
    append_le32(out, 1);
    append(out, vst3_class_id());
    append_le64(out, vst3_header_bytes + component.size());
    out.insert(out.end(), component.begin(), component.end());
    append(out, "List");
    append_le32(out, 1);
    append(out, "Comp");
    append_le64(out, vst3_header_bytes);
    append_le64(out, component.size());
    return out;
}

HostState read_vst3_preset(std::span<const std::byte> bytes) {
    if (bytes.size() < vst3_header_bytes || !equal_text(bytes, "VST3"))
        throw std::invalid_argument("not a VST3 preset file (no 'VST3' header)");
    std::string class_id;
    for (auto b : bytes.subspan(8, 32)) {
        const auto c = upper(static_cast<char>(b));
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
            class_id.push_back(c);
        } else { // Not a class ID: show the byte, never copy it into a message.
            constexpr std::string_view hex = "0123456789ABCDEF";
            const auto v = std::to_integer<unsigned>(b);
            class_id += std::string("\\x") + hex[v >> 4] + hex[v & 15];
        }
    }
    if (class_id != vst3_class_id())
        throw std::invalid_argument("this VST3 preset belongs to another plug-in (class ID " +
                                    class_id + "), not Convolver (" + vst3_class_id() + ")");
    const auto list = read_le64(bytes.subspan(40));
    if (list == 0 || list > bytes.size() - 8 || !equal_text(bytes.subspan(list), "List"))
        throw std::invalid_argument("damaged VST3 preset: no chunk list");
    auto entries = bytes.subspan(list + 4);
    const auto count = detail::read_le32(entries);
    entries = entries.subspan(4);
    for (std::uint32_t i = 0; i < count && entries.size() >= 20;
         ++i, entries = entries.subspan(20)) {
        if (!equal_text(entries, "Comp"))
            continue;
        const auto offset = read_le64(entries.subspan(4));
        const auto size = read_le64(entries.subspan(12));
        if (offset > bytes.size() || size > bytes.size() - offset || size < 4)
            throw std::invalid_argument("damaged VST3 preset: component chunk out of range");
        const auto chunk = bytes.subspan(offset, size);
        // As the plug-in reads it (vst3::read_state): a length, then the state;
        // anything after the state in the chunk is ignored.
        const auto length = detail::read_le32(chunk);
        if (length < 12 || length > max_host_state_bytes || length > chunk.size() - 4)
            throw std::invalid_argument("damaged VST3 preset: invalid state length");
        return decode_host_state(chunk.subspan(4, length));
    }
    throw std::invalid_argument("VST3 preset holds no component state ('Comp' chunk)");
}

std::string base64(std::span<const std::byte> data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const auto v = std::to_integer<std::uint32_t>(data[i]) << 16 |
                       std::to_integer<std::uint32_t>(data[i + 1]) << 8 |
                       std::to_integer<std::uint32_t>(data[i + 2]);
        for (int shift = 18; shift >= 0; shift -= 6)
            out.push_back(base64_digits[(v >> shift) & 63]);
    }
    if (const auto left = data.size() - i) {
        auto v = std::to_integer<std::uint32_t>(data[i]) << 16;
        if (left == 2)
            v |= std::to_integer<std::uint32_t>(data[i + 1]) << 8;
        out.push_back(base64_digits[(v >> 18) & 63]);
        out.push_back(base64_digits[(v >> 12) & 63]);
        out.push_back(left == 2 ? base64_digits[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::string au_preset(const HostState& state, std::string_view name) {
    const auto encoded = encode_host_state(state);
    const auto text = base64(encoded);
    std::string data;
    for (std::size_t i = 0; i < text.size(); i += 68)
        data += "\t" + text.substr(i, 68) + "\n";
    auto code = [](std::string_view four) {
        return std::to_string(static_cast<std::int32_t>(identity::four_char(four)));
    };
    // Keys in the order Core Foundation writes them (sorted). The standard
    // keys match AUBase::SaveState for an AU without parameters.
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n"
           "<dict>\n"
           "\t<key>" +
           std::string(au_state_key) +
           "</key>\n"
           "\t<data>\n" +
           data +
           "\t</data>\n"
           "\t<key>data</key>\n"
           "\t<data>\n"
           "\t</data>\n"
           "\t<key>manufacturer</key>\n"
           "\t<integer>" +
           code(identity::au_manufacturer) +
           "</integer>\n"
           "\t<key>name</key>\n"
           "\t<string>" +
           xml_text(name) +
           "</string>\n"
           "\t<key>subtype</key>\n"
           "\t<integer>" +
           code(identity::au_subtype) +
           "</integer>\n"
           "\t<key>type</key>\n"
           "\t<integer>" +
           code(identity::au_type) +
           "</integer>\n"
           "\t<key>version</key>\n"
           "\t<integer>0</integer>\n"
           "</dict>\n"
           "</plist>\n";
}

const identity::Layout* lv2_layout(std::size_t inputs, std::size_t outputs) {
    for (const auto& layout : identity::fixed_layouts)
        if (static_cast<std::size_t>(layout.inputs) == inputs &&
            static_cast<std::size_t>(layout.outputs) == outputs)
            return &layout;
    return nullptr;
}

Lv2Preset lv2_preset(const HostState& state, std::string_view name) {
    check_writable(Format::lv2, state, name);
    const auto& c = *state.configuration;
    const auto* layout = lv2_layout(c.input_channels, c.output_channels);
    Lv2Preset preset;
    preset.plugin_uri = std::string(identity::lv2_base_uri) + std::string(layout->id);
    // The forms lilv writes when it saves a preset: the manifest names the
    // preset and its plug-in; the preset file holds the label and state.
    preset.manifest = "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
                      "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
                      "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
                      "\n"
                      "<preset.ttl>\n"
                      "\ta pset:Preset ;\n"
                      "\tlv2:appliesTo <" +
                      preset.plugin_uri +
                      "> ;\n"
                      "\trdfs:seeAlso <preset.ttl> .\n";
    preset.preset = "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
                    "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
                    "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
                    "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
                    "@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n"
                    "\n"
                    "<>\n"
                    "\ta pset:Preset ;\n"
                    "\tlv2:appliesTo <" +
                    preset.plugin_uri +
                    "> ;\n"
                    "\trdfs:label " +
                    turtle_string(name) +
                    " ;\n"
                    "\tstate:state [\n"
                    "\t\t<" +
                    lv2_state_uri() + "> \"" + base64(encode_host_state(state)) +
                    "\"^^xsd:base64Binary\n"
                    "\t] .\n";
    return preset;
}

void check_writable(Format format, const HostState& state, std::string_view name) {
    const auto label = std::string(format_name(format));
    // A .vstpreset stores no name (hosts show its file name), so only AU and
    // LV2 presets need one that XML and Turtle can hold.
    if (format != Format::vst3) {
        if (name.empty())
            throw std::invalid_argument("the preset name is empty; give one with --name");
        if (!valid_utf8(name))
            throw std::invalid_argument(
                "the preset name (by default the file name) is not valid Unicode text, or "
                "holds characters that AU and LV2 presets cannot store; give one with --name");
        if (std::any_of(name.begin(), name.end(),
                        [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; }))
            throw std::invalid_argument(
                "the preset name (by default the file name) contains control characters such "
                "as tabs or line breaks; give a plain one with --name");
    }
    if (!state.configuration) {
        if (format == Format::lv2)
            throw std::invalid_argument(
                "this preset holds no filter (an empty plug-in), so it has no channel counts "
                "to choose an LV2 plug-in by");
        return;
    }
    const auto& c = *state.configuration;
    const auto low = static_cast<std::size_t>(identity::min_channels);
    const auto high = static_cast<std::size_t>(identity::max_channels);
    if (c.input_channels < low || c.input_channels > high || c.output_channels < low ||
        c.output_channels > high)
        throw std::invalid_argument(label + " presets support " + std::to_string(low) + " to " +
                                    std::to_string(high) +
                                    " input and output channels; this filter has " +
                                    channels_text(c.input_channels, c.output_channels));
    if (format == Format::lv2 && !lv2_layout(c.input_channels, c.output_channels))
        throw std::invalid_argument(
            "the LV2 plug-in comes in fixed channel layouts (inputs->outputs: " +
            lv2_layout_list() + "), and this filter has " +
            channels_text(c.input_channels, c.output_channels) +
            ". Use the VST3 or AU plug-in for it, or ask for the layout to be added "
            "(docs/plugin-identities.md).");
}

void write_preset(Format format, const HostState& state, const std::filesystem::path& path,
                  std::string_view name) {
    check_writable(format, state, name);
    auto bytes = [](std::string_view text) {
        return std::as_bytes(std::span(text.data(), text.size()));
    };
    switch (format) {
    case Format::vst3: {
        const auto data = vst3_preset(state);
        detail::write_new_file(path, data);
        return;
    }
    case Format::au: {
        const auto text = au_preset(state, name);
        detail::write_new_file(path, bytes(text));
        return;
    }
    case Format::lv2: {
        const auto preset = lv2_preset(state, name);
        std::error_code error;
        if (!std::filesystem::create_directory(path, error) || error)
            throw std::runtime_error("cannot create the LV2 preset folder (must be a new path)");
        try {
            detail::write_new_file(path / "manifest.ttl", bytes(preset.manifest));
            detail::write_new_file(path / "preset.ttl", bytes(preset.preset));
        } catch (...) {
            // Only what this call created: the folder was new and empty.
            std::filesystem::remove(path / "manifest.ttl", error);
            std::filesystem::remove(path / "preset.ttl", error);
            std::filesystem::remove(path, error);
            throw;
        }
        return;
    }
    }
}
} // namespace convolver::presets
