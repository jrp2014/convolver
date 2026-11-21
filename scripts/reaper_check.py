#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One command for the REAPER checks (HOST-11), run on jrp's Mac.

Run it from an unzipped preview package's folder, the one that holds bin/,
examples/ and presets/. Quit REAPER first, and install that package's
Convolver.vst3:

    python3 /path/to/convolver/scripts/reaper_check.py

It starts REAPER twice from its command line, each time with a ReaScript
that this script writes:

1. A new project at 48000 Hz. The stereo example's input.wav goes on a
   track, with Convolver's VST3 loaded from the example's .vstpreset.
   REAPER plays for a moment, renders the project, saves it and quits.
2. The saved project, reopened in a new REAPER process. REAPER plays for a
   moment, renders again, saves a copy and quits.

Both renders are compared with the example's expected.wav by the package's
bin/convolver --compare. Everything goes into a new folder in the package
folder, and result.md there holds what a host report needs
(docs/develop/host-testing.md#every-report), ready to paste back. The exit
status is 0 when every check passed, 1 when one failed, and 2 when the run
could not start.

This is the plan's first version: the smallest run that shows REAPER doing
one render and one save and reopen from a script. Refusals, the wrong host
rate, a Latency change and the Audio Unit come later
(docs/project/plans/2026-11-21-host-test-automation.md). It uses only
Python's standard library, so it runs with the python3 that macOS provides.
"""

import argparse
import datetime as dt
import hashlib
import platform
import plistlib
import shutil
import subprocess
import sys
import time
from pathlib import Path

RATE = 48000
EXAMPLE = Path('examples/first-use/stereo')
PRESET = EXAMPLE / 'first-use-stereo.vstpreset'
INPUT = EXAMPLE / 'input.wav'
EXPECTED = EXAMPLE / 'expected.wav'
CLI = Path('bin/convolver')
# What Status reads once the stereo example plays (portable/host.cpp).
LOADED = 'Filter loaded: 48000 Hz, 2 in, 2 out'
REAPERS = (
    '/Applications/REAPER.app/Contents/MacOS/REAPER',
    '/Applications/REAPER64.app/Contents/MacOS/REAPER',
)
BUNDLES = (
    Path.home() / 'Library/Audio/Plug-Ins/VST3/Convolver.vst3',
    Path('/Library/Audio/Plug-Ins/VST3/Convolver.vst3'),
)
LOG = Path.home() / 'Library/Logs/Convolver/Convolver.log'
# TrackFX_AddByName tries these in turn: REAPER's own listing first.
FX_NAMES = ('VST3: Convolver (Convolver)', 'VST3:Convolver')
FIRST_RENDER = 'host-render'
REOPENED_RENDER = 'host-render-reopened'

# The ReaScripts, in Lua. CONFIG, written by lua_config(), comes first.
LUA_COMMON = r"""
-- Written by scripts/reaper_check.py (HOST-11). It runs inside REAPER and
-- records what it sees in CONFIG.results, one "key<TAB>value" line each.
local project = 0
local results = {}

local function escape(text)
  return (tostring(text):gsub("\\", "\\\\"):gsub("\t", "\\t"):gsub("\r", "\\r"):gsub("\n", "\\n"))
end

-- Rewrites the whole file each time, through a rename, so that the script
-- outside REAPER never reads half a line and keeps everything up to a crash.
local function record(key, value)
  if value == nil then value = "" end
  results[#results + 1] = {key, tostring(value)}
  local partial = CONFIG.results .. ".partial"
  local file = assert(io.open(partial, "w"))
  for _, entry in ipairs(results) do
    file:write(entry[1], "\t", escape(entry[2]), "\n")
  end
  file:close()
  assert(os.rename(partial, CONFIG.results))
end

local function exists(path)
  local file = io.open(path, "rb")
  if file then
    file:close()
    return true
  end
  return false
end

local function device_info(name)
  local ok, value = reaper.GetAudioDeviceInfo(name, "")
  if ok then return value end
  return "(not reported)"
end

-- The plug-in type ("VST3"), which REAPER's naming preferences can hide
-- from the displayed name.
local function fx_type(track, fx)
  if not reaper.TrackFX_GetNamedConfigParm then return "" end
  local ok, value = reaper.TrackFX_GetNamedConfigParm(track, fx, "fx_type")
  if ok then return value end
  return ""
end

local function find_convolver(track)
  for fx = 0, reaper.TrackFX_GetCount(track) - 1 do
    local _, name = reaper.TrackFX_GetFXName(track, fx, "")
    if name:find("Convolver", 1, true) then return fx, name end
  end
end

local function find_parameter(track, fx, wanted)
  for index = 0, reaper.TrackFX_GetNumParams(track, fx) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, index, "")
    if name == wanted then return index end
  end
end

-- The full text of Convolver's read-only Status parameter.
local function status(track, fx)
  local index = find_parameter(track, fx, "Status")
  if not index then return "(no Status parameter)" end
  local _, text = reaper.TrackFX_GetFormattedParamValue(track, fx, index, "")
  return text
end

local function settled(track, fx)
  return function() return status(track, fx):find("^No filter") == nil end
end

local function prepare_render(name)
  reaper.GetSetProjectInfo_String(project, "RENDER_FILE", CONFIG.output, true)
  reaper.GetSetProjectInfo_String(project, "RENDER_PATTERN", name, true)
  reaper.GetSetProjectInfo_String(project, "RENDER_FORMAT", "evaw", true) -- WAV, default settings
  reaper.GetSetProjectInfo_String(project, "RENDER_FORMAT2", "", true)
  reaper.GetSetProjectInfo(project, "RENDER_SETTINGS", 0, true) -- the master mix
  reaper.GetSetProjectInfo(project, "RENDER_BOUNDSFLAG", 1, true) -- the entire project
  reaper.GetSetProjectInfo(project, "RENDER_CHANNELS", 2, true)
  reaper.GetSetProjectInfo(project, "RENDER_SRATE", CONFIG.rate, true)
  reaper.GetSetProjectInfo(project, "RENDER_TAILFLAG", 127, true) -- a tail for every bound
  reaper.GetSetProjectInfo(project, "RENDER_TAILMS", 1000, true)
  reaper.GetSetProjectInfo(project, "RENDER_NORMALIZE", 0, true)
  reaper.GetSetProjectInfo(project, "RENDER_DITHER", 0, true)
  reaper.GetSetProjectInfo(project, "RENDER_ADDTOPROJ", 0, true)
end

local function render(track, fx, name)
  prepare_render(name)
  record(name .. "_started", "yes")
  -- File: Render project, using the most recent render settings, auto-close
  -- render dialog. It returns once the file is written.
  reaper.Main_OnCommand(42230, 0)
  record(name .. "_written", exists(CONFIG.output .. "/" .. name .. ".wav") and "yes" or "no")
  record(name .. "_tracks", reaper.CountTracks(project))
end

-- REAPER runs a script's code between its own work, so each step returns:
-- nothing (go on), a number of seconds to wait, or a test and a timeout to
-- wait for. An error ends the run, and the script outside reports it.
local steps, position, resume_at, waiting = {}, 1, 0, nil
local function step(action) steps[#steps + 1] = action end

local function finish(message)
  if message then record("error", message) end
  -- A project with unsaved changes would stop the quit at "Save changes?".
  -- Opening the copy just saved, without that prompt, leaves nothing to ask.
  if reaper.IsProjectDirty(project) ~= 0 and exists(CONFIG.project) then
    record("dirty_at_quit", "yes")
    reaper.Main_openProject("noprompt:" .. CONFIG.project)
  end
  record("done", "yes")
  reaper.Main_OnCommand(40004, 0) -- File: Quit REAPER
end

local function tick()
  local now = reaper.time_precise()
  if waiting then
    local ok, ready = pcall(waiting.test)
    if not ok then
      finish("while waiting: " .. tostring(ready))
      return
    end
    if not ready and now < waiting.deadline then
      reaper.defer(tick)
      return
    end
    waiting = nil
  elseif now < resume_at then
    reaper.defer(tick)
    return
  end
  local action = steps[position]
  if not action then
    finish()
    return
  end
  position = position + 1
  local ok, result, timeout = pcall(action)
  if not ok then
    finish("step " .. (position - 1) .. ": " .. tostring(result))
    return
  end
  if type(result) == "number" then
    resume_at = now + result
  elseif type(result) == "function" then
    waiting = {test = result, deadline = now + (timeout or 10)}
  end
  reaper.defer(tick)
end

local function play_briefly(track, fx)
  step(function()
    reaper.OnPlayButton()
    return CONFIG.play_seconds
  end)
  step(function()
    record("play_state", reaper.GetPlayState())
    record("device_rate_playing", device_info("SRATE"))
    record("status_playing", status(track(), fx()))
    reaper.OnStopButton()
    return 0.5
  end)
end
"""

LUA_FIRST = r"""
local track, fx

step(function()
  record("stage", "first")
  record("reaper_version", reaper.GetAppVersion())
  record("os", reaper.GetOS())
  record("device_rate", device_info("SRATE"))
  record("device_block", device_info("BSIZE"))
  record("device_output", device_info("IDENT_OUT"))
  -- A default template can add tracks or master effects; this project
  -- starts empty, with the master at unity.
  record("template_tracks", reaper.CountTracks(project))
  for index = reaper.CountTracks(project) - 1, 0, -1 do
    reaper.DeleteTrack(reaper.GetTrack(project, index))
  end
  local master = reaper.GetMasterTrack(project)
  record("template_master_fx", reaper.TrackFX_GetCount(master))
  for index = reaper.TrackFX_GetCount(master) - 1, 0, -1 do
    reaper.TrackFX_Delete(master, index)
  end
  reaper.SetMediaTrackInfo_Value(master, "D_VOL", 1)
  reaper.SetMediaTrackInfo_Value(master, "D_PAN", 0)
  reaper.SetMediaTrackInfo_Value(master, "I_NCHAN", 2)
  reaper.GetSetProjectInfo(project, "PROJECT_SRATE", CONFIG.rate, true)
  reaper.GetSetProjectInfo(project, "PROJECT_SRATE_USE", 1, true)
  record("project_rate", reaper.GetSetProjectInfo(project, "PROJECT_SRATE", 0, false))
end)

step(function()
  reaper.SetEditCurPos(0, false, false)
  reaper.InsertMedia(CONFIG.input, 1) -- on a new track, at the start
  track = reaper.GetTrack(project, 0)
  if not track then error("REAPER did not insert " .. CONFIG.input, 0) end
  local item = reaper.GetTrackMediaItem(track, 0)
  if not item then error("REAPER made a track but put no item on it", 0) end
  reaper.SetMediaTrackInfo_Value(track, "D_VOL", 1)
  reaper.SetMediaTrackInfo_Value(track, "D_PAN", 0)
  reaper.SetMediaTrackInfo_Value(track, "I_NCHAN", 2)
  reaper.SetMediaItemInfo_Value(item, "D_POSITION", 0)
  reaper.SetMediaItemInfo_Value(item, "D_VOL", 1)
  for _, name in ipairs({"D_FADEINLEN", "D_FADEOUTLEN", "D_FADEINLEN_AUTO", "D_FADEOUTLEN_AUTO"}) do
    reaper.SetMediaItemInfo_Value(item, name, 0)
  end
  record("tracks", reaper.CountTracks(project))
  record("item_position", reaper.GetMediaItemInfo_Value(item, "D_POSITION"))
  record("item_length", reaper.GetMediaItemInfo_Value(item, "D_LENGTH"))
  reaper.UpdateArrange()
end)

step(function()
  local added = -1
  for _, name in ipairs(CONFIG.fx_names) do
    added = reaper.TrackFX_AddByName(track, name, false, -1)
    if added >= 0 then break end
  end
  if added < 0 then
    error("REAPER has no VST3 plug-in named Convolver. Rescan in Settings > Plug-ins > VST.", 0)
  end
  fx = added
  local _, name = reaper.TrackFX_GetFXName(track, fx, "")
  record("fx_name", name)
  record("fx_type", fx_type(track, fx))
  record("fx_parameters", reaper.TrackFX_GetNumParams(track, fx))
  return 1
end)

step(function()
  record("status_initial", status(track, fx))
  record("preset_applied", reaper.TrackFX_SetPreset(track, fx, CONFIG.preset))
  return settled(track, fx), 15
end)

step(function()
  record("status_after_preset", status(track, fx))
end)

play_briefly(function() return track end, function() return fx end)

step(function()
  render(track, fx, CONFIG.render)
  return 1
end)

step(function()
  record("status_after_render", status(track, fx))
  if not reaper.Main_SaveProjectEx then error("This REAPER cannot save a project from a script; update it.", 0) end
  reaper.Main_SaveProjectEx(project, CONFIG.project, 0)
  record("project_saved", exists(CONFIG.project) and "yes" or "no")
  record("dirty_after_save", reaper.IsProjectDirty(project))
end)

reaper.defer(tick)
"""

LUA_REOPEN = r"""
local track, fx

step(function()
  record("stage", "reopen")
  record("reaper_version", reaper.GetAppVersion())
  return function() return reaper.CountTracks(project) > 0 end, 30
end)

step(function()
  local _, opened = reaper.EnumProjects(-1, "")
  record("project_opened", opened)
  record("tracks", reaper.CountTracks(project))
  track = reaper.GetTrack(project, 0)
  if not track then error("The reopened project has no track.", 0) end
  fx = find_convolver(track)
  if not fx then error("The reopened project's track has no Convolver.", 0) end
  local _, name = reaper.TrackFX_GetFXName(track, fx, "")
  record("fx_name", name)
  record("fx_type", fx_type(track, fx))
  return settled(track, fx), 15
end)

step(function()
  record("status_reopened", status(track, fx))
end)

play_briefly(function() return track end, function() return fx end)

step(function()
  render(track, fx, CONFIG.render)
  return 1
end)

step(function()
  record("status_after_render", status(track, fx))
  if reaper.Main_SaveProjectEx then
    -- A copy, so that quitting does not stop to ask about changes.
    reaper.Main_SaveProjectEx(project, CONFIG.project, 0)
    record("project_saved", exists(CONFIG.project) and "yes" or "no")
  end
end)

reaper.defer(tick)
"""


def lua_string(text):
    """A Lua string literal holding exactly `text`."""
    if '\0' in text:
        raise ValueError(f'a path with a NUL byte cannot be passed to REAPER: {text!r}')
    escaped = text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r')
    return f'"{escaped}"'


def lua_value(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, (list, tuple)):
        return '{' + ', '.join(lua_value(item) for item in value) + '}'
    return lua_string(str(value))


def lua_config(config):
    lines = ['local CONFIG = {']
    lines += [f'  {key} = {lua_value(value)},' for key, value in config.items()]
    return '\n'.join([*lines, '}'])


def lua_script(stage, config):
    """The ReaScript for one stage ('first' or 'reopen')."""
    body = {'first': LUA_FIRST, 'reopen': LUA_REOPEN}[stage]
    return '-- SPDX-License-Identifier: GPL-2.0-or-later\n' + lua_config(config) + LUA_COMMON + body


def unescape(text):
    out, chars = [], iter(text)
    for char in chars:
        if char != '\\':
            out.append(char)
            continue
        following = next(chars, '')
        out.append({'t': '\t', 'n': '\n', 'r': '\r', '\\': '\\'}.get(following, '\\' + following))
    return ''.join(out)


def parse_results(text):
    """The ReaScript's records as a dict; a key recorded twice keeps its last value."""
    results = {}
    for line in text.splitlines():
        key, tab, value = line.partition('\t')
        if tab:
            results[key] = unescape(value)
    return results


def read_results(path):
    try:
        return parse_results(path.read_text(encoding='utf-8', errors='replace'))
    except FileNotFoundError:
        return {}


def reaper_running():
    """True or False, or None when it cannot be told (no pgrep)."""
    try:
        found = subprocess.run(['pgrep', '-x', 'REAPER'], capture_output=True, check=False)
    except OSError:
        return None
    return found.returncode == 0


def run_stage(reaper, name, arguments, folder, results, timeout):
    """Starts REAPER with `arguments`, waits for the ReaScript to record
    "done" and for REAPER to quit, and returns what happened."""
    log_path = folder / f'reaper-{name}.log'
    started = time.monotonic()
    with log_path.open('wb') as log:
        process = subprocess.Popen(
            [reaper, '-newinst', '-nosplash', '-ignoreerrors', *map(str, arguments)],
            stdout=log,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
        )
        outcome = {'pid': process.pid, 'log': log_path.name}
        while time.monotonic() - started < timeout:
            if read_results(results).get('done') == 'yes':
                break
            if process.poll() is not None:
                break
            time.sleep(0.5)
        recorded = read_results(results)
        outcome['finished'] = recorded.get('done') == 'yes'
        if process.poll() is not None and not outcome['finished']:
            outcome['problem'] = f'REAPER exited (status {process.returncode}) before the ReaScript finished.'
        elif not outcome['finished']:
            outcome['problem'] = (
                f'The ReaScript did not finish within {timeout} s: REAPER did not run it, '
                'or it stopped at a dialog. What it recorded is below.'
            )
        try:
            process.wait(timeout=30)
            outcome['quit_by_itself'] = True
        except subprocess.TimeoutExpired:
            outcome['quit_by_itself'] = False
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    outcome['seconds'] = round(time.monotonic() - started, 1)
    outcome['results'] = read_results(results)
    return outcome


def compare(cli, render, expected):
    if not render.exists():
        return {'match': False, 'output': f'No render: {render.name} was not written.'}
    finished = subprocess.run(
        [str(cli), '--compare', str(render), str(expected)], capture_output=True, text=True, check=False
    )
    output = (finished.stdout + finished.stderr).strip()
    return {'match': finished.returncode == 0, 'output': output, 'status': finished.returncode}


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def command_output(command):
    """What a command prints, or None when it cannot run."""
    try:
        finished = subprocess.run(command, capture_output=True, text=True, check=False)
    except OSError:
        return None
    return (finished.stdout + finished.stderr).strip()


def bundle_version(bundle):
    try:
        with (bundle / 'Contents/Info.plist').open('rb') as file:
            info = plistlib.load(file)
    except (OSError, plistlib.InvalidFileException) as error:
        return f'(no version: {error})'
    return info.get('CFBundleShortVersionString', '(no version)')


def log_size():
    try:
        return LOG.stat().st_size
    except OSError:
        return 0


def new_log_lines(start):
    """What the Convolver log gained during the run (all of it if it was
    emptied or rotated meanwhile)."""
    try:
        data = LOG.read_bytes()
    except OSError:
        return f'(no Convolver log at {LOG})'
    if len(data) < start:
        start = 0
    return data[start:].decode('utf-8', errors='replace').strip() or '(nothing written)'


def rate_hint(results):
    """A note when the audio device did not run at the example's rate, which
    makes Status report the mismatch while REAPER plays."""
    rates = {results.get('device_rate'), results.get('device_rate_playing')} - {None, '(not reported)'}
    if rates and rates != {str(RATE)}:
        return f' The audio device ran at {" and ".join(sorted(rates))} Hz: set it to {RATE} Hz and run again.'
    return ''


def evaluate(first, reopen, first_compare, reopen_compare, project):
    """The checks, in order: (name, passed, evidence)."""
    one = first['results']
    two = reopen['results'] if reopen else {}
    return [
        (
            'REAPER ran the first ReaScript to the end',
            first['finished'] and 'error' not in one,
            one.get('error') or first.get('problem') or f'{first["seconds"]} s',
        ),
        (
            "Convolver's VST3 is on the track",
            'Convolver' in one.get('fx_name', '') and 'VST3' in (one.get('fx_type', '') + one.get('fx_name', '')),
            f'{one.get("fx_name", "(not added)")} (type {one.get("fx_type") or "not reported"})',
        ),
        (
            'The .vstpreset loads the stereo example',
            one.get('status_after_preset', '').startswith(LOADED) or one.get('status_playing', '').startswith(LOADED),
            (
                f'Status after the preset: {one.get("status_after_preset", "(none)")}; '
                f'while playing: {one.get("status_playing", "(none)")}{rate_hint(one)}'
            ),
        ),
        ('The render matches expected.wav', first_compare['match'], last_line(first_compare['output'])),
        ('The project is saved', project.exists(), project.name if project.exists() else 'not saved'),
        (
            'REAPER reopened the project in a new process and ran the second ReaScript to the end',
            bool(reopen) and reopen['finished'] and 'error' not in two,
            (two.get('error') or reopen.get('problem') or f'{reopen["seconds"]} s, process {reopen["pid"]}')
            if reopen
            else 'not run',
        ),
        (
            'The reopened project restores the filter',
            two.get('status_reopened', '').startswith(LOADED) or two.get('status_playing', '').startswith(LOADED),
            (
                f'Status on reopening: {two.get("status_reopened", "(none)")}; '
                f'while playing: {two.get("status_playing", "(none)")}{rate_hint(two)}'
            ),
        ),
        ('The reopened render matches expected.wav', reopen_compare['match'], last_line(reopen_compare['output'])),
    ]


def last_line(text):
    lines = [line for line in text.splitlines() if line.strip()]
    return ' / '.join(lines[-2:]) if lines else '(no output)'


def cell(text):
    return str(text).replace('|', '\\|').replace('\n', ' ')


def report(context):
    """result.md: what every host report lists, then the checks and evidence."""
    checks = context['checks']
    failed = [name for name, passed, _ in checks if not passed]
    first, reopen = context['first'], context['reopen']
    one = first['results']

    def quit_text(outcome):
        return 'by itself' if outcome.get('quit_by_itself') else 'only when this script stopped it'

    verdict = 'PASS' if not failed else 'FAIL'
    device = (
        f'{one.get("device_output", "(not reported)")} at {one.get("device_rate", "?")} Hz '
        f'({one.get("device_rate_playing", "?")} Hz while playing), block size {one.get("device_block", "?")}'
    )
    if reopen:
        reopened = (
            f'4. REAPER started again with the saved project (process {reopen["pid"]}). It played, '
            f'rendered `{REOPENED_RENDER}.wav` and quit ({quit_text(reopen)}).'
        )
    else:
        reopened = '4. REAPER did not start again: the project was not saved.'
    lines = [
        f'# REAPER check, {context["when"]}',
        '',
        (
            f'**{verdict}: {len(checks) - len(failed)} of {len(checks)} checks passed.** '
            'Written by `scripts/reaper_check.py` (HOST-11).'
        ),
        '',
        '| # | Check | Result | Evidence |',
        '| --- | --- | --- | --- |',
    ]
    for number, (name, passed, evidence) in enumerate(checks, 1):
        lines.append(f'| {number} | {cell(name)} | {"Pass" if passed else "**FAIL**"} | {cell(evidence)} |')
    lines += [
        '',
        '## Environment',
        '',
        f'- REAPER: {one.get("reaper_version", "(not reported)")}, `{context["reaper"]}`',
        f'- macOS: {context["macos"]}; CPU: {context["machine"]}',
        f'- Package folder: `{context["package"].name}`',
        f'- Package SHA-256: {context["package_sha"]}',
        f'- Convolver CLI: {context["cli_version"]}',
        f'- Installed VST3: {context["bundles"]}',
        f'- Project and render rate: {one.get("project_rate", "(not reported)")} Hz; audio device: {device}',
        (
            '- Channels: one stereo track (2 channels) into a stereo master; Convolver has '
            f'{one.get("fx_parameters", "?")} parameters'
        ),
        (
            f'- Default template: {one.get("template_tracks", "?")} tracks and '
            f'{one.get("template_master_fx", "?")} master effects removed'
        ),
        '',
        '## Steps',
        '',
        (
            f'1. REAPER started with a new project (process {first["pid"]}). The ReaScript set it to '
            f'{RATE} Hz, put `{INPUT}` on a new track at 0 s with no fades, added Convolver '
            f'(VST3) and loaded `{PRESET}` with TrackFX_SetPreset.'
        ),
        (
            f'2. REAPER played for {context["play_seconds"]} s, then rendered the master to '
            f'`{FIRST_RENDER}.wav` (WAV, {RATE} Hz, stereo, 1 s tail, no normalising or dither).'
        ),
        f'3. It saved the project as `{context["project"].name}` and quit ({quit_text(first)}).',
        reopened,
        f'5. Each render was compared with `{EXPECTED}` by `bin/convolver --compare`.',
        '',
        '## Compare output',
        '',
        f'`{FIRST_RENDER}.wav`:',
        '',
        '```text',
        context['first_compare']['output'],
        '```',
        '',
        f'`{REOPENED_RENDER}.wav`:',
        '',
        '```text',
        context['reopen_compare']['output'],
        '```',
        '',
        '## What each ReaScript recorded',
        '',
    ]
    for stage, outcome in (('First', first), ('Reopen', reopen)):
        lines.append(f'{stage}:')
        lines.append('')
        lines.append('```text')
        if outcome:
            lines += [f'{key}: {value}' for key, value in outcome['results'].items()] or ['(nothing)']
            if outcome.get('problem'):
                lines.append(f'problem: {outcome["problem"]}')
        else:
            lines.append('(not run)')
        lines += ['```', '']
    lines += [
        '## Convolver log (lines written during the run)',
        '',
        '```text',
        context['log'],
        '```',
        '',
        f'Files are in `{context["folder"]}`.',
        '',
    ]
    return '\n'.join(lines)


def find_reaper(given):
    for candidate in [given] if given else REAPERS:
        if candidate and Path(candidate).is_file():
            return candidate
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--package', default='.', help='the unzipped package folder (default: this folder)')
    parser.add_argument('--reaper', help='the REAPER program (default: /Applications/REAPER.app)')
    parser.add_argument('--zip', help='the package ZIP, for its SHA-256 (default: the folder name plus .zip)')
    parser.add_argument('--timeout', type=int, default=300, help='seconds to wait for each REAPER run')
    parser.add_argument('--play-seconds', type=float, default=1.5, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    package = Path(args.package).resolve()
    missing = [str(path) for path in (CLI, INPUT, EXPECTED, PRESET) if not (package / path).exists()]
    if missing:
        print(f'reaper_check: {package} is not an unzipped package folder; missing: {", ".join(missing)}')
        return 2
    reaper = find_reaper(args.reaper)
    if not reaper:
        print('reaper_check: REAPER not found; give its program with --reaper')
        return 2
    if reaper_running():
        print('reaper_check: REAPER is running. Save your work, quit REAPER, and run this again.')
        return 2

    when = dt.datetime.now().astimezone()
    folder = package / f'reaper-check-{when:%Y%m%d-%H%M%S}'
    folder.mkdir()
    project = folder / 'reaper-check.rpp'
    common = {
        'output': str(folder),
        'rate': RATE,
        'play_seconds': args.play_seconds,
        'fx_names': list(FX_NAMES),
    }
    first_lua = folder / 'first.lua'
    first_lua.write_text(
        lua_script(
            'first',
            {
                **common,
                'results': str(folder / 'first.txt'),
                'input': str(package / INPUT),
                'preset': str(package / PRESET),
                'render': FIRST_RENDER,
                'project': str(project),
            },
        ),
        encoding='utf-8',
    )
    reopen_lua = folder / 'reopen.lua'
    reopen_lua.write_text(
        lua_script(
            'reopen',
            {
                **common,
                'results': str(folder / 'reopen.txt'),
                'render': REOPENED_RENDER,
                'project': str(folder / 'reaper-check-reopened.rpp'),
            },
        ),
        encoding='utf-8',
    )

    start = log_size()
    print(f'reaper_check: REAPER starts twice; leave it alone. Results go in {folder}')
    print('  If REAPER asks anything, answer it and note what it asked.')
    first = run_stage(reaper, 'first', ['-new', first_lua], folder, folder / 'first.txt', args.timeout)
    reopen = None
    if project.exists():
        reopen = run_stage(reaper, 'reopen', [project, reopen_lua], folder, folder / 'reopen.txt', args.timeout)
    expected = package / EXPECTED
    cli = package / CLI
    first_compare = compare(cli, folder / f'{FIRST_RENDER}.wav', expected)
    reopen_compare = compare(cli, folder / f'{REOPENED_RENDER}.wav', expected)
    checks = evaluate(first, reopen, first_compare, reopen_compare, project)

    archive = Path(args.zip) if args.zip else package.with_name(package.name + '.zip')
    installed = [bundle for bundle in BUNDLES if bundle.exists()]
    macos = command_output(['sw_vers']) or ''
    cli_version = command_output([str(cli), '--version']) or '(bin/convolver --version printed nothing)'
    context = {
        'when': f'{when:%Y-%m-%d %H:%M}',
        'checks': checks,
        'first': first,
        'reopen': reopen,
        'first_compare': first_compare,
        'reopen_compare': reopen_compare,
        'reaper': reaper,
        'macos': ', '.join(line.split(':', 1)[-1].strip() for line in macos.splitlines()) or platform.platform(),
        'machine': platform.machine(),
        'package': package,
        'package_sha': f'`{sha256(archive)}` (`{archive.name}`)'
        if archive.is_file()
        else 'not found (give the ZIP with --zip)',
        'cli_version': '; '.join(line.strip() for line in cli_version.splitlines() if line.strip()),
        'bundles': '; '.join(f'`{bundle}` {bundle_version(bundle)}' for bundle in installed)
        + (' (two copies: REAPER may load either)' if len(installed) > 1 else '')
        if installed
        else 'none found in ~/Library or /Library',
        'play_seconds': args.play_seconds,
        'project': project,
        'log': new_log_lines(start),
        'folder': folder,
    }
    result = folder / 'result.md'
    result.write_text(report(context), encoding='utf-8')
    failed = [name for name, passed, _ in checks if not passed]
    print(f'reaper_check: {"PASS" if not failed else "FAIL"}, {len(checks) - len(failed)} of {len(checks)} checks.')
    for name in failed:
        print(f'  failed: {name}')
    print(f'  Report: {result}')
    if shutil.which('pbcopy'):
        print(f'  To paste it back: pbcopy < "{result}"')
    return 0 if not failed else 1


if __name__ == '__main__':
    sys.exit(main())
