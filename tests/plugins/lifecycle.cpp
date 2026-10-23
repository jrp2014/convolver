// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-7: the reference model and the sequence runner (lifecycle.hpp).
#include "lifecycle.hpp"
#include "../native/oracle.hpp"
#include "audio_guard.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

namespace convolver::plugin_test::lifecycle {
namespace {
constexpr std::size_t latency = 256;    // The contract's reported latency.
constexpr std::size_t transition = 512; // Warm-up and crossfade, 256 frames each.
constexpr std::size_t ramp = 64;        // Bypass and mute ramps.
constexpr double tolerance = 1e-5;      // PLUG-2's tolerance against the oracle.

constexpr std::array<std::pair<Op, const char*>, 24> names{{
    {Op::state_a, "state-A"},
    {Op::state_b, "state-B"},
    {Op::state_other_rate, "state-other-rate"},
    {Op::state_malformed, "state-malformed"},
    {Op::state_empty, "state-empty"},
    {Op::state_failed, "state-failed"},
    {Op::state_b_starved, "state-B-starved"},
    {Op::state_b_shortage, "state-B-shortage"},
    {Op::play, "play"},
    {Op::bypass, "bypass"},
    {Op::reset, "reset"},
    {Op::stop, "stop"},
    {Op::start, "start"},
    {Op::reopen, "reopen"},
    {Op::rate, "rate"},
    {Op::layout, "layout"},
    {Op::setup, "setup"},
    {Op::listen, "listen"},
    {Op::empty_map, "empty-map"},
    {Op::file_a, "file-A"},
    {Op::file_missing, "file-missing"},
    {Op::file_refused, "file-refused"},
    {Op::work, "work"},
    {Op::overtake, "overtake"},
}};

// A check that failed; the runner adds the sequence.
class Mismatch : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
void expect(bool condition, const std::string& what) {
    if (!condition)
        throw Mismatch(what);
}

const char* kind_name(HostFailureKind k) {
    switch (k) {
    case HostFailureKind::filter_file:
        return "filter_file";
    case HostFailureKind::state_unreadable:
        return "state_unreadable";
    case HostFailureKind::state_newer:
        return "state_newer";
    case HostFailureKind::sample_rate:
        return "sample_rate";
    case HostFailureKind::channels:
        return "channels";
    case HostFailureKind::preparation:
        return "preparation";
    case HostFailureKind::activation:
        return "activation";
    case HostFailureKind::saved_failure:
        return "saved_failure";
    }
    return "?";
}

// --- The reference model ------------------------------------------------------
// Plain data, changed by one function per operation. Comments cite where each
// rule comes from: "contract" is docs/develop/plugin-contract.md.

enum class Which { a, b, other_rate };
struct Failure {
    HostFailureKind kind;
    std::uint64_t request;
    bool silent;
};
struct Job {
    std::uint64_t generation;
    bool exists;
};
struct Model {
    // The host.
    Host host;
    bool configured = true, active = false;
    bool rendered = false; // AU: this initialization has rendered audio.
    // Contract, replacement and threads: audio has been processed since the
    // last activation or reset; until then a filter is installed directly.
    bool started = false;
    bool settled_bypass = false;   // Bypass as of that activation or reset (no ramp).
    std::optional<Host> committed; // Known while stopped (Known::activation, rendering).
    bool bypass = false;
    // What the instance keeps and saves.
    std::optional<Which> filter;
    bool failed = false; // A failed first load: saved as state version 2.
    bool path = false;   // LV2: the saved filter came from A's file.
    // PLUG-8: request numbers and the failure record.
    std::uint64_t requests = 0, accepted = 0;
    std::optional<Failure> load_failure, activation_failure;
    // LV2: requests queued for the worker, oldest first.
    std::vector<Job> jobs;
    // After a load that ran out of memory the drivers' request numbers are
    // not predictable (each attempt is a request).
    bool numbers_unknown = false;
};

struct Context {
    const Format& format;
    const Filters& filters;
};

const Configuration& configuration(const Filters& f, Which w) {
    return w == Which::a ? f.a : w == Which::b ? f.b : f.other_rate_filter;
}
// Contract, loading and failure: the checks against the host are its sample
// rate and its channel counts.
std::optional<HostFailureKind> mismatch(const Configuration& c, const Host& h) {
    if (c.sample_rate != h.sample_rate)
        return HostFailureKind::sample_rate;
    if (c.input_channels != h.inputs || c.output_channels != h.outputs)
        return HostFailureKind::channels;
    return std::nullopt;
}
// Contract, "state that arrives before the host is configured"; the format
// notes say when each format counts its configuration as known.
bool known(const Context& x, const Model& m) {
    if (!m.configured)
        return false;
    switch (x.format.known) {
    case Known::always:
        return true;
    case Known::activation:
    case Known::rendering:
        return m.active || m.committed == m.host;
    }
    return false;
}
// Contract, failure reasons: a rejected request's reason, silent when nothing
// was accepted; HostFilter::failed() after a failed first load.
void reject(Model& m, HostFailureKind kind, std::uint64_t request) {
    m.load_failure = Failure{kind, request, !m.filter};
    m.failed = !m.filter;
}
// Contract, failure reasons: success clears the reason, and restoring a saved
// failed load is a successful request whose instance plays silence.
void accept(const Context& x, Model& m, std::optional<Which> filter, bool failed,
            std::uint64_t request, bool path) {
    m.filter = filter;
    m.failed = failed;
    m.path = path;
    m.accepted = request;
    m.load_failure.reset();
    if (failed)
        m.load_failure = Failure{HostFailureKind::saved_failure, request, true};
    m.activation_failure.reset();
    if (x.format.state_bypass)
        m.bypass = false; // Contract, bypass: VST3 saves and restores it with the state.
}
// Contract, loading and failure: checks on the state itself at once; checks
// against the host at once when it is known, otherwise at activation.
void restore(const Context& x, Model& m, std::optional<Which> filter, bool failed, bool malformed) {
    const auto request = ++m.requests;
    if (malformed)
        return reject(m, HostFailureKind::state_unreadable, request);
    if (filter && known(x, m))
        if (auto kind = mismatch(configuration(x.filters, *filter), m.host))
            return reject(m, *kind, request);
    accept(x, m, filter, failed, request, false);
}
// Contract, failure reasons: a mismatch found at activation belongs to the
// request that supplied the filter, comes first, and clears at the next
// activation that suits the filter.
void start(const Context& x, Model& m) {
    m.active = true;
    m.rendered = false;
    m.started = false;
    m.settled_bypass = m.bypass;
    m.activation_failure.reset();
    if (m.filter)
        if (auto kind = mismatch(configuration(x.filters, *m.filter), m.host))
            m.activation_failure = Failure{*kind, m.accepted, true};
    if (x.format.known == Known::activation)
        m.committed = m.host;
}
void stop(const Context& x, Model& m) {
    m.active = false;
    if (x.format.known == Known::rendering && m.rendered)
        m.committed = m.host;
}
// LV2 worker (plan, worker and state lifecycle, "requests that never reach
// work() still get an answer"): a request run() cannot queue fails, and is
// recorded at the next non-real-time call unless a newer request is.
void refused(Model& m, std::uint64_t generation) {
    reject(m, HostFailureKind::filter_file, generation);
}

void apply(const Context& x, Model& m, Op op) {
    const auto& f = x.filters;
    switch (op) {
    case Op::state_a:
        return restore(x, m, Which::a, false, false);
    case Op::state_b:
        return restore(x, m, Which::b, false, false);
    case Op::state_b_starved:
    case Op::state_b_shortage:
        restore(x, m, Which::b, false, false);
        m.numbers_unknown = true;
        return;
    case Op::state_other_rate:
        return restore(x, m, Which::other_rate, false, false);
    case Op::state_malformed:
        return restore(x, m, std::nullopt, false, true);
    case Op::state_empty:
        return restore(x, m, std::nullopt, false, false);
    case Op::state_failed:
        return restore(x, m, std::nullopt, true, false);
    case Op::empty_map:
        // LV2 state: "This allows the host to reset the plugin state with an
        // empty map"; contract, format notes (LV2): no filter, no failure, no
        // path, and loads still outstanding are superseded.
        return accept(x, m, std::nullopt, false, ++m.requests, false);
    case Op::play:
    case Op::listen:
        m.rendered = m.rendered || m.active;
        m.started = m.started || m.active;
        return;
    case Op::bypass:
        m.bypass = !m.bypass;
        return;
    case Op::reset:
        m.started = false;
        m.settled_bypass = m.bypass;
        return;
    case Op::stop:
        return stop(x, m);
    case Op::start:
        return start(x, m);
    case Op::rate:
        m.host.sample_rate =
            m.host.sample_rate == f.host.sample_rate ? f.other_rate : f.host.sample_rate;
        return;
    case Op::layout:
        m.host.outputs = m.host.outputs == f.host.outputs ? f.other_outputs : f.host.outputs;
        return;
    case Op::setup:
        m.configured = true;
        m.host = f.host;
        return;
    case Op::reopen: {
        // Contract, host state: the saved state round-trips (a filter, an
        // empty instance or a saved failed load); bypass returns with the
        // session (VST3's state, the host in LV2 and AU). LV2 saves the file's
        // path too. A new instance numbers its requests afresh; restored
        // before the host negotiates, its configuration is not known yet.
        const bool was_active = m.active;
        Model next;
        next.host = m.host;
        next.bypass = m.bypass;
        accept(x, next, m.filter, m.failed, ++next.requests, m.path);
        next.bypass = m.bypass;
        m = next;
        if (was_active)
            start(x, m);
        return;
    }
    case Op::file_a:
    case Op::file_missing:
    case Op::file_refused: {
        // Plan, LV2 adapter design: a patch:Set is a request numbered by
        // generation, queued for the worker.
        const auto generation = ++m.requests;
        if (op == Op::file_refused || !x.format.worker)
            return refused(m, generation);
        m.jobs.push_back({generation, op == Op::file_a});
        return;
    }
    case Op::overtake:
        // The queued request is read while a newer one arrives: the worker
        // stops before recording anything for it (contract, format notes).
        m.jobs.clear();
        m.jobs.push_back({++m.requests, true});
        return;
    case Op::work:
        // Plan, stale requests: rapid loads coalesce to the newest; older
        // ones are skipped with nothing recorded.
        for (const auto& job : m.jobs) {
            if (job.generation < m.requests)
                continue;
            if (job.exists)
                accept(x, m, Which::a, false, job.generation, true);
            else
                reject(m, HostFailureKind::filter_file, job.generation);
        }
        m.jobs.clear();
        return;
    }
}

// Loads under an allocation limit, kept out of the enumeration for time.
bool starving(Op op) {
    return op == Op::state_b_starved || op == Op::state_b_shortage;
}
bool supports(const Format& format, Op op) {
    return op == Op::listen || std::find(format.operations.begin(), format.operations.end(), op) !=
                                   format.operations.end();
}
// Whether a host may do this now: audio only while active, configuration
// changes only while stopped, LV2 run() (which carries patch messages) only
// while active.
bool allowed(const Context& x, const Model& m, Op op) {
    if (!supports(x.format, op))
        return false;
    switch (op) {
    case Op::play:
    case Op::listen:
    case Op::reset:
    case Op::stop:
    case Op::bypass:
    case Op::file_a:
    case Op::file_missing:
    case Op::file_refused:
        return m.active;
    case Op::start:
        return m.configured && !m.active;
    case Op::reopen:
        return m.configured;
    case Op::rate:
    case Op::layout:
        return m.configured && !m.active;
    case Op::setup:
        return !m.configured;
    case Op::work:
        return !m.jobs.empty();
    case Op::overtake:
        return m.active && m.jobs.size() == 1 && m.jobs[0].generation == m.requests;
    default:
        return true;
    }
}

// --- What is observable -------------------------------------------------------

// Contract, loading and failure (the three situations), bypass and the format
// notes: what an active instance plays once every ramp and transition is over.
struct Audible {
    enum Kind { filter, dry, silence } kind;
    Configuration configuration;
    std::string describe() const {
        return kind == dry ? "the dry input" : kind == silence ? "silence" : "a filter";
    }
};
Audible audible(const Context& x, const Model& m) {
    const auto& h = m.host;
    if (m.bypass)
        return {h.inputs == h.outputs ? Audible::dry : Audible::silence, {}};
    if (m.filter) {
        const auto& c = configuration(x.filters, *m.filter);
        if (mismatch(c, h))
            return {Audible::silence, {}};
        return {Audible::filter, c};
    }
    if (m.failed || h.inputs != h.outputs)
        return {Audible::silence, {}};
    return {Audible::filter, unfiltered_configuration({h.sample_rate, h.inputs, h.outputs}, false)};
}

std::string describe(const std::optional<Which>& w) {
    if (!w)
        return "none";
    return *w == Which::a ? "A" : *w == Which::b ? "B" : "other-rate";
}
std::string describe(const std::optional<Failure>& f) {
    if (!f)
        return "none";
    return std::string(kind_name(f->kind)) + " request " + std::to_string(f->request) +
           (f->silent ? " silent" : " filter kept");
}
std::string describe(const std::optional<HostFailure>& f) {
    if (!f)
        return "none";
    return std::string(kind_name(f->kind)) + " request " + std::to_string(f->request.number) +
           (f->silent ? " silent" : " filter kept");
}

// --- The runner ---------------------------------------------------------------

class Runner {
  public:
    Runner(Driver& d, const Filters& f) : driver_(d), format_(d.format()), filters_(f) {
        tail_ = std::max({test::tail(f.a), test::tail(f.b), test::tail(f.other_rate_filter)});
        // The guard must see what it is meant to catch.
        audio_guard::take();
        {
            audio_guard::Scope audio;
            // Called directly: a new-expression's allocation may be elided.
            void* p = ::operator new(16);
            ::operator delete(p);
            std::mutex mutex;
            const std::lock_guard lock(mutex);
        }
        const auto seen = audio_guard::take();
        if (seen.allocations != 2 || (audio_guard::counts_locks() && seen.locks != 1))
            throw std::logic_error("the audio-thread guard does not count allocations and locks");
    }
    // One sequence on a new instance. Throws Mismatch.
    void run(bool configured, const std::vector<Op>& ops, std::size_t unchecked) {
        const Context x{format_, filters_};
        Model m;
        m.host = filters_.host;
        m.configured = configured;
        if (configured)
            m.committed = m.host; // Created as PLUG-2 creates: negotiated and bound.
        driver_.create(filters_.host, configured);
        step_ = 0;
        for (const auto op : ops) {
            if (!allowed(x, m, op))
                throw std::logic_error(std::string("not allowed here: ") + name(op));
            perform(x, m, op);
            ++step_;
            if (step_ > unchecked || step_ == ops.size())
                check(m);
        }
        if (m.active)
            listen(x, m);
        driver_.destroy();
    }

  private:
    // Plays interleaved input (silence past its end) for `frames` frames in
    // uneven callbacks; returns interleaved output.
    std::vector<float> render(const Model& m, const std::vector<float>& input, std::size_t frames) {
        const auto ni = m.host.inputs, no = m.host.outputs;
        static constexpr std::array<std::size_t, 6> sizes{61, 128, 1, 300, 17, 512};
        std::vector<std::vector<float>> in(ni), out(no);
        std::vector<const float*> ip(ni);
        std::vector<float*> op(no);
        std::vector<float> result(frames * no);
        for (std::size_t done = 0, i = 0; done < frames; ++i) {
            const auto n = std::min({sizes[i % sizes.size()], driver_.max_block(), frames - done});
            for (std::size_t c = 0; c < ni; ++c) {
                in[c].assign(n, 0.f);
                for (std::size_t t = 0; t < n; ++t) {
                    const auto k = (done + t) * ni + c;
                    in[c][t] = k < input.size() ? input[k] : 0.f;
                }
                ip[c] = in[c].data();
            }
            for (std::size_t c = 0; c < no; ++c) {
                out[c].assign(n, .75f); // Not silence: the plug-in must write every frame.
                op[c] = out[c].data();
            }
            audio_guard::take();
            const bool ok = driver_.process(ip, op, n);
            const auto counted = audio_guard::take();
            expect(ok, "a callback reported a processing error");
            expect(!counted.allocations,
                   "a callback allocated or freed memory " + std::to_string(counted.allocations) +
                       " times (contract: nothing on the audio thread allocates)");
            expect(!counted.locks, "a callback locked a mutex " + std::to_string(counted.locks) +
                                       " times (contract: nothing on the audio thread locks)");
            for (std::size_t c = 0; c < no; ++c)
                for (std::size_t t = 0; t < n; ++t)
                    result[(done + t) * no + c] = out[c][t];
            done += n;
        }
        return result;
    }
    std::vector<float> noise(std::size_t frames, std::size_t channels) {
        std::vector<float> v(frames * channels);
        for (auto& s : v)
            s = std::uniform_real_distribution<float>(-.5f, .5f)(random_);
        return v;
    }

    void perform(const Context& x, Model& m, Op op) {
        const auto& f = filters_;
        const auto state = [&](const std::optional<Configuration>& c, bool failed) {
            HostState s;
            s.configuration = c;
            s.failed = failed;
            return encode_host_state(s);
        };
        bool accepted = true, expected = true;
        switch (op) {
        case Op::state_a:
        case Op::state_b:
        case Op::state_other_rate:
        case Op::state_empty:
        case Op::state_failed:
        case Op::state_malformed:
        case Op::empty_map: {
            std::vector<std::byte> bytes;
            if (op == Op::state_a)
                bytes = state(f.a, false);
            else if (op == Op::state_b)
                bytes = state(f.b, false);
            else if (op == Op::state_other_rate)
                bytes = state(f.other_rate_filter, false);
            else if (op == Op::state_empty)
                bytes = state(std::nullopt, false);
            else if (op == Op::state_failed)
                bytes = state(std::nullopt, true);
            else if (op == Op::state_malformed)
                for (const char c : std::string("this is not Convolver state"))
                    bytes.push_back(static_cast<std::byte>(c));
            accepted = op == Op::empty_map ? driver_.restore_empty_map() : driver_.restore(bytes);
            const auto requests = m.requests;
            apply(x, m, op);
            // Accepted exactly when the model recorded no rejection for it.
            expected = !(m.load_failure && m.load_failure->request == requests + 1 &&
                         m.load_failure->kind != HostFailureKind::saved_failure);
            expect(accepted == expected, std::string("the restore was ") +
                                             (accepted ? "accepted" : "rejected") +
                                             ", expected the other");
            return;
        }
        case Op::state_b_starved:
        case Op::state_b_shortage:
            return starved(x, m, op);
        case Op::play:
            // Long enough to finish a ramp, short enough to stop inside a
            // replacement's crossfade (256 frames of warm-up, then 256).
            render(m, noise(300, m.host.inputs), 300);
            break;
        case Op::listen:
            return listen(x, m); // Updates the model as apply() would.
        case Op::bypass:
            driver_.set_bypass(!m.bypass);
            break;
        case Op::reset:
            driver_.reset();
            break;
        case Op::stop:
            driver_.stop();
            break;
        case Op::start:
            expect(driver_.start(), "activation failed");
            break;
        case Op::rate:
        case Op::layout:
        case Op::setup: {
            Model next = m;
            apply(x, next, op);
            driver_.configure(next.host);
            m = next;
            return;
        }
        case Op::reopen:
            expect(driver_.reopen(m.host, m.active, m.bypass), "the reopened session was rejected");
            break;
        case Op::file_a:
        case Op::file_missing:
        case Op::file_refused: {
            const bool refused = op == Op::file_refused || !x.format.worker;
            // Contract, format notes (LV2): when run() refuses a file, an
            // instance with no accepted filter mutes at once, before any
            // control call; an accepted filter keeps playing.
            const bool mutes = refused && m.active && !m.bypass && !m.filter;
            driver_.choose_file(op == Op::file_missing ? f.missing_file : f.a_file, refused);
            if (mutes) {
                const auto length = ramp + transition + 512;
                const auto out = render(m, noise(length, m.host.inputs), length);
                const auto from = (ramp + transition) * m.host.outputs;
                expect(std::all_of(out.begin() + static_cast<std::ptrdiff_t>(from), out.end(),
                                   [](float v) { return v == 0; }),
                       "a refused first load did not mute at once (it should be silent within " +
                           std::to_string(ramp + transition) + " frames, before any control call)");
                m.rendered = m.started = true;
            }
            break;
        }
        case Op::work:
            driver_.work();
            break;
        case Op::overtake:
            driver_.overtake(f.a_file);
            break;
        }
        apply(x, m, op);
        if (op == Op::play)
            m.rendered = true;
    }

    // Contract, host layer: "running out of memory never leaves one filter
    // playing while another is saved"; replacement: a rejected request leaves
    // the previous state unchanged. B is loaded with each allocation in turn
    // failing until it loads. After every failed attempt the saved filter is
    // unchanged and, while active, what plays is still what the model
    // expects; then B is in place as for state-B.
    void starved(const Context& x, Model& m, Op op) {
        HostState s;
        s.configuration = filters_.b;
        const auto bytes = encode_host_state(s);
        const auto filter_of = [](HostState h) {
            h.bypass = false;
            h.failed = false;
            return encode_host_state(h);
        };
        const auto before = filter_of(driver_.saved());
        for (std::size_t allowed = 0;; ++allowed) {
            if (allowed == 100000)
                throw std::logic_error("B never loaded under the allocation limit");
            const auto accepted =
                driver_.restore_starved(bytes, allowed, op == Op::state_b_shortage);
            if (!accepted)
                throw std::logic_error("this driver cannot fail allocations");
            if (*accepted) {
                expect(allowed > 0, "the allocation limit never applied");
                break;
            }
            const auto saved = driver_.saved();
            expect(filter_of(saved) == before, "a load that ran out of memory after " +
                                                   std::to_string(allowed) +
                                                   " allocations changed the saved filter");
            // A failed first load plays silence once its failure is recorded;
            // recording it needs memory too, so it may not be.
            if (saved.failed && !m.failed)
                reject(m, HostFailureKind::preparation, ++m.requests);
            m.numbers_unknown = true;
            if (m.active)
                listen(x, m);
        }
        apply(x, m, op);
    }

    // The control side: what is saved, the failure record, LV2's path and
    // status. A host saving at any moment is ordinary.
    void check(Model& m) {
        const auto saved = driver_.saved();
        const auto expected_filter =
            m.filter ? std::optional(configuration(filters_, *m.filter)) : std::nullopt;
        const auto encoded = [](const std::optional<Configuration>& c) {
            HostState s;
            s.configuration = c;
            return encode_host_state(s);
        };
        expect(encoded(saved.configuration) == encoded(expected_filter),
               "saved filter differs: expected " + describe(m.filter));
        expect(saved.failed == m.failed,
               std::string("saved \"load failed\" is ") + (saved.failed ? "set" : "clear"));
        if (format_.state_bypass)
            expect(saved.bypass == m.bypass,
                   std::string("saved bypass is ") + (saved.bypass ? "on" : "off"));
        const auto failure = driver_.failure();
        const auto& want = m.activation_failure ? m.activation_failure : m.load_failure;
        expect(failure.has_value() == want.has_value() &&
                   (!failure || (failure->kind == want->kind &&
                                 (m.numbers_unknown || failure->request.number == want->request) &&
                                 failure->silent == want->silent)),
               "failure record is " + describe(failure) + ", expected " + describe(want));
        if (format_.path) {
            const auto path = driver_.saved_path();
            const auto want_path = m.path ? filters_.a_file.string() : std::string();
            expect(path == want_path,
                   "saved path is \"" + path + "\", expected \"" + want_path + "\"");
        }
        if (format_.status && m.active) {
            // Plan, LV2 adapter design: the status follows the newest request
            // (3 loading while it is queued), then the record.
            int want_status = 1;
            if (!m.jobs.empty() && m.jobs.back().generation == m.requests)
                want_status = 3;
            else if (want || m.failed)
                want_status = 2;
            else if (m.filter)
                want_status = 0;
            const auto status = driver_.status();
            const auto counted = audio_guard::take();
            expect(!counted.allocations && !counted.locks,
                   "a run() of no frames allocated or locked");
            expect(status == want_status, "status port shows " + std::to_string(status) +
                                              ", expected " + std::to_string(want_status));
        }
    }

    // A burst of noise, compared with what the model says plays. With no
    // audio since the last activation or reset (and bypass unchanged since),
    // the contract installs a filter directly: it plays from the first frame
    // with fresh history, and nothing older is heard. Otherwise, silence is
    // played first until every ramp, transition and tail is over.
    void listen(const Context& x, Model& m) {
        const auto ni = m.host.inputs, no = m.host.outputs;
        const bool direct = !m.started && m.bypass == m.settled_bypass;
        if (!direct)
            render(m, {}, 2 * latency + transition + 2 * ramp + tail_);
        constexpr std::size_t burst = 96;
        const auto input = noise(burst, ni);
        const auto frames = burst + latency + tail_ + ramp;
        const auto out = render(m, input, frames);
        m.rendered = m.started = true;
        const auto a = audible(x, m);
        std::vector<double> want(frames * no);
        if (a.kind == Audible::filter) {
            const auto o = test::oracle(a.configuration, input, latency);
            std::copy_n(o.begin(), std::min(o.size(), want.size()), want.begin());
        } else if (a.kind == Audible::dry) {
            for (std::size_t t = 0; t < burst; ++t)
                for (std::size_t c = 0; c < no; ++c)
                    want[(t + latency) * no + c] = input[t * ni + c];
        }
        double worst = 0;
        for (std::size_t i = 0; i < want.size(); ++i)
            worst = std::max(worst, std::abs(out[i] - want[i]));
        const bool ok = a.kind == Audible::silence
                            ? std::all_of(out.begin(), out.end(), [](float v) { return v == 0; })
                            : worst <= tolerance;
        expect(ok, "audible output is not " + a.describe() +
                       (direct ? " from the first frame" : " once settled") + " (filter " +
                       describe(m.filter) + (m.failed ? ", load failed" : "") +
                       (m.bypass ? ", bypassed" : "") + "; worst difference " +
                       std::to_string(worst) + ")");
        worst_ = std::max(worst_, a.kind == Audible::silence ? 0. : worst);
    }

  public:
    double worst() const {
        return worst_;
    }
    std::size_t step() const {
        return step_;
    }

  private:
    Driver& driver_;
    Format format_;
    const Filters& filters_;
    std::size_t tail_ = 0, step_ = 0;
    std::mt19937 random_{7};
    double worst_ = 0;
};

std::string text(const std::vector<Op>& ops) {
    std::string s;
    for (const auto op : ops)
        s += std::string(s.empty() ? "" : " ") + name(op);
    return s;
}
} // namespace

const char* name(Op op) {
    for (const auto& [o, n] : names)
        if (o == op)
            return n;
    return "?";
}

std::vector<Op> parse(const std::string& s) {
    std::vector<Op> ops;
    std::istringstream in(s);
    for (std::string word; in >> word;) {
        const auto it = std::find_if(names.begin(), names.end(),
                                     [&](const auto& p) { return word == p.second; });
        if (it == names.end())
            throw std::invalid_argument("unknown operation " + word);
        ops.push_back(it->first);
    }
    return ops;
}

Filters make_filters(const Host& host) {
    Filters f;
    f.host = host;
    f.other_rate = host.sample_rate == 48000 ? 44100 : 48000;
    f.other_outputs = host.inputs != host.outputs ? host.inputs : host.outputs + 1;
    auto& a = f.a;
    a.sample_rate = host.sample_rate;
    a.input_channels = host.inputs;
    a.output_channels = host.outputs;
    a.input_delays.assign(host.inputs, 0); // As a .cfg file loads (LV2's file holds A).
    a.output_delays.assign(host.outputs, 0);
    // One path per output, from input (output mod inputs), five taps each and
    // all different; with two or more inputs, the last input also reaches
    // output 0 at half gain.
    for (std::size_t k = 0; k < host.outputs; ++k) {
        const auto g = static_cast<float>(k);
        a.paths.push_back({{.5f - .1f * g, .25f, -.125f * (g + 1), .05f, .0625f},
                           {{k % host.inputs, 1}},
                           {{k, 1}}});
    }
    if (host.inputs > 1)
        a.paths.push_back({{.3f, -.2f, .1f, 0.f, .02f}, {{host.inputs - 1, .5f}}, {{0, 1}}});
    // B: two frames later, inverted and scaled.
    f.b = a;
    for (auto& p : f.b.paths) {
        p.impulse.insert(p.impulse.begin(), 2, 0.f);
        for (auto& v : p.impulse)
            v *= -.75f;
    }
    f.other_rate_filter = a;
    f.other_rate_filter.sample_rate = f.other_rate;
    return f;
}

std::vector<Start> default_starts(const Format& format) {
    std::vector<Start> starts{{true, {}}};
    if (supports(format, Op::setup))
        starts.push_back({false, {}});
    starts.push_back({true, {Op::start, Op::state_a, Op::play}});
    return starts;
}

std::size_t run(Driver& driver, const Filters& filters, const Options& options) {
    const auto format = driver.format();
    const Context x{format, filters};
    Runner runner(driver, filters);
    std::size_t sequences = 0, failures = 0;
    const auto label = format.name + " " + std::to_string(filters.host.inputs) + "->" +
                       std::to_string(filters.host.outputs);
    // `unchecked`: operations checked only at the end (an enumerated
    // sequence, whose prefixes are sequences of their own).
    const auto one = [&](bool configured, const std::vector<Op>& ops, std::size_t unchecked) {
        ++sequences;
        try {
            runner.run(configured, ops, unchecked);
        } catch (const std::exception& e) {
            ++failures;
            try {
                driver.destroy();
            } catch (...) {
            }
            if (failures <= 20) {
                std::cout << "FAIL  " << label << (configured ? "" : " (unconfigured)") << ": "
                          << text(ops) << "\n      after operation " << runner.step() << ": "
                          << e.what() << '\n';
            }
        }
    };
    const auto initial = [&](bool configured) {
        Model m;
        m.host = filters.host;
        m.configured = configured;
        if (configured)
            m.committed = m.host; // Created as PLUG-2 creates: negotiated and bound.
        return m;
    };
    for (const auto& ops : options.fixed) {
        // A fixed sequence that negotiates with `setup` starts unconfigured.
        const bool configured = std::find(ops.begin(), ops.end(), Op::setup) == ops.end();
        one(configured, ops, 0);
        // Each load that runs out of memory also runs with a passing shortage.
        if (std::find(ops.begin(), ops.end(), Op::state_b_starved) != ops.end()) {
            auto shortage = ops;
            std::replace(shortage.begin(), shortage.end(), Op::state_b_starved,
                         Op::state_b_shortage);
            one(configured, shortage, 0);
        }
    }
    // Every allowed sequence up to options.length after each start, each on a
    // new instance and checked at its end. The model is replayed to know
    // which operations are allowed next; listening is left to the end.
    const auto starts = options.starts.empty() ? default_starts(format) : options.starts;
    for (const auto& start : starts) {
        auto m = initial(start.configured);
        for (const auto op : start.prologue) {
            if (!allowed(x, m, op))
                throw std::logic_error(std::string("prologue not allowed: ") + name(op));
            apply(x, m, op);
        }
        auto ops = start.prologue;
        const auto fixed = ops.size();
        const std::function<void(const Model&)> visit = [&](const Model& now) {
            if (ops.size() > fixed)
                one(start.configured, ops, ops.size());
            if (ops.size() == fixed + options.length)
                return;
            for (const auto& [op, n] : names) {
                if (op == Op::listen || starving(op) || !allowed(x, now, op))
                    continue;
                Model next = now;
                apply(x, next, op);
                ops.push_back(op);
                visit(next);
                ops.pop_back();
            }
        };
        visit(m);
    }
    // Longer seeded sequences, checked after every operation.
    for (unsigned seed = 1; seed <= options.seeds; ++seed) {
        std::mt19937 random(seed);
        const bool configured = !supports(format, Op::setup) || seed % 2;
        auto m = initial(configured);
        std::vector<Op> ops;
        while (ops.size() < options.seeded_length) {
            std::vector<Op> choices;
            for (const auto& [op, n] : names)
                if (!starving(op) && allowed(x, m, op))
                    choices.push_back(op);
            const auto op =
                choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(random)];
            apply(x, m, op);
            ops.push_back(op);
        }
        one(configured, ops, 0);
    }
    std::cout << (failures ? "FAIL  " : "ok    ") << label << ": " << sequences << " sequences, "
              << failures << " failed; worst error against the oracle " << runner.worst() << '\n';
    return failures;
}
} // namespace convolver::plugin_test::lifecycle
