// SPDX-License-Identifier: GPL-2.0-or-later
// Configuration inspection and validation without rendering (Convolver::io).
#pragma once
#include "convolver/engine.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace convolver {
enum class InspectionProfile {
    convolver,      // Native validity on this machine.
    portable_paths, // Also warn about references that will not travel to other machines.
};
struct InspectionOptions {
    int expected_rate = 0;           // Zero: unspecified, as load_configuration.
    std::size_t expected_inputs = 0; // Zero: unspecified.
    std::size_t block_frames = 256;  // For latency/tail/storage analysis.
    bool syntax_only = false;        // One configuration text; IRs are not opened.
    bool all_configurations = false; // Validate every list leaf; no selectors allowed.
    InspectionProfile profile = InspectionProfile::convolver;
};
// Physical 1-based line and byte column; zero means unknown or the whole file.
struct SourcePosition {
    std::filesystem::path file;
    std::size_t line = 0, column = 0;
};
enum class DiagnosticSeverity { error, warning };
struct Diagnostic {
    std::string code; // Stable identifier, for example "missing-asset" or "syntax".
    DiagnosticSeverity severity = DiagnosticSeverity::error;
    std::string message; // Without the location prefix.
    SourcePosition location;
    std::vector<SourcePosition> include_chain; // Enclosing list entries, outermost first.
};
struct InspectedRoute {
    std::optional<std::string> token; // As written; absent for a sound file's implicit routes.
    std::size_t channel = 0;
    float gain = 1; // Effective: "-0" is channel 0 with gain -1.
};
struct InspectedDelay {
    std::optional<std::string> token;
    std::optional<float> milliseconds; // Declared value.
    std::size_t frames = 0;            // Effective, truncated.
};
struct InspectedFilterPath {
    std::string reference; // IR spelling as written (a sound file's own path otherwise).
    SourcePosition location;
    std::filesystem::path resolved; // Machine-specific.
    std::size_t ir_channel = 0;
    // Present once the IR has been opened and decoded.
    std::optional<int> ir_sample_rate;
    std::optional<std::size_t> ir_file_channels, ir_frames, partitions;
    std::vector<InspectedRoute> inputs, outputs;
};
struct InspectedConfiguration {
    std::filesystem::path file;
    std::vector<SourcePosition> include_chain;
    bool sound_file = false;
    bool valid = false;                 // No error diagnostics for this entry.
    bool coefficients_verified = false; // IRs decoded and engine validation passed.
    int sample_rate = 0;
    std::size_t input_channels = 0, output_channels = 0;
    std::uint32_t channel_mask = 0;
    std::vector<InspectedDelay> input_delays, output_delays;
    std::vector<InspectedFilterPath> paths;
    // Present only when coefficients were verified.
    std::optional<ConfigurationAnalysis> analysis;
};
struct SelectionStep {
    std::filesystem::path file;
    std::vector<SourcePosition> include_chain;
    // "list", "selected", "validated", "rate-mismatch", "input-mismatch",
    // "rate-and-input-mismatch" or "failed".
    std::string outcome;
    std::optional<int> sample_rate;
    std::optional<std::size_t> input_channels;
};
struct InspectionReport {
    static constexpr int schema_version = 1;
    std::filesystem::path path;
    InspectionOptions options;
    bool valid = false; // No error diagnostics and, unless all_configurations, a selection.
    std::string selection_reason;
    std::optional<std::filesystem::path> selected;
    std::vector<SelectionStep> traversal;               // Files in visiting order.
    std::vector<InspectedConfiguration> configurations; // Leaves inspected in detail.
    std::vector<Diagnostic> diagnostics;
};
// Inspect what a configuration, sound file or list resolves to, using the same
// parser, selection and limits as load_configuration, without rendering, FFT
// planning or writing files. Expected configuration problems become diagnostics;
// invalid options throw std::invalid_argument, and resource exhaustion or
// unexpected failures propagate as exceptions.
InspectionReport inspect_configuration(const std::filesystem::path&, const InspectionOptions& = {});
} // namespace convolver
