# SPDX-License-Identifier: GPL-2.0-or-later
"""Write the LV2 bundle's manifest.ttl and convolver.ttl.

Usage: generate_ttl.py IDENTITIES_JSON VERSION_FILE BINARY_NAME OUTPUT_DIRECTORY

One plug-in per fixed layout in plugins/identities.json (PLUG-4), all from the
same template. Port numbers match plugins/lv2/plugin.hpp: three control ports,
then the audio inputs, then the audio outputs, then the control and notify
atom ports. The lv2.adapter test reads the
result through lilv and checks it against the adapter.
"""
import json
import pathlib
import re
import sys

PREFIXES = """\
@prefix atom: <http://lv2plug.in/ns/ext/atom#> .
@prefix bufsz: <http://lv2plug.in/ns/ext/buf-size#> .
@prefix doap: <http://usefulinc.com/ns/doap#> .
@prefix foaf: <http://xmlns.com/foaf/0.1/> .
@prefix log: <http://lv2plug.in/ns/ext/log#> .
@prefix lv2: <http://lv2plug.in/ns/lv2core#> .
@prefix opts: <http://lv2plug.in/ns/ext/options#> .
@prefix patch: <http://lv2plug.in/ns/ext/patch#> .
@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix rsz: <http://lv2plug.in/ns/ext/resize-port#> .
@prefix state: <http://lv2plug.in/ns/ext/state#> .
@prefix units: <http://lv2plug.in/ns/extensions/units#> .
@prefix urid: <http://lv2plug.in/ns/ext/urid#> .
@prefix work: <http://lv2plug.in/ns/ext/worker#> .
"""

# Status port values, as in plugin.hpp's Status.
STATUS = [(0, 'Running'), (1, 'No filter'), (2, 'Load failed'), (3, 'Loading')]
# The filter parameter's suffix and the atom ports' buffer size, as in plugin.hpp.
FILTER = 'filter'
ATOM_BUFFER_BYTES = 8192


def turtle_string(text):
    if re.search(r'["\\\x00-\x1f]', text):
        raise SystemExit(f'not a plain Turtle string: {text!r}')
    return f'"{text}"'


def channels(count, side):
    noun = 'input' if side == 'in' else 'output'
    return f'{count} {noun}' + ('' if count == 1 else 's')


def audio_ports(layout, first):
    ports = []
    index = first
    for side, count, kind in (('in', layout['inputs'], 'lv2:InputPort'),
                              ('out', layout['outputs'], 'lv2:OutputPort')):
        for channel in range(1, count + 1):
            label = 'Input' if side == 'in' else 'Output'
            ports.append(f"""\t[
\t\ta lv2:AudioPort , {kind} ;
\t\tlv2:index {index} ;
\t\tlv2:symbol "{side}_{channel}" ;
\t\tlv2:name "{label} {channel}"
\t]""")
            index += 1
    return ports


def plugin(base, layout, vendor, version):
    inputs, outputs = layout['inputs'], layout['outputs']
    if layout['input'] != 'discrete' or layout['output'] != 'discrete':
        raise SystemExit(f"{layout['id']}: named layouts need port groups (not yet written)")
    _, minor, micro = version
    status_points = ' ,\n'.join(
        f'\t\t\t[ rdfs:label "{label}" ; rdf:value {value} ]' for value, label in STATUS)
    control = [
        """\t[
\t\ta lv2:ControlPort , lv2:InputPort ;
\t\tlv2:index 0 ;
\t\tlv2:symbol "enabled" ;
\t\tlv2:name "Enabled" ;
\t\tlv2:designation lv2:enabled ;
\t\tlv2:portProperty lv2:toggled , lv2:integer ;
\t\tlv2:default 1 ;
\t\tlv2:minimum 0 ;
\t\tlv2:maximum 1
\t]""",
        """\t[
\t\ta lv2:ControlPort , lv2:OutputPort ;
\t\tlv2:index 1 ;
\t\tlv2:symbol "latency" ;
\t\tlv2:name "Latency" ;
\t\tlv2:designation lv2:latency ;
\t\tlv2:portProperty lv2:reportsLatency , lv2:integer ;
\t\tunits:unit units:frame ;
\t\tlv2:minimum 0 ;
\t\tlv2:maximum 256
\t]""",
        f"""\t[
\t\ta lv2:ControlPort , lv2:OutputPort ;
\t\tlv2:index 2 ;
\t\tlv2:symbol "status" ;
\t\tlv2:name "Status" ;
\t\tlv2:portProperty lv2:integer , lv2:enumeration ;
\t\tlv2:minimum 0 ;
\t\tlv2:maximum 3 ;
\t\tlv2:scalePoint
{status_points}
\t]""",
    ]
    first_atom = 3 + inputs + outputs
    atoms = [
        f"""\t[
\t\ta lv2:InputPort , atom:AtomPort ;
\t\tatom:bufferType atom:Sequence ;
\t\tatom:supports patch:Message ;
\t\tlv2:designation lv2:control ;
\t\trsz:minimumSize {ATOM_BUFFER_BYTES} ;
\t\tlv2:index {first_atom} ;
\t\tlv2:symbol "control" ;
\t\tlv2:name "Control"
\t]""",
        f"""\t[
\t\ta lv2:OutputPort , atom:AtomPort ;
\t\tatom:bufferType atom:Sequence ;
\t\tatom:supports patch:Message ;
\t\tlv2:designation lv2:control ;
\t\trsz:minimumSize {ATOM_BUFFER_BYTES} ;
\t\tlv2:index {first_atom + 1} ;
\t\tlv2:symbol "notify" ;
\t\tlv2:name "Notify"
\t]""",
    ]
    ports = ' ,\n'.join(control + audio_ports(layout, 3) + atoms)
    name = turtle_string(f"{vendor['name']} ({inputs} in, {outputs} out)")
    comment = turtle_string(
        'Applies the FIR filters of a Convolver configuration (.cfg) or impulse response '
        f"to {channels(inputs, 'in')}, giving {channels(outputs, 'out')}.")
    return f"""<{base}{layout['id']}>
\ta lv2:Plugin , lv2:FilterPlugin ;
\tdoap:name {name} ;
\trdfs:comment {comment} ;
\tdoap:license <https://spdx.org/licenses/GPL-2.0-or-later> ;
\tdoap:maintainer [
\t\tfoaf:name {turtle_string(vendor['name'])} ;
\t\tfoaf:homepage <{vendor['url']}>
\t] ;
\tlv2:minorVersion {minor} ;
\tlv2:microVersion {micro} ;
\tlv2:requiredFeature urid:map ;
\tlv2:optionalFeature lv2:hardRTCapable , log:log , opts:options , work:schedule ;
\topts:supportedOption bufsz:maxBlockLength ;
\tlv2:extensionData state:interface , work:interface ;
\tpatch:writable <{base}{FILTER}> ;
\tlv2:port
{ports} .
"""


def parameter(base):
    """The filter parameter: hosts show a file chooser for it."""
    comment = turtle_string(
        'The filter to apply: a Convolver configuration (.cfg), a list of them, or an impulse '
        'response file. Lists choose the entry for the sample rate and channels.')
    return f"""<{base}{FILTER}>
\ta lv2:Parameter ;
\trdfs:label "Filter" ;
\trdfs:comment {comment} ;
\trdfs:range atom:Path .
"""


def main(argv):
    if len(argv) != 5:
        raise SystemExit(__doc__)
    identities = json.loads(pathlib.Path(argv[1]).read_text(encoding='utf-8'))
    text = pathlib.Path(argv[2]).read_text(encoding='utf-8').strip()
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', text)
    if not match:
        raise SystemExit(f'{argv[2]}: not MAJOR.MINOR.PATCH: {text!r}')
    version = tuple(int(part) for part in match.groups())
    binary = argv[3]
    if not re.fullmatch(r'[\w.\-]+', binary):
        raise SystemExit(f'unexpected binary name {binary!r}')
    out = pathlib.Path(argv[4])
    out.mkdir(parents=True, exist_ok=True)
    base = identities['lv2']['base_uri']
    layouts = identities['fixed_layouts']
    manifest = ['@prefix lv2: <http://lv2plug.in/ns/lv2core#> .',
                '@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .', '']
    for layout in layouts:
        manifest += [f"<{base}{layout['id']}>", '\ta lv2:Plugin ;', f'\tlv2:binary <{binary}> ;',
                     '\trdfs:seeAlso <convolver.ttl> .', '']
    header = ('# Generated by plugins/lv2/generate_ttl.py from plugins/identities.json.\n'
              '# Plug-in URIs are permanent: never change a released one.\n\n')
    files = {
        'manifest.ttl': header + '\n'.join(manifest),
        'convolver.ttl': header + PREFIXES + '\n' + parameter(base) + '\n' + '\n'.join(
            plugin(base, layout, identities['vendor'], version) for layout in layouts),
    }
    for name, content in files.items():
        path = out / name
        # Leave an unchanged file alone, so the build does not redo dependent steps.
        if not path.exists() or path.read_text(encoding='utf-8') != content:
            path.write_text(content, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main(sys.argv)
