<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plug-in adapter contract

[Documentation index](../README.md) · [Implementation map](architecture.md) ·
[Plug-in identities and layouts](plugin-identities.md) · [Testing](testing.md#plug-in-comparisons-plug-2)

For anyone writing, changing or reviewing a plug-in adapter (VST3, Audio Unit,
LV2), and for host integrators. This page is the reference for what every
adapter must do; each format's user guide ([VST3](../vst3.md), [AU](../au.md),
[LV2](../lv2.md)) says how that looks to a user. The rules were designed in the
[plug-in formats plan](../plans/2026-10-21-plugin-formats.md#shared-adapter-contract)
and are maintained here.

**In short:**

- An instance with no filter passes input to output only when the channel
  counts are equal; otherwise it plays silence.
- A filter that cannot be used as an instance's first filter makes it play
  **silence**, never unfiltered audio. A rejected replacement leaves the
  working filter playing.
- State is checked in two stages: the state itself at once, and the host's
  sample rate and channels as soon as the host has settled them.
- Every failure keeps a structured reason on the control side, for the host's
  log, LV2's status port and, later, a status display in the other formats.
- Bypass ramps over 64 samples to the input delayed by the latency, or to
  silence when the counts differ.
- Nothing on the audio thread allocates, locks or does I/O; filters are
  prepared elsewhere and handed over.

These rules hold for VST3, LV2 and AU alike. Each adapter's tests check them
through the [PLUG-2 harness](testing.md#plug-in-comparisons-plug-2), and a
review of plug-in work checks them ([independent review](workflow.md#independent-review)).
Changing a rule is an owner's decision and goes in its own pull request.
Compatibility and numerical correctness take priority over CPU-specific
optimization in every adapter.

Contents: [loading and failure](#loading-and-failure) ·
[failure reasons](#failure-reasons) ·
[initial restoration and live replacement](#initial-restoration-and-live-replacement) ·
[bypass](#bypass) · [channel layouts](#channel-layouts) ·
[replacement and threads](#replacement-and-threads) · [host state](#host-state)

## Loading and failure

An adapter instance is in one of three situations, and each has one defined
behaviour:

| Situation | Audio | Indication | VST3 (since VST3-7, 22 October 2026) |
| --- | --- | --- | --- |
| **Empty instance:** no filter was ever loaded | Equal input and output counts: identity (input *c* to output *c*). Unequal counts: silence (owner's decision, 21 October 2026), because an unfiltered 2→6 crossover would send full-range audio to whatever is on outputs 1 and 2 | "No filter" status where the format has one | As specified |
| **Failed initial load:** the first filter (a session restore or the first preset) cannot be used: missing file without an embedded copy, wrong rate or channel counts, corrupt state | Silence (owner's decision, 21 October 2026), never a silent fallback to identity: a room correction or crossover that quietly plays unfiltered audio is worse than an obvious failure | Error returned to the host where the format allows it, status "load failed", and the explanatory message in the host's log | Silence; `setState` returns `kResultFalse`. A mismatch found at activation plays silence with no message, because VST3 has no status display without an editor; accepted until VST3-3 (owner's decision, 22 October 2026) |
| **Failed replacement:** a working filter is running and a new one is rejected | The working filter keeps running, unchanged | Error returned to the host, status "load failed", message in the log; saved state still describes the working filter | As specified, active or inactive once the plug-in has been activated at the current rate and buses; `setState` returns `kResultFalse` |

The control side of these rules is shared code in the host layer, so every
adapter gets the same decisions and messages: `HostFilter` (which filter a load
replaces, which is saved and what plays) and `unfiltered_configuration` in
[`host.hpp`](../../include/convolver/host.hpp). `HostProcessor` installs a
filter directly when nothing has been processed since construction or reset.

**Saving after a failed initial load** saves the failure: the host state
records "load failed" (flag 4, state version 2; owner's decision, 22 October
2026) and no filter, so reopening the project plays silence again instead of
an empty instance's identity. Only this state is written as version 2.
Restoring it is a successful load of what was saved, not a rejected
replacement, so it replaces a working filter with silence.

A replacement is accepted only after it has been fully validated and prepared,
so rejection never disturbs the running filter. The rejected request is not
retried automatically.

**State that arrives before the host is configured.** Hosts can restore state
before they set the sample rate and channel layout (VST3 `setState` before
`setupProcessing`/`setBusArrangements`; AU `RestoreState` before
`Initialize`). Two kinds of check are therefore kept apart:

- **Checks on the state itself** run immediately: decoding, the version, the
  resource limits and the 1–8 channel bound. Malformed state is rejected at
  once, and the previously accepted state is unchanged.
- **Checks against the host's configuration** (sample rate, channel counts,
  layout) run as soon as that configuration is known. If it is already known
  when the state arrives, a mismatch is rejected at once, as a failed
  replacement (or a failed initial load if nothing was accepted before).
- **When the configuration is not known yet,** well-formed state replaces the
  previously accepted state straight away: the host is restoring what it saved.
  The configuration check then happens at activation. If it fails there, that is
  a failed initial load (silence and an error), but the restored state is kept
  as the saved state, so opening a project at the wrong sample rate and then
  correcting the rate brings the filter back, and saving the project does not
  lose it.
- LV2 knows the sample rate at instantiation and its ports are fixed, so its
  configuration is always known and both kinds of check run in `restore()`.
- VST3 counts its configuration as known once the plug-in has been activated
  with it, and while the rate and buses stay the same. `setupProcessing` alone
  is not binding: JUCE-based hosts, pluginval among them, call it with
  placeholder values (44.1 kHz) before restoring state made for another rate,
  which a check at that point would wrongly reject (found by
  `vst3.pluginval.acceptance` in VST3-7; owner's decision, 22 October 2026).

## Failure reasons

Added 22 October 2026 from jrp's
[release-readiness guidance](../review-2026-10/release-readiness-guidance-2026-10-22.md);
PLUG-8 implements it. Silence can be correct processing and still confuse the
user, so every failure in the table above keeps a structured reason, even while
the place it is shown is undecided (DIAG-1):

- **What failed:** a missing or unreadable filter file, unreadable or
  malformed state, a newer state version, the sample rate, the channel counts
  or layout, preparation, or activation (the same list as DIAG-1's table).
- **The request it belongs to:** where the filter came from (preset, saved
  state, file path), its rate and channel counts, and a request number, so a
  later request's success or failure is never confused with it.
- **The outcome:** a working filter kept, or silence.
- **The explanatory message,** written as the CLI writes its diagnostics: what
  failed, why, and what to change.

The reason lives on the control side (in `HostFilter` or beside it) and never
on the audio thread: the audio callback only plays what it was given. Every
adapter records a reason on every failure path, including activation and
worker completions, and clears it when a later request succeeds. Tests read it
through the adapter even where the format cannot display it yet. Notices that
are not failures use the same record unless PLUG-8 records why not.

Implemented 23 October 2026 by PLUG-8 (PR #46): `HostFailure` in
[`host.hpp`](../../include/convolver/host.hpp), returned by
`HostFilter::failure()`. A rejected request's reason stays until a later
request succeeds, across activations, so the "load failed" status does not
disappear when the host restarts the plug-in. A mismatch found at activation
belongs to the request that supplied the filter, comes first while it lasts,
and clears at the next activation that suits the filter. Restoring a saved
failed load is a successful request whose instance plays silence, so it keeps
a reason of its own kind (the original reason is not saved; the state format
is unchanged). The speaker-layout notice stays a separate string
(`HostFilter::notice()`): it never changes what plays, and it describes the
host as it is now rather than a request, so it is recomputed at every check
instead of being kept until a later request succeeds. Adapters log
`HostFilter::rejection()`, the newest request's own reason, when a load is
rejected, so a failure found at activation never hides why the new preset was
refused. A host format that the adapter refuses before any filter is
involved (an Audio Unit asked for different input and output sample rates)
is returned to the host as an error and logged, but not recorded: the unit
does not start, and no filter request failed.

## Initial restoration and live replacement

- **Initial restoration** installs the filter directly: when no audio has been
  processed since activation, the adapter constructs the `HostProcessor` with
  the restored configuration. There is no warm-up or crossfade, so output
  matches the filter from the first block (after the reported latency).
- **Live replacement** uses `HostProcessor::replace`: the new filter starts with
  fresh history, warms up for 256 frames and crossfades for 256 frames, as described under
  [replacement and threads](#replacement-and-threads).
- Pending replacements coalesce to the newest, as `HostProcessor` already does.

## Bypass

Bypass ramps over 64 samples to the dry signal and back, with the dry path
delayed by the reported latency so wet and dry line up. Wet processing carries
on during bypass, so switching back does not lose history.

- **Equal input and output counts:** the dry signal is input *c* on output *c*.
- **Unequal counts:** every output ramps to silence (owner's decision,
  22 October 2026). No output has an unfiltered counterpart: bypassing a 2→6
  crossover must not send full-range audio to a tweeter, and a 1→2 filter that
  forks a mono input to two outputs cannot tell which output the input belongs
  on. This matches the contract for an empty instance with unequal counts.

`HostProcessor` implements this, so every adapter that drives it through
`set_bypass` follows the rule; the PLUG-2 `bypass` case checks it. An adapter
must not use its format's own pass-through bypass (see the AU design in the
[plug-in formats plan](../plans/2026-10-21-plugin-formats.md#au-adapter-design)).

## Channel layouts

What a channel means (discrete or a named speaker layout), how presets with no
recorded layout are treated, and the per-format details are in
[plug-in identities and channel layouts](plugin-identities.md#channel-layouts).
In short: version-1 presets load on any arrangement with matching channel
counts, and a recorded output speaker mask only ever gives a notice.

## Replacement and threads

The host owns the audio device, clock and scheduling. Each callback supplies
the next section of one continuous stream; convolution history persists
between callbacks, and processing produces one output frame per input frame
after the reported latency (256 samples, independent of the callback length).

Filters are prepared outside processing and transferred through a bounded
single-producer/single-consumer pointer handoff. A filter that arrives before
any audio has been processed since activation or reset is installed directly.
A reset (the host stopping and restarting processing) also ends a crossfade in
progress, so the newer filter plays at once, and a filter published before
audio resumes is installed directly. Later, a replacement runs for 256 samples
to fill its latency, then crossfades for 256 samples. It starts with fresh
input history: it cannot reconstruct audio that preceded loading. Old engines
are retired on the control thread. Pending changes coalesce to the newest; at
most one transition and one pending replacement are retained, with up to two
old engines awaiting control-thread collection after the last publication.
Collection does not hold up pending changes. The tail is available before
activation and includes routed delays; it can grow conservatively during a
session, and reactivation recomputes it.

`Convolver::host` exports `HostProcessor`, `HostFilter` and the state codec
without SDK or file I/O dependencies ([`host.hpp`](../../include/convolver/host.hpp)).
Prepare, replace, collect and destruct calls must be serialized on the control
side. One audio thread calls process, reset and set_bypass. Destruction and
reconfiguration require audio to have stopped. Reset clears history without
allocation, but its work scales with prepared storage. Filter preparation can
be expensive; neither FFT planning nor file access belongs in a callback.

Finite input outside float range, and nonfinite input, become silence.
Nonfinite output is contained and history reset. The processing boundary does
not throw; invalid dimensions return failure without advancing audio.

## Host state

Host state uses a bounded, versioned little-endian format (64 MiB encoded
limit), validated transactionally: state that fails decoding or its own checks
leaves the previous state untouched. Checks against the host's configuration
follow [loading and failure](#loading-and-failure), including state restored
before the host is configured. Version 1 records the filter (or an empty
instance) and the bypass setting; version 2 is written only for a saved failed
initial load, with no filter.
Decoders read both; encoders write version 1 whenever nothing new is recorded,
so older builds still read those presets. How each format stores it (the
`.vstpreset` chunk, the AU `convolver-state` key, the LV2 state property) is in
[what the files contain](../presets.md#what-the-files-contain).
