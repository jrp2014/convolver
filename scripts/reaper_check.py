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

With --soak (HOST-12), it instead starts REAPER once for a 30-minute soak:
four sparse sources, each sending to a return with Convolver and a long
reverb that this script makes, one filter replaced halfway, and REAPER's
last-underrun times compared before and after
(docs/develop/host-testing.md#a-long-reaper-soak).

The checks are the plan's first version: the smallest run that shows REAPER
doing one render and one save and reopen from a script. Refusals, the wrong host
rate, a Latency change and the Audio Unit stay manual (HOST-13 in TODO;
docs/archive/plans/2026-11-21-host-test-automation.md). It uses only
Python's standard library, so it runs with the python3 that macOS provides.
"""

import argparse
import array
import datetime as dt
import functools
import hashlib
import math
import platform
import plistlib
import random
import shutil
import struct
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
# What Status reads, in full, while the stereo example is active
# (portable/host.cpp). Other texts start the same way but go on to say
# that the filter waits for the host's settings, that Mix 0% plays only the
# dry signal, or that speaker labels differ (jrp's review of 21 November
# 2026), so only the whole text counts.
ACTIVE = 'Filter loaded: 48000 Hz, 2 in, 2 out.'
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
-- A key recorded again keeps its place and takes the new value.
local function record(key, value)
  if value == nil then value = "" end
  local found = false
  for _, entry in ipairs(results) do
    if entry[1] == key then
      entry[2], found = tostring(value), true
    end
  end
  if not found then results[#results + 1] = {key, tostring(value)} end
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

-- Waits past "No filter" and past any waiting text ("Filter selected.
-- Waiting..." or "...; waiting for audio settings"), so that a brief
-- waiting state is not what the check reads.
local function settled(track, fx)
  return function()
    local text = status(track, fx)
    return text:find("^No filter") == nil and text:find("aiting", 1, true) == nil
  end
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

-- REAPER stops at the end of a project, and the example lasts 85 ms, so
-- Status would be read after the transport stopped (jrp's first run, 21
-- November 2026). Repeat over the first seconds keeps it running until the
-- reading; the project's own loop and repeat come back afterwards.
local function play_briefly(track, fx)
  local loop_start, loop_end, repeat_before
  step(function()
    loop_start, loop_end = reaper.GetSet_LoopTimeRange2(project, false, true, 0, 0, false)
    repeat_before = reaper.GetSetRepeatEx(project, -1)
    reaper.GetSet_LoopTimeRange2(project, true, true, 0, CONFIG.play_seconds + 1, false)
    reaper.GetSetRepeatEx(project, 1)
    reaper.SetEditCurPos(0, false, false)
    reaper.OnPlayButton()
    return function() return (reaper.GetPlayState() & 1) == 1 end, 10
  end)
  step(function()
    record("play_started", (reaper.GetPlayState() & 1) == 1 and "yes" or "no")
    return CONFIG.play_seconds
  end)
  step(function()
    record("play_state", reaper.GetPlayState())
    record("play_position", string.format("%.3f", reaper.GetPlayPosition()))
    record("device_rate_playing", device_info("SRATE"))
    record("status_playing", status(track(), fx()))
    reaper.OnStopButton()
    reaper.GetSet_LoopTimeRange2(project, true, true, loop_start, loop_end, false)
    reaper.GetSetRepeatEx(project, repeat_before)
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
  -- A gain of 1 at the centre, whatever the project's default pan law
  -- (taken from REAPER's preferences or template), which -1 would follow.
  reaper.SetMediaTrackInfo_Value(master, "D_PANLAW", 1)
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
  reaper.SetMediaTrackInfo_Value(track, "D_PANLAW", 1)
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

# The soak (HOST-12): four source tracks, each sending to its own return
# with Convolver and a long reverb, played for CONFIG.duration seconds.
LUA_SOAK = r"""
local sources, returns = {}, {}
local soak = {}

-- REAPER's last audio and media underrun times and its clock, in
-- milliseconds, or nil when this REAPER does not report them.
local function underrun_times()
  if not reaper.GetUnderrunTime then return nil end
  local ok, audio, media, now = pcall(reaper.GetUnderrunTime)
  if not ok or type(audio) ~= "number" or type(media) ~= "number" or type(now) ~= "number" then
    return nil
  end
  return audio, media, now
end

local function record_underrun_times(key)
  local audio, media, now = underrun_times()
  if audio then
    record(key, string.format("%d %d %d", audio, media, now))
  else
    record(key, "unavailable")
  end
  return audio, media
end

local function named_track(index, name)
  reaper.InsertTrackAtIndex(index, false)
  local track = reaper.GetTrack(project, index)
  reaper.GetSetMediaTrackInfo_String(track, "P_NAME", name, true)
  reaper.SetMediaTrackInfo_Value(track, "D_VOL", 1)
  reaper.SetMediaTrackInfo_Value(track, "D_PAN", 0)
  reaper.SetMediaTrackInfo_Value(track, "D_PANLAW", 1)
  reaper.SetMediaTrackInfo_Value(track, "I_NCHAN", 2)
  return track
end

local function add_convolver(track)
  for _, name in ipairs(CONFIG.fx_names) do
    local added = reaper.TrackFX_AddByName(track, name, false, -1)
    if added >= 0 then return added end
  end
  error("REAPER has no VST3 plug-in named Convolver. Rescan in Settings > Plug-ins > VST.", 0)
end

local function all_settled()
  for _, entry in ipairs(returns) do
    if not settled(entry.track, entry.fx)() then return false end
  end
  return true
end

step(function()
  record("stage", "soak")
  record("reaper_version", reaper.GetAppVersion())
  record("os", reaper.GetOS())
  record("device_rate", device_info("SRATE"))
  record("device_block", device_info("BSIZE"))
  record("device_output", device_info("IDENT_OUT"))
  record("underrun_api", reaper.GetUnderrunTime and "yes" or "no")
  record_underrun_times("underrun_setup")
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
  reaper.SetMediaTrackInfo_Value(master, "D_PANLAW", 1)
  reaper.SetMediaTrackInfo_Value(master, "I_NCHAN", 2)
  reaper.GetSetProjectInfo(project, "PROJECT_SRATE", CONFIG.rate, true)
  reaper.GetSetProjectInfo(project, "PROJECT_SRATE_USE", 1, true)
  record("project_rate", reaper.GetSetProjectInfo(project, "PROJECT_SRATE", 0, false))
end)

-- The sources: the same short sound for each track, at the times the
-- script outside chose, with silence between (and true silence in the
-- quiet windows). Each source plays dry to the master and sends to its own
-- return at unity.
step(function()
  local items = 0
  for index, sound in ipairs(CONFIG.sounds) do
    local track = named_track(index - 1, "Source " .. index)
    reaper.SetMediaTrackInfo_Value(track, "D_VOL", CONFIG.dry_gain)
    for _, position in ipairs(CONFIG.items[index]) do
      local item = reaper.AddMediaItemToTrack(track)
      local take = reaper.AddTakeToMediaItem(item)
      local source = reaper.PCM_Source_CreateFromFile(sound)
      if not source then error("REAPER could not read " .. sound, 0) end
      reaper.SetMediaItemTake_Source(take, source)
      reaper.SetMediaItemInfo_Value(item, "D_POSITION", position)
      reaper.SetMediaItemInfo_Value(item, "D_LENGTH", (reaper.GetMediaSourceLength(source)))
      reaper.SetMediaItemInfo_Value(item, "D_FADEINLEN", 0)
      reaper.SetMediaItemInfo_Value(item, "D_FADEOUTLEN", 0)
      items = items + 1
    end
    sources[index] = track
  end
  record("items", items)
  reaper.UpdateArrange()
end)

step(function()
  for index, preset in ipairs(CONFIG.presets) do
    local track = named_track(reaper.CountTracks(project), "Reverb " .. index)
    local send = reaper.CreateTrackSend(sources[index], track)
    if send < 0 then error("REAPER made no send from Source " .. index .. " to Reverb " .. index, 0) end
    reaper.SetTrackSendInfo_Value(sources[index], 0, send, "D_VOL", 1)
    local fx = add_convolver(track)
    local _, name = reaper.TrackFX_GetFXName(track, fx, "")
    returns[index] = {track = track, fx = fx}
    record("fx_name_" .. index, name)
    record("fx_type_" .. index, fx_type(track, fx))
    record("preset_applied_" .. index, reaper.TrackFX_SetPreset(track, fx, preset))
  end
  record("tracks", reaper.CountTracks(project))
  return all_settled, CONFIG.settle_seconds
end)

step(function()
  for index, entry in ipairs(returns) do
    record("status_ready_" .. index, status(entry.track, entry.fx))
  end
  record_underrun_times("underrun_before")
  reaper.GetSetRepeatEx(project, 0)
  reaper.SetEditCurPos(0, false, false)
  reaper.OnPlayButton()
  return function() return (reaper.GetPlayState() & 1) == 1 end, 10
end)

-- The soak itself: one test that REAPER calls between its own work until
-- the time is up or the transport stops. Each second it reads the underrun
-- times and the transport; every CONFIG.status_every seconds it reads each
-- return's Status, recording a reading only when it changes; halfway it
-- replaces one return's filter while the others play.
step(function()
  record("play_started", (reaper.GetPlayState() & 1) == 1 and "yes" or "no")
  record("device_rate_playing", device_info("SRATE"))
  soak.start = reaper.time_precise()
  soak.next_poll, soak.next_status = 0, 0
  soak.last_audio, soak.last_media = underrun_times()
  soak.underruns, soak.readings, soak.other, soak.changes = 0, 0, 0, 0
  -- Only changes from the readings before playing are recorded.
  soak.last_status = {}
  for index, entry in ipairs(returns) do
    soak.last_status[index] = status(entry.track, entry.fx)
  end
  local function read_statuses(elapsed)
    for index, entry in ipairs(returns) do
      local text = status(entry.track, entry.fx)
      soak.readings = soak.readings + 1
      if text ~= CONFIG.active then soak.other = soak.other + 1 end
      if text ~= soak.last_status[index] then
        soak.changes = soak.changes + 1
        record("status_change_" .. soak.changes, string.format("%.1f s, Reverb %d: %s", elapsed, index, text))
        soak.last_status[index] = text
      end
    end
    record("status_readings", soak.readings)
    record("status_other", soak.other)
  end
  return function()
    local elapsed = reaper.time_precise() - soak.start
    if elapsed >= soak.next_poll then
      soak.next_poll = soak.next_poll + 1
      local audio, media = underrun_times()
      if audio and (audio ~= soak.last_audio or media ~= soak.last_media) then
        soak.underruns = soak.underruns + 1
        record("underrun_" .. soak.underruns, string.format("%.1f s: audio %d, media %d", elapsed, audio, media))
        soak.last_audio, soak.last_media = audio, media
      end
      if (reaper.GetPlayState() & 1) ~= 1 then
        record("transport_stopped_at", string.format("%.1f", elapsed))
        return true
      end
    end
    if elapsed >= soak.next_status then
      soak.next_status = soak.next_status + CONFIG.status_every
      read_statuses(elapsed)
    end
    if not soak.replaced and elapsed >= CONFIG.replace_at then
      soak.replaced = elapsed
      local entry = returns[CONFIG.replace_return]
      record("replacement_at", string.format("%.1f", elapsed))
      record("replacement_applied", reaper.TrackFX_SetPreset(entry.track, entry.fx, CONFIG.replacement))
    end
    if soak.replaced and not soak.replacement_read then
      local entry = returns[CONFIG.replace_return]
      local waited = elapsed - soak.replaced
      -- The old filter reads the same until the new one plays, so the
      -- reading waits a moment as well as past any waiting text.
      if (waited >= 2 and settled(entry.track, entry.fx)()) or waited >= CONFIG.settle_seconds then
        soak.replacement_read = true
        record("replacement_status", status(entry.track, entry.fx))
        record("replacement_settled_after", string.format("%.1f", waited))
      end
    end
    return elapsed >= CONFIG.duration
  end, CONFIG.duration + 60
end)

step(function()
  local elapsed = reaper.time_precise() - soak.start
  record("soak_seconds", string.format("%.1f", elapsed))
  record("play_state", reaper.GetPlayState())
  record("play_position", string.format("%.3f", reaper.GetPlayPosition()))
  record_underrun_times("underrun_after")
  for index, entry in ipairs(returns) do
    record("status_end_" .. index, status(entry.track, entry.fx))
  end
  reaper.OnStopButton()
  return 0.5
end)

step(function()
  if reaper.Main_SaveProjectEx then
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
    body = {'first': LUA_FIRST, 'reopen': LUA_REOPEN, 'soak': LUA_SOAK}[stage]
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


def run_stage(reaper, name, arguments, folder, results, timeout, *, keep_awake=False):
    """Starts REAPER with `arguments`, waits for the ReaScript to record
    "done" and for REAPER to quit, and returns what happened. With
    `keep_awake`, macOS's caffeinate keeps the Mac from idle sleep while
    REAPER runs."""
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
        if keep_awake and shutil.which('caffeinate'):
            subprocess.Popen(['caffeinate', '-i', '-w', str(process.pid)])  # Ends when REAPER does.
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


def playing(results):
    """Whether REAPER's transport was running when Status was read while
    playing (GetPlayState: 1 playing, 2 paused, 4 recording)."""
    try:
        return int(float(results.get('play_state', ''))) & 1 == 1
    except ValueError:
        return False


def active(text):
    """Whether a Status reading is exactly the stereo example's Active text."""
    return (text or '').strip() == ACTIVE


def loaded_while_playing(results):
    return playing(results) and active(results.get('status_playing'))


def playing_text(results):
    if 'play_state' not in results:
        return 'while playing: (not read)'
    if playing(results):
        when = 'transport running'
    elif results.get('play_started') == 'no':
        when = 'the transport did not start within 10 s: check the audio device in REAPER'
    else:
        when = 'transport stopped, so this is not a reading during playback'
    return (
        f'while playing: {results.get("status_playing", "(none)")} '
        f'(play state {results["play_state"]}, {when}, at {results.get("play_position", "?")} s)'
    )


def same_file(recorded, project):
    """Whether the path REAPER reports is the project saved in the first
    stage (allowing for symbolic links such as /var and /private/var)."""
    if not recorded:
        return False
    try:
        return Path(recorded).samefile(project)
    except OSError:
        return Path(recorded).resolve() == Path(project).resolve()


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
            one.get('preset_applied') == 'true'
            and active(one.get('status_after_preset'))
            and loaded_while_playing(one),
            (
                f'TrackFX_SetPreset returned {one.get("preset_applied") or "nothing"}; '
                f'Status after the preset: {one.get("status_after_preset", "(none)")}; '
                f'{playing_text(one)}{rate_hint(one)}'
            ),
        ),
        ('The render matches expected.wav', first_compare['match'], last_line(first_compare['output'])),
        ('The project is saved', project.exists(), project.name if project.exists() else 'not saved'),
        (
            'REAPER reopened the saved project in a new process and ran the second ReaScript to the end',
            bool(reopen)
            and reopen['finished']
            and 'error' not in two
            and reopen['pid'] != first['pid']
            and same_file(two.get('project_opened'), project),
            (
                two.get('error')
                or reopen.get('problem')
                or f'{reopen["seconds"]} s, process {reopen["pid"]}; REAPER opened '
                f'{two.get("project_opened") or "(no project path)"}'
                + ('' if same_file(two.get('project_opened'), project) else f', not {project}')
            )
            if reopen
            else 'not run',
        ),
        (
            'The reopened project restores the filter',
            active(two.get('status_reopened')) and loaded_while_playing(two),
            (f'Status on reopening: {two.get("status_reopened", "(none)")}; {playing_text(two)}{rate_hint(two)}'),
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
            f'2. REAPER played for {context["play_seconds"]} s, repeating the first '
            f'{context["play_seconds"] + 1} s so that Status is read while the transport runs, then '
            'rendered the master to '
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


# The soak (HOST-12). Its filters and sounds are made here, with Python's
# standard library, so that the run needs only the package and REAPER.
PRESET_TOOL = Path('bin/convolver-preset')
# The four returns, (name, seconds, layout): long stereo and true-stereo
# reverbs as README's "reverb on a send" describes, up to its 30 s.
SOAK_REVERBS = (
    ('stereo-10s', 10, 'stereo'),
    ('true-stereo-10s', 10, 'true stereo'),
    ('stereo-30s', 30, 'stereo'),
    ('true-stereo-30s', 30, 'true stereo'),
)
# Halfway, the first return's filter is replaced by a longer, true-stereo one.
SOAK_REPLACEMENT = ('true-stereo-20s', 20, 'true stereo')
SOAK_REPLACE_RETURN = 1
SOAK_SOUNDS = ('hits', 'chord', 'clicks', 'sweep')
SOAK_SOUND_SECONDS = 2
# The largest audio block that counts as a small buffer, in frames.
SMALL_BLOCK = 128
SOAK_STATUS_EVERY = 10
SOAK_SETTLE_SECONDS = 120
SOAK_DRY_GAIN = 0.5
SOAK_PATH_ENERGY = 0.25  # -6 dB
SOAK_SEED = 1200


def wav_bytes(rate, channels, frames, samples):
    """A 32-bit IEEE float WAV (tag 3) from interleaved float samples."""
    data = array.array('f', samples)
    if sys.byteorder != 'little':
        data.byteswap()
    payload = data.tobytes()
    if len(payload) != frames * channels * 4:
        raise ValueError('the sample count does not match the frames and channels')
    fmt = struct.pack('<HHIIHH', 3, channels, rate, rate * 4 * channels, 4 * channels, 32)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(payload)) + payload
    return b'RIFF' + struct.pack('<I', len(body)) + body


def reverb_samples(seconds, paths, seed, rate=RATE):
    """Interleaved decaying noise, one channel per path, falling 60 dB over
    its length. Each path's energy is SOAK_PATH_ENERGY, so a return is
    plainly heard beside the dry sources without clipping the master."""
    frames = int(seconds * rate)
    generator = random.Random(seed)  # noqa: S311 - a reproducible filter, not a secret
    decay = math.exp(math.log(0.001) / frames)
    samples = array.array('f', bytes(4 * frames * paths))
    envelope = 1.0
    for frame in range(frames):
        base = frame * paths
        for path in range(paths):
            samples[base + path] = envelope * (2 * generator.random() - 1)
        envelope *= decay
    # Uniform noise has a mean square of 1/3; the envelope's mean square is
    # the sum of a geometric series.
    energy = (1 / 3) * (1 - decay ** (2 * frames)) / (1 - decay * decay)
    scale = math.sqrt(SOAK_PATH_ENERGY / energy)
    for index in range(len(samples)):
        samples[index] *= scale
    return frames, samples


TRUE_STEREO_CFG = """// Written by scripts/reaper_check.py --soak (HOST-12): a true-stereo reverb.
{rate} 2 2 3
0 0
0 0
{wav}
0
0
0
{wav}
1
0
1
{wav}
2
1
0
{wav}
3
1
1
"""


def write_reverb(folder, name, seconds, layout, seed):
    """The reverb's WAV, and for true stereo its .cfg; returns what the
    preset tool reads."""
    paths = 2 if layout == 'stereo' else 4
    frames, samples = reverb_samples(seconds, paths, seed)
    wav = folder / f'{name}.wav'
    wav.write_bytes(wav_bytes(RATE, paths, frames, samples))
    if layout == 'stereo':
        return wav
    config = folder / f'{name}.cfg'
    config.write_text(TRUE_STEREO_CFG.format(rate=RATE, wav=wav.name), encoding='utf-8')
    return config


@functools.lru_cache(maxsize=None)
def sound_samples(kind, rate=RATE):
    """Two seconds of stereo, peaking at -12 dBFS, that each source repeats:
    noise hits, a chord, clicks from side to side, or a rising tone."""
    frames = SOAK_SOUND_SECONDS * rate
    left, right = [0.0] * frames, [0.0] * frames
    generator = random.Random(f'{SOAK_SEED}-{kind}')  # noqa: S311 - a reproducible sound
    for i in range(frames):
        t = i / rate
        if kind == 'hits':
            since = t % 0.4
            value = math.exp(-since / 0.05) * (2 * generator.random() - 1) if t < 1.6 else 0.0
            left[i], right[i] = value, value * 0.8
        elif kind == 'chord':
            envelope = min(1.0, t / 0.01) * math.exp(-t / 0.5)
            value = envelope * sum(math.sin(2 * math.pi * f * t) for f in (220.0, 277.18, 329.63)) / 3
            left[i], right[i] = value, value
        elif kind == 'clicks':
            since = t % 0.25
            value = math.exp(-since / 0.002) * math.cos(2 * math.pi * 1700 * since) if since < 0.02 else 0.0
            if int(t / 0.25) % 2:
                right[i] = value
            else:
                left[i] = value
        else:
            # A rising tone, 300 Hz to 3 kHz over 1.5 s (an exponential sweep).
            envelope = min(1.0, t / 0.05, max(0.0, (1.5 - t) / 0.05))
            phase = 2 * math.pi * 300 * 1.5 / math.log(10) * (10 ** (min(t, 1.5) / 1.5) - 1)
            value = envelope * math.sin(phase)
            left[i], right[i] = value * 0.9, value
    peak = max(abs(v) for v in (*left, *right)) or 1.0
    scale = 0.25 / peak
    samples = [0.0] * (2 * frames)
    samples[0::2] = [v * scale for v in left]
    samples[1::2] = [v * scale for v in right]
    return frames, samples


def quiet_windows(duration):
    """Two stretches where every source is silent for longer than the
    longest tail (at most 60 s), so each return goes quiet and wakes again."""
    length = min(60.0, duration / 10)
    return [(round(duration * share, 3), round(duration * share + length, 3)) for share in (0.25, 0.65)]


def soak_schedule(duration, tracks=4, seed=SOAK_SEED):
    """Each source's item start times: the sound, then 3 to 20 s of silence,
    on to at least 5 s past the end, with no sound in the quiet windows."""
    windows = quiet_windows(duration)
    schedule = []
    for track in range(tracks):
        generator = random.Random(seed + track)  # noqa: S311 - a reproducible schedule
        times, start = [], 1.0 + 1.3 * track
        # Until a sound ends past the end, so that REAPER, which can stop
        # at the end of a project, plays the whole time.
        while not times or times[-1] + SOAK_SOUND_SECONDS < duration + 5:
            clash = next((end for begin, end in windows if start < end and start + SOAK_SOUND_SECONDS > begin), None)
            if clash is not None:
                start = clash + generator.uniform(0.5, 3)
                continue
            times.append(round(start, 3))
            start += SOAK_SOUND_SECONDS + generator.uniform(3, 20)
        schedule.append(times)
    return schedule


def make_preset(tool, source, preset):
    """Runs bin/convolver-preset; returns None or what went wrong."""
    try:
        finished = subprocess.run([str(tool), str(source), str(preset)], capture_output=True, text=True, check=False)
    except OSError as error:
        return f'{tool.name} did not run: {error}'
    if finished.returncode != 0 or not preset.is_file():
        said = (finished.stdout + finished.stderr).strip() or f'status {finished.returncode}'
        return f'{tool.name} {source.name}: {said}'
    return None


def prepare_soak(folder, tool):
    """Writes the reverbs, their presets and the sounds into `folder`.
    Returns (presets, replacement, sounds), or raises RuntimeError."""
    filters = folder / 'filters'
    filters.mkdir()
    presets = []
    for number, (name, seconds, layout) in enumerate((*SOAK_REVERBS, SOAK_REPLACEMENT), 1):
        source = write_reverb(filters, name, seconds, layout, SOAK_SEED + 100 * number)
        preset = filters / f'soak-{name}.vstpreset'
        problem = make_preset(tool, source, preset)
        if problem:
            raise RuntimeError(problem)
        presets.append(preset)
    # The presets hold the filters, so the large WAVs can go.
    for wav in filters.glob('*.wav'):
        wav.unlink()
    sounds = []
    for kind in SOAK_SOUNDS:
        frames, samples = sound_samples(kind)
        sound = folder / f'source-{kind}.wav'
        sound.write_bytes(wav_bytes(RATE, 2, frames, samples))
        sounds.append(sound)
    return presets[:-1], presets[-1], sounds


def parse_underrun_times(text):
    """(audio, media, now) in milliseconds, or None when unavailable."""
    try:
        audio, media, now = (int(part) for part in (text or '').split())
    except ValueError:
        return None
    return audio, media, now


UNDERRUN_NONE = 'no new underrun'
UNDERRUN_SOME = 'at least one new underrun'
UNDERRUN_UNAVAILABLE = 'underrun reporting unavailable'


def underrun_result(results):
    """One of the three results, and the evidence for it. REAPER gives the
    time of the latest audio and media underrun, not a count, so the
    comparison is of those times before and after the soak; the readings
    each second only add when they were seen."""
    before = parse_underrun_times(results.get('underrun_before'))
    after = parse_underrun_times(results.get('underrun_after'))
    if results.get('underrun_api') == 'no':
        return UNDERRUN_UNAVAILABLE, 'This REAPER has no GetUnderrunTime.'
    if not before or not after:
        return UNDERRUN_UNAVAILABLE, (
            f'GetUnderrunTime gave no times (before: {results.get("underrun_before", "not read")}; '
            f'after: {results.get("underrun_after", "not read")}).'
        )
    if after[2] <= before[2]:
        return UNDERRUN_UNAVAILABLE, f"REAPER's underrun clock did not advance ({before[2]} ms, then {after[2]} ms)."
    seen = [value for key, value in results.items() if key.startswith('underrun_') and key[9:].isdigit()]
    times = (
        f'last audio underrun {before[0]} ms before and {after[0]} ms after; last media underrun {before[1]} ms '
        f'before and {after[1]} ms after; clock {before[2]} ms to {after[2]} ms'
    )
    if before[:2] == after[:2] and not seen:
        return UNDERRUN_NONE, f'Both times unchanged ({times}).'
    sightings = f' Seen at: {"; ".join(seen)}.' if seen else ''
    return UNDERRUN_SOME, f'The times changed ({times}).{sightings}'


def block_size(results):
    try:
        return int(float(results.get('device_block', '')))
    except ValueError:
        return None


def evaluate_soak(outcome, duration):
    """The soak's checks, in order: (name, passed, evidence)."""
    one = outcome['results']
    count = len(SOAK_REVERBS)
    loaded = [
        one.get(f'preset_applied_{n}') == 'true' and active(one.get(f'status_ready_{n}')) for n in range(1, count + 1)
    ]
    block = block_size(one)
    try:
        ran = float(one.get('soak_seconds', ''))
    except ValueError:
        ran = 0.0
    try:
        readings = int(one.get('status_readings', ''))
        other = int(one.get('status_other', ''))
    except ValueError:
        readings, other = 0, None
    changes = [value for key, value in one.items() if key.startswith('status_change_')]
    ends = [one.get(f'status_end_{n}') for n in range(1, count + 1)]
    replaced = SOAK_REPLACE_RETURN
    return [
        (
            'REAPER ran the soak ReaScript to the end',
            outcome['finished'] and 'error' not in one,
            one.get('error') or outcome.get('problem') or f'{outcome["seconds"]} s',
        ),
        (
            f"Each of the {count} returns has Convolver's VST3 with its reverb loaded",
            all(loaded)
            and all('Convolver' in one.get(f'fx_name_{n}', '') for n in range(1, count + 1))
            and len(loaded) == count,
            '; '.join(
                f'Reverb {n} ({SOAK_REVERBS[n - 1][0]}): {one.get(f"fx_name_{n}", "(not added)")}, preset '
                f'{one.get(f"preset_applied_{n}") or "not applied"}, Status {one.get(f"status_ready_{n}", "(none)")}'
                for n in range(1, count + 1)
            ),
        ),
        (
            f'The audio device ran at {RATE} Hz with a block of {SMALL_BLOCK} frames or fewer',
            block is not None
            and block <= SMALL_BLOCK
            and one.get('device_rate') == str(RATE)
            and one.get('device_rate_playing', str(RATE)) == str(RATE),
            (
                f'{one.get("device_rate", "?")} Hz ({one.get("device_rate_playing", "?")} Hz while playing), '
                f'block {one.get("device_block", "not reported")}'
                + (
                    ''
                    if block is not None and block <= SMALL_BLOCK
                    else f". Set the block size to {SMALL_BLOCK} or fewer in REAPER's Settings > Audio > Device "
                    'and run again.'
                )
                + rate_hint(one)
            ),
        ),
        (
            'The transport ran for the whole soak',
            one.get('play_started') == 'yes' and 'transport_stopped_at' not in one and ran >= duration,
            (
                f'transport stopped after {one["transport_stopped_at"]} s of {duration:g} s'
                if 'transport_stopped_at' in one
                else f'{ran:g} s of {duration:g} s; play position at the end {one.get("play_position", "?")} s'
            ),
        ),
        (
            'Every return read Active at every reading, and at the end',
            readings > 0 and other == 0 and all(active(text) for text in ends),
            f'{readings} readings while playing, {other if other is not None else "?"} not Active'
            + (f'; changes: {"; ".join(changes)}' if changes else '')
            + ''.join(
                f'; at the end, Reverb {n}: {text or "(not read)"}'
                for n, text in enumerate(ends, 1)
                if not active(text)
            ),
        ),
        (
            f"Replacing Reverb {replaced}'s filter while playing loaded the new one",
            one.get('replacement_applied') == 'true' and active(one.get('replacement_status')),
            (
                f'at {one.get("replacement_at", "?")} s TrackFX_SetPreset returned '
                f'{one.get("replacement_applied") or "nothing"}; Status '
                f'{one.get("replacement_settled_after", "?")} s later: {one.get("replacement_status", "(none)")}'
            )
            if 'replacement_at' in one
            else 'not reached',
        ),
    ]


def soak_report(context):
    """result.md for the soak: the result, the checks, and what every host
    report lists."""
    checks, outcome = context['checks'], context['outcome']
    one = outcome['results']
    failed = [name for name, passed, _ in checks if not passed]
    result, evidence = context['underruns']
    duration = context['duration']
    setup_before = parse_underrun_times(one.get('underrun_setup'))
    setup_after = parse_underrun_times(one.get('underrun_before'))
    setup_note = (
        'REAPER reported an underrun while the filters loaded, before the soak started '
        f'(times {one.get("underrun_setup")}, then {one.get("underrun_before")}); it does not count.'
        if setup_before and setup_after and setup_before[:2] != setup_after[:2]
        else 'none reported.'
    )
    lines = [
        f'# REAPER soak, {context["when"]}',
        '',
        (
            f'**Result: {result}.** {len(checks) - len(failed)} of {len(checks)} checks passed. '
            'Written by `scripts/reaper_check.py --soak` (HOST-12).'
        ),
        '',
        f'Underruns: {evidence}',
        '',
        '| # | Check | Result | Evidence |',
        '| --- | --- | --- | --- |',
    ]
    for number, (name, passed, text) in enumerate(checks, 1):
        lines.append(f'| {number} | {cell(name)} | {"Pass" if passed else "**FAIL**"} | {cell(text)} |')
    reverbs = ', '.join(
        f'Reverb {n}: {name} ({seconds} s {layout})' for n, (name, seconds, layout) in enumerate(SOAK_REVERBS, 1)
    )
    windows = ', '.join(f'{begin:g} to {end:g} s' for begin, end in context['windows'])
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
        (
            f'- Project rate: {one.get("project_rate", "(not reported)")} Hz; audio device: '
            f'{one.get("device_output", "(not reported)")} at {one.get("device_rate", "?")} Hz, '
            f'block size {one.get("device_block", "?")}'
        ),
        (
            f'- Channels: {len(SOAK_SOUNDS)} stereo source tracks, each sending to its own stereo return '
            f'(2 in, 2 out) and playing dry to the stereo master'
        ),
        (
            f'- Default template: {one.get("template_tracks", "?")} tracks and '
            f'{one.get("template_master_fx", "?")} master effects removed'
        ),
        '',
        '## Steps',
        '',
        (
            f'1. REAPER started with a new project at {RATE} Hz (process {outcome["pid"]}). The ReaScript made '
            f'{len(SOAK_SOUNDS)} source tracks with {one.get("items", "?")} items of 2-second sounds '
            f'({", ".join(SOAK_SOUNDS)}) separated by 3 to 20 s of silence, and a return for each with '
            f'Convolver (VST3) and a decaying-noise reverb from a preset made by `bin/convolver-preset`: {reverbs}.'
        ),
        (
            f"2. With every return Active, it played for {duration:g} s from the start, reading REAPER's "
            f"underrun times and transport each second and each return's Status every {SOAK_STATUS_EVERY} s. "
            f'All sources were silent from {windows}'
            + (
                ', longer than the longest tail.'
                if context['windows'][0][1] - context['windows'][0][0] > max(seconds for _, seconds, _ in SOAK_REVERBS)
                else ', shorter than the longest tail at this length.'
            )
        ),
        (
            f'3. At {one.get("replacement_at", "?")} s, while playing, it loaded {SOAK_REPLACEMENT[0]} '
            f'({SOAK_REPLACEMENT[1]} s {SOAK_REPLACEMENT[2]}) into Reverb {SOAK_REPLACE_RETURN}.'
        ),
        (
            f'4. It stopped, saved the project as `{context["project"].name}` and quit '
            f'({"by itself" if outcome.get("quit_by_itself") else "only when this script stopped it"}).'
        ),
        '',
        f'Underruns while the filters loaded: {setup_note}',
        '',
        '## What the ReaScript recorded',
        '',
        '```text',
    ]
    lines += [f'{key}: {value}' for key, value in one.items()] or ['(nothing)']
    if outcome.get('problem'):
        lines.append(f'problem: {outcome["problem"]}')
    lines += [
        '```',
        '',
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


def environment(args, package, reaper):
    """What every host report lists about the machine and the package."""
    cli = package / CLI
    archive = Path(args.zip) if args.zip else package.with_name(package.name + '.zip')
    installed = [bundle for bundle in BUNDLES if bundle.exists()]
    macos = command_output(['sw_vers']) or ''
    cli_version = command_output([str(cli), '--version']) or '(bin/convolver --version printed nothing)'
    return {
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
    }


def run_soak(args, package, reaper):
    """The soak (HOST-12): one REAPER run of --soak-minutes, then result.md."""
    duration = args.soak_minutes * 60
    when = dt.datetime.now().astimezone()
    folder = package / f'reaper-soak-{when:%Y%m%d-%H%M%S}'
    folder.mkdir()
    print(f"reaper_check: making the soak's filters and sounds in {folder}")
    try:
        presets, replacement, sounds = prepare_soak(folder, package / PRESET_TOOL)
    except RuntimeError as error:
        print(f"reaper_check: could not make the soak's presets: {error}")
        return 2
    project = folder / 'reaper-soak.rpp'
    script = folder / 'soak.lua'
    script.write_text(
        lua_script(
            'soak',
            {
                'results': str(folder / 'soak.txt'),
                'project': str(project),
                'rate': RATE,
                'fx_names': list(FX_NAMES),
                'active': ACTIVE,
                'presets': [str(preset) for preset in presets],
                'replacement': str(replacement),
                'replace_return': SOAK_REPLACE_RETURN,
                'replace_at': duration / 2,
                'sounds': [str(sound) for sound in sounds],
                'items': soak_schedule(duration),
                'dry_gain': SOAK_DRY_GAIN,
                'duration': duration,
                'status_every': SOAK_STATUS_EVERY,
                'settle_seconds': SOAK_SETTLE_SECONDS,
            },
        ),
        encoding='utf-8',
    )
    start = log_size()
    print(f'reaper_check: REAPER plays for {args.soak_minutes:g} minutes; leave it alone.')
    print('  If REAPER asks anything, answer it and note what it asked.')
    outcome = run_stage(
        reaper,
        'soak',
        ['-new', script],
        folder,
        folder / 'soak.txt',
        args.timeout + duration + SOAK_SETTLE_SECONDS,
        keep_awake=True,
    )
    checks = evaluate_soak(outcome, duration)
    underruns = underrun_result(outcome['results'])
    context = {
        'when': f'{when:%Y-%m-%d %H:%M}',
        'checks': checks,
        'outcome': outcome,
        'underruns': underruns,
        'duration': duration,
        'windows': quiet_windows(duration),
        'project': project,
        'log': new_log_lines(start),
        'folder': folder,
        **environment(args, package, reaper),
    }
    result = folder / 'result.md'
    result.write_text(soak_report(context), encoding='utf-8')
    failed = [name for name, passed, _ in checks if not passed]
    print(f'reaper_check: {underruns[0]}; {len(checks) - len(failed)} of {len(checks)} checks passed.')
    for name in failed:
        print(f'  failed: {name}')
    print(f'  Report: {result}')
    if shutil.which('pbcopy'):
        print(f'  To paste it back: pbcopy < "{result}"')
    return 0 if not failed and underruns[0] == UNDERRUN_NONE else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--package', default='.', help='the unzipped package folder (default: this folder)')
    parser.add_argument('--reaper', help='the REAPER program (default: /Applications/REAPER.app)')
    parser.add_argument('--zip', help='the package ZIP, for its SHA-256 (default: the folder name plus .zip)')
    parser.add_argument('--timeout', type=int, default=300, help='seconds to wait for each REAPER run')
    parser.add_argument('--play-seconds', type=float, default=1.5, help=argparse.SUPPRESS)
    parser.add_argument(
        '--soak', action='store_true', help='instead of the checks, the long soak with four reverb returns (HOST-12)'
    )
    parser.add_argument(
        '--soak-minutes', type=float, default=30, help='how long the soak plays, in minutes (default: 30)'
    )
    args = parser.parse_args(argv)
    if args.soak_minutes <= 0:
        parser.error('--soak-minutes must be more than 0')

    package = Path(args.package).resolve()
    needed = (CLI, PRESET_TOOL) if args.soak else (CLI, INPUT, EXPECTED, PRESET)
    missing = [str(path) for path in needed if not (package / path).exists()]
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
    if args.soak:
        return run_soak(args, package, reaper)

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

    context = {
        'when': f'{when:%Y-%m-%d %H:%M}',
        'checks': checks,
        'first': first,
        'reopen': reopen,
        'first_compare': first_compare,
        'reopen_compare': reopen_compare,
        'play_seconds': args.play_seconds,
        'project': project,
        'log': new_log_lines(start),
        'folder': folder,
        **environment(args, package, reaper),
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
