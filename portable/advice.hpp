// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/advice.hpp"
#include <string>
#include <string_view>

// DIAG-2: every message a user can meet has the same parts (Advice, in
// convolver/advice.hpp; docs/archive/plans/2026-11-05-actionable-diagnostics.md):
// the problem in one sentence, then its cause, the fix and, where an
// environment, host or design limit is the reason, a note. DIAG-4: failure
// records (HostFailure::message, HostFilter::notice()) keep the parts, and
// only exceptions carry them as text (advice_text()), with each part after
// the problem on its own line starting "Cause: ", "Fix: " or "Note: ". A part
// can go on over more lines (an example .cfg, say); those lines start with
// spaces. Text without labels is all problem, so older messages still read
// as before. parse_advice() recovers the parts from an exception's text.
// Internal to Convolver's tools and adapters.
namespace convolver::detail {
using convolver::Advice;

inline constexpr std::string_view advice_labels[] = {"Cause: ", "Fix: ", "Note: "};

// The parts as one text, leaving out empty ones. The problem is a phrase
// that follows "file:line: " or "error: ", so it starts in lower case
// (unless it starts with a name) and has no full stop; the other parts are
// whole sentences.
inline std::string advice_text(const Advice& a) {
    std::string text = a.problem;
    const std::string* parts[] = {&a.cause, &a.fix, &a.note};
    for (std::size_t i = 0; i < 3; ++i)
        if (!parts[i]->empty())
            text += "\n" + std::string(advice_labels[i]) + *parts[i];
    return text;
}

inline Advice parse_advice(std::string_view text) {
    Advice a;
    std::string* parts[] = {&a.problem, &a.cause, &a.fix, &a.note};
    std::string* current = &a.problem;
    bool first = true;
    while (true) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        bool labelled = false;
        if (!first)
            for (std::size_t i = 0; i < 3; ++i)
                if (line.starts_with(advice_labels[i])) {
                    current = parts[i + 1];
                    if (!current->empty())
                        *current += ' ';
                    *current += line.substr(advice_labels[i].size());
                    labelled = true;
                    break;
                }
        if (!labelled) {
            if (!first)
                *current += '\n';
            *current += line;
        }
        first = false;
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return a;
}

// The problem alone: what another message quotes when it explains the
// failure in its own words.
inline std::string advice_problem(std::string_view text) {
    return parse_advice(text).problem;
}

// Whether a sentence or phrase already ends as a sentence does.
inline bool ends_sentence(std::string_view text) {
    return !text.empty() &&
           (text.back() == '.' || text.back() == '!' || text.back() == '?' || text.back() == ':');
}

// One line, for the Convolver log: "problem. Cause: ... Fix: ... Note: ...".
// Lines that go on (an example .cfg) are joined with " / ", which keeps them
// apart.
inline std::string advice_line(const Advice& a) {
    std::string line;
    const auto join = [&](std::string_view part) {
        for (std::size_t i = 0; i < part.size(); ++i) {
            if (part[i] != '\n') {
                line += part[i];
                continue;
            }
            // A continued line: drop its indentation.
            while (i + 1 < part.size() && part[i + 1] == ' ')
                ++i;
            line += " / ";
        }
    };
    join(a.problem);
    const std::string* parts[] = {&a.cause, &a.fix, &a.note};
    bool first = true;
    for (std::size_t i = 0; i < 3; ++i) {
        if (parts[i]->empty())
            continue;
        if (first && !ends_sentence(line))
            line += '.';
        first = false;
        line += ' ';
        line += advice_labels[i];
        join(*parts[i]);
    }
    return line;
}
// The same for text that may hold labelled lines (an exception's message).
inline std::string advice_line(std::string_view text) {
    return advice_line(parse_advice(text));
}

// A problem phrase as a sentence of its own, for the plug-in window and the
// log: the first letter in capitals, unless the phrase starts with a file
// name or path (a word holding '.', '/', '\\', ':' or a quote), and a full
// stop at the end.
inline std::string as_sentence(std::string_view phrase) {
    std::string text(phrase);
    if (text.empty())
        return text;
    const auto word = text.substr(0, text.find(' '));
    if (word.find_first_of("./\\:'\"") == std::string::npos && text[0] >= 'a' && text[0] <= 'z')
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    if (!ends_sentence(text))
        text += '.';
    return text;
}

// What a command-line tool prints: "program: error: problem", then each
// further line indented by two spaces.
inline std::string cli_error(std::string_view program, std::string_view text) {
    std::string out = std::string(program) + ": error: ";
    for (std::size_t i = 0; i < text.size(); ++i) {
        out += text[i];
        if (text[i] == '\n')
            out += "  ";
    }
    return out + "\n";
}
} // namespace convolver::detail
