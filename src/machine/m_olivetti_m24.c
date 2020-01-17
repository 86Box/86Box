/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Olivetti M24.
 *
 * Version:	@(#)m_olivetti_m24.c	1.0.21	2019/11/15
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include "../86box.h"
#include "../timer.h"
#include "../io.h"
#include "../pic.h"
#include "../pit.h"
#include "../ppi.h"
#include "../nmi.h"
#include "../mem.h"
#include "../device.h"
#include "../nvr.h"
#include "../keyboard.h"
#include "../mouse.h"
#include "../rom.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../game/gameport.h"
#include "../sound/sound.h"
#include "../sound/snd_speaker.h"
#include "../video/video.h"
#include "machine.h"


#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01


typedef struct {
    /* Video stuff. */
    mem_mapping_t mapping;
    uint8_t	crtc[32];
    int		crtcreg;
	uint8_t monitor_type, port_23c6;
    uint8_t	*vram;
    uint8_t	charbuffer[256];
    uint8_t	ctrl;
    uint32_t	base;
    uint8_t	cgamode, cgacol;
    uint8_t	stat;
    int		linepos, displine;
    int		sc, vc;
    int		con, coff, cursoron, blink;
    int	vsynctime;
    int		vadj;
    int		lineff;
    uint16_t	ma, maback;
    int		dispon;
    uint64_t	dispontime, dispofftime;
    pc_timer_t	timer;
    int		firstline, lastline;

    /* Keyboard stuff. */
    int		wantirq;
    uint8_t	command;
    uint8_t	status;
    uint8_t	out;
    uint8_t	output_port;
    int		param,
		param_total;
    uint8_t	params[16];
    uint8_t	scan[7];

    /* Mouse stuff. */
    int		mouse_mode;
    int		x, y, b;
	pc_timer_t send_delay_timer;
} olim24_t;

static video_timings_t timing_m24      = {VIDEO_ISA, 8,16,32, 8,16,32};


static uint8_t crtcmask[32] = {
    0xff, 0xff, 0xff, 0xff, 0x7f, 0x1f, 0x7f, 0x7f,
    0xf3, 0x1f, 0x7f, 0x1f, 0x3f, 0xff, 0x3f, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static uint8_t	key_queue[16];
static int	key_queue_start = 0,
		key_queue_end = 0;

		
		
#ifdef ENABLE_M24VID_LOG
int m24vid_do_log = ENABLE_M24VID_LOG;


static void
m24_log(const char *fmt, ...)
{
    va_list ap;

    if (m24vid_do_log) {
	va_start(ap, fmt);
	vfprintf(stdlog, fmt, ap);
	va_end(ap);
	fflush(stdlog);
    }
}
#else
#define m24_log(fmt, ...)
#endif


static void
recalc_timings(olim24_t *m24)
{
    double _dispontime, _dispofftime, disptime;

    if (m24->cgamode & 1) {
	disptime    = m24->crtc[0] + 1;
	_dispontime = m24->crtc[1];
    } else {
	disptime    = (m24->crtc[0] + 1) << 1;
	_dispontime = m24->crtc[1] << 1;
    }

    _dispofftime = disptime - _dispontime;
    _dispontime  *= CGACONST / 2;
    _dispofftime *= CGACONST / 2;
    m24->dispontime  = (uint64_t)(_dispontime);
    m24->dispofftime = (uint64_t)(_dispofftime);
}


static void
vid_out(uint16_t addr, uint8_t val, void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;
    uint8_t old;

    switch (addr) {
	case 0x3d4:
		m24->crtcreg = val & 31;
		break;

	case 0x3d5:
		old = m24->crtc[m24->crtcreg];
		m24->crtc[m24->crtcreg] = val & crtcmask[m24->crtcreg];
		if (old != val) {
			if (m24->crtcreg < 0xe || m24->crtcreg > 0x10) {
				fullchange = changeframecount;
				recalc_timings(m24);
			}
		}
		break;

	case 0x3d8:
		m24->cgamode = val;
		break;

	case 0x3d9:
		m24->cgacol = val;
		break;

	case 0x3de:
		m24->ctrl = val;
		m24->base = (val & 0x08) ? 0x4000 : 0;
		break;
		
	case 0x13c6:
		m24->monitor_type = val;
		break;
		
	case 0x23c6:
		m24->port_23c6 = val;
		break;
    }
}


static uint8_t
vid_in(uint16_t addr, void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;
    uint8_t ret = 0xff;

    switch (addr) {
	case 0x3d4:
		ret = m24->crtcreg;
		break;

	case 0x3d5:
		ret = m24->crtc[m24->crtcreg];
		break;

	case 0x3da:
		ret = m24->stat;
		break;
		
	case 0x13c6:
		ret = m24->monitor_type;
		break;
		
	case 0x23c6:
		ret = m24->port_23c6;
		break;
    }

    return(ret);
}


static void
vid_write(uint32_t addr, uint8_t val, void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;
	int offset;

    m24->vram[addr & 0x7FFF]=val;
	offset = ((timer_get_remaining_u64(&m24->timer) / CGACONST) * 4) & 0xfc;
	m24->charbuffer[offset] = m24->vram[addr & 0x7fff];
	m24->charbuffer[offset | 1] = m24->vram[addr & 0x7fff];
}


static uint8_t
vid_read(uint32_t addr, void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;

    return(m24->vram[addr & 0x7FFF]);
}


static void
vid_poll(void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;
    uint16_t ca = (m24->crtc[15] | (m24->crtc[14] << 8)) & 0x3fff;
    int drawcursor;
    int x, c, xs_temp, ys_temp;
    int oldvc;
    uint8_t chr, attr;
    uint16_t dat, dat2;
    int cols[4];
    int col;
    int oldsc;

    if (!m24->linepos) {
	timer_advance_u64(&m24->timer, m24->dispofftime);
	m24->stat |= 1;
	m24->linepos = 1;
	oldsc = m24->sc;
	if ((m24->crtc[8] & 3) == 3) 
		m24->sc = (m24->sc << 1) & 7;
	if (m24->dispon) {
		if (m24->displine < m24->firstline) {
			m24->firstline = m24->displine;
		}
		m24->lastline = m24->displine;
		for (c = 0; c < 8; c++) 
		{
			if ((m24->cgamode & 0x12) == 0x12) {
				((uint32_t *)buffer32->line[m24->displine])[c] = 0;
				if (m24->cgamode & 1)
					((uint32_t *)buffer32->line[m24->displine])[c + (m24->crtc[1] << 3) + 8] = 0;
				else
					((uint32_t *)buffer32->line[m24->displine])[c + (m24->crtc[1] << 4) + 8] = 0;
			} else {
				((uint32_t *)buffer32->line[m24->displine])[c] = (m24->cgacol & 15) + 16;
				if (m24->cgamode & 1)
					((uint32_t *)buffer32->line[m24->displine])[c + (m24->crtc[1] << 3) + 8] = (m24->cgacol & 15) + 16;
				else
					((uint32_t *)buffer32->line[m24->displine])[c + (m24->crtc[1] << 4) + 8] = (m24->cgacol & 15) + 16;
			}
		}
		if (m24->cgamode & 1) {
			for (x = 0; x < m24->crtc[1]; x++) {
				chr  = m24->charbuffer[ x << 1];
				attr = m24->charbuffer[(x << 1) + 1];
				drawcursor = ((m24->ma == ca) && m24->con && m24->cursoron);
				if (m24->cgamode & 0x20) {
					cols[1] = (attr & 15) + 16;
					cols[0] = ((attr >> 4) & 7) + 16;
					if ((m24->blink & 16) && (attr & 0x80) && !drawcursor) 
						cols[1] = cols[0];
				} else {
					cols[1] = (attr & 15) + 16;
					cols[0] = (attr >> 4) + 16;
				}
				if (drawcursor) {
					for (c = 0; c < 8; c++)
					    ((uint32_t *)buffer32->line[m24->displine])[(x << 3) + c + 8] = cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
				} else {
					for (c = 0; c < 8; c++)
					    ((uint32_t *)buffer32->line[m24->displine])[(x << 3) + c + 8] = cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff] & (1 << (c ^ 7))) ? 1 : 0];
				}
				m24->ma++;
			}
		} else if (!(m24->cgamode & 2)) {
			for (x = 0; x < m24->crtc[1]; x++) {
				chr  = m24->vram[((m24->ma << 1) & 0x3fff) + m24->base];
				attr = m24->vram[(((m24->ma << 1) + 1) & 0x3fff) + m24->base];
				drawcursor = ((m24->ma == ca) && m24->con && m24->cursoron);
				if (m24->cgamode & 0x20) {
					cols[1] = (attr & 15) + 16;
					cols[0] = ((attr >> 4) & 7) + 16;
					if ((m24->blink & 16) && (attr & 0x80)) 
						cols[1] = cols[0];
				} else {
					cols[1] = (attr & 15) + 16;
					cols[0] = (attr >> 4) + 16;
				}
				m24->ma++;
				if (drawcursor) {
					for (c = 0; c < 8; c++)
					    ((uint32_t *)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 8] = 
					    ((uint32_t *)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff] & (1 << (c ^ 7))) ? 1 : 0] ^ 15;
				} else {
					for (c = 0; c < 8; c++)
					    ((uint32_t *)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 8] = 
					    ((uint32_t *)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[(fontdatm[chr][((m24->sc & 7) << 1) | m24->lineff] & (1 << (c ^ 7))) ? 1 : 0];
				}
			}
		} else if (!(m24->cgamode & 16)) {
			cols[0] = (m24->cgacol & 15) | 16;
			col = (m24->cgacol & 16) ? 24 : 16;
			if (m24->cgamode & 4) {
					cols[1] = col | 3;
					cols[2] = col | 4;
					cols[3] = col | 7;
			} else if (m24->cgacol & 32) {
				cols[1] = col | 3;
				cols[2] = col | 5;
				cols[3] = col | 7;
			} else {
				cols[1] = col | 2;
				cols[2] = col | 4;
				cols[3] = col | 6;
			}
			for (x = 0; x < m24->crtc[1]; x++) {
				dat = (m24->vram[((m24->ma << 1) & 0x1fff) + ((m24->sc & 1) * 0x2000) + m24->base] << 8) | 
				       m24->vram[((m24->ma << 1) & 0x1fff) + ((m24->sc & 1) * 0x2000) + 1 + m24->base];
				m24->ma++;
				for (c = 0; c < 8; c++) {
					((uint32_t *)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 8] =
					((uint32_t *)buffer32->line[m24->displine])[(x << 4) + (c << 1) + 1 + 8] = cols[dat >> 14];
					dat <<= 2;
				}
			}
		} else {
			if (m24->ctrl & 1 || ((m24->monitor_type & 8) && (m24->port_23c6 & 1))) {
				dat2 = ((m24->sc & 1) * 0x4000) | (m24->lineff * 0x2000);
				cols[0] = 0; cols[1] = /*(m24->cgacol & 15)*/15 + 16;
			} else {
				dat2 = (m24->sc & 1) * 0x2000;
				cols[0] = 0; cols[1] = (m24->cgacol & 15) + 16;
			}
			
			for (x = 0; x < m24->crtc[1]; x++) {
				dat = (m24->vram[((m24->ma << 1) & 0x1fff) + dat2] << 8) | m24->vram[((m24->ma << 1) & 0x1fff) + dat2 + 1];
				m24->ma++;
				for (c = 0; c < 16; c++) {
					((uint32_t *)buffer32->line[m24->displine])[(x << 4) + c + 8] = cols[dat >> 15];
					dat <<= 1;
				}
			}
		}
	} else {
		cols[0] = ((m24->cgamode & 0x12) == 0x12) ? 0 : (m24->cgacol & 15) + 16;
		if (m24->cgamode & 1)	hline(buffer32, 0, m24->displine, (m24->crtc[1] << 3) + 16, cols[0]);
		else			hline(buffer32, 0, m24->displine, (m24->crtc[1] << 4) + 16, cols[0]);
	}

	if (m24->cgamode & 1)
		x = (m24->crtc[1] << 3) + 16;
	else
		x = (m24->crtc[1] << 4) + 16;

	m24->sc = oldsc;
	if (m24->vc == m24->crtc[7] && !m24->sc)
		m24->stat |= 8;
	m24->displine++;
	if (m24->displine >= 720) m24->displine = 0;
    } else {
	timer_advance_u64(&m24->timer, m24->dispontime);
	if (m24->dispon) m24->stat &= ~1;
	m24->linepos = 0;
	m24->lineff ^= 1;
	if (m24->lineff) {
		m24->ma = m24->maback;
	} else {
		if (m24->vsynctime) {
			m24->vsynctime--;
			if (!m24->vsynctime)
			   m24->stat &= ~8;
		}
		if (m24->sc == (m24->crtc[11] & 31) || ((m24->crtc[8] & 3) == 3 && m24->sc == ((m24->crtc[11] & 31) >> 1))) { 
			m24->con = 0; 
			m24->coff = 1; 
		}
		if (m24->vadj) {
			m24->sc++;
			m24->sc &= 31;
			m24->ma = m24->maback;
			m24->vadj--;
			if (!m24->vadj) {
				m24->dispon = 1;
				m24->ma = m24->maback = (m24->crtc[13] | (m24->crtc[12] << 8)) & 0x3fff;
				m24->sc = 0;
			}
		} else if (m24->sc == m24->crtc[9] || ((m24->crtc[8] & 3) == 3 && m24->sc == (m24->crtc[9] >> 1))) {
			m24->maback = m24->ma;
			m24->sc = 0;
			oldvc = m24->vc;
			m24->vc++;
			m24->vc &= 127;

			if (m24->vc == m24->crtc[6]) 
				m24->dispon=0;

			if (oldvc == m24->crtc[4]) {
				m24->vc = 0;
				m24->vadj = m24->crtc[5];
				if (!m24->vadj) m24->dispon = 1;
				if (!m24->vadj) m24->ma = m24->maback = (m24->crtc[13] | (m24->crtc[12] << 8)) & 0x3fff;
				if ((m24->crtc[10] & 0x60) == 0x20)
					m24->cursoron = 0;
				else
					m24->cursoron = m24->blink & 16;
			}

			if (m24->vc == m24->crtc[7]) {
				m24->dispon = 0;
				m24->displine = 0;
				m24->vsynctime = (m24->crtc[3] >> 4) + 1;
				if (m24->crtc[7]) {
					if (m24->cgamode & 1)
						x = (m24->crtc[1] << 3) + 16;
					else
						x = (m24->crtc[1] << 4) + 16;
					m24->lastline++;

					xs_temp = x;
					ys_temp = (m24->lastline - m24->firstline);

					if ((xs_temp > 0) && (ys_temp > 0)) {
						if (xsize < 64) xs_temp = 656;
						if (ysize < 32) ys_temp = 200;
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
							video_blit_memtoscreen_8(0, m24->firstline - 8, 0, (m24->lastline - m24->firstline) + 16,
										 xsize, (m24->lastline - m24->firstline) + 16);
						} else
							video_blit_memtoscreen_8(8, m24->firstline, 0, (m24->lastline - m24->firstline),
										 xsize, (m24->lastline - m24->firstline));
					}

					frames++;

					video_res_x = xsize;
					video_res_y = ysize;
					if (m24->cgamode & 1) {
						video_res_x /= 8;
						video_res_y /= (m24->crtc[9] + 1) * 2;
						video_bpp = 0;
					} else if (!(m24->cgamode & 2)) {
						video_res_x /= 16;
						video_res_y /= (m24->crtc[9] + 1) * 2;
						video_bpp = 0;
					} else if (!(m24->cgamode & 16)) {
						video_res_x /= 2;
						video_res_y /= 2;
						video_bpp = 2;
					} else if (!(m24->ctrl & 1)) {
						video_res_y /= 2;
						video_bpp = 1;
					}
				}
				m24->firstline = 1000;
				m24->lastline = 0;
			m24->blink++;
			}
		} else {
			m24->sc++;
			m24->sc &= 31;
			m24->ma = m24->maback;
		}
		if ((m24->sc == (m24->crtc[10] & 31) || ((m24->crtc[8] & 3) == 3 && m24->sc == ((m24->crtc[10] & 31) >> 1)))) 
			m24->con = 1;
	}
	if (m24->dispon && (m24->cgamode & 1)) {
		for (x = 0; x < (m24->crtc[1] << 1); x++)
		    m24->charbuffer[x] = m24->vram[(((m24->ma << 1) + x) & 0x3fff) + m24->base];
	}
    }
}


static void
speed_changed(void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;

    recalc_timings(m24);
}


static void
kbd_poll(void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;

    timer_advance_u64(&m24->send_delay_timer, 1000 * TIMER_USEC);
    if (m24->wantirq) {
	m24->wantirq = 0;
	picint(2);
#if ENABLE_KEYBOARD_LOG
	m24_log("M24: take IRQ\n");
#endif
    }

    if (!(m24->status & STAT_OFULL) && key_queue_start != key_queue_end) {
#if ENABLE_KEYBOARD_LOG
	m24_log("Reading %02X from the key queue at %i\n",
				m24->out, key_queue_start);
#endif
	m24->out = key_queue[key_queue_start];
	key_queue_start = (key_queue_start + 1) & 0xf;
	m24->status |=  STAT_OFULL;
	m24->status &= ~STAT_IFULL;
	m24->wantirq = 1;
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
kbd_write(uint16_t port, uint8_t val, void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;

#if ENABLE_KEYBOARD_LOG
    m24_log("M24: write %04X %02X\n", port, val);
#endif

#if 0
    if (ram[8] == 0xc3) {
	output = 3;
    }
#endif
    switch (port) {
	case 0x60:
		if (m24->param != m24->param_total) {
			m24->params[m24->param++] = val;
			if (m24->param == m24->param_total) {
				switch (m24->command) {
					case 0x11:
						m24->mouse_mode = 0;
						m24->scan[0] = m24->params[0];
						m24->scan[1] = m24->params[1];
						m24->scan[2] = m24->params[2];
						m24->scan[3] = m24->params[3];
						m24->scan[4] = m24->params[4];
						m24->scan[5] = m24->params[5];
						m24->scan[6] = m24->params[6];
						break;

					case 0x12:
						m24->mouse_mode = 1;
						m24->scan[0] = m24->params[0];
						m24->scan[1] = m24->params[1];
						m24->scan[2] = m24->params[2];
						break;
					
					default:
						m24_log("M24: bad keyboard command complete %02X\n", m24->command);
				}
			}
		} else {
			m24->command = val;
			switch (val) {
				case 0x01: /*Self-test*/
					break;

				case 0x05: /*Read ID*/
					kbd_adddata(0x00);
					break;

				case 0x11:
					m24->param = 0;
					m24->param_total = 9;
					break;

				case 0x12:
					m24->param = 0;
					m24->param_total = 4;
					break;

				default:
					m24_log("M24: bad keyboard command %02X\n", val);
			}
		}
		break;

	case 0x61:
		ppi.pb = val;

		speaker_update();
		speaker_gated = val & 1;
		speaker_enable = val & 2;
		if (speaker_enable) 
			was_speaker_enable = 1;
		pit_ctr_set_gate(&pit->counters[2], val & 1);
		break;
    }
}


static uint8_t
kbd_read(uint16_t port, void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x60:
		ret = m24->out;
		if (key_queue_start == key_queue_end) {
			m24->status &= ~STAT_OFULL;
			m24->wantirq = 0;
		} else {
			m24->out = key_queue[key_queue_start];
			key_queue_start = (key_queue_start + 1) & 0xf;
			m24->status |= STAT_OFULL;
			m24->status &= ~STAT_IFULL;
			m24->wantirq = 1;	
		}
		break;

	case 0x61:
		ret = ppi.pb;
		break;

	case 0x64:
		ret = m24->status;
		m24->status &= ~(STAT_RTIMEOUT | STAT_TTIMEOUT);
		break;

	default:
		m24_log("\nBad M24 keyboard read %04X\n", port);
    }

    return(ret);
}


static int
ms_poll(int x, int y, int z, int b, void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;

    m24->x += x;
    m24->y += y;

    if (((key_queue_end - key_queue_start) & 0xf) > 14) return(0xff);

    if ((b & 1) && !(m24->b & 1))
	kbd_adddata(m24->scan[0]);
    if (!(b & 1) && (m24->b & 1))
	kbd_adddata(m24->scan[0] | 0x80);
    m24->b = (m24->b & ~1) | (b & 1);

    if (((key_queue_end - key_queue_start) & 0xf) > 14) return(0xff);

    if ((b & 2) && !(m24->b & 2))
	kbd_adddata(m24->scan[2]);
    if (!(b & 2) && (m24->b & 2))
	kbd_adddata(m24->scan[2] | 0x80);
    m24->b = (m24->b & ~2) | (b & 2);

    if (((key_queue_end - key_queue_start) & 0xf) > 14) return(0xff);

    if ((b & 4) && !(m24->b & 4))
	kbd_adddata(m24->scan[1]);
    if (!(b & 4) && (m24->b & 4))
	kbd_adddata(m24->scan[1] | 0x80);
    m24->b = (m24->b & ~4) | (b & 4);

    if (m24->mouse_mode) {
	if (((key_queue_end - key_queue_start) & 0xf) > 12) return(0xff);

	if (!m24->x && !m24->y) return(0xff);
	
	m24->y = -m24->y;

	if (m24->x < -127) m24->x = -127;
	if (m24->x >  127) m24->x =  127;
	if (m24->x < -127) m24->x = 0x80 | ((-m24->x) & 0x7f);

	if (m24->y < -127) m24->y = -127;
	if (m24->y >  127) m24->y =  127;
	if (m24->y < -127) m24->y = 0x80 | ((-m24->y) & 0x7f);

	kbd_adddata(0xfe);
	kbd_adddata(m24->x);
	kbd_adddata(m24->y);

	m24->x = m24->y = 0;
    } else {
	while (m24->x < -4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24->x += 4;
		kbd_adddata(m24->scan[3]);
	}
	while (m24->x > 4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24->x -= 4;
		kbd_adddata(m24->scan[4]);
	}
	while (m24->y < -4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24->y += 4;
		kbd_adddata(m24->scan[5]);
	}
	while (m24->y > 4) {
		if (((key_queue_end - key_queue_start) & 0xf) > 14)
							return(0xff);
		m24->y -= 4;
		kbd_adddata(m24->scan[6]);
	}
    }

    return(0);
}


static uint8_t
m24_read(uint16_t port, void *priv)
{
    switch (port) {
	case 0x66:
		return 0x00;
	case 0x67:
		return 0x20 | 0x40 | 0x0C;
    }

    return(0xff);
}

static void
vid_close(void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;

    free(m24->vram);

    free(m24);
}

const device_t m24_device = {
    "Olivetti M24",
    0, 0,
    NULL, vid_close, NULL,
    NULL,
    speed_changed,
    NULL,
    NULL
};


static void
kbd_reset(void *priv)
{
    olim24_t *m24 = (olim24_t *)priv;
 
    /* Initialize the keyboard. */
    m24->status = STAT_LOCK | STAT_CD;
    m24->wantirq = 0;
    keyboard_scan = 1;
    m24->param = m24->param_total = 0;
    m24->mouse_mode = 0;
    m24->scan[0] = 0x1c;
    m24->scan[1] = 0x53;
    m24->scan[2] = 0x01;
    m24->scan[3] = 0x4b;
    m24->scan[4] = 0x4d;
    m24->scan[5] = 0x48;
    m24->scan[6] = 0x50;   
}


int
machine_olim24_init(const machine_t *model)
{
    int ret;

    ret = bios_load_interleaved(L"roms/machines/olivetti_m24/olivetti_m24_version_1.43_low.bin",
				L"roms/machines/olivetti_m24/olivetti_m24_version_1.43_high.bin",
				0x000fc000, 16384, 0);

    if (bios_only || !ret)
	return ret;

    olim24_t *m24;

    m24 = (olim24_t *)malloc(sizeof(olim24_t));
    memset(m24, 0x00, sizeof(olim24_t));

    machine_common_init(model);
    device_add(&fdc_xt_device);

    io_sethandler(0x0066, 2, m24_read, NULL, NULL, NULL, NULL, NULL, m24);

    /* Initialize the video adapter. */
    // loadfont(L"roms/machines/olivetti_m24/ATT-FONT-DUMPED-VERIFIED.BIN", 1);
    loadfont(L"roms/machines/olivetti_m24/m24 graphics board go380 258 pqbq.bin", 1);
    m24->vram = malloc(0x8000);
    overscan_x = overscan_y = 16;
    mem_mapping_add(&m24->mapping, 0xb8000, 0x08000,
		    vid_read, NULL, NULL,
		    vid_write, NULL, NULL,  NULL, 0, m24);
    io_sethandler(0x03d0, 16, vid_in, NULL, NULL, vid_out, NULL, NULL, m24);
    timer_add(&m24->timer, vid_poll, m24, 1);
    device_add_ex(&m24_device, m24);
    video_inform(VIDEO_FLAG_TYPE_CGA, &timing_m24);
    cga_palette = 0;
    cgapal_rebuild();

    /* Initialize the keyboard. */
    io_sethandler(0x0060, 2,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, m24);
    io_sethandler(0x0064, 1,
		  kbd_read, NULL, NULL, kbd_write, NULL, NULL, m24);
    keyboard_send = kbd_adddata_ex;
    kbd_reset(m24);
    timer_add(&m24->send_delay_timer, kbd_poll, m24, 1);

    /* Tell mouse driver about our internal mouse. */
    mouse_reset();
    mouse_set_poll(ms_poll, m24);

    keyboard_set_table(scancode_xt);

    if (joystick_type != JOYSTICK_TYPE_NONE)
	device_add(&gameport_device);

    /* FIXME: make sure this is correct?? */
    device_add(&at_nvr_device);

    nmi_init();

    return ret;
}
