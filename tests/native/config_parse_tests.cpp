// SPDX-License-Identifier: GPL-2.0-or-later
// Parser-only tests: physical locations, original tokens and effective values.
#include "config_parse.hpp"
#include <cmath>
#include <iostream>
#include <string>

using namespace convolver::detail;
namespace {
void require(bool b, const char* message) {
    if (!b)
        throw std::runtime_error(message);
}
// Returns the error, or fails if parsing succeeds.
ConfigurationError rejected(std::string_view text) {
    try {
        parse_configuration_text(text, "t.cfg");
    } catch (const ConfigurationError& e) {
        return e;
    }
    throw std::runtime_error("expected parse failure: " + std::string(text));
}
void located(std::string_view text, std::size_t line, std::size_t column, const char* message) {
    const auto e = rejected(text);
    if (e.location().line != line || e.location().column != column)
        throw std::runtime_error(std::string(message) + ": got " + e.what());
}
} // namespace

int main() {
    try {
        const std::string text = "\xef\xbb\xbf// generated\r\n\r\n48000 2 3 3f // header\r\n"
                                 "0.04 1\r\n0 .02 0\r\n\r\n// path\r\n  ir ü.wav  \r\n1\r\n"
                                 "-0 1.25 1\r\n0 -2.5\r\n";
        const auto doc = parse_configuration_text(text, "room.cfg");
        require(doc.kind == ParsedDocument::Kind::configuration, "configuration kind");
        const auto& c = doc.configuration;
        require(c.header_line == 3 && c.header_tokens.size() == 4 && c.header_tokens[3] == "3f",
                "header tokens and line after BOM, comment and blank line");
        require(c.sample_rate == 48000 && c.input_channels == 2 && c.output_channels == 3 &&
                    c.channel_mask == 0x3f,
                "header values");
        require(c.input_delay_line == 4 && c.output_delay_line == 5, "delay lines");
        require(c.input_delays[0].token == "0.04" && c.input_delays[0].frames == 1 &&
                    c.input_delays[1].frames == 48 && c.input_delays[1].column == 6,
                "declared delay tokens and truncated frames");
        require(c.output_delays[1].milliseconds == .02f && c.output_delays[1].frames == 0,
                "fractional delay truncation");
        require(c.paths.size() == 1, "one path");
        const auto& p = c.paths[0];
        require(p.ir.spelling == "ir ü.wav" && p.ir.location.line == 8 && p.ir.location.column == 3,
                "trimmed reference spelling and its column");
        require(p.ir_channel == 1 && p.channel_location.line == 9, "IR channel");
        require(p.input_line == 10 && p.output_line == 11, "route lines");
        require(p.inputs.size() == 3 && p.inputs[0].token == "-0" && p.inputs[0].channel == 0 &&
                    p.inputs[0].gain == -1 && std::signbit(p.inputs[0].gain),
                "signed zero keeps its token and becomes gain -1");
        require(p.inputs[1].channel == 1 && p.inputs[1].gain == .25f && p.inputs[1].column == 4,
                "fractional weight");
        require(p.inputs[2].channel == 1 && p.inputs[2].gain == 1, "repeated channel");
        require(p.outputs[1].channel == 2 && p.outputs[1].gain == -.5f, "negative fraction");

        const auto list =
            parse_configuration_text("// lists\n\na b.cfg\n  sub\\c.txt // x\n", "filters.txt");
        require(list.kind == ParsedDocument::Kind::list && list.entries.size() == 2 &&
                    list.entries[0].spelling == "a b.cfg" && list.entries[0].location.line == 3 &&
                    list.entries[1].spelling == "sub\\c.txt" &&
                    list.entries[1].location.column == 3,
                "list entries keep spaces, separators and locations");
        require(parse_configuration_text("48000.cfg\n", "l").kind == ParsedDocument::Kind::list,
                "non-numeric first token is a list");

        located("// c\n\n48000 1 1 0\n0\n0\nir.wav\n0\n 1\n0\n", 8, 2,
                "route channel after comment and blank lines");
        located("\xef\xbb\xbf"
                "48000 1 1 0\n0\n0\nir.wav\n0\n0\nnan\n",
                7, 1, "malformed output route after BOM");
        located("48000 1 1 0\r\n0 0\r\n0\r\nir.wav\r\n0\r\n0\r\n0\r\n", 2, 1,
                "delay count with CRLF");
        located("48000 1 1 0\n-1\n0\nir.wav\n0\n0\n0\n", 2, 1, "negative delay");
        located("48000 999 1 0\n0\n0\nir.wav\n0\n0\n0\n", 1, 7, "channel count limit");
        located("48000 1 1 0\n0\n0\nir.wav\n0\n0\n0\nb.wav\n0\n", 8, 1,
                "incomplete second path points at its first line");
        located("48000 1 1 0\n0\n0\nir.wav\n256\n0\n0\n", 5, 1, "IR channel limit");
        located("48000 1 1 0 x\n0\n0\nir.wav\n0\n0\n0\n", 1, 1, "header field count");
        located("44100 2 2 0\nD:\\media\\center.pcm\n0\n0.0\n0.0\n", 2, 1,
                "pre-3.0 configuration without delay lines");
        require(std::string(rejected("44100 2 2 0\nir.wav\n0\n0\n0\n").what())
                        .find("pre-3.0 configuration without delay lines: insert a line of 2 "
                              "zeros") != std::string::npos,
                "pre-3.0 diagnostic says what to add");
        // A malformed delay is reported as one, not as a pre-3.0 file.
        for (const auto* delay : {"0,5", "nan", "x"}) {
            const std::string what =
                rejected("48000 1 1 0\n" + std::string(delay) + "\n0\nir.wav\n0\n0\n0\n").what();
            require(what.find("pre-3.0") == std::string::npos &&
                        what.find("t.cfg:2:") != std::string::npos,
                    "malformed delays are not called pre-3.0");
        }
        require(rejected("48000 1 1 0\n0\n0\nir.wav\n0\n2\n0\n").code() == "syntax", "code");
        require(std::string(rejected("48000 1 1 0\n0\n0\nir.wav\n0\n2\n0\n").what()) ==
                    "invalid configuration: t.cfg:6:1: route channel out of range: 2 (1 "
                    "channel declared in the header, numbered from 0, so the channel must be 0; "
                    "a route's integer part is the channel and its fractional part the gain)",
                "message format");
        for (const auto bad : {std::string_view(""), std::string_view("// only\n\n"),
                               std::string_view("48\0", 3), std::string_view("a\x01")})
            rejected(bad);
        rejected(std::string(max_text_bytes + 1, 'x'));
        auto chained = rejected("48000 1 1 0\n0\n0\nir.wav\n0\n2\n0\n");
        chained.included_from({"inner.txt", 2, 1});
        chained.included_from({"outer.txt", 4, 1});
        require(chained.include_chain().front().file == "outer.txt" &&
                    std::string(chained.what())
                        .ends_with("; included from inner.txt:2; included from outer.txt:4"),
                "include chain is reported innermost first");
        std::cout << "configuration parser tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
