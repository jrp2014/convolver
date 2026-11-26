<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# REL-11

## Changed

- Release notes (REL-11): the notes now say where the packages were
  installed before the draft was made: the Windows package on Windows Server
  2025 and on Windows 11 on Arm under its x64 emulation, and the macOS
  packages on macOS 15, with nothing yet run on macOS 14 itself. See
  [publishing](../docs/develop/releasing.md#publishing-an-unsigned-preview).

## For contributors

- Releases (REL-11): the "Draft preview release" workflow's clean-machine
  jobs now also run the macOS packages, on GitHub's `macos-15` (Apple
  silicon, arm64 package) and `macos-15-intel` (x86-64 package): the checks
  on the unpacked ZIP and its VST3 library, then the `.pkg` installed and
  checked as on the build machine (`macos_installer.py`, with `auval
  -strict`), the installed VST3 library loaded, a first-use example rendered
  with the installed CLI, and `uninstall.sh` run. Windows is now also tried on
  `windows-11-arm` with an x86-64 Python, besides Windows Server 2025. Gaps,
  stated in the [publishing steps](../docs/develop/releasing.md#publishing-an-unsigned-preview):
  GitHub has no macOS 14 image (retired 2 November 2026) and no Windows 11
  image for x86-64. The Audio Unit's version is checked against the
  package's own `VERSION.txt`, so the dry run on a pull request can check an
  earlier tag's files.

## Done

- [x] **REL-11 — Clean installs on the oldest systems CI offers.** 26 November
  2026, PR #300
  ([publishing](../docs/develop/releasing.md#publishing-an-unsigned-preview)).
