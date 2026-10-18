#!/usr/bin/env python3
"""End-to-end CLI checks through real FFTW and libsndfile, no Python packages."""
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

EXE = str(Path(sys.argv[1]).resolve())

def pcm(path, values, channels=1, rate=48000):
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(channels); f.setsampwidth(2); f.setframerate(rate)
        f.writeframes(struct.pack('<%dh' % len(values), *values))

def floats(path):
    data = Path(path).read_bytes()
    assert data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE'
    pos = 12; payload = None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt, channels, rate = struct.unpack_from('<HHI', data, pos + 8)
            assert fmt in (3, 65534), fmt
        if tag == b'data': payload = data[pos+8:pos+8+size]
        pos += 8 + size + (size & 1)
    assert payload is not None
    return channels, rate, list(struct.unpack('<%df' % (len(payload)//4), payload))

def near(actual, expected):
    assert len(actual) == len(expected), (len(actual), len(expected))
    for i, (a,b) in enumerate(zip(actual, expected)):
        assert math.isfinite(a) and abs(a-b) <= 3e-5 + 3e-5*abs(b), (i,a,b)

def call(args, ok=True, data=None):
    p = subprocess.run([EXE, *map(str,args)], input=data, capture_output=True)
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stderr.decode(errors='replace'))
    return p

def channel_mask(path):
    """dwChannelMask from an extensible fmt chunk, parsed without libsndfile; None if plain."""
    data = Path(path).read_bytes(); pos = 12
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt = struct.unpack_from('<H', data, pos + 8)[0]
            return struct.unpack_from('<I', data, pos + 28)[0] if fmt == 0xfffe else None
        pos += 8 + size + (size & 1)
    raise AssertionError('no fmt chunk')

def speaker_map_tests(d):
    """--speaker-map config labels WAV channels with the configuration's mask."""
    ir=d/'sm-ir.wav'; src=d/'sm-in.wav'; out=d/'sm-out.wav'
    pcm(ir,[16384]); pcm(src,[8192,-4096,2048,-1024,4096,1024],channels=2)
    def config(mask, outputs):
        # Input 0 feeds output o with weight 0.(o+1), so each channel is distinguishable.
        cfg=d/'sm.cfg'
        cfg.write_text('48000 2 %d %x\n0 0\n%s\n' % (outputs, mask, ' '.join('0'*outputs)) +
                       ''.join('sm-ir.wav\n0\n0\n%d.%d\n' % (o, o+1) for o in range(outputs)))
        return cfg
    for mask, outputs, fmt in ((0x3,2,'wav-float'), (0x3f,6,'wav-pcm24'), (0x3f,6,'wav-float')):
        cfg=config(mask, outputs)
        base=['--filter',cfg,'--input',src,'--output',out,'--output-format',fmt]
        call(base); plain=out.read_bytes(); assert channel_mask(out) is None
        call(base+['--speaker-map','config']); assert channel_mask(out)==mask, hex(channel_mask(out) or 0)
        if fmt=='wav-float':
            ch,rate,labelled=floats(out); assert ch==outputs
            out.write_bytes(plain); assert floats(out)[2]==labelled, 'labels must not reorder samples'
    # Zero mask: an unlabelled file and a note; mismatched masks and RAW fail before output.
    cfg=config(0,2)
    p=call(['--filter',cfg,'--input',src,'--output',out,'--speaker-map','config'])
    assert channel_mask(out) is None and b'no speaker mask' in p.stderr
    for args in ([config(0x7,2)], [config(0x3,2), '--output-format', 'raw-float'],
                 [config(0x40001,2)]):
        out.unlink(missing_ok=True)
        p=call(['--filter',args[0],'--input',src,'--output',out,'--speaker-map','config',*args[1:]],ok=False)
        assert not out.exists() and b'speaker' in p.stderr, p.stderr
    call(['--filter',config(0x3,2),'--input',src,'--output',out,'--speaker-map','wave'],ok=False)

def main():
    call(['--help']); assert b'FFT backend:' in call(['--version']).stdout
    with tempfile.TemporaryDirectory(prefix='convolver-cli-') as t:
        d=Path(t); src=d/'entrée.wav'; ir=d/'ir.wav'; dst=d/'sortie ü.wav'
        for count in (0,1,16,17,64):
            x=[((i*379)%12001)-6000 for i in range(count)]
            h=[16384,-8192,4096,0,2048,0,1024]
            pcm(src,x); pcm(ir,h)
            expected=[0.]*(count+len(h)-1) if count else []
            for i,v in enumerate(x):
                for j,w in enumerate(h): expected[i+j] += v*w/(32768.**2)
            base=['--filter',ir,'--input',src,'--output',dst,'--block-size','8']
            call(base); channels,rate,y=floats(dst)
            assert (channels,rate)==(1,48000); near(y,expected)
            call(base+['--keep-latency']); near(floats(dst)[2], ([0.]*8+expected) if count else [])
        # Delay/routing/negative-zero and relative IR path, distinct I/O counts.
        pcm(ir,[16384,8192]); pcm(src,[8192,4096,-4096,2048],channels=2)
        cfg=d/'matrix.cfg'
        cfg.write_text('48000 2 3 0\n0 0.020833333333333333\n0 0 0\nir.wav\n0\n-0 1.5\n0 2.5\n')
        call(['--filter',cfg,'--input',src,'--output',dst,'--block-size','3'])
        ch,rate,y=floats(dst); assert ch==3
        # Exact delay truncation belongs to parser tests; use a zero-delay config here.
        cfg.write_text('48000 2 3 0\n0 0\n0 0 0\nir.wav\n0\n-0 1.5\n0 2.5\n')
        call(['--filter',cfg,'--input',src,'--output',dst,'--block-size','3'])
        mixed=[-.25+.5*.125, .125+.5*.0625]
        z=[mixed[0]*.5,mixed[1]*.5+mixed[0]*.25,mixed[1]*.25]
        near(floats(dst)[2], [v for s in z for v in (s,0,s*.5)])
        # True pipes: explicit float32 LE metadata, no seekable header dependency.
        pcm(ir,[16384,8192]); raw=struct.pack('<3f',.25,-.5,.75)
        args=['--filter',ir,'--input','-','--raw-input','48000','1','--output','-',
              '--output-format','raw-float','--block-size','2']
        p=call(args,data=raw)
        near(struct.unpack('<4f',p.stdout), [.125,-.1875,.25,.1875])
        p=call(args+['--gain-db','6.020599913279624'],data=raw)
        near(struct.unpack('<4f',p.stdout), [.25,-.375,.5,.375])
        # EOF after full/partial read buffers must retain every frame and tail.
        for count in (0, 1, 4096, 4097):
            payload=struct.pack('<%df' % count, *([.25]*count))
            p=call(args,data=payload)
            expected=([.125]+[.1875]*(count-1)+[.0625]) if count else []
            near(struct.unpack('<%df' % (len(p.stdout)//4),p.stdout),expected)
        # Normal pipe EOF must not hide incomplete samples or nonfinite input.
        for payload in (b'\0', raw+b'\0', struct.pack('<f',float('nan'))):
            call(args,ok=False,data=payload)
        # A producer may split float samples across separate writes.
        with subprocess.Popen([EXE,*map(str,args)],stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE) as p:
            for byte in raw:
                p.stdin.write(bytes([byte])); p.stdin.flush()
            p.stdin.close(); p.stdin=None
            output,errors=p.communicate(timeout=30)
            assert p.returncode==0, errors
            near(struct.unpack('<4f',output),[.125,-.1875,.25,.1875])
        # Referenced IRs and nested config/list files must never be overwritten.
        original_ir=ir.read_bytes()
        call(['--filter',cfg,'--input',src,'--output',ir],ok=False)
        assert ir.read_bytes()==original_ir
        listing=d/'filters.txt'; listing.write_text('matrix.cfg\n')
        original_cfg=cfg.read_bytes()
        call(['--filter',listing,'--input',src,'--output',cfg],ok=False)
        assert cfg.read_bytes()==original_cfg
        # stdin can be a regular file; opening that same path as output is destructive.
        raw_path=d/'source.raw'; raw_path.write_bytes(raw)
        with raw_path.open('rb') as redirected:
            p=subprocess.run([EXE,'--filter',str(ir),'--input','-',
                              '--raw-input','48000','1','--output',str(raw_path),
                              '--output-format','raw-float'],stdin=redirected,capture_output=True)
        assert p.returncode!=0 and raw_path.read_bytes()==raw
        # Errors must be failures, not successful silent output.
        for extra in (['--block-size','0'], ['--block-size','-1'], ['--gain-db','nan'], ['--unknown']):
            call(['--filter',ir,'--input',src,'--output',dst]+extra,ok=False)
        call(['--filter',ir,'--input',src,'--output',src],ok=False)
        call(['--filter',ir,'--input','-','--output',dst],ok=False,data=raw)
        call(['--filter',ir,'--input','-','--raw-input','44100','1','--output',dst],ok=False,data=raw)
        call(args,data=b'')
        speaker_map_tests(d)
    print('PASS: native CLI files, matrices, EOF, gain, errors and real RAW pipes')

if __name__=='__main__': main()
