/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the EGA, Chips & Technologies SuperEGA, and
 *		AX JEGA graphics cards.
 *
 * Version:	@(#)vid_ega.h	1.0.7	2018/03/18
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		akm,
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 akm.
 */
#ifndef VIDEO_EGA_H
# define VIDEO_EGA_H


#ifdef JEGA
# define SBCS		0
# define DBCS		1
# define ID_LEN		6
# define NAME_LEN	8
# define SBCS19_LEN	256 * 19
# define DBCS16_LEN	65536 * 32
#endif


#if defined(EMU_MEM_H) && defined(EMU_ROM_H)
typedef struct ega_t {
    mem_mapping_t mapping;

    rom_t bios_rom;

    uint8_t crtcreg, gdcaddr, attraddr, attrff,
	    attr_palette_enable, seqaddr, miscout,
	    writemask, la, lb, lc, ld,
	    stat, colourcompare, colournocare, scrblank;
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
	lowres, interlace, lindebl, rowcount,
	vtotal, dispend, vsyncstart, split,
	hdisp, htotal, hdisp_time, rowoffset,
	vblankstart, scrollcache, firstline, lastline,
	firstline_draw, lastline_draw,
	displine, video_res_x, video_res_y, video_bpp;

    uint32_t charseta, charsetb, ma_latch, ma,
	     maback, ca, vram_limit, overscan_color;

    uint32_t *pallook;

    uint64_t dispontime, dispofftime;
	pc_timer_t timer;

    double clock;

#ifdef JEGA
    uint8_t RMOD1, RMOD2, RDAGS, RDFFB, RDFSB, RDFAP, RPESL, RPULP, RPSSC, RPSSU, RPSSL;
    uint8_t RPPAJ;
    uint8_t RCMOD, RCCLH, RCCLL, RCCSL, RCCEL, RCSKW, ROMSL, RSTAT;
    int is_jega, font_index;
    int chr_left, chr_wide;
#endif
} ega_t;
#endif


#ifdef EMU_DEVICE_H
extern const device_t ega_device;
extern const device_t cpqega_device;
extern const device_t sega_device;
#endif
#ifdef JEGA
extern uint8_t jfont_sbcs_19[SBCS19_LEN];	/* 256 * 19( * 8) */
extern uint8_t jfont_dbcs_16[DBCS16_LEN];	/* 65536 * 16 * 2 (* 8) */
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
