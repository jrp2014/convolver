// SPDX-License-Identifier: GPL-2.0-or-later
// Configuration/list text through the production parser. Only ConfigurationError
// is an expected outcome; any other exception or crash is a finding.
#include "config_parse.hpp"
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace convolver::detail;
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    try {
        const auto doc = parse_configuration_text(text, "fuzz.cfg");
        if (doc.kind == ParsedDocument::Kind::list)
            return 0;
        const auto& c = doc.configuration;
        // Invariants the loader and engine rely on.
        if (c.input_delays.size() != c.input_channels ||
            c.output_delays.size() != c.output_channels || c.paths.empty() ||
            c.paths.size() > max_filter_paths)
            std::abort();
        for (const auto& p : c.paths) {
            for (const auto& r : p.inputs)
                if (r.channel >= c.input_channels || r.gain == 0)
                    std::abort();
            for (const auto& r : p.outputs)
                if (r.channel >= c.output_channels || r.gain == 0)
                    std::abort();
        }
        for (const auto& d : c.input_delays)
            if (d.frames > max_delay_frames)
                std::abort();
        (void)configuration_graph(c);
    } catch (const ConfigurationError&) {
    }
    return 0;
}
