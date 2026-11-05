// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>

// DIAG-2: every message a user can meet has the same parts
// (docs/project/plans/2026-11-05-actionable-diagnostics.md): the problem in
// one sentence, then its cause, the fix and, where an environment, host or
// design limit is the reason, a note. Messages travel as plain text (in
// exceptions, HostFailure::message and the log), with each part after the
// problem on its own line starting "Cause: ", "Fix: " or "Note: ". A part can
// go on over more lines (an example .cfg, say); those lines start with
// spaces. Text without labels is all problem, so older messages still read
// as before. Internal to Convolver's tools and adapters.
namespace convolver::detail {
struct Advice {
    std::string problem, cause, fix, note;
};

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

// One line, for the Convolver log and JSON reports: "Problem. Cause: ...
// Fix: ... Note: ...". Lines that go on (an example .cfg) are joined with
// " / ", which keeps them apart.
inline std::string advice_line(std::string_view text) {
    std::string line;
    line.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n') {
            line += text[i];
            continue;
        }
        const auto next = text.substr(i + 1);
        bool labelled = false;
        for (const auto label : advice_labels)
            labelled = labelled || next.starts_with(label);
        if (labelled) {
            line += ' ';
            continue;
        }
        // A continued line: drop its indentation.
        while (i + 1 < text.size() && text[i + 1] == ' ')
            ++i;
        line += " / ";
    }
    return line;
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
