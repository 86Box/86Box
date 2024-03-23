/*
 * ESFMu: emulator for the ESS "ESFM" enhanced OPL3 clone
 * Copyright (C) 2023 Kagamiin~
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
 * - akumanatt
 *       For helping out with code optimization.
 * - And everybody who helped out with real hardware testing
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _esfm_slot esfm_slot;
typedef struct _esfm_slot_internal esfm_slot_internal;
typedef struct _esfm_channel esfm_channel;
typedef struct _esfm_chip esfm_chip;


void ESFM_init (esfm_chip *chip);
void ESFM_write_reg (esfm_chip *chip, uint16_t address, uint8_t data);
void ESFM_write_reg_buffered (esfm_chip *chip, uint16_t address, uint8_t data);
void ESFM_write_reg_buffered_fast (esfm_chip *chip, uint16_t address, uint8_t data);
void ESFM_write_port (esfm_chip *chip, uint8_t offset, uint8_t data);
uint8_t ESFM_readback_reg (esfm_chip *chip, uint16_t address);
uint8_t ESFM_read_port (esfm_chip *chip, uint8_t offset);
void ESFM_generate(esfm_chip *chip, int32_t *buf);
void ESFM_generate_stream(esfm_chip *chip, int16_t *sndptr, uint32_t num_samples);
int16_t ESFM_get_channel_output_native(esfm_chip *chip, int channel_idx);


// These are fake types just for syntax sugar.
// Beware of their underlying types when reading/writing to them.
typedef uint8_t ebit;
typedef uint8_t uint2;
typedef uint8_t uint3;
typedef uint8_t uint4;
typedef uint8_t uint5;
typedef uint8_t uint6;
typedef uint8_t uint8;
typedef uint16_t uint9;
typedef uint16_t uint10;
typedef uint16_t uint11;
typedef uint16_t uint12;
typedef uint16_t uint16;
typedef uint32_t uint19;
typedef uint32_t uint23;
typedef uint32_t uint32;
typedef uint64_t uint36;

typedef int16_t int13;
typedef int16_t int14;
typedef int16_t int16;
typedef int32_t int32;

enum eg_states
{
	EG_ATTACK,
	EG_DECAY,
	EG_SUSTAIN,
	EG_RELEASE
};


typedef struct _esfm_write_buf
{
	uint64_t timestamp;
	uint16_t address;
	uint8_t data;
	ebit valid;

} esfm_write_buf;

typedef struct _emu_slot_channel_mapping
{
	int channel_idx;
	int slot_idx;

} emu_slot_channel_mapping;

typedef struct _esfm_slot_internal
{
	uint9 eg_position;
	uint9 eg_ksl_offset;
	uint10 eg_output;

	uint4 keyscale;

	int13 output;
	int13 emu_output_enable;
	int13 emu_mod_enable;
	int13 feedback_buf;
	int13 *mod_input;

	uint19 phase_acc;
	uint10 phase_out;
	ebit phase_reset;
	ebit *key_on;
	ebit key_on_gate;

	uint2 eg_state;
	ebit eg_delay_run;
	ebit eg_delay_transitioned_10;
	ebit eg_delay_transitioned_10_gate;
	ebit eg_delay_transitioned_01;
	ebit eg_delay_transitioned_01_gate;
	uint16 eg_delay_counter;
	uint16 eg_delay_counter_compare;

} esfm_slot_internal;

struct _esfm_slot
{
	// Metadata
	esfm_channel *channel;
	esfm_chip *chip;
	uint2 slot_idx;

	// Register data
	int13 out_enable[2];
	uint10 f_num;
	uint3 block;
	uint3 output_level;
	// a.k.a. feedback level in emu mode
	uint3 mod_in_level;

	uint6 t_level;
	uint4 mult;
	uint3 waveform;
	// Only for 4th slot
	uint2 rhy_noise;

	uint4 attack_rate;
	uint4 decay_rate;
	uint4 sustain_lvl;
	uint4 release_rate;

	ebit tremolo_en;
	ebit tremolo_deep;
	ebit vibrato_en;
	ebit vibrato_deep;
	ebit emu_connection_typ;
	ebit env_sustaining;
	ebit ksr;
	uint2 ksl;
	uint3 env_delay;
	// overlaps with env_delay bit 0
	// TODO: check if emu mode only uses this, or if it actually overwrites the channel field used by native mode
	ebit emu_key_on;

	// Internal state
	esfm_slot_internal in;
};

struct _esfm_channel
{
	esfm_chip *chip;
	esfm_slot slots[4];
	uint5 channel_idx;
	int16 output[2];
	ebit key_on;
	ebit emu_mode_4op_enable;
	// Only for 17th and 18th channels
	ebit key_on_2;
	ebit emu_mode_4op_enable_2;
};

#define ESFM_WRITEBUF_SIZE 1024
#define ESFM_WRITEBUF_DELAY 2

struct _esfm_chip
{
	esfm_channel channels[18];
	int32 output_accm[2];
	uint16 addr_latch;

	ebit emu_wavesel_enable;
	ebit emu_newmode;
	ebit native_mode;

	ebit keyscale_mode;

	// Global state
	uint36 eg_timer;
	uint10 global_timer;
	uint8 eg_clocks;
	ebit eg_tick;
	ebit eg_timer_overflow;
	uint8 tremolo;
	uint8 tremolo_pos;
	uint8 vibrato_pos;
	uint23 lfsr;

	ebit rm_hh_bit2;
	ebit rm_hh_bit3;
	ebit rm_hh_bit7;
	ebit rm_hh_bit8;
	ebit rm_tc_bit3;
	ebit rm_tc_bit5;

	// 0xbd register in emulation mode, exposed in 0x4bd in native mode
	// ("bass drum" register)
	uint8 emu_rhy_mode_flags;

	ebit emu_vibrato_deep;
	ebit emu_tremolo_deep;

	double timer_accumulator[2];
	uint8 timer_reload[2];
	uint8 timer_counter[2];
	ebit timer_enable[2];
	ebit timer_mask[2];
	ebit timer_overflow[2];
	ebit irq_bit;

	// -- Test bits (NOT IMPLEMENTED) --
	// Halts the envelope generators from advancing. Written on bit 0, read back from bit 5.
	ebit test_bit_w0_r5_eg_halt;
	/*
	 * Activates some sort of waveform test mode that amplifies the output volume greatly
	 * and continuously shifts the waveform table downwards, possibly also outputting the
	 * waveform's derivative? (it's so weird!)
	 */
	ebit test_bit_1_distort;
	// Seems to do nothing.
	ebit test_bit_2;
	// Seems to do nothing.
	ebit test_bit_3;
	// Appears to attenuate the output by about 3 dB.
	ebit test_bit_4_attenuate;
	// Written on bit 5, read back from bit 0. Seems to do nothing.
	ebit test_bit_w5_r0;
	// Resets all phase generators and holds them in the reset state while this bit is set.
	ebit test_bit_6_phase_stop_reset;
	// Seems to do nothing.
	ebit test_bit_7;

	esfm_write_buf write_buf[ESFM_WRITEBUF_SIZE];
	size_t write_buf_start;
	size_t write_buf_end;
	uint64_t write_buf_timestamp;
};

#ifdef __cplusplus
}
#endif
