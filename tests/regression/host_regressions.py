#!/usr/bin/env python3
"""Compile actual sample.h adapters with portable boundary types.

This tests conversion and frame transfer, not a native Windows/VST plug-in.
NoDither/NoNoiseShape and their scales are actual source; Boost's Floor boundary
is replaced by checked std::floor for the tested finite, in-range values.
The extracted adapters use the review branch small input-delay-line API.
Use --source to test another sample.h with the same API.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def section(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def source_fragments(text, dither):
    interfaces = section(text, "template <typename T>\nstruct __declspec", "template <typename T>\nclass InterleavedIO")
    vst = section(text, "template <typename SampleT, typename T>\nclass VSTIO", "// Specializations with")
    converters = section(text, "template <typename T>\nstruct ConvertSample_ieeefloat", "template <typename T>\nstruct ConvertSample_pcm8")
    pcm24 = section(text, "struct ConvertSample_pcm24", "struct ConvertSample_pcm32")
    pcm32 = section(text, "struct ConvertSample_pcm32", "// 32-bit sound.")
    fragments = interfaces + vst + converters
    # MSVC's old dependent-base lookup accepted these names unqualified.
    # Make that lookup explicit for the portable compiler, without changing logic.
    fragments = fragments.replace("__declspec(novtable)", "").replace("__single_inheritance", "")
    fragments = fragments.replace("public:\n\tVSTIO", "public:\n\tusing SampleIO<T>::input_sample_convertor_;\n\tusing SampleIO<T>::output_sample_convertor_;\n\tVSTIO")
    quantization = section(dither, "template <typename T>\nclass Dither", "//  \n")
    quantization += section(dither, "template <typename T, typename IntType>\nstruct NoiseShape", "// SimpleShaping")
    adapters = []
    for bits, pcm in [(24, pcm24), (32, pcm32)]:
        methods = section(pcm, "\tvoid GetSample(T& dstSample,", "\tWORD nContainerSize()")
        adapters.append(f"""
template<int validBits> struct Pcm{bits}Adapter {{
    typedef float T;
    TestHolder<Dither<T> > dither_;
    TestHolder<NoiseShape<T, INT32> > noiseshape_;
    Pcm{bits}Adapter() : dither_(new NoDither<T>()),
        noiseshape_(new NoNoiseShape<T, INT32, validBits>(1)) {{}}
{methods}
}};
""")
    return fragments + quantization + "\n".join(adapters)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "convolver/convolution/sample.h")
    args = parser.parse_args()
    text = args.source.read_text(encoding="latin-1")
    dither = args.source.with_name("dither.h").read_text(encoding="latin-1")
    text = text.replace("\r\n", "\n")
    dither = dither.replace("\r\n", "\n")
    scaffold = (Path(__file__).with_name("host_regressions.cpp")).read_text()
    code = scaffold.replace("// INSERT_SOURCE_ADAPTERS", source_fragments(text, dither))
    compiler = shutil.which(os.environ.get("CXX", "clang++"))
    if compiler is None:
        parser.error("Clang is required; set CXX to its executable path")
    failures = 0
    cases = ["pcm24", "pcm20", "pcm16", "inplace", "cross_alias", "accumulate", "double", "delays", "null_input", "null_delayed_history", "mixed_delays", "overlap", "null_output"]
    cases += [f"pcm32_{bits}" for bits in (16, 20, 24)]
    cases += [f"out{container}_{bits}" for container in (24, 32) for bits in (16, 20, 24)]
    with tempfile.TemporaryDirectory(prefix="convolver-host-tests-") as temp:
        source = Path(temp) / "regressions.cpp"
        source.write_text(code)
        for build, options in [("debug", []), ("release", ["-DNDEBUG"])]:
            executable = Path(temp) / build
            subprocess.run([compiler, "-std=c++11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *options, str(source), "-o", str(executable)], check=True)
            for case in cases:
                result = subprocess.run([str(executable), case], capture_output=True, text=True)
                status = "PASS" if result.returncode == 0 else "FAIL"
                print(f"{status} {build}: {case}", flush=True)
                if result.returncode:
                    failures += 1
                    print(result.stdout + result.stderr, end="", flush=True)
    print(f"{len(cases) * 2 - failures}/{len(cases) * 2} cases passed")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
