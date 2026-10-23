// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, hap, superctr, cam900
/***************************************************************************

    Taito Zoom ZSG-2 sound board

***************************************************************************/
#ifndef MAME_SONY_TAITO_ZM_H
#define MAME_SONY_TAITO_ZM_H

#pragma once

#include "cpu/mn10200/mn10200.h"
#include "cpu/tms57002/tms57002.h"
#include "sound/zsg2.h"

class taito_zoom_device : public device_t, public device_mixer_interface
{
public:
	static constexpr feature_type imperfect_features() { return feature::SOUND; }

	taito_zoom_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	void sound_irq_w(uint16_t data);
	uint16_t sound_irq_r();
	void reg_data_w(uint16_t data);
	void reg_address_w(uint16_t data);

	uint8_t shared_ram_r(offs_t offset);
	void shared_ram_w(offs_t offset, uint8_t data);

	void set_use_flash() { m_use_flash = true; }

	void taitozoom_mn_map(address_map &map) ATTR_COLD;
	void tms57002_map(address_map &map) ATTR_COLD;

protected:
	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	// inherited devices/pointers
	required_device<mn10200_device> m_soundcpu;
	required_device<tms57002_device> m_tms57002;
	required_device<zsg2_device> m_zsg2;

	// internal state
	uint16_t m_reg_address;
	uint8_t m_tms_ctrl;
	bool m_use_flash;
	std::unique_ptr<uint8_t[]> m_snd_shared_ram;

	uint8_t tms_ctrl_r();
	void tms_ctrl_w(uint8_t data);
	uint8_t tms_data_r();
	void tms_data_w(uint8_t data);

	// VGM logging (through the ZSG-2 device, command 0xE4)
	struct vgm_dsp_write { uint64_t ticks; uint8_t sub; uint8_t data; };
	std::vector<vgm_dsp_write> m_vgm_dsp_hist;   // DSP host writes and control lines since reset
	uint16_t m_vgm_gain[2];
	void vgm_log_raw(uint8_t sub, uint8_t aa, uint16_t data);
	void vgm_log_dsp(uint8_t sub, uint8_t data);
	void vgm_log_gains();
	void vgm_dump_dsp();
};

DECLARE_DEVICE_TYPE(TAITO_ZOOM, taito_zoom_device)


#endif // MAME_SONY_TAITO_ZM_H
