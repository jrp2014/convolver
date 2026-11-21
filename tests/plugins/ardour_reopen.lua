-- SPDX-License-Identifier: GPL-2.0-or-later
-- Reopens a session that ardour_session.lua saved, in a new ardourN-lua
-- process, and checks the latency the restored plug-in reports to Ardour:
-- the Latency the session was saved with (HOST-9). tests/plugins/ardour_tests.py
-- runs it before the export, which then checks that the audio is in time.
-- The settings come from the environment:
--   CONVOLVER_SESSION_DIR, CONVOLVER_SESSION_NAME  the session
--   CONVOLVER_LATENCY  the Latency it was saved with, in frames

if io and io.stdout then
	io.stdout:setvbuf("no")
end

local function setting(name)
	local value = os.getenv(name)
	assert(value and value ~= "", name .. " is not set")
	return value
end

local latency = tonumber(setting("CONVOLVER_LATENCY"))

-- The same backend as ardour_session.lua: a session opens on the engine.
ARDOUR.config():set_hide_dummy_backend(false)
local backend = AudioEngine:set_backend("None (Dummy)", "", "")
assert(not backend:isnil(), "Ardour has no Dummy backend")
assert(backend:set_driver("10x Speed") == 0, "cannot choose the Dummy backend's speed")
assert(backend:set_sample_rate(48000) == 0, "cannot set the sample rate")

local session = load_session(setting("CONVOLVER_SESSION_DIR"), setting("CONVOLVER_SESSION_NAME"))
assert(session, "cannot reopen the session")
local track = session:route_by_name("Convolver")
assert(not track:isnil(), "the reopened session has no Convolver track")
local processor = track:nth_plugin(0)
assert(not processor:isnil(), "the reopened track has no plug-in")

-- The restored filter is prepared on a worker; its latency follows.
local waited = 0
while processor:signal_latency() ~= latency and waited < 1000 do
	ARDOUR.LuaAPI.usleep(10000)
	waited = waited + 1
end
print("Reopened: Ardour reports latency " .. processor:signal_latency())
close_session()
