# SPDX-License-Identifier: GPL-2.0-or-later
"""Re-use the repository extractor, sourcing DSP and float I/O from the 4.3 tag."""

import pathlib
import subprocess
import sys

repo, out = map(pathlib.Path, sys.argv[1:])
old = 'ee3323f2078846fef8c30da19771820f3f2f793d'


def historical(name):
    return (
        subprocess.check_output(['git', '-C', str(repo), 'show', old + ':convolver/convolution/' + name])
        .decode('latin-1')
        .replace('\r\n', '\n')
    )


extractor = repo / 'tests/regression/core_regressions.py'
code = extractor.read_text().replace(
    "sample = read('sample.h')", "sample = (ROOT/'historical/convolution/sample.h').read_text(encoding='latin1')"
)
ns = {'__file__': str(extractor), '__name__': 'extract_only'}
exec(compile(code, str(extractor), 'exec'), ns)  # noqa: S102 - extractor from the pinned local Git revision
s = ns['generate'](old)
s = s[: s.index('// Independent linear convolution oracle')]
s = s[: s.index('using fftwf_complex =')] + s[s.index('struct SF_INFO') :]
s = s.replace('#define FFTW 1', '#include <fftw3.h>\n#define FFTW 1')
s = s.replace(
    '#define ALIGNED_MALLOC(n) std::malloc(n)',
    """// The old FastArray copy reads FFTW's two padding floats when assigning buffers of unequal size.
inline void* padded(size_t n) {
    const auto bytes = n + 2 * sizeof(float);
    void* p = fftwf_malloc(bytes);
    if (!p) throw std::bad_alloc();
    std::memset(p, 0, bytes);
    return p;
}
#define ALIGNED_MALLOC(n) padded(n)""",
).replace('#define ALIGNED_FREE(p) std::free(p)', '#define ALIGNED_FREE(p) fftwf_free(p)')
s = s.replace(
    'Flag[5]={0,0,0,0,0}',
    'Flag[5]={FFTW_ESTIMATE|FFTW_DESTROY_INPUT,FFTW_MEASURE|FFTW_DESTROY_INPUT,FFTW_PATIENT|FFTW_DESTROY_INPUT,FFTW_EXHAUSTIVE|FFTW_DESTROY_INPUT,FFTW_PATIENT|FFTW_DESTROY_INPUT}',
)
s = s.replace('struct PathFixture', 'static unsigned fixture_rate=44100;\nstruct PathFixture').replace(
    'n,0,48000,rigor', 'n,0,fixture_rate,rigor'
)
sample = historical('sample.h')
io = sample[
    sample.index('template <typename T>\nstruct __declspec(novtable) __single_inheritance SampleIO') : sample.index(
        'template <typename SampleT, typename T>\nclass VSTIO'
    )
]
io = (
    io.replace('__declspec(novtable) ', '')
    .replace('output_sample_convertor_->', 'this->output_sample_convertor_->')
    .replace('input_sample_convertor_->', 'this->input_sample_convertor_->')
)
start = sample.index('void GetSample(', sample.index('struct ConvertSample_ieeefloat :'))
end = sample.index('nBytesGenerated += sizeof(float);', start) + len('nBytesGenerated += sizeof(float);')
conv = (
    'using BYTE=unsigned char;\n'
    'template<class T> struct ConvertSample { unsigned nContainerSize() const {return sizeof(float);} '
    + sample[start:end]
    + '\n}\n};\n'
)
a = s.index('template<class T> struct ConvertSample')
b = s.index('template <typename T>\nclass Convolution', a)
s = s[:a] + conv + io + s[b:]
out.write_text(s)
# Compile the archived attenuation routine behind perftest's SecCalc/Rate.
oldcpp = historical('convolution.cpp')
a = oldcpp.index('template <typename T>\nHRESULT Convolution<T>::calculateOptimumAttenuation')
b = oldcpp.index('template <typename T>\nConvolutionList<T>::ConvolutionList', a)
fn = oldcpp[a:b]
a = fn.index('// This is a typedef for a random number generator.')
b = fn.index('float maxSample = 0;', a)
fn = fn[:a] + 'bool again=true;\n' + fn[b:]
fn = fn.replace(
    'std::generate_n(InputSamples.begin(), nInputBufferLength, uni);',
    'archival_noise(InputSamples.data(), nInputBufferLength);',
)
a = fn.index('DWORD nBytesGenerated = overlapsave')
b = fn.index('assert (nBytesGenerated', a)
fn = (
    fn[:a]
    + """InterleavedIO<BaseT> io(reinterpret_cast<BYTE*>(InputSamples.data()),
        reinterpret_cast<BYTE*>(OutputSamples.data()), convertor.get_ptr(), convertor.get_ptr());
    DWORD nBytesGenerated = overlapsave && nPartitions_ == 1
        ? doConvolution(io,nBlocks,0) : doPartitionedConvolution(io,nBlocks,0);
    """
    + fn[b:]
)
with out.open('a') as f:
    f.write(
        """
extern "C" void archival_noise(float*, size_t);
using std::abs;
constexpr bool TRUE=true, FALSE=false;
constexpr int S_OK=0, NSAMPLES=10, MAX_ATTENUATION_DB=1000;
template<class T> using ConvertSample_ieeefloat = ConvertSample<T>;
template<class T> struct Holder { std::unique_ptr<T> p; explicit Holder(T* x):p(x) {} T* get_ptr() {return p.get();} };
"""
        + fn
    )
