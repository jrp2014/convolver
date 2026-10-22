// SPDX-License-Identifier: GPL-2.0-or-later
#include "harness.hpp"
#include "../native/oracle.hpp"
#include "convolver/io.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace convolver::plugin_test {
namespace {
struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Skipped : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void require(bool condition, const std::string& message) {
    if (!condition)
        throw Failure(message);
}

struct Fixture {
    std::string name;
    Filter filter;
    std::vector<float> input; // Interleaved.
    std::vector<float> cli;   // Interleaved CLI render: latency trimmed, full tail.
    std::size_t frames() const {
        return input.size() / filter.configuration.input_channels;
    }
};

std::vector<float> read_audio(const std::filesystem::path& path, std::size_t channels) {
    AudioReader reader(path);
    if (reader.info().channels != channels)
        throw std::runtime_error(path.string() + ": unexpected channel count");
    std::vector<float> result, chunk(4096 * channels);
    while (const auto frames = reader.read(chunk))
        result.insert(result.end(), chunk.begin(),
                      chunk.begin() + static_cast<std::ptrdiff_t>(frames * channels));
    return result;
}

std::vector<Fixture> load_fixtures(const std::filesystem::path& listing) {
    std::ifstream file(listing);
    if (!file)
        throw std::runtime_error(listing.string() +
                                 ": missing; run the examples.first_use test first");
    std::vector<Fixture> result;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty())
            continue;
        std::vector<std::string> fields;
        std::istringstream text(line);
        for (std::string field; std::getline(text, field, '\t');)
            fields.push_back(field);
        if (fields.size() != 4)
            throw std::runtime_error(listing.string() + ": malformed line: " + line);
        const auto path = [](const std::string& s) {
            return std::filesystem::path(std::u8string(s.begin(), s.end()));
        };
        Fixture f;
        f.name = fields[0];
        f.filter = {f.name, path(fields[1]), load_configuration(path(fields[1]))};
        const auto& c = f.filter.configuration;
        f.input = read_audio(path(fields[2]), c.input_channels);
        f.cli = read_audio(path(fields[3]), c.output_channels);
        result.push_back(std::move(f));
    }
    if (result.empty())
        throw std::runtime_error(listing.string() + ": no fixtures");
    return result;
}

// --- Filters the harness derives from a fixture ---------------------------------
Configuration identity(const Setup& s) {
    Configuration c{s.sample_rate, s.inputs, s.outputs, 0, {}, {}, {}};
    for (std::size_t i = 0; i < std::min(s.inputs, s.outputs); ++i)
        c.paths.push_back({{1}, {{i, 1}}, {{i, 1}}});
    return c;
}
// A different, equally long-or-longer response on every route: three frames
// later, inverted and halved.
Configuration replacement(Configuration c) {
    for (auto& p : c.paths) {
        p.impulse.insert(p.impulse.begin(), 3, 0.f);
        for (auto& v : p.impulse)
            v *= -.5f;
    }
    return c;
}
// One tap per route (its largest), so the tail is the routing delays alone.
Configuration shortened(Configuration c) {
    for (auto& p : c.paths) {
        auto peak = std::max_element(p.impulse.begin(), p.impulse.end(),
                                     [](float a, float b) { return std::abs(a) < std::abs(b); });
        p.impulse = {*peak};
    }
    return c;
}
Configuration wrong_rate(Configuration c) {
    c.sample_rate = c.sample_rate == 44100 ? 48000 : 44100;
    return c;
}
Configuration wrong_outputs(Configuration c) {
    ++c.output_channels;
    if (!c.output_delays.empty())
        c.output_delays.push_back(0);
    return c;
}
Configuration wrong_inputs(Configuration c) {
    ++c.input_channels;
    if (!c.input_delays.empty())
        c.input_delays.push_back(0);
    return c;
}
bool same(const Configuration& a, const Configuration& b) {
    const auto same_routes = [](const std::vector<Route>& x, const std::vector<Route>& y) {
        return std::equal(x.begin(), x.end(), y.begin(), y.end(), [](auto& r, auto& s) {
            return r.channel == s.channel && std::signbit(r.gain) == std::signbit(s.gain) &&
                   r.gain == s.gain;
        });
    };
    return a.sample_rate == b.sample_rate && a.input_channels == b.input_channels &&
           a.output_channels == b.output_channels && a.channel_mask == b.channel_mask &&
           a.input_delays == b.input_delays && a.output_delays == b.output_delays &&
           std::equal(a.paths.begin(), a.paths.end(), b.paths.begin(), b.paths.end(),
                      [&](auto& p, auto& q) {
                          return p.impulse == q.impulse && same_routes(p.inputs, q.inputs) &&
                                 same_routes(p.outputs, q.outputs);
                      });
}

// --- Driving an adapter ------------------------------------------------------------
double worst = 0; // Largest error against the oracle over the whole run.

struct Stream {
    Adapter& adapter;
    Setup setup;
    std::vector<std::size_t> pattern{300};
    bool in_place = false;
    std::size_t blocks = 0;

    // Process `frames` frames of `signal` (interleaved) starting at frame
    // `from`; frames past its end are silence. Returns interleaved output.
    std::vector<float> render(const std::vector<float>& signal, std::size_t from,
                              std::size_t frames, bool flag_silence = false) {
        const auto ni = setup.inputs, no = setup.outputs;
        const auto available = signal.size() / ni;
        std::vector<std::vector<float>> buffers(in_place ? std::max(ni, no) : ni + no,
                                                std::vector<float>(setup.max_block));
        std::vector<const float*> in(ni);
        std::vector<float*> out(no);
        for (std::size_t c = 0; c < ni; ++c)
            in[c] = buffers[c].data();
        for (std::size_t c = 0; c < no; ++c)
            out[c] = buffers[in_place ? c : ni + c].data();
        std::vector<float> result(frames * no);
        std::size_t done = 0;
        while (done < frames) {
            const auto n =
                std::min({pattern[blocks++ % pattern.size()], frames - done, setup.max_block});
            for (auto& b : buffers)
                std::fill_n(b.begin(), n, .75f); // Untouched output shows as an error.
            std::uint64_t silent = 0;
            for (std::size_t c = 0; c < ni; ++c) {
                bool all_zero = true;
                for (std::size_t t = 0; t < n; ++t) {
                    const auto frame = from + done + t;
                    const float v = frame < available ? signal[frame * ni + c] : 0.f;
                    buffers[c][t] = v;
                    all_zero = all_zero && v == 0;
                }
                if (flag_silence && all_zero)
                    silent |= std::uint64_t(1) << c;
            }
            require(adapter.process({in, out, n, silent}),
                    "process failed at frame " + std::to_string(from + done));
            if (auto flags = adapter.silent_outputs())
                for (std::size_t c = 0; c < no; ++c)
                    if (*flags & (std::uint64_t(1) << c))
                        for (std::size_t t = 0; t < n; ++t)
                            require(out[c][t] == 0, "output " + std::to_string(c) +
                                                        " flagged silent but not silent");
            for (std::size_t c = 0; c < no; ++c)
                for (std::size_t t = 0; t < n; ++t)
                    result[(done + t) * no + c] = out[c][t];
            done += n;
        }
        return result;
    }
};

// Compare interleaved output with interleaved double expectations, frame by frame.
void close(const std::vector<float>& actual, const std::vector<double>& expected,
           std::size_t channels, const std::string& what, std::size_t first_frame = 0) {
    const auto frames = actual.size() / channels;
    require(expected.size() >= actual.size(), what + ": reference too short");
    double local = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        require(std::isfinite(actual[i]), what + ": nonfinite output");
        const double error = std::abs(actual[i] - expected[i]);
        if (!(error <= tolerance)) {
            std::ostringstream m;
            m << what << ": frame " << first_frame + i / channels << " channel " << i % channels
              << " is " << actual[i] << ", expected " << expected[i] << " (of " << frames
              << " frames)";
            throw Failure(m.str());
        }
        local = std::max(local, error);
    }
    worst = std::max(worst, local);
}
std::vector<double> slice(const std::vector<double>& v, std::size_t channels, std::size_t from,
                          std::size_t frames) {
    std::vector<double> r(frames * channels);
    for (std::size_t i = 0; i < r.size() && from * channels + i < v.size(); ++i)
        r[i] = v[from * channels + i];
    return r;
}
void silent(const std::vector<float>& actual, const std::string& what) {
    for (std::size_t i = 0; i < actual.size(); ++i)
        require(actual[i] == 0, what + ": sample " + std::to_string(i) + " is " +
                                    std::to_string(actual[i]) + ", expected exact silence");
}

// --- The cases ------------------------------------------------------------------------
struct Context {
    Adapter& adapter;
    const Fixture& f;
    std::size_t max_block;
    const Configuration& c() const {
        return f.filter.configuration;
    }
    Setup setup() const {
        return {c().sample_rate, c().input_channels, c().output_channels, max_block};
    }
    // Frames to cover the input, the filter tail and the latency, then two
    // more blocks, which must be silent (the oracle is zero there).
    std::size_t span(const Configuration& x) const {
        return f.frames() + test::tail(x) + 3 * latency;
    }
    Stream start(const Configuration* filter, std::vector<std::size_t> pattern = {300},
                 bool in_place = false) const {
        adapter.create(setup());
        if (filter)
            require(
                adapter.load({filter == &c() ? f.name : "variant",
                              filter == &c() ? f.filter.source : std::filesystem::path{}, *filter}),
                "load rejected");
        require(adapter.activate(), "activation failed");
        return Stream{adapter, setup(), std::move(pattern), in_place};
    }
    void tail_is(std::size_t expected, const std::string& when) const {
        if (!adapter.reports_tail())
            return;
        const auto reported = adapter.tail_frames();
        require(reported == expected, "tail " + when + " is " + std::to_string(reported) +
                                          ", expected " + std::to_string(expected));
    }
};

// A fresh instance plays the fixture exactly like the CLI and within tolerance
// of the oracle, from the first frame, at any block size and buffer sharing.
void initial(const Context& x, std::vector<std::size_t> pattern, bool in_place) {
    const auto& c = x.c();
    auto s = x.start(&c, std::move(pattern), in_place);
    require(x.adapter.latency_frames() == latency,
            "latency " + std::to_string(x.adapter.latency_frames()) + ", expected 256");
    x.tail_is(test::tail(c), "of a fresh processor");
    const auto frames = x.span(c);
    const auto out = s.render(x.f.input, 0, frames);
    close(out, test::oracle(c, x.f.input, latency), c.output_channels, "against the oracle");
    const auto& cli = x.f.cli;
    require(cli.size() == (x.f.frames() + test::tail(c)) * c.output_channels,
            "the CLI render's length is not input plus tail");
    for (std::size_t i = 0; i < cli.size(); ++i)
        if (out[latency * c.output_channels + i] != cli[i]) {
            std::ostringstream m;
            m << "differs from the CLI render at frame " << i / c.output_channels << " channel "
              << i % c.output_channels << ": " << out[latency * c.output_channels + i] << " versus "
              << cli[i];
            throw Failure(m.str());
        }
    x.adapter.destroy();
}

// The contract's initial restoration: loading after activation but before any
// audio still installs the filter directly, without a crossfade.
void restore_after_activation(const Context& x) {
    const auto& c = x.c();
    auto s = x.start(nullptr);
    require(x.adapter.load(x.f.filter), "load rejected");
    close(s.render(x.f.input, 0, x.span(c)), test::oracle(c, x.f.input, latency), c.output_channels,
          "against the oracle");
    x.adapter.destroy();
}

// Live replacement: the new filter starts with fresh history, the old one plays
// on for 256 frames, then a 256-frame linear crossfade. Two loads before the
// next callback coalesce: only the newest is heard.
void live_replacement(const Context& x) {
    const auto& a = x.c();
    const auto b = replacement(a);
    const std::size_t at = 1200, no = a.output_channels;
    auto s = x.start(&a);
    auto out = s.render(x.f.input, 0, at);
    require(x.adapter.load({"superseded", {}, shortened(a)}), "first replacement rejected");
    require(x.adapter.load({"replacement", {}, b}), "replacement rejected");
    x.tail_is(std::max(test::tail(a), test::tail(b)), "after a replacement");
    const auto frames = x.span(b);
    const auto rest = s.render(x.f.input, at, frames - at);
    out.insert(out.end(), rest.begin(), rest.end());
    auto later = x.f.input; // The new filter hears only what arrives after the switch.
    std::fill_n(later.begin(), at * a.input_channels, 0.f);
    const auto old_wet = test::oracle(a, x.f.input, latency);
    const auto new_wet = test::oracle(b, later, latency);
    std::vector<double> expected(frames * no);
    for (std::size_t t = 0; t < frames; ++t) {
        double blend = 0;
        if (t >= at + 256)
            blend = static_cast<float>(std::min<std::size_t>(t - at - 255, 256)) / 256;
        for (std::size_t c = 0; c < no; ++c) {
            const auto i = t * no + c;
            const double o = i < old_wet.size() ? old_wet[i] : 0, n = new_wet[i];
            expected[i] = (1.0 - blend) * o + blend * n;
        }
    }
    close(out, expected, no, "through the replacement");
    x.adapter.destroy();
}

// The reported tail never shrinks during a session, and is exact again after
// reactivation, which installs the current filter afresh.
void tail_after_replacement(const Context& x) {
    const auto& a = x.c();
    const auto b = shortened(a);
    require(test::tail(b) < test::tail(a), "fixture too short to test tails");
    auto s = x.start(&a);
    s.render(x.f.input, 0, 600);
    require(x.adapter.load({"short", {}, b}), "short filter rejected");
    x.tail_is(test::tail(a), "after replacing long with short");
    s.render(x.f.input, 600, 1200);
    x.adapter.deactivate();
    require(x.adapter.activate(), "reactivation failed");
    x.tail_is(test::tail(b), "after reactivation");
    s.blocks = 0;
    close(s.render(x.f.input, 0, x.span(b)), test::oracle(b, x.f.input, latency), a.output_channels,
          "after reactivation");
    require(x.adapter.load(x.f.filter), "long filter rejected");
    x.tail_is(test::tail(a), "after replacing short with long");
    x.adapter.destroy();
}

// A rejected replacement leaves the running filter playing, uninterrupted, and
// the saved state describing it.
void failed_replacement(const Context& x) {
    const auto& a = x.c();
    auto s = x.start(&a);
    auto out = s.render(x.f.input, 0, 900);
    require(!x.adapter.load({"wrong rate", {}, wrong_rate(a)}), "wrong sample rate accepted");
    require(!x.adapter.load({"wrong inputs", {}, wrong_inputs(a)}), "wrong input count accepted");
    require(!x.adapter.load({"wrong outputs", {}, wrong_outputs(a)}),
            "wrong output count accepted");
    const auto saved = x.adapter.saved();
    require(saved && same(*saved, a), "saved state no longer describes the running filter");
    const auto rest = s.render(x.f.input, 900, x.span(a) - 900);
    out.insert(out.end(), rest.begin(), rest.end());
    close(out, test::oracle(a, x.f.input, latency), a.output_channels, "across the rejection");
    x.adapter.destroy();
}

// The same while inactive: the host's rate and layout are known, so the
// mismatch is rejected at once and the last valid filter survives.
void failed_replacement_inactive(const Context& x) {
    const auto& a = x.c();
    auto s = x.start(&a);
    s.render(x.f.input, 0, 600);
    x.adapter.deactivate();
    require(!x.adapter.load({"wrong rate", {}, wrong_rate(a)}),
            "wrong sample rate accepted while inactive");
    const auto saved = x.adapter.saved();
    require(saved && same(*saved, a), "saved state replaced by a rejected filter");
    require(x.adapter.activate(), "reactivation after a rejected filter failed");
    s.blocks = 0;
    close(s.render(x.f.input, 0, x.span(a)), test::oracle(a, x.f.input, latency), a.output_channels,
          "after the rejection");
    x.adapter.destroy();
}

// A first filter that cannot be used: rejected, and the instance plays
// silence (never the unfiltered input), with separate and shared buffers.
void failed_initial_load(const Context& x) {
    const auto& a = x.c();
    x.adapter.create(x.setup());
    require(!x.adapter.load({"wrong rate", {}, wrong_rate(a)}), "wrong sample rate accepted");
    require(!x.adapter.saved(), "a rejected first filter became the saved state");
    require(x.adapter.activate(), "activation after a failed load failed");
    Stream s{x.adapter, x.setup()};
    silent(s.render(x.f.input, 0, 1000), "after a failed first load");
    s.in_place = true;
    silent(s.render(x.f.input, 1000, 1000), "after a failed first load, in place");
    require(x.adapter.load(x.f.filter), "a valid filter after the failure was rejected");
    x.adapter.destroy();
}

// State restored before the host negotiates rate and layout is checked once it
// does: a valid filter is installed directly at activation.
void restore_before_setup(const Context& x) {
    const auto& a = x.c();
    if (!x.adapter.create_unconfigured())
        throw Skipped("the format always knows its configuration first");
    require(x.adapter.load(x.f.filter), "load before setup rejected");
    x.adapter.configure(x.setup());
    require(x.adapter.activate(), "activation failed");
    x.tail_is(test::tail(a), "of a fresh processor");
    Stream s{x.adapter, x.setup()};
    close(s.render(x.f.input, 0, x.span(a)), test::oracle(a, x.f.input, latency), a.output_channels,
          "against the oracle");
    x.adapter.destroy();
}

// Well-formed state for another rate, restored before setup, is kept (the
// host is restoring its project); activation at the wrong rate is a failed
// initial load (silence), and correcting the rate brings the filter back.
void restore_before_setup_mismatch(const Context& x) {
    const auto& a = x.c();
    const auto other = wrong_rate(a);
    if (!x.adapter.create_unconfigured())
        throw Skipped("the format always knows its configuration first");
    require(x.adapter.load({"other rate", {}, other}), "well-formed state before setup rejected");
    x.adapter.configure(x.setup());
    require(x.adapter.activate(), "activation after a mismatched restore failed");
    Stream s{x.adapter, x.setup()};
    silent(s.render(x.f.input, 0, 1000), "at the wrong rate");
    const auto saved = x.adapter.saved();
    require(saved && same(*saved, other), "the restored state was not kept");
    x.adapter.deactivate();
    auto corrected = x.setup();
    corrected.sample_rate = other.sample_rate;
    x.adapter.configure(corrected);
    require(x.adapter.activate(), "activation at the corrected rate failed");
    Stream t{x.adapter, corrected};
    close(t.render(x.f.input, 0, x.span(other)), test::oracle(other, x.f.input, latency),
          other.output_channels, "at the corrected rate");
    x.adapter.destroy();
}

// Nothing loaded: identity for equal counts, silence for unequal counts.
void empty_instance(const Context& x) {
    const auto setup = x.setup();
    auto s = x.start(nullptr);
    require(!x.adapter.saved(), "an empty instance reports a saved filter");
    x.tail_is(0, "of an empty instance");
    const auto out = s.render(x.f.input, 0, x.f.frames() + latency);
    if (setup.inputs == setup.outputs)
        close(out, test::oracle(identity(setup), x.f.input, latency), setup.outputs,
              "identity of an empty instance");
    else
        silent(out, "an empty instance with unequal counts");
    x.adapter.destroy();
}

// Bypass ramps over 64 samples to the input delayed by the reported latency
// (input c to output c), and back. With unequal counts bypass ramps every
// output to silence instead (owner's decision, 22 October 2026).
void bypass(const Context& x) {
    const auto& a = x.c();
    const auto ni = a.input_channels, no = a.output_channels;
    const std::size_t on = 900, off = 1800;
    auto s = x.start(&a);
    auto out = s.render(x.f.input, 0, on);
    x.adapter.set_bypass(true);
    auto more = s.render(x.f.input, on, off - on);
    out.insert(out.end(), more.begin(), more.end());
    x.adapter.set_bypass(false);
    const auto frames = x.span(a);
    more = s.render(x.f.input, off, frames - off);
    out.insert(out.end(), more.begin(), more.end());
    const auto wet = test::oracle(a, x.f.input, latency);
    std::vector<double> expected(frames * no);
    double mix = 0;
    for (std::size_t t = 0; t < frames; ++t) {
        if (t >= on)
            mix = std::clamp(mix + (t < off ? 1. / 64 : -1. / 64), 0., 1.);
        for (std::size_t c = 0; c < no; ++c) {
            const double dry = ni == no && t >= latency && t - latency < x.f.frames()
                                   ? x.f.input[(t - latency) * ni + c]
                                   : 0;
            expected[t * no + c] = (1 - mix) * wet[t * no + c] + mix * dry;
        }
    }
    close(out, expected, no, "through bypass");
    x.adapter.destroy();
}

// Reset clears all history: exact silence afterwards, then the filter plays
// the input again as if fresh.
void reset(const Context& x) {
    const auto& a = x.c();
    auto s = x.start(&a);
    s.render(x.f.input, 0, 1500);
    x.adapter.reset();
    silent(s.render({}, 0, x.span(a)), "after reset");
    x.adapter.reset();
    close(s.render(x.f.input, 0, x.span(a)), test::oracle(a, x.f.input, latency), a.output_channels,
          "after reset");
    x.adapter.destroy();
}

// Input flagged silent does not cut the filter's tail short.
void silence_flags(const Context& x) {
    const auto& a = x.c();
    auto s = x.start(&a);
    auto out = s.render(x.f.input, 0, x.f.frames());
    const auto rest = s.render({}, 0, x.span(a) - x.f.frames(), true);
    out.insert(out.end(), rest.begin(), rest.end());
    close(out, test::oracle(a, x.f.input, latency), a.output_channels, "with silent input");
    x.adapter.destroy();
}

// NaN and infinite input samples are treated as silence; output stays finite.
void nonfinite_input(const Context& x) {
    const auto& a = x.c();
    const auto ni = a.input_channels;
    auto bad = x.f.input, clean = x.f.input;
    const std::size_t places[][2] = {{1000, 0}, {2000, ni - 1}, {3000, 0}};
    const float values[] = {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()};
    for (std::size_t k = 0; k < 3; ++k) {
        bad[places[k][0] * ni + places[k][1]] = values[k];
        clean[places[k][0] * ni + places[k][1]] = 0;
    }
    auto s = x.start(&a);
    close(s.render(bad, 0, x.span(a)), test::oracle(a, clean, latency), a.output_channels,
          "with nonfinite input");
    x.adapter.destroy();
}
} // namespace

int run(Adapter& adapter, const std::filesystem::path& listing, const Options& options) {
    std::vector<Fixture> fixtures;
    try {
        fixtures = load_fixtures(listing);
    } catch (const std::exception& e) {
        std::cerr << "cannot load fixtures: " << e.what() << '\n';
        return 1;
    }
    std::size_t passed = 0, deviations = 0, failed = 0, skipped = 0;
    std::vector<std::string> seen;
    const auto check = [&](const std::string& name, const std::function<void()>& body) {
        seen.push_back(name);
        const auto known =
            std::find_if(options.known_deviations.begin(), options.known_deviations.end(),
                         [&](const Deviation& d) { return d.case_name == name; });
        std::string problem;
        try {
            body();
        } catch (const Skipped& e) {
            ++skipped;
            std::cout << "skip  " << name << ": " << e.what() << '\n';
            adapter.destroy();
            return;
        } catch (const std::exception& e) {
            problem = e.what();
            try {
                adapter.destroy(); // A failed case may leave an instance behind.
            } catch (...) {
            }
        }
        if (problem.empty() && known == options.known_deviations.end()) {
            ++passed;
            std::cout << "pass  " << name << '\n';
        } else if (problem.empty()) {
            ++failed;
            std::cout << "FAIL  " << name << ": listed as a known deviation but now passes; "
                      << "remove it from the list (" << known->reason << ")\n";
        } else if (known != options.known_deviations.end() &&
                   problem.find(known->expected_problem) == std::string::npos) {
            ++failed;
            std::cout << "FAIL  " << name << ": " << problem << " (listed as a known deviation "
                      << "failing with \"" << known->expected_problem << "\")\n";
        } else if (known != options.known_deviations.end()) {
            ++deviations;
            std::cout << "known " << name << ": " << problem << " [" << known->reason << "]\n";
        } else {
            ++failed;
            std::cout << "FAIL  " << name << ": " << problem << '\n';
        }
    };
    for (const auto& f : fixtures) {
        const Context x{adapter, f, options.max_block};
        const auto at = [&](const std::string& c) { return c + "[" + f.name + "]"; };
        for (const std::size_t block :
             {std::size_t(1), std::size_t(61), std::size_t(300), options.max_block})
            for (const bool shared : {false, true})
                check(at("initial") + "[block " + std::to_string(block) +
                          (shared ? ", in place]" : "]"),
                      [&] { initial(x, {block}, shared); });
        check(at("initial") + "[variable blocks]",
              [&] { initial(x, {1, 61, 300, options.max_block, 17, 256, 255}, false); });
        check(at("restore-after-activation"), [&] { restore_after_activation(x); });
        check(at("restore-before-setup"), [&] { restore_before_setup(x); });
        check(at("restore-before-setup-mismatch"), [&] { restore_before_setup_mismatch(x); });
        check(at("live-replacement"), [&] { live_replacement(x); });
        check(at("tail-after-replacement"), [&] { tail_after_replacement(x); });
        check(at("failed-replacement"), [&] { failed_replacement(x); });
        check(at("failed-replacement-inactive"), [&] { failed_replacement_inactive(x); });
        check(at("failed-initial-load"), [&] { failed_initial_load(x); });
        check(at("empty-instance"), [&] { empty_instance(x); });
        check(at("bypass"), [&] { bypass(x); });
        check(at("reset"), [&] { reset(x); });
        check(at("silence-flags"), [&] { silence_flags(x); });
        check(at("nonfinite-input"), [&] { nonfinite_input(x); });
    }
    for (const auto& d : options.known_deviations)
        if (std::find(seen.begin(), seen.end(), d.case_name) == seen.end()) {
            ++failed;
            std::cout << "FAIL  known deviation names no case: " << d.case_name << '\n';
        }
    std::cout << adapter.format() << " against the PLUG-2 harness: " << passed << " passed, "
              << deviations << " known deviations, " << skipped << " skipped, " << failed
              << " failed; worst error against the double-precision oracle " << worst << '\n';
    return failed ? 1 : 0;
}
} // namespace convolver::plugin_test
