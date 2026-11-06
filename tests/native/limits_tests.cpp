// SPDX-License-Identifier: GPL-2.0-or-later
// Deterministic boundary tests for configuration, list, IR and host-state limits:
// each limit is accepted at its value and rejected just beyond it, using the
// production constants and without allocating near the decoded-sample limit.
#include "config_parse.hpp"
#include "convolver/host.hpp"
#include "convolver/inspection.hpp"
#include "convolver/io.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

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
// The message of the expected rejection, or failure if f succeeds.
template <class F> std::string rejection(F f, const std::string& what) {
    try {
        f();
    } catch (const std::exception& e) {
        return e.what();
    }
    throw std::runtime_error("expected rejection: " + what);
}
bool contains(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

// A mono 8-bit WAV of silence with the given (even) frame count, written
// sparsely where the filesystem allows.
void silent_u8_wave(const fs::path& path, std::size_t frames) {
    unsigned char header[44] = {'R', 'I', 'F',  'F',  0,   0,   0,    0,    'W', 'A', 'V',
                                'E', 'f', 'm',  't',  ' ', 16,  0,    0,    0,   1,   0,
                                1,   0,   0x80, 0xbb, 0,   0,   0x80, 0xbb, 0,   0,   1,
                                0,   8,   0,    'd',  'a', 't', 'a',  0,    0,   0,   0};
    const auto data = static_cast<std::uint32_t>(frames);
    const std::uint32_t riff = 36 + data;
    for (int i = 0; i < 4; ++i) {
        header[4 + i] = static_cast<unsigned char>(riff >> (8 * i));
        header[40 + i] = static_cast<unsigned char>(data >> (8 * i));
    }
    text_file(path, std::string(reinterpret_cast<const char*>(header), sizeof header));
    fs::resize_file(path, sizeof header + frames);
}

// Canonicalization failures must use the same located error path on every OS,
// including Windows runners without symlink privileges. Only the OS operation
// is substituted; traversal, parsing and continuation use production code.
void canonical_errors(const fs::path& dir) {
    using namespace detail;
    text_file(dir / "canonical-list.txt", "bad.cfg\ngood.cfg\n");
    text_file(dir / "good.cfg", "48000 1 1 0\n0\n0\nir.wav\n0\n0\n0\n");
    for (const auto* failing : {"bad.cfg", "ir.wav"}) {
        auto files = Resolver::from(libsndfile_files());
        files.canonical = [&](const fs::path& p) {
            if (p.filename() == failing)
                throw fs::filesystem_error("canonical", p,
                                           std::make_error_code(std::errc::permission_denied));
            return p;
        };
        std::vector<ConfigurationError> errors;
        std::vector<fs::path> visited;
        Resolver resolver(
            [&](const Resolver::Leaf& leaf) {
                visited.push_back(leaf.path);
                return false;
            },
            [&](const ConfigurationError& e) {
                errors.push_back(e);
                return true;
            },
            {}, files);
        resolver.run(dir / "canonical-list.txt");
        if (std::string_view(failing) == "bad.cfg") {
            require(visited == std::vector<fs::path>{dir / "good.cfg"},
                    "canonical error must allow the valid sibling");
            require(errors.size() == 1 && errors[0].code() == "unreadable" &&
                        errors[0].location().file == dir / "bad.cfg" &&
                        errors[0].include_chain().size() == 1 &&
                        errors[0].include_chain()[0].line == 1,
                    "canonical error must carry its file and include chain");
        } else {
            require(visited.empty() && errors.size() == 2 &&
                        errors[1].location().file == dir / "good.cfg" &&
                        errors[1].location().line == 4 && errors[1].code() == "unreadable",
                    "IR canonical error must carry the reference location");
        }
    }
    auto files = Resolver::from(libsndfile_files());
    files.canonical = [](const fs::path&) -> fs::path { throw std::bad_alloc(); };
    Resolver resolver([](const Resolver::Leaf&) { return false; },
                      [](const ConfigurationError&) { return true; }, {}, files);
    bool exhausted = false;
    try {
        resolver.run(dir / "canonical-list.txt");
    } catch (const std::bad_alloc&) {
        exhausted = true;
    }
    require(exhausted, "resource exhaustion must not become an input diagnostic");
}

void run(const fs::path& dir) {
    using namespace detail;
    canonical_errors(dir);
    {
        AudioWriter w(dir / "ir.wav", {48000, 1}, OutputFormat::wav_float);
        const std::vector<float> one{1};
        w.write(one);
        w.close();
    }
    const std::string cfg = "48000 1 1 0\n0\n0\nir.wav\n0\n0\n0\n";
    const std::string skip = "44100 1 1 0\n0\n0\nnot-installed.wav\n0\n0\n0\n";
    text_file(dir / "match.cfg", cfg);
    text_file(dir / "skip.cfg", skip);

    // 1 MiB per text file.
    {
        std::string exact = cfg + "//";
        exact.resize(max_text_bytes, 'x');
        require(parse_configuration_text(exact, "t").configuration.paths.size() == 1,
                "text of exactly 1 MiB is accepted");
        rejection([&] { parse_configuration_text(exact + "x", "t"); }, "1 MiB + 1 byte");
    }
    // 16 active nesting levels: 15 lists then a configuration load; one more fails.
    for (const std::size_t depth : {max_list_depth, max_list_depth + 1}) {
        for (std::size_t i = 0; i + 1 < depth; ++i)
            text_file(dir / ("nest" + std::to_string(i) + ".txt"),
                      i + 2 < depth ? "nest" + std::to_string(i + 1) + ".txt\n" : "match.cfg\n");
        const auto load = [&] { load_configuration(dir / "nest0.txt"); };
        if (depth == max_list_depth)
            load();
        else
            require(contains(rejection(load, "17 nesting levels"), "nesting/entry limit"),
                    "nesting limit diagnostic");
    }
    // 4096 visits per resolution, counting the list itself.
    for (const std::size_t entries : {max_list_visits - 1, max_list_visits}) {
        std::string list;
        for (std::size_t i = 0; i + 1 < entries; ++i)
            list += "skip.cfg\n";
        list += "match.cfg\n";
        text_file(dir / "visits.txt", list);
        const auto load = [&] { return load_configuration(dir / "visits.txt", 48000); };
        if (entries < max_list_visits)
            require(load().sample_rate == 48000, "4096 visits are accepted");
        else
            require(contains(rejection(load, "4097 visits"), "nesting/entry limit"),
                    "visit limit diagnostic");
    }
    // 16 Mi frames per delay, 64 Mi delay frames in total.
    {
        const auto delays = [](int rate, const std::string& line, std::size_t channels) {
            std::string zeros;
            for (std::size_t c = 0; c < channels; ++c)
                zeros += c ? " 0" : "0";
            return std::to_string(rate) + " " + std::to_string(channels) + " 1 0\n" + line +
                   "\n0\nir.wav\n0\n" + zeros + "\n0\n";
        };
        const auto at_limit = parse_configuration_text(delays(2000, "8388608", 1), "t");
        require(at_limit.configuration.input_delays[0].frames == max_delay_frames,
                "a delay of exactly 16 Mi frames is accepted");
        rejection([&] { parse_configuration_text(delays(2000, "8388609", 1), "t"); },
                  "delay beyond 16 Mi frames");
        parse_configuration_text(delays(2000, "8388608 8388608 8388608 8388608", 4), "t");
        require(contains(rejection(
                             [&] {
                                 parse_configuration_text(
                                     delays(2000, "8388608 8388608 8388608 8388608 1", 5), "t");
                             },
                             "delay storage beyond 64 Mi frames"),
                         "total delay storage"),
                "total delay storage diagnostic");
    }
    // 64 Mi decoded samples: a file declaring two samples more is rejected from its
    // header, before any sample buffer is allocated. (Exactly 64 Mi would decode
    // 256 MiB, so the accepted side is covered by the identical comparison.)
    {
        // An even data size needs no RIFF pad byte.
        const auto frames = max_decoded_samples + 2;
        const auto big = dir / "declared-huge.wav";
        silent_u8_wave(big, frames);
        const auto started = std::chrono::steady_clock::now();
        require(contains(rejection([&] { load_configuration(big); }, "64 Mi + 2 samples"),
                         "64 Mi samples"),
                "decoded sample limit diagnostic");
        require(std::chrono::steady_clock::now() - started < std::chrono::seconds(5),
                "oversized IR rejected before decoding");
        fs::remove(big);
    }
    // 16 MiB of configuration text per resolution; shared sublists count each read.
    {
        std::string large = skip + "//";
        large.resize(1000000, 'x');
        for (int i = 0; i < 17; ++i)
            text_file(dir / ("large" + std::to_string(i) + ".cfg"), large);
        std::string sixteen, seventeen, shared;
        for (int i = 0; i < 16; ++i)
            sixteen += "large" + std::to_string(i) + ".cfg\n";
        seventeen = sixteen + "large16.cfg\n";
        for (int i = 0; i < 17; ++i)
            shared += "large0.cfg\n";
        text_file(dir / "sixteen.txt", sixteen + "match.cfg\n");
        text_file(dir / "seventeen.txt", seventeen + "match.cfg\n");
        text_file(dir / "shared.txt", shared + "match.cfg\n");
        require(load_configuration(dir / "sixteen.txt", 48000).sample_rate == 48000,
                "16 MB of text is accepted");
        for (const auto* name : {"seventeen.txt", "shared.txt"})
            require(contains(rejection([&] { load_configuration(dir / name, 48000); }, name),
                             "exceeds 16 MiB"),
                    "aggregate text limit diagnostic");
        InspectionOptions all;
        all.all_configurations = true;
        const auto report = inspect_configuration(dir / "shared.txt", all);
        require(!report.valid && report.diagnostics.back().code == "limit",
                "all-entry inspection shares the text budget");
    }
    // Each entry validated with --all-configurations gets the decode budget a
    // render would: two entries sharing an IR of more than half the limit pass.
    {
        silent_u8_wave(dir / "half.wav", max_decoded_samples / 2 + 2);
        text_file(dir / "half1.cfg", "48000 1 1 0\n0\n0\nhalf.wav\n0\n0\n0\n");
        text_file(dir / "half2.cfg", "48000 2 2 0\n0 0\n0 0\nhalf.wav\n0\n0\n0\n");
        text_file(dir / "halves.txt", "half1.cfg\nhalf2.cfg\n");
        InspectionOptions all;
        all.all_configurations = true;
        const auto report = inspect_configuration(dir / "halves.txt", all);
        require(report.valid && report.configurations.size() == 2,
                "the decode budget applies per validated entry");
        // One configuration naming the file twice decodes it once (PERF-6) but
        // charges both references, so it is still rejected, at the second.
        text_file(dir / "twice.cfg",
                  "48000 2 2 0\n0 0\n0 0\nhalf.wav\n0\n0\n0\nhalf.wav\n0\n1\n1\n");
        require(contains(rejection([&] { load_configuration(dir / "twice.cfg"); }, "twice.cfg"),
                         "twice.cfg:8:1: decoded impulse data exceeds 64 Mi samples"),
                "a file named twice is charged twice");
        fs::remove(dir / "half.wav");
        // Three references to a file of a third of the limit: the second fits
        // in what the first decode left, so only charging each reference
        // rejects the third, as the loader did when it decoded per reference.
        silent_u8_wave(dir / "third.wav", max_decoded_samples / 3 + 3); // Even: no pad byte.
        text_file(dir / "thirds.cfg", "48000 3 3 0\n0 0 0\n0 0 0\n"
                                      "third.wav\n0\n0\n0\nthird.wav\n0\n1\n1\n"
                                      "third.wav\n0\n2\n2\n");
        require(contains(rejection([&] { load_configuration(dir / "thirds.cfg"); }, "thirds.cfg"),
                         "thirds.cfg:12:1: decoded impulse data exceeds 64 Mi samples"),
                "every reference to a shared file is charged");
        fs::remove(dir / "third.wav");
    }
    // At most 256 diagnostics, then one truncation marker.
    {
        std::string list;
        for (int i = 0; i < 300; ++i)
            list += "absent" + std::to_string(i) + ".cfg\n";
        text_file(dir / "many.txt", list);
        InspectionOptions all;
        all.all_configurations = true;
        const auto report = inspect_configuration(dir / "many.txt", all);
        require(report.diagnostics.size() == 257 &&
                    report.diagnostics.back().code == "diagnostics-truncated" &&
                    contains(report.diagnostics.back().message, "44 further"),
                "diagnostic cap and marker");
        require(!report.valid, "dropped errors still invalidate the report");
    }
    // ROBUST-2: one --all-configurations check stops, with one error, before
    // the next entry once the entries checked have decoded the sample limit,
    // or before an entry that would take the reported paths past theirs; the
    // production limits are 1 Gi samples and 16384 paths. Each match.cfg
    // entry decodes one sample and reports one path.
    {
        std::string list;
        for (int i = 0; i < 5; ++i)
            list += "match.cfg\n";
        text_file(dir / "five.txt", list);
        InspectionOptions all;
        all.all_configurations = true;
        static_assert(max_inspected_samples == 16 * max_decoded_samples &&
                      max_inspected_paths == 4 * max_filter_paths &&
                      max_inspected_routes == 32 * max_routes);
        require(InspectionLimits{}.samples == max_inspected_samples &&
                    InspectionLimits{}.paths == max_inspected_paths &&
                    InspectionLimits{}.routes == max_inspected_routes,
                "the default inspection limits are the production ones");
        for (const auto& [limits, what] :
             {std::pair{InspectionLimits{4, max_inspected_paths}, "decoded"},
              std::pair{InspectionLimits{max_inspected_samples, 4}, "past 4"}}) {
            const auto report = detail::inspect_configuration(dir / "five.txt", all, limits);
            require(!report.valid && report.configurations.size() == 4 &&
                        report.traversal.size() == 6 && report.traversal.back().outcome == "failed",
                    std::string("an all-entry check stops at the fifth entry: ") + what);
            const auto& d = report.diagnostics.back();
            require(report.diagnostics.size() == 1 && d.code == "limit" &&
                        contains(d.message, what) && contains(d.message, "smaller lists") &&
                        d.include_chain.size() == 1 && d.include_chain[0].line == 5,
                    std::string("one located limit diagnostic: ") + what);
            require(contains(report.selection_reason, "until a limit"),
                    "the selection reason says the check stopped");
        }
        text_file(dir / "four.txt", "match.cfg\nmatch.cfg\nmatch.cfg\nmatch.cfg\n");
        for (const auto& limits :
             {InspectionLimits{4, max_inspected_paths}, InspectionLimits{max_inspected_samples, 4}})
            require(detail::inspect_configuration(dir / "four.txt", all, limits).valid,
                    "an all-entry check reaching a limit exactly passes");
        // The entry that crosses the sample limit is checked in full: with
        // three samples an entry and a limit of 4, the second runs (6
        // samples) and the third is stopped.
        {
            AudioWriter w(dir / "ir3.wav", {48000, 1}, OutputFormat::wav_float);
            const std::vector<float> three{1, 0.5F, 0.25F};
            w.write(three);
            w.close();
        }
        text_file(dir / "three.cfg", "48000 1 1 0\n0\n0\nir3.wav\n0\n0\n0\n");
        text_file(dir / "threes.txt", "three.cfg\nthree.cfg\nthree.cfg\n");
        const auto crossed =
            detail::inspect_configuration(dir / "threes.txt", all, {4, max_inspected_paths});
        require(!crossed.valid && crossed.configurations.size() == 2 &&
                    contains(crossed.diagnostics.back().message, "decoded 6 IR samples"),
                "the entry that crosses the sample limit is checked, the next stopped");
        // Routes count on their own (jrp's review of 7b4cb7a): one path can
        // have thousands, so a list of one-path entries with many routes made a
        // large report within the path limit. Each wide.cfg entry has one path
        // and five routes (one input, four outputs).
        text_file(dir / "wide.cfg", "48000 1 4 0\n0\n0 0 0 0\nir.wav\n0\n0\n0 1 2 3\n");
        text_file(dir / "wides.txt", "wide.cfg\nwide.cfg\nwide.cfg\nwide.cfg\nwide.cfg\n");
        const InspectionLimits twenty{max_inspected_samples, max_inspected_paths, 20};
        const auto wide = detail::inspect_configuration(dir / "wides.txt", all, twenty);
        require(!wide.valid && wide.configurations.size() == 4 && wide.diagnostics.size() == 1 &&
                    wide.diagnostics.back().code == "limit" &&
                    contains(wide.diagnostics.back().message, "routes reported past 20") &&
                    wide.diagnostics.back().include_chain[0].line == 5,
                "an all-entry check stops before the entry past the route limit");
        text_file(dir / "wides4.txt", "wide.cfg\nwide.cfg\nwide.cfg\nwide.cfg\n");
        require(detail::inspect_configuration(dir / "wides4.txt", all, twenty).valid,
                "an all-entry check reaching the route limit exactly passes");
        // The limit error survives the 256-diagnostic cap.
        std::string noisy;
        for (int i = 0; i < 300; ++i)
            noisy += "absent.cfg\n";
        noisy += "wide.cfg\nwide.cfg\n";
        text_file(dir / "noisy.txt", noisy);
        const auto capped = detail::inspect_configuration(
            dir / "noisy.txt", all, {max_inspected_samples, max_inspected_paths, 5});
        const auto has = [&](const char* code) {
            return std::any_of(capped.diagnostics.begin(), capped.diagnostics.end(),
                               [&](const auto& d) { return d.code == code; });
        };
        require(!capped.valid && capped.configurations.size() == 1 &&
                    has("diagnostics-truncated") && has("limit") &&
                    std::any_of(capped.diagnostics.begin(), capped.diagnostics.end(),
                                [](const auto& d) { return d.code == "limit"; }),
                "the limit error is kept past the diagnostic cap");
    }
    // ROBUST-2: a no-match message names at most ten of a list's rates.
    {
        std::string list;
        for (int i = 0; i < 12; ++i) {
            const auto name = "rate" + std::to_string(i) + ".cfg";
            text_file(dir / name, std::to_string(1000 + i) + " 1 1 0\n0\n0\nabsent.wav\n0\n0\n0\n");
            list += name + "\n";
        }
        text_file(dir / "rates.txt", list);
        const auto message =
            rejection([&] { load_configuration(dir / "rates.txt", 48000); }, "no rate matches");
        require(contains(message, "1000 Hz, 1001 Hz") &&
                    contains(message, "1009 Hz or one of "
                                      "the list's 2 other "
                                      "rates") &&
                    !contains(message, "1010 Hz"),
                "the rates named in a no-match message are bounded: " + message);
        list.erase(list.rfind("rate11.cfg"));
        text_file(dir / "rates11.txt", list);
        const auto eleven =
            rejection([&] { load_configuration(dir / "rates11.txt", 48000); }, "eleven rates");
        require(contains(eleven, "1009 Hz or the list's one other rate"),
                "one rate beyond the ten named is singular: " + eleven);
    }
    // 64 MiB of encoded host state.
    {
        std::vector<std::byte> state(max_host_state_bytes + 1);
        rejection([&] { decode_host_state(state); }, "host state beyond 64 MiB");
        state.resize(max_host_state_bytes);
        const auto message = rejection([&] { decode_host_state(state); }, "zeroed 64 MiB state");
        require(!contains(message, "exceeds limit"), "64 MiB state passes the size check");
    }
}
} // namespace

int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-limits-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
        run(dir);
        fs::remove_all(dir);
        std::cout << "limit tests passed\n";
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
