-- vgmtime: overlay of the emulated time, i.e. the position in the VGM being logged.
-- Independent of the emulation speed (throttle off, fast forward), so a capture can be
-- timed from the screen.  Shown only when -vgmwrite is active, unless the option cannot be read.
local exports = {
	name = 'vgmtime',
	version = '0.0.1',
	description = 'Emulated time overlay for VGM logging',
	license = 'BSD-3-Clause',
	author = { name = 'yoyofr' } }

local vgmtime = exports

function vgmtime.startplugin()
	local enabled = nil

	local function logging_enabled()
		if enabled == nil then
			local ok, value = pcall(function() return manager.machine.options.entries.vgmwrite:value() end)
			enabled = (not ok) or (tonumber(value) or 0) ~= 0
		end
		return enabled
	end

	emu.register_frame_done(function()
		local machine = manager.machine
		if not logging_enabled() then
			return
		end
		local t = machine.time.seconds + machine.time.attoseconds / 1e18
		local samples = math.floor(t * 44100 + 0.5)
		local str = string.format('VGM %d:%05.2f  %d', math.floor(t / 60), t % 60, samples)
		machine.render.ui_container:draw_text('right', 0, str, 0xfff0f010, 0xc0000000)
	end)
end

return exports
