// SPDX-License-Identifier: GPL-2.0-or-later
// Inspection API: selection, diagnostics, null fields, and agreement with Engine.
#include "convolver/inspection.hpp"
#include "convolver/io.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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
        // An entry that cannot resolve here (a drive path on macOS/Linux, a missing
        // file on Windows) is reported and its siblings are still validated, in the
        // root list and in a nested one, with the whole chain of enclosing lists.
        InspectionOptions all;
        all.all_configurations = true;
        text_file(dir / "drive.txt", "C:\\absent\\x.cfg\nroom.cfg\nbad.cfg\n");
        const auto root = inspect_configuration(dir / "drive.txt", all);
        require(!root.valid && root.configurations.size() == 1 &&
                    root.configurations[0].file == dir / "room.cfg" && has_code(root, "syntax"),
                "entries after an unresolvable one are validated");
        text_file(dir / "outer.txt", "sub.txt\nroom.cfg\n");
        text_file(dir / "sub.txt", "C:\\absent\\x.cfg\nbad.cfg\n");
        const auto nested = inspect_configuration(dir / "outer.txt", all);
        const Diagnostic* bad = nullptr;
        for (const auto& d : nested.diagnostics)
            if (d.location.file.filename() == "bad.cfg")
                bad = &d;
        require(bad && nested.configurations.size() == 1, "nested siblings are validated");
        require(bad->include_chain.size() == 2 &&
                    bad->include_chain[0].file.filename() == "outer.txt" &&
                    bad->include_chain[0].line == 1 &&
                    bad->include_chain[1].file.filename() == "sub.txt" &&
                    bad->include_chain[1].line == 2,
                "all-entry diagnostics carry every enclosing list entry");
        // A bare "-" beside a configuration in the working directory is a missing
        // filter, reported in the report rather than thrown.
        const auto previous = fs::current_path();
        fs::current_path(dir);
        text_file("dash.cfg", "48000 1 1 0\n0\n0\n-\n0\n0\n0\n");
        const auto dash = inspect_configuration("dash.cfg");
        fs::current_path(previous);
        require(!dash.valid && has_code(dash, "missing-asset"), "'-' as a filter is diagnosed");
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
    {
        // Output-channel selection and combined mismatch outcomes.
        InspectionOptions options;
        options.expected_rate = 48000;
        options.expected_outputs = 3;
        const auto report = inspect_configuration(dir / "list.txt", options);
        require(report.valid && report.selected == dir / "room.cfg" &&
                    report.traversal.back().output_channels == 3,
                "select by output channels");
        options.expected_outputs = 2;
        options.expected_inputs = 1;
        const auto none = inspect_configuration(dir / "list.txt", options);
        std::vector<std::string> outcomes;
        for (const auto& step : none.traversal)
            outcomes.push_back(step.outcome);
        require(!none.valid && has_code(none, "no-match") &&
                    outcomes == std::vector<std::string>{"list", "rate-and-output-mismatch",
                                                         "rate-and-output-mismatch",
                                                         "input-and-output-mismatch"},
                "output mismatch outcomes");
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
    // A .pcm or .raw filter is mono little-endian float32 at the configuration's
    // rate: decoded, located when damaged, and flagged by portable-paths.
    {
        const auto raw_bytes = [&](const char* name, const std::vector<unsigned char>& b) {
            std::ofstream f(dir / name, std::ios::binary);
            f.write(reinterpret_cast<const char*>(b.data()),
                    static_cast<std::streamsize>(b.size()));
        };
        raw_bytes("drc.pcm", {0, 0, 0, 0x3f, 0, 0, 0x80, 0x3e}); // 0.5, 0.25
        raw_bytes("cut.RAW", {0, 0, 0x80});
        text_file(dir / "raw.cfg", "44100 1 1 0\n0\n0\ndrc.pcm\n0\n0\n0\n");
        text_file(dir / "cut.cfg", "44100 1 1 0\n0\n0\ncut.RAW\n0\n0\n0\n");
        text_file(dir / "rawch.cfg", "44100 1 1 0\n0\n0\ndrc.pcm\n1\n0\n0\n");
        const auto raw = inspect_configuration(dir / "raw.cfg");
        const auto& f = raw.configurations.at(0).paths.at(0);
        require(raw.valid && raw.diagnostics.empty() && f.ir_frames == 2 &&
                    f.ir_sample_rate == 44100 && f.ir_file_channels == 1,
                "a raw filter takes the configuration's rate");
        const auto loaded = load_configuration(dir / "raw.cfg");
        require(loaded.paths.at(0).impulse == std::vector<float>{.5f, .25f},
                "raw filter coefficients");
        const auto cut = inspect_configuration(dir / "cut.cfg");
        require(!cut.valid && has_code(cut, "damaged-asset") &&
                    cut.diagnostics[0].location.line == 4,
                "a damaged raw filter is located");
        require(!inspect_configuration(dir / "rawch.cfg").valid, "a raw filter has only channel 0");
        InspectionOptions portable;
        portable.profile = InspectionProfile::portable_paths;
        const auto flagged = inspect_configuration(dir / "raw.cfg", portable);
        require(flagged.valid && has_code(flagged, "raw-filter"),
                "portable-paths warns that other readers expect sound files");
        require(f.raw_encoding == RawEncoding{},
                "inspection reports the raw filter's assumed sample type (f32le)");
        // Another sample type can be chosen: big-endian 24-bit integers here.
        raw_bytes("s24.pcm", {0x40, 0, 0, 0xc0, 0, 0}); // 0.5, -0.5
        text_file(dir / "s24.cfg", "44100 1 1 0\n0\n0\ns24.pcm\n0\n0\n0\n");
        ConfigurationSelection s24;
        s24.raw_filter_encoding = {SampleType::pcm24, true};
        std::vector<std::filesystem::path> raws;
        const auto s24_loaded = load_configuration(dir / "s24.cfg", s24, nullptr, &raws);
        require(s24_loaded.paths.at(0).impulse == std::vector<float>{.5f, -.5f} &&
                    raws.size() == 1 && raws[0].filename() == "s24.pcm",
                "a raw filter in another sample type, and the list of raw filters read");
        // The list is replaced, not appended to, and is empty for a sound file.
        wave(dir / "plain.wav", 44100, 1, {1.f});
        load_configuration(dir / "plain.wav", s24, nullptr, &raws);
        require(raws.empty(), "a sound file reads no raw filters");
        InspectionOptions s24_inspect;
        s24_inspect.raw_filter_encoding = s24.raw_filter_encoding;
        const auto s24_report = inspect_configuration(dir / "s24.cfg", s24_inspect);
        require(s24_report.valid && s24_report.configurations.at(0).paths.at(0).ir_frames == 2 &&
                    s24_report.configurations.at(0).paths.at(0).raw_encoding ==
                        s24.raw_filter_encoding,
                "inspection reads a raw filter with the chosen sample type");
        // Six bytes are not whole float64 samples: damaged, and the message says why.
        ConfigurationSelection f64;
        f64.raw_filter_encoding = {SampleType::float64, false};
        bool damaged = false;
        try {
            load_configuration(dir / "s24.cfg", f64);
        } catch (const std::exception& e) {
            damaged = std::string(e.what()).find("f64le") != std::string::npos;
        }
        require(damaged, "a raw filter that does not fit the chosen type names the type");
        // Raw filters have no 8-bit type; the option is refused up front.
        ConfigurationSelection pcm8;
        pcm8.raw_filter_encoding = {SampleType::pcm8, false};
        InspectionOptions pcm8_inspect;
        pcm8_inspect.raw_filter_encoding = pcm8.raw_filter_encoding;
        for (const auto& call : std::vector<std::function<void()>>{
                 [&] { load_configuration(dir / "s24.cfg", pcm8); },
                 [&] { inspect_configuration(dir / "s24.cfg", pcm8_inspect); }}) {
            bool refused = false;
            try {
                call();
            } catch (const std::invalid_argument& e) {
                refused = std::string(e.what()) == "raw filters have no 8-bit sample type";
            }
            require(refused, "an 8-bit raw filter type is refused");
        }
        require(parse_raw_encoding("s24be") == RawEncoding{SampleType::pcm24, true} &&
                    !parse_raw_encoding("u8") && raw_encoding_name({}) == "f32le" &&
                    raw_encoding_name({SampleType::pcm16, true}) == "s16be",
                "raw encoding names");
        // A WAV named .pcm keeps reading through its header, without the warning.
        wave(dir / "wav.pcm", 44100, 2, {.5f, 1, .25f, 1});
        text_file(dir / "wavpcm.cfg", "44100 1 1 0\n0\n0\nwav.pcm\n0\n0\n0\n");
        const auto named = inspect_configuration(dir / "wavpcm.cfg", portable);
        require(named.valid && !has_code(named, "raw-filter") &&
                    named.configurations.at(0).paths.at(0).ir_frames == 2 &&
                    named.configurations.at(0).paths.at(0).ir_file_channels == 2 &&
                    !named.configurations.at(0).paths.at(0).raw_encoding,
                "a WAV named .pcm is read as WAV");
    }
    // Headered files named .pcm or .raw keep their header's meaning, whatever
    // the format: libsndfile decides, not a list of signatures. Syntax-only
    // inspection opens no filter and says its raw-filter warning is assumed.
    {
        const auto box = dir / "headered";
        fs::create_directory(box);
        const auto bytes = [&](const char* name, const std::vector<unsigned char>& b) {
            std::ofstream f(box / name, std::ios::binary);
            f.write(reinterpret_cast<const char*>(b.data()),
                    static_cast<std::streamsize>(b.size()));
        };
        // Sun/NeXT AU, mono float32 at 44100 Hz: 1, 0.5, 0.25, 0.125 (big-endian).
        const std::vector<unsigned char> au{'.', 's', 'n',  'd',  0,    0,    0,    24, 0,    0,
                                            0,   16,  0,    0,    0,    6,    0,    0,  0xac, 0x44,
                                            0,   0,   0,    1,    0x3f, 0x80, 0,    0,  0x3f, 0,
                                            0,   0,   0x3e, 0x80, 0,    0,    0x3e, 0,  0,    0};
        InspectionOptions portable;
        portable.profile = InspectionProfile::portable_paths;
        for (const auto* name : {"sun.raw", "sun.PCM"}) {
            bytes(name, au);
            text_file(box / "sun.cfg", std::string("44100 1 1 0\n0\n0\n") + name + "\n0\n0\n0\n");
            const auto report = inspect_configuration(box / "sun.cfg", portable);
            const auto& f = report.configurations.at(0).paths.at(0);
            require(report.valid && !has_code(report, "raw-filter") && f.ir_frames == 4 &&
                        !f.raw_encoding,
                    std::string("an AU file named ") + name + " is read through its header");
            std::vector<std::filesystem::path> raws;
            const auto loaded = load_configuration(box / "sun.cfg", {}, nullptr, &raws);
            require(loaded.paths.at(0).impulse == std::vector<float>{1, .5f, .25f, .125f} &&
                        raws.empty(),
                    std::string("an AU file named ") + name + " keeps its coefficients");
        }
        // f32le 1159.875 is FF FB 90 44, a bare MPEG frame sync, and 1.0001222
        // is 01 04 80 3F, the MPC 2000 marker: realistic raw filters starting
        // with them are still read as samples.
        for (const auto& [name, first] : {std::pair{"sync.raw", std::uint32_t{0x4490fbff}},
                                          std::pair{"mpc.raw", std::uint32_t{0x3f800401}}}) {
            std::vector<unsigned char> b;
            for (int i = 0; i < 4096; ++i) {
                const auto word = i == 0 ? first : std::bit_cast<std::uint32_t>(1e-6f * i);
                for (int k = 0; k < 4; ++k)
                    b.push_back(static_cast<unsigned char>(word >> (8 * k)));
            }
            bytes(name, b);
            text_file(box / "first.cfg", std::string("44100 1 1 0\n0\n0\n") + name + "\n0\n0\n0\n");
            const auto impulse = load_configuration(box / "first.cfg").paths.at(0).impulse;
            require(impulse.size() == 4096 && impulse[0] == std::bit_cast<float>(first) &&
                        impulse[1] == 1e-6f,
                    std::string("raw samples starting like a weak signature are samples: ") + name);
        }
        // Markers libsndfile does not report as recognised (BW64, an AU in an
        // encoding it lacks) still make the file a sound file: an error that
        // explains itself, not header bytes read as coefficients.
        bytes("bw.raw", {'B', 'W', '6', '4', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 0, 0, 0, 0});
        bytes("unknown.raw", {'.', 's', 'n', 'd', 0,    0,    0, 24, 0, 0, 0, 4, 0, 0,
                              0,   99,  0,   0,   0xac, 0x44, 0, 0,  0, 1, 0, 0, 0, 0});
        // A recognised header that cannot be read is an error that explains itself.
        bytes("broken.raw", {'.', 's', 'n', 'd', 0, 0, 0, 0, 0, 0, 0, 0});
        for (const auto* name : {"broken.raw", "bw.raw", "unknown.raw"}) {
            text_file(box / "broken.cfg",
                      std::string("44100 1 1 0\n0\n0\n") + name + "\n0\n0\n0\n");
            const auto broken = inspect_configuration(box / "broken.cfg");
            require(!broken.valid && has_code(broken, "missing-asset") &&
                        broken.diagnostics.at(0).message.find(
                            "the content starts like a sound file") != std::string::npos,
                    std::string("an unreadable header is reported, not read as samples: ") + name);
        }
        // Syntax-only opens no filter: the warning describes an assumption.
        InspectionOptions syntax = portable;
        syntax.syntax_only = true;
        const auto assumed = inspect_configuration(box / "sun.cfg", syntax);
        require(assumed.valid && has_code(assumed, "raw-filter") &&
                    assumed.diagnostics.at(0).message.find("did not open it") != std::string::npos,
                "syntax-only warns from the name and says it did not check");
#ifndef _WIN32
        // A named pipe called .raw must not be opened: that would block.
        const auto pipe = box / "pipe.raw";
        require(mkfifo(pipe.c_str(), 0600) == 0, "mkfifo");
        text_file(box / "pipe.cfg", "44100 1 1 0\n0\n0\npipe.raw\n0\n0\n0\n");
        auto pending = std::async(std::launch::async,
                                  [&] { return inspect_configuration(box / "pipe.cfg", syntax); });
        if (pending.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
            // Release the blocked reader, as often as it opens the pipe again,
            // so the test fails instead of hanging.
            while (pending.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
                const int writer = open(pipe.c_str(), O_WRONLY | O_NONBLOCK);
                if (writer >= 0)
                    close(writer);
            }
            require(false, "syntax-only inspection opened a named pipe");
        }
        const auto piped = pending.get();
        require(piped.valid && has_code(piped, "raw-filter"),
                "syntax-only inspection of a pipe named .raw finishes and warns");
#endif
        fs::remove_all(box);
    }
    // Profiles: portable-paths adds warnings only; it never changes validity of errors.
    {
        fs::create_directory(dir / "pack");
        fs::create_directory(dir / "pack" / "irs");
        wave(dir / "pack" / "irs" / "a.wav", 48000, 1, {1, .5f});
        const auto absolute = fs::absolute(dir / "pack" / "irs" / "a.wav").generic_u8string();
        text_file(dir / "pack" / "abs.cfg", "48000 1 1 0\n0\n0\n" +
                                                std::string(absolute.begin(), absolute.end()) +
                                                "\n0\n0\n0\n");
        text_file(dir / "pack" / "mixed.cfg", "48000 1 1 0\n0\n0\nirs\\./a.wav\n0\n0\n0\n");
        text_file(dir / "pack" / "up.cfg", "48000 1 1 0\n0\n0\n../long.wav\n0\n0\n0\n");
        text_file(dir / "pack" / "gone.cfg", "48000 1 1 0\n0\n0\nirs/none.wav\n0\n0\n0\n");
        InspectionOptions native, portable;
        portable.profile = InspectionProfile::portable_paths;
        auto warnings = [](const InspectionReport& r) {
            std::vector<std::string> codes;
            for (const auto& d : r.diagnostics)
                if (d.severity == DiagnosticSeverity::warning)
                    codes.push_back(d.code);
            return codes;
        };
        const auto abs_native = inspect_configuration(dir / "pack" / "abs.cfg", native);
        const auto abs_portable = inspect_configuration(dir / "pack" / "abs.cfg", portable);
        require(abs_native.valid && abs_native.diagnostics.empty(), "native absolute path valid");
        require(abs_portable.valid &&
                    warnings(abs_portable) == std::vector<std::string>{"absolute-reference"} &&
                    abs_portable.diagnostics[0].location.line == 4,
                "absolute path is a located warning under portable-paths");
        const auto mixed = inspect_configuration(dir / "pack" / "mixed.cfg", portable);
        require(mixed.valid && mixed.diagnostics.empty(),
                "existing relative mixed-separator path needs no warning");
        const auto up = inspect_configuration(dir / "pack" / "up.cfg", portable);
        require(up.valid && warnings(up) == std::vector<std::string>{"outside-tree"} &&
                    inspect_configuration(dir / "pack" / "up.cfg", native).diagnostics.empty(),
                "'..' outside the configuration folder is a packaging warning only");
        for (const auto& options : {native, portable}) {
            const auto gone = inspect_configuration(dir / "pack" / "gone.cfg", options);
            require(!gone.valid && has_code(gone, "missing-asset"),
                    "missing IR is an error under either profile");
        }
        auto syntax = portable;
        syntax.syntax_only = true;
        require(warnings(inspect_configuration(dir / "pack" / "abs.cfg", syntax)) ==
                    std::vector<std::string>{"absolute-reference"},
                "syntax-only applies the profile");
#ifndef _WIN32
        wave(dir / "pack" / "a?b.wav", 48000, 1, {1});
        text_file(dir / "pack" / "q.cfg", "48000 1 1 0\n0\n0\na?b.wav\n0\n0\n0\n");
        require(warnings(inspect_configuration(dir / "pack" / "q.cfg", portable)) ==
                    std::vector<std::string>{"windows-invalid-name"},
                "names Windows cannot store are warned about");
#endif
        text_file(dir / "pack" / "case.cfg", "48000 1 1 0\n0\n0\nIRS/A.wav\n0\n0\n0\n");
        const auto cased = inspect_configuration(dir / "pack" / "case.cfg", portable);
        require(warnings(cased) == std::vector<std::string>{"case-mismatch"},
                "case differences from the stored names are warned about on every filesystem");
        text_file(dir / "pack" / "list.txt", "C.CFG\nabs.cfg\n");
        text_file(dir / "pack" / "c.cfg", "48000 1 1 0\n0\n0\nirs/a.wav\n0\n0\n0\n");
        const auto list = inspect_configuration(dir / "pack" / "list.txt", portable);
        require(has_code(list, "case-mismatch"), "list entries are checked too");
        fs::remove_all(dir / "pack");
    }
    // Inspection never plans FFTs or creates files.
    if (fft_supports_wisdom())
        require(export_fft_wisdom() == wisdom, "inspection changed FFTW wisdom");
    auto after = listing(dir);
    for (const auto& p : before)
        after.erase(p);
    for (const auto* name :
         {"absent.cfg", "two.txt",   "bad.cfg",     "unrelated.cfg", "list.txt",  "cyclic.txt",
          "broken.txt", "fails.txt", "absent2.cfg", "odd.cfg",       "drive.txt", "outer.txt",
          "sub.txt",    "dash.cfg",  "drc.pcm",     "cut.RAW",       "raw.cfg",   "cut.cfg",
          "rawch.cfg",  "wav.pcm",   "wavpcm.cfg",  "s24.pcm",       "s24.cfg",   "plain.wav"})
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
