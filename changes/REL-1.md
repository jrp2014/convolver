<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-1

## For contributors

- Release (REL-1): `scripts/release/candidate.py` runs in two phases,
  `--phase build` (dependencies, compile, CTest, staging and the debugging
  symbols) and `--phase package` (the NSIS notice, seal, ZIP, package checks
  and installers), as two steps of each candidate job, so that the macOS
  signing step can run in the package phase with secrets the build and
  tests never see. See [candidates](../docs/develop/releasing.md#candidates).
- Release (REL-1): the "Draft preview release" workflow and
  `prepare_release.py` have a production mode for a full release, which
  checks the production gates with the requirements on `main` and the
  evidence in `packaging/evidence/X.Y.Z.json`, and drafts a release that is
  not a pre-release. Production stays disabled (`production_enabled` is
  false), so a real run refuses; a pull request's dry run now also runs the
  production checks and lists every gate the newest candidate does not yet
  meet. The release policy says per platform what production needs: the
  macOS packages signed and notarized, the Windows packages unsigned, and
  named-host tests not required on Windows and Linux. See
  [production gates](../docs/develop/releasing.md#production-gates).

## Decisions: Releases

- **26 November 2026.** The first full release is 1.0, signed with an Apple
  Developer ID and notarized on macOS, and tested in CI elsewhere: the
  Windows packages stay unsigned, so Windows shows a SmartScreen warning,
  and the Windows and Linux plug-ins ship with named-host tests listed as
  not verified (jrp chose "1.0 signed on macOS, CI-tested elsewhere": "Apple
  signing plus Mac host checks; Windows keeps the SmartScreen warning and
  Windows/Linux host tests are listed as unverified"). This replaces
  Authenticode as a production gate for 1.0. See
  [production gates](../docs/develop/releasing.md#production-gates).
