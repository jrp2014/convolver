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
// state is refused only once audio has played with them; a chosen file is
// refused at once; a project the host is opening never is.
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
    require(s.check(HostRequestKind::chosen_file) == HostLoadCheck::refuse_mismatch,
            "a chosen file is not refused once started");
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
// rendered with it, and then every request is checked against them.
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
    for (const auto kind :
         {HostRequestKind::chosen_file, HostRequestKind::restore, HostRequestKind::session})
        require(s.check(kind) == HostLoadCheck::refuse_mismatch,
                "a request is not checked against bound formats");
    // A later unrendered initialization at other formats leaves the bound
    // ones: the next restore at 48 kHz is still checked.
    s.started(probe);
    s.stopped();
    require(known_as(s, stereo) && !s.known(probe), "an unrendered start replaced the binding");
    require(s.last_start() && same_configuration(*s.last_start(), probe), "last start");
}
} // namespace

int main() {
    if (test::run_cases({
            {"activation_binding", activation_binding},
            {"rendering_binding", rendering_binding},
        }))
        return 1;
    std::cout << "host settling tests passed\n";
    return 0;
}
