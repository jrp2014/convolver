// SPDX-License-Identifier: GPL-2.0-or-later
// The load-timing harness itself (tests/plugins/load_timing.hpp): the input
// it plays.
#include "../plugins/load_timing.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace timing = convolver::plugin_test::load_timing;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cout << "FAILED: " << what << '\n';
        ++failures;
    }
}

// PERF-11: the input is quiet noise of both signs, different on each channel
// (the first version wrapped its negative half to about 1.8e15).
void input_is_quiet_noise() {
    const auto a = timing::quiet_noise(0, 512), b = timing::quiet_noise(1, 512);
    for (const auto* v : {&a, &b}) {
        const auto [lo, hi] = std::minmax_element(v->begin(), v->end());
        std::cout << "input: " << *lo << " to " << *hi << '\n';
        check(*lo >= -0.1f && *lo < -0.05f, "the input reaches below -0.05 and not below -0.1");
        check(*hi <= 0.1f && *hi > 0.05f, "the input reaches above 0.05 and not above 0.1");
        check(std::all_of(v->begin(), v->end(), [](float x) { return std::isfinite(x); }),
              "the input is finite");
    }
    check(a != b, "the channels differ");
}
} // namespace

int main() {
    try {
        input_is_quiet_noise();
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << '\n';
        return 1;
    }
    std::cout << "load-timing harness: " << failures << " failed\n";
    return failures ? 1 : 0;
}
