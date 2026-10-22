// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Preset files for every plug-in format, from one encoded host state (PLUG-3).
// The same CNVS bytes go into each container, so a filter sounds the same in
// VST3, AU and LV2. Nothing here needs a plug-in SDK. Control-thread only.
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace convolver::presets {
enum class Format { vst3, au, lv2 };
std::string_view format_name(Format); // "VST3", "AU" or "LV2"
// By file name: .vstpreset, .aupreset, or a .lv2 bundle folder (NAME.preset.lv2).
std::optional<Format> format_for(const std::filesystem::path&);

// Permanent names (owner's decision, 22 October 2026): AU-2 and LV2-2 read
// the encoded host state (bare, without VST3's length prefix) from these.
// The AU ClassInfo dictionary keeps Apple's standard keys (an empty "data",
// since the AU has no parameters, so AUBase::RestoreState still works) and adds:
inline constexpr std::string_view au_state_key = "convolver-state";
// LV2 state property, an atom:Chunk (xsd:base64Binary in Turtle): the base URI
// from plugins/identities.json followed by this.
inline constexpr std::string_view lv2_state_suffix = "state";
std::string lv2_state_uri();

// VST3: what IComponent::getState writes (a little-endian length, then the
// encoded state), and a complete .vstpreset holding it as the 'Comp' chunk,
// byte-identical to Steinberg's PresetFile::savePreset.
std::string vst3_class_id(); // 32 uppercase hex digits, as in .vstpreset headers
std::vector<std::byte> vst3_component_state(const HostState&);
std::vector<std::byte> vst3_preset(const HostState&);
// Reads the 'Comp' chunk of any .vstpreset for our processor class, including
// those hosts save with other chunks. Every version-1 state is accepted.
HostState read_vst3_preset(std::span<const std::byte>);

// AU: an XML property list (.aupreset) that the host gives to ClassInfo.
std::string au_preset(const HostState&, std::string_view name);

// LV2: a preset bundle (folder) with manifest.ttl and preset.ttl. The preset
// applies to the fixed layout whose channel counts match the filter.
struct Lv2Preset {
    std::string plugin_uri, manifest, preset;
};
// The fixed layout for these counts, or none (LV2 publishes only those).
const identity::Layout* lv2_layout(std::size_t inputs, std::size_t outputs);
Lv2Preset lv2_preset(const HostState&, std::string_view name);

std::string base64(std::span<const std::byte>);

// Checks that the state can be written in this format, with an explanatory
// message otherwise (channel limits, LV2 layouts, preset names).
void check_writable(Format, const HostState&, std::string_view name);
// Creates the file (or LV2 folder); never replaces an existing entry. A
// partly written LV2 folder is removed again.
void write_preset(Format, const HostState&, const std::filesystem::path&, std::string_view name);
} // namespace convolver::presets
