// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace convolver {
// DIAG-2: a message a user can meet, in its parts
// (docs/develop/plugin-contract.md#failure-reasons). `problem` is a phrase
// that follows "file:line: " or "error: ", so it starts in lower case
// (unless it starts with a name) and has no full stop; the other parts are
// whole sentences: the cause, the fix and, where an environment, host or
// design limit is the reason, a note. A part may go on over more lines (an
// example .cfg, say), whose continuation lines start with spaces.
// DIAG-4: failure records carry these parts as they are; each tool, log or
// window formats them where the message leaves the library. Exceptions carry
// the same parts as text, one labelled line each ("Cause: ", "Fix: ",
// "Note: ") after the problem.
struct Advice {
    // The empty initializers let `Advice{"problem"}` name only the problem
    // without a missing-field warning.
    std::string problem{}, cause{}, fix{}, note{};
    bool empty() const noexcept {
        return problem.empty() && cause.empty() && fix.empty() && note.empty();
    }
    bool operator==(const Advice&) const = default;
};
} // namespace convolver
