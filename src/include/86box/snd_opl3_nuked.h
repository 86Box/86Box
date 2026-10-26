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
#ifndef SOUND_OPL3_NUKED_H
#define SOUND_OPL3_NUKED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#ifndef OPL3_ENABLE_STEREOEXT
#define OPL3_ENABLE_STEREOEXT 0
#endif

/* Quirk: Some FM channels are output one sample later on the left side than
 * the right. Defined here (not only in the .c) so the opl3_channel mix
 * pointer lists guarded by it are laid out consistently. */
#ifndef OPL3_QUIRK_CHANNELSAMPLEDELAY
#define OPL3_QUIRK_CHANNELSAMPLEDELAY (!OPL3_ENABLE_STEREOEXT)
#endif

/* OPL3_WF_TABLE_RUNTIME=1 builds the 16 KB logsin waveform table at the
 * first OPL3_Reset instead of compiling in snd_opl3_nuked_wf_rom.h, trading
 * read-only data for zero-initialized RAM. The table data is identical
 * either way. The first OPL3_Reset in the process must not run concurrently
 * with another reset. */
#ifndef OPL3_WF_TABLE_RUNTIME
#define OPL3_WF_TABLE_RUNTIME 0
#endif

/* Compatibility switches for parity with older upstream Nuked-OPL3 commits.
 * Both default to 0, the behavior of upstream master (cfedb09). Enabled,
 * they reproduce the older behavior exactly.
 *
 * OPL3_COMPAT_OLD_EG=1 selects the envelope stepping from before upstream
 * commits e4afafc and cfedb09 (June/July 2024). OPL3_COMPAT_DEFERRED_4OP_ALG=1
 * selects the pre-f2c9873 (Nov 2022) behavior where writes to the 4-op enable
 * register 0x104 do not update a channel's operator routing until its next
 * C0 write. */
#ifndef OPL3_COMPAT_OLD_EG
#define OPL3_COMPAT_OLD_EG 0
#endif
#ifndef OPL3_COMPAT_DEFERRED_4OP_ALG
#define OPL3_COMPAT_DEFERRED_4OP_ALG 0
#endif

#define OPL3_WRITEBUF_SIZE  1024
#define OPL3_WRITEBUF_DELAY 2

typedef struct _opl3_slot    opl3_slot;
typedef struct _opl3_channel opl3_channel;
typedef struct _opl3_chip    opl3_chip;

struct _opl3_slot {
    opl3_channel *channel;
    opl3_chip    *chip;
    int16_t      *mod;
    uint8_t      *trem;
    uint32_t      pg_reset;
    uint32_t      pg_phase;
    uint32_t      pg_inc;
    /* Equal to chip->write_gen while the slot is provably inert: fully
     * attenuated, key off, all-zero phase/output state, and mod/trem frozen
     * at zeromod. Set by the trivially-dead path in OPL3_ProcessSlotImpl;
     * invalidated by any register write (write_gen bump). 0 = not dormant. */
    uint32_t      dormant_gen;
    int16_t       out;
    int16_t       fbmod;
    int16_t       prout;
    uint16_t      eg_rout;
    uint16_t      eg_out;
    /* Cached (reg_tl << 2) + (eg_ksl >> kslshift[reg_ksl]); maintained by
     * OPL3_EnvelopeUpdateKSL whenever any of those inputs change. Hoists
     * a load + lookup + shift out of the per-sample envelope hot path. */
    uint16_t eg_tl_ksl;
    uint16_t pg_phase_out;
    uint8_t  key;
    uint8_t  eg_gen;
    uint8_t  reg_vib;
    uint8_t  reg_mult;
    uint8_t  reg_wf;
    uint8_t  slot_num;
    uint8_t  eg_ksl;
    uint8_t  eg_ks;
    uint8_t  reg_type;
    uint8_t  reg_ksr;
    uint8_t  reg_ksl;
    uint8_t  reg_tl;
    uint8_t  reg_ar;
    uint8_t  reg_dr;
    uint8_t  reg_sl;
    uint8_t  reg_rr;
    uint8_t  eg_rates[4];
    uint8_t  eg_rate_hi[4];
    uint8_t  eg_rate_lo[4];
    /* Phase increment per vibrato position, maintained by
     * OPL3_PhaseUpdateInc (and rebuilt on vibshift changes); pg_inc_vib[pos]
     * equals the upstream per-sample vibrato f_num adjustment for that pos. */
    uint32_t pg_inc_vib[8];
};

struct _opl3_channel {
    opl3_slot    *slotz[2]; /*Don't use "slots" keyword to avoid conflict with Qt applications*/
    opl3_channel *pair;
    opl3_chip    *chip;
    int16_t      *out[4];
#if OPL3_QUIRK_CHANNELSAMPLEDELAY
    /* Mix-pass pointer lists: identical to out[] except entries pointing at
     * a delayed slot's out are redirected to its prout, which holds the
     * previous sample's out once all 36 slots are processed. out_left delays
     * slots 15-35 and out_right delays 33-35, reproducing the
     * CHANNELSAMPLEDELAY snapshots without staging slot processing around
     * the mixes. */
    int16_t      *out_left[4];
    int16_t      *out_right[4];
#endif
    uint8_t       out_cnt;

#if OPL_ENABLE_STEREOEXT
    int32_t leftpan;
    int32_t rightpan;
#endif

    uint8_t  chtype;
    uint16_t f_num;
    uint8_t  block;
    uint8_t  fb;
    uint8_t  con;
    uint8_t  alg;
    uint8_t  ksv;
    uint16_t cha, chb;
    uint16_t chc, chd;
    uint8_t  ch_num;
};

typedef struct _opl3_writebuf {
    uint64_t time;
    uint16_t reg;
    uint8_t  data;
} opl3_writebuf;

struct _opl3_chip {
    opl3_channel channel[18];
    opl3_slot    slot[36];
    uint16_t     timer;
    uint64_t     eg_timer;
    uint8_t      eg_timerrem;
    uint8_t      eg_state;
    uint8_t      eg_add;
    uint8_t      eg_timer_lo;
    uint8_t      newm;
    uint8_t      nts;
    uint8_t      rhy;
    uint8_t      vibpos;
    uint8_t      vibshift;
    uint8_t      tremolo;
    uint8_t      tremolopos;
    uint8_t      tremoloshift;
    uint8_t      tremolo_dirty;
    /* Bumped on every OPL3_WriteReg call; never 0 after reset. A slot whose
     * dormant_gen matches is skipped without re-checking its dead-state
     * conditions. Wrap is handled by clearing all dormant_gen tags. */
    uint32_t     write_gen;
    uint32_t     noise;
    /* Bit 0 of the noise LFSR state as seen by the hh (slot 13) and sd
     * (slot 16) rhythm operators, precomputed per sample. */
    uint32_t     noise_hh;
    uint32_t     noise_sd;
    /* Channels eligible for each mix pass: out_cnt > 0 and routed to at
     * least one output on that side. Eligibility only changes on register
     * writes; mix_dirty triggers a rebuild at the top of the next sample. */
    opl3_channel *mix_left[18];
    opl3_channel *mix_right[18];
    uint8_t      nmix_left;
    uint8_t      nmix_right;
    uint8_t      mix_dirty;
    int16_t      zeromod;
    int32_t      mixbuff[4];
    uint8_t      rm_hh_bit2;
    uint8_t      rm_hh_bit3;
    uint8_t      rm_hh_bit7;
    uint8_t      rm_hh_bit8;
    uint8_t      rm_tc_bit3;
    uint8_t      rm_tc_bit5;

#if OPL3_ENABLE_STEREOEXT
    uint8_t stereoext;
#endif

    /* OPL3L */
    int32_t rateratio;
    int32_t samplecnt;
    int32_t oldsamples[4];
    int32_t samples[4];

    uint64_t      writebuf_samplecnt;
    uint32_t      writebuf_cur;
    uint32_t      writebuf_last;
    uint64_t      writebuf_lasttime;
    opl3_writebuf writebuf[OPL3_WRITEBUF_SIZE];
};

typedef struct {
    opl3_chip opl;
    int8_t    flags;
    int8_t    is_48k;
    int8_t    is_cs;

    uint16_t port;
    uint8_t  status;
    uint8_t  timer_ctrl;
    uint16_t timer_count[2];
    uint16_t timer_cur_count[2];

    pc_timer_t timers[2];

    int     pos;
    int32_t buffer[MUSICBUFLEN * 2];

    int32_t *(*update)(void *priv);
} nuked_opl3_drv_t;

enum {
    FLAG_CRYSTAL = 0x04,
    FLAG_CYCLES  = 0x02,
    FLAG_OPL3    = 0x01
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

#endif /*SOUND_OPL3_NUKED_H*/
