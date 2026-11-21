// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-39 step 2: when the host has settled its rate and channels
// (HostSettling). The expected answers are the adapter contract's "state that
// arrives before the host is configured" and its format notes, written out
// here, for the hosts' orders that made each rule: REAPER's placeholder start
// for VST3, a JUCE host's 44.1 kHz probe for the Audio Unit. The adapters'
// own tests (vst3.adapter restored_while_host_settles, au.adapter
// layout_probe_before_restore) and the lifecycle runners replay those orders
// through each format's API.
#include "cases.hpp"
#include "host_settling.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace convolver;

namespace {
void require(bool condition, const std::string& what) {
    if (!condition)
        throw std::runtime_error(what);
}
const HostLayout stereo{48000, 2, 2}, six{48000, 6, 6}, probe{44100, 2, 2};

bool known_as(const HostSettling& s, const HostLayout& now) {
    const auto k = s.known(now);
    return k && same_configuration(*k, now);
}

// VST3: a start binds the rate and buses while they stay the same; restored
// state is refused only once audio has played with them; a project the host
// is opening never is. (A chosen file: chosen_file() below.)
void activation_binding() {
    HostSettling s(HostBinding::activation);
    require(!s.known(stereo) && !s.last_start(), "known before any start");
    // setupProcessing alone is not a start.
    require(s.check(HostRequestKind::restore) == HostLoadCheck::keep_until_suited,
            "restored state refused before any audio");
    // REAPER reopening a project: started on stereo buses, state restored,
    // stopped, given the track's six channels, started again.
    s.started(stereo);
    require(known_as(s, stereo), "not known while started");
    require(s.check(HostRequestKind::restore) == HostLoadCheck::keep_until_suited,
            "restored state refused before audio played");
    require(s.check(HostRequestKind::session) == HostLoadCheck::keep_until_suited,
            "a project the host opens refused");
    s.stopped();
    require(known_as(s, stereo), "not known while stopped with the same buses");
    require(!s.known(six), "known after the buses changed");
    s.started(six);
    s.played();
    require(s.check(HostRequestKind::restore) == HostLoadCheck::refuse_mismatch,
            "restored state kept after audio played");
    require(s.check(HostRequestKind::session) == HostLoadCheck::keep_until_suited,
            "a project the host opens refused after audio played");
    // Played with these buses before: a new start with them still counts.
    s.stopped();
    s.started(six);
    require(s.check(HostRequestKind::restore) == HostLoadCheck::refuse_mismatch,
            "a restart with the same buses forgot that audio played");
    // A new configuration needs the host to play again.
    s.stopped();
    s.started(stereo);
    require(s.check(HostRequestKind::restore) == HostLoadCheck::keep_until_suited,
            "a new configuration kept the earlier audio");
    require(s.last_start() && same_configuration(*s.last_start(), stereo), "last start");
}

// Audio Unit: an initialization binds the formats only once audio has
// rendered with it, and then restored state is checked against them.
void rendering_binding() {
    HostSettling s(HostBinding::rendering);
    require(!s.known(probe), "known before any initialization");
    // A JUCE host's probe: initialized at 44.1 kHz, uninitialized unrendered.
    s.started(probe);
    require(known_as(s, probe), "not known while initialized");
    s.stopped();
    require(!s.known(probe), "an unrendered probe bound the formats");
    // The project's own formats, rendered.
    s.started(stereo);
    s.played();
    s.stopped();
    require(known_as(s, stereo), "rendered formats not known after uninitializing");
    require(!s.known(probe), "known at other formats");
    for (const auto kind : {HostRequestKind::restore, HostRequestKind::session})
        require(s.check(kind) == HostLoadCheck::refuse_mismatch,
                "restored state is not checked against bound formats");
    // A later unrendered initialization at other formats leaves the bound
    // ones: the next restore at 48 kHz is still checked.
    s.started(probe);
    s.stopped();
    require(known_as(s, stereo) && !s.known(probe), "an unrendered start replaced the binding");
    require(s.last_start() && same_configuration(*s.last_start(), probe), "last start");
}

// PLUG-27, both formats: a chosen file is refused for not suiting the host
// only while the host plays audio through the plug-in (started, and audio
// has played with that configuration); otherwise it is kept and checked at
// the next start. Audacity 4.0.2's order for its window's copy: set up at
// 44.1 kHz, started for a moment with one block of silence to pass on an
// edit, stopped, while the project plays at 48 kHz in other copies.
void chosen_file() {
    for (const auto binding : {HostBinding::activation, HostBinding::rendering}) {
        const auto name = binding == HostBinding::activation ? std::string("VST3: ")
                                                             : std::string("Audio Unit: ");
        HostSettling s(binding);
        const auto chosen = [&] { return s.check(HostRequestKind::chosen_file); };
        require(chosen() == HostLoadCheck::keep_until_suited, name + "refused before any start");
        s.started(probe);
        require(chosen() == HostLoadCheck::keep_until_suited,
                name + "refused while started before any audio");
        s.played();
        require(s.playing(), name + "audio processed while started is not the host playing");
        require(chosen() == HostLoadCheck::refuse_mismatch,
                name + "kept while the host plays audio");
        s.stopped();
        require(chosen() == HostLoadCheck::keep_until_suited && !s.playing(),
                name + "refused while stopped");
        // A host that keeps the plug-in started and plays (REAPER).
        s.started(stereo);
        s.played();
        require(chosen() == HostLoadCheck::refuse_mismatch,
                name + "kept while the host plays audio again");
    }
}

// Calls a host makes out of order bind nothing they should not: a stop
// before any start, and a second start without a stop, which counts as a
// start with the new configuration.
void out_of_order() {
    for (const auto binding : {HostBinding::activation, HostBinding::rendering}) {
        HostSettling s(binding);
        s.stopped();
        require(!s.known(stereo) && !s.last_start(), "a stop before any start bound something");
        s.started(stereo);
        s.played();
        s.started(six);
        require(known_as(s, six), "the second start is not the host's configuration");
        s.stopped();
        require(!s.known(stereo), "the first start still binds after a second");
        // VST3: audio played with the first configuration, not this one;
        // the Audio Unit checks every request.
        const auto expected = binding == HostBinding::activation ? HostLoadCheck::keep_until_suited
                                                                 : HostLoadCheck::refuse_mismatch;
        require(s.check(HostRequestKind::restore) == expected,
                "restored state after a second start without a stop");
    }
}
} // namespace

int main() {
    if (test::run_cases({
            {"activation_binding", activation_binding},
            {"rendering_binding", rendering_binding},
            {"chosen_file", chosen_file},
            {"out_of_order", out_of_order},
        }))
        return 1;
    std::cout << "host settling tests passed\n";
    return 0;
}
