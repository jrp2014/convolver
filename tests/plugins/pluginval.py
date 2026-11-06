# SPDX-License-Identifier: GPL-2.0-or-later
"""Run Tracktion's pluginval, an independent JUCE-based host, on a plug-in.

Every format uses the same strictness (10, the highest) and the same command
line; this module holds what they share.

  validate <pluginval> <plugin> [extra pluginval options...]
           Host conformance at strictness level 10: lifecycle, state, buses,
           parameter fuzzing and allocations during processing. On macOS,
           where the plug-ins have a window, pluginval's editor tests open and
           close it too; elsewhere they are skipped. <plugin> is a VST3
           bundle, an installed Audio Unit component or a one-plug-in LV2
           bundle (the LV2 test makes one for each plug-in).

pluginval 1 and 2 take different command lines; the version is read from
`pluginval --version`.
"""

import os
import re
import shutil
import subprocess
import sys

STRICTNESS = '10'


def run_pluginval(pluginval, arguments, **kwargs):
    command = [pluginval, *arguments]
    # JUCE needs an X server to load plug-ins on Linux.
    if sys.platform.startswith('linux') and not os.environ.get('DISPLAY'):
        xvfb = shutil.which('xvfb-run')
        if xvfb is None:
            sys.exit('pluginval needs DISPLAY or xvfb-run on Linux')
        command = [xvfb, '--auto-servernum', *command]
    print('+', ' '.join(command), flush=True)
    check = kwargs.pop('check', False)
    return subprocess.run(command, check=check, **kwargs)


def major_version(pluginval):
    """pluginval's major version, from `pluginval --version` ("pluginval - 1.0.4")."""
    output = subprocess.run([pluginval, '--version'], capture_output=True, text=True, timeout=120, check=False).stdout
    match = re.search(r'(\d+)\.\d+', output)
    if not match:
        sys.exit(f'cannot read a version from `{pluginval} --version`: {output.strip()!r}')
    return int(match[1])


def validate_command(major, plugin, extra, platform=sys.platform):
    """pluginval 2 has a `validate` command; pluginval 1 takes options and --validate.

    The editor tests run only on macOS, the one system with a window so far
    (VST3-11 and the Audio Unit; Windows and Linux follow in VST3-12 and VST3-13).
    """
    gui = [] if platform == 'darwin' else ['--skip-gui-tests']
    options = ['--strictness-level', STRICTNESS, *gui, *extra]
    if major >= 2:
        return ['validate', *options, plugin]
    return [*options, '--validate', plugin]


def validate(pluginval, plugin, extra=(), **kwargs):
    """Validate one plug-in; returns pluginval's exit status."""
    major = major_version(pluginval)
    print(f'pluginval {major}.x', flush=True)
    return run_pluginval(pluginval, validate_command(major, str(plugin), list(extra)), **kwargs).returncode


def main():
    if len(sys.argv) < 4 or sys.argv[1] != 'validate':
        sys.exit(__doc__)
    pluginval, plugin, *extra = sys.argv[2:]
    sys.exit(validate(pluginval, plugin, extra))


if __name__ == '__main__':
    main()
