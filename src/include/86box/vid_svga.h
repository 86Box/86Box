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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 */


#define FLAG_EXTRA_BANKS	1
#define	FLAG_ADDR_BY8		2
#define FLAG_LATCH8		4


typedef struct {
    int ena,
	x, y, xoff, yoff, xsize, ysize,
	v_acc, h_acc;
    uint32_t addr, pitch;
} hwcursor_t;

typedef union {
    uint64_t	q;
    uint32_t	d[2];
    uint16_t	w[4];
    uint8_t	b[8];
} latch_t;

typedef struct svga_t
{
    mem_mapping_t mapping;

    uint8_t fast, chain4, chain2_write, chain2_read,
	    ext_overscan, bus_size,
	    lowres, interlace, linedbl, rowcount,
	    set_reset_disabled, bpp, ramdac_type, fb_only,
	    readmode, writemode, readplane,
	    hwcursor_oddeven, dac_hwcursor_oddeven, overlay_oddeven;

    int dac_addr, dac_pos, dac_r, dac_g,
	vtotal, dispend, vsyncstart, split, vblankstart,
	hdisp,  hdisp_old, htotal,  hdisp_time, rowoffset,
	dispon, hdisp_on,
	vc, sc, linepos, vslines, linecountff, oddeven,
	con, cursoron, blink, scrollcache, char_width,
	firstline, lastline, firstline_draw, lastline_draw,
	displine, fullchange, x_add, y_add, pan,
	vram_display_mask, vidclock,
	hwcursor_on, dac_hwcursor_on, overlay_on;

    /*The three variables below allow us to implement memory maps like that seen on a 1MB Trio64 :
      0MB-1MB - VRAM
      1MB-2MB - VRAM mirror
      2MB-4MB - open bus
      4MB-xMB - mirror of above

      For the example memory map, decode_mask would be 4MB-1 (4MB address space), vram_max would be 2MB
      (present video memory only responds to first 2MB), vram_mask would be 1MB-1 (video memory wraps at 1MB)
    */
    uint32_t decode_mask, vram_max,
	     vram_mask,
	     charseta, charsetb,
	     adv_flags, ma_latch,
	     ca_adj, ma, maback,
	     write_bank, read_bank,
	     extra_banks[2],
	     banked_mask,
	     ca, overscan_color,
	     *map8, pallook[256];

    PALETTE vgapal;

    uint64_t dispontime, dispofftime;
    latch_t latch;

    pc_timer_t timer;

    double clock;

    hwcursor_t hwcursor, hwcursor_latch,
	       dac_hwcursor, dac_hwcursor_latch,
	       overlay, overlay_latch;

    void (*render)(struct svga_t *svga);
    void (*recalctimings_ex)(struct svga_t *svga);

    void    (*video_out)(uint16_t addr, uint8_t val, void *p);
    uint8_t (*video_in) (uint16_t addr, void *p);

    void (*hwcursor_draw)(struct svga_t *svga, int displine);

    void (*dac_hwcursor_draw)(struct svga_t *svga, int displine);

    void (*overlay_draw)(struct svga_t *svga, int displine);

    void (*vblank_start)(struct svga_t *svga);

    void (*ven_write)(struct svga_t *svga, uint8_t val, uint32_t addr);
    float (*getclock)(int clock, void *p);

    /* Called when VC=R18 and friends. If this returns zero then MA resetting
       is skipped. Matrox Mystique in Power mode reuses this counter for
       vertical line interrupt*/
    int (*line_compare)(struct svga_t *svga);    

    /*Called at the start of vertical sync*/
    void (*vsync_callback)(struct svga_t *svga);

    uint32_t (*translate_address)(uint32_t addr, void *p);
    /*If set then another device is driving the monitor output and the SVGA
      card should not attempt to display anything */
    int override;
    void *p;

    uint8_t crtc[128], gdcreg[64], attrregs[32], seqregs[64],
	    egapal[16],
	    *vram, *changedvram;

    uint8_t crtcreg, gdcaddr,
	    attrff, attr_palette_enable, attraddr, seqaddr,
	    miscout, cgastat, scrblank,
	    plane_mask, writemask,
	    colourcompare, colournocare,
	    dac_mask, dac_status,
	    ksc5601_sbyte_mask, ksc5601_udc_area_msb[2];

    int ksc5601_swap_mode;
    uint16_t ksc5601_english_font_type;

    int vertical_linedbl;
        
    /*Used to implement CRTC[0x17] bit 2 hsync divisor*/
    int hsync_divisor;

    void *ramdac, *clock_gen;
} svga_t;


extern int	svga_init(const device_t *info, svga_t *svga, void *p, int memsize, 
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

void		svga_set_ramdac_type(svga_t *svga, int type);
void		svga_close(svga_t *svga);

uint32_t	svga_mask_addr(uint32_t addr, svga_t *svga);
uint32_t	svga_mask_changedaddr(uint32_t addr, svga_t *svga);

void		svga_doblit(int y1, int y2, int wx, int wy, svga_t *svga);


enum {
    RAMDAC_6BIT = 0,
    RAMDAC_8BIT
};


/* We need a way to add a device with a pointer to a parent device so it can attach itself to it, and
   possibly also a second ATi 68860 RAM DAC type that auto-sets SVGA render on RAM DAC render change. */
extern void	ati68860_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga);
extern uint8_t	ati68860_ramdac_in(uint16_t addr, void *p, svga_t *svga);
extern void	ati68860_set_ramdac_type(void *p, int type);
extern void	ati68860_ramdac_set_render(void *p, svga_t *svga);
extern void	ati68860_ramdac_set_pallook(void *p, int i, uint32_t col);
extern void	ati68860_hwcursor_draw(svga_t *svga, int displine);

extern void	att49x_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga);
extern uint8_t	att49x_ramdac_in(uint16_t addr, void *p, svga_t *svga);

extern float	av9194_getclock(int clock, void *p);

extern void	bt48x_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *p, svga_t *svga);
extern uint8_t	bt48x_ramdac_in(uint16_t addr, int rs2, int rs3, void *p, svga_t *svga);
extern void	bt48x_recalctimings(void *p, svga_t *svga);
extern void	bt48x_hwcursor_draw(svga_t *svga, int displine);

extern void	icd2061_write(void *p, int val);
extern float	icd2061_getclock(int clock, void *p);

/* The code is the same, the #define's are so that the correct name can be used. */
#define ics9161_write icd2061_write
#define ics9161_getclock icd2061_getclock

extern float	ics2494_getclock(int clock, void *p);

extern void	ics2595_write(void *p, int strobe, int dat);
extern double	ics2595_getclock(void *p);
extern void	ics2595_setclock(void *p, double clock);

extern void	sc1148x_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga);
extern uint8_t	sc1148x_ramdac_in(uint16_t addr, void *p, svga_t *svga);

extern void	sc1502x_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga);
extern uint8_t	sc1502x_ramdac_in(uint16_t addr, void *p, svga_t *svga);

extern void	sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *p, svga_t *svga);
extern uint8_t	sdac_ramdac_in(uint16_t addr, int rs2, void *p, svga_t *svga);
extern float	sdac_getclock(int clock, void *p);

extern void	stg_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga);
extern uint8_t	stg_ramdac_in(uint16_t addr, void *p, svga_t *svga);
extern float	stg_getclock(int clock, void *p);

extern void	tkd8001_ramdac_out(uint16_t addr, uint8_t val, void *p, svga_t *svga);
extern uint8_t	tkd8001_ramdac_in(uint16_t addr, void *p, svga_t *svga);


#ifdef EMU_DEVICE_H
extern const device_t ati68860_ramdac_device;
extern const device_t att490_ramdac_device;
extern const device_t att492_ramdac_device;
extern const device_t av9194_device;
extern const device_t bt484_ramdac_device;
extern const device_t att20c504_ramdac_device;
extern const device_t bt485_ramdac_device;
extern const device_t att20c505_ramdac_device;
extern const device_t bt485a_ramdac_device;
extern const device_t gendac_ramdac_device;
extern const device_t ics2494an_305_device;
extern const device_t ics2595_device;
extern const device_t icd2061_device;
extern const device_t ics9161_device;
extern const device_t sc11483_ramdac_device;
extern const device_t sc11487_ramdac_device;
extern const device_t sc1502x_ramdac_device;
extern const device_t sdac_ramdac_device;
extern const device_t stg_ramdac_device;
extern const device_t tkd8001_ramdac_device;
#endif
