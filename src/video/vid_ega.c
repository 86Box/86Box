/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the EGA and Chips & Technologies SuperEGA
 *          graphics cards.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/video.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/vid_ega.h>

void ega_doblit(int wx, int wy, ega_t *ega);

#define BIOS_IBM_PATH    "roms/video/ega/ibm_6277356_ega_card_u44_27128.bin"
#define BIOS_CPQ_PATH    "roms/video/ega/108281-001.bin"
#define BIOS_SEGA_PATH   "roms/video/ega/lega.vbi"
#define BIOS_ATIEGA800P_PATH "roms/video/ega/ATI EGA Wonder 800+ N1.00.BIN"
#define BIOS_ISKRA_PATH  "roms/video/ega/143-02.bin", "roms/video/ega/143-03.bin"
#define BIOS_TSENG_PATH  "roms/video/ega/EGA ET2000.BIN"

enum {
    EGA_IBM = 0,
    EGA_COMPAQ,
    EGA_SUPEREGA,
    EGA_ATI800P,
    EGA_ISKRA,
    EGA_TSENG
};

enum {
    EGA_TYPE_IBM    = 0,
    EGA_TYPE_OTHER  = 1,
    EGA_TYPE_COMPAQ = 2
};

static video_timings_t timing_ega = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };
static uint8_t         ega_rotate[8][256];
static int             active             = 0;
static uint32_t        pallook16[256];
static uint32_t        pallook64[256];
static int             ega_type           = EGA_TYPE_IBM;
static int             old_overscan_color = 0;

/* 3C2 controls default mode on EGA. On VGA, it determines monitor type (mono or colour):
    7=CGA mode (200 lines), 9=EGA mode (350 lines), 8=EGA mode (200 lines). */
int egaswitchread;
int egaswitches     = 9;
int update_overscan = 0;

uint8_t ega_in(uint16_t addr, void *priv);

void
ega_out(uint16_t addr, uint8_t val, void *priv)
{
    ega_t  *ega = (ega_t *) priv;
    uint8_t o;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            ega->index = val;
            break;
        case 0x1cf:
            ega->regs[ega->index] = val;
            switch (ega->index) {
                case 0xb0:
                    ega_recalctimings(ega);
                    break;
                case 0xb3:
                    ati_eeprom_write((ati_eeprom_t *) ega->eeprom, val & 8, val & 2, val & 1);
                    break;

                default:
                    break;
            }
            break;

        case 0x3c0:
        case 0x3c1:
            if (!ega->attrff) {
                ega->attraddr = val & 31;
                if ((val & 0x20) != ega->attr_palette_enable) {
                    ega->fullchange          = 3;
                    ega->attr_palette_enable = val & 0x20;
                    ega_recalctimings(ega);
                }
            } else {
                if ((ega->attraddr == 0x13) && (ega->attrregs[0x13] != val))
                    ega->fullchange = changeframecount;
                o                                 = ega->attrregs[ega->attraddr & 31];
                ega->attrregs[ega->attraddr & 31] = val;
                if (ega->attraddr < 16)
                    ega->fullchange = changeframecount;
                if (ega->attraddr == 0x10 || ega->attraddr == 0x14 || ega->attraddr < 0x10) {
                    for (uint8_t c = 0; c < 16; c++) {
                        if (ega->attrregs[0x10] & 0x80)
                            ega->egapal[c] = (ega->attrregs[c] & 0xf) | ((ega->attrregs[0x14] & 0xf) << 4);
                        else
                            ega->egapal[c] = (ega->attrregs[c] & 0x3f) | ((ega->attrregs[0x14] & 0xc) << 4);
                    }
                    ega->fullchange = changeframecount;
                }
                /* Recalculate timings on change of attribute register 0x11
                   (overscan border color) too. */
                if (ega->attraddr == 0x10) {
                    if (o != val)
                        ega_recalctimings(ega);
                } else if (ega->attraddr == 0x11) {
                    ega->overscan_color = ega->vres ? pallook16[val & 0x0f] : pallook64[val & 0x3f];
                    if (o != val)
                        ega_recalctimings(ega);
                } else if (ega->attraddr == 0x12)
                    ega->plane_mask = val & 0xf;
            }
            ega->attrff ^= 1;
            break;
        case 0x3c2:
            o                   = ega->miscout;
            egaswitchread       = (val & 0xc) >> 2;
            ega->vres           = !(val & 0x80);
            ega->pallook        = ega->vres ? pallook16 : pallook64;
            ega->vidclock       = val & 4;
            ega->miscout        = val;
            ega->overscan_color = ega->vres ? pallook16[ega->attrregs[0x11] & 0x0f] : pallook64[ega->attrregs[0x11] & 0x3f];
            io_removehandler(0x03a0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
            if (!(val & 1))
                io_sethandler(0x03a0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
            ega_recalctimings(ega);
            if ((ega_type == EGA_TYPE_COMPAQ) && !(val & 0x02))
                mem_mapping_disable(&ega->mapping);
            else  switch (ega->gdcreg[6] & 0xc) {
                case 0x0: /*128k at A0000*/
                    mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x20000);
                    break;
                case 0x4: /*64k at A0000*/
                    mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x10000);
                    break;
                case 0x8: /*32k at B0000*/
                    mem_mapping_set_addr(&ega->mapping, 0xb0000, 0x08000);
                    break;
                case 0xC: /*32k at B8000*/
                    mem_mapping_set_addr(&ega->mapping, 0xb8000, 0x08000);
                    break;

                default:
                    break;
            }
            break;
        case 0x3c4:
            ega->seqaddr = val;
            break;
        case 0x3c5:
            o                                = ega->seqregs[ega->seqaddr & 0xf];
            ega->seqregs[ega->seqaddr & 0xf] = val;
            if (o != val && (ega->seqaddr & 0xf) == 1)
                ega_recalctimings(ega);
            switch (ega->seqaddr & 0xf) {
                case 1:
                    if (ega->scrblank && !(val & 0x20))
                        ega->fullchange = 3;
                    ega->scrblank = (ega->scrblank & ~0x20) | (val & 0x20);
                    break;
                case 2:
                    ega->writemask = val & 0xf;
                    break;
                case 3:
                    ega->charsetb = (((val >> 2) & 3) * 0x10000) + 2;
                    ega->charseta = ((val & 3) * 0x10000) + 2;
                    break;
                case 4:
                    ega->chain2_write = !(val & 4);
                    break;

                default:
                    break;
            }
            break;
        case 0x3c6:
            if (ega_type == EGA_TYPE_COMPAQ)
                ega->ctl_mode = val;
            break;
        case 0x3ce:
            ega->gdcaddr = val;
            break;
        case 0x3cf:
            ega->gdcreg[ega->gdcaddr & 15] = val;
            switch (ega->gdcaddr & 15) {
                case 2:
                    ega->colourcompare = val;
                    break;
                case 4:
                    ega->readplane = val & 3;
                    break;
                case 5:
                    ega->writemode   = val & 3;
                    ega->readmode    = val & 8;
                    ega->chain2_read = val & 0x10;
                    break;
                case 6:
                    if ((ega_type == EGA_TYPE_COMPAQ) && !(ega->miscout & 0x02))
                        mem_mapping_disable(&ega->mapping);
                    else  switch (val & 0xc) {
                        case 0x0: /*128k at A0000*/
                            mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x20000);
                            break;
                        case 0x4: /*64k at A0000*/
                            mem_mapping_set_addr(&ega->mapping, 0xa0000, 0x10000);
                            break;
                        case 0x8: /*32k at B0000*/
                            mem_mapping_set_addr(&ega->mapping, 0xb0000, 0x08000);
                            break;
                        case 0xC: /*32k at B8000*/
                            mem_mapping_set_addr(&ega->mapping, 0xb8000, 0x08000);
                            break;

                        default:
                            break;
                    }
                    break;
                case 7:
                    ega->colournocare = val;
                    break;

                default:
                    break;
            }
            break;
        case 0x3d0:
        case 0x3d4:
            if (ega->chipset)
                ega->crtcreg = val & 0x3f;
            else
                ega->crtcreg = val & 0x1f;
            return;
        case 0x3d1:
        case 0x3d5:
            if (ega->chipset) {
                if ((ega->crtcreg < 7) && (ega->crtc[0x11] & 0x80) && !(ega->regs[0xb4] & 0x80))
                    return;
                if ((ega->crtcreg == 7) && (ega->crtc[0x11] & 0x80) && !(ega->regs[0xb4] & 0x80))
                    val = (ega->crtc[7] & ~0x10) | (val & 0x10);
            } else {
                if ((ega->crtcreg < 7) && (ega->crtc[0x11] & 0x80))
                    return;
                if ((ega->crtcreg == 7) && (ega->crtc[0x11] & 0x80))
                    val = (ega->crtc[7] & ~0x10) | (val & 0x10);
            }
            old                     = ega->crtc[ega->crtcreg];
            ega->crtc[ega->crtcreg] = val;
            if (old != val) {
                if (ega->crtcreg < 0xe || ega->crtcreg > 0x10) {
                    if ((ega->crtcreg == 0xc) || (ega->crtcreg == 0xd)) {
                        ega->fullchange = 3;
                        ega->ma_latch   = ((ega->crtc[0xc] << 8) | ega->crtc[0xd]) + ((ega->crtc[8] & 0x60) >> 5);
                    } else {
                        ega->fullchange = changeframecount;
                        ega_recalctimings(ega);
                    }
                }
            }
            break;

        default:
            break;
    }
}

uint8_t
ega_in(uint16_t addr, void *priv)
{
    ega_t  *ega = (ega_t *) priv;
    uint8_t ret = 0xff;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x1ce:
            ret = ega->index;
            break;
        case 0x1cf:
            switch (ega->index) {
                case 0xb7:
                    ret = ega->regs[ega->index] & ~8;
                    if (ati_eeprom_read((ati_eeprom_t *) ega->eeprom))
                        ret |= 8;
                    break;

                default:
                    ret = ega->regs[ega->index];
                    break;
            }
            break;

        case 0x3c0:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->attraddr | ega->attr_palette_enable;
            break;
        case 0x3c1:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->attrregs[ega->attraddr];
            break;
        case 0x3c2:
            ret = (egaswitches & (8 >> egaswitchread)) ? 0x10 : 0x00;
            break;
        case 0x3c4:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->seqaddr;
            break;
        case 0x3c5:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->seqregs[ega->seqaddr & 0xf];
            break;
        case 0x3c6:
            if (ega_type == EGA_TYPE_COMPAQ)
                ret = ega->ctl_mode;
            break;
        case 0x3c8:
            if (ega_type == EGA_TYPE_OTHER)
                ret = 2;
            break;
        case 0x3cc:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->miscout;
            break;
        case 0x3ce:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->gdcaddr;
            break;
        case 0x3cf:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->gdcreg[ega->gdcaddr & 0xf];
            break;
        case 0x3d0:
        case 0x3d4:
            if (ega_type == EGA_TYPE_OTHER)
                ret = ega->crtcreg;
            break;
        case 0x3d1:
        case 0x3d5:
            switch (ega->crtcreg) {
                case 0xc:
                case 0xd:
                case 0xe:
                case 0xf:
                    ret = ega->crtc[ega->crtcreg];
                    break;

                case 0x10:
                    if (ega_type == EGA_TYPE_OTHER)
                        ret = ega->crtc[ega->crtcreg];
                    else
                        ret = ega->light_pen >> 8;
                    break;

                case 0x11:
                    if (ega_type == EGA_TYPE_OTHER)
                        ret = ega->crtc[ega->crtcreg];
                    else
                        ret = ega->light_pen & 0xff;
                    break;

                default:
                    if (ega_type == EGA_TYPE_OTHER)
                        ret = ega->crtc[ega->crtcreg];
                    break;
            }
            break;
        case 0x3da:
            ega->attrff = 0;
            if (ega_type == EGA_TYPE_COMPAQ) {
                ret = ega->stat & 0xcf;
                switch ((ega->attrregs[0x12] >> 4) & 0x03) {
                    case 0x00:
                        /* 00 = Pri. Red (5), Pri. Blue (4) */
                        ret |= (ega->color_mux & 0x04) ? 0x20 : 0x00;
                        ret |= (ega->color_mux & 0x01) ? 0x10 : 0x00;
                        break;
                    case 0x01:
                    case 0x03:
                        /* 01 = Sec. Red (5), Sec. Green (4) */
                        /* 11 = Sec. Red (5), Sec. Green (4) */
                        ret |= (ega->color_mux & 0x20) ? 0x20 : 0x00;
                        ret |= (ega->color_mux & 0x10) ? 0x10 : 0x00;
                        break;
                    case 0x02:
                        /* 10 = Sec. Blue (5), Pri. Green (4) */
                        ret |= (ega->color_mux & 0x08) ? 0x20 : 0x00;
                        ret |= (ega->color_mux & 0x02) ? 0x10 : 0x00;
                        break;
                }
            } else {
                ega->stat ^= 0x30; /* Fools IBM EGA video BIOS self-test. */
                ret = ega->stat;
            }
            break;
        case 0x7c6:
            ret = 0xfd;        /* EGA mode supported. */
            break;
        case 0xbc6:
            /* 0000 = None;
               0001 = Compaq Dual-Mode (DM) Monitor;
               0010 = RGBI Color Monitor;
               0011 = COMPAQ Color Monitor (RrGgBb) or Compatible;
               0100 - 1111 = Reserved. */
            ret = 0x01;
            break;
        case 0xfc6:
            ret = 0xfd;
            break;

        default:
            break;
    }

    return ret;
}

void
ega_recalctimings(ega_t *ega)
{
    int clksel;
    int color;

    double _dispontime;
    double _dispofftime;
    double disptime;
    double crtcconst;

    ega->vtotal     = ega->crtc[6];
    ega->dispend    = ega->crtc[0x12];
    ega->vsyncstart = ega->crtc[0x10];
    ega->split      = ega->crtc[0x18];

    if (ega->crtc[7] & 1)
        ega->vtotal |= 0x100;
    if (ega->crtc[7] & 32)
        ega->vtotal |= 0x200;
    ega->vtotal += 2;

    if (ega->crtc[7] & 2)
        ega->dispend |= 0x100;
    if (ega->crtc[7] & 64)
        ega->dispend |= 0x200;
    ega->dispend++;

    if (ega->crtc[7] & 4)
        ega->vsyncstart |= 0x100;
    if (ega->crtc[7] & 128)
        ega->vsyncstart |= 0x200;
    ega->vsyncstart++;

    if (ega->crtc[7] & 0x10)
        ega->split |= 0x100;
    if (ega->crtc[9] & 0x40)
        ega->split |= 0x200;
    ega->split++;

    ega->hdisp = ega->crtc[1];
    ega->hdisp++;

    ega->rowoffset = ega->crtc[0x13];

    ega->linedbl  = ega->crtc[9] & 0x80;
    ega->rowcount = ega->crtc[9] & 0x1f;

    if (ega_type == EGA_TYPE_COMPAQ) {
        color = (ega->miscout & 1);
        clksel = ((ega->miscout & 0xc) >> 2);

        if (color) {
            if (ega->vidclock)
                crtcconst = (cpuclock / 16257000.0 * (double) (1ULL << 32));
            else
                crtcconst = (cpuclock / (157500000.0 / 11.0) * (double) (1ULL << 32));
        } else {
            if (ega->vidclock)
                crtcconst = (cpuclock / 18981000.0 * (double) (1ULL << 32));
            else
                crtcconst = (cpuclock / 16872000.0 * (double) (1ULL << 32));
        }
        if (!(ega->seqregs[1] & 1))
            crtcconst *= 9.0;
        else
            crtcconst *= 8.0;
    } else if (ega->eeprom) {
        clksel = ((ega->miscout & 0xc) >> 2) | ((ega->regs[0xbe] & 0x10) ? 4 : 0);

        switch (clksel) {
            case 0:
                crtcconst = (cpuclock / 25175000.0 * (double) (1ULL << 32));
                break;
            case 1:
                crtcconst = (cpuclock / 28322000.0 * (double) (1ULL << 32));
                break;
            case 4:
                crtcconst = (cpuclock / 14318181.0 * (double) (1ULL << 32));
                break;
            case 5:
                crtcconst = (cpuclock / 16257000.0 * (double) (1ULL << 32));
                break;
            case 7:
            default:
                crtcconst = (cpuclock / 36000000.0 * (double) (1ULL << 32));
                break;
        }
        if (!(ega->seqregs[1] & 1))
            crtcconst *= 9.0;
        else
            crtcconst *= 8.0;
    } else {
        if (ega->vidclock)
            crtcconst = (ega->seqregs[1] & 1) ? MDACONST : (MDACONST * (9.0 / 8.0));
        else
            crtcconst = (ega->seqregs[1] & 1) ? CGACONST : (CGACONST * (9.0 / 8.0));
    }
    if (!(ega->seqregs[1] & 1))
        ega->dot_clock = crtcconst / 9.0;
    else
        ega->dot_clock = crtcconst / 8.0;

    ega->interlace = 0;

    ega->ma_latch = (ega->crtc[0xc] << 8) | ega->crtc[0xd];

    ega->render = ega_render_blank;
    if (!ega->scrblank && ega->attr_palette_enable) {
        if (!(ega->gdcreg[6] & 1)) {
            if (ega->seqregs[1] & 8)
                ega->hdisp *= (ega->seqregs[1] & 1) ? 16 : 18;
            else
                ega->hdisp *= (ega->seqregs[1] & 1) ? 8 : 9;
            ega->render    = ega_render_text;
            ega->hdisp_old = ega->hdisp;
        } else {
            ega->hdisp *= (ega->seqregs[1] & 8) ? 16 : 8;
            ega->render    = ega_render_graphics;
            ega->hdisp_old = ega->hdisp;
        }
    }

    if (ega->chipset) {
        if (ega->hdisp > 640) {
            ega->dispend <<= 1;
            ega->vtotal <<= 1;
            ega->split <<= 1;
            ega->vsyncstart <<= 1;
        }
    }

    overscan_y = (ega->rowcount + 1) << 1;

    if (overscan_y < 16)
        overscan_y = 16;

    overscan_x = (ega->seqregs[1] & 1) ? 16 : 18;

    if (ega->vres)
        overscan_y <<= 1;

    if (ega->seqregs[1] & 8)
        overscan_x <<= 1;

    ega->y_add = (overscan_y >> 1);
    ega->x_add = (overscan_x >> 1) - ega->scrollcache;

    if (ega->vres)
        ega->y_add >>= 1;

    if (ega->seqregs[1] & 8) {
        disptime    = (double) ((ega->crtc[0] + 2) << 1);
        _dispontime = (double) ((ega->crtc[1] + 1) << 1);
    } else {
        disptime    = (double) (ega->crtc[0] + 2);
        _dispontime = (double) (ega->crtc[1] + 1);
    }
    _dispofftime = disptime - _dispontime;
    _dispontime *= crtcconst;
    _dispofftime *= crtcconst;

    ega->dispontime  = (uint64_t) (_dispontime);
    ega->dispofftime = (uint64_t) (_dispofftime);
    if (ega->dispontime < TIMER_USEC)
        ega->dispontime = TIMER_USEC;
    if (ega->dispofftime < TIMER_USEC)
        ega->dispofftime = TIMER_USEC;

    if (ega_type == EGA_TYPE_COMPAQ) {
        ega->dot_time  = (uint64_t) (ega->dot_clock);
        if (ega->dot_time < TIMER_USEC)
            ega->dot_time = TIMER_USEC;
        timer_disable(&ega->dot_timer);
        timer_set_delay_u64(&ega->dot_timer, ega->dot_time);
        ega->cca = 0;
        active = 1;
        ega->dot = 0;
    }

    ega_recalc_remap_func(ega);
}

/* This is needed for the Compaq EGA so that it can pass the 3DA
   palette mux part of the self-test. */
void
ega_dot_poll(void *priv)
{
    ega_t   *ega = (ega_t *) priv;
    static uint8_t chr;
    static uint8_t attr;
    const bool doublewidth   = ((ega->seqregs[1] & 8) != 0);
    const bool attrblink     = ((ega->attrregs[0x10] & 8) != 0);
    const bool attrlinechars = (ega->attrregs[0x10] & 4);
    const bool crtcreset     = ((ega->crtc[0x17] & 0x80) == 0);
    const bool seq9dot       = ((ega->seqregs[1] & 1) == 0);
    const bool blinked       = ega->blink & 0x10;
    const int  dwshift       = doublewidth ? 1 : 0;
    const int  dotwidth      = 1 << dwshift;
    const int  charwidth     = dotwidth * (seq9dot ? 9 : 8);
    const int  cursoron      = (ega->sc == (ega->crtc[10] & 31));
    const int  cursoraddr    = (ega->crtc[0xe] << 8) | ega->crtc[0xf];
    uint32_t addr;
    int drawcursor;
    uint32_t charaddr;
    static int fg            = 0;
    static int bg            = 0;
    static uint32_t dat      = 0x00000000;
    static int cclock        = 0;
    static int disptime;
    static int _dispontime;
    static int _dispofftime;

    if (ega->seqregs[1] & 8) {
        disptime    = ((ega->crtc[0] + 2) << 1);
        _dispontime = ((ega->crtc[1] + 1) << 1);
    } else {
        disptime    = (ega->crtc[0] + 2);
        _dispontime = (ega->crtc[1] + 1);
    }
    _dispofftime = disptime - _dispontime;

    timer_advance_u64(&ega->dot_timer, ega->dot_time);

    if (ega->render == ega_render_text)
        ega->color_mux = (dat & (0x100 >> (ega->dot >> dwshift))) ? fg : bg;
    else
        ega->color_mux = 0x00;

    addr = ega->remap_func(ega, ega->cca) & ega->vrammask;

    if (!crtcreset) {
        chr  = ega->vram[addr];
        attr = ega->vram[addr + 1];
    } else
        chr = attr = 0;

    drawcursor = ((ega->cca == cursoraddr) && cursoron && ega->cursoron);

    if (attr & 8)
        charaddr = ega->charsetb + (chr * 0x80);
    else
        charaddr = ega->charseta + (chr * 0x80);

    dat = ega->vram[charaddr + (ega->sc << 2)];
    dat <<= 1;
    if ((chr & ~0x1F) == 0xC0 && attrlinechars)
        dat |= (dat >> 1) & 1;

    if (!active)
        dat = 0x200;

    if (drawcursor) {
        bg = ega->egapal[attr & 0x0f];
        fg = ega->egapal[attr >> 4];
    } else {
        fg = ega->egapal[attr & 0x0f];
        bg = ega->egapal[attr >> 4];
        if ((attr & 0x80) && attrblink) {
            bg = ega->egapal[(attr >> 4) & 7];
            if (blinked)
                fg = bg;
        }
    }

    ega->dot = (ega->dot + 1) % charwidth;

    if (ega->dot == 0) {
        ega->cca = (ega->cca + 4) & 0x3ffff;

        cclock++;

        if (active && (cclock == _dispofftime))
            active = 0;
        else if (!active && (cclock == _dispontime))
            active = 1;
    }
}

void
ega_poll(void *priv)
{
    ega_t   *ega = (ega_t *) priv;
    int      x, y;
    int      old_ma;
    int      wx = 640;
    int      wy = 350;
    uint32_t blink_delay;

    if (!ega->linepos) {
        timer_advance_u64(&ega->timer, ega->dispofftime);
        ega->stat |= 1;
        ega->linepos = 1;

        if (ega->dispon) {
            ega->hdisp_on = 1;

            ega->ma &= ega->vrammask;
            if (ega->firstline == 2000) {
                ega->firstline = ega->displine;
                video_wait_for_buffer();
            }

            old_ma = ega->ma;
            ega->displine *= ega->vres + 1;
            ega->y_add *= ega->vres + 1;
            for (y = 0; y <= ega->vres; y++) {
                /* Render scanline */
                ega->render(ega);

                /* Render overscan */
                ega->x_add = (overscan_x >> 1);
                ega_render_overscan_left(ega);
                ega_render_overscan_right(ega);
                ega->x_add = (overscan_x >> 1) - ega->scrollcache;

                if (y != ega->vres) {
                    ega->ma = old_ma;
                    ega->displine++;
                }
            }
            ega->displine /= ega->vres + 1;
            ega->y_add /= ega->vres + 1;

            if (ega->lastline < ega->displine)
                ega->lastline = ega->displine;
        }

        ega->displine++;
        if (ega->interlace)
            ega->displine++;
        if ((ega->stat & 8) && ((ega->displine & 15) == (ega->crtc[0x11] & 15)) && ega->vslines)
            ega->stat &= ~8;
        ega->vslines++;
        if (ega->chipset) {
            if (ega->hdisp > 640) {
                if (ega->displine > 2000)
                    ega->displine = 0;
            } else {
                if (ega->displine > 500)
                    ega->displine = 0;
            }
        } else {
            if (ega->displine > 500)
                ega->displine = 0;
        }
    } else {
        timer_advance_u64(&ega->timer, ega->dispontime);

        if (ega->dispon)
            ega->stat &= ~1;
        ega->hdisp_on = 0;

        ega->linepos = 0;
        if ((ega->sc == (ega->crtc[11] & 31)) || (ega->sc == ega->rowcount))
            ega->con = 0;
        if (ega->dispon) {
            /* TODO: Verify real hardware behaviour for out-of-range fine vertical scroll */
            if (ega->linedbl && !ega->linecountff) {
                ega->linecountff = 1;
                ega->ma          = ega->maback;
                ega->cca          = ega->maback;
            }
            if (ega->sc == (ega->crtc[9] & 31)) {
                ega->linecountff = 0;
                ega->sc          = 0;

                ega->maback += (ega->rowoffset << 3);
                if (ega->interlace)
                    ega->maback += (ega->rowoffset << 3);
                ega->maback &= ega->vrammask;
                ega->ma = ega->maback;
                ega->cca = ega->maback;
            } else {
                ega->linecountff = 0;
                ega->sc++;
                ega->sc &= 31;
                ega->ma = ega->maback;
                ega->cca = ega->maback;
            }
        }
        ega->vc++;
        if (ega->chipset) {
            if (ega->hdisp > 640)
                ega->vc &= 1023;
            else
                ega->vc &= 511;
        } else
            ega->vc &= 511;
        if (ega->vc == ega->split) {
            // TODO: Implement the hardware bug where the first scanline is drawn twice when the split happens
            if (ega->interlace && ega->oddeven)
                ega->ma = ega->maback = ega->rowoffset << 1;
            else
                ega->ma = ega->maback = 0;
            ega->ma <<= 2;
            ega->cca = ega->ma;
            ega->maback <<= 2;
            ega->sc = 0;
        }
        if (ega->vc == ega->dispend) {
            ega->dispon = 0;
            blink_delay = (ega->crtc[11] & 0x60) >> 5;
            if (ega->crtc[10] & 0x20)
                ega->cursoron = 0;
            else if (blink_delay == 2)
                ega->cursoron = ((ega->blink % 96) >= 48);
            else
                ega->cursoron = ega->blink & (16 + (16 * blink_delay));

            if (!(ega->gdcreg[6] & 1) && !(ega->blink & 15))
                ega->fullchange = 2;
            ega->blink = (ega->blink + 1) & 0x7f;

            if (ega->fullchange)
                ega->fullchange--;
        }
        if (ega->vc == ega->vsyncstart) {
            ega->dispon = 0;
            ega->stat |= 8;
#if 0
            picint(1 << 2);
#endif
            x = ega->hdisp;

            if (ega->interlace && !ega->oddeven)
                ega->lastline++;
            if (ega->interlace && ega->oddeven)
                ega->firstline--;

            wx = x;

            if (ega->vres) {
                wy = (ega->lastline - ega->firstline) << 1;
                ega_doblit(wx, wy, ega);
            } else {
                wy = ega->lastline - ega->firstline;
                ega_doblit(wx, wy, ega);
            }

            frames++;

            ega->firstline = 2000;
            ega->lastline  = 0;

            ega->firstline_draw = 2000;
            ega->lastline_draw  = 0;

            ega->oddeven ^= 1;

            changeframecount = ega->interlace ? 3 : 2;
            ega->vslines     = 0;

            if (ega->interlace && ega->oddeven)
                ega->ma = ega->maback = ega->ma_latch + (ega->rowoffset << 1);
            else
                ega->ma = ega->maback = ega->ma_latch;
            ega->ca = (ega->crtc[0xe] << 8) | ega->crtc[0xf];

            ega->ma <<= 2;
            ega->maback <<= 2;
            ega->ca <<= 2;
            ega->cca = ega->ma;
        }
        if (ega->vc == ega->vtotal) {
            ega->vc       = 0;
            ega->sc       = (ega->crtc[0x8] & 0x1f);
            ega->dispon   = 1;
            ega->displine = (ega->interlace && ega->oddeven) ? 1 : 0;

            ega->scrollcache = (ega->attrregs[0x13] & 0x0f);
            if (ega->scrollcache >= 0x8)
                ega->scrollcache = 0;
            else
                ega->scrollcache++;

            if (ega->seqregs[1] & 8)
                ega->scrollcache <<= 1;

            ega->x_add = (overscan_x >> 1) - ega->scrollcache;

            ega->linecountff = 0;
        }
        if (ega->sc == (ega->crtc[10] & 31))
            ega->con = 1;
    }
}

void
ega_doblit(int wx, int wy, ega_t *ega)
{
    int       unscaled_overscan_y = ega->vres ? overscan_y >> 1 : overscan_y;
    int       y_add   = enable_overscan ? unscaled_overscan_y : 0;
    int       x_add   = enable_overscan ? overscan_x : 0;
    int       y_start = enable_overscan ? 0 : (unscaled_overscan_y >> 1);
    int       x_start = enable_overscan ? 0 : (overscan_x >> 1);
    int       bottom  = (unscaled_overscan_y >> 1);
    uint32_t *p;
    int       i;
    int       j;
    int       xs_temp;
    int       ys_temp;

    if (ega->vres) {
        y_add <<= 1;
        y_start <<= 1;
        bottom <<= 1;
    }

    if ((wx <= 0) || (wy <= 0))
        return;

    if (ega->vres)
        ega->y_add <<= 1;

    xs_temp = wx;
    ys_temp = wy + 1;
    if (ega->vres)
        ys_temp++;
    if (xs_temp < 64)
        xs_temp = 640;
    if (ys_temp < 32)
        ys_temp = 200;

    if ((ega->crtc[0x17] & 0x80) && ((xs_temp != xsize) || (ys_temp != ysize) || video_force_resize_get())) {
        /* Screen res has changed.. fix up, and let them know. */
        xsize = xs_temp;
        ysize = ys_temp;

        if ((xsize > 1984) || (ysize > 2016)) {
            /* 2048x2048 is the biggest safe render texture, to account for overscan,
               we suppress overscan starting from x 1984 and y 2016. */
            x_add             = 0;
            y_add             = 0;
            suppress_overscan = 1;
        } else
            suppress_overscan = 0;

        set_screen_size(xsize + x_add, ysize + y_add);

        if (video_force_resize_get())
            video_force_resize_set(0);
    }

    if ((wx >= 160) && ((wy + 1) >= 120)) {
        /* Draw (overscan_size - scroll size) lines of overscan on top and bottom. */
        for (i = 0; i < ega->y_add; i++) {
            p = &buffer32->line[i & 0x7ff][0];

            for (j = 0; j < (xsize + x_add); j++)
                p[j] = ega->overscan_color;
        }

        for (i = 0; i < bottom; i++) {
            p = &buffer32->line[(ysize + ega->y_add + i) & 0x7ff][0];

            for (j = 0; j < (xsize + x_add); j++)
                p[j] = ega->overscan_color;
        }
    }

    video_blit_memtoscreen(x_start, y_start, xsize + x_add, ysize + y_add);

    if (ega->vres)
        ega->y_add >>= 1;
}

uint32_t
ega_remap_cpu_addr(uint32_t inaddr, ega_t *ega)
{
    int      a0mux;
    uint32_t addr = inaddr;

    // The CPU A0 line is multiplexed via a 3-to-8 mux.
    // Input bits are:
    // bit 0: 1 = 64K, 0 = 128K+ (from memory expansion connector)
    // bit 1: 1 = Odd/Even mode, 0 = normal mode (from GC reg 6 bit 1)
    // bit 2: 1 = 128K mapping, 0 = other mapping (from memory decode PROM)
    a0mux = 0;

    if (ega->gdcreg[6] & 2) {
        a0mux |= 2;
    }
    if (ega->vram_limit <= 64 * 1024) {
        a0mux |= 1;
    }

    switch (ega->gdcreg[6] & 0xC) {
        case 0x0: // 128K A000
            addr &= 0xFFFF;
            // TODO: Confirm the behaviour of this on actual hardware
            a0mux |= 4;
            break;
        case 0x4: // 64K A000
            addr &= 0xFFFF;
            break;
        case 0x8: // 32K B000
            addr &= 0x7FFF;
            break;
        case 0xC: // 32K B800
            addr &= 0x7FFF;
            break;

        default:
            break;
    }

    switch (a0mux) {
        case 0:
        case 1:
        case 4:
        case 5:
        case 7: // A0 becomes A0
            break;
        case 2:
            // A0 becomes the inversion of PGSEL (reg 0x3C2, miscout, bit 5)
            // That is, 1 selects the "low" 64k, and 0 selects the "high" 64k.
            addr &= ~1;
            addr |= (~ega->miscout >> 5) & 1;
            break;
        case 3: // A0 becomes A14
            addr &= ~1;
            addr |= (inaddr >> 14) & 1;
            break;
        case 6: // A0 becomes A16
            addr &= ~1;
            addr |= (inaddr >> 16) & 1;
            break;

        default:
            break;
    }

    // In 64k mode, only select the first 16Kword/64KB bank
    if (!(ega->seqregs[4] & 2)) {
        addr &= 0x3FFF;
    }
    return addr;
}

void
ega_write(uint32_t addr, uint8_t val, void *priv)
{
    ega_t  *ega = (ega_t *) priv;
    uint8_t vala;
    uint8_t valb;
    uint8_t valc;
    uint8_t vald;
    int     writemask2 = ega->writemask;

    cycles -= video_timing_write_b;

    if (ega->chain2_write) {
        writemask2 &= ~0xa;
        if (addr & 1)
            writemask2 <<= 1;
    }

    addr = ega_remap_cpu_addr(addr, ega);

    addr <<= 2;

    if (addr >= ega->vram_limit)
        return;

    if (!(ega->gdcreg[6] & 1))
        ega->fullchange = 2;

    switch (ega->writemode) {
        case 1:
            if (writemask2 & 1)
                ega->vram[addr] = ega->la;
            if (writemask2 & 2)
                ega->vram[addr | 0x1] = ega->lb;
            if (writemask2 & 4)
                ega->vram[addr | 0x2] = ega->lc;
            if (writemask2 & 8)
                ega->vram[addr | 0x3] = ega->ld;
            break;
        case 0:
            if (ega->gdcreg[3] & 7)
                val = ega_rotate[ega->gdcreg[3] & 7][val];

            if ((ega->gdcreg[8] == 0xff) && !(ega->gdcreg[3] & 0x18) && !ega->gdcreg[1]) {
                if (writemask2 & 1)
                    ega->vram[addr] = val;
                if (writemask2 & 2)
                    ega->vram[addr | 0x1] = val;
                if (writemask2 & 4)
                    ega->vram[addr | 0x2] = val;
                if (writemask2 & 8)
                    ega->vram[addr | 0x3] = val;
            } else {
                if (ega->gdcreg[1] & 1)
                    vala = (ega->gdcreg[0] & 1) ? 0xff : 0;
                else
                    vala = val;
                if (ega->gdcreg[1] & 2)
                    valb = (ega->gdcreg[0] & 2) ? 0xff : 0;
                else
                    valb = val;
                if (ega->gdcreg[1] & 4)
                    valc = (ega->gdcreg[0] & 4) ? 0xff : 0;
                else
                    valc = val;
                if (ega->gdcreg[1] & 8)
                    vald = (ega->gdcreg[0] & 8) ? 0xff : 0;
                else
                    vald = val;
                switch (ega->gdcreg[3] & 0x18) {
                    case 0: /*Set*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                        break;
                    case 8: /*AND*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala | ~ega->gdcreg[8]) & ega->la;
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb | ~ega->gdcreg[8]) & ega->lb;
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc | ~ega->gdcreg[8]) & ega->lc;
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald | ~ega->gdcreg[8]) & ega->ld;
                        break;
                    case 0x10: /*OR*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala & ega->gdcreg[8]) | ega->la;
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | ega->lb;
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | ega->lc;
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | ega->ld;
                        break;
                    case 0x18: /*XOR*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala & ega->gdcreg[8]) ^ ega->la;
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) ^ ega->lb;
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) ^ ega->lc;
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) ^ ega->ld;
                        break;

                    default:
                        break;
                }
            }
            break;
        case 2:
            if (!(ega->gdcreg[3] & 0x18) && !ega->gdcreg[1]) {
                if (writemask2 & 1)
                    ega->vram[addr] = (((val & 1) ? 0xff : 0) & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                if (writemask2 & 2)
                    ega->vram[addr | 0x1] = (((val & 2) ? 0xff : 0) & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                if (writemask2 & 4)
                    ega->vram[addr | 0x2] = (((val & 4) ? 0xff : 0) & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                if (writemask2 & 8)
                    ega->vram[addr | 0x3] = (((val & 8) ? 0xff : 0) & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
            } else {
                vala = ((val & 1) ? 0xff : 0);
                valb = ((val & 2) ? 0xff : 0);
                valc = ((val & 4) ? 0xff : 0);
                vald = ((val & 8) ? 0xff : 0);
                switch (ega->gdcreg[3] & 0x18) {
                    case 0: /*Set*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala & ega->gdcreg[8]) | (ega->la & ~ega->gdcreg[8]);
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | (ega->lb & ~ega->gdcreg[8]);
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | (ega->lc & ~ega->gdcreg[8]);
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | (ega->ld & ~ega->gdcreg[8]);
                        break;
                    case 8: /*AND*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala | ~ega->gdcreg[8]) & ega->la;
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb | ~ega->gdcreg[8]) & ega->lb;
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc | ~ega->gdcreg[8]) & ega->lc;
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald | ~ega->gdcreg[8]) & ega->ld;
                        break;
                    case 0x10: /*OR*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala & ega->gdcreg[8]) | ega->la;
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) | ega->lb;
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) | ega->lc;
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) | ega->ld;
                        break;
                    case 0x18: /*XOR*/
                        if (writemask2 & 1)
                            ega->vram[addr] = (vala & ega->gdcreg[8]) ^ ega->la;
                        if (writemask2 & 2)
                            ega->vram[addr | 0x1] = (valb & ega->gdcreg[8]) ^ ega->lb;
                        if (writemask2 & 4)
                            ega->vram[addr | 0x2] = (valc & ega->gdcreg[8]) ^ ega->lc;
                        if (writemask2 & 8)
                            ega->vram[addr | 0x3] = (vald & ega->gdcreg[8]) ^ ega->ld;
                        break;

                    default:
                        break;
                }
            }
            break;

        default:
            break;
    }
}

uint8_t
ega_read(uint32_t addr, void *priv)
{
    ega_t  *ega = (ega_t *) priv;
    uint8_t temp;
    uint8_t temp2;
    uint8_t temp3;
    uint8_t temp4;
    int     readplane = ega->readplane;

    cycles -= video_timing_read_b;

    if (ega->chain2_read) {
        readplane = (readplane & 2) | (addr & 1);
    }

    addr = ega_remap_cpu_addr(addr, ega);

    addr <<= 2;

    if (addr >= ega->vram_limit)
        return 0xff;

    ega->la = ega->vram[addr];
    ega->lb = ega->vram[addr | 0x1];
    ega->lc = ega->vram[addr | 0x2];
    ega->ld = ega->vram[addr | 0x3];
    if (ega->readmode) {
        temp = ega->la;
        temp ^= (ega->colourcompare & 1) ? 0xff : 0;
        temp &= (ega->colournocare & 1) ? 0xff : 0;
        temp2 = ega->lb;
        temp2 ^= (ega->colourcompare & 2) ? 0xff : 0;
        temp2 &= (ega->colournocare & 2) ? 0xff : 0;
        temp3 = ega->lc;
        temp3 ^= (ega->colourcompare & 4) ? 0xff : 0;
        temp3 &= (ega->colournocare & 4) ? 0xff : 0;
        temp4 = ega->ld;
        temp4 ^= (ega->colourcompare & 8) ? 0xff : 0;
        temp4 &= (ega->colournocare & 8) ? 0xff : 0;
        return ~(temp | temp2 | temp3 | temp4);
    }

    if ((ega_type == EGA_TYPE_COMPAQ) && (ega->gdcreg[4] & 0x04))
        return 0xff;

    return ega->vram[addr | readplane];
}

void
ega_init(ega_t *ega, int monitor_type, int is_mono)
{
    int c;
    int d;
    int e;

    ega->vram     = malloc(0x40000);
    ega->vrammask = 0x3ffff;

    for (c = 0; c < 256; c++) {
        e = c;
        for (d = 0; d < 8; d++) {
            ega_rotate[d][c] = e;
            e                = (e >> 1) | ((e & 1) ? 0x80 : 0);
        }
    }

    if (is_mono) {
        for (c = 0; c < 256; c++) {
            if (((c >> 3) & 3) == 0)
                pallook64[c] = pallook16[c] = makecol32(0, 0, 0);
            else
                switch (monitor_type >> 4) {
                    case DISPLAY_GREEN:
                        switch ((c >> 3) & 3) {
                            case 1:
                                pallook64[c] = pallook16[c] = makecol32(0x08, 0xc7, 0x2c);
                                break;
                            case 2:
                                pallook64[c] = pallook16[c] = makecol32(0x04, 0x8a, 0x20);
                                break;
                            case 3:
                                pallook64[c] = pallook16[c] = makecol32(0x34, 0xff, 0x5d);
                                break;

                            default:
                                break;
                        }
                        break;
                    case DISPLAY_AMBER:
                        switch ((c >> 3) & 3) {
                            case 1:
                                pallook64[c] = pallook16[c] = makecol32(0xef, 0x79, 0x00);
                                break;
                            case 2:
                                pallook64[c] = pallook16[c] = makecol32(0xb2, 0x4d, 0x00);
                                break;
                            case 3:
                                pallook64[c] = pallook16[c] = makecol32(0xff, 0xe3, 0x34);
                                break;

                            default:
                                break;
                        }
                        break;
                    case DISPLAY_WHITE:
                    default:
                        switch ((c >> 3) & 3) {
                            case 1:
                                pallook64[c] = pallook16[c] = makecol32(0xaf, 0xb3, 0xb0);
                                break;
                            case 2:
                                pallook64[c] = pallook16[c] = makecol32(0x7a, 0x81, 0x83);
                                break;
                            case 3:
                                pallook64[c] = pallook16[c] = makecol32(0xff, 0xfd, 0xed);
                                break;

                            default:
                                break;
                        }
                        break;
                }
        }

        io_sethandler(0x03a0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
    } else {
        for (c = 0; c < 256; c++) {
            pallook64[c] = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
            pallook64[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
            pallook16[c] = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
            pallook16[c] += makecol32(((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55);
            if ((c & 0x17) == 6)
                pallook16[c] = makecol32(0xaa, 0x55, 0);
        }

        ega->miscout |= 1;
    }

    ega->pallook = pallook16;

    egaswitches = monitor_type & 0xf;

    ega->vram_limit = 256 * 1024;
    ega->vrammask   = ega->vram_limit - 1;

    old_overscan_color = 0;

    ega->miscout |= 0x22;
    ega->oddeven_page = 0;

    ega->seqregs[4] |= 2;
    ega->extvram = 1;

    update_overscan = 0;

    ega->crtc[0] = 63;
    ega->crtc[6] = 255;

    timer_add(&ega->timer, ega_poll, ega, 1);
    if (ega_type == EGA_TYPE_COMPAQ)
        timer_add(&ega->dot_timer, ega_dot_poll, ega, 1);
}

static void *
ega_standalone_init(const device_t *info)
{
    ega_t *ega = malloc(sizeof(ega_t));
    int    monitor_type;

    memset(ega, 0x00, sizeof(ega_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ega);

    overscan_x = 16;
    overscan_y = 28;
    ega->x_add = 8;
    ega->y_add = 14;

    if ((info->local == EGA_IBM) || (info->local == EGA_ISKRA) || (info->local == EGA_TSENG))
        ega_type = EGA_TYPE_IBM;
    else if (info->local == EGA_COMPAQ)
        ega_type = EGA_TYPE_COMPAQ;
    else
        ega_type = EGA_TYPE_OTHER;

    ega->actual_type = info->local;
    ega->chipset = 0;

    switch (info->local) {
        default:
        case EGA_IBM:
            rom_init(&ega->bios_rom, BIOS_IBM_PATH,
                     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            break;
        case EGA_COMPAQ:
            ega->ctl_mode = 0x21;
            rom_init(&ega->bios_rom, BIOS_CPQ_PATH,
                     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            break;
        case EGA_SUPEREGA:
            rom_init(&ega->bios_rom, BIOS_SEGA_PATH,
                     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            break;
        case EGA_ATI800P:
            rom_init(&ega->bios_rom, BIOS_ATIEGA800P_PATH,
                     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            ega->chipset = 1;
            break;
        case EGA_ISKRA:
            rom_init_interleaved(&ega->bios_rom, BIOS_ISKRA_PATH,
                                 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            break;
        case EGA_TSENG:
            rom_init(&ega->bios_rom, BIOS_TSENG_PATH,
                     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
            break;
    }

    if ((ega->bios_rom.rom[0x3ffe] == 0xaa) && (ega->bios_rom.rom[0x3fff] == 0x55)) {
        for (uint16_t c = 0; c < 0x2000; c++) {
            uint8_t temp                  = ega->bios_rom.rom[c];
            ega->bios_rom.rom[c]          = ega->bios_rom.rom[0x3fff - c];
            ega->bios_rom.rom[0x3fff - c] = temp;
        }
    }

    monitor_type = device_get_config_int("monitor_type");
    ega_init(ega, monitor_type, (monitor_type & 0x0F) == 0x0B);

    ega->vram_limit = device_get_config_int("memory") * 1024;
    ega->vrammask   = ega->vram_limit - 1;

    mem_mapping_add(&ega->mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, ega);
    if (ega_type == EGA_TYPE_COMPAQ)
        mem_mapping_disable(&ega->mapping);
    io_sethandler(0x03c0, 0x0020, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);

    if (ega->chipset) {
        io_sethandler(0x01ce, 0x0002, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        ega->eeprom = malloc(sizeof(ati_eeprom_t));
        memset(ega->eeprom, 0, sizeof(ati_eeprom_t));
        ati_eeprom_load((ati_eeprom_t *) ega->eeprom, "egawonder800p.nvr", 0);
    } else if (info->local == EGA_COMPAQ) {
        io_sethandler(0x0084, 0x0001, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        io_sethandler(0x07c6, 0x0001, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        io_sethandler(0x0bc6, 0x0001, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
        io_sethandler(0x0fc6, 0x0001, ega_in, NULL, NULL, ega_out, NULL, NULL, ega);
    }

    return ega;
}

static int
ega_standalone_available(void)
{
    return rom_present(BIOS_IBM_PATH);
}

static int
cpqega_standalone_available(void)
{
    return rom_present(BIOS_CPQ_PATH);
}

static int
sega_standalone_available(void)
{
    return rom_present(BIOS_SEGA_PATH);
}

static int
atiega800p_standalone_available(void)
{
    return rom_present(BIOS_ATIEGA800P_PATH);
}

static int
iskra_ega_standalone_available(void)
{
    return rom_present("roms/video/ega/143-02.bin") && rom_present("roms/video/ega/143-03.bin");
}

static int
et2000_standalone_available(void)
{
    return rom_present(BIOS_TSENG_PATH);
}

static void
ega_close(void *priv)
{
    ega_t *ega = (ega_t *) priv;

    if (ega->eeprom)
        free(ega->eeprom);
    free(ega->vram);
    free(ega);
}

static void
ega_speed_changed(void *priv)
{
    ega_t *ega = (ega_t *) priv;

    ega_recalctimings(ega);
}

/* SW1 SW2 SW3 SW4
   OFF OFF  ON OFF  Monochrome                      (5151) 1011 0x0B
    ON OFF OFF  ON  Color 40x25                     (5153) 0110 0x06
   OFF OFF OFF  ON  Color 80x25                     (5153) 0111 0x07
    ON  ON  ON OFF  Enhanced Color - Normal Mode    (5154) 1000 0x08
   OFF  ON  ON OFF  Enhanced Color - Enhanced Mode  (5154) 1001 0x09

   0 = Switch closed (ON);
   1 = Switch open   (OFF). */
static const device_config_t ega_config[] = {
  // clang-format off
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 256,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description =  "32 KB", .value =  32 },
            { .description =  "64 KB", .value =  64 },
            { .description = "128 KB", .value = 128 },
            { .description = "256 KB", .value = 256 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "monitor_type",
        .description    = "Monitor type",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 9,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Monochrome (5151/MDA) (white)",             .value = 0x0B | (DISPLAY_WHITE << 4) },
            { .description = "Monochrome (5151/MDA) (green)",             .value = 0x0B | (DISPLAY_GREEN << 4) },
            { .description = "Monochrome (5151/MDA) (amber)",             .value = 0x0B | (DISPLAY_AMBER << 4) },
            { .description = "Color 40x25 (5153/CGA)",                    .value = 0x06                        },
            { .description = "Color 80x25 (5153/CGA)",                    .value = 0x07                        },
            { .description = "Enhanced Color - Normal Mode (5154/ECD)",   .value = 0x08                        },
            { .description = "Enhanced Color - Enhanced Mode (5154/ECD)", .value = 0x09                        },
            { .description = ""                                                                                }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t ega_device = {
    .name          = "IBM EGA",
    .internal_name = "ega",
    .flags         = DEVICE_ISA,
    .local         = EGA_IBM,
    .init          = ega_standalone_init,
    .close         = ega_close,
    .reset         = NULL,
    .available     = ega_standalone_available,
    .speed_changed = ega_speed_changed,
    .force_redraw  = NULL,
    .config        = ega_config
};

const device_t cpqega_device = {
    .name          = "Compaq EGA",
    .internal_name = "compaq_ega",
    .flags         = DEVICE_ISA,
    .local         = EGA_COMPAQ,
    .init          = ega_standalone_init,
    .close         = ega_close,
    .reset         = NULL,
    .available     = cpqega_standalone_available,
    .speed_changed = ega_speed_changed,
    .force_redraw  = NULL,
    .config        = ega_config
};

const device_t sega_device = {
    .name          = "SuperEGA",
    .internal_name = "superega",
    .flags         = DEVICE_ISA,
    .local         = EGA_SUPEREGA,
    .init          = ega_standalone_init,
    .close         = ega_close,
    .reset         = NULL,
    .available     = sega_standalone_available,
    .speed_changed = ega_speed_changed,
    .force_redraw  = NULL,
    .config        = ega_config
};

const device_t atiega800p_device = {
    .name          = "ATI EGA Wonder 800+",
    .internal_name = "egawonder800p",
    .flags         = DEVICE_ISA,
    .local         = EGA_ATI800P,
    .init          = ega_standalone_init,
    .close         = ega_close,
    .reset         = NULL,
    .available     = atiega800p_standalone_available,
    .speed_changed = ega_speed_changed,
    .force_redraw  = NULL,
    .config        = ega_config
};

const device_t iskra_ega_device = {
    .name          = "Iskra EGA (Cyrillic ROM)",
    .internal_name = "iskra_ega",
    .flags         = DEVICE_ISA,
    .local         = EGA_ISKRA,
    .init          = ega_standalone_init,
    .close         = ega_close,
    .reset         = NULL,
    .available     = iskra_ega_standalone_available,
    .speed_changed = ega_speed_changed,
    .force_redraw  = NULL,
    .config        = ega_config
};

const device_t et2000_device = {
    .name          = "Tseng Labs ET2000",
    .internal_name = "et2000",
    .flags         = DEVICE_ISA,
    .local         = EGA_TSENG,
    .init          = ega_standalone_init,
    .close         = ega_close,
    .reset         = NULL,
    .available     = et2000_standalone_available,
    .speed_changed = ega_speed_changed,
    .force_redraw  = NULL,
    .config        = ega_config
};
