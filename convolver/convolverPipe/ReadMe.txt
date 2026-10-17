convolverPipe
=============

Restored from the historical 4.4 source tree in homes.zip:
  homes/DOMAIN=JOHNPAVEL/jrp/My Documents/Visual Studio 2008/Projects/
  convolver/convolver-/convolverPipe/

The source, Visual Studio 2005 project, and 4.4 version resource were recovered
from that tree. Archive timestamps were not used to choose the version.
Generated build outputs, user settings and source-control bindings are omitted.
Copyright (C) 2006 John Pavel. GNU GPL v2 or later; see ../../COPYING.

The original nine-position command line is retained:
  convolverPipe nPartitions nTuningRigour fAttenuationDB nFilterLengths
               MajorFormatIn MinorFormatIn MajorFormatOut MinorFormatOut
               config.txt|IR.wav

See ../../docs/convolverPipe.md for usage and format limitations.

Recovery repairs
----------------
Numeric arguments are checked before constructing the engine. Zero partitions
selects overlap-save; 1 through 4096 selects partitioned convolution. The cap
bounds the allocation multiplier supplied directly on the command line.
nFilterLengths must be positive; it requests a processing batch size, internally
limited to 1 MiB per float buffer. Large valid batch counts therefore do not
allocate enormous buffers. DWORD engine byte counts are checked too.

Attenuation retains the engine's convention: negative dB attenuates and positive
dB amplifies. The value and computed float gain must be finite and within the
engine's range. Audio itself is not clipped by this front end.

Windows stdin/stdout are switched to binary mode. RAW input uses the filter
configuration's channels and sample rate. Headered input must match them.
Read errors, short writes, stream errors and close failures return failure.

For nonempty input, output retains the engine's initial block latency and flushes
the full connected convolution tail. The duration uses actual impulse-response
frames and connected channel delays, not padded FFT lengths. Empty input writes
no audio frames. Codec headers may still be emitted by the chosen output format.

Portable tests compile the actual main and helper functions with stream adapters;
the separate pipe DSP tests run the tail helper with the production FFT engine
bodies through the portable DFT adapter. They do not constitute a native Windows
build or validation of real libsndfile codecs and nonseekable standard streams.
