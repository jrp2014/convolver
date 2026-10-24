// SPDX-License-Identifier: GPL-2.0-or-later
#include "inspection_cli.hpp"
#include "convolver/io.hpp"
#include "convolver_version.hpp"
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace convolver::cli {
namespace {
constexpr auto usage = R"(Usage: convolver --inspect CONFIG [options]
       convolver --validate CONFIG [options]
  --sample-rate HZ         Select as rendering would for this input rate
  --input-channels N       Select as rendering would for this input channel count
  --output-channels N      Select as rendering would for this output channel count
  --block-size N           Partition size for latency/tail/storage (default 256)
  --all-configurations     Validate every list entry (no rate/channel selectors)
  --syntax-only            Check one configuration's text; do not open IR files
  --profile P              convolver (default) or portable-paths
  --raw-filter-format F    Sample type of headerless .pcm/.raw filters (default
                           f32le; any --raw-format name)
  --warnings-as-errors     Fail when any warning is reported
  --json                   Print one JSON report (schema 1) on stdout
Exit status: 0 valid, 1 invalid or unreadable, 2 invalid usage.
)";
struct Usage : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};
std::string text(const std::filesystem::path& p) {
    const auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}
std::filesystem::path utf8_path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
std::size_t count(std::string_view s, std::size_t limit, const char* what) {
    std::size_t n = 0;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (error != std::errc{} || end != s.data() + s.size() || n == 0 || n > limit)
        throw Usage(std::string(what) + " must be an integer from 1 to " + std::to_string(limit));
    return n;
}
// Shortest decimal that reads back as the same float, in the C locale.
std::string decimal(float v) {
    char buffer[32];
    for (int precision = 1; precision <= 9; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, static_cast<double>(v));
        if (std::strtof(buffer, nullptr) == v)
            break;
    }
    return buffer;
}
const char* severity(DiagnosticSeverity s) {
    return s == DiagnosticSeverity::error ? "error" : "warning";
}
const char* profile_name(InspectionProfile p) {
    return p == InspectionProfile::portable_paths ? "portable-paths" : "convolver";
}

// Writer for the fixed schema only: not a general JSON library.
class Json {
  public:
    std::string out;
    bool replaced_invalid_utf8 = false;
    void open(char c) {
        separator();
        out += c;
        first_.push_back(true);
    }
    void close(char c) {
        out += c;
        first_.pop_back();
    }
    void key(std::string_view k) {
        separator();
        quoted(k);
        out += ':';
        pending_ = true;
    }
    void string(std::string_view s) {
        separator();
        quoted(s);
    }
    void raw(std::string_view s) {
        separator();
        out += s;
    }
    void number(std::size_t n) {
        raw(std::to_string(n));
    }
    void number(float f) {
        raw(decimal(f));
    }
    void boolean(bool b) {
        raw(b ? "true" : "false");
    }
    void null() {
        raw("null");
    }
    template <class T> void optional(const std::optional<T>& v) {
        if (v)
            value(*v);
        else
            null();
    }
    void value(const std::string& s) {
        string(s);
    }
    void value(float f) {
        number(f);
    }
    void value(std::size_t n) {
        number(n);
    }
    void value(int n) {
        raw(std::to_string(n));
    }

  private:
    void separator() {
        if (pending_) {
            pending_ = false;
            return;
        }
        if (!first_.empty()) {
            if (!first_.back())
                out += ',';
            first_.back() = false;
        }
    }
    void quoted(std::string_view s) {
        out += '"';
        for (std::size_t i = 0; i < s.size();) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                switch (c) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20 || c == 0x7f) {
                        char escape[8];
                        std::snprintf(escape, sizeof escape, "\\u%04x", c);
                        out += escape;
                    } else
                        out += static_cast<char>(c);
                }
                ++i;
                continue;
            }
            const auto length = utf8_length(s.substr(i));
            if (length) {
                out.append(s.substr(i, length));
                i += length;
            } else {
                out += "\\ufffd";
                replaced_invalid_utf8 = true;
                ++i;
            }
        }
        out += '"';
    }
    // Length of one well-formed UTF-8 sequence at the start of s, or zero.
    static std::size_t utf8_length(std::string_view s) {
        const auto b = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
        const auto c0 = b(0);
        std::size_t n = c0 >= 0xf0 ? 4 : c0 >= 0xe0 ? 3 : c0 >= 0xc2 ? 2 : 0;
        if (!n || c0 > 0xf4 || s.size() < n)
            return 0;
        for (std::size_t i = 1; i < n; ++i)
            if ((b(i) & 0xc0) != 0x80)
                return 0;
        if ((c0 == 0xe0 && b(1) < 0xa0) || (c0 == 0xed && b(1) > 0x9f) ||
            (c0 == 0xf0 && b(1) < 0x90) || (c0 == 0xf4 && b(1) > 0x8f))
            return 0; // Overlong, surrogate or beyond U+10FFFF.
        return n;
    }
    std::vector<bool> first_;
    bool pending_ = false;
};

void position(Json& j, const SourcePosition& p) {
    j.key("file");
    if (p.file.empty())
        j.null();
    else
        j.string(text(p.file));
    j.key("line");
    p.line ? j.number(p.line) : j.null();
    j.key("column");
    p.column ? j.number(p.column) : j.null();
}
void positions(Json& j, const std::vector<SourcePosition>& chain) {
    j.open('[');
    for (const auto& p : chain) {
        j.open('{');
        position(j, p);
        j.close('}');
    }
    j.close(']');
}
void routes(Json& j, const std::vector<InspectedRoute>& rs) {
    j.open('[');
    for (const auto& r : rs) {
        j.open('{');
        j.key("token");
        j.optional(r.token);
        j.key("channel");
        j.number(r.channel);
        j.key("gain");
        j.number(r.gain);
        j.close('}');
    }
    j.close(']');
}
void delays(Json& j, const std::vector<InspectedDelay>& ds) {
    j.open('[');
    for (const auto& d : ds) {
        j.open('{');
        j.key("token");
        j.optional(d.token);
        j.key("milliseconds");
        j.optional(d.milliseconds);
        j.key("frames");
        j.number(d.frames);
        j.close('}');
    }
    j.close(']');
}
void configuration(Json& j, const InspectedConfiguration& c) {
    j.open('{');
    j.key("file");
    j.string(text(c.file));
    j.key("include_chain");
    positions(j, c.include_chain);
    j.key("kind");
    j.string(c.sound_file ? "sound-file" : "configuration");
    j.key("valid");
    j.boolean(c.valid);
    j.key("coefficients_verified");
    j.boolean(c.coefficients_verified);
    j.key("sample_rate");
    j.value(c.sample_rate);
    j.key("input_channels");
    j.number(c.input_channels);
    j.key("output_channels");
    j.number(c.output_channels);
    j.key("channel_mask");
    j.number(static_cast<std::size_t>(c.channel_mask));
    j.key("input_delays");
    delays(j, c.input_delays);
    j.key("output_delays");
    delays(j, c.output_delays);
    j.key("paths");
    j.open('[');
    for (const auto& p : c.paths) {
        j.open('{');
        j.key("ir");
        j.open('{');
        j.key("reference");
        j.string(p.reference);
        j.key("line");
        p.location.line ? j.number(p.location.line) : j.null();
        j.key("column");
        p.location.column ? j.number(p.location.column) : j.null();
        j.key("resolved");
        if (p.resolved.empty())
            j.null();
        else
            j.string(text(p.resolved));
        j.key("channel");
        j.number(p.ir_channel);
        j.key("sample_rate");
        j.optional(p.ir_sample_rate);
        j.key("file_channels");
        j.optional(p.ir_file_channels);
        j.key("frames");
        j.optional(p.ir_frames);
        j.key("raw_format");
        if (p.raw_encoding)
            j.string(raw_encoding_name(*p.raw_encoding));
        else
            j.null();
        j.close('}');
        j.key("partitions");
        j.optional(p.partitions);
        j.key("inputs");
        routes(j, p.inputs);
        j.key("outputs");
        routes(j, p.outputs);
        j.close('}');
    }
    j.close(']');
    const auto* a = c.analysis ? &*c.analysis : nullptr;
    j.key("timing");
    j.open('{');
    j.key("latency_frames");
    a ? j.number(a->latency_frames) : j.null();
    j.key("tail_frames");
    a ? j.number(a->tail_frames) : j.null();
    j.close('}');
    j.key("resources");
    j.open('{');
    j.key("fft_frames");
    a ? j.number(a->fft_frames) : j.null();
    j.key("accounted_buffer_bytes");
    a ? j.number(a->accounted_buffer_bytes) : j.null();
    j.key("excludes");
    j.open('[');
    for (const auto* e : {"FFT plan internals", "allocator overhead", "file decoding buffers"})
        j.string(e);
    j.close(']');
    j.close('}');
    j.close('}');
}
bool has(const InspectionReport& r, DiagnosticSeverity s) {
    for (const auto& d : r.diagnostics)
        if (d.severity == s)
            return true;
    return false;
}
bool passes(const InspectionReport& r, bool warnings_as_errors) {
    return r.valid && !(warnings_as_errors && has(r, DiagnosticSeverity::warning));
}
std::string location(const SourcePosition& p) {
    if (p.file.empty())
        return {};
    auto s = text(p.file);
    if (p.line)
        s += ':' + std::to_string(p.line) + (p.column ? ':' + std::to_string(p.column) : "");
    return s + ": ";
}
void print_diagnostics(const InspectionReport& r) {
    for (const auto& d : r.diagnostics) {
        std::cerr << "convolver: " << severity(d.severity) << ": " << location(d.location)
                  << d.message << " [" << d.code << ']';
        for (auto it = d.include_chain.rbegin(); it != d.include_chain.rend(); ++it)
            std::cerr << "; included from " << text(it->file) << ':' << it->line;
        std::cerr << '\n';
    }
}
std::string gain(float g) {
    return g == 1 ? "" : " x" + decimal(g);
}
void print_report(const InspectionReport& r, bool validate, bool warnings_as_errors) {
    auto& out = std::cout;
    const bool ok = passes(r, warnings_as_errors);
    if (validate) {
        out << (ok ? "valid: " : "invalid: ") << text(r.path);
        if (r.selected)
            out << " (selected " << text(*r.selected) << ")";
        if (r.options.syntax_only)
            out << "; syntax only, IR files not checked";
        out << '\n';
        return;
    }
    out << "Configuration: " << text(r.path) << '\n';
    out << "Selection: " << r.selection_reason;
    if (r.selected)
        out << " -> " << text(*r.selected);
    out << '\n';
    for (const auto& s : r.traversal)
        if (s.outcome != "selected" && s.outcome != "validated")
            out << "  " << s.outcome << ": " << text(s.file) << '\n';
    for (const auto& c : r.configurations) {
        out << '\n' << text(c.file) << (c.sound_file ? " (sound file)" : "") << '\n';
        out << "  " << c.sample_rate << " Hz, " << c.input_channels << " in, " << c.output_channels
            << " out, channel mask 0x" << std::hex << c.channel_mask << std::dec << '\n';
        auto delay_line = [&](const char* name, const std::vector<InspectedDelay>& ds) {
            bool any = false;
            for (const auto& d : ds)
                any = any || d.frames || (d.milliseconds && *d.milliseconds != 0);
            if (!any)
                return;
            out << "  " << name << " delays:";
            for (const auto& d : ds)
                out << ' ' << (d.token ? *d.token + " ms=" : "") << d.frames;
            out << " frames\n";
        };
        delay_line("input", c.input_delays);
        delay_line("output", c.output_delays);
        for (std::size_t i = 0; i < c.paths.size(); ++i) {
            const auto& p = c.paths[i];
            out << "  path " << i + 1 << ": " << p.reference << " channel " << p.ir_channel;
            if (p.ir_frames)
                out << ", " << *p.ir_frames << " frames at " << *p.ir_sample_rate << " Hz";
            if (p.raw_encoding)
                out << ", raw mono " << raw_encoding_name(*p.raw_encoding);
            if (p.partitions)
                out << ", " << *p.partitions << " partition" << (*p.partitions == 1 ? "" : "s");
            out << "\n    in:";
            for (const auto& route : p.inputs)
                out << ' ' << route.channel << gain(route.gain);
            out << "  out:";
            for (const auto& route : p.outputs)
                out << ' ' << route.channel << gain(route.gain);
            out << '\n';
        }
        if (c.analysis)
            out << "  latency " << c.analysis->latency_frames << " frames, tail "
                << c.analysis->tail_frames << " frames, FFT " << c.analysis->fft_frames
                << ", accounted buffers " << c.analysis->accounted_buffer_bytes
                << " bytes (excludes plan internals, allocator and decoding)\n";
        else
            out << "  coefficients and renderability not verified\n";
    }
    out << '\n' << (ok ? "Valid" : "Invalid");
    if (r.options.syntax_only)
        out << " (syntax only; IR files not checked)";
    out << '\n';
}

Json serialize_report(const InspectionReport& r, bool validate, bool warnings_as_errors) {
    Json j;
    j.open('{');
    j.key("schema_version");
    j.value(InspectionReport::schema_version);
    j.key("convolver");
    j.open('{');
    j.key("version");
    j.string(CONVOLVER_VERSION_STRING);
    j.key("fft_backend");
    j.string(fft_backend_name());
    j.key("fft_backend_version");
    j.string(fft_backend_version());
    j.key("sndfile_version");
    j.string(sndfile_version());
    j.close('}');
    j.key("mode");
    j.string(validate ? "validate" : "inspect");
    j.key("validation");
    j.string(r.options.syntax_only          ? "syntax-only"
             : r.options.all_configurations ? "all-configurations"
                                            : "selected");
    j.key("profile");
    j.string(profile_name(r.options.profile));
    j.key("warnings_as_errors");
    j.boolean(warnings_as_errors);
    j.key("valid");
    j.boolean(passes(r, warnings_as_errors));
    j.key("selection");
    j.open('{');
    j.key("path");
    j.string(text(r.path));
    j.key("expected_sample_rate");
    r.options.expected_rate ? j.value(r.options.expected_rate) : j.null();
    j.key("expected_input_channels");
    r.options.expected_inputs ? j.number(r.options.expected_inputs) : j.null();
    j.key("expected_output_channels");
    r.options.expected_outputs ? j.number(r.options.expected_outputs) : j.null();
    j.key("block_frames");
    j.number(r.options.block_frames);
    j.key("reason");
    j.string(r.selection_reason);
    j.key("selected");
    if (r.selected)
        j.string(text(*r.selected));
    else
        j.null();
    j.key("traversal");
    j.open('[');
    for (const auto& s : r.traversal) {
        j.open('{');
        j.key("file");
        j.string(text(s.file));
        j.key("include_chain");
        positions(j, s.include_chain);
        j.key("outcome");
        j.string(s.outcome);
        j.key("sample_rate");
        j.optional(s.sample_rate);
        j.key("input_channels");
        j.optional(s.input_channels);
        j.key("output_channels");
        j.optional(s.output_channels);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    j.key("configurations");
    j.open('[');
    for (const auto& c : r.configurations)
        configuration(j, c);
    j.close(']');
    j.key("diagnostics");
    j.open('[');
    for (const auto& d : r.diagnostics) {
        j.open('{');
        j.key("code");
        j.string(d.code);
        j.key("severity");
        j.string(severity(d.severity));
        j.key("message");
        j.string(d.message);
        position(j, d.location);
        j.key("include_chain");
        positions(j, d.include_chain);
        j.close('}');
    }
    j.close(']');
    j.close('}');
    return j;
}
} // namespace

std::string report_json(InspectionReport& r, bool validate, bool warnings_as_errors) {
    auto json = serialize_report(r, validate, warnings_as_errors);
    if (json.replaced_invalid_utf8) {
        // Include diagnostic strings in the encoding check. Record the warning
        // before deciding validity, printing stderr or choosing the exit status.
        r.diagnostics.push_back({"invalid-utf8",
                                 DiagnosticSeverity::warning,
                                 "reported text is not valid UTF-8; invalid bytes are shown as "
                                 "U+FFFD. Use UTF-8 for file names and configuration text",
                                 {},
                                 {}});
        json = serialize_report(r, validate, warnings_as_errors);
    }
    return std::move(json.out);
}

bool is_inspection(int argc, char** argv) {
    // Skip the values of render options, so a file named "--validate" stays a file.
    constexpr std::array<std::pair<std::string_view, int>, 19> render_values{{
        {"--filter", 1},
        {"--input", 1},
        {"--output", 1},
        {"--fft-plan", 1},
        {"--fft-wisdom", 1},
        {"--tune-fft", 1},
        {"--block-size", 1},
        {"--gain-db", 1},
        {"--output-format", 1},
        {"--speaker-map", 1},
        {"--raw-input", 2},
        {"--raw-format", 1},
        {"--raw-filter-format", 1},
        {"--dither", 1},
        {"--dither-seed", 1},
        {"--noise-shaping", 1},
        {"--valid-bits", 1},
        {"--output-channels", 1},
        {"--fft-time-limit", 1},
    }};
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--inspect" || a == "--validate")
            return true;
        for (const auto& [option, values] : render_values)
            if (a == option)
                i += values;
    }
    return false;
}

int run_inspection(int argc, char** argv) {
    InspectionOptions options;
    std::filesystem::path path;
    bool validate = false, json = false, warnings_as_errors = false, mode = false;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--help") {
            std::cout << usage;
            return 0;
        }
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw Usage("missing value for " + option);
                return argv[i];
            };
            if (option == "--inspect" || option == "--validate") {
                if (mode)
                    throw Usage("--inspect and --validate are mutually exclusive");
                mode = true;
                validate = option == "--validate";
                path = utf8_path(value());
            } else if (option == "--sample-rate")
                options.expected_rate = static_cast<int>(count(value(), 768000, "--sample-rate"));
            else if (option == "--input-channels")
                options.expected_inputs = count(value(), 256, "--input-channels");
            else if (option == "--output-channels")
                options.expected_outputs = count(value(), 256, "--output-channels");
            else if (option == "--block-size")
                options.block_frames = count(value(), 1u << 20, "--block-size");
            else if (option == "--all-configurations")
                options.all_configurations = true;
            else if (option == "--syntax-only")
                options.syntax_only = true;
            else if (option == "--warnings-as-errors")
                warnings_as_errors = true;
            else if (option == "--json")
                json = true;
            else if (option == "--raw-filter-format") {
                const auto name = value();
                const auto parsed = parse_raw_encoding(name);
                if (!parsed)
                    throw Usage("--raw-filter-format must be s16le, s24le, s32le, f32le or "
                                "f64le, or the same ending in be");
                options.raw_filter_encoding = *parsed;
            } else if (option == "--profile") {
                const auto name = value();
                if (name == "convolver")
                    options.profile = InspectionProfile::convolver;
                else if (name == "portable-paths")
                    options.profile = InspectionProfile::portable_paths;
                else
                    throw Usage("--profile must be convolver or portable-paths");
            } else
                throw Usage(option + " is not available with --inspect or --validate");
        }
        if (options.all_configurations &&
            (options.expected_rate || options.expected_inputs || options.expected_outputs))
            throw Usage("--all-configurations cannot be combined with --sample-rate, "
                        "--input-channels or --output-channels");
        if (options.all_configurations && options.syntax_only)
            throw Usage("--all-configurations and --syntax-only are mutually exclusive");
        if (path.empty() || path == "-")
            throw Usage("--inspect and --validate need a configuration file path");
    } catch (const Usage& e) {
        std::cerr << "convolver: " << e.what() << '\n' << usage;
        return 2;
    }
    try {
        auto report = inspect_configuration(path, options);
        // Use the same encoding check for JSON and human-readable reports.
        const auto serialized = report_json(report, validate, warnings_as_errors);
        print_diagnostics(report);
        if (json)
            std::cout << serialized << '\n';
        else
            print_report(report, validate, warnings_as_errors);
        std::cout.flush();
        if (!std::cout)
            throw std::runtime_error("cannot write report");
        return passes(report, warnings_as_errors) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "convolver: " << e.what() << '\n';
        return 1;
    }
}
} // namespace convolver::cli
