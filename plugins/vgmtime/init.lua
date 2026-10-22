-- vgmtime: VGM capture control and overlay.
--   * hotkey (default Alt+R, see HOTKEY below) pauses the current capture or starts the next
--     one (<game>_00.vgm, _01.vgm, ...); needs -vgmwrite 1, add -vgmpause to start paused
--   * overlay (top right): capture file and its position in m:ss.ss and samples, or "paused";
--     emulated time, independent of the emulation speed
local exports = {
	name = 'vgmtime',
	version = '0.0.2',
	description = 'VGM capture hotkey and time overlay',
	license = 'BSD-3-Clause',
	author = { name = 'yoyofr' } }

local HOTKEY = 'KEYCODE_R KEYCODE_LALT'   -- input sequence tokens (see MAME -listinput / input tokens)

local vgmtime = exports

function vgmtime.startplugin()
	local seq = nil
	local was_pressed = false
	local flash = 0

	emu.register_frame_done(function()
		local machine = manager.machine
		local ok, enabled = pcall(function() return machine.vgm_enabled end)
		if not ok or not enabled then
			return
		end
		if seq == nil then
			seq = machine.input:seq_from_tokens(HOTKEY)
		end
		local pressed = machine.input:seq_pressed(seq)
		if pressed and not was_pressed then
			local active = machine:vgm_toggle()
			machine:popmessage(active and ('VGM: capture ' .. machine.vgm_file .. '.vgm') or 'VGM: capture stopped')
			flash = 90
		end
		was_pressed = pressed

		local str, fg
		if machine.vgm_active then
			local t = machine.vgm_samples / 44100
			str = string.format('REC %s  %d:%05.2f  %d', machine.vgm_file, math.floor(t / 60), t % 60, machine.vgm_samples)
			fg = 0xfff0f010
		else
			str = 'VGM paused  (' .. HOTKEY:gsub('KEYCODE_', ''):gsub(' ', '+') .. ')'
			fg = 0xffa0a0a0
		end
		if flash > 0 then
			flash = flash - 1
			if (flash // 10) % 2 == 1 then fg = 0xffffffff end
		end
		machine.render.ui_container:draw_text('right', 0, str, fg, 0xc0000000)
	end)
end

return exports
