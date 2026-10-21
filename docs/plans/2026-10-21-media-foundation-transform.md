<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Media Foundation transform (MFT) plan

Status: proposal, dated 21 October 2026, written against `f6ac6c6` (package
0.4.3). Nothing here is implemented. Tracked as HOST-3 in [TODO](../../TODO.md).
External facts were checked on the date above; recheck them before implementing.

Revision note, 21 October 2026: the [documentation review](../review-2026-10/ooura-and-roadmap-2026-10-21.md)
corrected speaker-mask semantics, buffering/latency requirements and Equalizer
APO coverage. These are proposed contracts, not implemented adapter behavior.

**Parked (owner, 21 October 2026):** a Windows-native adapter needs more thought
before any work starts. VST3, LV2 and AU come first; see the
[plug-in formats plan](2026-10-21-plugin-formats.md).

**In short:**

- A Media Foundation transform (MFT) is a Windows COM component that media
  applications can insert into their audio path. Wrapping the engine as an
  audio-effect MFT would let Windows programs apply room correction, crossovers
  or convolution reverb during playback, transcoding or capture.
- Very few programs load a third-party audio MFT on their own. Windows Media
  Player Legacy does, through its DSP plug-in registration. Everywhere else, an
  application has to insert the effect in code. Media Foundation never inserts
  effects automatically, and the new Windows 11 Media Player has no documented
  plug-in mechanism.
- System-wide processing (Spotify, browsers, games) needs an Audio Processing
  Object (APO), not an MFT. Equalizer APO already convolves with WAV impulse
  responses, so exporting our `.cfg` files to Equalizer APO is likely the
  cheapest way to serve that need when applications use the system-effects path;
  ASIO and WASAPI exclusive playback bypass it. We recommend against writing our
  own APO for now.
- Recommended design: a synchronous, dual-mode DSP plug-in. One COM object
  implements both `IMFTransform` (Media Foundation) and `IMediaObject` (DirectX
  Media Object, for DirectShow players). It is built on the existing
  `HostProcessor`, with latency compensation and the filter tail emitted on drain.
  Input is 32-bit float or PCM. Output is float.
- Work in phases. First, a portable stream-transform layer that CI tests on every
  platform. Then the MFT DLL and Windows CI (hosted runners must install the Media
  Foundation feature). Then Windows Media Player and DirectShow support, WinRT
  activation, and finally signed installers through the REL-1 release work.

Contents: [who would load it](#who-would-load-an-audio-mft),
[recommendation](#recommendation), [design](#component-design),
[media types](#media-types-and-negotiation), [latency](#latency-tail-and-timestamps),
[configuration](#configuration-and-ir-loading), [registration](#registration-and-discovery),
[APOs](#system-wide-processing-apos-and-equalizer-apo), [testing](#testing),
[distribution](#distribution-and-signing), [phases](#phases-and-exit-criteria),
[history](#comparison-with-the-historical-plug-in), [open questions](#open-questions-and-risks).

## Who would load an audio MFT

The value of an MFT depends entirely on which hosts load it. The table lists the
realistic routes in October 2026.

| Host or API | How an effect gets in | What we must provide |
| --- | --- | --- |
| Windows Media Player Legacy (still an optional Windows 11 feature) | User enables a registered DSP plug-in in *Tools › Options › Plug-ins*. In the Media Foundation pipeline the player queries `IMFTransform` first and wraps a DMO otherwise ([required interfaces](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/wmp/required-interfaces)). | `IMFTransform` + `IMFGetService` (MFT), or `IMediaObject` + `IWMPPluginEnable` (DMO), or all four ("dual-mode"); `ISpecifyPropertyPages` for settings; registration through `IWMPMediaPluginRegistrar`. |
| Windows 11 Media Player (the modern app) | No documented third-party effect mechanism found. | Nothing we can target. |
| Media Foundation applications | The application inserts the effect: a transform node in a Media Session topology, [`IMFMediaEngineEx::InsertAudioEffect`](https://learn.microsoft.com/en-us/windows/win32/api/mfmediaengine/nf-mfmediaengine-imfmediaengineex-insertaudioeffect) (Windows 8+, takes an `IMFTransform` or `IMFActivate`), `IMFSourceReaderEx::AddTransformForStream`, or legacy [`IMFPMediaPlayer::InsertEffect`](https://learn.microsoft.com/en-us/windows/win32/medfound/how-to-add-audio-or-video-effects). | A registered COM class, or a DLL the application loads itself. |
| WinRT media APIs (`MediaPlayer`, `MediaTranscoder`, `MediaCapture`) | The application calls [`MediaPlayer.AddAudioEffect`](https://learn.microsoft.com/en-us/uwp/api/windows.media.playback.mediaplayer.addaudioeffect) (or the transcoder/capture equivalents) with an activatable class ID and an `IPropertySet`. | A WinRT activatable class implementing `IMediaExtension` (for `SetProperties`) and `IMFTransform`, registered in the application's package manifest or its registry-free WinRT manifest. |
| DirectShow players (MPC-HC, MPC-BE, Zoom Player, older editors) | They load DirectShow filters and DMOs, not MFTs. | `IMediaObject`, registered with `DMORegister` in the audio-effect category. The dual-mode object covers this. |
| `Windows.Media.Audio.AudioGraph` | Uses `IBasicAudioEffect`, a different WinRT interface. | Out of scope here. |
| Everything that plays through the system mixer | Only an APO in the audio engine sees it. | See [APOs](#system-wide-processing-apos-and-equalizer-apo). |

Consequences:

- An MFT on its own reaches **one end-user program**: Windows Media Player
  Legacy. Microsoft calls the WMP SDK a legacy feature superseded by `MediaPlayer`,
  and is trimming the Legacy player's features. In August 2026, Experimental build
  29648 removed its caption controls ([report](https://windowslatest.com/2026/08/19/windows-11-is-breaking-legacy-media-player-features-to-push-you-to-a-modern-app-that-uses-3x-more-ram/)).
  Plan for it to disappear.
- Otherwise the MFT is a **developer component**: a player, transcoder or capture
  tool written against Media Foundation or WinRT can add Convolver with a few lines.
- The DMO face of a dual-mode object restores what the historical ConvolverWMP
  offered DirectShow players, which are still in use for home-theatre playback.
- Audio in the Protected Media Path (DRM-protected streams) does not load unsigned
  or untrusted MFTs, so those streams would play unprocessed.

## Recommendation

1. Before building anything, confirm which of the routes above a real user
   needs (Phase 0). If the need is system-wide room correction, do the Equalizer
   APO export first if its endpoint/system-effects coverage meets that need.
   It does not cover ASIO or WASAPI exclusive playback; see the
   [upstream requirements](https://sourceforge.net/projects/equalizerapo/).
2. If an MFT is wanted, build one dual-mode DSP plug-in (MFT + DMO + WMP
   enable/property page) instead of separate MFT and DMO projects. Microsoft's
   own audio DSPs follow this pattern, and so did our historical WMP plug-in on the DMO side.
3. Make it synchronous. Asynchronous MFTs exist for hardware codecs and add an
   event-driven protocol (`MF_TRANSFORM_ASYNC`, `MF_TRANSFORM_ASYNC_UNLOCK`,
   `METransformNeedInput`/`METransformHaveOutput`) that an audio effect gains
   nothing from. The Legacy player, Media Engine and Source Reader all accept
   synchronous effects.
4. Keep the format-independent logic (negotiation decisions, latency compensation,
   flush/drain state) in a portable class tested on every CI platform. That way
   the Windows-only COM shell stays thin.

## Component design

New optional adapter `plugins/mediafoundation/`, built only on Windows with
`-DCONVOLVER_BUILD_MFT=ON`, producing `ConvolverMFT.dll` (in-process COM server).

```text
Media Foundation / WMP / DirectShow / WinRT host
        │  IMFTransform · IMediaObject · IWMPPluginEnable · IMFGetService
        │  ISpecifyPropertyPages · IMediaExtension · IConvolverMediaEffect
        ▼
ConvolverMediaEffect (COM shell, WRL)          ← plugins/mediafoundation/
        │  media-type translation, IMFSample/IMediaBuffer access, registration
        ▼
StreamTransform (portable, no Windows headers) ← new, in the host library
        │  format choice, latency compensation, flush/drain/tail, chunking
        ▼
HostProcessor (existing)                       ← include/convolver/host.hpp
        ▼
Engine (existing)
```

- **COM framework:** use WRL (`Microsoft::WRL::RuntimeClass`, in the Windows SDK)
  instead of ATL. WRL can serve both classic COM (`DllGetClassObject`) and WinRT
  activation (`DllGetActivationFactory`) from one class, and it needs no ATL
  redistributable or Visual Studio ATL component.
- **`StreamTransform`** (portable): owns a `HostProcessor`. It accepts arbitrary
  input frame counts, processes in chunks of at most `max_frames` (for example
  4096), drops the first `latency_frames()` output frames after a start or flush,
  and on drain feeds `latency_frames() + tail_frames()` zero frames through the
  processor, discarding any remaining startup latency before emitting the tail.
  An empty stream emits nothing. Its unit tests run under the sanitizers on Linux
  and macOS as well as Windows.
- **Threading:** the shell declares `ThreadingModel=Both`. Media Foundation calls
  `ProcessInput`/`ProcessOutput` from its work-queue threads, and configuration
  arrives from UI or application threads. An `SRWLOCK` guards the shell's bookkeeping. Configuration changes go through
  `HostProcessor::replace` and `collect`, which already prepare off the audio path
  and crossfade. The engine rule stands: nothing called from `ProcessOutput` allocates,
  plans FFTs or reads files. An MFT is pull-driven rather than a hard-deadline
  callback, but playback still stutters if it stalls.
- **Allocation:** the caller provides output samples. Do not set
  `MFT_OUTPUT_STREAM_PROVIDES_SAMPLES`, so the MFT never allocates `IMFSample`s
  while streaming. Report the output audio frame size (`MF_MT_AUDIO_BLOCK_ALIGNMENT`)
  as `cbSize`, the minimum output buffer size, following
  [`MFT_OUTPUT_STREAM_INFO`](https://learn.microsoft.com/en-us/windows/win32/api/mftransform/ns-mftransform-mft_output_stream_info).
  There is no maximum input sample size negotiated by the listed media types;
  `HostProcessor::max_frames` is an internal per-call processing bound.
  Retain at most one caller-owned input sample and a cursor, validate its byte
  length/frame alignment, and process it in bounded chunks. Allocate any working
  FIFO at media-type setup with explicit capacity; never copy an arbitrarily
  sized input sample into a growing queue. Honor output buffers as small as one
  audio frame and preserve unconsumed input and pending output between calls.
  Advertise this retention with `MFT_INPUT_STREAM_HOLDS_BUFFERS`, leave
  `MFT_INPUT_STREAM_DOES_NOT_ADDREF` unset, and report the corresponding input
  latency/lookahead fields consistently with the
  [input-stream contract](https://learn.microsoft.com/en-us/windows/win32/api/mftransform/ne-mftransform-_mft_input_stream_info_flags).
  Release retained samples on consumption, flush, type clear and teardown.
- **Messages:** `MFT_MESSAGE_NOTIFY_BEGIN_STREAMING` (allocate if not done),
  `NOTIFY_START_OF_STREAM` (arm latency compensation), `COMMAND_FLUSH`
  (`reset()`, discard queued audio, re-arm), `COMMAND_DRAIN` (emit tail, then
  `MF_E_TRANSFORM_NEED_MORE_INPUT`), `NOTIFY_END_OF_STREAM`/`END_STREAMING`
  (no work), `SET_D3D_MANAGER` (`E_NOTIMPL`; audio only).
- **Input sample discontinuity** (`MFSampleExtension_Discontinuity`): treat it as a
  flush (fresh history, re-armed compensation), as the historical DMO did. Mixing
  audio across a seek would be wrong.
- **Process model:** one retained input sample at a time. `ProcessInput` returns
  `MF_E_NOTACCEPTING` while that sample or pending output remains. `ProcessOutput`
  emits as many whole frames as fit, retaining the remainder, and sets
  `MFT_OUTPUT_DATA_BUFFER_INCOMPLETE` when more output is available without new
  input, including during drain. Once an input sample is consumed with no output
  ready (for example during initial latency), release it and accept the next;
  do not deadlock by waiting for output before accepting enough input.

## Media types and negotiation

| Attribute | Input | Output |
| --- | --- | --- |
| `MF_MT_MAJOR_TYPE` | `MFMediaType_Audio` | `MFMediaType_Audio` |
| `MF_MT_SUBTYPE` | `MFAudioFormat_Float` (32-bit); `MFAudioFormat_PCM` 16/24/32-bit | `MFAudioFormat_Float` (32-bit) |
| `MF_MT_AUDIO_SAMPLES_PER_SECOND` | Must equal the selected configuration's rate | Same as input |
| `MF_MT_AUDIO_NUM_CHANNELS` | Configuration's input channels | Configuration's output channels (may differ) |
| `MF_MT_AUDIO_CHANNEL_MASK` | Preserve the host's declared layout; routes address input indices, and `.cfg` declares no input mask | Configuration's explicit output mask, validated against the output count; otherwise leave unspecified |
| `MF_MT_AUDIO_BLOCK_ALIGNMENT`, `MF_MT_AUDIO_AVG_BYTES_PER_SECOND`, `MF_MT_AUDIO_BITS_PER_SAMPLE` | From the format | From the format |
| `MF_MT_ALL_SAMPLES_INDEPENDENT` | TRUE | TRUE |

Build and parse these through `MFInitMediaTypeFromWaveFormatEx` and
`MFCreateWaveFormatExFromMFMediaType` so that `WAVEFORMATEXTENSIBLE` is handled one way.

- **Available input types:** with a configuration list, offer one partial type per
  distinct (rate, input channels) entry, in list order. A Media Session topology
  loader can then insert the Audio Resampler DSP upstream when the source rate
  differs. Resampling the music to the filter's rate is acceptable here and avoids
  resampling IRs (IO-2).
- **Output type:** fixed once the input is set. `GetOutputAvailableType` returns
  `MF_E_TRANSFORM_TYPE_NOT_SET` until then. Downstream converters handle the
  float-to-device conversion. Integer output with dither stays out of scope: the
  quantizer belongs to file output, and the audio engine converts formats anyway.
- **Rejection with explanation:** when no configuration matches, `SetInputType`
  returns `MF_E_INVALIDMEDIATYPE`. Hosts that treat the effect as optional then
  play unprocessed. That silent fallback confused historical users ("ConvolverWMP is
  not engaged"), so record the reason where the user can see it. The property page
  and `IConvolverMediaEffect::GetStatus` show the stream's format, the candidates,
  and what differs, reusing the explanations of `detail::explain_no_match`. Also
  send it to `OutputDebugString` and an ETW/TraceLogging provider for developers.
- **Speaker layout:** the `.cfg` mask labels outputs only. Reject an output type
  that conflicts with an explicit output mask; never compare that mask with the
  input layout. Stereo input and 5.1 output are a valid combination. Do not guess
  an arrangement from a channel count. If a host needs labelled output where the
  configuration has none, require explicit metadata rather than inventing it.
  Input routes remain index-based until a separate input-layout contract is
  defined under VST3-1. Channel limits follow the core, not the VST3 1–8 bus limit.
- **Format change mid-stream:** a new `SetInputType` while streaming loads the matching
  configuration on the calling thread (control path) and resets. The Legacy player
  sets types before streaming. Other hosts drain or flush first.

## Latency, tail and timestamps

This adapter initially chooses B = 256 frames. The core and `HostProcessor`
support other block sizes; obtain L from `HostProcessor::latency_frames()` and
use that value throughout buffering, trimming and drain logic.
Unlike a VST3 callback, an MFT may hold input before producing output, so the
transform can **compensate the latency**:

- After start, flush or a discontinuity, discard the first L output frames.
- Output sample times equal input sample times. Duration follows from the frame count.
- On `COMMAND_DRAIN` after nonempty input, feed L plus `tail_frames()` of silence,
  continue discarding any remaining startup latency, and emit the remaining
  output. Repeated drain calls must not restart the flush. Empty input emits no
  frames. For a reverb IR the tail can be several seconds, and it is audible.

This keeps the output timestamps aligned with video without requiring VST3-style
host delay compensation. The MFT must still report its input buffering through
`GetInputStreamInfo`; those fields do not replace correct sample timestamps.
It matches the CLI's file behaviour, where latency is trimmed and the whole tail
written. Streaming capture through `MediaCapture` still incurs the inherent
L-frame block delay (about 5 ms at 48 kHz with B = 256). A `KeepLatency` attribute can disable
compensation for hosts that prefer constant throughput.

## Configuration and IR loading

| Source, highest priority first | Used by |
| --- | --- |
| `IMFAttributes` from `IMFTransform::GetAttributes`: `CONVOLVER_MFT_CONFIGURATION_PATH` (wide string) or `CONVOLVER_MFT_STATE` (blob, the `encode_host_state` bytes also used by VST3 presets) | Media Foundation applications |
| `IPropertySet` keys `ConfigurationPath` / `State` via `IMediaExtension::SetProperties` | WinRT `AddAudioEffect` callers |
| `IConvolverMediaEffect` (custom COM interface): set path/state, bypass, get status and description | Property page, advanced hosts |
| Per-user default `HKCU\Software\Convolver\MediaFoundation` (`ConfigurationPath`, `Bypass`) | Windows Media Player Legacy, where the host passes nothing |
| None | Pass-through identity at the stream's layout (`HostState` with no configuration) |

- Load on the control path when the input type is known:
  `load_configuration(path, {rate, inputs})` selects the first matching list entry,
  then construct or `replace()` the `HostProcessor`. Relative paths, signed-zero
  routes, delays and list selection all behave as in the CLI.
- FFTW uses estimate planning inside the host process. It optionally imports wisdom
  from `%LOCALAPPDATA%\Convolver\fftw-wisdom` produced by `convolver --tune-fft`.
  Statically linking FFTW gives this DLL its own planner state, separate from any
  other FFTW user in the same host.
- **Property page:** a small Win32 dialog in the DLL (no framework) to choose a WAV,
  `.cfg` or list, show the inspection summary (paths, rate, channels, gain estimate,
  latency, tail) and the last negotiation diagnostic, and toggle bypass. The
  historical WMP page needed a player restart after a filter change. The crossfading
  `replace()` removes that.
- **Reverb use:** a reverb needs dry and wet signals. Today a `.cfg` can express
  this with a one-sample unit-impulse path beside the reverb IR, because portable
  paths may have different lengths. Document a reverb example. Mix and output-gain
  parameters are a shared follow-up with VST3-4.

## Registration and discovery

`DllRegisterServer`/`DllUnregisterServer`, called by the installer (or `regsvr32`
for developers), perform four registrations:

1. **COM class:** `HKLM\Software\Classes\CLSID\{new CLSID}\InprocServer32`,
   `ThreadingModel=Both`. Use **new CLSIDs**: the historical
   `{47427372-7AED-4E37-ABEB-7BD64C4184BF}` had different interfaces and state, and
   reusing it would collide with an old 4.x install.
2. **MFT category:** `MFTRegister(clsid, MFT_CATEGORY_AUDIO_EFFECT, L"Convolver",
   0, inputTypes, outputTypes, attributes)`. This writes machine-wide keys and needs
   elevation. Applications find it with `MFTEnumEx(MFT_CATEGORY_AUDIO_EFFECT,
   MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, …)`. Registration only
   makes the effect discoverable; it never inserts it.
   `MFTRegisterLocalByCLSID` serves an application that bundles the DLL and wants
   enumeration within its own process only.
3. **Windows Media Player:** `IWMPMediaPluginRegistrar::WMPRegisterPlayerPlugin(…,
   WMP_PLUGINTYPE_DSP, clsid, …)` with PCM and float partial types, as the
   historical `convolverdll.cpp` did.
4. **DMO:** `DMORegister(…, DMOCATEGORY_AUDIO_EFFECT, …)` for DirectShow players.

Unregistration reverses all four and tolerates a missing player (Legacy WMP
not installed).

**WinRT activation** cannot be registered system-wide for an unpackaged
third-party DLL. Packaged applications declare it in their manifest
(`inProcessServer`). Unpackaged desktop applications use registry-free WinRT
activation (Windows 10 1903+) through an `activatableClass` entry in their
application manifest. The DLL exports `DllGetActivationFactory`. The sample and
test application will show the manifest.

**Architectures:** build x64 and ARM64. An in-process DLL must match its host
process, so x86 is needed only if a target host is still 32-bit. Check which
`wmplayer.exe` the target Windows versions launch before deciding. ARM64EC or
ARM64X is a later option for x64 hosts emulated on ARM.

## System-wide processing: APOs and Equalizer APO

An **Audio Processing Object (APO)** is a COM component loaded by the Windows
audio engine (`audiodg.exe`) for an endpoint. It processes streams using that
endpoint's system-effects path; ASIO and WASAPI exclusive playback bypass
Equalizer APO. Processing runs in engine periods under real-time rules (no
allocation, locks or paging in `APOProcess`), and reports latency through
`IAudioProcessingObject::GetLatency`. The engine's real-time design would fit.

Deployment is the problem. Microsoft supports APOs delivered in driver packages:
`Class=AudioProcessingObject` INF packages on Windows 11 21H2+ and
`SoftwareComponent` on Windows 10, shipped against hardware IDs
([deploying APOs](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/deploying-audio-processing-objects)).
A stand-alone product that writes itself into endpoint effect properties, as
Equalizer APO does, works outside that model. It needs administrator rights, can be
displaced by driver updates, and depends on audio enhancements being enabled. It
also needs the newer Windows 11 APO interfaces (`IAudioSystemEffects3`, settings
framework) to integrate cleanly.

**Equalizer APO** is already installed by many room-correction users. Its
[configuration reference](https://sourceforge.net/p/equalizerapo/wiki/Configuration%20reference/)
provides `Convolution: <file>` (any libsndfile format, rate must match the device,
multichannel IRs mapped to the selected channels in order) and `Copy:` for
weighted channel mixes. So:

- **Recommended:** add an export that writes an Equalizer APO configuration for
  `.cfg` files it can express: per-output convolution with copy-matrix input mixing
  and delays. Explain clearly which routes cannot be expressed. This is in the
  roadmap's "converter to CamillaDSP / Equalizer APO" idea, tracked under CFG-1/HOST-3.
- **Not recommended now:** our own APO. Revisit only if users need configurations
  the export cannot express and accept the installation fragility.
- Equalizer APO also hosts VST plug-ins. Check whether its current release can
  load our VST3 build before promising that route.

## Testing

Hosted CI runners have no audio device, so CI tests are offline. Playback hosts
are checked manually and recorded like HOST-1.

| Layer | Test | Runs on |
| --- | --- | --- |
| `StreamTransform` | Latency compensation equals a direct double-precision oracle (worst error reported as for other DSP changes); variable input sizes including 0 and 1; flush, drain and discontinuity; tail length; chunking above `max_frames`; type selection and rejection diagnostics; allocation guard in process calls | Linux, macOS, Windows (sanitizers where available) |
| COM shell | Create through `DllGetClassObject` without registration; full `IMFTransform` contract: stream counts, type enumeration, set/clear types, `GetInputStatus`, `MF_E_NOTACCEPTING`, drain/flush messages, timestamps and durations, PCM 16/24/32 input; same for `IMediaObject` | Windows |
| Registration | `regsvr32 /s` on the runner (administrator), then `MFTEnumEx` finds it with the advertised types; `MFTGetInfo`; unregister removes it | Windows |
| Pipeline | Source Reader with `AddTransformForStream`: WAV in, effect, Sink Writer WAV out; compare with the CLI render of the same `.cfg` (expect identical samples, since it is the same engine and block size) | Windows |
| WinRT | C++/WinRT test executable with a registry-free manifest: `MediaTranscoder.AddAudioEffect` file to file, configuration via `IPropertySet` | Windows |
| Hosts (manual) | Windows Media Player Legacy on a clean Windows 11 VM (enable optional feature, enable plug-in, play 44.1/48 kHz stereo and 5.1, mismatched file shows diagnostic, filter change without restart); MPC-HC with the DMO; a MediaPlayer sample app | Recorded matrix |

Notes for Windows CI:

- Buffering tests must include one-frame output buffers, repeated partial output,
  input samples larger than `max_frames`, back-pressure, zero/one-frame and
  shorter-than-latency input, flush/discontinuity, and repeated drain. Repeat
  portable timing tests with non-default block sizes. Negotiation tests must
  include stereo-to-5.1, absent output masks and same-count/different-layout
  cases, without inferring input labels from the output mask.

- Windows Server runner images do not include Media Foundation. Add
  `Install-WindowsFeature Server-Media-Foundation` before the tests
  ([example](https://zenn.dev/shimat/articles/d9ed0345c9866e)). Check on the first
  run whether a reboot is required.
- Use `mftrace` (Windows SDK) when diagnosing pipeline failures. The Windows HLK
  MFT tests target hardware certification and are not planned for CI.
- Build with the same static triplet as the VST3 job (`x64-windows-static-md`).

## Distribution and signing

- **Installer:** reuse the REL-1 release machinery (NSIS or MSI per the
  [distribution plan](2026-10-18-plugin-distribution.md)). Install to
  `%ProgramFiles%\Convolver\MediaFoundation\<arch>\` and register elevated.
  Uninstall unregisters before deleting. Per-user installs cannot register MFT
  categories machine-wide, so offer them only for application developers.
- **Dependencies:** statically link FFTW (or build with Ooura) and libsndfile, as
  the VST3 build does. A DLL loaded into someone else's process must not depend on
  that process's DLL search path.
- **Signing:** Authenticode-sign the DLL and installer with a timestamp. No driver
  or WHQL signing is needed, unlike an APO package. Signing helps SmartScreen and
  administrators; it does not admit the MFT to the Protected Media Path.
- **Licence:** a GPL-2.0-or-later DLL loaded by proprietary players is the same
  situation as the historical WMP plug-in and the VST3. Ship the corresponding
  source with each binary (REL-1).

## Phases and exit criteria

| Phase | Work | Done when |
| --- | --- | --- |
| 0. Need | Name the target: WMP Legacy users, DirectShow players, developers, or system-wide. Prototype the Equalizer APO export if system-wide. | A recorded decision in TODO HOST-3 |
| 1. Portable layer | `StreamTransform` in the host library, tests on all CI platforms | Oracle and allocation tests pass; worst error reported |
| 2. MFT DLL | WRL shell, media types, attributes, registration; Windows CI with Media Foundation | Contract, registration and Source Reader tests pass in CI |
| 3. Players | `IWMPPluginEnable`, `IMFGetService`, property page, `IMediaObject`, WMP and DMO registration | Manual WMP Legacy and MPC-HC results recorded |
| 4. WinRT | `IMediaExtension`, `DllGetActivationFactory`, sample app and transcoder test | WinRT test passes in CI; sample documented |
| 5. Release | Signed x64/ARM64 installer via REL-1; user guide `docs/media-foundation.md` | Clean-machine install, register, play, uninstall |

Phases 1–2 are useful alone, to developers. Phase 3 is what end users would see.

## Comparison with the historical plug-in

The historical ConvolverWMP (`historical/convolverWMP/`) was an ATL DMO with
`IWMPPluginEnable`, `ISpecifyPropertyPages`, `IPropertyBag` and a custom
`IConvolver` interface. It registered through the WMP registrar and `DMORegister`
for PCM and float, with `ThreadingModel=Apartment`, and kept settings under
`HKCU\Software\Convolver\DSP Plugin`. ConvolverFilter and ConvolverWrapper served
DirectShow hosts. What changes:

| Historical | Planned |
| --- | --- |
| DMO only. The Media Foundation pipeline wrapped it | Native MFT plus DMO in one object |
| ATL, Visual Studio 2005 | WRL, current MSVC, CMake, CI |
| Output silent for the lag of half a partition; tail cut off | Latency compensated; tail emitted on drain |
| Rebuilt the filter under the audio lock; restart often needed after a change | Off-thread preparation and crossfade via `HostProcessor` |
| Mismatched format left the plug-in silently disengaged | Rejection plus a visible explanation |
| Integer output with dither/noise shaping | Float output; the audio engine converts |
| File path in settings | Path or self-contained state blob (as VST3 presets) |

## Open questions and risks

- **Demand:** the only zero-code end-user host is a legacy player that Microsoft is
  winding down. Phase 0 must establish a real user before Phase 3.
- **Topology behaviour:** whether a given host lets the topology loader insert a
  resampler before an effect, or simply skips it, varies. Verify per host and
  document what users see.
- **Long reverb tails** on drain may be cut by hosts that stop at end of stream.
  Check in the Legacy player.
- **Per-user registration:** we found no per-user equivalent of `MFTRegister`.
  Verify before promising non-administrator installs.
- **Speaker masks** share the open VST3-1 decision. Settle it once for both adapters.
