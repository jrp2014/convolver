-- SPDX-License-Identifier: GPL-2.0-or-later
-- Builds an Ardour session around Convolver's LV2 or VST3 plug-in for
-- tests/plugins/ardour_tests.py, which runs it with Ardour's headless Lua
-- interpreter (ardourN-lua, Ardour 9 or 8) and then exports the session with
-- ardourN-export.
--
-- It opens a session made by ardourN-new_session (master bus with one
-- channel per plug-in output) on Ardour's Dummy audio backend, adds a track
-- with the plug-in, loads a preset made by convolver-preset from the host's
-- preset list, sets the plug-in's Latency through the host's parameter,
-- records the Dummy backend's white noise onto the track, turns off the
-- region's fades, moves it later, extends the session past the filter's tail
-- and saves. The settings come from the environment:
--   CONVOLVER_SESSION_DIR, CONVOLVER_SESSION_NAME  the session
--   CONVOLVER_FORMAT  LV2 or VST3
--   CONVOLVER_PLUGIN  the plug-in: its URI (LV2) or its name (VST3)
--   CONVOLVER_PRESET  the preset's label
--   CONVOLVER_LATENCY    the Latency to choose, in frames
--   CONVOLVER_LATENCIES  the Latency menu's choices, in frames, in order
--   CONVOLVER_INPUTS, CONVOLVER_OUTPUTS  the track's channels
--   CONVOLVER_FRAMES  how much to record
--   CONVOLVER_TAIL    frames to add after the recording
--   CONVOLVER_LEAD    frames of silence before it
--   CONVOLVER_ARDOUR_MAJOR  Ardour's major version (8 or 9)
-- Each step that fails stops the script with an error, which the test reports.

-- Unbuffered, so that if Ardour crashes the log shows the last step begun.
if io and io.stdout then
	io.stdout:setvbuf("no")
end
local function step(text)
	print("Step: " .. text)
end

local function setting(name)
	local value = os.getenv(name)
	assert(value and value ~= "", name .. " is not set")
	return value
end

local directory = setting("CONVOLVER_SESSION_DIR")
local name = setting("CONVOLVER_SESSION_NAME")
local format = setting("CONVOLVER_FORMAT")
local identifier = setting("CONVOLVER_PLUGIN")
local label = setting("CONVOLVER_PRESET")
local latency = tonumber(setting("CONVOLVER_LATENCY"))
local latencies = {}
for frames in setting("CONVOLVER_LATENCIES"):gmatch("%d+") do
	latencies[#latencies + 1] = tonumber(frames)
end
local inputs = tonumber(setting("CONVOLVER_INPUTS"))
local outputs = tonumber(setting("CONVOLVER_OUTPUTS"))
local frames = tonumber(setting("CONVOLVER_FRAMES"))
local tail = tonumber(setting("CONVOLVER_TAIL"))
local lead = tonumber(setting("CONVOLVER_LEAD"))

-- The Dummy backend is hidden unless asked for. At "10x Speed" it runs ten
-- times faster than real time; its inputs carry the chosen test signal.
step("show the Dummy backend")
ARDOUR.config():set_hide_dummy_backend(false)
step("choose the Dummy backend")
local backend = AudioEngine:set_backend("None (Dummy)", "", "")
assert(not backend:isnil(), "Ardour has no Dummy backend")
step("set the Dummy backend's speed, signal and rate")
assert(backend:set_driver("10x Speed") == 0, "cannot choose the Dummy backend's speed")
assert(backend:set_device_name("Uniform White Noise") == 0, "cannot choose the Dummy backend's signal")
assert(backend:set_sample_rate(48000) == 0, "cannot set the sample rate")

step("open the session")
local session = load_session(directory, name)
assert(session, "cannot open the session")
print("Ardour engine running: " .. tostring(AudioEngine:running()) .. " on " .. AudioEngine:current_backend_name())

step("add a track")
-- No route group: Ardour 9 takes an empty RouteGroup, as its own scripts
-- pass, where Ardour 8 takes nil (Ardour 9 crashes on nil).
local no_group = nil
if tonumber(setting("CONVOLVER_ARDOUR_MAJOR")) >= 9 then
	no_group = ARDOUR.RouteGroup()
end
local track = session:new_audio_track(inputs, outputs, no_group, 1, "Convolver",
	ARDOUR.PresentationInfo.max_order, ARDOUR.TrackMode.Normal, true):front()
assert(track, "cannot add a track")
-- Each output goes straight to the master bus channel of the same number,
-- and on to the export: with more than two channels, the panners would
-- spread each channel over its neighbours.
track:panner_shell():set_bypassed(true)
session:master_out():panner_shell():set_bypassed(true)

step("add the plug-in")
local plugin_type = ({ LV2 = ARDOUR.PluginType.LV2, VST3 = ARDOUR.PluginType.VST3 })[format]
assert(plugin_type, "CONVOLVER_FORMAT must be LV2 or VST3, not " .. format)
local processor = ARDOUR.LuaAPI.new_plugin(session, identifier, plugin_type, "")
assert(not processor:isnil(), "Ardour cannot find the " .. format .. " plug-in " .. identifier)
assert(track:add_processor_by_index(processor, 0, nil, true) == 0, "cannot add the plug-in to the track")
local plugin = processor:to_insert():plugin(0)
local preset = plugin:preset_by_label(label)
assert(preset, "Ardour does not list the preset " .. label)
assert(plugin:load_preset(preset), "Ardour cannot load the preset " .. label)
print("Loaded preset " .. label .. " into " .. plugin:name())

-- Latency, as a user chooses it from the host's controls after the preset.
-- Ardour lists the LV2 port as Latency, in frames. Ardour 8.4 names every
-- VST3 parameter that cannot be automated "hidden", so the VST3 one is found
-- by its shape instead: the one input whose steps are the menu's entries
-- (Gain mode has three). Its value is the entry's index.
step("choose Latency " .. latency)
local which, value
for n = 0, plugin:parameter_count() - 1 do
	local id, found = plugin:nth_parameter(n, true)
	if found[2] and plugin:parameter_is_input(id) then
		local _, descriptor = plugin:get_parameter_descriptor(id, ARDOUR.ParameterDescriptor())
		descriptor = descriptor[2]
		if descriptor.label == "Latency" and descriptor.upper == latencies[#latencies] then
			which, value = n, latency
		elseif descriptor.integer_step and descriptor.lower == 0 and descriptor.upper == #latencies - 1 then
			for index, frames in ipairs(latencies) do
				if frames == latency then
					which, value = n, index - 1
				end
			end
		end
	end
end
assert(which and value, "cannot find a Latency parameter offering " .. latency .. " frames")
assert(ARDOUR.LuaAPI.set_processor_param(processor, which, value), "Ardour refused Latency " .. latency)
assert(ARDOUR.LuaAPI.get_processor_param(processor, which) == value, "Latency did not keep its new value")
print("Latency set to " .. latency .. " frames (parameter " .. which .. ", value " .. value .. ")")
-- The plug-in prepares the new Latency on a worker and then tells the host,
-- which must learn it without the session being reopened.
local waited = 0
while processor:signal_latency() ~= latency do
	assert(waited < 1000, "Ardour reports latency " .. processor:signal_latency() .. ", not " .. latency .. ", after 10 s")
	ARDOUR.LuaAPI.usleep(10000)
	waited = waited + 1
end
print("Ardour reports latency " .. processor:signal_latency())

-- Record: the track records its input, before the plug-in.
step("record")
track:rec_enable_control():set_value(1, PBD.GroupControlDisposition.NoGroup)
session:maybe_enable_record(false)
session:request_roll(ARDOUR.TransportRequestSource.TRS_UI)
waited = 0
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
