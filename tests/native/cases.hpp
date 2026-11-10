// SPDX-License-Identifier: GPL-2.0-or-later
// CI-37: run every case of a test program, so that one failed check does not
// hide the cases after it (each hidden failure cost another CI round). A case
// fails when it throws; the runner prints its name and reason and goes on.
// No framework: a case is a name and a function, as the programs already
// had, and require() still throws.
#pragma once
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace convolver::test {
struct Case {
    std::string name;
    std::function<void()> run;
};
// Runs `cases` in order and returns how many failed. `after_failure`, when
// given, runs first after a failed case, to undo what the case may have left
// set (an allocation guard, a forced code path) before the report is printed
// and the next case starts. Ends with a count when any case failed.
inline std::size_t run_cases(const std::vector<Case>& cases,
                             const std::function<void()>& after_failure = {}) {
    std::size_t failed = 0;
    for (const auto& c : cases) {
        std::string reason;
        try {
            c.run();
            continue;
        } catch (const std::exception& e) {
            if (after_failure)
                after_failure();
            reason = e.what();
        } catch (...) {
            if (after_failure)
                after_failure();
            reason = "an exception that is not a std::exception";
        }
        ++failed;
        std::cerr << "FAILED " << c.name << ": " << reason << '\n';
    }
    if (failed)
        std::cerr << failed << " of " << cases.size() << " cases failed\n";
    return failed;
}
} // namespace convolver::test
