// SPDX-License-Identifier: GPL-2.0-or-later
// Private: legacy configuration text parsing and list/asset resolution, shared by
// the loader (config.cpp) and inspection (inspection.cpp). Not installed.
#pragma once
#include "convolver/io.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace convolver::detail {
// Limits shared by loading, inspection and tests.
inline constexpr std::size_t max_text_bytes = 1024 * 1024;
inline constexpr std::size_t max_decoded_samples = 64 * 1024 * 1024;
inline constexpr std::size_t max_delay_frames = 16 * 1024 * 1024;
inline constexpr std::size_t max_list_depth = 16;
inline constexpr std::size_t max_list_visits = 4096;
inline constexpr std::size_t max_filter_paths = 4096;
inline constexpr std::size_t max_routes = 4096;
// Configuration/list text read by one resolution; a shared sublist counts each read.
inline constexpr std::size_t max_total_text_bytes = 16 * 1024 * 1024;

// Physical 1-based line and byte column (a UTF-8 BOM is not counted). Zero means
// the location is unknown or the whole file.
struct SourceLocation {
    std::filesystem::path file;
    std::size_t line = 0, column = 0;
};
// One list entry that led to a nested file, outermost first.
using IncludeChain = std::vector<SourceLocation>;

std::string utf8_string(const std::filesystem::path&);

// Expected configuration failures. what() is
// "invalid configuration: FILE:LINE:COLUMN: REASON" plus "; included from LIST:LINE"
// for each enclosing list, innermost first.
class ConfigurationError : public std::runtime_error {
  public:
    ConfigurationError(std::string code, SourceLocation, std::string reason);
    const char* what() const noexcept override;
    const std::string& code() const noexcept {
        return code_;
    }
    const SourceLocation& location() const noexcept {
        return location_;
    }
    const std::string& reason() const noexcept {
        return reason_;
    }
    // Outermost first.
    const IncludeChain& include_chain() const noexcept {
        return chain_;
    }
    void included_from(SourceLocation);

  private:
    void format();
    std::string code_;
    SourceLocation location_;
    std::string reason_;
    IncludeChain chain_;
    std::string message_;
};

struct ParsedReference {
    std::string spelling; // As written, after comment removal and trimming.
    SourceLocation location;
};
struct ParsedRoute {
    std::string token;
    std::size_t column = 0;
    std::size_t channel = 0;
    float gain = 1; // Effective: -0 becomes channel 0 with gain -1.
};
struct ParsedDelay {
    std::string token;
    std::size_t column = 0;
    float milliseconds = 0;
    std::size_t frames = 0; // Truncated, as rendering uses.
};
struct ParsedPath {
    ParsedReference ir;
    std::string channel_token;
    SourceLocation channel_location;
    std::size_t ir_channel = 0;
    std::vector<ParsedRoute> inputs, outputs;
    std::size_t input_line = 0, output_line = 0;
};
struct ParsedConfiguration {
    std::vector<std::string> header_tokens;
    std::size_t header_line = 0;
    int sample_rate = 0;
    std::size_t input_channels = 0, output_channels = 0;
    std::uint32_t channel_mask = 0;
    std::vector<ParsedDelay> input_delays, output_delays;
    std::size_t input_delay_line = 0, output_delay_line = 0;
    std::vector<ParsedPath> paths;
};
struct ParsedDocument {
    enum class Kind { configuration, list };
    Kind kind = Kind::configuration;
    std::filesystem::path source;
    ParsedConfiguration configuration;    // Kind::configuration only.
    std::vector<ParsedReference> entries; // Kind::list only.
};

// Parse bounded configuration or list text. No filesystem or decoder access.
// Throws ConfigurationError with the physical location of the first problem.
ParsedDocument parse_configuration_text(std::string_view text, const std::filesystem::path& source);
// Read at most max_text_bytes; throws ConfigurationError if unreadable/too large.
std::string read_configuration_text(const std::filesystem::path&);

// How a reference spelled in owner resolves on this platform, without opening it.
// Throws ConfigurationError (code "nonportable-path") for Windows drive, UNC and
// rooted references on non-Windows platforms; the reason names any existing
// relative replacement beside the owner.
// file_exists replaces the filesystem check used for that suggestion.
using FileExists = std::function<bool(const std::filesystem::path&)>;
std::filesystem::path resolve_reference(const std::filesystem::path& owner, const ParsedReference&,
                                        const FileExists& = {});

// Header, delays and routes without coefficients.
Configuration configuration_graph(const ParsedConfiguration&);
// Zero expected values are unspecified and match anything.
bool matches(int sample_rate, std::size_t input_channels, std::size_t output_channels,
             const ConfigurationSelection&);

// A filter considered during selection, for explaining why none matched.
struct SelectionCandidate {
    std::filesystem::path path;
    bool sound_file = false;
    int sample_rate = 0;
    std::size_t input_channels = 0, output_channels = 0;
};
// A self-contained explanation of why no candidate matches `wanted`, naming each
// difference and what to change. `subject` says where the wanted properties came
// from, for example "the input audio". `list` is true when `filter` is a list.
std::string explain_no_match(const std::filesystem::path& filter, bool list,
                             const std::vector<SelectionCandidate>& candidates,
                             const ConfigurationSelection& wanted, std::string_view subject);

struct DecodedIr {
    std::filesystem::path resolved;
    AudioInfo info{};
    std::size_t frames = 0;
};
// Decode every path's selected IR channel into cfg (from configuration_graph).
// budget counts decoded samples across one resolution operation.
std::vector<DecodedIr> decode_impulses(const ParsedDocument&, Configuration& cfg,
                                       std::size_t& budget);
// A sound file used directly: diagonal routing, decoded coefficients.
Configuration decode_audio_filter(AudioReader&, std::size_t& budget, const SourceLocation&);

// Walks a configuration, sound file or (nested) list in entry order, with the
// historical limits and cycle detection, and hands each leaf to a visitor.
class Resolver {
  public:
    struct Leaf {
        const std::filesystem::path& path;
        const IncludeChain& chain;
        const ParsedDocument* document; // Null for a sound file.
        AudioReader* audio;             // Null for configuration text.
    };
    // Return true to stop traversal (first match found).
    using Visitor = std::function<bool(const Leaf&)>;
    // Called for a failed entry when set; return true to continue with siblings.
    using ErrorHandler = std::function<bool(const ConfigurationError&)>;
    // Lists, before their entries are visited.
    using ListObserver = std::function<void(const ParsedDocument&, const IncludeChain&)>;

    // Replaceable file access, for bounded in-memory fuzzing. Empty members use
    // the filesystem, libsndfile and read_configuration_text.
    struct Sources {
        std::function<std::unique_ptr<AudioReader>(const std::filesystem::path&)> open_audio;
        std::function<std::string(const std::filesystem::path&)> read_text;
        std::function<std::filesystem::path(const std::filesystem::path&)> canonical;
        FileExists exists;
    };
    explicit Resolver(Visitor, ErrorHandler = {}, ListObserver = {}, Sources = {});
    // Returns true if the visitor stopped traversal.
    bool run(const std::filesystem::path&);
    // Every configuration, list and referenced IR visited, canonical, in order.
    const std::vector<std::filesystem::path>& sources() const noexcept {
        return sources_;
    }

  private:
    bool visit(const std::filesystem::path&);
    std::filesystem::path canonical(const std::filesystem::path&, const SourceLocation&);
    Visitor visitor_;
    ErrorHandler on_error_;
    ListObserver on_list_;
    Sources files_;
    std::size_t text_bytes_ = 0;
    std::set<std::filesystem::path> active_;
    std::vector<std::filesystem::path> sources_;
    IncludeChain chain_;
    std::size_t visits_ = 0;
};
} // namespace convolver::detail
