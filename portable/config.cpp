// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/io.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace convolver {
namespace {
constexpr std::size_t max_samples = 64 * 1024 * 1024;
constexpr std::size_t max_delay = 16 * 1024 * 1024;
constexpr std::size_t max_text = 1024 * 1024;
struct Context {
    std::set<std::filesystem::path> active;
    std::vector<std::filesystem::path> sources;
    std::size_t visits = 0, samples = 0;
};
std::runtime_error invalid(const std::string& why) { return std::runtime_error("invalid configuration: " + why); }
std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
std::vector<std::string> tokens(const std::string& line) {
    std::istringstream stream(line); stream.imbue(std::locale::classic());
    std::vector<std::string> result; std::string item;
    while (stream >> item) result.push_back(item);
    return result;
}
std::size_t integer(const std::string& s, std::size_t limit, int base = 10) {
    std::string_view v(s);
    if (base == 16 && v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v.remove_prefix(2);
    std::size_t result = 0;
    const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), result, base);
    if (error != std::errc{} || end != v.data() + v.size() || result > limit) throw invalid("integer out of range: " + s);
    return result;
}
float number(const std::string& s) {
    // Match the historical parser's single-precision route and delay values.
    std::istringstream stream(s); stream.imbue(std::locale::classic()); float result;
    if (!(stream >> result) || !stream.eof() || !std::isfinite(result)) throw invalid("nonfinite or malformed number: " + s);
    return result;
}
bool matches(const Configuration& cfg, int rate, std::size_t channels) {
    return (!rate || rate == cfg.sample_rate) && (!channels || channels == cfg.input_channels);
}
std::vector<std::string> lines(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw invalid("cannot read " + path.string());
    std::string text(max_text + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (file.bad()) throw invalid("read failed: " + path.string());
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (text.size() > max_text) throw invalid("text exceeds 1 MiB");
    if (text.find('\0') != std::string::npos) throw invalid("binary or damaged audio file");
    for (unsigned char c : text) if (c < 32 && c != '\n' && c != '\r' && c != '\t') throw invalid("binary or damaged audio file");
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    std::vector<std::string> result; std::istringstream stream(text); std::string line;
    while (std::getline(stream, line)) {
        const auto comment = line.find("//");
        if (comment != std::string::npos) line.erase(comment);
        line = trim(std::move(line));
        if (!line.empty()) result.push_back(std::move(line));
    }
    if (result.empty()) throw invalid("empty file");
    return result;
}
void read_impulses(AudioReader& reader, std::vector<FilterPath>& paths, std::optional<std::size_t> selected, Context& context) {
    const auto channels = reader.info().channels;
    if (selected && *selected >= channels) throw invalid("filter channel out of range");
    std::vector<float> buffer(4096 * channels);
    while (const auto frames = reader.read(buffer)) {
        const auto samples = frames * channels;
        if (samples > max_samples - context.samples) throw invalid("decoded impulse data exceeds 64 Mi samples");
        context.samples += samples;
        if (selected) {
            for (std::size_t f = 0; f < frames; ++f) paths.back().impulse.push_back(buffer[f * channels + *selected]);
        } else {
            for (std::size_t c = 0; c < channels; ++c)
                for (std::size_t f = 0; f < frames; ++f) paths[c].impulse.push_back(buffer[f * channels + c]);
        }
    }
    reader.close();
    if (paths.back().impulse.empty()) throw invalid("empty impulse response");
}
std::vector<std::size_t> delays(const std::string& line, std::size_t count, int rate) {
    const auto fields = tokens(line);
    if (fields.size() != count) throw invalid("delay count differs from channel count");
    std::vector<std::size_t> result; std::size_t total = 0;
    for (const auto& field : fields) {
        const float ms = number(field);
        const double frames = static_cast<float>(rate) * ms / 1000.0;
        if (ms < 0 || frames > static_cast<double>(max_delay)) throw invalid("delay out of range");
        const auto truncated = static_cast<std::size_t>(frames);
        if (truncated > max_samples - total) throw invalid("total delay storage exceeds limit");
        total += truncated; result.push_back(truncated);
    }
    return result;
}
std::vector<Route> routes(const std::string& line, std::size_t channels) {
    const auto fields = tokens(line);
    if (fields.empty() || fields.size() > 4096) throw invalid("route count out of range");
    std::vector<Route> result;
    for (const auto& field : fields) {
        float integral = 0; float gain = std::modf(number(field), &integral);
        if (std::abs(integral) >= static_cast<float>(channels)) throw invalid("route channel out of range");
        if (gain == 0) gain = std::copysign(1.0f, integral);
        result.push_back({static_cast<std::size_t>(std::abs(integral)), gain});
    }
    return result;
}
std::filesystem::path relative(const std::filesystem::path& owner, const std::string& name) {
    const std::u8string utf8(name.begin(), name.end());
    const std::filesystem::path p(utf8);
    return p.is_absolute() ? p : owner.parent_path() / p;
}
std::optional<Configuration> load(const std::filesystem::path& path, int rate, std::size_t inputs, Context& context) {
    if (++context.visits > 4096 || context.active.size() >= 16) throw invalid("configuration list nesting/entry limit exceeded");
    const auto canonical = std::filesystem::weakly_canonical(path);
    context.sources.push_back(canonical);
    if (!context.active.insert(canonical).second) throw invalid("configuration list cycle: " + path.string());
    struct Pop { Context& context; std::filesystem::path path; ~Pop() { context.active.erase(path); } } pop{context, canonical};
    std::unique_ptr<AudioReader> audio;
    try { audio = std::make_unique<AudioReader>(path); }
    catch (const std::runtime_error&) { /* Bounded text validation below distinguishes configs from damaged audio. */ }
    if (audio) {
        const auto info = audio->info(); Configuration cfg;
        cfg.sample_rate = info.sample_rate; cfg.input_channels = cfg.output_channels = info.channels;
        if (!matches(cfg, rate, inputs)) return {};
        cfg.input_delays.resize(info.channels); cfg.output_delays.resize(info.channels); cfg.paths.resize(info.channels);
        for (std::size_t c = 0; c < info.channels; ++c) { cfg.paths[c].inputs.push_back({c,1}); cfg.paths[c].outputs.push_back({c,1}); }
        read_impulses(*audio, cfg.paths, {}, context); return cfg;
    }
    const auto content = lines(path);
    const auto header = tokens(content.front());
    const auto& first = header.front();
    const bool numeric = first.find_first_not_of("+-0123456789") == std::string::npos;
    if (!numeric) {
        for (const auto& entry : content) {
            if (auto cfg = load(relative(path, entry), rate, inputs, context)) return cfg;
        }
        return {};
    }
    if (header.size() != 4 || content.size() < 7 || (content.size() - 3) % 4) throw invalid("expected header, two delay lines, and four lines per filter path");
    Configuration cfg;
    cfg.sample_rate = static_cast<int>(integer(header[0], 768000));
    cfg.input_channels = integer(header[1], 256); cfg.output_channels = integer(header[2], 256);
    cfg.channel_mask = static_cast<std::uint32_t>(integer(header[3], 0xffffffffu, 16));
    if (!cfg.sample_rate || !cfg.input_channels || !cfg.output_channels) throw invalid("zero rate or channel count");
    cfg.input_delays = delays(content[1], cfg.input_channels, cfg.sample_rate);
    cfg.output_delays = delays(content[2], cfg.output_channels, cfg.sample_rate);
    if ((content.size() - 3) / 4 > 4096) throw invalid("too many filter paths");
    for (std::size_t i = 3; i < content.size(); i += 4) {
        integer(content[i+1], 255);
        context.sources.push_back(std::filesystem::weakly_canonical(relative(path, content[i])));
        FilterPath filter;
        filter.inputs = routes(content[i+2], cfg.input_channels); filter.outputs = routes(content[i+3], cfg.output_channels);
        cfg.paths.push_back(std::move(filter));
    }
    // Lists select by stream properties. Validate unrelated entries' text, but
    // do not require their IR assets to be installed or decode them into memory.
    if (!matches(cfg, rate, inputs)) return {};
    for (std::size_t i = 3; i < content.size(); i += 4) {
        const auto channel = integer(content[i+1], 255);
        const auto ir_path = relative(path, content[i]);
        AudioReader ir(ir_path);
        if (ir.info().sample_rate != cfg.sample_rate) throw invalid("filter sample rate differs from configuration");
        // read_impulses targets the last path, so decode into a single path and
        // transfer only its coefficients after all routing has been validated.
        std::vector<FilterPath> decoded(1);
        read_impulses(ir, decoded, channel, context);
        cfg.paths[(i - 3) / 4].impulse = std::move(decoded.front().impulse);
    }
    return cfg;
}
}
Configuration load_configuration(const std::filesystem::path& path, int expected_rate, std::size_t expected_inputs,
                                 std::vector<std::filesystem::path>* source_paths) {
    if (expected_rate < 0 || expected_rate > 768000 || expected_inputs > 256) throw std::invalid_argument("expected audio properties out of range");
    if (path == "-") throw std::invalid_argument("configuration must be a file");
    Context context;
    if (auto cfg = load(path, expected_rate, expected_inputs, context)) {
        if (source_paths) *source_paths = std::move(context.sources);
        return std::move(*cfg);
    }
    throw std::runtime_error("no configuration matches the input sample rate and channel count");
}
}
