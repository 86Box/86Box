/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the NukedOPL3 driver.
 *
 * Version: @(#)snd_opl_nuked.h 1.0.5 2020/07/16
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2016-2019 Miran Grca.
 */
#ifndef SOUND_OPL_NUKED_H
#define SOUND_OPL_NUKED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#ifndef OPL_ENABLE_STEREOEXT
#define OPL_ENABLE_STEREOEXT 0
#endif

#define OPL_WRITEBUF_SIZE  1024
#define OPL_WRITEBUF_DELAY 2

typedef struct _opl3_slot    opl3_slot;
typedef struct _opl3_channel opl3_channel;
typedef struct _opl3_chip    opl3_chip;

struct _opl3_slot {
    opl3_channel *channel;
    opl3_chip    *chip;
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

struct _opl3_channel {
    opl3_slot    *slotz[2]; // Don't use "slots" keyword to avoid conflict with Qt applications
    opl3_channel *pair;
    opl3_chip    *chip;
    int16_t      *out[4];

#if OPL_ENABLE_STEREOEXT
    int32_t       leftpan;
    int32_t       rightpan;
#endif

    uint8_t       chtype;
    uint16_t      f_num;
    uint8_t       block;
    uint8_t       fb;
    uint8_t       con;
    uint8_t       alg;
    uint8_t       ksv;
    uint16_t      cha;
    uint16_t      chb;
    uint16_t      chc;
    uint16_t      chd;
    uint8_t       ch_num;
};

typedef struct _opl3_writebuf {
    uint64_t time;
    uint16_t reg;
    uint8_t  data;
} opl3_writebuf;

struct _opl3_chip {
    opl3_channel channel[18];
    opl3_slot slot[36];
    uint16_t timer;
    uint64_t eg_timer;
    uint8_t  eg_timerrem;
    uint8_t  eg_state;
    uint8_t  eg_add;
    uint8_t  eg_timer_lo;
    uint8_t  newm;
    uint8_t  nts;
    uint8_t  rhy;
    uint8_t  vibpos;
    uint8_t  vibshift;
    uint8_t  tremolo;
    uint8_t  tremolopos;
    uint8_t  tremoloshift;
    uint32_t noise;
    int16_t  zeromod;
    int32_t  mixbuff[4];
    uint8_t  rm_hh_bit2;
    uint8_t  rm_hh_bit3;
    uint8_t  rm_hh_bit7;
    uint8_t  rm_hh_bit8;
    uint8_t  rm_tc_bit3;
    uint8_t  rm_tc_bit5;

#if OPL_ENABLE_STEREOEXT
    uint8_t  stereoext;
#endif

    // OPL3L
    int32_t  rateratio;
    int32_t  samplecnt;
    int32_t  oldsamples[4];
    int32_t  samples[4];

    uint64_t writebuf_samplecnt;
    uint32_t writebuf_cur;
    uint32_t writebuf_last;
    uint64_t writebuf_lasttime;
    opl3_writebuf writebuf[OPL_WRITEBUF_SIZE];
};

typedef struct {
    opl3_chip opl;
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
} nuked_drv_t;

enum {
    FLAG_CYCLES = 0x02,
    FLAG_OPL3   = 0x01
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

void OPL3_Generate(opl3_chip *chip, int32_t *buf);
void OPL3_GenerateResampled(opl3_chip *chip, int32_t *buf);
void OPL3_Reset(opl3_chip *chip, uint32_t samplerate);
void OPL3_WriteReg(void *priv, uint16_t reg, uint8_t val);
void OPL3_WriteRegBuffered(void *priv, uint16_t reg, uint8_t val);
void OPL3_GenerateStream(opl3_chip *chip, int32_t *sndptr, uint32_t numsamples);

static void OPL3_Generate4Ch(void *priv, int32_t *buf4);
void OPL3_Generate4Ch_Resampled(opl3_chip *chip, int32_t *buf4);
void OPL3_Generate4Ch_Stream(opl3_chip *chip, int32_t *sndptr1, int32_t *sndptr2, uint32_t numsamples);

#ifdef __cplusplus
}
#endif

#endif /*SOUND_OPL_NUKED_H*/
