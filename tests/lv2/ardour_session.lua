-- SPDX-License-Identifier: GPL-2.0-or-later
-- Builds an Ardour session around Convolver's LV2 plug-in for
-- tests/lv2/ardour_tests.py, which runs it with Ardour's headless Lua
-- interpreter (ardour8-lua) and then exports the session with ardour8-export.
--
-- It opens a session made by ardour8-new_session (master bus with one
-- channel per plug-in output) on Ardour's Dummy audio backend, adds a track
-- with the plug-in, loads a preset made by convolver-preset from the host's
-- preset list, records the Dummy backend's white noise onto the track, turns
-- off the region's fades, moves it later, extends the session past the
-- filter's tail and saves. The settings come from the environment:
--   CONVOLVER_SESSION_DIR, CONVOLVER_SESSION_NAME  the session
--   CONVOLVER_URI     the plug-in's URI
--   CONVOLVER_PRESET  the preset's label
--   CONVOLVER_INPUTS, CONVOLVER_OUTPUTS  the track's channels
--   CONVOLVER_FRAMES  how much to record
--   CONVOLVER_TAIL    frames to add after the recording
--   CONVOLVER_LEAD    frames of silence before it
-- Each step that fails stops the script with an error, which the test reports.

local function setting(name)
	local value = os.getenv(name)
	assert(value and value ~= "", name .. " is not set")
	return value
end

local directory = setting("CONVOLVER_SESSION_DIR")
local name = setting("CONVOLVER_SESSION_NAME")
local uri = setting("CONVOLVER_URI")
local label = setting("CONVOLVER_PRESET")
local inputs = tonumber(setting("CONVOLVER_INPUTS"))
local outputs = tonumber(setting("CONVOLVER_OUTPUTS"))
local frames = tonumber(setting("CONVOLVER_FRAMES"))
local tail = tonumber(setting("CONVOLVER_TAIL"))
local lead = tonumber(setting("CONVOLVER_LEAD"))

-- The Dummy backend is hidden unless asked for. At "10x Speed" it runs ten
-- times faster than real time; its inputs carry the chosen test signal.
ARDOUR.config():set_hide_dummy_backend(false)
local backend = AudioEngine:set_backend("None (Dummy)", "", "")
assert(not backend:isnil(), "Ardour has no Dummy backend")
assert(backend:set_driver("10x Speed") == 0, "cannot choose the Dummy backend's speed")
assert(backend:set_device_name("Uniform White Noise") == 0, "cannot choose the Dummy backend's signal")
assert(backend:set_sample_rate(48000) == 0, "cannot set the sample rate")

local session = load_session(directory, name)
assert(session, "cannot open the session")
print("Ardour engine running: " .. tostring(AudioEngine:running()) .. " on " .. AudioEngine:current_backend_name())

local track = session:new_audio_track(inputs, outputs, nil, 1, "Convolver",
	ARDOUR.PresentationInfo.max_order, ARDOUR.TrackMode.Normal, true):front()
assert(track, "cannot add a track")
-- Each output goes straight to the master bus channel of the same number,
-- and on to the export: with more than two channels, the panners would
-- spread each channel over its neighbours.
track:panner_shell():set_bypassed(true)
session:master_out():panner_shell():set_bypassed(true)

local processor = ARDOUR.LuaAPI.new_plugin(session, uri, ARDOUR.PluginType.LV2, "")
assert(not processor:isnil(), "Ardour cannot find " .. uri)
assert(track:add_processor_by_index(processor, 0, nil, true) == 0, "cannot add the plug-in to the track")
local plugin = processor:to_insert():plugin(0)
local preset = plugin:preset_by_label(label)
assert(preset, "Ardour does not list the preset " .. label)
assert(plugin:load_preset(preset), "Ardour cannot load the preset " .. label)
print("Loaded preset " .. label .. " into " .. plugin:name())

-- Record: the track records its input, before the plug-in.
track:rec_enable_control():set_value(1, PBD.GroupControlDisposition.NoGroup)
session:maybe_enable_record(false)
session:request_roll(ARDOUR.TransportRequestSource.TRS_UI)
local waited = 0
while session:transport_sample() < frames do
	assert(waited < 6000, "the transport did not reach " .. frames .. " frames within 60 s")
	ARDOUR.LuaAPI.usleep(10000)
	waited = waited + 1
end
session:request_stop(false, false, ARDOUR.TransportRequestSource.TRS_UI)
waited = 0
while session:transport_rolling() do
	assert(waited < 1000, "the transport did not stop within 10 s")
	ARDOUR.LuaAPI.usleep(10000)
	waited = waited + 1
end
-- The butler finishes writing the take.
ARDOUR.LuaAPI.usleep(500000)

local regions = {}
for region in track:playlist():region_list():iter() do
	regions[#regions + 1] = region
end
assert(#regions == 1, "expected one recorded region, found " .. #regions)
local region = regions[1]:to_audioregion()
region:set_fade_in_active(false)
region:set_fade_out_active(false)
-- Ardour's gain stages ramp up over the first thousand or so frames of an
-- export; the take starts after they have settled on silence.
region:set_position(Temporal.timepos_t(lead))
print("Recorded " .. region:length():samples() .. " frames, now from " .. region:position():samples())

local range = session:locations():session_range_location()
assert(range, "the session has no range after recording")
range:set_start(Temporal.timepos_t(0), false)
range:set_end(Temporal.timepos_t(lead + region:length():samples() + tail), false)
print("Session range " .. range:start():samples() .. " to " .. range:_end():samples())

session:save_state("", false, false, false, false, false)
close_session()
print("Saved the session")
