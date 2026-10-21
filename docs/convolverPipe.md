# convolverPipe

[History](history.md) · [Documentation index](README.md)

This page documents a **historical** Windows tool kept in `convolver/`. For
streaming with the current portable CLI, see
[RAW streams](portable-core.md#raw-streams).

`convolverPipe` filters interleaved audio from standard input and writes the
convolved audio to standard output. It uses the same engine and filter/config
format as the plug-ins. This is the recovered 4.4 command-line interface with
local correctness fixes; its Windows executable has not yet been built and
validated from this repository.

## Build

Open `convolver/convolver.sln` and select the `convolverPipe` project. Its
historical Visual Studio 2005 project is registered for Debug, Release and
Release PIII and includes the recovered `convolution/wavefile.cpp` format tables.
It expects the existing Windows/ATL, Boost, FFTW and libsndfile build environment;
its original project also retains DirectX library and Intel compiler search
paths. These will need adapting to the installed SDKs. This recovery is not a
modern cross-platform build-system port. The old installer has not been rebuilt
or modified to package the recovered tool.

## Command line

```text
convolverPipe nPartitions nTuningRigour fAttenuationDB nFilterLengths MajorFormatIn MinorFormatIn MajorFormatOut MinorFormatOut config.txt|IR.wav
```

| Argument | Meaning |
| --- | --- |
| `nPartitions` | `0` selects ordinary overlap-save; `1`–`4096` select partitioned processing. More partitions reduce the processing block size but add overhead |
| `nTuningRigour` | For FFTW: `0` estimate, `1` measure, `2` patient, `3` exhaustive, `4` time-limited |
| `fAttenuationDB` | Gain in dB: `-6` attenuates, `0` leaves level unchanged, positive values amplify. Invalid/non-finite values and values that overflow the float gain are rejected |
| `nFilterLengths` | Positive integer controlling the requested I/O batch size, not how much tail is emitted. Individual input/output buffers are capped at 1 MiB |
| Format pairs | Case-insensitive libsndfile container and sample-format names, such as `RAW FLOAT`, `RAW PCM_16`, `RAW PCM_24`, or `WAV PCM_16`. The library validates supported output combinations |
| Filter | An impulse response or configuration selecting one filter configuration. That configuration may contain several routes/channels |

Running without arguments prints usage and the historical format-name lists.
Input channel count and sample rate must match the filter configuration; the
tool does not resample. For RAW input those values come from the configuration.
For headered input, libsndfile detects the format and reads the actual metadata.
Output channel count and sample rate also come from the configuration.

Windows `cmd.exe` example, assuming `input.f32` contains interleaved little-endian
32-bit float samples at the channel count and rate specified by `filters.txt`:

```bat
convolverPipe.exe 2 0 -6 1 RAW FLOAT RAW FLOAT filters.txt < input.f32 > output.f32
```

The program switches stdin/stdout to binary mode. Audio goes only to stdout;
diagnostics go to stderr. Use a shell/redirection path that preserves binary
bytes. For non-seekable pipelines, RAW is the straightforward choice: headered
formats may require seeking or final header updates, depending on the format
and libsndfile implementation. Errors opening or finalizing those streams are
reported; the full format-name list is not a promise that every format works on
a pipe. Downstream RAW consumers must use the configured output channels, rate
and sample format.

## EOF, latency and errors

Unlike the archived implementation, the restored tool flushes the complete
convolution tail on clean EOF. It **retains the engine's initial latency**; it
does not trim or time-align the output. For nonempty input of `N` frames, output
length is:

```text
N + B + max_over_connected_paths(IR_frames - 1 + max_input_delay + max_output_delay)
```

Here `B` is the half-partition length; delays and maxima are taken over each
path's actual input/output routes. IR length comes from the original impulse
response, not its FFT padding. This is the full support length, including any
trailing zero taps in the file. Empty input produces zero audio frames, though a
headered output may still contain its format header. The tool continues reading
after partial batches and flushes only on clean EOF.

The exit status is zero on success and nonzero on invalid arguments, mismatched
input metadata, allocation/processing errors, read errors, short writes or
reported close errors. A failed stream may already contain partial output.

## Historical usage and validation

The [archived pipe guide](historical/convolver-4.4/convolverpipe.html) and
[custom-convert.conf](historical/convolver-4.4/custom-convert.conf) preserve the
original SlimServer/SqueezeCenter integration examples. Their paths, host
versions and command recipes are historical references; compatibility with
current media servers has not been established.

Run `python3 tests/regression/run_all.py` for portable regression checks. They
exercise the actual pipe control flow with simulated streams, the real audio
wrapper with libsndfile boundary stubs, and the real extracted convolution
engine against time-domain reference results. They do not establish a native
Windows executable build, real descriptor/codec behaviour or real-time
performance. See [recovery findings](review-2026-10/archive-recovery.md).
