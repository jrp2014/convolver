// SPDX-License-Identifier: GPL-2.0-or-later
// Legacy configuration text grammar with physical source locations. This file has
// no filesystem or audio decoder access; config.cpp resolves references.
#include "config_parse.hpp"
#include "advice.hpp"
#include "convolver/engine.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>

namespace convolver::detail {
std::string utf8_string(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
ConfigurationError::ConfigurationError(std::string code, SourceLocation location,
                                       std::string reason)
    : std::runtime_error(reason), code_(std::move(code)), location_(std::move(location)),
      reason_(std::move(reason)) {
    format();
}
const char* ConfigurationError::what() const noexcept {
    return message_.c_str();
}
void ConfigurationError::included_from(SourceLocation list) {
    chain_.insert(chain_.begin(), std::move(list));
    format();
}
// DIAG-2: "FILE:LINE:COLUMN: problem; included from LIST:LINE", then the
// reason's cause, fix and note lines (advice.hpp).
void ConfigurationError::format() {
    message_.clear();
    if (!location_.file.empty()) {
        message_ += utf8_string(location_.file);
        if (location_.line) {
            message_ += ':' + std::to_string(location_.line);
            if (location_.column)
                message_ += ':' + std::to_string(location_.column);
        }
        message_ += ": ";
    }
    const auto newline = reason_.find('\n');
    message_ += reason_.substr(0, newline);
    for (auto it = chain_.rbegin(); it != chain_.rend(); ++it)
        message_ += "; included from " + utf8_string(it->file) + ':' + std::to_string(it->line);
    if (newline != std::string::npos)
        message_ += reason_.substr(newline);
}

namespace {
struct Token {
    std::string text;
    std::size_t column;
};
// A nonblank line after comment removal.
struct Line {
    std::string text;
    std::size_t number, column; // Column of text's first byte.
    std::vector<Token> tokens;
};
bool space(char c) {
    // Classic-locale isspace; other control bytes are rejected before tokenizing.
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
std::vector<Token> split(std::string_view text, std::size_t first_column) {
    std::vector<Token> result;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && space(text[i]))
            ++i;
        const auto start = i;
        while (i < text.size() && !space(text[i]))
            ++i;
        if (i > start)
            result.push_back({std::string(text.substr(start, i - start)), first_column + start});
    }
    return result;
}
struct Parser {
    const std::filesystem::path& source;
    SourceLocation at(std::size_t line, std::size_t column = 0) const {
        return {source, line, column};
    }
    [[noreturn]] void fail(std::size_t line, std::size_t column, const std::string& why,
                           const char* code = "syntax") const {
        throw ConfigurationError(code, at(line, column), why);
    }
    [[noreturn]] void fail(std::size_t line, std::size_t column, const Advice& why) const {
        fail(line, column, advice_text(why));
    }
    // `what` names the number ("the sample rate"), `range` its allowed values
    // ("a whole number from 1 to 768000").
    std::size_t integer(const Line& line, const Token& t, std::size_t limit,
                        const std::string& what, const std::string& range, int base = 10) const {
        std::string_view v(t.text);
        if (base == 16 && v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
            v.remove_prefix(2);
        std::size_t result = 0;
        const auto [end, error] = std::from_chars(v.data(), v.data() + v.size(), result, base);
        if (error != std::errc{} || end != v.data() + v.size() || result > limit)
            fail(line.number, t.column,
                 Advice{what + " '" + t.text + "' is not valid",
                        "It must be " + range + ".",
                        "Correct it on line " + std::to_string(line.number) + ".",
                        {}});
        return result;
    }
    float number(const Line& line, const Token& t) const {
        // Match the historical parser's single-precision route and delay values.
        std::istringstream stream(t.text);
        stream.imbue(std::locale::classic());
        float result;
        if (!(stream >> result) || !stream.eof() || !std::isfinite(result))
            fail(line.number, t.column,
                 Advice{"'" + t.text + "' is not a number",
                        "Line " + std::to_string(line.number) +
                            " holds numbers only, written with digits and a dot, such as 0.5 or 2.",
                        "Correct it, or remove the stray text.",
                        {}});
        return result;
    }
    std::vector<ParsedDelay> delays(const Line& line, std::size_t count, int rate) const {
        if (line.tokens.size() != count)
            fail(line.number, line.column,
                 Advice{"the delay line has " + std::to_string(line.tokens.size()) +
                            (line.tokens.size() == 1 ? " value" : " values") + " for " +
                            std::to_string(count) + (count == 1 ? " channel" : " channels"),
                        "Each delay line holds one delay in milliseconds per channel, in the "
                        "order of the channel counts on the header line.",
                        "Write " + std::to_string(count) + (count == 1 ? " value" : " values") +
                            " on line " + std::to_string(line.number) + " (0 for no delay).",
                        {}});
        std::vector<ParsedDelay> result;
        std::size_t total = 0;
        for (const auto& field : line.tokens) {
            const float ms = number(line, field);
            const double frames = static_cast<float>(rate) * ms / 1000.0;
            if (ms < 0 || frames > static_cast<double>(max_delay_frames))
                fail(line.number, field.column,
                     Advice{"the delay " + field.text + " ms is out of range",
                            "A delay must be 0 or more milliseconds and at most " +
                                std::to_string(max_delay_frames) + " frames at the sample rate.",
                            "Use a smaller delay on line " + std::to_string(line.number) + ".",
                            {}});
            const auto truncated = static_cast<std::size_t>(frames);
            if (truncated > max_decoded_samples - total)
                fail(line.number, field.column,
                     Advice{"total delay storage exceeds the limit",
                            "Convolver keeps every delayed sample in memory, and these delays "
                            "would need more than its limit of " +
                                std::to_string(max_decoded_samples) + " samples.",
                            "Use shorter delays.",
                            {}});
            total += truncated;
            result.push_back({field.text, field.column, ms, truncated});
        }
        return result;
    }
    std::vector<ParsedRoute> routes(const Line& line, std::size_t channels) const {
        if (line.tokens.empty() || line.tokens.size() > max_routes)
            fail(
                line.number, line.column,
                Advice{"line " + std::to_string(line.number) + " names " +
                           (line.tokens.empty() ? std::string("no channels")
                                                : std::to_string(line.tokens.size()) + " channels"),
                       "Each route line names 1 to " + std::to_string(max_routes) +
                           " channels, numbered from 0.",
                       "Write the channel numbers this filter path uses on that line.",
                       {}});
        std::vector<ParsedRoute> result;
        for (const auto& field : line.tokens) {
            float integral = 0;
            float gain = std::modf(number(line, field), &integral);
            if (std::abs(integral) >= static_cast<float>(channels))
                fail(line.number, field.column,
                     Advice{"route channel out of range: " + field.text,
                            "The header declares " + std::to_string(channels) +
                                (channels == 1 ? " channel" : " channels") +
                                ", numbered from 0. A route's whole part is the channel and its "
                                "fraction the gain (1.5 is channel 1 at gain 0.5).",
                            std::string("Use ") +
                                (channels == 1
                                     ? std::string("channel 0")
                                     : "a channel from 0 to " + std::to_string(channels - 1)) +
                                ", or raise the channel count on the header line.",
                            {}});
            if (gain == 0)
                gain = std::copysign(1.0f, integral);
            result.push_back(
                {field.text, field.column, static_cast<std::size_t>(std::abs(integral)), gain});
        }
        return result;
    }
};
std::vector<Line> logical_lines(std::string_view text, const Parser& p) {
    if (text.size() > max_text_bytes)
        p.fail(0, 0,
               Advice{"the file is too large for a configuration",
                      "A configuration is a short text file of at most 1 MiB.",
                      "Check that this is the configuration and not another file.",
                      {}});
    for (unsigned char c : text)
        if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t'))
            p.fail(0, 0,
                   Advice{"neither a configuration nor a sound file this Convolver can read",
                          "The file holds bytes that a text configuration cannot, and no sound "
                          "file format recognises it: it is binary, or a damaged sound file.",
                          "If it is a configuration, save it as plain text (UTF-8, not UTF-16 "
                          "\"Unicode\"). If it is a sound file, export it again from the "
                          "program that made it.",
                          {}});
    if (text.starts_with("\xef\xbb\xbf"))
        text.remove_prefix(3);
    std::vector<Line> result;
    std::size_t number = 0;
    while (!text.empty() || number == 0) {
        ++number;
        const auto end = text.find('\n');
        auto raw = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto comment = raw.find("//");
        if (comment != std::string_view::npos)
            raw = raw.substr(0, comment);
        const auto first = raw.find_first_not_of(" \t\r\n");
        if (first != std::string_view::npos) {
            const auto last = raw.find_last_not_of(" \t\r\n");
            const auto body = raw.substr(first, last - first + 1);
            result.push_back({std::string(body), number, first + 1, split(body, first + 1)});
        }
        if (end == std::string_view::npos)
            break;
    }
    if (result.empty())
        p.fail(0, 0,
               Advice{"the configuration is empty",
                      "It holds no lines other than comments.",
                      "Write the header line (sample rate, input channels, output channels, "
                      "channel mask), the two delay lines and the filter paths; see "
                      "docs/use/configuration.md.",
                      {}});
    return result;
}
} // namespace

ParsedDocument parse_configuration_text(std::string_view text,
                                        const std::filesystem::path& source) {
    const Parser p{source};
    const auto content = logical_lines(text, p);
    ParsedDocument doc;
    doc.source = source;
    const auto& header = content.front().tokens;
    const auto& first = header.front().text;
    if (first.find_first_not_of("+-0123456789") != std::string::npos) {
        doc.kind = ParsedDocument::Kind::list;
        for (const auto& line : content)
            doc.entries.push_back({line.text, p.at(line.number, line.column)});
        return doc;
    }
    auto& cfg = doc.configuration;
    const auto& h = content.front();
    if (header.size() != 4)
        p.fail(h.number, h.column,
               Advice{"the header line has " + std::to_string(header.size()) +
                          (header.size() == 1 ? " value" : " values") + ", not 4",
                      "The first line holds the sample rate, input channels, output channels "
                      "and the channel mask in hexadecimal.",
                      "Write all four, such as 48000 2 2 3 (or 0 as the mask for no speaker "
                      "labels).",
                      {}});
    if (content.size() >= 2) {
        // Before version 3.0 the header was followed directly by filter paths.
        const auto& second = content[1];
        std::istringstream probe(second.tokens.front().text);
        probe.imbue(std::locale::classic());
        float value = 0;
        // Only a token that reads like a file name gets the pre-3.0 hint; other
        // malformed delays fall through to the delay parser's own error.
        const auto& token = second.tokens.front().text;
        const auto dot = token.rfind('.');
        const bool file_like =
            token.find_first_of("/\\") != std::string::npos ||
            (dot != std::string::npos && dot + 1 < token.size() &&
             std::all_of(token.begin() + static_cast<std::ptrdiff_t>(dot) + 1, token.end(),
                         [](unsigned char c) { return std::isalnum(c) != 0; }) &&
             std::any_of(token.begin() + static_cast<std::ptrdiff_t>(dot) + 1, token.end(),
                         [](unsigned char c) { return std::isalpha(c) != 0; }));
        if (file_like && (!(probe >> value) || !probe.eof()))
            p.fail(second.number, second.column,
                   Advice{"expected a line of input delays after the header, found '" +
                              second.text + "'",
                          "This looks like a configuration from before Convolver 3.0, which had "
                          "no delay lines.",
                          "Insert a line of " +
                              std::string(header.size() > 1 ? header[1].text : "input-count") +
                              " zeros (input delays) and a line of " +
                              std::string(header.size() > 2 ? header[2].text : "output-count") +
                              " zeros (output delays) after the header.",
                          {}});
    }
    if (content.size() < 3)
        p.fail(content.back().number, 0,
               Advice{"expected input and output delay lines after the header",
                      "The header is followed by a line of input delays and a line of output "
                      "delays, in milliseconds, before the filter paths.",
                      "Add the two delay lines (0 for each channel with no delay).",
                      {}});
    if (content.size() < 7 || (content.size() - 3) % 4) {
        const auto complete = (content.size() - 3) / 4;
        const auto& start =
            content[3 + 4 * complete < content.size() ? 3 + 4 * complete : content.size() - 1];
        p.fail(start.number, start.column,
               Advice{"filter path " + std::to_string(complete + 1) + " is incomplete (" +
                          std::to_string(content.size() - 3 - 4 * complete) + " of 4 lines)",
                      "After the header and two delay lines, each filter path has four lines: "
                      "the IR file, its channel, the input channels and the output channels.",
                      "Add the missing lines, or remove the incomplete path.",
                      {}});
    }
    for (const auto& t : header)
        cfg.header_tokens.push_back(t.text);
    cfg.header_line = h.number;
    const auto counts = "a whole number up to " + std::to_string(max_channel_count);
    cfg.sample_rate = static_cast<int>(
        p.integer(h, header[0], max_sample_rate, "the sample rate",
                  "a whole number of Hz up to " + std::to_string(max_sample_rate)));
    cfg.input_channels =
        p.integer(h, header[1], max_channel_count, "the input channel count", counts);
    cfg.output_channels =
        p.integer(h, header[2], max_channel_count, "the output channel count", counts);
    cfg.channel_mask = static_cast<std::uint32_t>(
        p.integer(h, header[3], 0xffffffffu, "the channel mask",
                  "a hexadecimal number of up to 8 digits, such as 3 or 0x3F (0 for none)", 16));
    if (!cfg.sample_rate || !cfg.input_channels || !cfg.output_channels)
        p.fail(h.number, h.column,
               Advice{"the header line has a zero sample rate or channel count",
                      "The sample rate and both channel counts must be at least 1.",
                      "Correct the first three numbers on line " + std::to_string(h.number) + ".",
                      {}});
    cfg.input_delay_line = content[1].number;
    cfg.output_delay_line = content[2].number;
    cfg.input_delays = p.delays(content[1], cfg.input_channels, cfg.sample_rate);
    cfg.output_delays = p.delays(content[2], cfg.output_channels, cfg.sample_rate);
    if ((content.size() - 3) / 4 > max_filter_paths)
        p.fail(content[3].number, 0,
               Advice{"the configuration has too many filter paths",
                      "Convolver accepts at most " + std::to_string(max_filter_paths) +
                          " filter paths in one configuration.",
                      "Combine paths that share an IR channel by listing several channels on "
                      "their route lines.",
                      {}});
    for (std::size_t i = 3; i < content.size(); i += 4) {
        ParsedPath path;
        path.ir = {content[i].text, p.at(content[i].number, content[i].column)};
        const auto& channel = content[i + 1];
        if (channel.tokens.size() != 1)
            p.fail(channel.number, channel.column,
                   Advice{"the IR channel line has " + std::to_string(channel.tokens.size()) +
                              " values, not 1",
                          "The line after each IR file name gives the one channel of that file "
                          "to use, numbered from 0.",
                          "Write a single channel number on line " +
                              std::to_string(channel.number) + ".",
                          {}});
        path.channel_token = channel.tokens.front().text;
        path.channel_location = p.at(channel.number, channel.tokens.front().column);
        path.ir_channel = p.integer(channel, channel.tokens.front(), 255, "the IR channel",
                                    "a whole number from 0 to 255");
        path.input_line = content[i + 2].number;
        path.output_line = content[i + 3].number;
        path.inputs = p.routes(content[i + 2], cfg.input_channels);
        path.outputs = p.routes(content[i + 3], cfg.output_channels);
        cfg.paths.push_back(std::move(path));
    }
    return doc;
}
} // namespace convolver::detail
