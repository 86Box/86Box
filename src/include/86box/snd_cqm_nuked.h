/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the NukedCQM driver.
 *
 *          Nuked CQM is free software: you can redistribute it and/or modify
 *          it under the terms of the GNU Lesser General Public License as
 *          published by the Free Software Foundation, either version 2.1
 *          of the License, or (at your option) any later version.
 *
*          Nuked CQM is free software: you can redistribute it and/or modify
 *          it under the terms of the GNU Lesser General Public License as
 *          published by the Free Software Foundation, either version 2.1
 *          of the License, or (at your option) any later version.
 *
 *          Nuked CQM is distributed in the hope that it will be useful,
 *          but WITHOUT ANY WARRANTY; without even the implied warranty of
 *          MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *          GNU Lesser General Public License for more details.
 *
 *          You should have received a copy of the GNU Lesser General Public License
 *          along with Nuked CQM. If not, see <https://www.gnu.org/licenses/>.
 *
 *          Nuked CQM emulator.
 *          Thanks:
 *              Wohlstand:
 *                  Sending CQM chips
 *              org(ogamespec):
 *                  Deroute tool
 *
 *          version: 0.9 beta
 *
 * Authors: Alexey Khokholov (Nuke.YKT),
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2026 Alexey Khokholov (Nuke.YKT)
 *          Copyright 2026 Miran Grca.
 */
#ifndef SOUND_CQM_NUKED_H
#define SOUND_CQM_NUKED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CQM_WRITEBUF_SIZE   2048
#define CQM_WRITEBUF_DELAY  2

typedef struct
{
	uint64_t time;
	uint16_t reg;
	uint8_t data;
} cqm_writebuf;

typedef struct
{
	int32_t env;
	uint32_t phase;
	int32_t mod[2];
} cqmslot_t;

typedef struct
{
	uint8_t regs[256];
	cqmslot_t slotz[48];
	uint8_t oddeven;
	uint8_t newm;
	uint8_t rhy;
	uint8_t mode;
	uint8_t key[18];
	uint32_t keyl;
	uint32_t okeyl;
	uint16_t counter;
	uint8_t trem_cnt1;
	uint8_t trem_cnt2;
	uint8_t trem_cnt3;
	uint8_t trem_cnt;
	int32_t dooutput;
	int32_t wave_prev;
	int32_t wavesample;
	int32_t waveshift;
	uint8_t wavepan;
	uint8_t is4op2;
	uint32_t noise;
	uint8_t hh_bit1;
	uint8_t hh_bit2;
	uint8_t rhy_bit;

	int32_t rateratio;
	int32_t samplecnt;
	int16_t oldsamples[2];
	int16_t samples[2];

	uint64_t writebuf_samplecnt;
	uint32_t writebuf_cur;
	uint32_t writebuf_last;
	uint64_t writebuf_lasttime;
	cqm_writebuf writebuf[CQM_WRITEBUF_SIZE];
} cqm_t;

typedef struct {
    cqm_t    cqm;
    int8_t   flags;

    uint16_t port;
    uint8_t  status;
    uint8_t  timer_ctrl;
    uint16_t timer_count[2];
    uint16_t timer_cur_count[2];

    pc_timer_t timers[2];

    int     pos;
    int32_t buffer[CQMBUFLEN * 2];

    int32_t *(*update)(void *priv);
} nuked_cqm_drv_t;

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

void CQM_Reset(cqm_t* chip, uint32_t samplerate, uint32_t genrate);
void CQM_WriteReg(cqm_t* chip, uint16_t reg, uint8_t data);
void CQM_WriteRegBuffered(cqm_t* chip, uint16_t reg, uint8_t data);
void CQM_Generate(cqm_t* chip, int16_t* sample);
void CQM_GenerateResampled(cqm_t* chip, int32_t* sample);
void CQM_GenerateStream(cqm_t* chip, int32_t* sndptr, uint32_t numsamples);

#ifdef __cplusplus
}
#endif

#endif /*SOUND_CQM_NUKED_H*/
