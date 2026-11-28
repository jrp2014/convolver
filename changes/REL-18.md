<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-18

## Removed

- C++ library (REL-18): the installed CMake package no longer includes the
  plug-ins' host layer: the header `convolver/host.hpp` and the target
  `Convolver::host` (`find_package(Convolver COMPONENTS host)`), with
  everything they declared: `HostProcessor`, `HostFilter`, `HostSettings`
  and the settings constants, `HostLayout`, `HostPlayback`, `HostFailure`,
  `HostFaults`, `HostStatus`, `host_status()` and the other status and
  advice helpers, and the saved-state codec (`encode_host_state()`,
  `decode_host_state()`). These were installed by the previews up to 0.19
  and are what preview users of the C++ library lose. They change with the
  plug-ins, so they are not a library API; asking for the `host` component
  now fails with a message saying so. The installed library is
  `Convolver::core` (`engine.hpp`, `advice.hpp`) and `Convolver::io`
  (`io.hpp`, `inspection.hpp`). A program that used `HostProcessor` for
  planar buffers can call `Engine::process` with interleaved buffers; one
  that needs the host layer itself can build Convolver from source with
  `add_subdirectory`, where `Convolver::host` remains, unversioned
  ([package guide](../docs/develop/library.md#installed-package)).

## Changed

- C++ library (REL-18): from 1.0, `find_package(Convolver 1.x)` accepts any
  later 1.x version (CMake's `SameMajorVersion`); before 1.0 a version you
  ask for still matches only the same minor version. The package guide's
  examples no longer name the long-gone 0.13
  ([versioning](../docs/develop/versioning.md#minor-and-major-versions)).

## For contributors

- REL-18: `packaging.consumer.installed-host` is replaced by
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

- [x] **REL-18 — The installed C++ library is the core and I/O.**
  27 November 2026, jrp's decision on the pre-1.0 review of 0.19.21
  ([package guide](../docs/develop/library.md)).
