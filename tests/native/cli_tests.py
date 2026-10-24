#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""End-to-end CLI checks through real FFTW and libsndfile, no Python packages."""
import math
import re
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

def wave_fields(path):
    """(format tag, container bits, valid bits, mask, raw sample bytes) without libsndfile."""
    data = Path(path).read_bytes(); pos = 12; fields = None; payload = None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        if tag == b'fmt ':
            fmt, _, _, _, _, bits = struct.unpack_from('<HHIIHH', data, pos + 8)
            valid, mask = struct.unpack_from('<HI', data, pos + 26) if fmt == 0xfffe else (bits, 0)
            fields = (fmt, bits, valid, mask)
        if tag == b'data': payload = data[pos+8:pos+8+size]
        pos += 8 + size + (size & 1)
    return (*fields, payload)

def integer_format_tests(d):
    """Dither, noise shaping, PCM8 and valid bits through the CLI."""
    ir=d/'if-ir.wav'; src=d/'if-in.wav'; dst=d/'if-out.wav'
    pcm(ir,[32767]); pcm(src,[(((i*379)%12001)-6000) for i in range(3000)])
    base=['--filter',ir,'--input',src,'--output',dst]
    call(base+['--output-format','wav-pcm8'])
    fmt,bits,valid,mask,payload=wave_fields(dst)
    assert (fmt,bits,valid)==(1,8,8) and len(payload)==3000
    exact=[(((i*379)%12001)-6000)*32767/32768/256 for i in range(3000)]
    assert all(abs((b-128)-x)<=0.5+1e-6 for b,x in zip(payload,exact))
    for fmt_name,container in (('wav-pcm24',24),('wav-pcm32',32)):
        for shaping in ('simple','second-order','third-order','ninth-order','sony-sbm'):
            call(base+['--output-format',fmt_name,'--valid-bits','16','--dither','triangular',
                       '--noise-shaping',shaping])
            fmt,bits,valid,mask,payload=wave_fields(dst)
            assert (fmt,bits,valid,mask)==(0xfffe,container,16,0), (fmt,bits,valid,mask)
            width=container//8
            values=[int.from_bytes(payload[i:i+width],'little',signed=True)
                    for i in range(0,len(payload),width)]
            assert all(v % (1<<(container-16))==0 for v in values)
            assert values!=[0]*len(values)
    call(base+['--output-format','wav-pcm24','--dither','rectangular','--noise-shaping','none'])
    fmt,bits,valid,mask,_=wave_fields(dst); assert (fmt,bits,valid)==(1,24,24)

def automatic_gain_tests(d):
    """--gain-db auto/no-clip apply the attenuation estimates and report them."""
    ir=d/'ag-ir.wav'; src=d/'ag-in.wav'; dst=d/'ag-out.wav'
    pcm(ir,[8192]); values=[(((i*379)%12001)-6000) for i in range(3000)]; pcm(src,values)
    for mode in ('no-clip','auto'):
        p=call(['--filter',ir,'--input',src,'--output',dst,'--gain-db',mode])
        err=p.stderr.decode()
        # A 0.25 impulse: the largest possible peak and the noise peak are both ~0.25.
        db=float(re.search(r'attenuation (-?[0-9.]+) dB',err).group(1))
        assert abs(db-12.04)<0.02, err
        _,_,out=floats(dst)
        near(out,[v/32768*0.25*10**(db/20) for v in values]) if mode=='no-clip' else None
    call(['--filter',ir,'--input',src,'--output',dst,'--gain-db','loud'],ok=False)

def output_selection_tests(d):
    """--output-channels selects list entries by output count, as the historical engine did."""
    ir=d/'os-ir.wav'; src=d/'os-in.wav'; dst=d/'os-out.wav'
    pcm(ir,[16384]); pcm(src,[1000,-2000,3000])
    (d/'os-wide.cfg').write_text('48000 1 3 0\n0\n0 0 0\nos-ir.wav\n0\n0\n0 1 2\n')
    (d/'os-narrow.cfg').write_text('48000 1 1 0\n0\n0\nos-ir.wav\n0\n0\n0\n')
    (d/'os-list.txt').write_text('os-wide.cfg\nos-narrow.cfg\n')
    base=['--filter',d/'os-list.txt','--input',src,'--output',dst]
    call(base); assert floats(dst)[0]==3
    call(base+['--output-channels','1']); assert floats(dst)[0]==1
    call(base+['--output-channels','3']); assert floats(dst)[0]==3
    p=call(base+['--output-channels','2'],ok=False)
    assert b'output channel count' in p.stderr, p.stderr

def repeatability_tests(d):
    """Separate runs on the same input must write bit-identical samples.

    Each run is a new process, so its heap layout differs. Output must not depend
    on where buffers land, or exact comparisons between renders become flaky.
    """
    ir=d/'rep-ir.wav'; src=d/'rep-in.wav'; out=d/'rep-out.wav'
    pcm(ir,[(i*7919)%20001-10000 for i in range(300)])
    pcm(src,[(i*104729)%30001-15000 for i in range(2*3000)],channels=2)
    cfg=d/'rep.cfg'
    cfg.write_text('48000 2 6 3f\n0 0\n0 0 0 0 0 0\n' +
                   ''.join('rep-ir.wav\n0\n%d\n%d.%d\n' % (o%2, o, o+1) for o in range(6)))
    first=None
    for run in range(20):
        call(['--filter',cfg,'--input',src,'--output',out,'--output-format','wav-float'])
        samples=floats(out)[2]
        if first is None: first=samples
        differing=sum(a!=b for a,b in zip(samples,first))
        assert samples==first, 'run %d differs from run 0 in %d of %d samples' % (
            run, differing, len(first))

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

def container_tests(d):
    """--output-format CONTAINER-TYPE and --raw-format, end to end."""
    ir=d/'ct-ir.wav'; pcm(ir,[16384]); raw=struct.pack('<4f',.5,-.25,.125,-1.)
    base=['--filter',ir,'--raw-input','48000','1','--input','-']
    magic={'wav':b'RIFF','rf64':b'RF64','w64':b'riff','aiff':b'FORM','caf':b'caff','flac':b'fLaC'}
    # Preview packages build libsndfile without FLAC; --version says which.
    flac=b'(FLAC output available)' in call(['--version']).stdout
    for name in ('wav-double','rf64-pcm24','w64-pcm8','aiff-pcm16','aiff-float','caf-pcm32',
                 'caf-double','flac-pcm8','flac-pcm16','flac-pcm24'):
        out=d/('ct.'+name.split('-')[0]); out.unlink(missing_ok=True)
        if name.startswith('flac') and not flac:
            p=call(base+['--output',out,'--output-format',name],ok=False,data=raw)
            assert b'without FLAC support' in p.stderr and not out.exists(), p.stderr
            continue
        call(base+['--output',out,'--output-format',name],data=raw)
        assert out.read_bytes()[:4]==magic[name.split('-')[0]], name
    # The original format names still write the same files as before.
    for name, subtype, size in (('wav-pcm8',1,1),('wav-pcm16',1,2),('wav-pcm24',1,3),
                                ('wav-pcm32',1,4),('wav-float',3,4)):
        out=d/'legacy.wav'
        call(base+['--output',out,'--output-format',name],data=raw)
        b=out.read_bytes()
        assert b[:4]==b'RIFF' and b[8:16]==b'WAVEfmt ', name
        assert struct.unpack('<HHIIHH',b[20:36])==(subtype,1,48000,48000*size,size,8*size), name
    # Integer and double raw output, read back with the matching --raw-format.
    for name, fmt, size in (('raw-pcm16','s16le',2),('raw-pcm24','s24le',3),
                            ('raw-pcm32','s32le',4),('raw-double','f64le',8)):
        p=call(base+['--output','-','--output-format',name],data=raw)
        assert len(p.stdout)==4*size, (name, len(p.stdout))
        back=call(['--filter',ir,'--raw-input','48000','1','--raw-format',fmt,'--input','-',
                   '--output','-','--output-format','raw-float'],data=p.stdout)
        near(struct.unpack('<4f',back.stdout),[.125,-.0625,.03125,-.25])  # filtered twice
    assert call(base+['--output','-','--output-format','raw-pcm16'],data=raw).stdout[:2]==b'\0\x20'
    # s16 and s24 files whose size is not a multiple of 4 bytes keep every frame,
    # across a read-chunk boundary, from a file and from redirected stdin.
    for fmt, width in (('s16le',2),('s24le',3)):
        n=4097; f=d/('odd.'+fmt); f.write_bytes(b''.join(
            ((i%7-3)<<(8*width-4)).to_bytes(width,'little',signed=True) for i in range(n)))
        args=['--filter',ir,'--raw-input','48000','1','--raw-format',fmt,'--output','-',
              '--output-format','raw-float']
        p=call(args+['--input',f]); assert len(p.stdout)==4*n, (fmt, len(p.stdout))
        with open(f,'rb') as seekable:  # stdin redirected from a file
            p=subprocess.run([EXE,*map(str,args),'--input','-'],stdin=seekable,capture_output=True)
        assert p.returncode==0 and len(p.stdout)==4*n, (fmt, p.returncode, len(p.stdout))
        call(args+['--input','-'],data=f.read_bytes()+b'\0',ok=False)  # incomplete frame
    # Big-endian input in each type.
    for fmt, packed in (('s16be',struct.pack('>4h',16384,-8192,4096,-32768)),
                        ('s32be',struct.pack('>4i',1<<30,-(1<<29),1<<28,-(1<<31))),
                        ('f32be',struct.pack('>4f',.5,-.25,.125,-1.)),
                        ('f64be',struct.pack('>4d',.5,-.25,.125,-1.)),
                        ('s24be',b''.join(v.to_bytes(3,'big',signed=True)
                                          for v in (1<<22,-(1<<21),1<<20,-(1<<23))))):
        p=call(['--filter',ir,'--raw-input','48000','1','--raw-format',fmt,'--input','-',
                '--output','-','--output-format','raw-float'],data=packed)
        near(struct.unpack('<4f',p.stdout),[.25,-.125,.0625,-.5])
    out=d/'ct.flac'
    for bad in (['--output-format','flac-float'],['--output-format','raw-pcm8'],
                ['--output-format','flac-pcm32'],['--output-format','ogg-pcm16'],
                ['--output-format','wav'],['--raw-format','u8'],['--raw-format','s16'],
                ['--output-format','flac-pcm24','--speaker-map','config'],
                ['--output-format','aiff-pcm24','--valid-bits','20']):
        out.unlink(missing_ok=True)
        call(base+['--output',out,*bad],ok=False,data=raw); assert not out.exists(), bad
    call(['--filter',ir,'--input',d/'ct.aiff','--raw-format','s16le','--output',out],ok=False)
    # A non-raw stream on stdout names the raw formats.
    p=call(base+['--output','-','--output-format','aiff-pcm16'],ok=False,data=raw)
    assert b'raw output format' in p.stderr, p.stderr
    # Without FLAC, a FLAC input or IR says what is missing and what to do.
    if b'no FLAC output' in call(['--version']).stdout:
        fake=d/'ct-fake.flac'; fake.write_bytes(b'fLaC'+bytes(64))
        p=call(['--filter',ir,'--input',fake,'--output',d/'ct-fake.wav'],ok=False)
        assert b'built without FLAC' in p.stderr and b'flac --decode' in p.stderr, p.stderr
        (d/'ct-fake.cfg').write_text('48000 1 1 0\n0\n0\nct-fake.flac\n0\n0\n0\n')
        p=call(['--filter',d/'ct-fake.cfg','--input',d/'ct.aiff','--output',d/'ct-fake.wav'],
               ok=False)
        assert b'built without FLAC' in p.stderr and b'looked for' not in p.stderr, p.stderr
    # A DRC-style .pcm filter in a configuration.
    (d/'ct.pcm').write_bytes(struct.pack('<2f',.5,.25))
    cfg=d/'ct-raw.cfg'; cfg.write_text('48000 1 1 0\n0\n0\nct.pcm\n0\n0\n0\n')
    p=call(['--filter',cfg,'--raw-input','48000','1','--input','-','--output','-',
            '--output-format','raw-float'],data=raw)
    near(struct.unpack('<5f',p.stdout),[.25,0.,0.,-.46875,-.25])
    # The assumed sample type is reported, and --raw-filter-format changes it.
    assert b"raw filter 'ct.pcm' read as mono f32le at 48000 Hz" in p.stderr, p.stderr
    assert b'--raw-filter-format' in p.stderr, p.stderr
    (d/'ct.pcm').write_bytes(struct.pack('<2h',16384,8192))
    p=call(['--filter',cfg,'--raw-filter-format','s16le','--raw-input','48000','1','--input','-',
            '--output','-','--output-format','raw-float'],data=raw)
    near(struct.unpack('<5f',p.stdout),[.25,0.,0.,-.46875,-.25])
    assert b'read as mono s16le' in p.stderr, p.stderr
    p=call(['--filter',cfg,'--raw-filter-format','s16','--raw-input','48000','1','--input','-',
            '--output','-','--output-format','raw-float'],ok=False,data=raw)
    assert b'unsupported raw filter format' in p.stderr, p.stderr
    # A WAV that happens to be named .pcm is still read through its header.
    pcm(d/'ct.pcm',[16384,8192]); p=call(['--filter',cfg,'--raw-input','48000','1','--input','-',
                                          '--output','-','--output-format','raw-float'],data=raw)
    near(struct.unpack('<5f',p.stdout),[.25,0.,0.,-.46875,-.25])
    assert b'raw filter' not in p.stderr, p.stderr

def main():
    call(['--help'])
    version = call(['--version']).stdout
    assert re.fullmatch(rb'Convolver portable [0-9]+\.[0-9]+\.[0-9]+(?:\+g[0-9a-f]+(?:\.dirty)?)?', version.splitlines()[0]), version
    assert b'FFT backend:' in version
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
        # PCM16 output is plainly rounded by default; dither is opt-in and reproducible.
        pcm(ir,[13107]); pcm(src,[((i*379)%12001)-6000 for i in range(4000)])
        def shorts(path):
            with wave.open(str(path),'rb') as f:
                assert f.getsampwidth()==2
                return struct.unpack('<%dh' % f.getnframes(), f.readframes(f.getnframes()))
        base=['--filter',ir,'--input',src,'--output',dst,'--output-format','wav-pcm16']
        exact=[(((i*379)%12001)-6000)*13107/32768 for i in range(4000)]
        call(base); plain=shorts(dst); call(base+['--dither','none']); assert shorts(dst)==plain
        assert all(a==math.floor(b+.5) for a,b in zip(plain,exact) if abs(b%1-.5)>.01)
        call(base+['--dither','triangular']); first=shorts(dst)
        call(base+['--dither','triangular','--dither-seed','0'])
        assert shorts(dst)==first and first!=plain
        assert all(abs(a-b)<=1.5 for a,b in zip(first,exact))
        call(base+['--dither','triangular','--dither-seed','1']); assert shorts(dst)!=first
        for mode in ('rectangular','triangular-highpass'):
            call(base+['--dither',mode]); assert all(abs(a-b)<=1.5 for a,b in zip(shorts(dst),exact))
        for extra in (['--dither','bogus'],['--dither-seed','-1'],['--dither-seed','x'],
                      ['--output-format','wav-float','--dither','rectangular'],
                      ['--output-format','raw-float','--dither','triangular'],
                      ['--noise-shaping','bogus'],['--output-format','wav-float','--noise-shaping','simple'],
                      ['--valid-bits','7'],['--valid-bits','17'],['--output-format','wav-pcm8','--valid-bits','9'],
                      ['--output-format','wav-float','--valid-bits','16']):
            call(['--filter',ir,'--input',src,'--output',dst]+extra,ok=False)
        call(['--filter',ir,'--input',src,'--output',dst,'--dither','none'])
        integer_format_tests(d)
        automatic_gain_tests(d)
        output_selection_tests(d)
        # Errors must be failures, not successful silent output.
        for extra in (['--block-size','0'], ['--block-size','-1'], ['--gain-db','nan'], ['--unknown']):
            call(['--filter',ir,'--input',src,'--output',dst]+extra,ok=False)
        call(['--filter',ir,'--input',src,'--output',src],ok=False)
        call(['--filter',ir,'--input','-','--output',dst],ok=False,data=raw)
        call(['--filter',ir,'--input','-','--raw-input','44100','1','--output',dst],ok=False,data=raw)
        call(args,data=b'')
        repeatability_tests(d)
        speaker_map_tests(d)
        container_tests(d)
        # An option value spelled like an inspection flag is still a value.
        pcm(d/'v-ir.wav',[16384]); pcm(d/'v-in.wav',[1000,-1000])
        p=subprocess.run([EXE,'--filter','v-ir.wav','--input','v-in.wav','--output','--validate'],
                         cwd=d,capture_output=True)
        assert p.returncode==0 and (d/'--validate').is_file(), p.stderr
    print('PASS: native CLI files, matrices, EOF, gain, errors and real RAW pipes')

if __name__=='__main__': main()
