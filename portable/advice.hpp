// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/advice.hpp"
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

// DIAG-2: every message a user can meet has the same parts (Advice, in
// convolver/advice.hpp; docs/archive/plans/2026-11-05-actionable-diagnostics.md):
// the problem in one sentence, then its cause, the fix and, where an
// environment, host or design limit is the reason, a note. DIAG-4: failure
// records (HostFailure::message, HostFilter::notice()) keep the parts.
// PLUG-39 step 5b: so do exceptions. An AdvisedError keeps its Advice typed
// beside its what() text, which is advice_text(): each part after the
// problem on its own line starting "Cause: ", "Fix: " or "Note: ", a part
// going on over more lines (an example .cfg, say) on lines that start with
// spaces. advice_of() reads the parts back without parsing the text; an
// exception that is not advised (the standard library's, a decoder's) is
// all problem. Internal to Convolver's tools and adapters.
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
        if (!parts[i]->empty()) {
            text += '\n';
            text += advice_labels[i];
            text += *parts[i];
        }
    return text;
}

// An exception's Advice, whatever its standard type. Shared, so copying the
// exception cannot throw.
class Advised {
  public:
    explicit Advised(Advice a) : advice_(std::make_shared<const Advice>(std::move(a))) {}
    const Advice& advice() const noexcept {
        return *advice_;
    }

  protected:
    ~Advised() = default;
    Advised(const Advised&) noexcept = default;
    Advised& operator=(const Advised&) noexcept = default;
    void advise(Advice a) {
        advice_ = std::make_shared<const Advice>(std::move(a));
    }

  private:
    std::shared_ptr<const Advice> advice_;
};
// A standard exception (std::runtime_error, std::invalid_argument, ...) or
// one of Convolver's, whose what() is advice_text() of its Advice, so code
// that catches the standard type reads the same text as before.
template <class Error> class AdvisedError final : public Error, public Advised {
  public:
    explicit AdvisedError(Advice a) : Error(advice_text(a)), Advised(std::move(a)) {}
};
// The parts of a caught exception: as thrown when it was advised, otherwise
// its text as the problem.
inline Advice advice_of(const std::exception& e) {
    if (const auto* advised = dynamic_cast<const Advised*>(&e))
        return advised->advice();
    return Advice{e.what()};
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

// PLUG-25 (decisions log, 8 November 2026): the sentence every refusal for
// the host's sample rate adds to its fix, wherever the fix is shown (the
// window, Status details, the Convolver log, LV2's host log). Audacity 4
// played real-time effects at its Default sample rate while it exported at
// the project's, so a filter that exported well was refused during playback.
inline constexpr std::string_view host_playback_rate_fix =
    "The host may play at a different rate from the one it exports at.";

// What a command-line tool prints: "program: KIND: problem" (KIND is error,
// warning or note), then each further line indented by two spaces.
inline std::string cli_message(std::string_view program, std::string_view kind,
                               std::string_view text) {
    std::string out = std::string(program) + ": " + std::string(kind) + ": ";
    for (std::size_t i = 0; i < text.size(); ++i) {
        out += text[i];
        if (text[i] == '\n')
            out += "  ";
    }
    return out + "\n";
}
inline std::string cli_error(std::string_view program, std::string_view text) {
    return cli_message(program, "error", text);
}
} // namespace convolver::detail
