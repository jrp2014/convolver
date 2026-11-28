<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-23

## For contributors

- Release (REL-23): the production gate refuses evidence whose `record` is
  not an `https://` address with a host and no spaces; a bare `https://`
  passed as a GarageBand test's record, and the release notes then linked
  the test to nowhere. It also refuses a production release whose policy
  `channel` is `developer-preview`, or any platform whose files were built
  for another channel than the policy's: each candidate platform record now
  carries the channel from its artifact manifest (REL-21). Previews are
  checked as before. `gate_tests` covers both, and the new tests fail on the
  gate before this change. From John's external release audit of
  28 November 2026
  ([production gates](../docs/develop/releasing.md#production-gates)).

## Done

- [x] **REL-23 — The production gate checks evidence links and the release
  channel.** 28 November 2026, from the external release audit of 0.22.7
  ([production gates](../docs/develop/releasing.md#production-gates)).
