<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Host qualification

No named DAW is currently qualified for portable Convolver. Automated SDK
validation, standalone adapter tests and manual DAW tests are different evidence.
REAPER on Windows/macOS/Linux is the proposed first smoke host. Ableton Live and
Cubase need available licences/testers; no compatibility claim is inferred.

There are two levels of testing. An **unsigned preview** needs the smoke test
below. A **signed production release** needs the full qualification procedure.
Both gates are set out in the plug-in formats plan in the source repository's
`docs/superpowers/plans/` ("Unsigned previews and production").

## Every report

Record host version, OS version/build, CPU architecture, package SHA-256, FFT
backend, sample rate, input/output channel layout, buffer size and exact steps/results.
Keep private audio out of reports. Use the supplied synthetic examples.

## Smoke test for an unsigned preview

1. Download the preview the way a user would and check its SHA-256 against the
   release notes. Install it as the [install guide](install.md) says, including
   its quarantine removal on macOS for this bundle only. Never disable security
   system-wide.
2. Scan, insert Convolver, and load one example preset at 48 kHz in mono and
   stereo. Check that the identity and echo examples sound as expected.
3. Save the project, close and reopen it, and check that the filter is restored.
4. Try a preset with the wrong rate or channel count while a working filter is
   running. The working filter must keep running, and the host should show an
   error. Record what the host displays.

## Production qualification

1. Download through the actual user-facing route, retaining quarantine metadata.
   Install the signed package and scan without disabling security. Check
   converter operation too.
2. Check identity and echo output at 44.1/48/96 kHz, mono/stereo, variable blocks,
   long-tail silence, stop/start/seek and offline/bounce rendering. Align numerical
   comparison by 256 samples; use an independent time-domain convolution reference.
3. Exercise bypass automation, multiple instances and filter replacements. Check
   levels and nonfinite output. The filter transition starts with fresh history.
4. Save a project, remove its original IRs, close/reopen, and transfer the project
   to another supported architecture. Check a prior release's state after upgrading.
5. Try incorrect rate/layout and malformed presets. Confirm the last valid state
   survives a rejected load. Record the host's actual error display.
6. Install over the previous version, then uninstall. Check that unrelated plug-ins,
   host projects and custom user presets survive. Follow the guide without assistance.
7. Measure callback-time distributions and allocations with short and long IRs.
   Record CPU, host buffer and workload. An average is not a hard-real-time bound.

Nonfinite samples, wrong tails, state corruption or policy-bypass requirements
block qualification. Log each failure and add a deterministic regression when
possible. The current automated tests cover allocation guards and finite numerical
cases; no measured DAW deadline envelope is advertised.
