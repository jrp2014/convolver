// SPDX-License-Identifier: GPL-2.0-or-later
// Inspection API: selection, diagnostics, null fields, and agreement with Engine.
#include "convolver/inspection.hpp"
#include "convolver/io.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace convolver;
namespace {
void require(bool b, const std::string& message) {
    if (!b)
        throw std::runtime_error(message);
}
void text_file(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
    if (!f)
        throw std::runtime_error("fixture write failed");
}
void wave(const fs::path& p, int rate, std::size_t channels, const std::vector<float>& samples) {
    AudioWriter w(p, {rate, channels}, OutputFormat::wav_float);
    w.write(samples);
    w.close();
}
bool has_code(const InspectionReport& r, const std::string& code) {
    for (const auto& d : r.diagnostics)
        if (d.code == code)
            return true;
    return false;
}
std::set<fs::path> listing(const fs::path& dir) {
    std::set<fs::path> result;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        result.insert(e.path());
    return result;
}
template <class F> void rejects_argument(F f, const char* message) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

void run(const fs::path& dir) {
    std::vector<float> long_ir(1000);
    for (std::size_t i = 0; i < long_ir.size(); ++i)
        long_ir[i] = std::cos(static_cast<float>(i)) / static_cast<float>(i + 1);
    wave(dir / "long.wav", 48000, 1, long_ir);
    wave(dir / "short.wav", 48000, 2, {1, .5f, .25f, 0});
    wave(dir / "ir44.wav", 44100, 1, {1});
    text_file(dir / "room.cfg", "48000 2 3 3\n0.5 0\n0 0 1.25\nlong.wav\n0\n-0 1.5\n0 2.25\n"
                                "short.wav\n1\n1\n1\n");
    const auto cfg = load_configuration(dir / "room.cfg");
    // Analysis latency/tail equal a constructed Engine's at odd block sizes.
    for (std::size_t block : {1u, 17u, 1023u}) {
        const auto analysis = analyze_configuration(cfg, block);
        Engine engine(cfg, block);
        require(engine.latency_frames() == analysis.latency_frames &&
                    engine.tail_frames() == analysis.tail_frames,
                "analysis latency/tail equal Engine values at block " + std::to_string(block));
    }
    // From here on nothing may plan FFTs (changing wisdom) or create files.
    const auto before = listing(dir);
    const auto wisdom = fft_supports_wisdom() ? export_fft_wisdom() : std::string();
    for (std::size_t block : {1u, 3u, 17u, 100u, 256u, 1023u}) {
        const auto analysis = analyze_configuration(cfg, block);
        InspectionOptions options;
        options.block_frames = block;
        const auto report = inspect_configuration(dir / "room.cfg", options);
        require(report.valid && report.configurations.size() == 1, "valid single report");
        const auto& leaf = report.configurations[0];
        require(leaf.analysis && leaf.analysis->tail_frames == analysis.tail_frames &&
                    leaf.analysis->latency_frames == analysis.latency_frames &&
                    leaf.analysis->accounted_buffer_bytes == analysis.accounted_buffer_bytes,
                "inspection uses the core analysis");
        require(analysis.tail_frames == configuration_tail_frames(cfg, block),
                "analysis tail matches configuration_tail_frames");
        require(analysis.partitions_per_path ==
                    std::vector<std::size_t>{(999 + block) / block, (1 + block) / block},
                "partition counts");
        require(analysis.fft_frames >= 2 * block && analysis.accounted_buffer_bytes > 0,
                "FFT length and storage");
    }
    {
        const auto report = inspect_configuration(dir / "room.cfg");
        const auto& leaf = report.configurations[0];
        require(leaf.channel_mask == 3 && leaf.output_channels == 3 &&
                    leaf.input_delays[0].frames == 24 && leaf.input_delays[0].token == "0.5" &&
                    leaf.output_delays[2].milliseconds == 1.25f &&
                    leaf.output_delays[2].frames == 60,
                "declared and effective delays");
        const auto& p = leaf.paths[0];
        require(p.inputs[0].token == "-0" && p.inputs[0].channel == 0 && p.inputs[0].gain == -1 &&
                    p.inputs[1].channel == 1 && p.inputs[1].gain == .5f,
                "signed zero reported as channel 0 gain -1 with its token");
        require(p.reference == "long.wav" && p.location.line == 4 && p.ir_frames == 1000u &&
                    p.ir_sample_rate == 48000 && p.ir_file_channels == 1u,
                "IR reference, location and decoded size");
        require(leaf.paths[1].ir_channel == 1 && leaf.paths[1].ir_frames == 2u, "IR channel");
        require(leaf.analysis->tail_frames == 999 + 24 + 60, "tail includes delays");
    }
    // Syntax-only: no IR is opened, so missing IRs pass and unverified fields are null.
    text_file(dir / "absent.cfg", "48000 1 1 0\n0\n0\nnot-installed.wav\n0\n0\n0\n");
    {
        InspectionOptions options;
        options.syntax_only = true;
        const auto report = inspect_configuration(dir / "absent.cfg", options);
        require(report.valid && !report.configurations[0].coefficients_verified &&
                    !report.configurations[0].analysis &&
                    !report.configurations[0].paths[0].ir_frames &&
                    !report.configurations[0].paths[0].partitions &&
                    report.configurations[0].paths[0].resolved == dir / "not-installed.wav",
                "syntax-only leaves unverified values null");
        require(!inspect_configuration(dir / "absent.cfg").valid &&
                    has_code(inspect_configuration(dir / "absent.cfg"), "missing-asset"),
                "full validation reports the missing IR");
        text_file(dir / "two.txt", "absent.cfg\nroom.cfg\n");
        const auto list = inspect_configuration(dir / "two.txt", options);
        require(!list.valid && has_code(list, "syntax-only-scope"), "syntax-only rejects lists");
        require(has_code(inspect_configuration(dir / "long.wav", options), "syntax-only-scope"),
                "syntax-only rejects sound files");
        text_file(dir / "bad.cfg", "48000 1 1 0\n0\n0\nx.wav\n0\n3\n0\n");
        const auto bad = inspect_configuration(dir / "bad.cfg", options);
        require(!bad.valid && bad.diagnostics.at(0).location.line == 6 &&
                    bad.diagnostics[0].code == "syntax",
                "syntax-only reports located grammar errors");
    }
    // Selection: unrelated missing IRs matter only to all-entry validation.
    text_file(dir / "unrelated.cfg", "44100 1 1 0\n0\n0\nmissing-unused.wav\n0\n0\n0\n");
    text_file(dir / "list.txt", "unrelated.cfg\nir44.wav\nroom.cfg\n");
    {
        InspectionOptions options;
        options.expected_rate = 48000;
        const auto report = inspect_configuration(dir / "list.txt", options);
        require(report.valid && report.selected == dir / "room.cfg", "selected entry");
        require(report.traversal.size() == 4 && report.traversal[0].outcome == "list" &&
                    report.traversal[1].outcome == "rate-mismatch" &&
                    report.traversal[2].outcome == "rate-mismatch" &&
                    report.traversal[3].outcome == "selected",
                "traversal outcomes");
        require(report.configurations.size() == 1, "only the selected entry is inspected");
        options.expected_rate = 0;
        options.all_configurations = true;
        const auto all = inspect_configuration(dir / "list.txt", options);
        require(!all.valid && all.configurations.size() == 3 && !all.configurations[0].valid &&
                    all.configurations[1].valid && all.configurations[2].valid &&
                    has_code(all, "missing-asset"),
                "all-entry mode validates every leaf and continues after failures");
        require(all.diagnostics[0].include_chain.size() == 1 &&
                    all.diagnostics[0].include_chain[0].line == 1,
                "leaf failures carry the include chain");
        text_file(dir / "cyclic.txt", "room.cfg\ncyclic.txt\nbroken.txt\n");
        text_file(dir / "broken.txt", "48000 1\n");
        const auto cyclic = inspect_configuration(dir / "cyclic.txt", options);
        require(!cyclic.valid && cyclic.configurations.size() == 1 && has_code(cyclic, "cycle") &&
                    has_code(cyclic, "syntax"),
                "all-entry mode reports a cycle and a broken sibling");
        InspectionOptions selected;
        require(inspect_configuration(dir / "cyclic.txt", selected).valid,
                "selected mode stops at the first entry before the cycle");
    }
    {
        InspectionOptions options;
        options.expected_rate = 96000;
        const auto none = inspect_configuration(dir / "list.txt", options);
        require(!none.valid && has_code(none, "no-match") && !none.selected, "no match");
        options.expected_rate = 48000;
        options.expected_inputs = 2;
        text_file(dir / "fails.txt", "unrelated.cfg\nabsent2.cfg\nroom.cfg\n");
        text_file(dir / "absent2.cfg", "48000 2 1 0\n0 0\n0\nmissing.wav\n0\n0\n0\n");
        const auto failed = inspect_configuration(dir / "fails.txt", options);
        require(!failed.valid && failed.selected == dir / "absent2.cfg" &&
                    !failed.configurations[0].valid,
                "a failed selected entry cannot produce a valid report");
    }
    rejects_argument(
        [&] {
            InspectionOptions o;
            o.all_configurations = true;
            o.expected_rate = 48000;
            inspect_configuration(dir / "list.txt", o);
        },
        "all-entry mode rejects selectors");
    rejects_argument(
        [&] {
            InspectionOptions o;
            o.block_frames = 0;
            inspect_configuration(dir / "room.cfg", o);
        },
        "zero block");
    // Unicode, spaces and (where the filesystem allows) quotes in names survive.
#ifdef _WIN32
    const std::u8string odd_name = u8"ïr 'q' ü.wav";
#else
    const std::u8string odd_name = u8"ïr \"q\" ü.wav";
#endif
    const std::string odd_text(odd_name.begin(), odd_name.end());
    const auto odd = dir / fs::path(odd_name);
    wave(odd, 48000, 1, {1});
    text_file(dir / "odd.cfg", "48000 1 1 0\n0\n0\n" + odd_text + "\n0\n0\n0\n");
    {
        const auto report = inspect_configuration(dir / "odd.cfg");
        require(report.valid && report.configurations[0].paths[0].reference == odd_text,
                "Unicode reference spelling");
    }
    // Inspection never plans FFTs or creates files.
    if (fft_supports_wisdom())
        require(export_fft_wisdom() == wisdom, "inspection changed FFTW wisdom");
    auto after = listing(dir);
    for (const auto& p : before)
        after.erase(p);
    for (const auto* name : {"absent.cfg", "two.txt", "bad.cfg", "unrelated.cfg", "list.txt",
                             "cyclic.txt", "broken.txt", "fails.txt", "absent2.cfg", "odd.cfg"})
        after.erase(dir / name);
    after.erase(odd);
    require(after.empty(), "inspection created files");
}
} // namespace

int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-inspection-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
        run(dir);
        fs::remove_all(dir);
        std::cout << "inspection tests passed\n";
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
