/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Generic SVGA handling.
 *
 * Version:	@(#)vid_svga.h	1.0.13	2018/08/14
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */

typedef struct {
    int ena,
	x, y, xoff, yoff, xsize, ysize,
	v_acc, h_acc;
    uint32_t addr, pitch;
} hwcursor_t;

typedef struct svga_t
{
    mem_mapping_t mapping;

    int enabled;

    uint8_t crtcreg, crtc[128],
	    gdcaddr, gdcreg[64],
	    attrff, attr_palette_enable,
	    attraddr, attrregs[32],
	    seqaddr, seqregs[64],
	    miscout, cgastat,
	    plane_mask, writemask,
	    colourcompare, colournocare,
	    scrblank, egapal[16],
	    *vram, *changedvram;

    int vidclock, fb_only,
	fast;

    /*The three variables below allow us to implement memory maps like that seen on a 1MB Trio64 :
      0MB-1MB - VRAM
      1MB-2MB - VRAM mirror
      2MB-4MB - open bus
      4MB-xMB - mirror of above

      For the example memory map, decode_mask would be 4MB-1 (4MB address space), vram_max would be 2MB
      (present video memory only responds to first 2MB), vram_mask would be 1MB-1 (video memory wraps at 1MB)
    */
    uint32_t decode_mask;
    uint32_t vram_max;
    uint32_t vram_mask;

    uint8_t dac_mask, dac_status;
    int dac_read, dac_write,
	dac_pos, ramdac_type,
	dac_r, dac_g;

    int readmode, writemode,
	readplane, extvram,
	chain4, chain2_write, chain2_read,
	oddeven_page, oddeven_chain,
	set_reset_disabled;

    uint32_t charseta, charsetb,
	     latch, ma_latch,
	     ma, maback,
	     write_bank, read_bank,
	     banked_mask,
	     ca, overscan_color,
	     pallook[256];

    PALETTE vgapal;

    int vtotal, dispend, vsyncstart, split, vblankstart,
	hdisp,  hdisp_old, htotal,  hdisp_time, rowoffset,
	lowres, interlace, linedbl, rowcount, bpp,
	dispon, hdisp_on,
	vc, sc, linepos, vslines, linecountff, oddeven,
	con, cursoron, blink, scrollcache,
	firstline, lastline, firstline_draw, lastline_draw,
	displine, fullchange,
	video_res_x, video_res_y, video_bpp, frames, fps,
	vram_display_mask,
	hwcursor_on, overlay_on,
	hwcursor_oddeven, overlay_oddeven;

    double clock;

    int64_t dispontime, dispofftime,
	    vidtime;

    hwcursor_t hwcursor, hwcursor_latch,
	       overlay, overlay_latch;

    void (*render)(struct svga_t *svga);
    void (*recalctimings_ex)(struct svga_t *svga);

    void    (*video_out)(uint16_t addr, uint8_t val, void *p);
    uint8_t (*video_in) (uint16_t addr, void *p);

    void (*hwcursor_draw)(struct svga_t *svga, int displine);

    void (*overlay_draw)(struct svga_t *svga, int displine);

    void (*vblank_start)(struct svga_t *svga);

    void (*ven_write)(struct svga_t *svga, uint8_t val, uint32_t addr);

    /*If set then another device is driving the monitor output and the SVGA
      card should not attempt to display anything */
    int override;
    void *p;
	
	uint8_t ksc5601_sbyte_mask;
} svga_t;


extern int	svga_init(svga_t *svga, void *p, int memsize, 
			  void (*recalctimings_ex)(struct svga_t *svga),
			  uint8_t (*video_in) (uint16_t addr, void *p),
			  void    (*video_out)(uint16_t addr, uint8_t val, void *p),
			  void (*hwcursor_draw)(struct svga_t *svga, int displine),
			  void (*overlay_draw)(struct svga_t *svga, int displine));
extern void	svga_recalctimings(svga_t *svga);
extern void	svga_close(svga_t *svga);

uint8_t		svga_read(uint32_t addr, void *p);
uint16_t	svga_readw(uint32_t addr, void *p);
uint32_t	svga_readl(uint32_t addr, void *p);
void		svga_write(uint32_t addr, uint8_t val, void *p);
void		svga_writew(uint32_t addr, uint16_t val, void *p);
void		svga_writel(uint32_t addr, uint32_t val, void *p);
uint8_t		svga_read_linear(uint32_t addr, void *p);
uint8_t		svga_readb_linear(uint32_t addr, void *p);
uint16_t	svga_readw_linear(uint32_t addr, void *p);
uint32_t	svga_readl_linear(uint32_t addr, void *p);
void		svga_write_linear(uint32_t addr, uint8_t val, void *p);
void		svga_writeb_linear(uint32_t addr, uint8_t val, void *p);
void		svga_writew_linear(uint32_t addr, uint16_t val, void *p);
void		svga_writel_linear(uint32_t addr, uint32_t val, void *p);

void		svga_add_status_info(char *s, int max_len, void *p);

extern		uint8_t svga_rotate[8][256];

void		svga_out(uint16_t addr, uint8_t val, void *p);
uint8_t		svga_in(uint16_t addr, void *p);

svga_t		*svga_get_pri();
void		svga_set_override(svga_t *svga, int val);

void		svga_set_ven_write(svga_t *svga,
				   void (*ven_write)(struct svga_t *svga, uint8_t val, uint32_t addr));

void		svga_set_ramdac_type(svga_t *svga, int type);
void		svga_close(svga_t *svga);

uint32_t	svga_mask_addr(uint32_t addr, svga_t *svga);
uint32_t	svga_mask_changedaddr(uint32_t addr, svga_t *svga);

void		svga_doblit(int y1, int y2, int wx, int wy, svga_t *svga);


enum {
    RAMDAC_6BIT = 0,
    RAMDAC_8BIT
};
