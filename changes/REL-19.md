<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-19

## Removed

- C++ library (REL-19): the installed CMake package no longer includes the
  plug-ins' host layer: the header `convolver/host.hpp` and the target
  `Convolver::host` (`find_package(Convolver COMPONENTS host)`). The
  previews up to 0.19 installed them; this is everything they declared,
  which preview users of the C++ library lose:
  - processing and loading: `HostProcessor`, `HostFilter`, `HostLayout`,
    `set_host_rate()`, `host_rate_text()`, `same_rate()`, `HostPlayback`,
    `filter_playback()`, `unfiltered_playback()`,
    `unfiltered_configuration()`, `HostRequest`, `HostRequestKind`,
    `HostFilterSource`, `HostLoadCheck`, `HostChange`, `HostActivation`,
    `SupersededRequest`, `filter_file_name()` and `max_filter_name_bytes`;
  - settings and gain: `HostSettings`, `min_gain_db`, `max_gain_db`,
    `host_latencies`, `default_host_latency`, `GainMode`,
    `gain_mode_count`, `gain_mode_name()`, `FilterGains`, `filter_gains()`
    and `applied_gain_db()`;
  - saved state: `HostState`, `HostStateView`, `view_of()`, `HostSnapshot`,
    `encode_host_state()`, `decode_host_state()`,
    `encoded_host_state_size()`, `host_state_size_error()`,
    `HostStateHeader`, `decode_host_state_header()`,
    `host_state_header_bytes`, `NewerHostState`, `same_host_state()`,
    `max_host_state_bytes`, `host_settings_room_bytes`,
    `max_preset_name_bytes` and `storable_preset_name()`;
  - routing and comparison: `same_configuration()` (both overloads),
    `configuration_identity()`, `routing_sentence()`,
    `true_stereo_routing()`, `plays_on_each_channel()`, `ignored_inputs()`
    and `speaker_notice()`;
  - status and faults: `HostStatus`, `HostStatusLine`, `max_status_text`,
    `host_status()`, `host_status_value()`, `HostDrySignal`,
    `dry_signal_rule()`, `dry_signal_before_host()`, `no_dry_reason()`,
    `HostFailure`, `HostFailureKind`, `HostFaults`, `HostFaultReport` and
    `fault_message()` (both overloads).

  They change with the plug-ins, so they are not a library API; asking for
  the `host` component now fails with a message saying so. The installed
  library is `Convolver::core` (`engine.hpp`, `advice.hpp`) and
  `Convolver::io` (`io.hpp`, `inspection.hpp`). To move off them: a program
  that used `HostProcessor` for planar buffers can call `Engine::process`
  with interleaved buffers; `worst_case_peak()` and
  `estimate_attenuation()` in `engine.hpp` give what `filter_gains()` did;
  `Configuration`'s own fields give its routing. A program that needs the
  host layer itself can build Convolver from source with
  `add_subdirectory`, where `Convolver::host` remains, unversioned and
  unsupported as an API
  ([package guide](../docs/develop/library.md#installed-package)).

## Changed

- C++ library (REL-19): from 1.0, `find_package(Convolver 1.x)` accepts any
  later 1.x version (CMake's `SameMajorVersion`); before 1.0 a version you
  ask for still matches only the same minor version. The package guide's
  examples no longer name the long-gone 0.13
  ([versioning](../docs/develop/versioning.md#minor-and-major-versions)).

## For contributors

- REL-19: `packaging.consumer.installed-host` is replaced by
  `missing-host`, which requires the explanation; the installed consumers
  check that none of the package's include directories is in the source
  tree and that `convolver/host.hpp` cannot be found, so they build from the
  installed headers alone (jrp's acceptance test). The package guide no
  longer calls the engine uniform-partitioned (non-uniform since PERF-2);
  from the pre-1.0 review of 0.19.21.

## Decisions: Releases

- **27 November 2026.** The 1.0 C++ library is the core and I/O: stop
  installing `host.hpp` and `Convolver::host`, keep the host layer in the
  source tree, and prove it with an external consumer built against the
  installed package with no source-tree headers available. jrp: "Ship core
  and I/O; keep host support in-tree." The 1.0 release notes must say which
  C++ interfaces preview users lose. The CMake package version follows
  `SameMinorVersion` before 1.0 and `SameMajorVersion` from 1.0. See the
  [package guide](../docs/develop/library.md).

## Done

- [x] **REL-19 — The installed C++ library is the core and I/O.**
  27 November 2026, jrp's decision on the pre-1.0 review of 0.19.21
  ([package guide](../docs/develop/library.md)).
