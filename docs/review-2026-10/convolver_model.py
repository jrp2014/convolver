"""Line-by-line Python model of Convolver 4.3's Filter + Convolution::doPartitionedConvolution
(single path, mono), used to check the algorithm against a direct convolution.

Usage: python3 -I model.py <path to convolution/ffthelp.cpp>
"""
import re, sys
import numpy as np

table = [int(n) for n in re.findall(r'\d+', open(sys.argv[1], encoding='latin-1').read()
         .split('OptimalDFTSize[]  =')[1].split('};')[0])]

def optimal(n):
    return next(t for t in table if n <= t)

def make_filter(h, P, fix_gap=False):
    """Filter::Filter. Returns (coeff spectra, L=half padded partition length)."""
    M = len(h)
    nHalf = (M + P - 1) // P
    if nHalf == 1:
        nHalf = 2
    L = optimal(nHalf)
    N = 2 * L
    if fix_gap:
        nHalf = L               # proposed fix: slice the IR at the padded block size
    coeffs = np.zeros((P, N // 2 + 1), dtype=complex)
    for p in range(P):
        seg = h[p * nHalf:(p + 1) * nHalf]
        buf = np.zeros(N)
        buf[:len(seg)] = seg      # samples at [0,nHalf), zero padding to N
        coeffs[p] = np.fft.rfft(buf) / N
    return coeffs, L

def run(x, h, P, in_delay=0, out_delay=0, fix_gap=False, fix_delay=False):
    coeffs, L = make_filter(h, P, fix_gap)
    N = 2 * L
    inbuf = np.zeros(N)
    outacc = np.zeros(N)
    comp = np.zeros((P, N // 2 + 1), dtype=complex)  # ComputationCircularBuffer_
    idx = 0; pidx = 0; prev = P - 1
    dline = np.zeros(in_delay + 1)  # used only by the proposed delay fix
    y = np.zeros(len(x))
    for t, s in enumerate(x):
        # TransferFrame: output first ...
        y[t] = outacc[(idx - out_delay) % N]
        # ... then input, written "ahead" by the input delay
        if fix_delay:
            dline = np.roll(dline, 1); dline[0] = s
            inbuf[idx] = dline[in_delay]
        else:
            inbuf[(idx + in_delay) % N] = s
        if idx == L - 1 or idx == N - 1:
            idx = (idx + 1) % N
            outacc[idx:idx + L] = 0
            comp[prev] = 0
            # mix_input: [samples idx..N) then [0..idx)  -> [old, new]
            acc = np.concatenate([inbuf[idx:], inbuf[:idx]])
            X = np.fft.rfft(acc)        # numpy rfft, like FFTW r2c, is unnormalised
            for p in range(P):
                comp[pidx] += X * coeffs[p]
                pidx = (pidx + 1) % P
            out = np.fft.irfft(comp[pidx], N) * N  # FFTW c2r is unnormalised
            outacc[idx:idx + L] += out[L:]
            prev = pidx
            pidx = (pidx + 1) % P
        else:
            idx += 1
    return y, L

def check(label, M, P, in_delay=0, out_delay=0, **fix):
    rng = np.random.default_rng(1)
    h = rng.standard_normal(M) * np.exp(-np.arange(M) / (M / 4))
    x = rng.uniform(-1, 1, 6 * M + 4000)
    y, L = run(x, h, P, in_delay, out_delay, **fix)
    ref = np.convolve(x, h)[:len(x)]
    ref = np.concatenate([np.zeros(L + in_delay + out_delay), ref])[:len(x)]
    start = 2 * M + 4 * L  # skip start-up
    err = np.max(np.abs(y[start:] - ref[start:])) / np.max(np.abs(ref[start:]))
    print(f"{label:58s} taps={M:6d} P={P:2d} L={L:6d}  max rel err = {err:.1e}")

if __name__ == '__main__':
    print("-- clean cases")
    check("power-of-2 filter, 1 partition", 8192, 1)
    check("power-of-2 filter, 4 partitions", 8192, 4)
    check("power-of-2 filter, 16 partitions", 8192, 16)
    check("odd length, 1 partition (padding harmless)", 8000 - 3, 1)
    check("output delay 37 samples", 8192, 4, out_delay=37)
    print("-- partition padding gap")
    check("3 partitions, half-partition padded up", 8192, 3)
    check("  ... with proposed fix", 8192, 3, fix_gap=True)
    check("7 partitions, odd length", 10007, 7)
    check("  ... with proposed fix", 10007, 7, fix_gap=True)
    print("-- input channel delay")
    check("input delay 37 samples, 1 partition", 8192, 1, in_delay=37)
    check("input delay 37 samples, 8 partitions", 8192, 8, in_delay=37)
    check("  ... with proposed fix", 8192, 8, in_delay=37, fix_delay=True)
