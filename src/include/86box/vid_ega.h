/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the EGA and Chips & Technologies SuperEGA
 *		graphics cards.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 */

#ifndef VIDEO_EGA_H
#define VIDEO_EGA_H

#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
typedef struct ega_t {
    mem_mapping_t mapping;

    rom_t bios_rom;

    uint8_t crtcreg, gdcaddr, attraddr, attrff,
        attr_palette_enable, seqaddr, miscout,
        writemask, la, lb, lc, ld,
        stat, colourcompare, colournocare, scrblank,
        plane_mask, pad, pad0, pad1;
    uint8_t crtc[32];
    uint8_t gdcreg[16];
    uint8_t attrregs[32];
    uint8_t seqregs[64];
    uint8_t egapal[16];
    uint8_t regs[256];

    uint8_t *vram;

    int vidclock, fast, extvram, vres,
        readmode, writemode, readplane, vrammask,
        chain4, chain2_read, chain2_write, con,
        oddeven_page, oddeven_chain, vc, sc,
        dispon, hdisp_on, cursoron, blink, fullchange,
        linepos, vslines, linecountff, oddeven,
        lowres, interlace, linedbl, lindebl, rowcount,
        vtotal, dispend, vsyncstart, split,
        hdisp, hdisp_old, htotal, hdisp_time, rowoffset,
        vblankstart, scrollcache, firstline, lastline,
        firstline_draw, lastline_draw, x_add, y_add,
        displine, res_x, res_y, bpp, index;

    uint32_t charseta, charsetb, ma_latch, ma,
        maback, ca, vram_limit, overscan_color;

    uint32_t *pallook;

    uint64_t   dispontime, dispofftime;
    pc_timer_t timer;

    double clock;

    int remap_required;
    uint32_t (*remap_func)(struct ega_t *ega, uint32_t in_addr);

    void (*render)(struct ega_t *svga);

    void *eeprom;
} ega_t;
#endif

#ifdef EMU_DEVICE_H
extern const device_t ega_device;
extern const device_t cpqega_device;
extern const device_t sega_device;
extern const device_t atiega_device;
extern const device_t iskra_ega_device;
extern const device_t et2000_device;
#endif

extern int update_overscan;

#define DISPLAY_RGB          0
#define DISPLAY_COMPOSITE    1
#define DISPLAY_RGB_NO_BROWN 2
#define DISPLAY_GREEN        3
#define DISPLAY_AMBER        4
#define DISPLAY_WHITE        5

#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
extern void ega_init(ega_t *ega, int monitor_type, int is_mono);
extern void ega_recalctimings(struct ega_t *ega);
extern void ega_recalc_remap_func(struct ega_t *ega);
#endif

extern void    ega_out(uint16_t addr, uint8_t val, void *p);
extern uint8_t ega_in(uint16_t addr, void *p);
extern void    ega_poll(void *p);
extern void    ega_write(uint32_t addr, uint8_t val, void *p);
extern uint8_t ega_read(uint32_t addr, void *p);

extern int firstline_draw, lastline_draw;
extern int displine;
extern int sc;

extern uint32_t ma, ca;
extern int      con, cursoron, cgablink;

extern int scrollcache;

extern uint8_t edatlookup[4][4];

#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
void ega_render_blank(ega_t *ega);

void ega_render_overscan_left(ega_t *ega);
void ega_render_overscan_right(ega_t *ega);

void ega_render_text_40(ega_t *ega);
void ega_render_text_80(ega_t *ega);

void ega_render_2bpp_lowres(ega_t *ega);
void ega_render_2bpp_highres(ega_t *ega);

void ega_render_4bpp_lowres(ega_t *ega);
void ega_render_4bpp_highres(ega_t *ega);
#endif

#endif /*VIDEO_EGA_H*/
