/*
 * ESFMu: emulator for the ESS "ESFM" enhanced OPL3 clone
 * Copyright (C) 2023 Kagamiin~
 *
 * This file includes code and data from the Nuked OPL3 project, copyright (C)
 * 2013-2023 Nuke.YKT. Its usage, modification and redistribution is allowed
 * under the terms of the GNU Lesser General Public License version 2.1 or
 * later.
 *
 * ESFMu is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 2.1
 * of the License, or (at your option) any later version.
 *
 * ESFMu is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with ESFMu. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * ESFMu wouldn't have been possible without the hard work and dedication of
 * the retro computer hardware research and preservation community.
 *
 * I'd like to thank:
 *  - Nuke.YKT
 *        Developer of Nuked OPL3, which was the basis for ESFMu's code and
 *        also a great learning resource on Yamaha FM synthesis for myself.
 *        Nuke.YKT also gives shoutouts on behalf of Nuked OPL3 to:
 *        - MAME Development Team(Jarek Burczynski, Tatsuyuki Satoh):
 *              Feedback and Rhythm part calculation information.
 *        - forums.submarine.org.uk(carbon14, opl3):
 *              Tremolo and phase generator calculation information.
 *        - OPLx decapsulated(Matthew Gambrell, Olli Niemitalo):
 *              OPL2 ROMs.
 *        - siliconpr0n.org(John McMaster, digshadow):
 *              YMF262 and VRC VII decaps and die shots.
 * - rainwarrior
 *       For performing the initial research on ESFM drivers and documenting
 *       ESS's patent on native mode operator organization.
 * - jwt27
 *       For kickstarting the ESFM research project and compiling rainwarrior's
 *       findings and more in an accessible document ("ESFM Demystified").
 * - pachuco/CatButts
 *       For documenting ESS's patent on ESFM's feedback implementation, which
 *       was vital in getting ESFMu's sound output to be accurate.
 * - And everybody who helped out with real hardware testing
 */

#include "esfm.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>


/*
 * Table of KSL values extracted from OPL3 ROM; taken straight from Nuked OPL3
 * source code.
 * TODO: Check if ESFM uses the same KSL values.
 */

static const int16 kslrom[16] = {
	0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};

/*
 * This maps the low 5 bits of emulation mode address to an emulation mode
 * slot; taken straight from Nuked OPL3. Used for decoding certain emulation
 * mode address ranges.
 */
static const int8_t ad_slot[0x20] = {
	 0,  1,  2,  3,  4,  5, -1, -1,  6,  7,  8,  9, 10, 11, -1, -1,
	12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/*
 * This maps an emulation mode slot index to a tuple representing the
 * corresponding native mode channel and slot.
 */
static const emu_slot_channel_mapping emu_slot_map[36] =
{
	{ 0, 0}, { 1, 0}, { 2, 0}, { 0, 1}, { 1, 1}, { 2, 1},
	{ 3, 0}, { 4, 0}, { 5, 0}, { 3, 1}, { 4, 1}, { 5, 1},
	{ 6, 0}, { 7, 0}, { 8, 0}, { 6, 1}, { 7, 1}, { 8, 1},
	{ 9, 0}, {10, 0}, {11, 0}, { 9, 1}, {10, 1}, {11, 1},
	{12, 0}, {13, 0}, {14, 0}, {12, 1}, {13, 1}, {14, 1},
	{15, 0}, {16, 0}, {17, 0}, {15, 1}, {16, 1}, {17, 1}
};

/*
 * This encodes which emulation mode channels are the secondary channel in a
 * 4-op channel pair (where the entry is non-negative), and which is the
 * corresponding primary channel for that secondary channel.
 */
static const int emu_4op_secondary_to_primary[18] =
{
	-1, -1, -1, 0,  1,  2, -1, -1, -1,
	-1, -1, -1, 9, 10, 11, -1, -1, -1
};

/*
 * This encodes the operator outputs to be enabled or disabled for
 * each 4-op algorithm in emulation mode.
 * Indices: FM+FM, FM+AM, AM+FM, AM+AM (lower channel MSB, upper channel LSB)
 * Values: enable OP1, OP2, OP3, OP4
 */
static const bool emu_4op_alg_output_enable[4][4] =
{
	{0, 0, 0, 1},
	{0, 1, 0, 1},
	{1, 0, 0, 1},
	{1, 0, 1, 1}
};

/*
 * This encodes the operator interconnections to be enabled or disabled for
 * each 4-op algorithm in emulation mode.
 * Indices: FM+FM, FM+AM, AM+FM, AM+AM (lower channel MSB, upper channel LSB)
 * Values: enable OP1FB, OP1->2, OP2->3, OP3->4
 */
static const bool emu_4op_alg_mod_enable[4][4] =
{
	{1, 1, 1, 1},
	{1, 1, 0, 1},
	{1, 0, 1, 1},
	{1, 0, 1, 0}
};


/* ------------------------------------------------------------------------- */
static void
ESFM_emu_rearrange_connections(esfm_channel *channel)
{
	int secondary_to_primary;

	secondary_to_primary = emu_4op_secondary_to_primary[channel->channel_idx];
	if (secondary_to_primary >= 0)
	{
		esfm_channel *pair_primary = &channel->chip->channels[secondary_to_primary];
		if (pair_primary->emu_mode_4op_enable)
		{
			// always work from primary channel in pair when dealing with 4-op
			channel = pair_primary;
		}
	}

	if (channel->emu_mode_4op_enable && (channel->channel_idx % 9) < 3 && channel->chip->emu_newmode)
	{
		esfm_channel *secondary = &channel->chip->channels[channel->channel_idx + 3];
		uint2 algorithm = ((channel->slots[0].emu_connection_typ != 0) << 1)
			| (secondary->slots[0].emu_connection_typ != 0);
		int i;

		secondary->slots[0].in.mod_input = &channel->slots[1].in.output;

		for (i = 0; i < 2; i++)
		{
			channel->slots[i].in.emu_mod_enable =
				emu_4op_alg_mod_enable[algorithm][i] ? ~((int13) 0) : 0;
			channel->slots[i].in.emu_output_enable =
				emu_4op_alg_output_enable[algorithm][i] ? ~((int13) 0) : 0;

			secondary->slots[i].in.emu_mod_enable =
				emu_4op_alg_mod_enable[algorithm][i + 2] ? ~((int13) 0) : 0;
			secondary->slots[i].in.emu_output_enable =
				emu_4op_alg_output_enable[algorithm][i + 2] ? ~((int13) 0) : 0;
		}
	}
	else if ((channel->chip->emu_rhy_mode_flags & 0x20) != 0
		&& (channel->channel_idx == 7 || channel->channel_idx == 8))
	{
		channel->slots[0].in.emu_mod_enable = 0;
		channel->slots[1].in.emu_mod_enable = 0;
		channel->slots[0].in.emu_output_enable = ~((int13) 0);
		channel->slots[1].in.emu_output_enable = ~((int13) 0);
	}
	else
	{
		channel->slots[0].in.mod_input = &channel->slots[0].in.feedback_buf;

		channel->slots[0].in.emu_mod_enable = ~((int13) 0);
		channel->slots[0].in.emu_output_enable =
			(channel->slots[0].emu_connection_typ != 0) ? ~((int13) 0) : 0;
		channel->slots[1].in.emu_output_enable = ~((int13) 0);
		channel->slots[1].in.emu_mod_enable =
			(channel->slots[0].emu_connection_typ != 0) ? 0 : ~((int13) 0);
	}
}


/* ------------------------------------------------------------------------- */
static void
ESFM_emu_to_native_switch(esfm_chip *chip)
{
	size_t channel_idx, slot_idx;
	for (channel_idx = 0; channel_idx < 18; channel_idx++)
	{
		for (slot_idx = 0; slot_idx < 4; slot_idx++)
		{
			esfm_channel *channel = &chip->channels[channel_idx];
			esfm_slot *slot = &channel->slots[slot_idx];

			if (slot_idx == 0)
			{
				slot->in.mod_input = &slot->in.feedback_buf;
			}
			else
			{
				esfm_slot *prev_slot = &channel->slots[slot_idx - 1];
				slot->in.mod_input = &prev_slot->in.output;
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
static void
ESFM_native_to_emu_switch(esfm_chip *chip)
{
	size_t channel_idx;
	for (channel_idx = 0; channel_idx < 18; channel_idx++)
	{
		ESFM_emu_rearrange_connections(&chip->channels[channel_idx]);
	}
}

/* ------------------------------------------------------------------------- */
static void
ESFM_slot_update_keyscale(esfm_slot *slot)
{
	if (slot->slot_idx > 0 && !slot->chip->native_mode)
	{
		return;
	}

	int16 ksl = (kslrom[slot->f_num >> 6] << 2) - ((0x08 - slot->block) << 5);
	if (ksl < 0)
	{
		ksl = 0;
	}
	slot->in.eg_ksl_offset = ksl;
	slot->in.keyscale = (slot->block << 1)
		| ((slot->f_num >> (8 + !slot->chip->keyscale_mode)) & 0x01);
}

/* ------------------------------------------------------------------------- */
static void
ESFM_emu_channel_update_keyscale(esfm_channel *channel)
{
	int secondary_to_primary;

	secondary_to_primary = emu_4op_secondary_to_primary[channel->channel_idx];
	if (secondary_to_primary >= 0)
	{
		esfm_channel *pair_primary = &channel->chip->channels[secondary_to_primary];
		if (pair_primary->emu_mode_4op_enable)
		{
			// always work from primary channel in pair when dealing with 4-op
			channel = pair_primary;
		}
	}

	ESFM_slot_update_keyscale(&channel->slots[0]);
	channel->slots[1].in.eg_ksl_offset = channel->slots[0].in.eg_ksl_offset;
	channel->slots[1].in.keyscale = channel->slots[0].in.keyscale;

	if (channel->emu_mode_4op_enable && (channel->channel_idx % 9) < 3 && channel->chip->emu_newmode)
	{
		int i;
		esfm_channel *secondary = &channel->chip->channels[channel->channel_idx + 3];
		secondary->slots[0].f_num = channel->slots[0].f_num;
		secondary->slots[0].block = channel->slots[0].block;

		for (i = 0; i < 2; i++)
		{
			secondary->slots[i].in.eg_ksl_offset = channel->slots[0].in.eg_ksl_offset;
			secondary->slots[i].in.keyscale = channel->slots[0].in.keyscale;
		}
	}
}

/* ------------------------------------------------------------------------- */
static inline uint8_t
ESFM_slot_readback (esfm_slot *slot, uint8_t register_idx)
{
	uint8_t data = 0;
	switch (register_idx & 0x07)
	{
	case 0x00:
		data |= (slot->tremolo_en != 0) << 7;
		data |= (slot->vibrato_en != 0) << 6;
		data |= (slot->env_sustaining != 0) << 5;
		data |= (slot->vibrato_en != 0) << 4;
		data |= slot->mult & 0x0f;
		break;
	case 0x01:
		data |= slot->ksl << 6;
		data |= slot->t_level & 0x3f;
		break;
	case 0x02:
		data |= slot->attack_rate << 4;
		data |= slot->decay_rate & 0x0f;
		break;
	case 0x03:
		data |= slot->sustain_lvl << 4;
		data |= slot->release_rate & 0x0f;
		break;
	case 0x04:
		data = slot->f_num & 0xff;
		break;
	case 0x05:
		data |= slot->env_delay << 5;
		data |= (slot->block & 0x07) << 2;
		data |= (slot->f_num >> 8) & 0x03;
		break;
	case 0x06:
		data |= (slot->tremolo_deep != 0) << 7;
		data |= (slot->vibrato_deep != 0) << 6;
		data |= (slot->out_enable[1] != 0) << 5;
		data |= (slot->out_enable[0] != 0) << 4;
		data |= (slot->mod_in_level & 0x07) << 1;
		data |= slot->emu_connection_typ & 0x01;
		break;
	case 0x07:
		data |= slot->output_level << 5;
		data |= (slot->rhy_noise & 0x03) << 3;
		data |= slot->waveform & 0x07;
		break;
	}
	return data;
}

/* ------------------------------------------------------------------------- */
static inline void
ESFM_slot_write (esfm_slot *slot, uint8_t register_idx, uint8_t data)
{
	switch (register_idx & 0x07)
	{
	case 0x00:
		slot->tremolo_en = (data & 0x80) != 0;
		slot->vibrato_en = (data & 0x40) != 0;
		slot->env_sustaining = (data & 0x20) != 0;
		slot->ksr = (data & 0x10) != 0;
		slot->mult = data & 0x0f;
		break;
	case 0x01:
		slot->ksl = data >> 6;
		slot->t_level = data & 0x3f;
		ESFM_slot_update_keyscale(slot);
		break;
	case 0x02:
		slot->attack_rate = data >> 4;
		slot->decay_rate = data & 0x0f;
		break;
	case 0x03:
		slot->sustain_lvl = data >> 4;
		slot->release_rate = data & 0x0f;
		break;
	case 0x04:
		slot->f_num = (slot->f_num & 0x300) | data;
		ESFM_slot_update_keyscale(slot);
		break;
	case 0x05:
		if (slot->env_delay < (data >> 5))
		{
			slot->in.eg_delay_transitioned_01 = 1;
		}
		else if (slot->env_delay > (data >> 5))
		{
			slot->in.eg_delay_transitioned_10 = 1;
		}
		slot->env_delay = data >> 5;
		slot->emu_key_on = (data >> 5) & 0x01;
		slot->block = (data >> 2) & 0x07;
		slot->f_num = (slot->f_num & 0xff) | ((data & 0x03) << 8);
		ESFM_slot_update_keyscale(slot);
		break;
	case 0x06:
		slot->tremolo_deep = (data & 0x80) != 0;
		slot->vibrato_deep = (data & 0x40) != 0;
		slot->out_enable[1] = (data & 0x20) ? ~((int13) 0) : 0;
		slot->out_enable[0] = (data & 0x10) ? ~((int13) 0) : 0;
		slot->mod_in_level = (data >> 1) & 0x07;
		slot->emu_connection_typ = data & 0x01;
		break;
	case 0x07:
		slot->output_level = data >> 5;
		slot->rhy_noise = (data >> 3) & 0x03;
		slot->waveform = data & 0x07;
		break;
	}
}

#define KEY_ON_REGS_START (18 * 4 * 8)
#define TIMER1_REG (0x402)
#define TIMER2_REG (0x403)
#define TIMER_SETUP_REG (0x404)
#define CONFIG_REG (0x408)
#define BASSDRUM_REG (0x4bd)
#define TEST_REG (0x501)
#define FOUROP_CONN_REG (0x504)
#define NATIVE_MODE_REG (0x505)

/* ------------------------------------------------------------------------- */
static void
ESFM_write_reg_native (esfm_chip *chip, uint16_t address, uint8_t data)
{
	int i;
	address = address & 0x7ff;

	if (address < KEY_ON_REGS_START)
	{
		// Slot register write
		size_t channel_idx = address >> 5;
		size_t slot_idx = (address >> 3) & 0x03;
		size_t register_idx = address & 0x07;
		esfm_slot *slot = &chip->channels[channel_idx].slots[slot_idx];

		ESFM_slot_write(slot, register_idx, data);
	}
	else if (address < KEY_ON_REGS_START + 16)
	{
		// Key-on registers
		size_t channel_idx = (address - KEY_ON_REGS_START);
		esfm_channel *channel = &chip->channels[channel_idx];
		channel->key_on = data & 0x01;
		channel->emu_mode_4op_enable = (data & 0x02) != 0;
	}
	else if (address < KEY_ON_REGS_START + 20)
	{
		// Key-on channels 17 and 18 (each half)
		size_t channel_idx = 16 + ((address & 0x02) >> 1);
		bool second_half = address & 0x01;
		esfm_channel *channel = &chip->channels[channel_idx];
		if (second_half)
		{
			channel->key_on_2 = data & 0x01;
			channel->emu_mode_4op_enable_2 = (data & 0x02) != 0;
		}
		else
		{
			channel->key_on = data & 0x01;
			channel->emu_mode_4op_enable = (data & 0x02) != 0;
		}
	}
	else
	{
		switch (address & 0x5ff)
		{
		case TIMER1_REG:
			chip->timer_reload[0] = data;
			chip->timer_counter[0] = data;
			break;
		case TIMER2_REG:
			chip->timer_reload[1] = data;
			chip->timer_counter[1] = data;
			break;
		case TIMER_SETUP_REG:
			if (data & 0x80)
			{
				chip->irq_bit = 0;
				chip->timer_overflow[0] = 0;
				chip->timer_overflow[1] = 0;
				break;
			}
			chip->timer_enable[0] = (data & 0x01) != 0;
			chip->timer_enable[1] = (data & 0x02) != 0;
			chip->timer_mask[1] = (data & 0x20) != 0;
			chip->timer_mask[0] = (data & 0x40) != 0;
			break;
		case CONFIG_REG:
			chip->keyscale_mode = (data & 0x40) != 0;
			break;
		case BASSDRUM_REG:
			chip->emu_rhy_mode_flags = data & 0x3f;
			chip->emu_vibrato_deep = (data & 0x40) != 0;
			chip->emu_tremolo_deep = (data & 0x80) != 0;
			break;
		case FOUROP_CONN_REG:
			for (i = 0; i < 3; i++)
			{
				chip->channels[i].emu_mode_4op_enable = (data >> i) & 0x01;
				chip->channels[i + 9].emu_mode_4op_enable = (data >> (i + 3)) & 0x01;
			}
			break;
		case TEST_REG:
			chip->test_bit_w0_r5_eg_halt = (data & 0x01) | ((data & 0x20) != 0);
			chip->test_bit_1_distort = (data & 0x02) != 0;
			chip->test_bit_2 = (data & 0x04) != 0;
			chip->test_bit_3 = (data & 0x08) != 0;
			chip->test_bit_4_attenuate = (data & 0x10) != 0;
			chip->test_bit_w5_r0 = (data & 0x20) != 0;
			chip->test_bit_6_phase_stop_reset = (data & 0x40) != 0;
			chip->test_bit_7 = (data & 0x80) != 0;
			break;
		}
	}
}

/* ------------------------------------------------------------------------- */
static uint8_t
ESFM_readback_reg_native (esfm_chip *chip, uint16_t address)
{
	int i;
	uint8_t data = 0;
	address = address & 0x7ff;

	if (address < KEY_ON_REGS_START)
	{
		// Slot register read
		size_t channel_idx = address >> 5;
		size_t slot_idx = (address >> 3) & 0x03;
		size_t register_idx = address & 0x07;
		esfm_slot *slot = &chip->channels[channel_idx].slots[slot_idx];

		data = ESFM_slot_readback(slot, register_idx);
	}
	else if (address < KEY_ON_REGS_START + 16)
	{
		// Key-on registers
		size_t channel_idx = (address - KEY_ON_REGS_START);
		esfm_channel *channel = &chip->channels[channel_idx];

		data |= channel->key_on != 0;
		data |= (channel->emu_mode_4op_enable != 0) << 1;
	}
	else if (address < KEY_ON_REGS_START + 20)
	{
		// Key-on channels 17 and 18 (each half)
		size_t channel_idx = 16 + ((address & 0x02) >> 1);
		bool second_half = address & 0x01;
		esfm_channel *channel = &chip->channels[channel_idx];
		if (second_half)
		{
			data |= channel->key_on_2 != 0;
			data |= (channel->emu_mode_4op_enable_2 != 0) << 1;
		}
		else
		{
			data |= channel->key_on != 0;
			data |= (channel->emu_mode_4op_enable != 0) << 1;
		}
	}
	else
	{
		switch (address & 0x5ff)
		{
		case TIMER1_REG:
			data = chip->timer_counter[0];
			break;
		case TIMER2_REG:
			data = chip->timer_counter[1];
			break;
		case TIMER_SETUP_REG:
			data |= chip->timer_enable[0] != 0;
			data |= (chip->timer_enable[1] != 0) << 1;
			data |= (chip->timer_mask[1] != 0) << 5;
			data |= (chip->timer_mask[0] != 0) << 6;
			break;
		case CONFIG_REG:
			data |= (chip->keyscale_mode != 0) << 6;
			break;
		case BASSDRUM_REG:
			data |= chip->emu_rhy_mode_flags;
			data |= chip->emu_vibrato_deep << 6;
			data |= chip->emu_tremolo_deep << 7;
			break;
		case TEST_REG:
			data |= chip->test_bit_w5_r0 != 0;
			data |= (chip->test_bit_1_distort != 0) << 1;
			data |= (chip->test_bit_2 != 0) << 2;
			data |= (chip->test_bit_3 != 0) << 3;
			data |= (chip->test_bit_4_attenuate != 0) << 4;
			data |= (chip->test_bit_w0_r5_eg_halt != 0) << 5;
			data |= (chip->test_bit_6_phase_stop_reset != 0) << 6;
			data |= (chip->test_bit_7 != 0) << 7;
			break;
		case FOUROP_CONN_REG:
			for (i = 0; i < 3; i++)
			{
				data |= (chip->channels[i].emu_mode_4op_enable != 0) << i;
				data |= (chip->channels[i + 9].emu_mode_4op_enable != 0) << (i + 3);
			}
			break;
		case NATIVE_MODE_REG:
			data |= (chip->emu_newmode != 0);
			data |= (chip->native_mode != 0) << 7;
			break;
		}
	}
	return data;
}

/* ------------------------------------------------------------------------- */
static void
ESFM_write_reg_emu (esfm_chip *chip, uint16_t address, uint8_t data)
{
	bool high = (address & 0x100) != 0;
	uint8_t reg = address & 0xff;
	int emu_slot_idx = ad_slot[address & 0x1f];
	int natv_chan_idx = -1;
	int natv_slot_idx = -1;
	int emu_chan_idx = (reg & 0x0f) > 8 ? -1 : ((reg & 0x0f) + high * 9);

	if (emu_slot_idx >= 0)
	{
		if (high)
		{
			emu_slot_idx += 18;
		}

		natv_chan_idx = emu_slot_map[emu_slot_idx].channel_idx;
		natv_slot_idx = emu_slot_map[emu_slot_idx].slot_idx;
	}

	if (reg == 0xbd)
	{
		chip->emu_rhy_mode_flags = data & 0x3f;
		chip->emu_vibrato_deep = (data & 0x40) != 0;
		chip->emu_tremolo_deep = (data & 0x80) != 0;
		if (chip->emu_rhy_mode_flags & 0x20)
		{
			// TODO: check if writes to 0xbd actually affect the readable key-on flags at
			// 0x246, 0x247, 0x248; and if there's any visible effect from the SD and TC flags
			chip->channels[6].key_on = (data & 0x10) != 0;
			chip->channels[7].key_on = (data & 0x01) != 0;
			chip->channels[8].key_on = (data & 0x04) != 0;
			chip->channels[7].key_on_2 = (data & 0x08) != 0;
			chip->channels[8].key_on_2 = (data & 0x02) != 0;
		}
		ESFM_emu_rearrange_connections(&chip->channels[7]);
		ESFM_emu_rearrange_connections(&chip->channels[8]);
		return;
	}

	switch(reg & 0xf0)
	{
	case 0x00:
		if (high)
		{
			int i;
			switch(reg & 0x0f)
			{
			case 0x01:
				chip->emu_wavesel_enable = (data & 0x20) != 0;
				break;
			case 0x02:
				chip->timer_reload[0] = data;
				chip->timer_counter[0] = data;
				break;
			case 0x03:
				chip->timer_reload[1] = data;
				chip->timer_counter[1] = data;
				break;
			case 0x04:
				for (i = 0; i < 3; i++)
				{
					chip->channels[i].emu_mode_4op_enable = (data >> i) & 0x01;
					chip->channels[i + 9].emu_mode_4op_enable = (data >> (i + 3)) & 0x01;
				}
				for (i = 0; i < 6; i++)
				{
					ESFM_emu_rearrange_connections(&chip->channels[i]);
					ESFM_emu_rearrange_connections(&chip->channels[i + 9]);
				}
				break;
			case 0x05:
				chip->emu_newmode = data & 0x01;
				if ((data & 0x80) != 0)
				{
					chip->native_mode = 1;
					ESFM_emu_to_native_switch(chip);
				}
				break;
			case 0x08:
				chip->keyscale_mode = (data & 0x40) != 0;
				break;
			}
		}
		else
		{
			switch(reg & 0x0f)
			{
			case 0x01:
				chip->emu_wavesel_enable = (data & 0x20) != 0;
				break;
			case 0x02:
				chip->timer_reload[0] = data;
				chip->timer_counter[0] = data;
				break;
			case 0x03:
				chip->timer_reload[1] = data;
				chip->timer_counter[1] = data;
				break;
			case 0x04:
				if (data & 0x80)
				{
					chip->irq_bit = 0;
					chip->timer_overflow[0] = 0;
					chip->timer_overflow[1] = 0;
					break;
				}
				chip->timer_enable[0] = data & 0x01;
				chip->timer_enable[1] = (data & 0x02) != 0;
				chip->timer_mask[1] = (data & 0x20) != 0;
				chip->timer_mask[0] = (data & 0x40) != 0;
				break;
			case 0x08:
				chip->keyscale_mode = (data & 0x40) != 0;
				break;
			}
		}
		break;
	case 0x20: case 0x30:
		if (emu_slot_idx >= 0)
		{
			ESFM_slot_write(&chip->channels[natv_chan_idx].slots[natv_slot_idx], 0x0, data);
		}
		break;
	case 0x40: case 0x50:
		if (emu_slot_idx >= 0)
		{
			ESFM_slot_write(&chip->channels[natv_chan_idx].slots[natv_slot_idx], 0x1, data);
			ESFM_emu_channel_update_keyscale(&chip->channels[natv_chan_idx]);
		}
		break;
	case 0x60: case 0x70:
		if (emu_slot_idx >= 0)
		{
			ESFM_slot_write(&chip->channels[natv_chan_idx].slots[natv_slot_idx], 0x2, data);
		}
		break;
	case 0x80: case 0x90:
		if (emu_slot_idx >= 0)
		{
			ESFM_slot_write(&chip->channels[natv_chan_idx].slots[natv_slot_idx], 0x3, data);
		}
		break;
	case 0xa0:
		if (emu_chan_idx >= 0)
		{
			ESFM_slot_write(&chip->channels[emu_chan_idx].slots[0], 0x4, data);
			ESFM_emu_channel_update_keyscale(&chip->channels[emu_chan_idx]);
		}
		break;
	case 0xb0:
		if (emu_chan_idx >= 0)
		{
			esfm_channel *channel = &chip->channels[emu_chan_idx];
			// TODO: check if emulation mode actually writes to the native mode key on registers
			// it might only use slot 0's emu key on field...
			channel->key_on = (data & 0x20) != 0;
			if (channel->channel_idx == 7 || channel->channel_idx == 8)
			{
				channel->key_on_2 = (data & 0x20) != 0;
			}
			ESFM_slot_write(&channel->slots[0], 0x5, data);
			ESFM_emu_channel_update_keyscale(&chip->channels[emu_chan_idx]);
		}
		break;
	case 0xc0:
		if (emu_chan_idx >= 0)
		{
			ESFM_slot_write(&chip->channels[emu_chan_idx].slots[0], 0x6, data);
			ESFM_emu_rearrange_connections(&chip->channels[emu_chan_idx]);
		}
		break;
	case 0xe0: case 0xf0:
		if (emu_slot_idx >= 0)
		{
			ESFM_slot_write(&chip->channels[natv_chan_idx].slots[natv_slot_idx], 0x7, data);
		}
		break;
	}
}


/* ------------------------------------------------------------------------- */
void
ESFM_write_reg (esfm_chip *chip, uint16_t address, uint8_t data)
{
	if (chip->native_mode)
	{
		ESFM_write_reg_native(chip, address, data);
		return;
	}
	else
	{
		ESFM_write_reg_emu(chip, address, data);
		return;
	}
}

/* ------------------------------------------------------------------------- */
void
ESFM_write_reg_buffered (esfm_chip *chip, uint16_t address, uint8_t data)
{
	uint64_t timestamp;
	esfm_write_buf *new_entry, *last_entry;

	new_entry = &chip->write_buf[chip->write_buf_end];
	last_entry = &chip->write_buf[(chip->write_buf_end - 1) % ESFM_WRITEBUF_SIZE];

	if (new_entry->valid) {
		ESFM_write_reg(chip, new_entry->address, new_entry->data);
		chip->write_buf_start = (chip->write_buf_end + 1) % ESFM_WRITEBUF_SIZE;
	}

	new_entry->valid = 1;
	new_entry->address = address;
	new_entry->data = data;
	timestamp = last_entry->timestamp + ESFM_WRITEBUF_DELAY;
	if (timestamp < chip->write_buf_timestamp)
	{
		timestamp = chip->write_buf_timestamp;
	}

	new_entry->timestamp = timestamp;
	chip->write_buf_end = (chip->write_buf_end + 1) % ESFM_WRITEBUF_SIZE;
}

/* ------------------------------------------------------------------------- */
void
ESFM_write_reg_buffered_fast (esfm_chip *chip, uint16_t address, uint8_t data)
{
	esfm_write_buf *new_entry;

	new_entry = &chip->write_buf[chip->write_buf_end];

	if (new_entry->valid) {
		ESFM_write_reg(chip, new_entry->address, new_entry->data);
		chip->write_buf_start = (chip->write_buf_end + 1) % ESFM_WRITEBUF_SIZE;
	}

	new_entry->valid = 1;
	new_entry->address = address;
	new_entry->data = data;
	new_entry->timestamp = chip->write_buf_timestamp;
	chip->write_buf_end = (chip->write_buf_end + 1) % ESFM_WRITEBUF_SIZE;
}

/* ------------------------------------------------------------------------- */
uint8_t
ESFM_readback_reg (esfm_chip *chip, uint16_t address)
{
	if (chip->native_mode)
	{
		return ESFM_readback_reg_native(chip, address);
	}
	else
	{
		return 0;
	}
}

/* ------------------------------------------------------------------------- */
void
ESFM_write_port (esfm_chip *chip, uint8_t offset, uint8_t data)
{
	if (chip->native_mode)
	{
		switch(offset)
		{
		case 0:
			chip->native_mode = 0;
			ESFM_native_to_emu_switch(chip);
			// TODO: verify if the address write goes through
			chip->addr_latch = data;
			break;
		case 1:
			ESFM_write_reg_native(chip, chip->addr_latch, data);
			break;
		case 2:
			chip->addr_latch = (chip->addr_latch & 0xff00) | data;
			break;
		case 3:
			chip->addr_latch = chip->addr_latch & 0xff;
			chip->addr_latch |= (uint16)data << 8;
			break;
		}
	}
	else
	{
		switch(offset)
		{
		case 0:
			chip->addr_latch = data;
			break;
		case 1: case 3:
			ESFM_write_reg_emu(chip, chip->addr_latch, data);
			break;
		case 2:
			chip->addr_latch = (uint16)data | 0x100;
			break;
		}
	}
}

/* ------------------------------------------------------------------------- */
uint8_t
ESFM_read_port (esfm_chip *chip, uint8_t offset)
{
	uint8_t data = 0;

	switch(offset)
	{
	case 0:
		data |= (chip->irq_bit != 0) << 7;
		data |= (chip->timer_overflow[0] != 0) << 6;
		data |= (chip->timer_overflow[1] != 0) << 5;
		break;
	case 1:
		if (chip->native_mode)
		{
			data = ESFM_readback_reg_native(chip, chip->addr_latch);
		}
		else
		{
			data = 0;
		}
		break;
	case 2: case 3:
		// This matches OPL3 behavior.
		data = 0xff;
		break;
	}

	return data;
}

/* ------------------------------------------------------------------------- */
void
ESFM_set_mode (esfm_chip *chip, bool native_mode)
{
	native_mode = native_mode != 0;

	if (native_mode != (chip->native_mode != 0))
	{
		chip->native_mode = native_mode;
		if (native_mode)
		{
			ESFM_emu_to_native_switch(chip);
		}
		else
		{
			ESFM_native_to_emu_switch(chip);
		}
	}
}

/* ------------------------------------------------------------------------- */
void
ESFM_init (esfm_chip *chip)
{
	esfm_slot *slot;
	esfm_channel *channel;
	size_t channel_idx, slot_idx;

	memset(chip, 0, sizeof(esfm_chip));
	for (channel_idx = 0; channel_idx < 18; channel_idx++)
	{
		for (slot_idx = 0; slot_idx < 4; slot_idx++)
		{
			channel = &chip->channels[channel_idx];
			slot = &channel->slots[slot_idx];

			channel->chip = chip;
			channel->channel_idx = channel_idx;
			slot->channel = channel;
			slot->chip = chip;
			slot->slot_idx = slot_idx;
			slot->in.eg_position = slot->in.eg_output = 0x1ff;
			slot->in.eg_state = EG_RELEASE;
			slot->in.emu_mod_enable = ~((int13) 0);
			if (slot_idx == 0)
			{
				slot->in.mod_input = &slot->in.feedback_buf;
			}
			else
			{
				esfm_slot *prev_slot = &channel->slots[slot_idx - 1];
				slot->in.mod_input = &prev_slot->in.output;
			}

			if (slot_idx == 1)
			{
				slot->in.emu_output_enable = ~((int13) 0);
			}

			if (channel_idx > 15 && slot_idx & 0x02)
			{
				slot->in.key_on = &channel->key_on_2;
			}
			else
			{
				slot->in.key_on = &channel->key_on;
			}

			slot->out_enable[0] = slot->out_enable[1] = ~((int13) 0);
		}
	}

	chip->lfsr = 1;
}

