/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the TI CF62011 SVGA chip.
 *
 *          This chip was used in several of IBM's later machines, such
 *          as the PS/1 Model 2121, and a number of PS/2 models. As noted
 *          in an article on Usenet:
 *
 *            "In the early 90s IBM looked for some cheap VGA card to
 *            substitute the (relatively) expensive XGA-2 adapter for
 *            *servers*, where the primary purpose is supervision of the
 *            machine rather than real *work* with it in Hi-Res. It was
 *            just to supply a base video, where a XGA-2 were a waste of
 *            potential. They had a contract with TI for some DSPs in
 *            multimedia already (the MWave for instance is based on
 *            TI-DSPs as well as many Thinkpad internal chipsets) and TI
 *            offered them a rather cheap – and inexpensive – chipset
 *            and combined it with a cheap clock oscillator and an Inmos
 *            RAMDAC. That chipset was already pretty much outdated at
 *            that time but IBM decided it would suffice for that low
 *            end purpose.
 *
 *            Driver support was given under DOS and OS/2 only for base
 *            functions like selection of the vertical refresh and few
 *            different modes only. Not even the Win 3.x support has
 *            been finalized. Technically the adapter could do better
 *            than VGA, but its video BIOS is largely undocumented and
 *            intentionally crippled down to a few functions."
 *
 *          This chip is reportedly the same one as used in the MCA
 *          IBM SVGA Adapter/A (ID 090EEh), which mostly had faster
 *          VRAM and RAMDAC. The VESA DOS graphics driver for that
 *          card can be used: m95svga.exe
 *
 *          The controller responds at ports in the range 0x2100-0x210F,
 *          which are the same as the XGA. It supports up to 1MB of VRAM,
 *          but we lock it down to 512K. The PS/1 2122 had 256K.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_upd7220.h>

//**************************************************************************
//  MACROS / CONSTANTS
//**************************************************************************

// TODO: typedef
enum
{
	COMMAND_INVALID = -1,
	COMMAND_RESET,
	COMMAND_RESET2,
	COMMAND_RESET3,
	COMMAND_SYNC,
	COMMAND_VSYNC,
	COMMAND_CCHAR,
	COMMAND_START,
	COMMAND_BLANK,
	COMMAND_BLANK2,
	COMMAND_ZOOM,
	COMMAND_CURS,
	COMMAND_PRAM,
	COMMAND_PITCH,
	COMMAND_WDAT,
	COMMAND_MASK,
	COMMAND_FIGS,
	COMMAND_FIGD,
	COMMAND_GCHRD,
	COMMAND_RDAT,
	COMMAND_CURD,
	COMMAND_LPRD,
	COMMAND_DMAR,
	COMMAND_DMAW,
	COMMAND_5A
};

enum
{
	FIFO_READ = 0,
	FIFO_WRITE
};

enum
{
	FIFO_EMPTY = -1,
	FIFO_PARAMETER,
	FIFO_COMMAND
};

#define UPD7220_COMMAND_RESET           0x00
#define UPD7220_COMMAND_RESET2          0x01
#define UPD7220_COMMAND_RESET3          0x09
#define UPD7220_COMMAND_SYNC            0x0e // & 0xfe
#define UPD7220_COMMAND_VSYNC           0x6e // & 0xfe
#define UPD7220_COMMAND_CCHAR           0x4b
#define UPD7220_COMMAND_START           0x6b
#define UPD7220_COMMAND_BLANK           0x0c // & 0xfe
#define UPD7220_COMMAND_BLANK2          0x05
#define UPD7220_COMMAND_ZOOM            0x46
#define UPD7220_COMMAND_CURS            0x49
#define UPD7220_COMMAND_PRAM            0x70 // & 0xf0
#define UPD7220_COMMAND_PITCH           0x47
#define UPD7220_COMMAND_WDAT            0x20 // & 0xe4
#define UPD7220_COMMAND_MASK            0x4a
#define UPD7220_COMMAND_FIGS            0x4c
#define UPD7220_COMMAND_FIGD            0x6c
#define UPD7220_COMMAND_GCHRD           0x68
#define UPD7220_COMMAND_RDAT            0xa0 // & 0xe4
#define UPD7220_COMMAND_CURD            0xe0
#define UPD7220_COMMAND_LPRD            0xc0
#define UPD7220_COMMAND_DMAR            0xa4 // & 0xe4
#define UPD7220_COMMAND_DMAW            0x24 // & 0xe4
#define UPD7220_COMMAND_5A              0x5a

#define UPD7220_SR_DATA_READY           0x01
#define UPD7220_SR_FIFO_FULL            0x02
#define UPD7220_SR_FIFO_EMPTY           0x04
#define UPD7220_SR_DRAWING_IN_PROGRESS  0x08
#define UPD7220_SR_DMA_EXECUTE          0x10
#define UPD7220_SR_VSYNC_ACTIVE         0x20
#define UPD7220_SR_HBLANK_ACTIVE        0x40
#define UPD7220_SR_LIGHT_PEN_DETECT     0x80

#define UPD7220_MODE_REFRESH_RAM        0x04
#define UPD7220_MODE_DRAW_ON_RETRACE    0x10
#define UPD7220_MODE_DISPLAY_MASK       0x22
#define UPD7220_MODE_DISPLAY_MIXED      0x00
#define UPD7220_MODE_DISPLAY_GRAPHICS   0x02
#define UPD7220_MODE_DISPLAY_CHARACTER  0x20
#define UPD7220_MODE_DISPLAY_INVALID    0x22
#define UPD7220_MODE_INTERLACE_MASK     0x09
#define UPD7220_MODE_INTERLACE_NONE     0x00
#define UPD7220_MODE_INTERLACE_INVALID  0x01
#define UPD7220_MODE_INTERLACE_REPEAT   0x08
#define UPD7220_MODE_INTERLACE_ON       0x09

static const int x_dir[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
static const int y_dir[8] = { 1, 1, 0,-1,-1,-1, 0, 1};

static __inline void
upd7220_write_word(upd7220_t *dev, uint16_t address, uint16_t data)
{
    dev->vram[address] = data;
    dev->fullchange = 3;
}

static __inline uint16_t
upd7220_read_word(upd7220_t *dev, uint16_t address)
{
    uint16_t data = dev->vram[address];

    return data;
}

static __inline void
upd7220_fifo_clear(upd7220_t *dev)
{
    for (int i = 0; i < 16; i++) {
        dev->fifo[i] = 0;
        dev->fifo_flag[i] = UPD7220_FIFO_EMPTY;
    }

    dev->fifo_ptr = -1;

    dev->sr &= ~UPD7220_SR_DATA_READY;
    dev->sr |= UPD7220_SR_FIFO_EMPTY;
    dev->sr &= ~UPD7220_SR_FIFO_FULL;
}

static __inline int
upd7220_fifo_param_count(upd7220_t *dev)
{
    int i;

    for (i = 0; i < 16; i++) {
        if (dev->fifo_flag[i] != UPD7220_FIFO_PARAMETER)
            break;
    }

    return i;
}

static __inline void
upd7220_fifo_set_direction(upd7220_t *dev, int dir)
{
    if (dev->fifo_dir != dir)
        upd7220_fifo_clear(dev);

    dev->fifo_dir = dir;
}

static __inline void
upd7220_queue(upd7220_t *dev, uint8_t data, int flag)
{
    if (dev->fifo_ptr < 15) {
        dev->fifo_ptr++;

        dev->fifo[dev->fifo_ptr] = data;
        dev->fifo_flag[dev->fifo_ptr] = flag;
        if (dev->fifo_ptr == 16)
            dev->sr |= UPD7220_SR_FIFO_FULL;

        dev->sr &= ~UPD7220_SR_FIFO_EMPTY;
    } else
        pclog("FIFO overflow?\n");
}

static __inline void
upd7220_dequeue(upd7220_t *dev, uint8_t *data, int *flag)
{
    *data = dev->fifo[0];
    *flag = dev->fifo_flag[0];

    if (dev->fifo_ptr > -1) {
        for (int i = 0; i < 15; i++) {
            dev->fifo[i] = dev->fifo[i + 1];
            dev->fifo_flag[i] = dev->fifo_flag[i + 1];
        }

        dev->fifo[0x0f] = 0;
        dev->fifo_flag[0x0f] = 0;
        dev->fifo_ptr--;

        if (dev->fifo_ptr <= 0)
            dev->sr |= UPD7220_SR_FIFO_EMPTY;
        if (dev->fifo_ptr == -1)
            dev->sr &= ~UPD7220_SR_DATA_READY;
    } else {
		/* TODO: underflow details */
		/* pc9821:skinpan does SR checks over the wrong port during intro ... */
        *data = 0xff;
    }
}

static void
upd7220_calc_mode(upd7220_t *dev)
{
    dev->mode = dev->pr[1];
    dev->aw = dev->pr[2] + 2;
    dev->hs = (dev->pr[3] & 0x1f) + 1;
    dev->vs = ((dev->pr[4] & 0x03) << 3) | (dev->pr[3] >> 5);
    dev->hfp = (dev->pr[4] >> 2) + 1;
    dev->hbp = (dev->pr[5] & 0x3f) + 1;
    dev->vfp = dev->pr[6] & 0x3f;
    dev->al = ((dev->pr[8] & 0x03) << 8) | dev->pr[7];
    dev->vbp = dev->pr[8] >> 2;

    dev->pitch = dev->aw;

    dev->pitch = ((dev->pr[5] & 0x40) << 2) | (dev->pitch & 0xff);
    dev->al += (dev->pr[6] >> 6) & 1;

    pclog("- MODE: %02x\n", dev->mode);
    pclog("- HS=%u, HBP=%u, AW=%u, HFP=%u, PITCH=%u\n", dev->hs, dev->hbp, dev->aw, dev->hfp, dev->pitch);
    pclog("- VS=%u, VBP=%u, AL=%u, VFP=%u\n", dev->vs, dev->vbp, dev->al, dev->vfp);

    dev->fullchange = changeframecount;
    upd7220_recalctimings(dev);
}

void
upd7220_recalctimings(upd7220_t *dev)
{
    double disptime;
    double _dispontime;
    double _dispofftime;

    /* pc9801:diremono sets up m_mode to be specifically in character mode, wanting x8 here */
    dev->h_mult = ((dev->mode & UPD7220_MODE_DISPLAY_MASK) == UPD7220_MODE_DISPLAY_GRAPHICS) ? 16 : 8;
    dev->v_mult = ((dev->mode & UPD7220_MODE_INTERLACE_MASK) == UPD7220_MODE_INTERLACE_ON) ? 2 : 1;

    dev->h_pix_total = (dev->hs + dev->hbp + dev->hfp + dev->aw) * dev->h_mult;
    dev->v_pix_total = (dev->vs + dev->vbp + dev->al + dev->vfp) * dev->v_mult;

    disptime    = (double) (dev->h_pix_total + 1);
    _dispontime = (double) (dev->aw * dev->h_mult);
    _dispofftime     = disptime - _dispontime;
    _dispontime      = _dispontime * clock();
    _dispofftime     = _dispofftime * clock();
    dev->dispontime  = (uint64_t) (int64_t) (_dispontime);
    dev->dispofftime = (uint64_t) (int64_t) (_dispofftime);
}

static __inline void
upd7220_reset_figs_param(upd7220_t *dev)
{
    dev->figs.dc = 0x0000;
    dev->figs.d = 0x0008;
    dev->figs.d1 = 0xffff;
    dev->figs.d2 = 0x0008;
    dev->figs.dm = 0xffff;
    dev->figs.gd = 0;
    dev->figs.figure_type = 0;
    dev->pattern = (dev->ra[8] | (dev->ra[9] << 8));
}

static __inline void
upd7220_write_vram(upd7220_t *dev, uint8_t type, uint8_t mod, uint16_t data, uint16_t mask)
{
    uint16_t current = upd7220_read_word(dev, dev->ead);

    switch (mod & 3) {
        case 0x00: /*replace*/
            switch (type) {
                case 0:
                    current = (current & ~mask) | (data & mask);
                    break;
                case 2:
                    current = (current & ~(mask & 0x00ff)) | (data & 0x00ff);
                    break;
                case 3:
                    current = (current & ~(mask & 0xff00)) | (data & 0xff00);
                    break;
                default:
                    break;
            }
            break;
        case 0x01: /*complement*/
            switch (type) {
                case 0:
                    current = current ^ (data & mask);
                    break;
                case 2:
                    current = current ^ (data & (mask & 0x00ff));
                    break;
                case 3:
                    current = current ^ (data & (mask & 0xff00));
                    break;
                default:
                    break;
            }
            break;
        case 0x02: /*reset to zero*/
            switch (type) {
                case 0:
                    current = current & ~(data & mask);
                    break;
                case 2:
                    current = current & ~(data & (mask & 0x00ff));
                    break;
                case 3:
                    current = current & ~(data & (mask & 0xff00));
                    break;
                default:
                    break;
            }
            break;
        case 0x03: /*set to one*/
            switch (type) {
                case 0:
                    current = current | (data & mask);
                    break;
                case 2:
                    current = current | (data & (mask & 0x00ff));
                    break;
                case 3:
                    current = current | (data & (mask & 0xff00));
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    upd7220_write_word(dev, dev->ead, current);
}

static __inline void
upd7220_wdat(upd7220_t *dev, uint8_t type, uint8_t mod)
{
    if (type == 1) {
        pclog("uPD7220 invalid type 1 WDAT parameter\n");
        return;
    }

    uint16_t result = dev->pr[1] | (dev->pr[2] << 8);

    switch (type) {
        case 0:
            result &= dev->mask;
            break;
        case 2:
            result &= (dev->mask & 0x00ff);
            break;
        case 3:
            result <<= 8;
            result &= (dev->mask & 0xff00);
            break;
        default:
            break;
    }

    for (int i = 0; i < (dev->figs.dc + 1); i++) {
        upd7220_write_vram(dev, type, mod, result, 0xffff);
        dev->ead += x_dir[dev->figs.dir] + (y_dir[dev->figs.dir] * upd7220_get_pitch(dev));
        dev->ead &= 0x3ffff;
    }
}

static __inline void
upd7220_get_text_partition(upd7220_t *dev, int index, uint32_t *sad, uint16_t *len, int *im, int *wd)
{
    *sad = ((dev->ra[(index << 2) + 1] & 0x1f) << 8) | dev->ra[index << 2];
    *len = ((dev->ra[(index << 2) + 3] & 0x3f) << 4) | (dev->ra[(index << 2) + 2] >> 4);
    *im = !!(dev->ra[(index << 2) + 3] & 0x40);
    *wd = !!(dev->ra[(index << 2) + 3] & 0x80);
}

static __inline void
upd7220_get_graphics_partition(upd7220_t *dev, int index, uint32_t *sad, uint16_t *len, int *im, int *wd)
{
    *sad = ((dev->ra[(index << 2) + 2] & 0x03) << 16) | (dev->ra[(index << 2) + 1] << 8) | dev->ra[index << 2];
    *len = ((dev->ra[(index << 2) + 3] & 0x3f) << 4) | (dev->ra[(index << 2) + 2] >> 4);
    *im = !!(dev->ra[(index << 2) + 3] & 0x40);
    *wd = !!(dev->ra[(index << 2) + 3] & 0x80);
}

static __inline uint16_t
upd7220_read_vram(upd7220_t *dev)
{
    const uint16_t data = upd7220_read_word(dev, dev->ead);

    dev->ead += x_dir[dev->figs.dir] + (y_dir[dev->figs.dir] * upd7220_get_pitch(dev));
    dev->ead &= 0x3ffff;

    return data;
}

static __inline void
upd7220_rdat(upd7220_t *dev, uint8_t type, uint8_t mod)
{
    if (type == 1) {
        pclog("uPD7220 invalid type 1 RDAT parameter\n");
        return;
    }

    if (mod)
        pclog("uPD7220 RDAT used with mod = %02x?\n", mod);

    while (dev->figs.dc && (dev->fifo_ptr < (type ? 0x0f : 0x0e))) {
        const uint16_t data = upd7220_read_vram(dev);

        switch (type) {
            case 0:
                upd7220_queue(dev, data & 0xff, 0);
                upd7220_queue(dev, (data >> 8) & 0xff, 0);
                break;
            case 2:
                upd7220_queue(dev, data & 0xff, 0);
                break;
            case 3:
                upd7220_queue(dev, (data >> 8) & 0xff, 0);
                break;
            default:
                break;
        }
        dev->figs.dc--;
    }

    if (!dev->figs.dc)
        upd7220_reset_figs_param(dev);
}

static void
upd7220_draw_line(upd7220_t *dev)
{
    int d = (dev->figs.d & 0x2000) ? (int16_t)(dev->figs.d | 0xe000) : dev->figs.d;
	int d1 = (dev->figs.d1 & 0x2000) ? (int16_t)(dev->figs.d1 | 0xe000) : dev->figs.d1;
	int d2 = (dev->figs.d2 & 0x2000) ? (int16_t)(dev->figs.d2 | 0xe000) : dev->figs.d2;
	const uint8_t octant = dev->figs.dir;

	pclog("draw_line: EAD=%08x, MASK=%04x, MOD=%02x, FIGS DIR=%02x, DC=%d, D=%d, D1=%d, D2=%d\n", dev->ead, dev->mask, dev->bitmap_mod, dev->figs.dir, dev->figs.dc, d, d1, d2);

	for (int i = 0; i <= dev->figs.dc; ++i) {
        const uint16_t pattern = upd7220_get_pattern(dev, i & 0x0f);

        upd7220_write_vram(dev, 0, dev->bitmap_mod, pattern, dev->mask);
        if (octant & 1)
            dev->figs.dir = (d < 0) ? ((octant + 1) & 7) : octant;
        else
            dev->figs.dir = (d < 0) ? octant : ((octant + 1) & 7);

        d += ((d < 0) ? d1 : d2);
        upd7220_next_pixel(dev, dev->figs.dir);
	}
}

static void
upd7220_draw_arc(upd7220_t *dev)
{
    int err = -dev->figs.d;
    int d = dev->figs.d + 1;
    const uint16_t octant = dev->figs.dir;

    pclog("draw_arc: EAD=%08x, MASK=%04x, MOD=%02x - FIGS DIR=%02x, DC=%d, D=%d, D2=%d, DM=%d\n", dev->ead, dev->mask, dev->bitmap_mod, dev->figs.dir, dev->figs.dc, dev->figs.d, dev->figs.d2, dev->figs.dm);

    for (int i = 0; i <= dev->figs.dc; ++i) {
        const uint16_t pattern = upd7220_get_pattern(dev, i % 0x0f)
        if (i >= dev->figs.dm)
            upd7220_write_vram(dev, 0, dev->bitmap_mod, pattern, dev->mask);

        if (err < 0)
            dev->figs.dir = (octant & 1) ? ((octant + 1) & 7) : octant;
        else
            dev->figs.dir = (octant & 1) ? octant : ((octant + 1) & 7);

        err += (err < 0) ? ((i + 1) << 1) : ((i - --d + 1) << 1);
        upd7220_next_pixel(dev, dev->figs.dir);
    }
}

static void
upd7220_draw_rectangle(upd7220_t *dev)
{
    if (dev->figs.dc == 3)
        return;

    pclog("draw_rectangle: EAD=%08x, MASK=%04x, MOD=%02x - FIGS DIR=%02x, DC=%d, D=%d, D2=%d\n", dev->ead, dev->mask, dev->bitmap_mod, dev->figs.dir, dev->figs.dc, dev->figs.d, dev->figs.d2);

    for (int i = 0; i <= dev->figs.dc; ++i) {
        const uint16_t dist = ((i & 1) ? dev->figs.d2 : dev->figs.d);
        for (int j = 0; j < dist; ++j) {
            const uint16_t pattern = upd7220_get_pattern(dev, j & 0x0f);

            upd7220_write_vram(dev, 0, dev->bitmap_mod, pattern, dev->mask);
            if ((i > 0) && !j)
                dev->figs.dir = (dev->figs.dir + 2) & 7;

            upd7220_next_pixel(dev, dev->figs.dir);
        }
    }
}

static void
upd7220_draw_char(upd7220_t *dev)
{
	const int8_t dir_change[2][4] = {
		{2, 2, -2, -2},
		{1, 3, -3, -1}
	};
    const uint8_t type = (dev->figs.figure_type & 0x10) >> 4;

    pclog("draw_char: EAD=%08x, MASK=%04x, MOD=%02x, DIR=%02x, D=%d, DC=%d, FIGURE=%02x.\n", dev->ead, dev->mask, dev->bitmap_mod, dev->figs.dir, dev->figs.d, dev->figs.dc, dev->figs.figure_type);

    for (int i = 0, di = 0; i < (dev->figs.dc + 1); ++i) {
        dev->pattern = (dev->ra[0x0f - (i & 0x07)] << 8) | dev->ra[0x0f - (i & 0x07)];
        for (int zdc = 0; zdc <= dev->gchr; ++zdc, ++di) {
            for (int j = 0; j < dev->figs.d; ++j) {
                const uint16_t pattern (di % 2) ? upd7220_get_pattern(dev, 0x0f - (j & 0x0f)) : upd7220_get_pattern(dev, j & 0x0f);
                for (int zd = 0; zd <= dev->gchr; ++zd) {
                    upd7220_write_vram(dev, 0, dev->bitmap_mod, pattern, dev->mask);
                    if ((j != (dev->figs.d - 1)) || (zd != dev->gchr))
                        upd7220_next_pixel(dev, dev->figs.dir);
                }
            }
            dev->figs.dir = (((uint16_t)dev->figs.dir + dir_change[type][(di % 2) << 1]) & 7);
            upd7220_next_pixel(dev, dev->figs.dir);
            dev->figs.dir = (((uint16_t)dev->figs.dir + dir_change[type][((di % 2) << 1) + 1]) & 7);
        }
    }
}

static uint16_t
upd7220_rotate_rotate(uint16_t value)
{
    uint16_t val = (value >> 1) | (value << ((-1) & 0x0f));

    return val;
}

static uint16_t
upd7220_rotate_left(uint16_t value)
{
    uint16_t val = (value << 1) | (value >> ((-1) & 0x0f));

    return val;
}

static uint16_t
upd7220_get_pitch(upd7220_t *dev)
{
    bool mixed = ((dev->mode & UPD7220_MODE_DISPLAY_MASK) == UPD7220_MODE_DISPLAY_MIXED);
    uint16_t pitch;

    if (mixed)
        pitch = dev->pitch >> dev->figs.gd;
    else
        pitch = dev->pitch;

    return pitch;
}

static uint16_t
upd7220_get_pattern(upd7220_t *dev, uint8_t cycle)
{
    bool mixed = ((dev->mode & UPD7220_MODE_DISPLAY_MASK) == UPD7220_MODE_DISPLAY_MIXED);
    bool graphics = ((dev->mode & UPD7220_MODE_DISPLAY_MASK) == UPD7220_MODE_DISPLAY_GRAPHICS);
    uint16_t pattern;

    if ((mixed && dev->figs.gd) || graphics)
        pattern = ((dev->pattern >> (cycle & 0x0f) & 1)) * 0xffff;
    else
        pattern = dev->pattern;

    return pattern;
}

static void
upd7220_next_pixel(upd7220_t *dev, int direction)
{
    switch (direction & 7) {
        case 0:
            dev->ead += upd7220_get_pitch(dev);
            break;
        case 1:
            dev->ead += upd7220_get_pitch(dev);
            if (dev->mask & 0x8000)
                dev->ead++;

            dev->mask = upd7220_rotate_left(dev->mask);
            break;
        case 2:
            if (dev->mask & 0x8000)
                dev->ead++;

            dev->mask = upd7220_rotate_left(dev->mask);
            break;
        case 3:
            dev->ead -= upd7220_get_pitch(dev);
            if (dev->mask & 0x8000)
                dev->ead++;

            dev->mask = upd7220_rotate_left(dev->mask);
            break;
        case 4:
            dev->ead -= upd7220_get_pitch(dev);
            break;
        case 5:
            dev->ead -= upd7220_get_pitch(dev);
            if (dev->mask & 0x0001)
                dev->ead--;

            dev->mask = upd7220_rotate_right(dev->mask);
            break;
        case 6:
            if (dev->mask & 0x0001)
                dev->ead--;

            dev->mask = upd7220_rotate_right(dev->mask);
            break;
        case 7:
            dev->ead += upd7220_get_pitch(dev);
            if (dev->mask & 0x0001)
                dev->ead--;

            dev->mask = upd7220_rotate_right(dev->mask);
            break;
        default:
            break;
    }
    dev->ead &= 0x3ffff;
}

static int
upd7220_translate_command(uint8_t data)
{
	int command = COMMAND_INVALID;

	switch (data) {
        case UPD7220_COMMAND_RESET:
            command = COMMAND_RESET;
            break;
        case UPD7220_COMMAND_CCHAR:
            command = COMMAND_CCHAR;
            break;
        case UPD7220_COMMAND_START:
            command = COMMAND_START;
            break;
        case UPD7220_COMMAND_ZOOM:
            command = COMMAND_ZOOM;
            break;
        case UPD7220_COMMAND_CURS:
            command = COMMAND_CURS;
            break;
        case UPD7220_COMMAND_PITCH:
            command = COMMAND_PITCH;
            break;
        case UPD7220_COMMAND_MASK:
            command = COMMAND_MASK;
            break;
        case UPD7220_COMMAND_FIGS:
            command = COMMAND_FIGS;
            break;
        case UPD7220_COMMAND_FIGD:
            command = COMMAND_FIGD;
            break;
        case UPD7220_COMMAND_GCHRD:
            command = COMMAND_GCHRD;
            break;
        case UPD7220_COMMAND_CURD:
            command = COMMAND_CURD;
            break;
        case UPD7220_COMMAND_LPRD:
            command = COMMAND_LPRD;
            break;
        case UPD7220_COMMAND_5A:
            command = COMMAND_5A;
            break;
        case UPD7220_COMMAND_RESET2:
            command = COMMAND_RESET2:
            break;
        case UPD7220_COMMAND_RESET3:
            command = COMMAND_RESET3:
            break;
        case UPD7220_COMMAND_BLANK2:
            command = COMMAND_BLANK2:
            break;
        default:
            switch (data & 0xfe) {
                case UPD7220_COMMAND_SYNC:
                    command = COMMAND_SYNC;
                    break;
                case UPD7220_COMMAND_VSYNC:
                    command = COMMAND_VSYNC;
                    break;
                case UPD7220_COMMAND_BLANK:
                    command = COMMAND_BLANK;
                    break;
                default:
                    switch (data & 0xf0) {
                        case UPD7220_COMMAND_PRAM:
                            command = COMMAND_PRAM;
                            break;
                        default:
                            switch (data & 0xe4) {
                                case UPD7220_COMMAND_WDAT:
                                    command = COMMAND_WDAT;
                                    break;
                                case UPD7220_COMMAND_RDAT:
                                    command = COMMAND_RDAT;
                                    break;
                                case UPD7220_COMMAND_DMAR:
                                    command = COMMAND_DMAR;
                                    break;
                                case UPD7220_COMMAND_DMAW:
                                    command = COMMAND_DMAW;
                                    break;
                                default:
                                    break;
                            }
                            break;
                    }
                    break;
            }
            break;
	}

	return command;
}

static void
upd7220_process_fifo(upd7220_t *dev)
{
    uint8_t data;
    int flag;
    int cr;

    upd7220_dequeue(dev, &data, &flag);

    if (flag == FIFO_COMMAND) {
        cr = upd7220_translate_command(data);
        if (cr != COMMAND_BLANK) { /* workaround for Rainbow 100 Windows 1.03, needs verification */
            dev->cr = data;
            dev->param_ptr = 1;
        }
    } else {
        cr = upd7220_translate_command(dev->cr);
        dev->pr[dev->param_ptr] = data;
        dev->param_ptr++;
    }

    switch (cr) {
        case COMMAND_INVALID:
            pclog("uPD7220 Invalid Command Byte %02x\n", dev->cr);
            break;
        case COMMAND_5A:
            if (dev->param_ptr == 4)
                pclog("uPD7220 Undocumented Command 0x5A Executed %02x %02x %02x\n", dev->pr[0], dev->pr[1], dev->pr[2]);
            break;
        case COMMAND_RESET: /* reset */
        case COMMAND_RESET2:
        case COMMAND_RESET3:
            switch (dev->param_ptr) {
                case 1:
                    pclog("Reset Command %02x\n", cr);
                    if (cr != COMMAND_RESET)
                        dev->de = 0;

                    dev->ead = 0;
                    /* TODO: very unlikely, should be 0xffff assuming it's even touched ... */
                    dev->mask = 0;
                    /* FIFO, Command Processor and internal counters are cleared by this
                       - pc9801rs BIOS starts up a DMAW command that spindiz2 will dislike during its boot sequences */
                    dev->sr &= ~UPD7220_SR_DRAWING_IN_PROGRESS;
                    upd7220_fifo_clear(dev);
                    //upd7220_stop_dma(dev);
                    break;
                case 9:
                    upd7220_calc_mode(dev);
                    break;
                default:
                    break;
            }
            break;
        case COMMAND_SYNC: /* sync format specify */
            if (flag == FIFO_COMMAND) {
                dev->de = dev->cr & 0x01;
                pclog("SYNC %u\n", dev->de);
            }
            if (dev->param_ptr == 9)
                upd7220_calc_mode(dev);

            break;
        case COMMAND_VSYNC: /* vertical sync mode */
            dev->m = dev->cr & 0x01;
            pclog("VSYNC M=%u\n", dev->m);

            dev->fullchange = changeframecount;
            upd7220_recalctimings(dev);
            break;
        case COMMAND_CCHAR: /* cursor & character characteristics */
            switch (dev->param_ptr) {
                case 2:
                    dev->lr = (dev->pr[1] & 0x1f) + 1;
                    dev->dc = !!(dev->pr[1] & 0x80);
                    pclog("CCHAR LR=%u, DC=%u\n", dev->lr, dev->dc);
                    break;
                case 3:
                    dev->ctop = dev->pr[2] & 0x1f;
                    dev->sc = !!(dev->pr[2] & 0x20);
                    dev->br = (dev->pr[2] >> 6); /* guess, assume that blink rate clears upper bits (if any) */
                    pclog("- CTOP=%u, SC=%u\n", dev->ctop, dev->sc);
                    break;
                case 4:
                    dev->br = ((dev->pr[3] & 0x07) << 2) | (dev->pr[2] >> 6);
                    dev->cbot = dev->pr[3] >> 3;
                    pclog("- BR=%u, CBOT=%u\n", dev->br, dev->cbot);
                    break;
                default:
                    break;
            }
            break;
        case COMMAND_START: /* start display & end idle mode */
            dev->de = 1;
            pclog("START\n");
            break;
        case COMMAND_BLANK2:
            dev->de = 0;
            pclog("BLANK2\n");
            break;
        case COMMAND_BLANK: /* display blanking control */
            dev->de = data & 0x01;
            break;
        case COMMAND_ZOOM: /* zoom factors specify */
            if (flag == FIFO_PARAMETER) {
                dev->ghcr = dev->pr[1] & 0x0f;
                dev->disp = dev->pr[1] >> 4;
                pclog("ZOOM GCHR=%01x, DISP=%01x\n", dev->ghcr, dev->disp);
            }
            break;
        case COMMAND_CURS: /* cursor position specify */
            if (dev->param_ptr >= 3) {
                uint8_t upper_addr = (dev->param_ptr == 3) ? 0 : (dev->pr[3] & 0x03);

                dev->ead = (upper_addr << 16) | (dev->pr[2] << 8) | dev->pr[1];
                pclog("CURS EAD: %06x\n", dev->ead);

                if (dev->param_ptr == 4) {
                    dev->mask = 1 << ((dev->pr[3] >> 4) & 0x0f);
                    pclog("- MASK: %04x\n", dev->mask);
                }
            }
            break;
        case COMMAND_PRAM: /* parameter RAM load */
            if (flag == FIFO_COMMAND)
                dev->ra_addr = dev->cr & 0x0f;
            else {
                if (dev->ra_addr < 16) {
                    pclog("PRAM RA%u: %02x\n", dev->ra_addr, data);
                    switch (dev->ra_addr) {
                        case 8:
                            dev->pattern = (dev->pattern & 0xff00) | data;
                            break;
                        case 9:
                            dev->pattern = (dev->pattern & 0x00ff) | (data << 8);
                            break;
                        default:
                            break;
                    }
                    dev->ra[dev->ra_addr] = data;
                    dev->ra_addr++;
                }
                dev->param_ptr = 0;
            }
            break;
        case COMMAND_PITCH: /* pitch specification */
            /* pc9801:burai writes a spurious extra value during intro, effectively ignored
               (only first value matters) */
            if ((flag == FIFO_PARAMETER) && (dev->param_ptr == 2)) {
                dev->pitch = (dev->pitch & 0x100) | data;
                if (dev->pitch < 2) {
                    dev->pitch = 2;
                }
                pclog("PITCH %u\n", dev->pitch);
            }
            break;
        case COMMAND_WDAT: /* write data into display memory */
            dev->bitmap_mod = dev->cr & 3;
            if ((dev->param_ptr == 3) || ((dev->param_ptr == 2) && (dev->cr & 0x10))) {
                dev->pattern = (dev->pattern & 0xff00) | dev->pr[1];
                if (dev->param_ptr == 3)
                    dev->pattern = (dev->pattern & 0x00ff) | (dev->pr[2] << 8);

                pclog("WDAT PATTERN=%04x\n", dev->pattern);
                if (dev->figs.figure_type)
                    break;
                pclog("- CR=%02x (%02x %02x) (%c) EAD=%06x - FIGS DC=%04x\n", dev->cr, dev->pr[2], dev->pr[1], dev->pr[1] ? dev->pr[1] : ' ', dev->ead, dev->figs.dc);
                upd7220_set_direction(dev, FIFO_WRITE);
                upd7220_wdat(dev, (dev->cr & 0x18) >> 3, dev->cr & 3);
                upd7220_reset_figs_param(dev);
                dev->param_ptr = 1;
            }
            break;
        case COMMAND_MASK: /* mask register load */
            if (dev->param_ptr == 3) {
                dev->mask = (dev->pr[2] << 8) | dev->pr[1];
                pclog("MASK %04x\n", dev->mask);
            }
            break;
        case COMMAND_FIGS: /* figure drawing parameters specify */
            switch (dev->param_ptr) {
                case 2:
                    dev->figs.dir = dev->pr[1] & 0x07;
                    dev->figs.figure_type (dev->pr[1] & 0xf8) >> 3;
                    pclog("FIGS DIR=%02x, FIGURE=%02x\n", dev->figs.dir, dev->figs.figure_type);
                    break;
                /* the Decision Mate V during start-up test upload only 2 params before execute the
                   RDAT command, so I assume this is the expected behaviour, but this needs to be verified. */
                case 3:
                    dev->figs.dc = (dev->pr[2] | (dev->figs.dc & 0x3f00));
                    pclog("- DC=%04x (*)\n", dev->figs.dc);
                    break;
                case 4:
                    dev->figs.dc = (dev->pr[2] | ((dev->pr[3] & 0x3f) << 8));
                    dev->figs.gd = ((dev->pr[3] & 0x40) && ((dev->mode & UPD7220_MODE_DISPLAY_MASK) == UPD7220_MODE_DISPLAY_MIXED));
                    pclog("- DC=%04x\n", dev->figs.dc);
                    pclog("- GD=%02x\n", dev->figs.gd);
                    break;
                case 6:
                    dev->figs.d = (dev->pr[4] | ((dev->pr[5] & 0x3f) << 8));
                    pclog("- D=%04x\n", dev->figs.d);
                    break;
                case 8:
                    dev->figs.d2 = (dev->pr[6] | ((dev->pr[7] & 0x3f) << 8));
                    pclog("- D2=%04x\n", dev->figs.d2);
                    break;
                case 10:
                    dev->figs.d1 = (dev->pr[8] | ((dev->pr[9] & 0x3f) << 8));
                    pclog("- D1=%04x\n", dev->figs.d1);
                    break;
                case 12:
                    dev->figs.d_m = (dev->pr[10] | ((dev->pr[11] & 0x3f) << 8));
                    pclog("- DM=%04x\n", dev->figs.d_m);
                    break;
                default:
                    break;
            }
            break;
        case COMMAND_FIGD: /* figure draw start */
            switch (dev->figs.figure_type) {
                case 0:
                    upd7220_draw_pixel(dev);
                    break;
                case 1:
                    upd7220_draw_line(dev);
                    break;
                case 4:
                    upd7220_draw_arc(dev);
                    break;
                case 8:
                    upd7220_draw_rectangle(dev);
                    break;
                default:
                    pclog("Unimplemented command FIGD =%02x\n", dev->figs.figure_type);
                    break;
            }
            upd7220_reset_figs_param(dev);
            dev->sr |= UPD7220_SR_DRAWING_IN_PROGRESS;
            break;
        case COMMAND_GCHRD: /* graphics character draw and area filling start */
            pclog("GCHRD %u\n", dev->figs.figure_type);
            if ((dev->figs.figure_type & 0x0f) == 2)
                upd7220_draw_char(dev);
            else
                pclog("Unimplemented command GCHRD =%02x\n", dev->figs.figure_type);

            upd7220_reset_figs_param(dev);
            dev->sr |= UPD7220_SR_DRAWING_IN_PROGRESS;
            break;
        case COMMAND_RDAT: /* read data from display memory */
            pclog("RDAT %06x %02x\n", dev->ead, dev->cr);
            upd7220_set_direction(dev, FIFO_READ);
            upd7220_rdat(dev, ((dev->cr & 0x18) >> 3), dev->cr & 3);
            dev->sr |= UPD7220_SR_DATA_READY;
            break;
        case COMMAND_CURD: /* cursor address read */
            pclog("CURD %06x %04x\n", dev->ead, dev->mask);
            upd7220_set_direction(dev, FIFO_READ);

            upd7220_queue(dev, dev->ead & 0xff, 0);
            upd7220_queue(dev, (dev->ead >> 8) & 0xff, 0);
            upd7220_queue(dev, dev->ead >> 16, 0);
            upd7220_queue(dev, dev->mask & 0xff, 0);
            upd7220_queue(dev, dev->mask >> 8, 0);
            dev->sr |= UPD7220_SR_DATA_READY;
            break;
        case COMMAND_LPRD: /* light pen address read */
            pclog("LPRD %06x\n", dev->lad);
            upd7220_set_direction(dev, FIFO_READ);

            upd7220_queue(dev, dev->lad & 0xff, 0);
            upd7220_queue(dev, (dev->lad >> 8) & 0xff, 0);
            upd7220_queue(dev, dev->lad >> 16, 0);
            dev->sr |= UPD7220_SR_DATA_READY;
            dev->sr &= ~UPD7220_SR_LIGHT_PEN_DETECT;
            break;
        case COMMAND_DMAR: /* DMA read request */
            pclog("DMAR CR=%02x, DC=%d, D=%d\n", dev->cr, dev->figs.dc, dev->figs.d);
            dev->dma_type = (dev->cr >> 3) & 3;
            dev->dma_mod = dev->cr & 3;
            dev->dma_transfer_length = (dev->figs.dc + 1) * (dev->figs.d + 2);
            //upd7220_start_dma(dev);
            break;
        case COMMAND_DMAW: /* DMA write request */
            pclog("DMAW CR=%02x, DC=%d, D=%d\n", dev->cr, dev->figs.dc, dev->figs.d);
            dev->dma_type = (dev->cr >> 3) & 3;
            dev->dma_mod = dev->cr & 3;
            dev->dma_transfer_length = (dev->figs.dc + 1) * (dev->figs.d + 1);
            //upd7220_start_dma(dev);
            break;
        default:
            break;
    }
}

static void
upd7220_continue_command(upd7220_t *dev)
{
    /* continue RDAT command when data to read are larger than the FIFO (a5105 and dmv text scrolling) */
    if (dev->figs.dc && (upd7220_translate_command(dev->cr) == COMMAND_RDAT)) {
        upd7220_rdat(dev, ((dev->cr & 0x18) >> 3), dev->cr & 3);
        dev->sr |= UPD7220_SR_DATA_READY;
    }
}

void
upd7220_out(uint16_t addr, uint8_t val, void *priv)
{
    upd7220_t *dev = (upd7220_t *) priv;

    if (addr & 0x01) {
        /* command into FIFO */
        upd7220_set_direction(dev, FIFO_WRITE);
        upd7220_queue(dev, val, 1);
    } else {
        /* parameter into FIFO */
        upd7220_queue(dev, val, 0);
    }
    upd7220_process_fifo(dev);
}

uint8_t
upd7220_in(uint16_t addr, void *priv)
{
    upd7220_t *dev = (upd7220_t *) priv;
    uint8_t data;
    int flag;

    if (addr & 0x01) {
        /* FIFO read */
        upd7220_set_direction(dev, FIFO_READ);
        upd7220_dequeue(dev, &data, &flag);

        upd7220_continue_command(dev);
    } else {
        /* status register */
        data = dev->sr;

		/* TODO: timing of these */
		dev->sr &= ~UPD7220_SR_DRAWING_IN_PROGRESS;
    }

    return data;
}

static void
upd7220_update_text(upd7220_t *dev)
{
    uint32_t sad;
    uint32_t len;
    int im;
    int wd;
    int sy = 0;

    for (int area = 0; area < 4; area++) {
        upd7220_get_text_partition(dev, area, &sad, &len, &im, &wd);

        int y;
        for (y = sy; y < (sy + len); y++) {
            uint32_t const addr = sad + (y * dev->pitch);
            dev->draw_text_cb(dev, addr, (y * dev->lr) + dev->vbp, wd, dev->pitch, dev->lr, dev->dc, dev->ead, dev->ctop, dev->cbot);
        }
        sy = y + 1;
    }
}

static void
upd7220_draw_graphics_line(upd7220_t *dev, uint32_t addr, int y, int wd, int mixed)
{
    int al = ysize;
    int aw = dev->aw >> mixed;
    int pitch = dev->pitch >> mixed;

    for (int sx = 0; sx < aw; sx++) {
        if ((sx < dev->aw) && (y < al))
            dev->display_cb(dev, y, sx << 4, addr + (wd + 1) * (sx % pitch));
    }
}

static void
upd7220_update_graphics(upd7220_t *dev, int force_bitmap)
{
    uint32_t sad;
    uint16_t len;
    int im;
    int wd;
    int y = 0;
    int tsy = 0;
    int bsy = 0;
    int mixed = ((dev->mode & UPD7220_MODE_DISPLAY_MASK) == UPD7220_MODE_DISPLAY_MIXED) ? 1 : 0;
    uint8_t interlace = ((dev->mode & UPD7220_MODE_INTERLACE_MASK) == UPD7220_MODE_INTERLACE_ON) ? 1 : 0;
    uint8_t zoom = dev->disp + 1;

    for (int area = 0; area < 4; area++) {
        upd7220_get_graphics_partition(dev, area, &sad, &len, &im, &wd);

        pclog("update_graphics: AREA=%d, BSY=%4d, SAD=%06x, len=%04x, im=%d, wd=%d\n", area, bsy, sad, len, im, wd);

		/* pc9821:aitd (256 color mode) and pc9821:os2warp3 (16, installation screens) wants this shift */
		const uint8_t pitch_shift = force_bitmap ? im : mixed;

		if (im || force_bitmap) {
			/*  according to documentation only areas 0-1-2 can be drawn in bitmap mode
                - pc98:quarth definitely needs area 2 for player section.
                - pc98:steamhea wants area 3 for scrolling and dialogue screens to work together,
			     contradicting the doc. Fixed in 7220A or applies just for mixed mode?
                TODO: what happens if combined area size is smaller than display height?
                documentation suggests that it should repeat from area 0, needs real HW verification (no known SW do it). */
            if ((area >= 3) && !force_bitmap)
                break;

			/*  pc98:madoum1-3 sets up ALL areas to a length of 0 after initial intro screen.
                madoum1: area 0 sad==0 on gameplay (PC=0x955e7), sad==0xaa0 on second intro screen (tower) then intentionally scrolls up and back to initial position.
                Suggests that length should be treated as max size if this occurs, this is also proven to be correct via real HW verification.
                TODO: check if text partition does the same. */
            if (len == 0)
                len = 0x400;

            if (interlace)
                len <<= 1;

            for (y = 0; y < len; y++) {
                /*  TODO: not completely correct, all is drawn half size with real HW tests on pc98 msdos by just changing PRAM values.
                    pc98 quarth doesn't seem to use pitch here and it definitely wants bsy to be /2 to make scrolling to work.
                    pc98 xevious wants the pitch to be fixed at 80, and wants bsy to be /1
                    pc98 dbuster contradicts with Xevious with regards of the pitch tho ... */
                uint32_t const addr = (sad & 0x3ffff) + ((y / (mixed ? 1 : dev->lr)) * (dev->pitch >> pitch_shift));
                for (int z = 0; z <= dev->disp; ++z) {
                    int yval = (y * zoom) + z + bsy + dev->vbp;
					/*  pc9801:duel sets up bitmap layer with height 384 vs. 400 of text layer
                        so we scissor here, interlace wants it bumped x2 (microbx2) */
                    if ((yval >= dev->y_add) && (yval <= (overscan_y - dev->y_add)) && (yval - dev->vbp) < (dev->al << interlace))
                        upd7220_draw_graphics_line(dev, addr, yval, wd, pitch_shift);
                }
            }
		} else {
            if (dev->lr) {
                for (y = 0; y < len; y += dev->lr) {
                    uint32_t const addr = (sad & 0x3ffff) + ((y / dev->lr) * dev->pitch);
                    int yval = (y * zoom) + tsy + dev->vbp;
                    dev->draw_text_cb(dev, addr, yval, wd, dev->pitch, dev->lr, dev->dc, dev->ead, dev->ctop, dev->cbot);
                }
            }
		}
        if (dev->lr)
            tsy += (y * zoom);

        bsy += (y * zoom);
    }
}

void
upd7220_poll(void *priv)
{
    upd7220_t *dev = (upd7220_t *) priv;

    if (!dev->linepos) { /*Render the line*/
        timer_advance_u64(&dev->timer, dev->dispofftime);

        dev->sr &= ~UPD7220_SR_HBLANK_ACTIVE;
        dev->linepos = 1;
        if (dev->dispon) {
            dev->hdisp_on = 1;

            if (dev->firstline == 2000) {
                dev->firstline = dev->displine;
                video_wait_for_buffer();
            }

            if (dev->de) {
                switch (dev->mode & UPD7220_MODE_DISPLAY_MASK) {
                    case UPD7220_MODE_DISPLAY_MIXED:
                        upd7220_update_graphics(dev, 0);
                        break;
                    case UPD7220_MODE_DISPLAY_GRAPHICS:
                        upd7220_update_graphics(dev, 1);
                        break;
                    case UPD7220_MODE_DISPLAY_CHARACTER:
                        upd7220_update_text(dev);
                        break;
                    default:
                        break;
                }
            }

            if (dev->lastline < dev->displine)
                dev->lastline = dev->displine;

            dev->displine++;
            if ((dev->sr & UPD7220_SR_VSYNC_ACTIVE) && ((dev->displine & 0x0f) == ((dev->vbp + dev->al + dev->vfp) & 0x0f)) && dev->vslines) {
                dev->sr &= ~UPD7220_SR_VSYNC_ACTIVE;
            }
            dev->vslines++;
            if (dev->displine > 2000)
                dev->displine = 0;
        }
    } else { /*HSync */
        timer_advance_u64(&dev->timer, dev->dispontime);

        if (dev->dispon)
            dev->sr |= UPD7220_SR_HBLANK_ACTIVE;

        dev->hdisp_on = 0;
        dev->linepos = 0;

        dev->vc++;
        dev->vc &= 0x7ff;
    }
}

static void
vid_speed_changed(void *priv)
{
    tivga_t *ti = (tivga_t *) priv;

    svga_recalctimings(&ti->svga);
}

static void
vid_force_redraw(void *priv)
{
    tivga_t *ti = (tivga_t *) priv;

    ti->svga.fullchange = changeframecount;
}

static void
vid_close(void *priv)
{
    tivga_t *ti = (tivga_t *) priv;

    svga_close(&ti->svga);

    free(ti);
}

static void *
vid_init(const device_t *info)
{
    tivga_t *ti;

    /* Allocate control block and initialize. */
    ti = (tivga_t *) malloc(sizeof(tivga_t));
    memset(ti, 0x00, sizeof(tivga_t));

    /* Set amount of VRAM in KB. */
    if (info->local == 0)
        ti->vram_size = device_get_config_int("vram_size");
    else
        ti->vram_size = info->local;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ti_cf62011);

    svga_init(info, &ti->svga, ti,
              ti->vram_size << 10,
              NULL, vid_in, vid_out, NULL, NULL);

    io_sethandler(0x0100, 2, vid_in, NULL, NULL, NULL, NULL, NULL, ti);
    io_sethandler(0x03c0, 32, vid_in, NULL, NULL, vid_out, NULL, NULL, ti);
    io_sethandler(0x2100, 16, vid_in, NULL, NULL, vid_out, NULL, NULL, ti);

    ti->svga.bpp     = 8;
    ti->svga.miscout = 1;

    return ti;
}

const device_t ibm_ps1_2121_device = {
    .name          = "IBM PS/1 Model 2121 SVGA",
    .internal_name = "ibm_ps1_2121",
    .flags         = DEVICE_ISA,
    .local         = 512,
    .init          = vid_init,
    .close         = vid_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = vid_speed_changed,
    .force_redraw  = vid_force_redraw,
    .config        = NULL
};
