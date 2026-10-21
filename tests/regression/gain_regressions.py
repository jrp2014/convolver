#!/usr/bin/env python3
"""Run the production gain loops without the legacy Windows/FFT dependencies.

The source statements are extracted verbatim and compiled with AddressSanitizer.
Small data holders supply the same fields/accessors; expected gains are literal,
hand-calculated values. This checks gain accumulation, not config-file parsing
or the full Windows application.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


HARNESS = r"""
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
using WORD = std::uint16_t;
using std::abs;

struct ScaledChannel { WORD nChannel; float fScale; };
struct Filter {
    float estimated, peak;
    float estimated_gain() const { return estimated; }
    float peak_gain() const { return peak; }
};
struct Path {
    std::vector<ScaledChannel> inChannel;
    Filter filter;
    std::vector<ScaledChannel> outChannel;
};
struct Mixer {
    std::vector<Path> Paths_;
    unsigned int nPaths_;
    std::vector<float> estimated_output_gain_ = std::vector<float>(3, 0);
    std::vector<float> peak_output_gain_ = std::vector<float>(3, 0);
    float estimated_gain_ = 0, peak_gain_ = 0;
    WORD nOutputChannels() const { return 3; }
    void calculate() {
        for (WORD nPath = 0; nPath < nPaths_; ++nPath) {
            @GAIN_LOOPS@
    }
};

static bool check(const char* name, const std::vector<float>& got,
                  const std::vector<float>& expected) {
    bool ok = got.size() == expected.size();
    for (std::size_t i = 0; i < got.size() && i < expected.size(); ++i) {
        if (!std::isfinite(got[i]) || std::abs(got[i] - expected[i]) > 1e-6f) {
            std::cerr << name << '[' << i << "]: got " << got[i]
                      << ", expected " << expected[i] << '\n';
            ok = false;
        }
    }
    return ok;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Mixer mixer;
    const bool fewer = std::atoi(argv[1]) == 0;
    if (fewer) {
        mixer.Paths_ = {
            {{{0, 1}}, {2, 3}, {{0, 1}, {1, .5f}}},
            {{{0, -.5f}, {1, .25f}}, {4, 8}, {{1, -1}}}
        };
    } else {
        mixer.Paths_ = {
            {{{0, 1}}, {2, 3}, {{0, 1}}},
            {{{0, -.5f}, {1, .25f}}, {4, 8}, {{0, -.5f}, {2, 1}}}
        };
    }
    mixer.nPaths_ = static_cast<unsigned int>(mixer.Paths_.size());
    mixer.calculate();
    bool ok = check("estimated output gain", mixer.estimated_output_gain_,
                    fewer ? std::vector<float>{2, 4, 0}
                          : std::vector<float>{3.5f, 0, 3});
    ok = check("peak output gain", mixer.peak_output_gain_,
               fewer ? std::vector<float>{3, 7.5f, 0}
                     : std::vector<float>{6, 0, 6}) && ok;
    ok = check("maximum gains", {mixer.estimated_gain_, mixer.peak_gain_},
               fewer ? std::vector<float>{4, 7.5f}
                     : std::vector<float>{3.5f, 6}) && ok;
    return ok ? 0 : 1;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path,
        default=Path(__file__).resolve().parents[2]
        / "historical/convolution/channelpaths.cpp",
        help="channelpaths.cpp to test, including an untouched archived copy",
    )
    args = parser.parse_args()
    source = args.source.read_text()
    start = source.index("\t\t// Calculate the gain for each output channel")
    end = source.index("\n#if defined(DEBUG)", start)
    gain_loops = source[start:end]
    failures = 0
    with tempfile.TemporaryDirectory(prefix="convolver-gain-") as scratch:
        scratch = Path(scratch)
        cpp = scratch / "gain_regressions.cpp"
        binary = scratch / "gain_regressions"
        cpp.write_text(HARNESS.replace("@GAIN_LOOPS@", gain_loops))
        subprocess.run(
            [os.environ.get("CXX", "clang++"), "-std=c++11", "-O1", "-g",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
             str(cpp), "-o", str(binary)],
            check=True,
        )
        for scenario, label in enumerate(("later path has fewer outputs",
                                          "later path has more outputs")):
            result = subprocess.run([str(binary), str(scenario)],
                                    capture_output=True, text=True)
            print(f"{'PASS' if result.returncode == 0 else 'FAIL'}: {label}")
            if result.returncode:
                failures += 1
                print(result.stdout + result.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
