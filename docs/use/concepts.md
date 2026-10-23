<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Audio and processing concepts

[Using Convolver](README.md)

For anyone who wants to understand the terms the other pages use: what a
filter does to audio, latency, tails, and files versus live streams.

## What goes in and what comes out

Convolver takes audio samples and filter coefficients, then produces filtered
audio. For one channel, each output sample is the sum of recent input samples
multiplied by the corresponding filter coefficients. This is **convolution**.
A **finite impulse response (FIR)** filter has a finite number of coefficients.

For example, the two coefficients `[1, 0.5]` produce the current input plus half
of the previous input. A single input sample of value 1 followed by silence
therefore produces 1, then 0.5, then silence, after the processing delay.
A real room-correction filter typically has many more coefficients.

With multiple channels, a configuration specifies **paths**. Each path mixes
its selected inputs, applies one IR channel, and adds the result to its selected
outputs. Paths can reuse one IR file. Outputs add contributions from all paths;
they are not automatically normalized.

## Vocabulary

| Term | Meaning in this repository |
| --- | --- |
| Sample | One numeric value for one audio channel at one instant |
| Frame | One sample for every channel at the same instant; stereo has two samples per frame |
| Sample rate | Frames per second, such as 48,000 Hz |
| IR or coefficients | The finite sequence that defines the filter; stored in a sound file or in memory |
| Interleaved | Samples ordered by frame: left, right, left, right, … |
| Planar | One separate sample buffer per channel, as used by many plug-in hosts |
| Block size B | The number of frames the engine processes as one convolution partition |
| FFT | Fast Fourier transform; used to calculate long convolutions efficiently |
| FFT plan | Prepared data or a selected algorithm for a transform of a particular size |
| FFTW wisdom | FFTW's saved plan information; useful for avoiding measured planning at startup |
| Latency | Delay introduced by processing: B frames in the command-line tool before it trims them, 256 in the plug-ins |
| Tail | FIR and routing-delay output that can remain after input ends; excludes processing latency |
| PCM | Pulse-code modulation; the CLI's PCM format names refer to integer samples, such as 16-bit or 24-bit WAV |
| Dither | Low-level noise added before integer rounding to reduce signal-dependent quantization distortion |
| Noise shaping | Feedback that changes the frequency distribution of quantization error |
| DAW / host | A digital audio workstation, or another application that schedules audio processing and loads plug-ins |

At 48 kHz, 256 frames of latency is about 5.33 ms. That is Convolver's delay;
audio devices and the host can add more. A smaller block reduces this delay but
can increase CPU work. FFTW planning does not choose the block size for you.

## Files and continuous streams

The CLI's normal file render removes the initial processing delay and writes the
complete tail. The output therefore lines up with the input but can be longer.
`--keep-latency` retains the initial delay. An empty input produces no audio frames.

The plug-ins, and the C++ library's engine, produce one output frame for every
input frame. They keep history between calls, even when the host's buffer
length changes. To let the tail play out at the end of a stream, keep feeding
silence for the latency plus the tail.

The CLI can also read and write raw (headerless) audio through pipes. The producer and
consumer must agree on rate, channel count and sample representation, and own any
audio devices or timing. The CLI itself is not an audio-device application.

## Sample rates, channels and level

An IR's sample rate must match the configured processing rate. A direct stereo
IR requires stereo input; a mono IR is not automatically copied to stereo.
Use a [configuration](configuration.md) to share a mono IR between channels.
Convolver does not resample or design a replacement IR automatically.

Floating-point samples normally use -1 to +1 as full scale, but a float WAV can
store larger finite values. Filters and summed routes can raise the level.
Integer output clips values outside its representable range. Use explicit gain
and inspect the result; dither is not a limiter. The [command-line reference](command-line.md#gain-and-attenuation)
explains fixed gain and the two automatic attenuation estimates.

## How long filters are calculated

The engine divides each IR into B-frame partitions, transforms them with an FFT,
and combines them with a history of transformed input blocks. It converts the
result back to samples and adds the part overlapping the next block. This is
**uniform partitioned overlap-add**.

For developers, the [implementation map](../develop/architecture.md#processing-data-flow)
shows where those steps live, and the [FFT guide](../develop/fft-backends.md)
explains transform scaling, backend choices and performance measurements.
