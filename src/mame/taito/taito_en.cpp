// license:BSD-3-Clause
// copyright-holders:Bryan McPhail, Aaron Giles, R. Belmont, Philip Bennett
/***************************************************************************

    Taito Ensoniq ES5505-based sound hardware

    TODO:

    * ES5510 ESP emulation is not perfect
    * ES5510 Input Clock and ES5505 Output channels are same in other PCBs?
      (Currently these are verified from Gun Buster schematics)
    * Where does the MB8421 go? Taito F3 (and Super Chase) have 2 of them on
      the sound area, Taito JC has one.

****************************************************************************/

#include "emu.h"
#include "vgmwrite.hpp"
#include "taito_en.h"
#include <algorithm>


DEFINE_DEVICE_TYPE(TAITO_EN, taito_en_device, "taito_en", "Taito Ensoniq Sound System")

taito_en_device::taito_en_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, TAITO_EN, tag, owner, clock)
	, device_mixer_interface(mconfig, *this)
	, m_audiocpu(*this, "audiocpu")
	, m_ensoniq(*this, "ensoniq")
	, m_esp(*this, "esp")
	, m_pump(*this, "pump")
	, m_duart68681(*this, "duart68681")
	, m_mb87078(*this, "mb87078")
	, m_osram(*this, "osram")
	, m_otisbank(*this, "otisbank")
	, m_otisrom(*this, "ensoniq")
	, m_osrom(*this, "audiocpu")
	, m_cpubank(*this, "cpubank%u", 1)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void taito_en_device::device_start()
{
	// the ES5505 must be started first so that its VGM logger is available
	if (!m_ensoniq->started())
		throw device_missing_dependencies();

	// assumes it can make an address mask with .length() - 1
	assert(!(m_otisrom.length() & (m_otisrom.length() - 1)));

	// tell the pump about the ESP chips
	uint8_t *ROM = m_osrom->base();
	uint32_t max = (m_osrom->bytes() - 0x100000) / 0x20000;
	for (int i = 0; i < 3; i++)
		m_cpubank[i]->configure_entries(0, max, &ROM[0x100000], 0x20000);

	m_bankmask = ((m_otisrom.bytes()) / 0x200000) - 1;

	// VGM logging: board-level configuration, again whenever a capture is restarted at run time
	m_esp->set_vgm_log(m_ensoniq->get_vgmlog_dev());
	vgm_log_board_state();
	machine().vgm_logger().AddRestartCallback([this]() {
		vgm_log_board_state();
		// per-voice banks and the ESP program/registers as they are now
		for (int v = 0; v < 32; v++)
			m_ensoniq->get_vgmlog_dev()->Write(0x00, 0x40 | v, m_otisbank[v] & m_bankmask);
		m_esp->vgm_dump_state();
	});

	// initialize precalculated ES5505 bank table
	const size_t otisbank_size = m_otisbank.bytes() / 2;
	m_calculated_otisbank = make_unique_clear<offs_t[]>(otisbank_size);

	save_item(NAME(m_old_clock));
	save_pointer(NAME(m_calculated_otisbank), otisbank_size);
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void taito_en_device::device_reset()
{
	// sound cpu program loads to 0xc00000 so we use a bank
	uint32_t max = (m_osrom->bytes() - 0x100000) / 0x20000;
	for (int i = 0; i < 3; i++)
		m_cpubank[i]->set_entry(i % max);

	uint16_t *ROM = (uint16_t *)m_osrom->base();
	std::copy(&ROM[0x80000], &ROM[0x80004], &m_osram[0]); // stack and Reset vectors
}


/*************************************
 *
 *  Handlers
 *
 *************************************/

void taito_en_device::en_es5505_bank_w(offs_t offset, uint16_t data)
{
	// mask out unused bits
	m_otisbank[offset] = data;
	m_calculated_otisbank[offset] = (m_otisbank[offset] & m_bankmask) << 20;

	// VGM logging: per-voice external bank (register 0x40+voice, bank in units of 1M words)
	m_ensoniq->get_vgmlog_dev()->Write(0x00, 0x40 | (offset & 0x1f), m_otisbank[offset] & m_bankmask);
}

// VGM logging: sample ROM (both ES5505 banks map the same voice-banked ROM: dumped once, region 1
// aliased to region 0 with 0xD7 02), ESP routing preset (pair 0 direct, pairs 1-3 -> ESP), the
// ESPHALT line and the mixer gains
void taito_en_device::vgm_log_board_state()
{
	m_ensoniq->vgm_dump_rom(0, memregion("ensoniq"));
	static const uint8_t alias[3] = { 0x02, 0x01, 0x00 };
	m_ensoniq->get_vgmlog_dev()->WriteRaw(0xD7, alias, 3);
	static const uint8_t preset[3] = { 0x05, 0x00, 0x00 };
	m_ensoniq->get_vgmlog_dev()->WriteRaw(0xD8, preset, 3);
	m_esp->vgm_log_halt(m_pump->get_esp_halted());
	vgm_log_gains(0, m_vgm_mb_gain[0]);
	vgm_log_gains(1, m_vgm_mb_gain[1]);
}

// VGM logging: the total gain of one side (0 = left, 1 = right) of the audio path, command 0xE3.
// It combines the MB87078 volume with the fixed mixer routes of the board
// (ES5505 outputs -> pump at 0.18, pump (ESP and aux) outputs -> mixer at 0.5).
void taito_en_device::vgm_log_gains(int side, float mb87078_gain)
{
	m_vgm_mb_gain[side & 1] = mb87078_gain;
	auto log_gain = [this](uint8_t channel, float gain)
	{
		const uint16_t g = uint16_t(std::min(gain * 256.0f, 65535.0f) + 0.5f);
		const uint8_t raw[4] = { 0x00, channel, uint8_t(g >> 8), uint8_t(g) };
		m_ensoniq->get_vgmlog_dev()->WriteRaw(0xE3, raw, 4);
	};
	for (int i = 0; i < 4; i++)
		log_gain(side | (i * 2), mb87078_gain * 0.18f);	// ES5505 output channel gain
	log_gain(0x10 + side, mb87078_gain * 0.5f);	// ESP output L/R
	log_gain(0x12 + side, mb87078_gain * 0.5f);	// aux (direct) mix L/R
}

void taito_en_device::en_volume_w(offs_t offset, uint8_t data)
{
	m_mb87078->data_w(offset ^ 1, data);
}


/*************************************
 *
 *  68000 memory map
 *
 *************************************/

void taito_en_device::en_sound_map(address_map &map)
{
	map(0x000000, 0x00ffff).ram().mirror(0x30000).share("osram");
	map(0x140000, 0x140fff).rw("dpram", FUNC(mb8421_device::right_r), FUNC(mb8421_device::right_w)).umask16(0xff00);
	map(0x200000, 0x20001f).rw("ensoniq", FUNC(es5505_device::read), FUNC(es5505_device::write));
	map(0x260000, 0x2601ff).rw("esp", FUNC(es5510_device::host_r), FUNC(es5510_device::host_w)).umask16(0x00ff);
	map(0x280000, 0x28001f).rw("duart68681", FUNC(mc68681_device::read), FUNC(mc68681_device::write)).umask16(0x00ff);
	map(0x300000, 0x30003f).w(FUNC(taito_en_device::en_es5505_bank_w)).share("otisbank");
	map(0x340000, 0x340003).w(FUNC(taito_en_device::en_volume_w)).umask16(0xff00);
	map(0xc00000, 0xc1ffff).bankr("cpubank1");
	map(0xc20000, 0xc3ffff).bankr("cpubank2");
	map(0xc40000, 0xc7ffff).bankr("cpubank3");
	map(0xff0000, 0xffffff).ram().share("osram"); // mirror
}

void taito_en_device::fc7_map(address_map &map)
{
	map(0xfffffd, 0xfffffd).r(m_duart68681, FUNC(mc68681_device::get_irq_vector));
}


/*************************************
 *
 *  ES5505 memory map
 *
 *************************************/

void taito_en_device::en_otis_map(address_map &map)
{
	map(0x000000, 0x0fffff).lr16(
		[this](offs_t offset) -> u16 { return m_otisrom[(m_calculated_otisbank[m_ensoniq->get_voice_index()] + offset) & (m_otisrom.length() - 1)]; }, "banked_otisrom");
}


/*************************************
 *
 *  MB87078 callback
 *
 *************************************/

void taito_en_device::mb87078_gain_changed(offs_t offset, uint8_t data)
{
	if (offset >= 2)
	{
		// TODO: ES5505 Volume control is correct?
		for (int i = 0; i < 4; i++)
			m_ensoniq->set_output_gain((offset & 1) | (i * 2), data / 32.0);

		for (int i = 0; i < 2; i++)
			m_pump->set_output_gain((offset & 1) | (i * 2), data / 32.0);

		vgm_log_gains(offset & 1, data / 32.0);
	}
}


/*************************************
 *
 *  ES5510 callback
 *
 *************************************/

void taito_en_device::es5505_clock_changed(u32 data)
{
	if (m_old_clock != data)
	{
		m_pump->set_unscaled_clock(data);
		m_old_clock = data;
	}
}


/*************************************
 *
 *  Device interfaces
 *
 *************************************/

/*
    68681 I/O pin assignments
    (according to Gun Buster schematics):

    IP0: 5V         OP0-OP5: N/C
    IP1: 5V         OP6: ESPHALT
    IP2: 1MHz       OP7: N/C
    IP3: 0.5MHz
    IP4: 0.5MHz
    IP5: 1MHz
*/

void taito_en_device::duart_output(uint8_t data)
{
	if (data & 0x40)
	{
		if (!m_pump->get_esp_halted())
		{
			logerror("Asserting ESPHALT\n");
			m_pump->set_esp_halted(true);
			m_esp->vgm_log_halt(true);
		}
	}
	else
	{
		if (m_pump->get_esp_halted())
		{
			logerror("Clearing ESPHALT\n");
			m_pump->set_esp_halted(false);
			m_esp->vgm_log_halt(false);
		}
	}
}


//-------------------------------------------------
// device_add_mconfig - add device configuration
//-------------------------------------------------

void taito_en_device::device_add_mconfig(machine_config &config)
{
	// basic machine hardware
	M68000(config, m_audiocpu, 30.47618_MHz_XTAL / 2);
	m_audiocpu->set_addrmap(AS_PROGRAM, &taito_en_device::en_sound_map);
	m_audiocpu->set_addrmap(m68000_device::AS_CPU_SPACE, &taito_en_device::fc7_map);

	ES5510(config, m_esp, 10_MHz_XTAL); // from Gun Buster schematics
	m_esp->set_disable();

	MC68681(config, m_duart68681, 16_MHz_XTAL / 4);
	m_duart68681->set_clocks(16_MHz_XTAL / 2 / 8, 16_MHz_XTAL / 2 / 16, 16_MHz_XTAL / 2 / 16, 16_MHz_XTAL / 2 / 8);
	m_duart68681->irq_cb().set_inputline(m_audiocpu, M68K_IRQ_6);
	m_duart68681->outport_cb().set(FUNC(taito_en_device::duart_output));

	MB87078(config, m_mb87078);
	m_mb87078->gain_changed().set(FUNC(taito_en_device::mb87078_gain_changed));

	MB8421(config, "dpram", 0); // host accesses this from the other side

	// sound hardware
	ESQ_5505_5510_PUMP(config, m_pump, 30.47618_MHz_XTAL / (2 * 16 * 32));
	m_pump->set_esp(m_esp);
	m_pump->add_route(0, *this, 0.5, 0);
	m_pump->add_route(1, *this, 0.5, 1);
	m_pump->add_route(2, *this, 0.5, 0);
	m_pump->add_route(3, *this, 0.5, 1);

	ES5505(config, m_ensoniq, 30.47618_MHz_XTAL / 2);
	m_ensoniq->sample_rate_changed().set(FUNC(taito_en_device::es5505_clock_changed));
	m_ensoniq->set_addrmap(0, &taito_en_device::en_otis_map);
	m_ensoniq->set_addrmap(1, &taito_en_device::en_otis_map);
	m_ensoniq->set_channels(4);
	m_ensoniq->add_route(0, "pump", 0.18, 0);
	m_ensoniq->add_route(1, "pump", 0.18, 1);
	m_ensoniq->add_route(2, "pump", 0.18, 2);
	m_ensoniq->add_route(3, "pump", 0.18, 3);
	m_ensoniq->add_route(4, "pump", 0.18, 4);
	m_ensoniq->add_route(5, "pump", 0.18, 5);
	m_ensoniq->add_route(6, "pump", 0.18, 6);
	m_ensoniq->add_route(7, "pump", 0.18, 7);
}
