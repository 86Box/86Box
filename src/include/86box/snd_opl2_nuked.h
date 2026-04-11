/* Nuked OPL2 Lite
 * Copyright (C) 2026 nukeykt
 *
 * This file is part of Nuked OPL2 Lite.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  Nuked OPL2 Lite
 *  Thanks:
 *      MAME Development Team(Jarek Burczynski, Tatsuyuki Satoh):
 *          Feedback and Rhythm part calculation information.
 *      forums.submarine.org.uk(carbon14, opl3):
 *          Tremolo and phase generator calculation information.
 *      OPLx decapsulated(Matthew Gambrell, Olli Niemitalo):
 *          OPL2 ROMs.
 *      siliconpr0n.org(John McMaster, digshadow):
 *          YMF262 and VRC VII decaps and die shots.
 *      Travis Goodspeed:
 *          YM3812 decap and die shot
 *
 * version: 0.9 beta
 */

#ifndef SOUND_OPL2_NUKED_H
#define SOUND_OPL2_NUKED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define OPL2_WRITEBUF_SIZE   1024
#define OPL2_WRITEBUF_DELAY  2

typedef struct _opl2_slot opl2_slot;
typedef struct _opl2_channel opl2_channel;
typedef struct _opl2_chip opl2_chip;

struct _opl2_slot {
    opl2_channel *channel;
    opl2_chip    *chip;
    int16_t       out;
    int16_t       fbmod;
    int16_t      *mod;
    int16_t       prout;
    uint16_t      eg_rout;
    uint16_t      eg_out;
    uint8_t       eg_inc;
    uint8_t       eg_gen;
    uint8_t       eg_rate;
    uint8_t       eg_ksl;
    uint8_t       eg_mute;
    uint8_t      *trem;
    uint8_t       reg_vib;
    uint8_t       reg_type;
    uint8_t       reg_ksr;
    uint8_t       reg_mult;
    uint8_t       reg_ksl;
    uint8_t       reg_tl;
    uint8_t       reg_ar;
    uint8_t       reg_dr;
    uint8_t       reg_sl;
    uint8_t       reg_rr;
    uint8_t       reg_wf;
    uint8_t       key;
    uint32_t      pg_reset;
    uint32_t      pg_phase;
    uint16_t      pg_phase_out;
    uint8_t       slot_num;
};

struct _opl2_channel {
    opl2_slot *slotz[2]; // Don't use "slots" keyword to avoid conflict with Qt applications
    opl2_chip *chip;

    uint8_t  chtype;
    uint16_t f_num;
    uint8_t  block;
    uint8_t  fb;
    uint8_t  con;
    uint8_t  ksv;
    uint8_t  ch_num;
};

typedef struct _opl2_writebuf {
    uint64_t time;
    uint16_t reg;
    uint8_t  data;
} opl2_writebuf;

struct _opl2_chip {
    opl2_channel channel[9];
    opl2_slot    slot[18];
    uint16_t     timer;
    uint32_t     eg_timer;
    uint8_t      eg_timerrem;
    uint8_t      eg_state;
    uint8_t      eg_add;
    uint8_t      eg_timer_lo;
    uint8_t      nts;
    uint8_t      rhy;
    uint8_t      wfe;
    uint8_t      vibpos;
    uint8_t      vibshift;
    uint8_t      tremolo;
    uint8_t      tremolopos;
    uint8_t      tremoloshift;
    uint32_t     noise;
    int16_t      zeromod;
    int32_t      mixbuff;
    uint8_t      rm_hh_bit2;
    uint8_t      rm_hh_bit3;
    uint8_t      rm_hh_bit7;
    uint8_t      rm_hh_bit8;
    uint8_t      rm_tc_bit3;
    uint8_t      rm_tc_bit5;
    uint16_t     ch_upd_a0;
    uint16_t     ch_upd_b0;
    uint8_t      ch_upd_a0_value[12];
    uint8_t      ch_upd_b0_value[12];
    uint8_t      t1_start;
    uint8_t      t1_mask;
    uint8_t      t1_reg;
    uint8_t      t1_value;
    uint8_t      t1_status;
    uint8_t      t2_start;
    uint8_t      t2_mask;
    uint8_t      t2_reg;
    uint8_t      t2_value;
    uint8_t      t2_status;
    uint8_t      csm_enable;
    uint8_t      csm_kon;

    int32_t rateratio;
    int32_t samplecnt;
    int16_t oldsample;
    int32_t sample;

    uint64_t      writebuf_samplecnt;
    uint32_t      writebuf_cur;
    uint32_t      writebuf_last;
    uint64_t      writebuf_lasttime;
    opl2_writebuf writebuf[OPL2_WRITEBUF_SIZE];
};

typedef struct {
    opl2_chip opl;
    int8_t    flags;
    int8_t    is_48k;

    uint16_t port;
    uint8_t  status;
    uint8_t  timer_ctrl;
    uint16_t timer_count[2];
    uint16_t timer_cur_count[2];

    pc_timer_t timers[2];

    int     pos;
    int32_t buffer[MUSICBUFLEN * 2];

    int32_t *(*update)(void *priv);
} nuked_opl2_drv_t;

enum {
    FLAG_CYCLES = 0x02,
};

enum {
    STAT_TMR_OVER  = 0x60,
    STAT_TMR1_OVER = 0x40,
    STAT_TMR2_OVER = 0x20,
    STAT_TMR_ANY   = 0x80
};

enum {
    CTRL_RESET      = 0x80,
    CTRL_TMR_MASK   = 0x60,
    CTRL_TMR1_MASK  = 0x40,
    CTRL_TMR2_MASK  = 0x20,
    CTRL_TMR2_START = 0x02,
    CTRL_TMR1_START = 0x01
};

void OPL2_Generate(opl2_chip *chip, int32_t *buf);
void OPL2_GenerateResampled(opl2_chip *chip, int32_t *buf);
void OPL2_Reset(opl2_chip *chip, uint32_t samplerate);
void OPL2_WriteReg(void *priv, uint8_t reg, uint8_t val);
void OPL2_WriteRegBuffered(void *priv, uint8_t reg, uint8_t val);
void OPL2_GenerateStream(opl2_chip *chip, int32_t *sndptr, uint32_t numsamples);
uint8_t OPL2_ReadStatus(opl2_chip *chip);

#ifdef __cplusplus
}
#endif

#endif /*SOUND_OPL2_NUKED_H*/
