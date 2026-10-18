// SPDX-License-Identifier: GPL-2.0-or-later
// Lists and configurations resolved through the production resolver against a
// bounded in-memory file set: the input is split on NUL bytes into files named
// f0, f1, ... (at most 8); f0 is resolved. No machine file is ever opened.
#include "config_parse.hpp"
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace convolver::detail;
    std::map<std::string, std::string> files;
    std::string_view rest(reinterpret_cast<const char*>(data), size);
    for (int i = 0; i < 8 && !rest.empty(); ++i) {
        const auto end = rest.find('\0');
        files["f" + std::to_string(i)] = std::string(rest.substr(0, end));
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
    }
    const auto name = [](const std::filesystem::path& p) {
        const auto s = p.lexically_normal().generic_u8string();
        return std::string(s.begin(), s.end());
    };
    Resolver::Sources sources;
    sources.open_audio = [](const std::filesystem::path&) {
        return std::unique_ptr<convolver::AudioReader>();
    };
    sources.read_text = [&](const std::filesystem::path& p) {
        const auto it = files.find(name(p));
        if (it == files.end())
            throw ConfigurationError("unreadable", {p, 0, 0}, "not in the fuzz file set");
        return it->second;
    };
    sources.canonical = [](const std::filesystem::path& p) { return p.lexically_normal(); };
    sources.exists = [&](const std::filesystem::path& p) { return files.count(name(p)) > 0; };
    std::size_t leaves = 0;
    Resolver resolver(
        [&](const Resolver::Leaf& leaf) {
            if (leaf.document)
                (void)configuration_graph(leaf.document->configuration);
            ++leaves;
            return false; // Visit every entry, as all-configuration validation does.
        },
        [](const ConfigurationError&) { return true; }, {}, sources);
    try {
        resolver.run("f0");
    } catch (const ConfigurationError&) {
    }
    if (leaves > max_list_visits ||
        resolver.sources().size() > max_list_visits * (1 + max_filter_paths))
        std::abort();
    return 0;
}
