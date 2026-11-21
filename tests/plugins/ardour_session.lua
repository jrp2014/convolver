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
--   CONVOLVER_PLUGIN  the plug-in: its URI (LV2) or its processor class ID (VST3)
--   CONVOLVER_PRESET  the preset's label
--   CONVOLVER_LATENCY    the Latency to choose, in frames
--   CONVOLVER_LATENCIES  the Latency menu's choices, in frames, in order
--   CONVOLVER_INPUTS, CONVOLVER_OUTPUTS  the track's channels
--   CONVOLVER_FRAMES  how much to record
--   CONVOLVER_TAIL    frames to add after the recording
--   CONVOLVER_LEAD    frames of silence before it
--   CONVOLVER_ARDOUR_MAJOR  Ardour's major version (8 or 9)
--   CONVOLVER_PINS    optional, sets the plug-in's channels by hand, as the
--                     Pin Connections window's Manual Config does:
--                     "outputs" sets its outputs to the track's and leaves
--                     its inputs as Ardour has them (as in Ardour 8's window,
--                     which has no input-pin setting for a VST3); "all"
--                     sets its inputs to the track's too (Ardour 9's window
--                     has an Audio Input Pins setting)
--   CONVOLVER_LOG     Convolver's log file, which the plug-in also reads to
--                     know where to write it (the test passes the same path
--                     as XDG_STATE_HOME gives); with CONVOLVER_PINS the
--                     script waits for the plug-in to start with those
--                     outputs before it loads the preset (see below)
--   CONVOLVER_STRICT_IO  optional; 1 makes the track Strict-I/O, as a track
--                     added in Ardour's window is by default (a track the
--                     script adds is otherwise Flexible-I/O)
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
if os.getenv("CONVOLVER_STRICT_IO") == "1" then
	step("make the track Strict-I/O")
	track:set_strict_io(true)
end
print("Track Pin Mode: " .. (track:strict_io() and "Strict-I/O" or "Flexible-I/O"))
-- Each output goes straight to the master bus channel of the same number,
-- and on to the export: with more than two channels, the panners would
-- spread each channel over its neighbours.
track:panner_shell():set_bypassed(true)
session:master_out():panner_shell():set_bypassed(true)

step("add the plug-in")
local plugin_type = ({ LV2 = ARDOUR.PluginType.LV2, VST3 = ARDOUR.PluginType.VST3 })[format]
assert(plugin_type, "CONVOLVER_FORMAT must be LV2 or VST3, not " .. format)
local processor = ARDOUR.LuaAPI.new_plugin(session, identifier, plugin_type, "")
if processor:isnil() then
	-- What Ardour does list, to tell a plug-in it never found from one it
	-- found under another name.
	local listed = 0
	for info in ARDOUR.LuaAPI.list_plugins():iter() do
		if info.type == plugin_type then
			listed = listed + 1
			print("Ardour lists " .. format .. " plug-in " .. info.name .. " (" .. info.unique_id .. ") from " .. info.path)
		end
	end
	error("Ardour cannot find the " .. format .. " plug-in " .. identifier .. " among the " .. listed .. " it lists")
end
assert(track:add_processor_by_index(processor, 0, nil, true) == 0, "cannot add the plug-in to the track")
local pins = os.getenv("CONVOLVER_PINS") or ""
if pins ~= "" then
	assert(pins == "outputs" or pins == "all", "CONVOLVER_PINS is neither outputs nor all")
	local sinks = inputs
	if pins == "outputs" then
		sinks = processor:to_insert():natural_input_streams():n_audio()
	end
	step("set the plug-in's channels by hand: " .. sinks .. " input pins, " .. outputs .. " outputs")
	local log_path = assert(os.getenv("CONVOLVER_LOG"), "CONVOLVER_PINS needs CONVOLVER_LOG")
	local function log_text()
		local file = io.open(log_path, "rb")
		if not file then
			return ""
		end
		local text = file:read("a")
		file:close()
		return text
	end
	local before = #log_text()
	local audio = ARDOUR.DataType("audio")
	assert(track:customize_plugin_insert(processor, 1, ARDOUR.ChanCount(audio, outputs), ARDOUR.ChanCount(audio, sinks)),
		"Ardour refused " .. sinks .. " input pins, " .. outputs .. " outputs for the plug-in")
	-- Ardour 8.4 restarts the plug-in with the new outputs a little after
	-- customize_plugin_insert returns, while it goes on running it with the
	-- old ones. A filter chosen in between is judged against those, refused
	-- and not tried again (HOST-10), so the session would save the refusal.
	-- A user sees the change in the window before choosing a filter: wait,
	-- as they do, until the log says the plug-in started with these outputs.
	local started = "Started at %d+ Hz, %d+ in, " .. outputs .. " out"
	local waited = 0
	while true do
		local text = log_text()
		if #text < before then
			before = 0 -- The log was rotated to convolver.log.old.
		end
		if text:sub(before + 1):find(started) then
			break
		end
		assert(waited < 10000, "the plug-in did not start with " .. outputs .. " outputs within 10 s of setting them")
		ARDOUR.LuaAPI.usleep(50000)
		waited = waited + 50
	end
	step("the plug-in started with " .. outputs .. " outputs after " .. waited .. " ms")
end
local plugin = processor:to_insert():plugin(0)
local preset = plugin:preset_by_label(label)
assert(preset, "Ardour does not list the preset " .. label)
assert(plugin:load_preset(preset), "Ardour cannot load the preset " .. label)
print("Loaded preset " .. label .. " into " .. plugin:name())
-- HOST-10: Ardour's natural streams for the plug-in (its count of the
-- plug-in's pins, which can differ from the channels the plug-in was given:
-- the Convolver log says those) and the insert's on the track side, for
-- the record; and what Ardour calls each input parameter, since its generic
-- controls leave out every one it names "hidden".
local insert = processor:to_insert()
print("Natural streams: " .. insert:natural_input_streams():n_audio() .. " in, " ..
	insert:natural_output_streams():n_audio() .. " out")
print("Insert channels: " .. insert:input_streams():n_audio() .. " in, " ..
	insert:output_streams():n_audio() .. " out")
for n = 0, plugin:parameter_count() - 1 do
	local id, found = plugin:nth_parameter(n, true)
	if found[2] and plugin:parameter_is_input(id) then
		local _, descriptor = plugin:get_parameter_descriptor(id, ARDOUR.ParameterDescriptor())
		print("Parameter " .. n .. ": " .. descriptor[2].label)
	end
end

-- Latency, as a user chooses it from the host's controls after the preset.
-- Ardour lists the LV2 port as Latency, in frames. Ardour (8.4 and 9.0) names every
-- VST3 parameter that cannot be automated "hidden", so the VST3 one is found
-- by its shape instead: the one input whose steps are the menu's entries
-- (Gain mode has three). Its value is the entry's index.
step("choose Latency " .. latency)
local which, value
local matches = 0
for n = 0, plugin:parameter_count() - 1 do
	local id, found = plugin:nth_parameter(n, true)
	if found[2] and plugin:parameter_is_input(id) then
		local _, descriptor = plugin:get_parameter_descriptor(id, ARDOUR.ParameterDescriptor())
		descriptor = descriptor[2]
		if descriptor.label == "Latency" and descriptor.upper == latencies[#latencies] then
			matches = matches + 1
			which, value = n, latency
		elseif descriptor.integer_step and descriptor.lower == 0 and descriptor.upper == #latencies - 1 then
			matches = matches + 1
			for index, frames in ipairs(latencies) do
				if frames == latency then
					which, value = n, index - 1
				end
			end
		end
	end
end
-- Exactly one, so that a host listing its parameters differently fails
-- here rather than setting another one.
assert(matches == 1, matches .. " parameters look like Latency; expected one")
assert(which and value, "the Latency parameter does not offer " .. latency .. " frames")
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
