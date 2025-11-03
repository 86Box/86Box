/*
 * RoboPlay for MSX
 * Copyright (C) 2020 by RoboSoft Inc.
 *
 * opl4_defines.h
 *
 */
#ifndef __OPL4_DEFINES_H
#define __OPL4_DEFINES_H

/*
 * Register numbers
 */

#define OPL4_REG_TEST0                0x00
#define OPL4_REG_TEST1                0x01

#define OPL4_REG_MEMORY_CONFIGURATION 0x02
#define OPL4_MODE_BIT                 0x01
#define OPL4_MTYPE_BIT                0x02
#define OPL4_TONE_HEADER_MASK         0x1C
#define OPL4_DEVICE_ID_MASK           0xE0

#define OPL4_REG_MEMORY_ADDRESS_HIGH  0x03
#define OPL4_REG_MEMORY_ADDRESS_MID   0x04
#define OPL4_REG_MEMORY_ADDRESS_LOW   0x05
#define OPL4_REG_MEMORY_DATA          0x06

/*
 * Offsets to the register banks for voices. To get the
 * register number just add the voice number to the bank offset.
 *
 * Wave Table Number low bits (0x08 to 0x1F)
 */
#define OPL4_REG_TONE_NUMBER 0x08

/* Wave Table Number high bit, F-Number low bits (0x20 to 0x37) */
#define OPL4_REG_F_NUMBER      0x20
#define OPL4_TONE_NUMBER_BIT8  0x01
#define OPL4_F_NUMBER_LOW_MASK 0xFE

/* F-Number high bits, Octave, Pseudo-Reverb (0x38 to 0x4F) */
#define OPL4_REG_OCTAVE         0x38
#define OPL4_F_NUMBER_HIGH_MASK 0x07
#define OPL4_BLOCK_MASK         0xF0
#define OPL4_PSEUDO_REVERB_BIT  0x08

/* Total Level, Level Direct (0x50 to 0x67) */
#define OPL4_REG_LEVEL        0x50
#define OPL4_TOTAL_LEVEL_MASK 0xFE
#define OPL4_LEVEL_DIRECT_BIT 0x01

/* Key On, Damp, LFO RST, CH, Panpot (0x68 to 0x7F) */
#define OPL4_REG_MISC           0x68
#define OPL4_KEY_ON_BIT         0x80
#define OPL4_DAMP_BIT           0x40
#define OPL4_LFO_RESET_BIT      0x20
#define OPL4_OUTPUT_CHANNEL_BIT 0x10
#define OPL4_PAN_POT_MASK       0x0F

/* LFO, VIB (0x80 to 0x97) */
#define OPL4_REG_LFO_VIBRATO    0x80
#define OPL4_LFO_FREQUENCY_MASK 0x38
#define OPL4_VIBRATO_DEPTH_MASK 0x07
#define OPL4_CHORUS_SEND_MASK   0xC0

/* Attack / Decay 1 rate (0x98 to 0xAF) */
#define OPL4_REG_ATTACK_DECAY1  0x98
#define OPL4_ATTACK_RATE_MASK   0xF0
#define OPL4_DECAY1_RATE_MASK   0x0F

/* Decay level / 2 rate (0xB0 to 0xC7) */
#define OPL4_REG_LEVEL_DECAY2   0xB0
#define OPL4_DECAY_LEVEL_MASK   0xF0
#define OPL4_DECAY2_RATE_MASK   0x0F

/* Release rate / Rate correction (0xC8 to 0xDF) */
#define OPL4_REG_RELEASE_CORRECTION  0xC8
#define OPL4_RELEASE_RATE_MASK       0x0F
#define OPL4_RATE_INTERPOLATION_MASK 0xF0

/* AM (0xE0 to 0xF7) */
#define OPL4_REG_TREMOLO        0xE0
#define OPL4_TREMOLO_DEPTH_MASK 0x07
#define OPL4_REVERB_SEND_MASK   0xE0

/* Mixer */
#define OPL4_REG_MIX_CONTROL_FM  0xF8
#define OPL4_REG_MIX_CONTROL_PCM 0xF9
#define OPL4_MIX_LEFT_MASK       0x07
#define OPL4_MIX_RIGHT_MASK      0x38

#define OPL4_REG_ATC             0xFA
#define OPL4_ATC_BIT             0x01

/* Bits in the OPL4 Status register */
#define OPL4_STATUS_BUSY 0x01
#define OPL4_STATUS_LOAD 0x02

#endif /* __OPL4_DEFINES_H */
