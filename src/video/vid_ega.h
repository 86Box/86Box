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
 * Version:	@(#)vid_ega.h	1.0.8	2019/10/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#ifndef VIDEO_EGA_H
# define VIDEO_EGA_H


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

    uint8_t *vram;

    int vidclock, fast, extvram, vres,
	readmode, writemode, readplane, vrammask,
	chain4, chain2_read, chain2_write, con,
	oddeven_page, oddeven_chain, vc, sc,
	dispon, hdisp_on, cursoron, blink,
	linepos, vslines, linecountff, oddeven,
	lowres, interlace, linedbl, lindebl, rowcount,
	vtotal, dispend, vsyncstart, split,
	hdisp,  hdisp_old, htotal, hdisp_time, rowoffset,
	vblankstart, scrollcache, firstline, lastline,
	firstline_draw, lastline_draw, x_add, y_add,
	displine, video_res_x, video_res_y, video_bpp;

    uint32_t charseta, charsetb, ma_latch, ma,
	     maback, ca, vram_limit, overscan_color;

    uint32_t *pallook;

    uint64_t dispontime, dispofftime;
	pc_timer_t timer;

    double clock;

    void (*render)(struct ega_t *svga);
} ega_t;
#endif


#ifdef EMU_DEVICE_H
extern const device_t ega_device;
extern const device_t cpqega_device;
extern const device_t sega_device;
#endif

extern int update_overscan;

#define DISPLAY_RGB 0
#define DISPLAY_COMPOSITE 1
#define DISPLAY_RGB_NO_BROWN 2
#define DISPLAY_GREEN 3
#define DISPLAY_AMBER 4
#define DISPLAY_WHITE 5


#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
extern void	ega_init(ega_t *ega, int monitor_type, int is_mono);
extern void	ega_recalctimings(struct ega_t *ega);
#endif

extern void	ega_out(uint16_t addr, uint8_t val, void *p);
extern uint8_t	ega_in(uint16_t addr, void *p);
extern void	ega_poll(void *p);
extern void	ega_write(uint32_t addr, uint8_t val, void *p);
extern uint8_t	ega_read(uint32_t addr, void *p);


#endif	/*VIDEO_EGA_H*/
