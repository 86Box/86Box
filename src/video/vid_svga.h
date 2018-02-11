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
 * Version:	@(#)vid_svga.h	1.0.7	2018/02/11
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */

typedef struct svga_t
{
        mem_mapping_t mapping;

	int enabled;

        
        uint8_t crtcreg;
        uint8_t crtc[128];
        uint8_t gdcreg[64];
        int gdcaddr;
        uint8_t attrregs[32];
        int attraddr, attrff;
        int attr_palette_enable;
        uint8_t seqregs[64];
        int seqaddr;
        
        uint8_t miscout;
        int vidclock;

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

        uint8_t la, lb, lc, ld;
        
        uint8_t dac_mask, dac_status;
        int dac_read, dac_write, dac_pos;
        int dac_r, dac_g;
                
        uint8_t cgastat;
        
        uint8_t plane_mask;
        
        int fb_only;
        
        int fast;
        uint8_t colourcompare, colournocare;
        int readmode, writemode, readplane;
        int chain4, chain2_write, chain2_read;
	int oddeven_page, oddeven_chain;
	int extvram;
        uint8_t writemask;
        uint32_t charseta, charsetb;
        
        uint8_t egapal[16];
        uint32_t pallook[256];
        PALETTE vgapal;
        
        int ramdac_type;

        int vtotal, dispend, vsyncstart, split, vblankstart;
        int hdisp,  hdisp_old, htotal,  hdisp_time, rowoffset;
        int lowres, interlace;
        int linedbl, rowcount;
        double clock;
        uint32_t ma_latch;
        int bpp;
        
        int64_t dispontime, dispofftime;
        int64_t vidtime;
        
        uint8_t scrblank;
        
        int dispon;
        int hdisp_on;

        uint32_t ma, maback, ca;
        int vc;
        int sc;
        int linepos, vslines, linecountff, oddeven;
        int con, cursoron, blink;
        int scrollcache;
        
        int firstline, lastline;
        int firstline_draw, lastline_draw;
        int displine;
        
        uint8_t *vram;
        uint8_t *changedvram;
        int vram_display_mask;
        uint32_t banked_mask;

        uint32_t write_bank, read_bank;
                
        int fullchange;
        
        int video_res_x, video_res_y, video_bpp;
        int frames, fps;

        struct
        {
                int ena;
                int x, y;
                int xoff, yoff;
                int xsize, ysize;
                uint32_t addr;
		uint32_t pitch;
                int v_acc, h_acc;
        } hwcursor, hwcursor_latch, overlay, overlay_latch;
        
        int hwcursor_on;
        int overlay_on;
        
        int hwcursor_oddeven;
        int overlay_oddeven;
        
        void (*render)(struct svga_t *svga);
        void (*recalctimings_ex)(struct svga_t *svga);

        void    (*video_out)(uint16_t addr, uint8_t val, void *p);
        uint8_t (*video_in) (uint16_t addr, void *p);

        void (*hwcursor_draw)(struct svga_t *svga, int displine);

        void (*overlay_draw)(struct svga_t *svga, int displine);
        
        void (*vblank_start)(struct svga_t *svga);

        /*If set then another device is driving the monitor output and the SVGA
          card should not attempt to display anything */
        int override;
        void *p;

        uint32_t linear_base;
} svga_t;

extern int svga_init(svga_t *svga, void *p, int memsize, 
               void (*recalctimings_ex)(struct svga_t *svga),
               uint8_t (*video_in) (uint16_t addr, void *p),
               void    (*video_out)(uint16_t addr, uint8_t val, void *p),
               void (*hwcursor_draw)(struct svga_t *svga, int displine),
               void (*overlay_draw)(struct svga_t *svga, int displine));
extern void svga_recalctimings(svga_t *svga);
extern void svga_close(svga_t *svga);


uint8_t  svga_read(uint32_t addr, void *p);
uint16_t svga_readw(uint32_t addr, void *p);
uint32_t svga_readl(uint32_t addr, void *p);
void     svga_write(uint32_t addr, uint8_t val, void *p);
void     svga_writew(uint32_t addr, uint16_t val, void *p);
void     svga_writel(uint32_t addr, uint32_t val, void *p);
uint8_t  svga_read_linear(uint32_t addr, void *p);
uint16_t svga_readw_linear(uint32_t addr, void *p);
uint32_t svga_readl_linear(uint32_t addr, void *p);
void     svga_write_linear(uint32_t addr, uint8_t val, void *p);
void     svga_writew_linear(uint32_t addr, uint16_t val, void *p);
void     svga_writel_linear(uint32_t addr, uint32_t val, void *p);

void svga_add_status_info(char *s, int max_len, void *p);

extern uint8_t svga_rotate[8][256];

void svga_out(uint16_t addr, uint8_t val, void *p);
uint8_t svga_in(uint16_t addr, void *p);

svga_t *svga_get_pri();
void svga_set_override(svga_t *svga, int val);

#define RAMDAC_6BIT 0
#define RAMDAC_8BIT 1
void svga_set_ramdac_type(svga_t *svga, int type);

void svga_close(svga_t *svga);

uint32_t svga_mask_addr(uint32_t addr, svga_t *svga);
uint32_t svga_mask_changedaddr(uint32_t addr, svga_t *svga);

extern uint32_t shade[5][256];

static __inline__ uint32_t svga_color_transform(uint32_t color)
{
	uint8_t *clr8 = (uint8_t *) &color;
	if (!video_grayscale && !invert_display)
		return color;
	if (video_grayscale)
	{
		if (video_graytype)
		{
			if (video_graytype == 1)
				color = ((54 * (uint32_t)clr8[2]) + (183 * (uint32_t)clr8[1]) + (18 * (uint32_t)clr8[0])) / 255;
			else
				color = ((uint32_t)clr8[2] + (uint32_t)clr8[1] + (uint32_t)clr8[0]) / 3;
		}
		else
			color = ((76 * (uint32_t)clr8[2]) + (150 * (uint32_t)clr8[1]) + (29 * (uint32_t)clr8[0])) / 255;
		switch (video_grayscale)
		{
			case 2:
			case 3:
			case 4:
				color = shade[video_grayscale][color];
				break;
			default:
				clr8[3] = 0;
				clr8[0] = color;
				clr8[1] = clr8[2] = clr8[0];
				break;
		}
	}
	if (invert_display)
	{
		clr8[0] ^= 0xff;
		clr8[1] ^= 0xff;
		clr8[2] ^= 0xff;
	}
	return color;
}

void svga_doblit(int y1, int y2, int wx, int wy, svga_t *svga);
