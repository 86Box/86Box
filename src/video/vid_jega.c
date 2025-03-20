/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the JEGA (Japanese EGA), a part of the AX architecture.
 *
 *          It's an extension of the SuperEGA. Superimposing text (AX-2) is not available.
 *
 * Authors: Akamaki
 *
 *          Copyright 2025 Akamaki
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
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
#include <86box/vid_ega.h>

/* JEGA internal registers */
#define RPESL 0x09 /* End Scan Line */
#define RCCSL 0x0A /* Cursor Start Line */
#define RCCEL 0x0B /* Cursor End Line */
#define RCCLH 0x0E /* Cursor Location High */
#define RCCLL 0x0F /* Cursor Location Low */
#define RPULP 0x14 /* Under Line Position */
/* + 0xB8 - 0x20 */
#define RMOD1 0x21 /* Display out, EGA through, Superimpose, Sync with EGA, Master EGA, Slave EGA, n/a, Test */
#define RMOD2 0x22 /* 1st Attr, 2nd Attr, Blink/Int, n/a, Font access mode (b3-2), Font map sel (b1-0)*/
#define RDAGS 0x23 /* ANK Group Select */
#define RDFFB 0x24 /* Font Access First Byte */
#define RDFSB 0x25 /* Font Access Second Byte */
#define RDFAP 0x26 /* Font Access Pattern */
#define RSTAT 0x27 /* Font Status Register */
/* + 0xD0 - 0x20 */
#define RPSSU 0x29 /* Start Scan Upper */
#define RPSSL 0x2A /* Start Scan Lower */
#define RPSSC 0x2B /* Start Scan Count */
#define RPPAJ 0x2C /* Phase Adjust Count */
#define RCMOD 0x2D /* Cursor Mode */
#define RCSKW 0x2E /* Cursor Skew Control */
#define ROMSL 0x2F /* ? */
#define RINVALID_INDEX 0x30

#define JEGA_PATH_BIOS     "roms/video/jega/JEGABIOS.BIN"
#define JEGA_PATH_FONTDBCS "roms/video/jega/JPNZN16X.FNT"
#define SBCS19_FILESIZE    (256 * 19 * 2) /* 8 x 19 x 256 chr x 2 pages */
#define DBCS16_CHARS       0x2c10
#define DBCS16_FILESIZE    (DBCS16_CHARS * 16 * 2)

#define INVALIDACCESS8     0xffu
#define INVALIDACCESS16    0xffffu
#define INVALIDACCESS32    0xffffffffu

#ifndef RELEASE_BUILD
// #    define ENABLE_JEGA_LOG 1
#endif

#ifdef ENABLE_JEGA_LOG
int jega_do_log = ENABLE_JEGA_LOG;

static void
jega_log(const char *fmt, ...)
{
    va_list ap;

    if (jega_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define jega_log(fmt, ...)
#endif

static video_timings_t timing_ega = { .type = VIDEO_ISA, .write_b = 8, .write_w = 16, .write_l = 32, .read_b = 8, .read_w = 16, .read_l = 32 };

typedef struct jega_t {
    rom_t    bios_rom;
    ega_t    ega;
    uint8_t  regs_index; /* 3D4/3D5 index B9-BF, D9-DF */
    uint8_t  regs[0x31];
    uint8_t egapal[16];
    uint8_t attrregs[32];
    uint8_t attraddr;
    uint8_t attrff;
    uint8_t attr_palette_enable;
    uint32_t *pallook;
    int con;
    int cursoron;
    int cursorblink_disable;
    int ca;
    int      font_index;
    int     sbcsbank_inv;
    int     attr3_sbcsbank;
    int     start_scan_lower;
    int     start_scan_upper;
    int     start_scan_count;
    uint8_t *vram;
    uint8_t  jfont_sbcs_19[SBCS19_FILESIZE]; /* 8 x 19 font */
    uint8_t  jfont_dbcs_16[DBCS16_FILESIZE]; /* 16 x 16 font. Use dbcs_read/write to access it. */
} jega_t;

static void jega_recalctimings(void *priv);

#define FONTX_LEN_ID 6
#define FONTX_LEN_FN 8

typedef struct {
    char id[FONTX_LEN_ID];
    char name[FONTX_LEN_FN];
    unsigned char width;
    unsigned char height;
    unsigned char type;
} fontx_h;

typedef struct {
    uint16_t start;
	uint16_t end;
} fontx_tbl;

static uint32_t        pallook64[256];
static bool is_SJIS_1(uint8_t chr) { return (chr >= 0x81 && chr <= 0x9f) || (chr >= 0xe0 && chr <= 0xfc); }
static bool is_SJIS_2(uint8_t chr) { return (chr >= 0x40 && chr <= 0x7e) || (chr >= 0x80 && chr <= 0xfc); }

static uint16_t
SJIS_to_SEQ(uint16_t sjis)
{
	uint32_t chr1 = (sjis >> 8) & 0xff;
	uint32_t  chr2 = sjis & 0xff;
	if (!is_SJIS_1(chr1) || !is_SJIS_2(chr2)) return INVALIDACCESS16;
	chr1 -= 0x81;
	if (chr1 > 0x5E) chr1 -= 0x40;
	chr2 -= 0x40;
	if (chr2 > 0x3F) chr2--;
	chr1 *= 0xBC;
	return (chr1 + chr2);
}

static uint8_t
dbcs_read(uint16_t sjis, int index, void *priv) {
    jega_t  *jega = (jega_t *) priv;
    int seq = SJIS_to_SEQ(sjis);
    if (seq >= DBCS16_CHARS || index >= 32)
        return INVALIDACCESS8;
    return jega->jfont_dbcs_16[seq * 32 + index];
}

static void
dbcs_write(uint16_t sjis, int index, uint8_t val, void *priv) {
    jega_t  *jega = (jega_t *) priv;
    int seq = SJIS_to_SEQ(sjis);
    if (seq >= DBCS16_CHARS || index >= 32)
        return;
    jega->jfont_dbcs_16[seq * 32 + index] = val;
}

/* Display Adapter Mode 3 Drawing */
void
jega_render_text(void *priv)
{
    jega_t *jega = (jega_t *) priv;
    if (jega->ega.firstline_draw == 2000)
        jega->ega.firstline_draw = jega->ega.displine;
    jega->ega.lastline_draw = jega->ega.displine;

    if (jega->ega.fullchange) {
        // const bool doublewidth   = ((jega->ega.seqregs[1] & 8) != 0);
        const bool attrblink = ((jega->regs[RMOD2] & 0x20) == 0); /* JEGA specific */
        // const bool attrlinechars = (jega->ega.attrregs[0x10] & 4);
        const bool crtcreset = ((jega->ega.crtc[0x17] & 0x80) == 0) || ((jega->regs[RMOD1] & 0x80) == 0);
        const int  charwidth = 8;
        const bool blinked   = jega->ega.blink & 0x10;
        uint32_t  *p         = &buffer32->line[jega->ega.displine + jega->ega.y_add][jega->ega.x_add];
        bool       chr_wide  = false;
        int        sc_wide   = jega->ega.sc - jega->start_scan_count;
        const bool cursoron  = (blinked || jega->cursorblink_disable)
            && (jega->ega.sc >= jega->regs[RCCSL]) && (jega->ega.sc <= jega->regs[RCCEL]);
        uint32_t chr_first;
        uint32_t attr_basic;
        int      fg;
        int      bg;

        for (int x = 0; x < (jega->ega.hdisp + jega->ega.scrollcache); x += charwidth) {
            uint32_t addr = jega->ega.remap_func(&jega->ega, jega->ega.ma) & jega->ega.vrammask;

            int drawcursor = ((jega->ega.ma == jega->ca) && cursoron);

            uint32_t chr;
            uint32_t attr;
            if (!crtcreset) {
                chr  = jega->ega.vram[addr];
                attr = jega->ega.vram[addr + 1];
            } else
                chr = attr = 0;
            if (chr_wide) {
                uint8_t attr_ext = 0;
                /* the code may be in DBCS */
                if (jega->regs[RMOD2] & 0x40) {
                    /* Parse JEGA extended attribute */
                    /* Bold | 2x width | 2x height | U/L select | R/L select | - | - | - */
                    attr_ext = attr;
                    if ((attr_ext & 0x30) == 0x30)
                        sc_wide = jega->ega.sc - jega->start_scan_lower; /* Set top padding of lower 2x character */
                    else if ((attr_ext & 0x30) == 0x20)
                        sc_wide = jega->ega.sc - jega->start_scan_upper; /* Set top padding of upper 2x character */
                    else
                        sc_wide = jega->ega.sc - jega->start_scan_count;
                }
                if (is_SJIS_2(chr) && sc_wide >= 0 && sc_wide < 16 && jega->ega.sc <= jega->regs[RPESL]) {
                    chr_first <<= 8;
                    chr |= chr_first;
                    /* Vertical wide font (Extended Attribute) */
                    if (attr_ext & 0x20) {
                        if (attr_ext & 0x10)
                            sc_wide = (sc_wide >> 1) + 8;
                        else
                            sc_wide = sc_wide >> 1;
                    }
                    /* Horizontal wide font (Extended Attribute) */
                    if (attr_ext & 0x40) {
                        uint32_t dat = dbcs_read(chr, sc_wide, jega);
                        if (!(attr_ext & 0x08)) { /* right half of character */
                            dat = dbcs_read(chr, sc_wide + 16, jega);
                        }
                        for (int xx = 0; xx < charwidth; xx++) {
                            p[xx * 2]     = (dat & (0x80 >> xx)) ? fg : bg;
                            p[xx * 2 + 1] = (dat & (0x80 >> xx)) ? fg : bg;
                        }
                    } else {
                        uint32_t dat = dbcs_read(chr, sc_wide, jega);
                        dat <<= 8;
                        dat |= dbcs_read(chr, sc_wide + 16, jega);
                        /* Bold (Extended Attribute) */
                        if (attr_ext &= 0x80) {
                            uint32_t dat2 = dat;
                            dat2 >>= 1;
                            dat |= dat2;
                        }
                        for (int xx = 0; xx < charwidth * 2; xx++)
                            p[xx] = (dat & (0x8000 >> xx)) ? fg : bg;
                    }
                } else {
                    /* invalid DBCS code or line space then put blank */
                    for (int xx = 0; xx < charwidth * 2; xx++)
                        p[xx] = bg;
                }
                if (attr_basic & 0x20) { /* vertical line */
                    p[0] = fg;
                }
                if ((jega->ega.sc == jega->regs[RPULP]) && (attr_basic & 0x10)) { /* underline */
                    for (int xx = 0; xx < charwidth * 2; xx++)
                        p[xx] = fg;
                }
                chr_wide = false;
                p += (charwidth * 2);
            } else {
                /* SBCS or invalid second byte of DBCS */
                if (jega->regs[RMOD2] & 0x80) {
                    /* Parse attribute as JEGA */
                    /* Blink | Reverse | V line | U line | (Bit 3-0 is the same as EGA) */
                    /* The background color is always black (transparent in AX-2) */
                    if (drawcursor || attr & 0x40) {
                        bg = jega->pallook[jega->egapal[attr & 0x0f]];
                        fg = 0;
                    } else {
                        fg = jega->pallook[jega->egapal[attr & 0x0f]];
                        bg = 0;
                        if (attr & 0x80) {
                            bg = 0;
                            if (blinked)
                                fg = bg;
                        }
                    }
                    attr_basic = attr;
                } else {
                    /* Parse attribute as EGA */
                    /* BInt/Blink | BR | BG | BB | Int/Group | R | G | B */
                    if (drawcursor) {
                        bg = jega->pallook[jega->egapal[attr & 0x0f]];
                        fg = jega->pallook[jega->egapal[attr >> 4]];
                    } else {
                        fg = jega->pallook[jega->egapal[attr & 0x0f]];
                        bg = jega->pallook[jega->egapal[attr >> 4]];
                        if ((attr & 0x80) && attrblink) {
                            bg = jega->pallook[jega->egapal[(attr >> 4) & 7]];
                            if (blinked)
                                fg = bg;
                        }
                    }
                    attr_basic = 0;
                }

                if (is_SJIS_1(chr)) {
                    /* the char code maybe in DBCS */
                    chr_first = chr;
                    chr_wide  = true;
                } else {
                    /* the char code is in SBCS */
                    uint32_t charaddr = chr;
                    // if (jega->attr3_sbcsbank && (attr & 8))
                    //     charaddr |= 0x100;
                    // if (jega->sbcsbank_inv)
                    //     charaddr ^= 0x100;
                    charaddr *= 19;

                    uint32_t dat = jega->jfont_sbcs_19[charaddr + jega->ega.sc];
                    for (int xx = 0; xx < charwidth; xx++)
                        p[xx] = (dat & (0x80 >> xx)) ? fg : bg;

                    if (attr_basic & 0x20) { /* vertical line */
                        p[0] = fg;
                    }
                    if ((jega->ega.sc == jega->regs[RPULP]) && (attr_basic & 0x10)) { /* underline */
                        for (int xx = 0; xx < charwidth; xx++)
                            p[xx] = fg;
                    }
                    p += charwidth;
                }
            }
            jega->ega.ma += 4;
        }
        jega->ega.ma &= 0x3ffff;
    }
}

static void
jega_out(uint16_t addr, uint8_t val, void *priv)
{
    jega_t *jega = (jega_t *) priv;
    uint16_t chr;
    // jega_log("JEGA Out %04X %02X(%d) %04X:%04X\n", addr, val, val, cs >> 4, cpu_state.pc);
    switch (addr) {
        case 0x3c0:
        case 0x3c1:
        jega_log("Palette %02X %02X(%d) %04X:%04X\n", jega->attraddr, val, val, cs >> 4, cpu_state.pc);
            /* Palette (write only) */
            if (!jega->attrff) {
                jega->attraddr = val & 31;
                if ((val & 0x20) != jega->attr_palette_enable) {
                    jega->ega.fullchange      = 3;
                    jega->attr_palette_enable = val & 0x20;
                    jega_recalctimings(jega);
                }
            } else {
                jega->attrregs[jega->attraddr & 31] = val;
                if (jega->attraddr < 0x10) {
                    for (uint8_t c = 0; c < 16; c++) {
                            jega->egapal[c] = jega->attrregs[c] & 0x3f;
                    }
                    jega->ega.fullchange = changeframecount;
                }
            }
            jega->attrff ^= 1;
            break;
        case 0x3b4:
        case 0x3d4:
            /* Index 0x00-0x1F (write only), 0xB8-0xDF (write and read) */
            if (val >= 0xB8 && val <= 0xBF)
                jega->regs_index = val - 0xB8 + 0x20;
            else if (val >= 0xD8 && val <= 0xDF)
                jega->regs_index = val - 0xD0 + 0x20;
            else if (val <= 0x1F)
                jega->regs_index = val;
            else
                jega->regs_index = RINVALID_INDEX;
            break;
        case 0x3b5:
        case 0x3d5:
            /* Data */
            if (jega->regs_index != RINVALID_INDEX) {
                jega->regs[jega->regs_index] = val;
                jega_log("JEGA Out %04X(%02X) %02Xh(%d) %04X:%04X\n", addr, jega->regs_index, val, val, cs >> 4, cpu_state.pc);
                switch (jega->regs_index) {
                    case RMOD1:
                        /* if the value is changed */
                        // if (jega->regs[jega->regs_index] != val) {
                        if (val & 0x40)
                            jega->ega.render_override = NULL;
                        else
                            jega->ega.render_override = jega_render_text;
                        // }
                        break;
                    case RDAGS:
                        switch (val & 0x03) {
                            case 0x00:
                                jega->attr3_sbcsbank = false;
                                jega->sbcsbank_inv   = false;
                                break;
                            case 0x01:
                                jega->attr3_sbcsbank = true;
                                jega->sbcsbank_inv   = false;
                                break;
                            case 0x02:
                                jega->attr3_sbcsbank = true;
                                jega->sbcsbank_inv   = true;
                                break;
                            case 0x03:
                                jega->attr3_sbcsbank = false;
                                jega->sbcsbank_inv   = true;
                                break;
                        }
                        break;
                    case RCCLH:
                    case RCCLL:
                        jega->ca = jega->regs[RCCLH] << 10 | jega->regs[RCCLL] << 2;
                        break;
                    case RCMOD:
                        jega->cursoron = (val & 0x80);
                        jega->cursorblink_disable = (~val & 0x20);
                        break;
                    case RDFFB:
                    case RDFSB:
                        /* reset the line number */
                        jega->font_index = 0;
                        break;
                    case RPSSC:
                        if (val <= 17)
                            jega->start_scan_count = val + 1;
                        else
                            jega->start_scan_count = (val - 32) + 1;
                        break;
                    case RPSSL:
                        jega->start_scan_lower = val - 15;
                        break;
                    case RPSSU:
                        if (val <= 33)
                            jega->start_scan_upper = val + 4;
                        else
                            jega->start_scan_upper = (val - 64) + 4;
                        break;
                    case RDFAP:
                        chr = jega->regs[RDFFB];
                        if (is_SJIS_1(chr) && chr >= 0xf0 && chr <= 0xf3) {
                            chr <<= 8;
                            chr |= jega->regs[RDFSB];
                            if (jega->font_index < 32)
                                dbcs_write(chr, jega->font_index, val, jega);
                        } else {
                            if (jega->font_index <19)
                                jega->jfont_sbcs_19[chr * 19 + jega->font_index] = val;
                        }
                        jega_log("JEGA Font W %X %d %02Xh(%d) %04X:%04X\n", chr, jega->font_index, val, val, cs >> 4, cpu_state.pc);
                        jega->font_index++;
                        break;
                }
            }
            break;
        default:
            break;
    }
    if (jega->regs[RMOD1] & 0x0C) /* Accessing to Slave EGA is redirected to Master in AX-1. */
        ega_out(addr, val, &jega->ega);
}

static uint8_t
jega_in(uint16_t addr, void *priv)
{
    jega_t *jega = (jega_t *) priv;
    uint8_t ret  = INVALIDACCESS8;
    uint16_t chr;
    switch (addr) {
        case 0x3b5:
        case 0x3d5:
            if (jega->regs_index >= 0x20 && jega->regs_index <= 0x2F) {
                switch (jega->regs_index) {
                    case RDFAP:
                        chr = jega->regs[RDFFB];
                        /* DBCS or SBCS */
                        if (is_SJIS_1(chr)) {
                            chr <<= 8;
                            chr |= jega->regs[RDFSB];
                            if (jega->font_index < 32)
                                ret = dbcs_read(chr, jega->font_index, jega);
                        } else {
                            if (jega->font_index < 19)
                                ret = jega->jfont_sbcs_19[chr * 19 + jega->font_index];
                        }
                        jega_log("JEGA Font R %X %d %02Xh(%d) %04X:%04X\n", chr, jega->font_index, ret, ret, cs >> 4, cpu_state.pc);
                        jega->font_index++;
                        break;
                    case RSTAT:
                        ret = 0x03;
                        break;
                    default:
                        ret = jega->regs[jega->regs_index];
                        break;
                }
                jega_log("JEGA In %04X(%02X) %02X %04X:%04X\n", addr, jega->regs_index, ret, cs >> 4, cpu_state.pc);
            } else if (jega->regs[RMOD1] & 0x0C) /* Accessing to Slave EGA is redirected to Master in AX-1. */
                ret = ega_in(addr, &jega->ega);
            break;
        case 0x3ba:
        case 0x3da:
            jega->attrff = 0;
        default:
            if (jega->regs[RMOD1] & 0x0C) /* Accessing to Slave is redirected to Master in AX-1. */
                ret = ega_in(addr, &jega->ega);
            break;
    }
    // jega_log("JEGA In %04X(%02X) %02X %04X:%04X\n", addr, jega->regs_index, ret, cs >> 4, cpu_state.pc);
    return ret;
}

static int
getfontx2header(FILE *fp, fontx_h *header)
{
    fread(header->id, FONTX_LEN_ID, 1, fp);
    if (strncmp(header->id, "FONTX2", FONTX_LEN_ID) != 0) {
        return 1;
    }
    fread(header->name, FONTX_LEN_FN, 1, fp);
    header->width  = (uint8_t) getc(fp);
    header->height = (uint8_t) getc(fp);
    header->type   = (uint8_t) getc(fp);
    return 0;
}

static uint16_t
chrtosht(FILE *fp)
{
    uint16_t i, j;
    i = (uint16_t) getc(fp);
    j = (uint16_t) getc(fp) << 8;
    return (i | j);
}

static void
readfontxtbl(fontx_tbl *table, int size, FILE *fp)
{
    while (size > 0) {
        table->start = chrtosht(fp);
        table->end   = chrtosht(fp);
        ++table;
        --size;
    }
}

static int
LoadFontxFile(const char *fn, void *priv)
{
    fontx_h    fhead;
    fontx_tbl *ftbl;
    uint16_t   code;
    uint16_t   scode;
    uint8_t    size;
    uint8_t    buf;
    int        line;
    jega_t    *jega = (jega_t *) priv;
    FILE      *fp   = rom_fopen(fn, "rb");
    jega_log("JEGA: Loading font\n");
    if (fp == NULL) {
        jega_log("JEGA: font file '%s' not found.\n", fn);
        return 0;
    }
    if (getfontx2header(fp, &fhead) != 0) {
        fclose(fp);
        jega_log("JEGA: FONTX2 header is incorrect.\n");
        return 1;
    }
    /* DBCS or SBCS */
    if (fhead.type == 1) {
        if (fhead.width == 16 && fhead.height == 16) {
            size = getc(fp);
            ftbl = (fontx_tbl *) calloc(size, sizeof(fontx_tbl));
            readfontxtbl(ftbl, size, fp);
            for (int i = 0; i < size; i++) {
                for (code = ftbl[i].start; code <= ftbl[i].end; code++) {
                    scode = SJIS_to_SEQ(code);
                    if (scode != INVALIDACCESS16) {
                        for (line = 0; line < 16; line++) {
                            fread(&buf, sizeof(uint8_t), 1, fp);
                            jega->jfont_dbcs_16[(int) (scode * 32) + line] = buf;
                            fread(&buf, sizeof(uint8_t), 1, fp);
                            jega->jfont_dbcs_16[(int) (scode * 32) + line + 16] = buf;
                        }
                    } else {
                        fseek(fp, 32, SEEK_CUR);
                    }
                }
            }
        } else {
            fclose(fp);
            jega_log("JEGA: Width or height of DBCS font doesn't match.\n");
            return 1;
        }
    } else {
        if (fhead.width == 8 && fhead.height == 19) {
            fread(jega->jfont_sbcs_19, sizeof(uint8_t), SBCS19_FILESIZE, fp);
        } else {
            fclose(fp);
            jega_log("JEGA: Width or height of SBCS font doesn't match.\n");
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

static void *
jega_init(const device_t *info)
{
    jega_t *jega = calloc(1, sizeof(jega_t));

    rom_init(&jega->bios_rom, JEGA_PATH_BIOS, 0xc0000, 0x8000, 0x7fff, 0, 0);
    memset(&jega->jfont_dbcs_16, 0, DBCS16_FILESIZE);
    LoadFontxFile(JEGA_PATH_FONTDBCS, jega);

    for (int c = 0; c < 256; c++) {
        pallook64[c] = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
        pallook64[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
    }

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ega);
    jega->pallook = pallook64;
    ega_init(&jega->ega, 9, 0);
    jega->ega.actual_type = EGA_SUPEREGA;
    jega->ega.priv_parent = jega;
    mem_mapping_add(&jega->ega.mapping, 0xa0000, 0x20000, ega_read, NULL, NULL, ega_write, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, &jega->ega);

    io_sethandler(0x03b0, 0x0030, jega_in, NULL, NULL, jega_out, NULL, NULL, jega);

    jega->regs[RMOD1] = 0x48;

    return jega;
}

static void
jega_close(void *priv)
{
    jega_t *jega = (jega_t *) priv;
#ifdef ENABLE_JEGA_LOG
    FILE *f;
    // f = fopen("jega_font16.dmp", "wb");
    // if (f != NULL) {
    //     fwrite(jega->jfont_dbcs_16, DBCS16_FILESIZE, 1, f);
    //     fclose(f);
    // }
    // f = fopen("jega_font19.dmp", "wb");
    // if (f != NULL) {
    //     fwrite(jega->jfont_sbcs_19, SBCS19_FILESIZE, 1, f);
    //     fclose(f);
    // }
    f = fopen("jega_regs.txt", "wb");
    if (f != NULL) {
        for (int i = 0; i < 49; i++)
            fprintf(f, "Regs %02X: %4X\n", i, jega->regs[i]);
        for (int i = 0; i < 32; i++)
            fprintf(f, "Attr %02X: %4X\n", i, jega->attrregs[i]);
            for (int i = 0; i < 16; i++)
                fprintf(f, "JEGAPal %02X: %4X\n", i, jega->egapal[i]);
                for (int i = 0; i < 16; i++)
                    fprintf(f, "EGAPal %02X: %4X\n", i, jega->ega.egapal[i]);
        for (int i = 0; i < 64; i++)
        fprintf(f, "RealPal %02X: %4X\n", i, jega->pallook[i]);
        fclose(f);
    }
    // f = fopen("ega_vram.dmp", "wb");
    // if (f != NULL) {
    //     fwrite(jega->ega.vram, 256 * 1024, 1, f);
    //     fclose(f);
    // }
    f = fopen("ram_bda.dmp", "wb");
    if (f != NULL) {
        fwrite(&ram[0x0], 0x500, 1, f);
        fclose(f);
    }
    // jega_log("jeclosed %04X:%04X DS %04X\n", cs >> 4, cpu_state.pc, DS);
#endif
    if (jega->ega.eeprom)
        free(jega->ega.eeprom);
    free(jega->ega.vram);
    free(jega);
}

static void
jega_recalctimings(void *priv)
{
    jega_t *jega = (jega_t *) priv;
    ega_recalctimings(&jega->ega);
}
static void
jega_speed_changed(void *priv)
{
    jega_t *jega = (jega_t *) priv;

    jega_recalctimings(jega);
}

static int
jega_available(void)
{
    return (rom_present(JEGA_PATH_BIOS) && rom_present(JEGA_PATH_FONTDBCS));
}

const device_t jega_device = {
    .name          = "JEGA",
    .internal_name = "jega",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = jega_init,
    .close         = jega_close,
    .reset         = NULL,
    .available     = jega_available,
    .speed_changed = jega_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
