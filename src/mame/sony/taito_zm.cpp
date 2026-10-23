// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, hap, superctr, cam900
/***************************************************************************

    Taito Zoom ZSG-2 sound board
    Includes: MN10200 CPU, ZOOM ZSG-2 audio chip, TMS57002 DASP
    By Olivier Galibert.

----------------------------------------------------------------------------

Panasonic MN1020012A Sound CPU (QFP128), 12.5MHz pin 30 (OSCI)

Zoom Corp. ZSG-2 Sound PCM chip (QFP100), 25MHz pin 99

Texas Instruments TMS57002DPHA DSP (QFP80)
* 12.5MHz pin 11 [25/2] (CLKIN)
* 32.5525kHz pin 5 and 76 (LRCKO) (LRCKI)
* 1.5625MHz pin 75 and 2 [25/16] (BCKI) (BCKO)

Newer games have a Panasonic MN1020819DA,
and a Zoom Corp. ZFX-2 DSP instead of the TMS57002 (Functionally identical).

TODO:
- ZSG-2 sound chip emulation might not be perfect, see zsg2.cpp
- check DSP behavior. The DSP is programmed to expect 16-bit samples, but
  the range of the sample values seem really low. Normally the TMS57002 is
  supposed to left-shift 16-bit samples, but is this the case here?

***************************************************************************/

#include "emu.h"
#include "vgmwrite.hpp"
#include "taito_zm.h"
#include "machine/intelfsh.h"

#include <algorithm>

/**************************************************************************/

DEFINE_DEVICE_TYPE(TAITO_ZOOM, taito_zoom_device, "taito_zoom", "Taito Zoom Sound System")

//-------------------------------------------------
//  taito_zoom_device - constructor
//-------------------------------------------------

taito_zoom_device::taito_zoom_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, TAITO_ZOOM, tag, owner, clock),
	device_mixer_interface(mconfig, *this),
	m_soundcpu(*this, "mn10200"),
	m_tms57002(*this, "tms57002"),
	m_zsg2(*this, "zsg2"),
	m_reg_address(0),
	m_tms_ctrl(0),
	m_use_flash(false)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void taito_zoom_device::device_start()
{
	// the ZSG-2 must be started first so that its VGM logger is available
	if (!m_zsg2->started())
		throw device_missing_dependencies();

	m_snd_shared_ram = make_unique_clear<uint8_t[]>(0x100);

	// register for savestates
	save_item(NAME(m_reg_address));
	save_item(NAME(m_tms_ctrl));
	save_pointer(NAME(m_snd_shared_ram), 0x100);

	// VGM logging: board mixer gains (the DSP output gain is unity until the sound CPU programs it)
	m_vgm_gain[0] = m_vgm_gain[1] = 0x100;
	vgm_log_gains();
	machine().vgm_logger().AddRestartCallback([this]() { vgm_dump_dsp(); });
}

/***************************************************************************

  VGM logging (command 0xE4 of the ZSG-2 device)

***************************************************************************/

void taito_zoom_device::vgm_log_raw(uint8_t sub, uint8_t aa, uint16_t data)
{
	const uint8_t raw[4] = { sub, aa, uint8_t(data >> 8), uint8_t(data & 0xff) };
	m_zsg2->get_vgmlog_dev()->WriteRaw(0xE4, raw, 4);
}

// mixer gains (8.8 fixed point): ZSG-2 outputs -> DSP inputs (0.5, 0.5, 1.0, 1.0),
// DSP output 2 -> left and 3 -> right scaled by the global volume (board registers 4/5)
void taito_zoom_device::vgm_log_gains()
{
	vgm_log_raw(6, 0x00, 0x80);
	vgm_log_raw(6, 0x01, 0x80);
	vgm_log_raw(6, 0x02, 0x100);
	vgm_log_raw(6, 0x03, 0x100);
	vgm_log_raw(6, 0x12, m_vgm_gain[0]);
	vgm_log_raw(6, 0x17, m_vgm_gain[1]);
}

// restarted capture: the DSP state is re-created by replaying every host write and control line change
// since reset, with the sample periods the DSP ran in between as pre-runs (command 0xE4 sub 8). This is
// exact (coefficient updates land where the running program reads them, LFO phases), which an upload of
// the program and coefficient memory would not be.
void taito_zoom_device::vgm_dump_dsp()
{
	uint64_t emitted = 0;
	auto prerun_to = [this, &emitted](uint64_t ticks) {
		while (ticks > emitted)
		{
			const uint64_t n = std::min<uint64_t>(ticks - emitted, 0xFFFFFF);
			vgm_log_raw(8, uint8_t(n >> 16), uint16_t(n & 0xFFFF));
			emitted += n;
		}
	};
	for (const vgm_dsp_write &w : m_vgm_dsp_hist)
	{
		prerun_to(w.ticks);
		vgm_log_raw(w.sub, 0, w.data);
	}
	prerun_to(m_tms57002->vgm_total_ticks());
	vgm_log_gains();
}

void taito_zoom_device::vgm_log_dsp(uint8_t sub, uint8_t data)
{
	vgm_log_raw(sub, 0, data);
	m_vgm_dsp_hist.push_back({ m_tms57002->vgm_total_ticks(), sub, data });
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void taito_zoom_device::device_reset()
{
	m_reg_address = 0;

	m_zsg2->reset();
	m_tms57002->reset();
	vgm_log_raw(7, 0, 0);
	m_vgm_dsp_hist.clear();
}


/***************************************************************************

  MN10200 I/O and Memory Map

***************************************************************************/

uint8_t taito_zoom_device::shared_ram_r(offs_t offset)
{
	return m_snd_shared_ram[offset];
}

void taito_zoom_device::shared_ram_w(offs_t offset, uint8_t data)
{
	m_snd_shared_ram[offset] = data;
}


uint8_t taito_zoom_device::tms_ctrl_r()
{
	return m_tms_ctrl;
}

void taito_zoom_device::tms_ctrl_w(uint8_t data)
{
	// According to the TMS57002 manual, reset should NOT be set low during the data transfer.
	//m_tms57002->set_input_line(INPUT_LINE_RESET, data & 0x10 ? CLEAR_LINE : ASSERT_LINE);
	vgm_log_dsp(5, data & 3);
	m_tms57002->cload_w(data & 2);
	m_tms57002->pload_w(data & 1);
	// Other bits unknown (0x9F at most games)
	m_tms_ctrl = data;
}

uint8_t taito_zoom_device::tms_data_r()
{
	return m_tms57002->data_r();
}

void taito_zoom_device::tms_data_w(uint8_t data)
{
	vgm_log_dsp(4, data);
	m_tms57002->data_w(data);
}

void taito_zoom_device::taitozoom_mn_map(address_map &map)
{
	if(m_use_flash) {
		map(0x080000, 0x0fffff).r(":zoomprog", FUNC(intelfsh16_device::read));
	} else {
		map(0x080000, 0x0fffff).rom().region("mn10200", 0);
	}
	map(0x400000, 0x41ffff).ram();
	map(0x800000, 0x8007ff).rw(m_zsg2, FUNC(zsg2_device::read), FUNC(zsg2_device::write));
	map(0xc00000, 0xc00000).rw(FUNC(taito_zoom_device::tms_data_r), FUNC(taito_zoom_device::tms_data_w)); // TMS57002 comms
	map(0xe00000, 0xe000ff).rw(FUNC(taito_zoom_device::shared_ram_r), FUNC(taito_zoom_device::shared_ram_w)); // M66220FP for comms with maincpu
}

void taito_zoom_device::tms57002_map(address_map &map)
{
	map(0x00000, 0x3ffff).ram();
}


/***************************************************************************

  maincpu I/O

***************************************************************************/

void taito_zoom_device::sound_irq_w(uint16_t data)
{
	m_soundcpu->set_input_line(0, ASSERT_LINE);
	m_soundcpu->set_input_line(0, CLEAR_LINE);
}

uint16_t taito_zoom_device::sound_irq_r()
{
	// reads this before writing irq, bit 0 = busy?
	return 0;
}

void taito_zoom_device::reg_data_w(uint16_t data)
{
	switch (m_reg_address)
	{
		case 0x04:
			// zsg2+dsp global volume left
			if (data & 0xc0c0)
				popmessage("ZOOM gain L %04X, contact MAMEdev", data);
			m_tms57002->set_output_gain(2, (data & 0x3f) / 63.0);
			m_vgm_gain[0] = uint16_t((data & 0x3f) * 256 / 63.0 + 0.5);
			vgm_log_raw(6, 0x12, m_vgm_gain[0]);
			break;

		case 0x05:
			// zsg2+dsp global volume right
			if (data & 0xc0c0)
				popmessage("ZOOM gain R %04X, contact MAMEdev", data);
			m_tms57002->set_output_gain(3, (data & 0x3f) / 63.0);
			m_vgm_gain[1] = uint16_t((data & 0x3f) * 256 / 63.0 + 0.5);
			vgm_log_raw(6, 0x17, m_vgm_gain[1]);
			break;

		default:
			break;
	}
}

void taito_zoom_device::reg_address_w(uint16_t data)
{
	m_reg_address = data & 0xff;
}


/***************************************************************************

  Machine Config

***************************************************************************/

void taito_zoom_device::device_add_mconfig(machine_config &config)
{
	/* basic machine hardware */
	MN1020012A(config, m_soundcpu, XTAL(25'000'000)/2);
	m_soundcpu->read_port<1>().set(FUNC(taito_zoom_device::tms_ctrl_r));
	m_soundcpu->write_port<1>().set(FUNC(taito_zoom_device::tms_ctrl_w));
	m_soundcpu->set_addrmap(AS_PROGRAM, &taito_zoom_device::taitozoom_mn_map);

	config.set_maximum_quantum(attotime::from_hz(60000));

	TMS57002(config, m_tms57002, XTAL(25'000'000)/2);
	//m_tms57002->empty_callback().set_inputline(m_soundcpu, MN10200_IRQ1, m_tms57002->empty_r()); /*.invert();*/
	m_tms57002->empty_callback().set_inputline(m_soundcpu, MN10200_IRQ1).invert();

	m_tms57002->set_addrmap(AS_DATA, &taito_zoom_device::tms57002_map);
	m_tms57002->add_route(2, *this, 1.0, 0);
	m_tms57002->add_route(3, *this, 1.0, 1);

	ZSG2(config, m_zsg2, XTAL(25'000'000));
	m_zsg2->add_route(0, *m_tms57002, 0.5, 0); // reverb effect
	m_zsg2->add_route(1, *m_tms57002, 0.5, 1); // chorus effect
	m_zsg2->add_route(2, *m_tms57002, 1.0, 2); // left direct
	m_zsg2->add_route(3, *m_tms57002, 1.0, 3); // right direct
}
