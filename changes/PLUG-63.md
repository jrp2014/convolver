<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# PLUG-63

## For contributors

- Plug-ins (PLUG-63): `HostFilter` keeps the accepted filter and the
  figures found for it when it was accepted (peak, noise gain, tail, gains
  and identity) in one private record, which an accepted load's commit
  replaces whole. A new figure is a member of that record, found in its
  `find_figures()`, with its accessor; PLUG-52 had to add the noise gain in
  four places. No behaviour changes.

## Done

- [x] **PLUG-63 — The accepted filter's figures in one record.** 27 November
  2026 ([host.hpp](../include/convolver/host.hpp)).
