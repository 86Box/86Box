/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the IBM PCjr.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/cassette.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/serial.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/snd_sn76489.h>
#include <86box/video.h>
#include <86box/vid_cga_comp.h>
#include <86box/machine.h>


#define PCJR_RGB	0
#define PCJR_COMPOSITE	1

#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01


typedef struct {
    /* Video Controller stuff. */
    mem_mapping_t mapping;
    uint8_t	crtc[32];
    int		crtcreg;
    int		array_index;
    uint8_t	array[32];
    int		array_ff;
    int		memctrl;
    uint8_t	stat;
    int		addr_mode;
    uint8_t	*vram,
		*b8000;
    int		linepos, displine;
    int		sc, vc;
    int		dispon;
    int		con, coff, cursoron, blink;
    int		vsynctime;
    int		vadj;
    uint16_t	ma, maback;
    uint64_t	dispontime, dispofftime;
    pc_timer_t	timer;
    int		firstline, lastline;
    int		composite;

    /* Keyboard Controller stuff. */
    int		latched;
    int		data;
    int		serial_data[44];
    int		serial_pos;
    uint8_t	pa;
    uint8_t	pb;
    pc_timer_t	send_delay_timer;
} pcjr_t;

static video_timings_t timing_dram     = {VIDEO_BUS, 0,0,0, 0,0,0}; /*No additional waitstates*/


static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t	key_queue[16];
static int	key_queue_start = 0,
		key_queue_end = 0;


static void
recalc_address(pcjr_t *pcjr)
{
    if ((pcjr->memctrl & 0xc0) == 0xc0) {
	pcjr->vram  = &ram[(pcjr->memctrl & 0x06) << 14];
	pcjr->b8000 = &ram[(pcjr->memctrl & 0x30) << 11];
    } else {
	pcjr->vram  = &ram[(pcjr->memctrl & 0x07) << 14];
	pcjr->b8000 = &ram[(pcjr->memctrl & 0x38) << 11];
    }
}


static void
recalc_timings(pcjr_t *pcjr)
{
    double _dispontime, _dispofftime, disptime;

    if (pcjr->array[0] & 1) {
	disptime = pcjr->crtc[0] + 1;
	_dispontime = pcjr->crtc[1];
    } else {
	disptime = (pcjr->crtc[0] + 1) << 1;
	_dispontime = pcjr->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime  *= CGACONST;
    _dispofftime *= CGACONST;
    pcjr->dispontime  = (uint64_t)(_dispontime);
    pcjr->dispofftime = (uint64_t)(_dispofftime);
}


static void
vid_out(uint16_t addr, uint8_t val, void *p)
{
    pcjr_t *pcjr = (pcjr_t *)p;
    uint8_t old;

    switch (addr) {
	case 0x3d4:
		pcjr->crtcreg = val & 0x1f;
		return;

	case 0x3d5:
		old = pcjr->crtc[pcjr->crtcreg];
		pcjr->crtc[pcjr->crtcreg] = val & crtcmask[pcjr->crtcreg];
		if (old != val) {
			if (pcjr->crtcreg < 0xe || pcjr->crtcreg > 0x10) {
				fullchange = changeframecount;
				recalc_timings(pcjr);
			}
		}
		return;

	case 0x3da:
		if (!pcjr->array_ff)
			pcjr->array_index = val & 0x1f;
		else {
			if (pcjr->array_index & 0x10)
				val &= 0x0f;
			pcjr->array[pcjr->array_index & 0x1f] = val;
			if (!(pcjr->array_index & 0x1f))
				update_cga16_color(val);
		}
		pcjr->array_ff = !pcjr->array_ff;
		break;

	case 0x3df:
		pcjr->memctrl = val;
		pcjr->addr_mode = val >> 6;
		recalc_address(pcjr);
		break;
    }
}


static uint8_t
vid_in(uint16_t addr, void *p)
{
    pcjr_t *pcjr = (pcjr_t *)p;
    uint8_t ret = 0xff;

    switch (addr) {
	case 0x3d4:
		ret = pcjr->crtcreg;
		break;

	case 0x3d5:
		ret = pcjr->crtc[pcjr->crtcreg];
		break;

	case 0x3da:
		pcjr->array_ff = 0;
		pcjr->stat ^= 0x10;
		ret = pcjr->stat;
		break;
    }

    return(ret);
}


static void
vid_write(uint32_t addr, uint8_t val, void *p)
{
    pcjr_t *pcjr = (pcjr_t *)p;

    if (pcjr->memctrl == -1) return;

    pcjr->b8000[addr & 0x3fff] = val;
}


static uint8_t
vid_read(uint32_t addr, void *p)
{
    pcjr_t *pcjr = (pcjr_t *)p;

    if (pcjr->memctrl == -1) return(0xff);
		
    return(pcjr->b8000[addr & 0x3fff]);
}


static void
vid_poll(void *p)
{
    pcjr_t *pcjr = (pcjr_t *)p;
    uint16_t ca = (pcjr->crtc[15] | (pcjr->crtc[14] << 8)) & 0x3fff;
    int drawcursor;
    int x, c, xs_temp, ys_temp;
    int oldvc;
    uint8_t chr, attr;
    uint16_t dat;
    int cols[4];
    int oldsc;

    if (! pcjr->linepos) {
	timer_advance_u64(&pcjr->timer, pcjr->dispofftime);
	pcjr->stat &= ~1;
	pcjr->linepos = 1;
	oldsc = pcjr->sc;
	if ((pcjr->crtc[8] & 3) == 3) 
		pcjr->sc = (pcjr->sc << 1) & 7;
	if (pcjr->dispon) {
		uint16_t offset = 0;
		uint16_t mask = 0x1fff;

		if (pcjr->displine < pcjr->firstline) {
			pcjr->firstline = pcjr->displine;
			video_wait_for_buffer();
		}
		pcjr->lastline = pcjr->displine;
		cols[0] = (pcjr->array[2] & 0xf) + 16;
		for (c = 0; c < 8; c++) {
			((uint32_t *)buffer32->line[pcjr->displine])[c] = cols[0];
			if (pcjr->array[0] & 1) {
				buffer32->line[(pcjr->displine << 1)][c + (pcjr->crtc[1] << 3) + 8] =
				buffer32->line[(pcjr->displine << 1) + 1][c + (pcjr->crtc[1] << 3) + 8] = cols[0];
			} else {
				buffer32->line[(pcjr->displine << 1)][c + (pcjr->crtc[1] << 4) + 8] =
				buffer32->line[(pcjr->displine << 1) + 1][c + (pcjr->crtc[1] << 4) + 8] = cols[0];
			}
		}

		switch (pcjr->addr_mode) {
			case 0: /*Alpha*/
				offset = 0;
				mask = 0x3fff;
				break;
			case 1: /*Low resolution graphics*/
				offset = (pcjr->sc & 1) * 0x2000;
				break;
			case 3: /*High resolution graphics*/
				offset = (pcjr->sc & 3) * 0x2000;
				break;
		}
		switch ((pcjr->array[0] & 0x13) | ((pcjr->array[3] & 0x08) << 5)) {
			case 0x13: /*320x200x16*/
				for (x = 0; x < pcjr->crtc[1]; x++) {
					dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) | 
					       pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
					pcjr->ma++;
					buffer32->line[(pcjr->displine << 1)][(x << 3) + 8]  = buffer32->line[(pcjr->displine << 1)][(x << 3) + 9]  =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 8]  = buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 9]  =
						pcjr->array[((dat >> 12) & pcjr->array[1]) + 16] + 16;
					buffer32->line[(pcjr->displine << 1)][(x << 3) + 10] = buffer32->line[(pcjr->displine << 1)][(x << 3) + 11] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 10] = buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 11] =
						pcjr->array[((dat >>  8) & pcjr->array[1]) + 16] + 16;
					buffer32->line[(pcjr->displine << 1)][(x << 3) + 12] = buffer32->line[(pcjr->displine << 1)][(x << 3) + 13] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 12] = buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 13] =
						pcjr->array[((dat >>  4) & pcjr->array[1]) + 16] + 16;
					buffer32->line[(pcjr->displine << 1)][(x << 3) + 14] = buffer32->line[(pcjr->displine << 1)][(x << 3) + 15] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 14] = buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 15] =
						pcjr->array[(dat	 & pcjr->array[1]) + 16] + 16;
				}
				break;
			case 0x12: /*160x200x16*/
				for (x = 0; x < pcjr->crtc[1]; x++) {
					dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) | 
					       pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
					pcjr->ma++;
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 8]  = buffer32->line[(pcjr->displine << 1)][(x << 4) + 9]  =
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 10] = buffer32->line[(pcjr->displine << 1)][(x << 4) + 11] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 8]  = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 9]  =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 10] = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 11] =
						pcjr->array[((dat >> 12) & pcjr->array[1]) + 16] + 16;
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 12] = buffer32->line[(pcjr->displine << 1)][(x << 4) + 13] =
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 14] = buffer32->line[(pcjr->displine << 1)][(x << 4) + 15] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 12] = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 13] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 14] = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 15] =
						pcjr->array[((dat >>  8) & pcjr->array[1]) + 16] + 16;
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 16] = buffer32->line[(pcjr->displine << 1)][(x << 4) + 17] =
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 18] = buffer32->line[(pcjr->displine << 1)][(x << 4) + 19] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 16] = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 17] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 18] = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 19] =
						pcjr->array[((dat >>  4) & pcjr->array[1]) + 16] + 16;
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 20] = buffer32->line[(pcjr->displine << 1)][(x << 4) + 21] =
					buffer32->line[(pcjr->displine << 1)][(x << 4) + 22] = buffer32->line[(pcjr->displine << 1)][(x << 4) + 23] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 20] = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 21] =
					buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 22] = buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + 23] =
						pcjr->array[(dat	 & pcjr->array[1]) + 16] + 16;
				}
				break;
			case 0x03: /*640x200x4*/
				for (x = 0; x < pcjr->crtc[1]; x++) {
					dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
					       pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
					pcjr->ma++;
					for (c = 0; c < 8; c++) {
						chr  =  (dat >>  7) & 1;
						chr |= ((dat >> 14) & 2);
						buffer32->line[(pcjr->displine << 1)][(x << 3) + 8 + c] = buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + 8 + c] =
							pcjr->array[(chr & pcjr->array[1]) + 16] + 16;
						dat <<= 1;
					}
				}
				break;
			case 0x01: /*80 column text*/
				for (x = 0; x < pcjr->crtc[1]; x++) {
					chr  = pcjr->vram[((pcjr->ma << 1) & mask) + offset];
					attr = pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
					drawcursor = ((pcjr->ma == ca) && pcjr->con && pcjr->cursoron);
					if (pcjr->array[3] & 4) {
						cols[1] = pcjr->array[ ((attr & 15)      & pcjr->array[1]) + 16] + 16;
						cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1]) + 16] + 16;
						if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor) 
							cols[1] = cols[0];
					} else {
						cols[1] = pcjr->array[((attr & 15) & pcjr->array[1]) + 16] + 16;
						cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1]) + 16] + 16;
					}
					if (pcjr->sc & 8) {
						for (c = 0; c < 8; c++) {
							buffer32->line[(pcjr->displine << 1)][(x << 3) + c + 8] =
							buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + c + 8] = cols[0];
						}
					} else {
						for (c = 0; c < 8; c++) {
							buffer32->line[(pcjr->displine << 1)][(x << 3) + c + 8] =
							buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + c + 8] =
								cols[(fontdat[chr][pcjr->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
						}
					}
					if (drawcursor) {
						for (c = 0; c < 8; c++) {
						    buffer32->line[(pcjr->displine << 1)][(x << 3) + c + 8] ^= 15;
						    buffer32->line[(pcjr->displine << 1) + 1][(x << 3) + c + 8] ^= 15;
						}
					}
					pcjr->ma++;
				}
				break;
			case 0x00: /*40 column text*/
				for (x = 0; x < pcjr->crtc[1]; x++) {
					chr  = pcjr->vram[((pcjr->ma << 1) & mask) + offset];
					attr = pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
					drawcursor = ((pcjr->ma == ca) && pcjr->con && pcjr->cursoron);
					if (pcjr->array[3] & 4) {
						cols[1] = pcjr->array[ ((attr & 15)      & pcjr->array[1]) + 16] + 16;
						cols[0] = pcjr->array[(((attr >> 4) & 7) & pcjr->array[1]) + 16] + 16;
						if ((pcjr->blink & 16) && (attr & 0x80) && !drawcursor) 
							cols[1] = cols[0];
					} else {
						cols[1] = pcjr->array[((attr & 15) & pcjr->array[1]) + 16] + 16;
						cols[0] = pcjr->array[((attr >> 4) & pcjr->array[1]) + 16] + 16;
					}
					pcjr->ma++;
					if (pcjr->sc & 8) {
						for (c = 0; c < 8; c++) {
							buffer32->line[(pcjr->displine << 1)][(x << 4) + (c << 1) + 8] = 
							buffer32->line[(pcjr->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
							buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + (c << 1) + 8] = 
							buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[0];
						}
					} else {
						for (c = 0; c < 8; c++) {
							buffer32->line[(pcjr->displine << 1)][(x << 4) + (c << 1) + 8] =
							buffer32->line[(pcjr->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
							buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
							buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] =
								cols[(fontdat[chr][pcjr->sc & 7] & (1 << (c ^ 7))) ? 1 : 0];
						}
					}
					if (drawcursor) {
						for (c = 0; c < 16; c++) {
							buffer32->line[(pcjr->displine << 1)][(x << 4) + c + 8] ^= 15;
							buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + c + 8] ^= 15;
						}
					}
				}
				break;
			case 0x02: /*320x200x4*/
				cols[0] = pcjr->array[0 + 16] + 16;
				cols[1] = pcjr->array[1 + 16] + 16;
				cols[2] = pcjr->array[2 + 16] + 16;
				cols[3] = pcjr->array[3 + 16] + 16;
				for (x = 0; x < pcjr->crtc[1]; x++) {
					dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) | 
					       pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
					pcjr->ma++;
					for (c = 0; c < 8; c++) {
						buffer32->line[(pcjr->displine << 1)][(x << 4) + (c << 1) + 8] =
						buffer32->line[(pcjr->displine << 1)][(x << 4) + (c << 1) + 1 + 8] =
						buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + (c << 1) + 8] =
						buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
						dat <<= 2;
					}
				}
				break;
			case 0x102: /*640x200x2*/
				cols[0] = pcjr->array[0 + 16] + 16;
				cols[1] = pcjr->array[1 + 16] + 16;
				for (x = 0; x < pcjr->crtc[1]; x++) {
					dat = (pcjr->vram[((pcjr->ma << 1) & mask) + offset] << 8) |
					       pcjr->vram[((pcjr->ma << 1) & mask) + offset + 1];
					pcjr->ma++;
					for (c = 0; c < 16; c++) {
						buffer32->line[(pcjr->displine << 1)][(x << 4) + c + 8] =
						buffer32->line[(pcjr->displine << 1) + 1][(x << 4) + c + 8] =
							cols[dat >> 15];
						dat <<= 1;
					}
				}
				break;
		}
	} else {
		if (pcjr->array[3] & 4) {
			if (pcjr->array[0] & 1) {
				hline(buffer32, 0, (pcjr->displine << 1), (pcjr->crtc[1] << 3) + 16, (pcjr->array[2] & 0xf) + 16);
				hline(buffer32, 0, (pcjr->displine << 1) + 1, (pcjr->crtc[1] << 3) + 16, (pcjr->array[2] & 0xf) + 16);
			} else {
				hline(buffer32, 0, (pcjr->displine << 1), (pcjr->crtc[1] << 4) + 16, (pcjr->array[2] & 0xf) + 16);
				hline(buffer32, 0, (pcjr->displine << 1) + 1, (pcjr->crtc[1] << 4) + 16, (pcjr->array[2] & 0xf) + 16);
			}
		} else {
			cols[0] = pcjr->array[0 + 16] + 16;
			if (pcjr->array[0] & 1) {
				hline(buffer32, 0, (pcjr->displine << 1), (pcjr->crtc[1] << 3) + 16, cols[0]);
				hline(buffer32, 0, (pcjr->displine << 1) + 1, (pcjr->crtc[1] << 3) + 16, cols[0]);
			} else {
				hline(buffer32, 0, (pcjr->displine << 1), (pcjr->crtc[1] << 4) + 16, cols[0]);
				hline(buffer32, 0, (pcjr->displine << 1) + 1, (pcjr->crtc[1] << 4) + 16, cols[0]);
			}
		}
	}
	if (pcjr->array[0] & 1) x = (pcjr->crtc[1] << 3) + 16;
	else		    x = (pcjr->crtc[1] << 4) + 16;
	if (pcjr->composite) {
		Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[(pcjr->displine << 1)]);
		Composite_Process(pcjr->array[0], 0, x >> 2, buffer32->line[(pcjr->displine << 1) + 1]);
	}
	pcjr->sc = oldsc;
	if (pcjr->vc == pcjr->crtc[7] && !pcjr->sc) {
		pcjr->stat |= 8;
	}
	pcjr->displine++;
	if (pcjr->displine >= 360) 
		pcjr->displine = 0;
    } else {
	timer_advance_u64(&pcjr->timer, pcjr->dispontime);
	if (pcjr->dispon) 
		pcjr->stat |= 1;
	pcjr->linepos = 0;
	if (pcjr->vsynctime) {
		pcjr->vsynctime--;
		if (!pcjr->vsynctime) {
			pcjr->stat &= ~8;
		}
	}
	if (pcjr->sc == (pcjr->crtc[11] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == ((pcjr->crtc[11] & 31) >> 1))) { 
		pcjr->con = 0; 
		pcjr->coff = 1; 
	}
	if (pcjr->vadj) {
		pcjr->sc++;
		pcjr->sc &= 31;
		pcjr->ma = pcjr->maback;
		pcjr->vadj--;
		if (!pcjr->vadj) {
			pcjr->dispon = 1;
			pcjr->ma = pcjr->maback = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
			pcjr->sc = 0;
		}
	} else if (pcjr->sc == pcjr->crtc[9] || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == (pcjr->crtc[9] >> 1))) {
		pcjr->maback = pcjr->ma;
		pcjr->sc = 0;
		oldvc = pcjr->vc;
		pcjr->vc++;
		pcjr->vc &= 127;
		if (pcjr->vc == pcjr->crtc[6]) 
			pcjr->dispon = 0;
		if (oldvc == pcjr->crtc[4]) {
			pcjr->vc = 0;
			pcjr->vadj = pcjr->crtc[5];
			if (!pcjr->vadj) 
				pcjr->dispon = 1;
			if (!pcjr->vadj) 
				pcjr->ma = pcjr->maback = (pcjr->crtc[13] | (pcjr->crtc[12] << 8)) & 0x3fff;
			if ((pcjr->crtc[10] & 0x60) == 0x20) pcjr->cursoron = 0;
			else				  pcjr->cursoron = pcjr->blink & 16;
		}
		if (pcjr->vc == pcjr->crtc[7]) {
			pcjr->dispon = 0;
			pcjr->displine = 0;
			pcjr->vsynctime = 16;
			picint(1 << 5);
			if (pcjr->crtc[7]) {
				if (pcjr->array[0] & 1) x = (pcjr->crtc[1] << 3) + 16;
				else		    x = (pcjr->crtc[1] << 4) + 16;
				pcjr->lastline++;

				xs_temp = x;
				ys_temp = (pcjr->lastline - pcjr->firstline) << 1;

				if ((xs_temp > 0) && (ys_temp > 0)) {
					if (xs_temp < 64) xs_temp = 656;
					if (ys_temp < 32) ys_temp = 400;
					if (!enable_overscan)
						xs_temp -= 16;

					if ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get()) {
						xsize = xs_temp;
						ysize = ys_temp;
						set_screen_size(xsize, ysize + (enable_overscan ? 16 : 0));

						if (video_force_resize_get())
							video_force_resize_set(0);
					}

					if (enable_overscan) {
						if (pcjr->composite) 
							video_blit_memtoscreen(0, (pcjr->firstline - 4) << 1, 0, ((pcjr->lastline - pcjr->firstline) + 8) << 1,
								       xsize, ((pcjr->lastline - pcjr->firstline) + 8) << 1);
						else
							video_blit_memtoscreen_8(0, (pcjr->firstline - 4) << 1, 0, ((pcjr->lastline - pcjr->firstline) + 8) << 1,
										 xsize, ((pcjr->lastline - pcjr->firstline) + 8) << 1);
					} else {
						if (pcjr->composite) 
							video_blit_memtoscreen(8, pcjr->firstline << 1, 0, (pcjr->lastline - pcjr->firstline) << 1,
								       xsize, (pcjr->lastline - pcjr->firstline) << 1);
						else
							video_blit_memtoscreen_8(8, pcjr->firstline << 1, 0, (pcjr->lastline - pcjr->firstline) << 1,
										 xsize, (pcjr->lastline - pcjr->firstline) << 1);
					}
				}

				frames++;
				video_res_x = xsize;
				video_res_y = ysize;
			}
			pcjr->firstline = 1000;
			pcjr->lastline = 0;
			pcjr->blink++;
		}
	} else {
		pcjr->sc++;
		pcjr->sc &= 31;
		pcjr->ma = pcjr->maback;
	}
	if ((pcjr->sc == (pcjr->crtc[10] & 31) || ((pcjr->crtc[8] & 3) == 3 && pcjr->sc == ((pcjr->crtc[10] & 31) >> 1)))) 
		pcjr->con = 1;
    }
}


static void
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *)priv;

    if ((port >= 0xa0) && (port <= 0xa7))
	port = 0xa0;

    switch (port) {
	case 0x60:
		pcjr->pa = val;
		break;

	case 0x61:
		pcjr->pb = val;

		timer_process();

		if (cassette != NULL)
			pc_cas_set_motor(cassette, (pcjr->pb & 0x08) == 0);

		speaker_update();
		speaker_gated = val & 1;
		speaker_enable = val & 2;
		if (speaker_enable) 
			was_speaker_enable = 1;
		pit_ctr_set_gate(&pit->counters[2], val & 1);
		sn76489_mute = speaker_mute = 1;
		switch (val & 0x60) {
			case 0x00:
				speaker_mute = 0;
				break;

			case 0x60:
				sn76489_mute = 0;
			break;
		}
		break;

	case 0xa0:
		nmi_mask = val & 0x80;
		pit_ctr_set_using_timer(&pit->counters[1], !(val & 0x20));
		break;
    }
}


static uint8_t
kbd_read(uint16_t port, void *priv)
{
    pcjr_t *pcjr = (pcjr_t *)priv;
    uint8_t ret = 0xff;

    if ((port >= 0xa0) && (port <= 0xa7))
	port = 0xa0;

    switch (port) {
	case 0x60:
		ret = pcjr->pa;
		break;
		
	case 0x61:
		ret = pcjr->pb;
		break;

	case 0x62:
		ret = (pcjr->latched ? 1 : 0);
		ret |= 0x02; /*Modem card not installed*/
		if ((pcjr->pb & 0x08) || (cassette == NULL))
			ret |= (ppispeakon ? 0x10 : 0);
		else
			ret |= (pc_cas_get_inp(cassette) ? 0x10 : 0);
		ret |= (ppispeakon ? 0x10 : 0);
		ret |= (ppispeakon ? 0x20 : 0);
		ret |= (pcjr->data ? 0x40: 0);
		if (pcjr->data)
			ret |= 0x40;
		break;
		
	case 0xa0:
		pcjr->latched = 0;
		ret = 0;
		break;
    }

    return(ret);
}


static void
kbd_poll(void *priv)
{
    pcjr_t *pcjr = (pcjr_t *)priv;
    int c, p = 0, key;

    timer_advance_u64(&pcjr->send_delay_timer, 220 * TIMER_USEC);

    if (key_queue_start != key_queue_end &&
	!pcjr->serial_pos && !pcjr->latched) {
	key = key_queue[key_queue_start];

	key_queue_start = (key_queue_start + 1) & 0xf;

	pcjr->latched = 1;
	pcjr->serial_data[0] = 1; /*Start bit*/
	pcjr->serial_data[1] = 0;

	for (c = 0; c < 8; c++) {
		if (key & (1 << c)) {
			pcjr->serial_data[(c + 1) * 2] = 1;
			pcjr->serial_data[(c + 1) * 2 + 1] = 0;
			p++;
		} else {
			pcjr->serial_data[(c + 1) * 2] = 0;
			pcjr->serial_data[(c + 1) * 2 + 1] = 1;
		}
	}

	if (p & 1) { /*Parity*/
		pcjr->serial_data[9 * 2] = 1;
		pcjr->serial_data[9 * 2 + 1] = 0;
	} else {
		pcjr->serial_data[9 * 2] = 0;
		pcjr->serial_data[9 * 2 + 1] = 1;
	}

	for (c = 0; c < 11; c++) { /*11 stop bits*/
		pcjr->serial_data[(c + 10) * 2]     = 0;
		pcjr->serial_data[(c + 10) * 2 + 1] = 0;
	}

	pcjr->serial_pos++;
    }

    if (pcjr->serial_pos) {
	pcjr->data = pcjr->serial_data[pcjr->serial_pos - 1];
	nmi = pcjr->data;
	pcjr->serial_pos++;
	if (pcjr->serial_pos == 42+1)
		pcjr->serial_pos = 0;
    }
}


static void
kbd_adddata(uint16_t val)
{
    key_queue[key_queue_end] = val;
    key_queue_end = (key_queue_end + 1) & 0xf;
}




static void
kbd_adddata_ex(uint16_t val)
{
	kbd_adddata_process(val, kbd_adddata);
}


static void
speed_changed(void *priv)
{
    pcjr_t *pcjr = (pcjr_t *)priv;

    recalc_timings(pcjr);
}


static const device_config_t pcjr_config[] = {
    {
	"display_type", "Display type", CONFIG_SELECTION, "", PCJR_RGB, "", { 0 },
	{
		{
			"RGB", PCJR_RGB
		},
		{
			"Composite", PCJR_COMPOSITE
		},
		{
			""
		}
	}
    },
    {
	    "", "", -1
    }
};


static const device_t pcjr_device = {
    "IBM PCjr",
    0, 0,
    NULL, NULL, NULL,
    { NULL },
    speed_changed,
    NULL,
    pcjr_config
};


const device_t *
pcjr_get_device(void)
{
    return &pcjr_device;
}


int
machine_pcjr_init(const machine_t *model)
{
    int display_type;
    pcjr_t *pcjr;

    int ret;

    ret = bios_load_linear("roms/machines/ibmpcjr/bios.rom",
			   0x000f0000, 65536, 0);

    if (bios_only || !ret)
	return ret;

    pcjr = malloc(sizeof(pcjr_t));
    memset(pcjr, 0x00, sizeof(pcjr_t));
    pcjr->memctrl = -1;
    display_type = machine_get_config_int("display_type");
    pcjr->composite = (display_type != PCJR_RGB);

    pic_init_pcjr();
    pit_common_init(0, pit_irq0_timer_pcjr, NULL);

    cpu_set();

    /* Initialize the video controller. */
    video_reset(gfxcard);
    loadfont("roms/video/mda/mda.rom", 0);
    mem_mapping_add(&pcjr->mapping, 0xb8000, 0x08000,
		    vid_read, NULL, NULL,
		    vid_write, NULL, NULL,  NULL, 0, pcjr);
    io_sethandler(0x03d0, 16,
		  vid_in, NULL, NULL, vid_out, NULL, NULL, pcjr);
    timer_add(&pcjr->timer, vid_poll, pcjr, 1);
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_dram);
    device_add_ex(&pcjr_device, pcjr);
    cga_palette = 0;
    cgapal_rebuild();

    /* Initialize the keyboard. */
    keyboard_scan = 1;
    key_queue_start = key_queue_end = 0;
    io_sethandler(0x0060, 4,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, pcjr);
    io_sethandler(0x00a0, 8,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, pcjr);
    timer_add(&pcjr->send_delay_timer, kbd_poll, pcjr, 1);
    keyboard_set_table(scancode_xt);
    keyboard_send = kbd_adddata_ex;

    /* Technically it's the SN76496N, but the NCR 8496 is a drop-in replacement for it. */
    device_add(&ncr8496_device);

    nmi_mask = 0x80;

    device_add(&fdc_pcjr_device);

    device_add(&i8250_pcjr_device);
    serial_set_next_inst(2);	/* So that serial_standalone_init() won't do anything. */

    return ret;
}
