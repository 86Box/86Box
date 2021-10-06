/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		S3 ViRGE emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>


static uint64_t virge_time = 0;

static int dither[4][4] =
{
        {0,  4,  1,  5},
        {6,  2,  7,  3},
        {1,  5,  0,  4},
        {7,  3,  6,  2},
};

#define RB_SIZE 256
#define RB_MASK (RB_SIZE - 1)

#define RB_ENTRIES (virge->s3d_write_idx - virge->s3d_read_idx)
#define RB_FULL (RB_ENTRIES == RB_SIZE)
#define RB_EMPTY (!RB_ENTRIES)

#define FIFO_SIZE 65536
#define FIFO_MASK (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE (1 << 31)

#define FIFO_ENTRIES (virge->fifo_write_idx - virge->fifo_read_idx)
#define FIFO_FULL    ((virge->fifo_write_idx - virge->fifo_read_idx) >= FIFO_SIZE)
#define FIFO_EMPTY   (virge->fifo_read_idx == virge->fifo_write_idx)

#define FIFO_TYPE 0xff000000
#define FIFO_ADDR 0x00ffffff

#define ROM_VIRGE_325			"roms/video/s3virge/86c325.bin"
#define ROM_DIAMOND_STEALTH3D_2000	"roms/video/s3virge/s3virge.bin"
#define ROM_DIAMOND_STEALTH3D_3000	"roms/video/s3virge/diamondstealth3000.vbi"
#define ROM_VIRGE_DX			"roms/video/s3virge/86c375_1.bin"
#define ROM_DIAMOND_STEALTH3D_2000PRO	"roms/video/s3virge/virgedxdiamond.vbi"
#define ROM_VIRGE_GX			"roms/video/s3virge/86c375_4.bin"
#define ROM_VIRGE_GX2		"roms/video/s3virge/flagpoint.VBI"
#define ROM_DIAMOND_STEALTH3D_4000	"roms/video/s3virge/86c357.bin"
#define ROM_TRIO3D2X			"roms/video/s3virge/TRIO3D2X_8mbsdr.VBI"

enum
{
	S3_VIRGE_325,
	S3_DIAMOND_STEALTH3D_2000,
	S3_DIAMOND_STEALTH3D_3000,
	S3_VIRGE_DX,
	S3_DIAMOND_STEALTH3D_2000PRO,
	S3_VIRGE_GX,
	S3_VIRGE_GX2,
	S3_DIAMOND_STEALTH3D_4000,
	S3_TRIO_3D2X
};

enum
{
	S3_VIRGE,
	S3_VIRGEVX,
	S3_VIRGEDX,
	S3_VIRGEGX2,
	S3_TRIO3D2X
};

enum
{
        FIFO_INVALID     = (0x00 << 24),
        FIFO_WRITE_BYTE  = (0x01 << 24),
        FIFO_WRITE_WORD  = (0x02 << 24),
        FIFO_WRITE_DWORD = (0x03 << 24)
};

typedef struct
{
        uint32_t addr_type;
        uint32_t val;
} fifo_entry_t;

typedef struct s3d_t
{
        uint32_t cmd_set;
        int clip_l, clip_r, clip_t, clip_b;
                
        uint32_t dest_base;
        uint32_t dest_str;
        
        uint32_t z_base;
        uint32_t z_str;

        uint32_t tex_base;
        uint32_t tex_bdr_clr;
        uint32_t tbv, tbu;
        int32_t TdVdX, TdUdX;
        int32_t TdVdY, TdUdY;
        uint32_t tus, tvs;

        int32_t TdZdX, TdZdY;
        uint32_t tzs;

        int32_t TdWdX, TdWdY;
        uint32_t tws;
                
        int32_t TdDdX, TdDdY;
        uint32_t tds;
                
        int16_t TdGdX, TdBdX, TdRdX, TdAdX;
        int16_t TdGdY, TdBdY, TdRdY, TdAdY;
        uint32_t tgs, tbs, trs, tas;
                                
        uint32_t TdXdY12;
        uint32_t txend12;
        uint32_t TdXdY01;
        uint32_t txend01;
        uint32_t TdXdY02;
        uint32_t txs;
        uint32_t tys;
        int ty01, ty12, tlr;

        uint8_t fog_r, fog_g, fog_b;
} s3d_t;


typedef struct {
    uint16_t dblword_read, dblword_write;
	uint8_t datatype;
} VIRGEDMAHeader;

typedef struct virge_t
{
        mem_mapping_t   linear_mapping;
        mem_mapping_t     mmio_mapping;
        mem_mapping_t new_mmio_mapping;
        
        rom_t bios_rom;
        
        svga_t svga;
		
		VIRGEDMAHeader dmahdr;
        
        uint8_t bank;
        uint8_t ma_ext;
	uint8_t reg6b, lfb_bios;

        uint8_t virge_id, virge_id_high, virge_id_low, virge_rev;

	uint8_t int_line;

        uint32_t linear_base, linear_size;

        uint8_t pci_regs[256];
	int card;

	int pci;
        int chip;

        int bilinear_enabled;
        int dithering_enabled;
        uint32_t memory_size;
		uint32_t vram_mask;

        thread_t *render_thread;
        event_t *wake_render_thread;
		event_t *wake_main_thread;
        event_t *not_full_event;

        uint32_t hwc_fg_col, hwc_bg_col;
        int hwc_col_stack_pos;
                        
        struct
        {
                uint32_t src_base;
                uint32_t dest_base;
                int clip_l, clip_r, clip_t, clip_b;
                int dest_str, src_str;
                uint32_t mono_pat_0;
                uint32_t mono_pat_1;
                uint32_t pat_bg_clr;
                uint32_t pat_fg_clr;
                uint32_t src_bg_clr;
                uint32_t src_fg_clr;
                uint32_t cmd_set;
                int r_width, r_height;
                int rsrc_x, rsrc_y;
                int rdest_x, rdest_y;
                
                int lxend0, lxend1;
                int32_t ldx;
                uint32_t lxstart, lystart;
                int lycnt;
                int line_dir;
                
                int src_x, src_y;
                int dest_x, dest_y;
                int w, h;
                uint8_t rop;
                
                int data_left_count;
                uint32_t data_left;
                
                uint32_t pattern_8[8*8];
                uint32_t pattern_16[8*8];
                uint32_t pattern_24[8*8];
                uint32_t pattern_32[8*8];

                uint32_t prdx;
                uint32_t prxstart;
                uint32_t pldx;
                uint32_t plxstart;
                uint32_t pystart;
                uint32_t pycnt;
                uint32_t dest_l, dest_r;
        } s3d;
        
        s3d_t s3d_tri;

        s3d_t s3d_buffer[RB_SIZE];
        int s3d_read_idx, s3d_write_idx;
        int s3d_busy;
		int render_idx;
                
        struct
        {
                uint32_t pri_ctrl;
                uint32_t chroma_ctrl;
                uint32_t sec_ctrl;
                uint32_t chroma_upper_bound;
                uint32_t sec_filter;
                uint32_t blend_ctrl;
                uint32_t pri_fb0, pri_fb1;
                uint32_t pri_stride;
                uint32_t buffer_ctrl;
                uint32_t sec_fb0, sec_fb1;
                uint32_t sec_stride;
                uint32_t overlay_ctrl;
                 int32_t k1_vert_scale;
                 int32_t k2_vert_scale;
                 int32_t dda_vert_accumulator;
                 int32_t k1_horiz_scale;
                 int32_t k2_horiz_scale;
                 int32_t dda_horiz_accumulator;
                uint32_t fifo_ctrl;
                uint32_t pri_start;
                uint32_t pri_size;
                uint32_t sec_start;
                uint32_t sec_size;
                
                int sdif;
                
                int pri_x, pri_y, pri_w, pri_h;
                int sec_x, sec_y, sec_w, sec_h;
        } streams;

		int fifo_slot;
		uint8_t cmd_dma;
		uint8_t dma_bs;
		uint32_t cmd_dma_base;
		uint32_t dma_base_addr;
		uint32_t dma_ptr;
		uint8_t dmabuffer[65536];
		int dmaidx_r, dmaidx_w;
		
		pc_timer_t	tri_timer;
		
        fifo_entry_t fifo[FIFO_SIZE];
        volatile int fifo_read_idx, fifo_write_idx;

        int virge_busy, local;

	uint8_t subsys_stat, subsys_cntl, advfunc_cntl;

	uint8_t render_thread_run, fifo_thread_run;

	uint8_t serialport;

	void *i2c, *ddc;
} virge_t;

static video_timings_t timing_diamond_stealth3d_2000_pci	= {VIDEO_PCI, 2,  2,  3,  28, 28, 45};
static video_timings_t timing_diamond_stealth3d_3000_pci	= {VIDEO_PCI, 2,  2,  4,  26, 26, 42};
static video_timings_t timing_virge_dx_pci			= {VIDEO_PCI, 2,  2,  3,  28, 28, 45};
static video_timings_t timing_virge_agp			= {VIDEO_AGP, 2,  2,  3,  28, 28, 45};

static void s3_virge_triangle(virge_t *virge, s3d_t *s3d_tri);

static void s3_virge_recalctimings(svga_t *svga);
static void s3_virge_updatemapping(virge_t *virge);

static void s3_virge_bitblt(virge_t *virge, int count, uint32_t cpu_dat);

static uint8_t  s3_virge_mmio_read(uint32_t addr, void *p);
static uint16_t s3_virge_mmio_read_w(uint32_t addr, void *p);
static uint32_t s3_virge_mmio_read_l(uint32_t addr, void *p);
static void     s3_virge_mmio_write(uint32_t addr, uint8_t val, void *p);
static void     s3_virge_mmio_write_w(uint32_t addr, uint16_t val, void *p);
static void     s3_virge_mmio_write_l(uint32_t addr, uint32_t val, void *p);

enum
{
        CMD_SET_AE = 1,
        CMD_SET_HC = (1 << 1),
        
        CMD_SET_FORMAT_MASK = (7 << 2),
        CMD_SET_FORMAT_8 = (0 << 2),
        CMD_SET_FORMAT_16 = (1 << 2),
        CMD_SET_FORMAT_24 = (2 << 2),
        
        CMD_SET_MS = (1 << 6),
        CMD_SET_IDS = (1 << 7),
        CMD_SET_MP = (1 << 8),
        CMD_SET_TP = (1 << 9),
        
        CMD_SET_ITA_MASK = (3 << 10),
        CMD_SET_ITA_BYTE = (0 << 10),
        CMD_SET_ITA_WORD = (1 << 10),
        CMD_SET_ITA_DWORD = (2 << 10),
        
        CMD_SET_ZUP = (1 << 23),
        
        CMD_SET_ZB_MODE = (3 << 24),

        CMD_SET_XP = (1 << 25),
        CMD_SET_YP = (1 << 26),
        
        CMD_SET_COMMAND_MASK = (15 << 27)
};

#define CMD_SET_FE         (1 << 17)
#define CMD_SET_ABC_SRC    (1 << 18)
#define CMD_SET_ABC_ENABLE (1 << 19)
#define CMD_SET_TWE        (1 << 26)

enum
{
        CMD_SET_COMMAND_BITBLT = (0 << 27),
        CMD_SET_COMMAND_RECTFILL = (2 << 27),
        CMD_SET_COMMAND_LINE = (3 << 27),
        CMD_SET_COMMAND_POLY = (5 << 27),
        CMD_SET_COMMAND_NOP = (15 << 27)
};

#define INT_VSY      (1 << 0)
#define INT_S3D_DONE (1 << 1)
#define INT_FIFO_OVF (1 << 2)
#define INT_FIFO_EMP (1 << 3)
#define INT_3DF_EMP  (1 << 6)
#define INT_MASK 0xff

#define SERIAL_PORT_SCW (1 << 0)
#define SERIAL_PORT_SDW (1 << 1)
#define SERIAL_PORT_SCR (1 << 2)
#define SERIAL_PORT_SDR (1 << 3)


#ifdef ENABLE_S3_VIRGE_LOG
int s3_virge_do_log = ENABLE_S3_VIRGE_LOG;


static void
s3_virge_log(const char *fmt, ...)
{
    va_list ap;

    if (s3_virge_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define s3_virge_log(fmt, ...)
#endif


static void
s3_virge_tri_timer(void *p)
{
    virge_t *virge = (virge_t *)p;

    thread_set_event(virge->wake_render_thread); /*Wake up FIFO thread if moving from idle*/
}


static void
queue_triangle(virge_t *virge)
{
	if (RB_FULL)
	{
		thread_reset_event(virge->not_full_event);
		thread_reset_event(virge->wake_main_thread);
		if (RB_FULL) {
			thread_wait_event(virge->not_full_event, -1); /*Wait for room in ringbuffer*/
			thread_wait_event(virge->wake_main_thread, -1);
		}
	}
	virge->s3d_buffer[virge->s3d_write_idx & RB_MASK] = virge->s3d_tri;
	virge->s3d_write_idx++;

	if (!virge->s3d_busy) {
		if (!(timer_is_enabled(&virge->tri_timer)))
			timer_set_delay_u64(&virge->tri_timer, 100 * TIMER_USEC);
	}
}

static void
s3_virge_update_irqs(virge_t *virge)
{
        if ((virge->svga.crtc[0x32] & 0x10) && (virge->subsys_stat & (virge->subsys_cntl & INT_MASK)))
                pci_set_irq(virge->card, PCI_INTA);
        else
                pci_clear_irq(virge->card, PCI_INTA);
}


static void
render_thread(void *param)
{
	virge_t *virge = (virge_t *)param;
        
	while (virge->render_thread_run) {
		thread_wait_event(virge->wake_render_thread, -1);
		thread_reset_event(virge->wake_render_thread);
		virge->s3d_busy = 1;
		while (!RB_EMPTY) {
			s3_virge_triangle(virge, &virge->s3d_buffer[virge->s3d_read_idx & RB_MASK]);
			virge->s3d_read_idx++;
			if (RB_ENTRIES == RB_MASK) {
				thread_set_event(virge->not_full_event);
				thread_set_event(virge->wake_main_thread);
			}
		}
		virge->s3d_busy = 0;
		virge->subsys_stat |= INT_S3D_DONE;
		s3_virge_update_irqs(virge);
	}
}


static void s3_virge_out(uint16_t addr, uint8_t val, void *p)
{
        virge_t *virge = (virge_t *)p;
        svga_t *svga = &virge->svga;
        uint8_t old;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
       
        switch (addr)
        {
		case 0x3c5:
                if (svga->seqaddr >= 0x10)
                {
                        svga->seqregs[svga->seqaddr & 0x1f]=val;
                        svga_recalctimings(svga);
                        return;
                }
		if (svga->seqaddr == 4) /*Chain-4 - update banking*/
		{
			if (val & 8)
				svga->write_bank = svga->read_bank = virge->bank << 16;
			else
				svga->write_bank = svga->read_bank = virge->bank << 14;
		} else if (svga->seqaddr == 0x08) {
			svga->seqregs[svga->seqaddr] = val & 0x0f;
			return;
		} else if ((svga->seqaddr == 0x0d) && (svga->seqregs[0x08] == 0x06)) {
			svga->seqregs[svga->seqaddr] = val;
			svga->dpms = (svga->seqregs[0x0d] & 0x50) || (svga->crtc[0x56] & 0x06);
			svga_recalctimings(svga);
			return;
		}
		break;
                
                case 0x3d4:
                svga->crtcreg = val;
                return;
                case 0x3d5:
		if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);		
		if ((svga->crtcreg >= 0x20) && (svga->crtcreg < 0x40) &&
		    (svga->crtcreg != 0x36) && (svga->crtcreg != 0x38) &&
		    (svga->crtcreg != 0x39) && ((svga->crtc[0x38] & 0xcc) != 0x48))
			return;
		if ((svga->crtcreg >= 0x40) && ((svga->crtc[0x39] & 0xe0) != 0xa0))
			return;
		if ((svga->crtcreg == 0x36) && (svga->crtc[0x39] != 0xa5))
			return;
                if (svga->crtcreg >= 0x80)
                        return;
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;
				
				if (svga->crtcreg > 0x18)
					s3_virge_log("OUTB VGA reg = %02x, val = %02x\n", svga->crtcreg, val);
				
                switch (svga->crtcreg)
                {
                        case 0x31:
                        virge->ma_ext = (virge->ma_ext & 0x1c) | ((val & 0x30) >> 4);
                        break;
                        case 0x32:
			s3_virge_update_irqs(virge);
                        break;
                        
                        case 0x69:
                        virge->ma_ext = val & 0x1f;
                        break;
                        
                        case 0x35:
			virge->bank = (virge->bank & 0x70) | (val & 0xf);
			if (svga->chain4)
				svga->write_bank = svga->read_bank = virge->bank << 16;
			else
				svga->write_bank = svga->read_bank = virge->bank << 14;
                        break;
                        case 0x51:
                        virge->bank = (virge->bank & 0x4f) | ((val & 0xc) << 2);
			if (svga->chain4)
				svga->write_bank = svga->read_bank = virge->bank << 16;
			else
				svga->write_bank = svga->read_bank = virge->bank << 14;
                        virge->ma_ext = (virge->ma_ext & ~0xc) | ((val & 3) << 2);
                        break;
                        case 0x6a:
                        virge->bank = val;
			if (svga->chain4)
				svga->write_bank = svga->read_bank = virge->bank << 16;
			else
				svga->write_bank = svga->read_bank = virge->bank << 14;
                        break;
                        
                        case 0x3a:
                        if (val & 0x10)
				svga->gdcreg[5] |= 0x40; /*Horrible cheat*/
                        break;
                        
                        case 0x45:
                        svga->hwcursor.ena = val & 1;
                        break;
                        case 0x46: case 0x47: case 0x48: case 0x49:
                        case 0x4c: case 0x4d: case 0x4e: case 0x4f:
                        svga->hwcursor.x = ((svga->crtc[0x46] << 8) | svga->crtc[0x47]) & 0x7ff;
                        svga->hwcursor.y = ((svga->crtc[0x48] << 8) | svga->crtc[0x49]) & 0x7ff;
                        svga->hwcursor.xoff = svga->crtc[0x4e] & 63;
                        svga->hwcursor.yoff = svga->crtc[0x4f] & 63;
                        svga->hwcursor.addr = ((((svga->crtc[0x4c] << 8) | svga->crtc[0x4d]) & 0xfff) * 1024) + (svga->hwcursor.yoff * 16);
                        break;
                        
                        case 0x4a:
                        switch (virge->hwc_col_stack_pos)
                        {
                                case 0:
                                virge->hwc_fg_col = (virge->hwc_fg_col & 0xffff00) | val;
                                break;
                                case 1:
                                virge->hwc_fg_col = (virge->hwc_fg_col & 0xff00ff) | (val << 8);
                                break;
                                case 2:
                                virge->hwc_fg_col = (virge->hwc_fg_col & 0x00ffff) | (val << 16);
                                break;
                        }
                        virge->hwc_col_stack_pos = (virge->hwc_col_stack_pos + 1) & 3;
                        break;
                        case 0x4b:
                        switch (virge->hwc_col_stack_pos)
                        {
                                case 0:
                                virge->hwc_bg_col = (virge->hwc_bg_col & 0xffff00) | val;
                                break;
                                case 1:
                                virge->hwc_bg_col = (virge->hwc_bg_col & 0xff00ff) | (val << 8);
                                break;
                                case 2:
                                virge->hwc_bg_col = (virge->hwc_bg_col & 0x00ffff) | (val << 16);
                                break;
                        }
                        virge->hwc_col_stack_pos = (virge->hwc_col_stack_pos + 1) & 3;
                        break;

                        case 0x53:
                        case 0x58: case 0x59: case 0x5a:
                        s3_virge_updatemapping(virge);
                        break;

                        case 0x56:
                        svga->dpms = (svga->seqregs[0x0d] & 0x50) || (svga->crtc[0x56] & 0x06);
                        old = ~val; /* force recalc */
                        break;
                        
						case 0x5c:
						if ((val & 0xa0) == 0x80)
							i2c_gpio_set(virge->i2c, !!(val & 0x40), !!(val & 0x10));
						break;
						
                        case 0x67:
                        switch (val >> 4)
                        {
                                case 2: case 3: svga->bpp = 15; break;
                                case 4: case 5: svga->bpp = 16; break;
                                case 7:  svga->bpp = 24; break;
                                case 13: svga->bpp = (virge->chip == S3_VIRGEVX) ? 24 : 32; break;
                                default: svga->bpp = 8;  break;
                        }
                        break;
						
                        case 0xaa:
                        i2c_gpio_set(virge->i2c, !!(val & SERIAL_PORT_SCW), !!(val & SERIAL_PORT_SDW));
                        break;
                }
                if (old != val)
                {
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
				if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                                	svga->fullchange = 3;
					svga->ma_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
						if ((svga->crtc[0x67] & 0xc) != 0xc)
							svga->ma_latch |= (virge->ma_ext << 16);
				} else {
					svga->fullchange = changeframecount;
	                                svga_recalctimings(svga);
				}
                        }
                }
                break;
        }
        svga_out(addr, val, svga);
}

static uint8_t s3_virge_in(uint16_t addr, void *p)
{
        virge_t *virge = (virge_t *)p;
        svga_t *svga = &virge->svga;
	uint8_t ret;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3c1:
                if (svga->attraddr > 0x14)
                        ret = 0xff;
                else
                        ret = svga_in(addr, svga);
                break; 

                case 0x3c5:
                if (svga->seqaddr >= 8)
                        ret = svga->seqregs[svga->seqaddr & 0x1f];
                else if (svga->seqaddr <= 4)
                        ret = svga_in(addr, svga);
                else
                        ret = 0xff;
                break;

                case 0x3D4:
                ret = svga->crtcreg;
		break;
                case 0x3D5:
                switch (svga->crtcreg)
                {
                        case 0x2d: ret = virge->virge_id_high; break; /*Extended chip ID*/
                        case 0x2e: ret = virge->virge_id_low;  break; /*New chip ID*/
                        case 0x2f: ret = virge->virge_rev;     break; 
                        case 0x30: ret = virge->virge_id;      break; /*Chip ID*/
                        case 0x31: ret = (svga->crtc[0x31] & 0xcf) | ((virge->ma_ext & 3) << 4); break;
						case 0x33: ret = (svga->crtc[0x33] | 0x04); break;
                        case 0x35: ret = (svga->crtc[0x35] & 0xf0) | (virge->bank & 0xf); break;
						case 0x36: 
							ret = svga->crtc[0x36];
							if (virge->chip == S3_TRIO3D2X) {
								ret |= 0x40; /*Needed to get around glitches under Windows 95 using 1280x1024 24-bit.*/
							}
							break;
                        case 0x45: virge->hwc_col_stack_pos = 0; ret = svga->crtc[0x45]; break;
                        case 0x51: ret = (svga->crtc[0x51] & 0xf0) | ((virge->bank >> 2) & 0xc) | ((virge->ma_ext >> 2) & 3); break;
						case 0x5c:	/* General Output Port Register */
							ret = svga->crtc[svga->crtcreg] & 0xa0;
							if (((svga->miscout >> 2) & 3) == 3)
								ret |= svga->crtc[0x42] & 0x0f;
							else
								ret |= ((svga->miscout >> 2) & 3);
							if ((ret & 0xa0) == 0xa0) {
								if ((svga->crtc[0x5c] & 0x40) && i2c_gpio_get_scl(virge->i2c))
									ret |= 0x40;
								if ((svga->crtc[0x5c] & 0x10) && i2c_gpio_get_sda(virge->i2c))
									ret |= 0x10;
							}
							break;
                        case 0x69: ret = virge->ma_ext; break;
                        case 0x6a: ret = virge->bank; break;
                        case 0xaa: /* DDC */
                        if (virge->chip >= S3_VIRGEGX2) {
                                ret = svga->crtc[0xaa] & ~(SERIAL_PORT_SCR | SERIAL_PORT_SDR);
                                if ((svga->crtc[0xaa] & SERIAL_PORT_SCW) && i2c_gpio_get_scl(virge->i2c))
                                        ret |= SERIAL_PORT_SCR;
                                if ((svga->crtc[0xaa] & SERIAL_PORT_SDW) && i2c_gpio_get_sda(virge->i2c))
                                        ret |= SERIAL_PORT_SDR;
                                break;
                        } else
							ret = svga->crtc[0xaa];
						break;
                        
						default:
							ret = svga->crtc[svga->crtcreg];
							break;
                }
                break;
		
                default:
                ret = svga_in(addr, svga);
                break; 
        }
        return ret;
}

static void s3_virge_recalctimings(svga_t *svga)
{
        virge_t *virge = (virge_t *)svga->p;
	
	svga->hdisp = svga->hdisp_old;

	if (svga->crtc[0x5d] & 0x01) svga->htotal     += 0x100;
	if (svga->crtc[0x5d] & 0x02) {
		svga->hdisp_time += 0x100;
		svga->hdisp += 0x100 * ((svga->seqregs[1] & 8) ? 16 : 8);
	}
        if (svga->crtc[0x5e] & 0x01) svga->vtotal      += 0x400;
        if (svga->crtc[0x5e] & 0x02) svga->dispend     += 0x400;
        if (svga->crtc[0x5e] & 0x04) svga->vblankstart += 0x400;
        if (svga->crtc[0x5e] & 0x10) svga->vsyncstart  += 0x400;
        if (svga->crtc[0x5e] & 0x40) svga->split       += 0x400;
        svga->interlace = svga->crtc[0x42] & 0x20;

        if (((svga->miscout >> 2) & 3) == 3) {
                int n = svga->seqregs[0x12] & 0x1f;
                int r = (svga->seqregs[0x12] >> 5);
                
		if (virge->chip == S3_VIRGEVX || virge->chip == S3_VIRGEDX)	
			r &= 7;
		else if (virge->chip >= S3_VIRGEGX2)
			r &= 10;
		else
			r &= 3;
		
		int m = svga->seqregs[0x13] & 0x7f;
                double freq = (((double)m + 2) / (((double)n + 2) * (double)(1 << r))) * 14318184.0;

                svga->clock = (cpuclock * (float)(1ull << 32)) / freq;
        }

        if ((svga->crtc[0x67] & 0xc) != 0xc) /*VGA mode*/
        {
                svga->ma_latch |= (virge->ma_ext << 16);
                if (svga->crtc[0x51] & 0x30)      svga->rowoffset += (svga->crtc[0x51] & 0x30) << 4;
                else if (svga->crtc[0x43] & 0x04) svga->rowoffset += 0x100;
                if (!svga->rowoffset) svga->rowoffset = 256;
		
		svga->lowres = !((svga->gdcreg[5] & 0x40) && (svga->crtc[0x3a] & 0x10));
                if ((svga->gdcreg[5] & 0x40) && (svga->crtc[0x3a] & 0x10)) {
                        switch (svga->bpp) {
                                case 8: 
                                svga->render = svga_render_8bpp_highres; 
                                break;
                                case 15: 
                                svga->render = svga_render_15bpp_highres;
				if (virge->chip != S3_VIRGEVX && virge->chip < S3_VIRGEGX2)
				{
					svga->htotal >>= 1;
					svga->hdisp >>= 1;
				}
                                break;
                                case 16: 
                                svga->render = svga_render_16bpp_highres;
				if (virge->chip != S3_VIRGEVX && virge->chip < S3_VIRGEGX2)
				{
					svga->htotal >>= 1;
					svga->hdisp >>= 1;					
				}
                                break;
                                case 24:
                                svga->render = svga_render_24bpp_highres;
				if (virge->chip != S3_VIRGEVX && virge->chip < S3_VIRGEGX2)
					svga->rowoffset = (svga->rowoffset * 3) / 4; /*Hack*/
                                break;
                                case 32: 
                                svga->render = svga_render_32bpp_highres;				
                                break;
                        }
                }
		svga->vram_display_mask = (!(svga->crtc[0x31] & 0x08) && (svga->crtc[0x32] & 0x40)) ? 0x3ffff : virge->vram_mask;
        }
        else /*Streams mode*/
        {
                if (virge->streams.buffer_ctrl & 1)
                        svga->ma_latch = virge->streams.pri_fb1 >> 2;
                else
                        svga->ma_latch = virge->streams.pri_fb0 >> 2;
                        
                svga->hdisp = virge->streams.pri_w + 1;
                if (virge->streams.pri_h < svga->dispend)
                        svga->dispend = virge->streams.pri_h;
                
                svga->overlay.x = virge->streams.sec_x - virge->streams.pri_x;
                svga->overlay.y = virge->streams.sec_y - virge->streams.pri_y;
                svga->overlay.ysize = virge->streams.sec_h;

                if (virge->streams.buffer_ctrl & 2)
                        svga->overlay.addr = virge->streams.sec_fb1;
                else
                        svga->overlay.addr = virge->streams.sec_fb0;

                svga->overlay.ena = (svga->overlay.x >= 0);
                svga->overlay.v_acc = virge->streams.dda_vert_accumulator;
                svga->rowoffset = virge->streams.pri_stride >> 3;

                switch ((virge->streams.pri_ctrl >> 24) & 0x7)
                {
                        case 0: /*RGB-8 (CLUT)*/
                        svga->render = svga_render_8bpp_highres; 
                        break;
                        case 3: /*KRGB-16 (1.5.5.5)*/ 
                        svga->htotal >>= 1;
                        svga->render = svga_render_15bpp_highres; 
                        break;
                        case 5: /*RGB-16 (5.6.5)*/ 
                        svga->htotal >>= 1;
                        svga->render = svga_render_16bpp_highres; 
                        break;
                        case 6: /*RGB-24 (8.8.8)*/ 
                        svga->render = svga_render_24bpp_highres; 
                        break;
                        case 7: /*XRGB-32 (X.8.8.8)*/
                        svga->render = svga_render_32bpp_highres; 
                        break;
                }
		svga->vram_display_mask = virge->vram_mask;
        }
}

static void s3_virge_updatemapping(virge_t *virge)
{
        svga_t *svga = &virge->svga;

        if (!(virge->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))
        {
                mem_mapping_disable(&svga->mapping);
                mem_mapping_disable(&virge->linear_mapping);
                mem_mapping_disable(&virge->mmio_mapping);
                mem_mapping_disable(&virge->new_mmio_mapping);
                return;
        }

        s3_virge_log("Update mapping - bank %02X ", svga->gdcreg[6] & 0xc);        
	/*Banked framebuffer*/
	switch (svga->gdcreg[6] & 0xc) { /*VGA mapping*/
		case 0x0: /*128k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
		svga->banked_mask = 0xffff;
		break;
		case 0x4: /*64k at A0000*/
		mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
		svga->banked_mask = 0xffff;
		break;
		case 0x8: /*32k at B0000*/
		mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
		svga->banked_mask = 0x7fff;
		break;
		case 0xC: /*32k at B8000*/
		mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
		svga->banked_mask = 0x7fff;
		break;
	}
        
	virge->linear_base = (svga->crtc[0x5a] << 16) | (svga->crtc[0x59] << 24);

        s3_virge_log("Linear framebuffer %02X, linear base = %08x\n", svga->crtc[0x58] & 0x10, virge->linear_base);
        if ((svga->crtc[0x58] & 0x10) || (virge->advfunc_cntl & 0x10)) { /*Linear framebuffer*/
                switch (svga->crtc[0x58] & 7) {
                        case 0: /*64k*/
                        virge->linear_size = 0x10000;
                        break;
                        case 1: /*1mb*/
                        virge->linear_size = 0x100000;
                        break;
                        case 2: /*2mb*/
                        virge->linear_size = 0x200000;
                        break;
                        case 3: /*4mb on other than ViRGE/VX, 8mb on ViRGE/VX*/
						if (virge->chip == S3_VIRGEVX)
							virge->linear_size = 0x800000;
						else
							virge->linear_size = 0x400000;
                        break;
						case 7:
						virge->linear_size = 0x400000;
						break;
                }
                virge->linear_base &= ~(virge->linear_size - 1);
                s3_virge_log("Linear framebuffer at %08X size %08X, mask = %08x, CRTC58 sel = %02x\n", virge->linear_base, virge->linear_size, virge->vram_mask, svga->crtc[0x58] & 7);
                if (virge->linear_base == 0xa0000) {
			mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
			mem_mapping_disable(&virge->linear_mapping);
                } else {
					mem_mapping_set_addr(&virge->linear_mapping, virge->linear_base, virge->linear_size);
				}
			svga->fb_only = 1;
        } else {
                mem_mapping_disable(&virge->linear_mapping);
                svga->fb_only = 0;
        }
        
        s3_virge_log("Memory mapped IO %02X\n", svga->crtc[0x53] & 0x28);

	/* Memory mapped I/O. */
	/* Old MMIO. */
	if ((svga->crtc[0x53] & 0x10) || (virge->advfunc_cntl & 0x20)) {
		if (svga->crtc[0x53] & 0x20)
			mem_mapping_set_addr(&virge->mmio_mapping, 0xb8000, 0x8000);
		else
			mem_mapping_set_addr(&virge->mmio_mapping, 0xa0000, 0x10000);
	} else
		mem_mapping_disable(&virge->mmio_mapping);

	/* New MMIO. */
	if (svga->crtc[0x53] & 0x08)
		mem_mapping_set_addr(&virge->new_mmio_mapping, virge->linear_base + 0x1000000, 0x10000);
	else
		mem_mapping_disable(&virge->new_mmio_mapping);
}

static void s3_virge_vblank_start(svga_t *svga)
{
        virge_t *virge = (virge_t *)svga->p;

        virge->subsys_stat |= INT_VSY;
        s3_virge_update_irqs(virge);
}

static uint8_t
s3_virge_mmio_read(uint32_t addr, void *p)
{
        virge_t *virge = (virge_t *)p;
        uint8_t ret = 0xff;

		s3_virge_log("MMIO ReadB addr = %04x\n", addr & 0xffff);

        switch (addr & 0xffff)
        {
                case 0x8505:
				ret = 0;
                if (virge->s3d_busy || virge->fifo_slot) {
						ret = 0x10;
                } else {
						ret = 0x30;
				}
				if (virge->fifo_slot)
					virge->fifo_slot--;
                return ret;
                
                case 0x83b0: case 0x83b1: case 0x83b2: case 0x83b3:
                case 0x83b4: case 0x83b5: case 0x83b6: case 0x83b7:
                case 0x83b8: case 0x83b9: case 0x83ba: case 0x83bb:
                case 0x83bc: case 0x83bd: case 0x83be: case 0x83bf:
                case 0x83c0: case 0x83c1: case 0x83c2: case 0x83c3:
                case 0x83c4: case 0x83c5: case 0x83c6: case 0x83c7:
                case 0x83c8: case 0x83c9: case 0x83ca: case 0x83cb:
                case 0x83cc: case 0x83cd: case 0x83ce: case 0x83cf:
                case 0x83d0: case 0x83d1: case 0x83d2: case 0x83d3:
                case 0x83d4: case 0x83d5: case 0x83d6: case 0x83d7:
                case 0x83d8: case 0x83d9: case 0x83da: case 0x83db:
                case 0x83dc: case 0x83dd: case 0x83de: case 0x83df:
		return s3_virge_in(addr & 0x3ff, virge);
		
				case 0x859c:
				return virge->cmd_dma;
		
                case 0xff20: case 0xff21:
                ret = virge->serialport & ~(SERIAL_PORT_SCR | SERIAL_PORT_SDR);
                if ((virge->serialport & SERIAL_PORT_SCW) && i2c_gpio_get_scl(virge->i2c))
                        ret |= SERIAL_PORT_SCR;
                if ((virge->serialport & SERIAL_PORT_SDW) && i2c_gpio_get_sda(virge->i2c))
                        ret |= SERIAL_PORT_SDR;
                return ret;
        }
        return 0xff;
}
static uint16_t
s3_virge_mmio_read_w(uint32_t addr, void *p)
{
	virge_t *virge = (virge_t *)p;
	
	s3_virge_log("MMIO ReadW addr = %04x\n", addr & 0xfffc);
	
	switch (addr & 0xfffe) {
		case 0x859c:
		return virge->cmd_dma;
		
		default:
		return s3_virge_mmio_read(addr, virge) |
			(s3_virge_mmio_read(addr + 1, virge) << 8);
	}

	return 0xffff;
}

static uint32_t
s3_virge_mmio_read_l(uint32_t addr, void *p)
{
        virge_t *virge = (virge_t *)p;
		VIRGEDMAHeader *dmahdr = &virge->dmahdr;
        uint32_t ret = 0xffffffff;

	switch (addr & 0xfffc) {
		case 0x8180:
		ret = virge->streams.pri_ctrl;
		break;
		case 0x8184:
		ret = virge->streams.chroma_ctrl;
		break;
		case 0x8190:
		ret = virge->streams.sec_ctrl;
		break;
		case 0x8194:
		ret = virge->streams.chroma_upper_bound;
		break;
		case 0x8198:
		ret = virge->streams.sec_filter;
		break;
		case 0x81a0:
		ret = virge->streams.blend_ctrl;
		break;
		case 0x81c0:
		ret = virge->streams.pri_fb0;
		break;
		case 0x81c4:
		ret = virge->streams.pri_fb1;
		break;
		case 0x81c8:
		ret = virge->streams.pri_stride;
		break;
		case 0x81cc:
		ret = virge->streams.buffer_ctrl;
		break;
		case 0x81d0:
		ret = virge->streams.sec_fb0;
		break;
		case 0x81d4:
		ret = virge->streams.sec_fb1;
		break;
		case 0x81d8:
		ret = virge->streams.sec_stride;
		break;
		case 0x81dc:
		ret = virge->streams.overlay_ctrl;
		break;
		case 0x81e0:
		ret = virge->streams.k1_vert_scale;
		break;
		case 0x81e4:
		ret = virge->streams.k2_vert_scale;
		break;
		case 0x81e8:
		ret = virge->streams.dda_vert_accumulator;
		break;
		case 0x81ec:
		ret = virge->streams.fifo_ctrl;
		break;
		case 0x81f0:
		ret = virge->streams.pri_start;
		break;
		case 0x81f4:
		ret = virge->streams.pri_size;
		break;
		case 0x81f8:
		ret = virge->streams.sec_start;
		break;
		case 0x81fc:
		ret = virge->streams.sec_size;
		break;	

		case 0x8504:
		if (virge->s3d_busy || virge->fifo_slot) {
			ret = (0x10 << 8);
		} else {
			ret = (0x10 << 8) | (1 << 13);
			if (!virge->s3d_busy)
				virge->subsys_stat |= INT_3DF_EMP;
			if (!virge->fifo_slot)
				virge->subsys_stat |= INT_FIFO_EMP;
		}
		ret |= virge->subsys_stat;
		if (virge->fifo_slot)
			virge->fifo_slot--;
		s3_virge_update_irqs(virge);
		dmahdr->dblword_read = 0;
		dmahdr->dblword_write = 0;
		break;
		
		case 0x8590:
		ret = virge->cmd_dma_base;
		break;
		case 0x8594:
		break;
		case 0x8598:
		ret = virge->dma_ptr;
		break;
		case 0x859c:
		ret = virge->cmd_dma;
		break;
		
		case 0xa4d4:
		ret = virge->s3d.src_base;
		break;
		case 0xa4d8:
		ret = virge->s3d.dest_base;
		break;
		case 0xa4dc:
		ret = (virge->s3d.clip_l << 16) | virge->s3d.clip_r;
		break;
		case 0xa4e0:
		ret = (virge->s3d.clip_t << 16) | virge->s3d.clip_b;
		break;
		case 0xa4e4:
		ret = (virge->s3d.dest_str << 16) | virge->s3d.src_str;
		break;
		case 0xa4e8: case 0xace8:
		ret = virge->s3d.mono_pat_0;
		break;
		case 0xa4ec: case 0xacec:
		ret = virge->s3d.mono_pat_1;
		break;
		case 0xa4f0:
		ret = virge->s3d.pat_bg_clr;
		break;
		case 0xa4f4:
		ret = virge->s3d.pat_fg_clr;
		break;
		case 0xa4f8:
		ret = virge->s3d.src_bg_clr;
		break;
		case 0xa4fc:
		ret = virge->s3d.src_fg_clr;
		break;
		case 0xa500:
		ret = virge->s3d.cmd_set;
		break;
		case 0xa504:
		ret = (virge->s3d.r_width << 16) | virge->s3d.r_height;
		break;
		case 0xa508:
		ret = (virge->s3d.rsrc_x << 16) | virge->s3d.rsrc_y;
		break;
		case 0xa50c:
		ret = (virge->s3d.rdest_x << 16) | virge->s3d.rdest_y;
		break;
		
		default:
		ret = s3_virge_mmio_read(addr, virge) |
			(s3_virge_mmio_read(addr + 1, virge) << 8) |
			(s3_virge_mmio_read(addr + 2, virge) << 16) | 
			(s3_virge_mmio_read(addr + 3, virge) << 24);
		break;
	}

	if ((((addr & 0xfffc) >= 0xa000) && ((addr & 0xfffc) < 0xc000))) {
		if (virge->cmd_dma) {
			dmahdr->dblword_read++;
			virge->dmabuffer[virge->dmaidx_r] = dmahdr->dblword_read & 0xff; /*0-7*/
			virge->dmabuffer[virge->dmaidx_r + 1] = dmahdr->dblword_read << 8; /*8-15*/
			if (dmahdr->dblword_read == 1) {
				virge->dmabuffer[virge->dmaidx_r + 2] = ret & 0xff; /*16-23*/
				virge->dmabuffer[virge->dmaidx_r + 3] = ((ret >> 8) & 0x3f); /*24-31*/
			}
			virge->dmaidx_r += 4;
			if (virge->dma_bs) {
				if (virge->dmaidx_r == 65536)
					virge->dmaidx_r = 0;
			} else {
				if (virge->dmaidx_r == 4096)
					virge->dmaidx_r = 0;						
			}
			dma_bm_write(virge->dma_base_addr + virge->dma_ptr, virge->dmabuffer, (virge->dma_bs) ? 65536 : 4096, 4);
		}
	}

	s3_virge_log("MMIO ReadL addr = %04x, val = %08x\n", addr & 0xfffc, ret);
	return ret;
}


static void s3_virge_mmio_write(uint32_t addr, uint8_t val, void *p)
{
        virge_t *virge = (virge_t *)p;
	s3_virge_log("MMIO WriteB addr = %04x, val = %02x\n", addr & 0xffff, val);

	if ((addr & 0xffff) < 0x8000) {
		s3_virge_bitblt(virge, 8, val);
	} else {
		switch (addr & 0xffff) {
			case 0x83b0: case 0x83b1: case 0x83b2: case 0x83b3:
			case 0x83b4: case 0x83b5: case 0x83b6: case 0x83b7:
			case 0x83b8: case 0x83b9: case 0x83ba: case 0x83bb:
			case 0x83bc: case 0x83bd: case 0x83be: case 0x83bf:
			case 0x83c0: case 0x83c1: case 0x83c2: case 0x83c3:
			case 0x83c4: case 0x83c5: case 0x83c6: case 0x83c7:
			case 0x83c8: case 0x83c9: case 0x83ca: case 0x83cb:
			case 0x83cc: case 0x83cd: case 0x83ce: case 0x83cf:
			case 0x83d0: case 0x83d1: case 0x83d2: case 0x83d3:
			case 0x83d4: case 0x83d5: case 0x83d6: case 0x83d7:
			case 0x83d8: case 0x83d9: case 0x83da: case 0x83db:
			case 0x83dc: case 0x83dd: case 0x83de: case 0x83df:
				s3_virge_out(addr & 0x3ff, val, virge);
				break;
			
			case 0x859c:
			virge->cmd_dma = val;
			break;
			
			case 0xff20:
			virge->serialport = val;
			i2c_gpio_set(virge->i2c, !!(val & SERIAL_PORT_SCW), !!(val & SERIAL_PORT_SDW));
			break;
		}	
	}
}

static void
s3_virge_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
        virge_t *virge = (virge_t *)p;
	s3_virge_log("MMIO WriteW addr = %04x, val = %04x\n", addr & 0xfffe, val);
	if ((addr & 0xfffe) < 0x8000) {
		if (virge->s3d.cmd_set & CMD_SET_MS)
				s3_virge_bitblt(virge, 16, ((val >> 8) | (val << 8)) << 16);
		else
				s3_virge_bitblt(virge, 16, val);
	} else {
		if ((addr & 0xfffe) == 0x83d4) {
			s3_virge_mmio_write(addr, val, virge);
			s3_virge_mmio_write(addr + 1, val >> 8, virge);
		} else if ((addr & 0xfffe) == 0x859c) {
			virge->cmd_dma = val;
		} else if ((addr & 0xfffe) == 0xff20)
			s3_virge_mmio_write(addr, val, virge);
	}
}

static void
s3_virge_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
        virge_t *virge = (virge_t *)p;
	svga_t *svga = &virge->svga;
	VIRGEDMAHeader *dmahdr = &virge->dmahdr;

	s3_virge_log("MMIO WriteL addr = %04x, val = %08x\n", addr & 0xfffc, val);
        if ((addr & 0xfffc) < 0x8000) {
			if (virge->s3d.cmd_set & CMD_SET_MS)
				s3_virge_bitblt(virge, 32, ((val & 0xff000000) >> 24) | ((val & 0x00ff0000) >> 8) | ((val & 0x0000ff00) << 8) | ((val & 0x000000ff) << 24));
			else
				s3_virge_bitblt(virge, 32, val);

			if (virge->cmd_dma) {
				dmahdr->datatype = 1;
			}
        } else {
		if ((addr & 0xfffc) >= 0xa000)
			virge->fifo_slot++;
		if (virge->cmd_dma) {
			dmahdr->datatype = 0;
		}
		switch (addr & 0xfffc) {
			case 0x8180:
			virge->streams.pri_ctrl = val;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x8184:
			virge->streams.chroma_ctrl = val;
			break;
			case 0x8190:
			virge->streams.sec_ctrl = val;
			virge->streams.dda_horiz_accumulator = val & 0xfff;
			if (val & (1 << 11))
				virge->streams.dda_horiz_accumulator |= 0xfffff800;
			virge->streams.sdif = (val >> 24) & 7;
			break;
			case 0x8194:
			virge->streams.chroma_upper_bound = val;
			break;
			case 0x8198:
			virge->streams.sec_filter = val;
			virge->streams.k1_horiz_scale = val & 0x7ff;
			if (val & (1 << 10))
				virge->streams.k1_horiz_scale |= 0xfffff800;
			virge->streams.k2_horiz_scale = (val >> 16) & 0x7ff;
			if ((val >> 16) & (1 << 10))
				virge->streams.k2_horiz_scale |= 0xfffff800;
			break;
			case 0x81a0:
			virge->streams.blend_ctrl = val;
			break;
			case 0x81c0:
			virge->streams.pri_fb0 = val & 0x3fffff;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81c4:
			virge->streams.pri_fb1 = val & 0x3fffff;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81c8:
			virge->streams.pri_stride = val & 0xfff;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81cc:
			virge->streams.buffer_ctrl = val;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81d0:
			virge->streams.sec_fb0 = val;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81d4:
			virge->streams.sec_fb1 = val;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81d8:
			virge->streams.sec_stride = val;
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81dc:
			virge->streams.overlay_ctrl = val;
			break;
			case 0x81e0:
			virge->streams.k1_vert_scale = val & 0x7ff;
			if (val & (1 << 10))
				virge->streams.k1_vert_scale |= 0xfffff800;
			break;
			case 0x81e4:
			virge->streams.k2_vert_scale = val & 0x7ff;
			if (val & (1 << 10))
				virge->streams.k2_vert_scale |= 0xfffff800;
			break;
			case 0x81e8:
			virge->streams.dda_vert_accumulator = val & 0xfff;
			if (val & (1 << 11))
				virge->streams.dda_vert_accumulator |= 0xfffff800;
			break;
			case 0x81ec:
			virge->streams.fifo_ctrl = val;
			break;
			case 0x81f0:
			virge->streams.pri_start = val;
			virge->streams.pri_x = (val >> 16) & 0x7ff;
			virge->streams.pri_y = val & 0x7ff;                
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81f4:
			virge->streams.pri_size = val;
			virge->streams.pri_w = (val >> 16) & 0x7ff;
			virge->streams.pri_h = val & 0x7ff;                
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81f8:
			virge->streams.sec_start = val;
			virge->streams.sec_x = (val >> 16) & 0x7ff;
			virge->streams.sec_y = val & 0x7ff;                
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			case 0x81fc:
			virge->streams.sec_size = val;
			virge->streams.sec_w = (val >> 16) & 0x7ff;
			virge->streams.sec_h = val & 0x7ff;                
			svga_recalctimings(svga);
			svga->fullchange = changeframecount;
			break;
			
			case 0x8504:
			virge->subsys_stat &= ~(val & 0xff);
			virge->subsys_cntl = (val >> 8);
			s3_virge_update_irqs(virge);
			break;
			
			case 0x850c:
			virge->advfunc_cntl = val & 0xff;
			s3_virge_updatemapping(virge);
			break;
			
			case 0x8590:
			virge->cmd_dma_base = val;
			virge->dma_bs = !!(val & 2);
			if (virge->dma_bs)
				virge->dma_base_addr = val & 0xffff0000;
			else
				virge->dma_base_addr = val & 0xfffff000;
			break;
			
			case 0x8594:
			virge->dma_ptr = val;
			break;

			case 0x8598:
			break;

			case 0x859c:
			virge->cmd_dma = val;
			break;
	
			case 0xa000: case 0xa004: case 0xa008: case 0xa00c:
			case 0xa010: case 0xa014: case 0xa018: case 0xa01c:
			case 0xa020: case 0xa024: case 0xa028: case 0xa02c:
			case 0xa030: case 0xa034: case 0xa038: case 0xa03c:
			case 0xa040: case 0xa044: case 0xa048: case 0xa04c:
			case 0xa050: case 0xa054: case 0xa058: case 0xa05c:
			case 0xa060: case 0xa064: case 0xa068: case 0xa06c:
			case 0xa070: case 0xa074: case 0xa078: case 0xa07c:
			case 0xa080: case 0xa084: case 0xa088: case 0xa08c:
			case 0xa090: case 0xa094: case 0xa098: case 0xa09c:
			case 0xa0a0: case 0xa0a4: case 0xa0a8: case 0xa0ac:
			case 0xa0b0: case 0xa0b4: case 0xa0b8: case 0xa0bc:
			case 0xa0c0: case 0xa0c4: case 0xa0c8: case 0xa0cc:
			case 0xa0d0: case 0xa0d4: case 0xa0d8: case 0xa0dc:
			case 0xa0e0: case 0xa0e4: case 0xa0e8: case 0xa0ec:
			case 0xa0f0: case 0xa0f4: case 0xa0f8: case 0xa0fc:
			case 0xa100: case 0xa104: case 0xa108: case 0xa10c:
			case 0xa110: case 0xa114: case 0xa118: case 0xa11c:
			case 0xa120: case 0xa124: case 0xa128: case 0xa12c:
			case 0xa130: case 0xa134: case 0xa138: case 0xa13c:
			case 0xa140: case 0xa144: case 0xa148: case 0xa14c:
			case 0xa150: case 0xa154: case 0xa158: case 0xa15c:
			case 0xa160: case 0xa164: case 0xa168: case 0xa16c:
			case 0xa170: case 0xa174: case 0xa178: case 0xa17c:
			case 0xa180: case 0xa184: case 0xa188: case 0xa18c:
			case 0xa190: case 0xa194: case 0xa198: case 0xa19c:
			case 0xa1a0: case 0xa1a4: case 0xa1a8: case 0xa1ac:
			case 0xa1b0: case 0xa1b4: case 0xa1b8: case 0xa1bc:
			case 0xa1c0: case 0xa1c4: case 0xa1c8: case 0xa1cc:
			case 0xa1d0: case 0xa1d4: case 0xa1d8: case 0xa1dc:
			case 0xa1e0: case 0xa1e4: case 0xa1e8: case 0xa1ec:
			case 0xa1f0: case 0xa1f4: case 0xa1f8: case 0xa1fc:
			{
				int x = addr & 4;
				int y = (addr >> 3) & 7;
				int color, xx;
				int byte;
				virge->s3d.pattern_8[y*8 + x]     = val & 0xff;
				virge->s3d.pattern_8[y*8 + x + 1] = val >> 8;
				virge->s3d.pattern_8[y*8 + x + 2] = val >> 16;
				virge->s3d.pattern_8[y*8 + x + 3] = val >> 24;
				
				x = (addr >> 1) & 6;
				y = (addr >> 4) & 7;
				virge->s3d.pattern_16[y*8 + x]     = val & 0xffff;
				virge->s3d.pattern_16[y*8 + x + 1] = val >> 16;

				addr &= 0x00ff;
				for (xx = 0; xx < 4; xx++) {
					x = ((addr + xx) / 3) % 8;
					y = ((addr + xx) / 24) % 8;
					color = ((addr + xx) % 3) << 3;
					byte = (xx << 3);
					virge->s3d.pattern_24[y*8 + x] &= ~(0xff << color);
					virge->s3d.pattern_24[y*8 + x] |= ((val >> byte) & 0xff) << color;
				}

				x = (addr >> 2) & 7;
				y = (addr >> 5) & 7;
				virge->s3d.pattern_32[y*8 + x] = val & 0xffffff;
			}
			break;

			case 0xa4d4: case 0xa8d4:
			virge->s3d.src_base = val & 0x3ffff8;
			break;
			case 0xa4d8: case 0xa8d8:
			virge->s3d.dest_base = val & 0x3ffff8;
			break;
			case 0xa4dc: case 0xa8dc:
			virge->s3d.clip_l = (val >> 16) & 0x7ff;
			virge->s3d.clip_r = val & 0x7ff;
			break;
			case 0xa4e0: case 0xa8e0:
			virge->s3d.clip_t = (val >> 16) & 0x7ff;
			virge->s3d.clip_b = val & 0x7ff;
			break;
			case 0xa4e4: case 0xa8e4:
			virge->s3d.dest_str = (val >> 16) & 0xff8;
			virge->s3d.src_str = val & 0xff8;
			break;
			case 0xa4e8: case 0xace8:
			virge->s3d.mono_pat_0 = val;
			break;
			case 0xa4ec: case 0xacec:
			virge->s3d.mono_pat_1 = val;
			break;
			case 0xa4f0: case 0xacf0:
			virge->s3d.pat_bg_clr = val;
			break;
			case 0xa4f4: case 0xa8f4: case 0xacf4:
			virge->s3d.pat_fg_clr = val;
			break;
			case 0xa4f8:
			virge->s3d.src_bg_clr = val;
			break;
			case 0xa4fc:
			virge->s3d.src_fg_clr = val;
			break;
			case 0xa500: case 0xa900:
			virge->s3d.cmd_set = val;
			if (!(val & CMD_SET_AE)) {
				s3_virge_bitblt(virge, -1, 0);
			}
			break;
			case 0xa504:
			virge->s3d.r_width = (val >> 16) & 0x7ff;
			virge->s3d.r_height = val & 0x7ff;
			break;
			case 0xa508:
			virge->s3d.rsrc_x = (val >> 16) & 0x7ff;
			virge->s3d.rsrc_y = val & 0x7ff;
			break;
			case 0xa50c:
			virge->s3d.rdest_x = (val >> 16) & 0x7ff;
			virge->s3d.rdest_y = val & 0x7ff;
			if (virge->s3d.cmd_set & CMD_SET_AE) {
				s3_virge_bitblt(virge, -1, 0);
			}
			break;
			case 0xa96c:
			virge->s3d.lxend0 = (val >> 16) & 0x7ff;
			virge->s3d.lxend1 = val & 0x7ff;
			break;
			case 0xa970:
			virge->s3d.ldx = (int32_t)val;
			break;
			case 0xa974:
			virge->s3d.lxstart = val;
			break;
			case 0xa978:
			virge->s3d.lystart = val & 0x7ff;
			break;
			case 0xa97c:
			virge->s3d.lycnt = val & 0x7ff;
			virge->s3d.line_dir = val >> 31;
			if (virge->s3d.cmd_set & CMD_SET_AE)
				s3_virge_bitblt(virge, -1, 0);
			break;

			case 0xad00:
			virge->s3d.cmd_set = val;
			if (!(val & CMD_SET_AE))
				s3_virge_bitblt(virge, -1, 0);
			break;
			case 0xad68:
			virge->s3d.prdx = val;
			break;
			case 0xad6c:
			virge->s3d.prxstart = val;
			break;
			case 0xad70:
			virge->s3d.pldx = val;
			break;
			case 0xad74:
			virge->s3d.plxstart = val;
			break;
			case 0xad78:
			virge->s3d.pystart = val & 0x7ff;
			break;
			case 0xad7c:
			virge->s3d.pycnt = val & 0x300007ff;
			if (virge->s3d.cmd_set & CMD_SET_AE)
				s3_virge_bitblt(virge, -1, 0);
			break;
			
			case 0xb0f4: case 0xb4f4:
			virge->s3d_tri.fog_b = val & 0xff;
			virge->s3d_tri.fog_g = (val >> 8) & 0xff;
			virge->s3d_tri.fog_r = (val >> 16) & 0xff;
			break;
			case 0xb4d4:
			virge->s3d_tri.z_base = val & 0x3ffff8;
			break;
			case 0xb4d8:
			virge->s3d_tri.dest_base = val & 0x3ffff8;
			break;
			case 0xb4dc:
			virge->s3d_tri.clip_l = (val >> 16) & 0x7ff;
			virge->s3d_tri.clip_r = val & 0x7ff;
			break;
			case 0xb4e0:
			virge->s3d_tri.clip_t = (val >> 16) & 0x7ff;
			virge->s3d_tri.clip_b = val & 0x7ff;
			break;
			case 0xb4e4:
			virge->s3d_tri.dest_str = (val >> 16) & 0xff8;
			virge->s3d.src_str = val & 0xff8;
			break;
			case 0xb4e8:
			virge->s3d_tri.z_str = val & 0xff8;
			break;
			case 0xb4ec:
			virge->s3d_tri.tex_base = val & 0x3ffff8;
			break;
			case 0xb4f0:
			virge->s3d_tri.tex_bdr_clr = val & 0xffffff;
			break;
			case 0xb500:
			virge->s3d_tri.cmd_set = val;
			if (!(val & CMD_SET_AE))
				queue_triangle(virge);
			break;
			case 0xb504:
			virge->s3d_tri.tbv = val & 0xfffff;
			break;
			case 0xb508:
			virge->s3d_tri.tbu = val & 0xfffff;
			break;
			case 0xb50c:
			virge->s3d_tri.TdWdX = val;
			break;
			case 0xb510:
			virge->s3d_tri.TdWdY = val;
			break;
			case 0xb514:
			virge->s3d_tri.tws = val;
			break;
			case 0xb518:
			virge->s3d_tri.TdDdX = val;
			break;
			case 0xb51c:
			virge->s3d_tri.TdVdX = val;
			break;
			case 0xb520:
			virge->s3d_tri.TdUdX = val;
			break;
			case 0xb524:
			virge->s3d_tri.TdDdY = val;
			break;
			case 0xb528:
			virge->s3d_tri.TdVdY = val;
			break;
			case 0xb52c:
			virge->s3d_tri.TdUdY = val;
			break;
			case 0xb530:
			virge->s3d_tri.tds = val;
			break;
			case 0xb534:
			virge->s3d_tri.tvs = val;
			break;
			case 0xb538:
			virge->s3d_tri.tus = val;
			break;
			case 0xb53c:
			virge->s3d_tri.TdGdX = val >> 16;
			virge->s3d_tri.TdBdX = val & 0xffff;
			break;
			case 0xb540:
			virge->s3d_tri.TdAdX = val >> 16;
			virge->s3d_tri.TdRdX = val & 0xffff;
			break;
			case 0xb544:
			virge->s3d_tri.TdGdY = val >> 16;
			virge->s3d_tri.TdBdY = val & 0xffff;
			break;
			case 0xb548:
			virge->s3d_tri.TdAdY = val >> 16;
			virge->s3d_tri.TdRdY = val & 0xffff;
			break;
			case 0xb54c:
			virge->s3d_tri.tgs = (val >> 16) & 0xffff;
			virge->s3d_tri.tbs = val & 0xffff;
			break;
			case 0xb550:
			virge->s3d_tri.tas = (val >> 16) & 0xffff;
			virge->s3d_tri.trs = val & 0xffff;
			break;
			
			case 0xb554:
			virge->s3d_tri.TdZdX = val;
			break;
			case 0xb558:
			virge->s3d_tri.TdZdY = val;
			break;
			case 0xb55c:
			virge->s3d_tri.tzs = val;
			break;
			case 0xb560:
			virge->s3d_tri.TdXdY12 = val;
			break;
			case 0xb564:
			virge->s3d_tri.txend12 = val;
			break;
			case 0xb568:
			virge->s3d_tri.TdXdY01 = val;
			break;
			case 0xb56c:
			virge->s3d_tri.txend01 = val;
			break;
			case 0xb570:
			virge->s3d_tri.TdXdY02 = val;
			break;
			case 0xb574:
			virge->s3d_tri.txs = val;
			break;
			case 0xb578:
			virge->s3d_tri.tys = val;
			break;
			case 0xb57c:
			virge->s3d_tri.ty01 = (val >> 16) & 0x7ff;
			virge->s3d_tri.ty12 = val & 0x7ff;
			virge->s3d_tri.tlr = val >> 31;
			if (virge->s3d_tri.cmd_set & CMD_SET_AE) {
				queue_triangle(virge);
			}
			break;

			case 0xff20:
			s3_virge_mmio_write(addr, val, virge);
			break;
		}
		
		if ((((addr & 0xfffc) < 0x8000)) || (((addr & 0xfffc) >= 0xa000) && ((addr & 0xfffc) < 0xc000))) {
			if (virge->cmd_dma) {
				dmahdr->dblword_write++;
				virge->dmabuffer[virge->dmaidx_w] = dmahdr->dblword_write & 0xff; /*0-7*/
				virge->dmabuffer[virge->dmaidx_w + 1] = dmahdr->dblword_write << 8; /*8-15*/
				if (dmahdr->dblword_write == 1) {
					virge->dmabuffer[virge->dmaidx_w + 2] = val & 0xff; /*16-23*/
					virge->dmabuffer[virge->dmaidx_w + 3] = ((val >> 8) & (dmahdr->datatype ? 0xbf : 0x3f)); /*24-31*/
				}
				virge->dmaidx_w += 4;
				if (virge->dma_bs) {
					if (virge->dmaidx_w == 65536)
						virge->dmaidx_w = 0;
				} else {
					if (virge->dmaidx_w == 4096)
						virge->dmaidx_w = 0;						
				}
				dma_bm_read(virge->dma_base_addr + virge->dma_ptr, virge->dmabuffer, (virge->dma_bs) ? 65536 : 4096, 4);
			}
		}
	}
}

#define READ(addr, val)                                                                         \
        {                                                                                       \
                switch (bpp)                                                                    \
                {                                                                               \
                        case 0: /*8 bpp*/                                                       \
                        val = vram[addr & virge->vram_mask];                                     \
                        break;                                                                  \
                        case 1: /*16 bpp*/                                                      \
                        val = *(uint16_t *)&vram[addr & virge->vram_mask];                       \
                        break;                                                                  \
                        case 2: /*24 bpp*/                                                      \
                        val = (*(uint32_t *)&vram[addr & virge->vram_mask]) & 0xffffff;          \
                        break;                                                                  \
                }                                                                               \
        }

#define CLIP(x, y)                                              \
        {                                                       \
                if ((virge->s3d.cmd_set & CMD_SET_HC) &&     \
                    (x < virge->s3d.clip_l ||                \
                     x > virge->s3d.clip_r ||                \
                     y < virge->s3d.clip_t ||                \
                     y > virge->s3d.clip_b))                 \
                        update = 0;                             \
        }

#define CLIP_3D(x, y)                                           \
        {                                                       \
                if ((s3d_tri->cmd_set & CMD_SET_HC) &&        \
                    (x < s3d_tri->clip_l ||                   \
                     x > s3d_tri->clip_r ||                   \
                     y < s3d_tri->clip_t ||                   \
                     y > s3d_tri->clip_b))                    \
                        update = 0;                             \
        }
        
#define MIX()                                                   \
        {                                                       \
                int c;                                          \
                for (c = 0; c < 24; c++)                        \
                {                                               \
                        int d = (dest & (1 << c)) ? 1 : 0;      \
                        if (source & (1 << c))  d |= 2;         \
                        if (pattern & (1 << c)) d |= 4;         \
                        if (virge->s3d.rop & (1 << d)) out |= (1 << c);    \
                }                                               \
        }

#define WRITE(addr, val)                                                                        \
        {                                                                                       \
                switch (bpp)                                                                    \
                {                                                                               \
                        case 0: /*8 bpp*/                                                       \
                        vram[addr & virge->vram_mask] = val;                                     \
                        svga->changedvram[(addr & virge->vram_mask) >> 12] = changeframecount;    \
                        break;                                                                  \
                        case 1: /*16 bpp*/                                                      \
                        *(uint16_t *)&vram[addr & virge->vram_mask] = val;                       \
                        svga->changedvram[(addr & virge->vram_mask) >> 12] = changeframecount;    \
                        break;                                                                  \
                        case 2: /*24 bpp*/                                                      \
                        *(uint32_t *)&vram[addr & virge->vram_mask] = (val & 0xffffff) |         \
                                                              (vram[(addr + 3) & virge->vram_mask] << 24);  \
                        svga->changedvram[(addr & virge->vram_mask) >> 12] = changeframecount;    \
                        break;                                                                  \
                }                                                                               \
        }

static void s3_virge_bitblt(virge_t *virge, int count, uint32_t cpu_dat)
{
	svga_t *svga = &virge->svga;
        uint8_t *vram = virge->svga.vram;
        uint32_t mono_pattern[64];
        int count_mask;
        int x_inc = (virge->s3d.cmd_set & CMD_SET_XP) ? 1 : -1;
        int y_inc = (virge->s3d.cmd_set & CMD_SET_YP) ? 1 : -1;
        int bpp;
        int x_mul;
        int cpu_dat_shift;
        uint32_t *pattern_data;
        uint32_t src_fg_clr, src_bg_clr;
	uint32_t src_addr;
	uint32_t dest_addr;
	uint32_t source = 0, dest = 0, pattern;
	uint32_t out = 0;
	int update;
        
        switch (virge->s3d.cmd_set & CMD_SET_FORMAT_MASK)
        {
                case CMD_SET_FORMAT_8:
                bpp = 0;
                x_mul = 1;
                cpu_dat_shift = 8;
                pattern_data = virge->s3d.pattern_8;
                src_fg_clr = virge->s3d.src_fg_clr & 0xff;
                src_bg_clr = virge->s3d.src_bg_clr & 0xff;
                break;
                case CMD_SET_FORMAT_16:
                bpp = 1;
                x_mul = 2;
                cpu_dat_shift = 16;
                pattern_data = virge->s3d.pattern_16;
                src_fg_clr = virge->s3d.src_fg_clr & 0xffff;
                src_bg_clr = virge->s3d.src_bg_clr & 0xffff;
                break;
                case CMD_SET_FORMAT_24:
		default:
                bpp = 2;
                x_mul = 3;
                cpu_dat_shift = 24;
                pattern_data = virge->s3d.pattern_24;
                src_fg_clr = virge->s3d.src_fg_clr;
                src_bg_clr = virge->s3d.src_bg_clr;
                break;
        }
        if (virge->s3d.cmd_set & CMD_SET_MP)
                pattern_data = mono_pattern;
        
        switch (virge->s3d.cmd_set & CMD_SET_ITA_MASK)
        {
                case CMD_SET_ITA_BYTE:
                count_mask = ~0x7;
                break;
                case CMD_SET_ITA_WORD:
                count_mask = ~0xf;
                break;
                case CMD_SET_ITA_DWORD:
		default:
                count_mask = ~0x1f;
                break;
        }
        if (virge->s3d.cmd_set & CMD_SET_MP)
        {
                int x, y;
                for (y = 0; y < 4; y++)
                {
                        for (x = 0; x < 8; x++)
                        {
                                if (virge->s3d.mono_pat_0 & (1 << (x + y*8)))
                                        mono_pattern[y*8 + (7 - x)] = virge->s3d.pat_fg_clr;
                                else
                                        mono_pattern[y*8 + (7 - x)] = virge->s3d.pat_bg_clr;
                                if (virge->s3d.mono_pat_1 & (1 << (x + y*8)))
                                        mono_pattern[(y+4)*8 + (7 - x)] = virge->s3d.pat_fg_clr;
                                else
                                        mono_pattern[(y+4)*8 + (7 - x)] = virge->s3d.pat_bg_clr;
                        }
                }
        }
        switch (virge->s3d.cmd_set & CMD_SET_COMMAND_MASK)
        {
                case CMD_SET_COMMAND_BITBLT:
                if (count == -1)
                {
                        virge->s3d.src_x = virge->s3d.rsrc_x;
                        virge->s3d.src_y = virge->s3d.rsrc_y;
                        virge->s3d.dest_x = virge->s3d.rdest_x;
                        virge->s3d.dest_y = virge->s3d.rdest_y;
                        virge->s3d.w = virge->s3d.r_width;
                        virge->s3d.h = virge->s3d.r_height;
                        virge->s3d.rop = (virge->s3d.cmd_set >> 17) & 0xff;
                        virge->s3d.data_left_count = 0;
                        
                        s3_virge_log("BitBlt start src_x=%i,src_y=%i,dest_x=%i,dest_y=%i,w=%i,h=%i,rop=%02X,src_base=%x,dest_base=%x\n",
                                                                 virge->s3d.src_x,
                                                                 virge->s3d.src_y,
                                                                 virge->s3d.dest_x,
                                                                 virge->s3d.dest_y,
                                                                 virge->s3d.w,
                                                                 virge->s3d.h,
                                                                 virge->s3d.rop,
                                                                 virge->s3d.src_base,
                                                                 virge->s3d.dest_base);
                        
                        if (virge->s3d.cmd_set & CMD_SET_IDS)
                                return;
                }
                if (!virge->s3d.h)
                        return;
                while (count)
                {
                        src_addr = virge->s3d.src_base + (virge->s3d.src_x * x_mul) + (virge->s3d.src_y * virge->s3d.src_str);
                        dest_addr = virge->s3d.dest_base + (virge->s3d.dest_x * x_mul) + (virge->s3d.dest_y * virge->s3d.dest_str);
                        out = 0;
                        update = 1;

                        switch (virge->s3d.cmd_set & (CMD_SET_MS | CMD_SET_IDS))
                        {
                                case 0:
                                case CMD_SET_MS:
                                READ(src_addr, source);
                                if ((virge->s3d.cmd_set & CMD_SET_TP) && source == src_fg_clr)
                                        update = 0;
                                break;
                                case CMD_SET_IDS:
                                if (virge->s3d.data_left_count)
                                {
                                        /*Handle shifting for 24-bit data*/
                                        source = virge->s3d.data_left;
                                        source |= ((cpu_dat << virge->s3d.data_left_count) & ~0xff000000);
                                        cpu_dat >>= (cpu_dat_shift - virge->s3d.data_left_count);
                                        count -= (cpu_dat_shift - virge->s3d.data_left_count);
                                        virge->s3d.data_left_count = 0;
                                        if (count < cpu_dat_shift)
                                        {
                                                virge->s3d.data_left = cpu_dat;
                                                virge->s3d.data_left_count = count;
                                                count = 0;
                                        }
                                }
                                else
                                {
                                        source = cpu_dat;
                                        cpu_dat >>= cpu_dat_shift;
                                        count -= cpu_dat_shift;
                                        if (count < cpu_dat_shift)
                                        {
                                                virge->s3d.data_left = cpu_dat;
                                                virge->s3d.data_left_count = count;
                                                count = 0;
                                        }
                                }
                                if ((virge->s3d.cmd_set & CMD_SET_TP) && source == src_fg_clr)
                                        update = 0;
                                break;
                                case CMD_SET_IDS | CMD_SET_MS:
                                source = (cpu_dat & (1 << 31)) ? src_fg_clr : src_bg_clr;
                                if ((virge->s3d.cmd_set & CMD_SET_TP) && !(cpu_dat & (1 << 31)))
                                        update = 0;
                                cpu_dat <<= 1;
                                count--;
                                break;
                        }

                        CLIP(virge->s3d.dest_x, virge->s3d.dest_y);

                        if (update)
                        {
                                READ(dest_addr, dest);
                                pattern = pattern_data[(virge->s3d.dest_y & 7)*8 + (virge->s3d.dest_x & 7)];
                                MIX();

                                WRITE(dest_addr, out);
                        }
                
                        virge->s3d.src_x += x_inc;
                        virge->s3d.src_x &= 0x7ff;
                        virge->s3d.dest_x += x_inc;
                        virge->s3d.dest_x &= 0x7ff;
                        if (!virge->s3d.w)
                        {
                                virge->s3d.src_x = virge->s3d.rsrc_x;
                                virge->s3d.dest_x = virge->s3d.rdest_x;
                                virge->s3d.w = virge->s3d.r_width;

                                virge->s3d.src_y += y_inc;
                                virge->s3d.dest_y += y_inc;
                                virge->s3d.h--;
                                
                                switch (virge->s3d.cmd_set & (CMD_SET_MS | CMD_SET_IDS))
                                {
                                        case CMD_SET_IDS:
                                        cpu_dat >>= (count - (count & count_mask));
                                        count &= count_mask;
                                        virge->s3d.data_left_count = 0;
                                        break;

                                        case CMD_SET_IDS | CMD_SET_MS:
                                        cpu_dat <<= (count - (count & count_mask));
                                        count &= count_mask;
                                        break;
                                }
                                if (!virge->s3d.h)
                                {
                                        return;
                                }
                        }
                        else
                                virge->s3d.w--;                        
                }
                break;
                
                case CMD_SET_COMMAND_RECTFILL:
                /*No source, pattern = pat_fg_clr*/
                if (count == -1)
                {
                        virge->s3d.src_x = virge->s3d.rsrc_x;
                        virge->s3d.src_y = virge->s3d.rsrc_y;
                        virge->s3d.dest_x = virge->s3d.rdest_x;
                        virge->s3d.dest_y = virge->s3d.rdest_y;
                        virge->s3d.w = virge->s3d.r_width;
                        virge->s3d.h = virge->s3d.r_height;
                        virge->s3d.rop = (virge->s3d.cmd_set >> 17) & 0xff;
                        
                        s3_virge_log("RctFll start %i,%i %i,%i %02X %08x\n", virge->s3d.dest_x,
                                                                 virge->s3d.dest_y,
                                                                 virge->s3d.w,
                                                                 virge->s3d.h,
                                                                 virge->s3d.rop, virge->s3d.dest_base);
                }

                while (count && virge->s3d.h)
                {
						source = virge->s3d.pat_fg_clr;
						dest_addr = virge->s3d.dest_base + (virge->s3d.dest_x * x_mul) + (virge->s3d.dest_y * virge->s3d.dest_str);
                        pattern = virge->s3d.pat_fg_clr;
                        out = 0;
                        update = 1;

                        CLIP(virge->s3d.dest_x, virge->s3d.dest_y);

                        if (update)
                        {
                                READ(dest_addr, dest);

                                MIX();

                                WRITE(dest_addr, out);
                        }

                        virge->s3d.src_x += x_inc;
                        virge->s3d.src_x &= 0x7ff;
                        virge->s3d.dest_x += x_inc;
                        virge->s3d.dest_x &= 0x7ff;
                        if (!virge->s3d.w)
                        {
                                virge->s3d.src_x = virge->s3d.rsrc_x;
                                virge->s3d.dest_x = virge->s3d.rdest_x;
                                virge->s3d.w = virge->s3d.r_width;

                                virge->s3d.src_y += y_inc;
                                virge->s3d.dest_y += y_inc;
                                virge->s3d.h--;
                                if (!virge->s3d.h)
                                {
                                        return;
                                }
                        }
                        else
                                virge->s3d.w--;                        
                        count--;
                }
                break;
                
                case CMD_SET_COMMAND_LINE:
                if (count == -1)
                {
                        virge->s3d.dest_x = virge->s3d.lxstart;
                        virge->s3d.dest_y = virge->s3d.lystart;
                        virge->s3d.h = virge->s3d.lycnt;
                        virge->s3d.rop = (virge->s3d.cmd_set >> 17) & 0xff;
                }
                while (virge->s3d.h)
                {
                        int x;
                        int new_x;
                        int first_pixel = 1;
                        
                        x = virge->s3d.dest_x >> 20;

                        if (virge->s3d.h == virge->s3d.lycnt &&
                           ((virge->s3d.line_dir && x > virge->s3d.lxend0) ||
                           (!virge->s3d.line_dir && x < virge->s3d.lxend0)))
                                x = virge->s3d.lxend0;

                        if (virge->s3d.h == 1)
                                new_x = virge->s3d.lxend1 + (virge->s3d.line_dir ? 1 : -1);
                        else
                                new_x = (virge->s3d.dest_x + virge->s3d.ldx) >> 20;

                        
                        if ((virge->s3d.line_dir && x > new_x) ||
                            (!virge->s3d.line_dir && x < new_x))
                                goto skip_line;
                                
                        do
                        {
                                uint32_t dest_addr = virge->s3d.dest_base + (x * x_mul) + (virge->s3d.dest_y * virge->s3d.dest_str);
                                uint32_t source = 0, dest = 0, pattern;
                                uint32_t out = 0;
                                int update = 1;

                                if ((virge->s3d.h == virge->s3d.lycnt || !first_pixel) &&
                                   ((virge->s3d.line_dir && x < virge->s3d.lxend0) ||
                                   (!virge->s3d.line_dir && x > virge->s3d.lxend0)))
                                        update = 0;

                                if ((virge->s3d.h == 1  || !first_pixel) &&
                                   ((virge->s3d.line_dir && x > virge->s3d.lxend1) ||
                                   (!virge->s3d.line_dir && x < virge->s3d.lxend1)))
                                        update = 0;

                                CLIP(x, virge->s3d.dest_y);

                                if (update)
                                {
                                        READ(dest_addr, dest);
                                        pattern = virge->s3d.pat_fg_clr;

                                        MIX();

                                        WRITE(dest_addr, out);
                                }
                                
                                if (x < new_x)
                                        x++;
                                else if (x > new_x)
                                        x--;
                                first_pixel = 0;
                        } while (x != new_x);

skip_line:
                        virge->s3d.dest_x += virge->s3d.ldx;
                        virge->s3d.dest_y--;
                        virge->s3d.h--;
                }
                break;

                case CMD_SET_COMMAND_POLY:
                /*No source*/
                if (virge->s3d.pycnt & (1 << 28))
                        virge->s3d.dest_r = virge->s3d.prxstart;
                if (virge->s3d.pycnt & (1 << 29))
                        virge->s3d.dest_l = virge->s3d.plxstart;
                virge->s3d.h = virge->s3d.pycnt & 0x7ff;
                virge->s3d.rop = (virge->s3d.cmd_set >> 17) & 0xff;
                while (virge->s3d.h)
                {
                        int x = virge->s3d.dest_l >> 20;
                        int xend = virge->s3d.dest_r >> 20;
                        int y = virge->s3d.pystart & 0x7ff;
                        int xdir = (x < xend) ? 1 : -1;
                        do
                        {
                                uint32_t dest_addr = virge->s3d.dest_base + (x * x_mul) + (y * virge->s3d.dest_str);
                                uint32_t source = 0, dest = 0, pattern;
                                uint32_t out = 0;
                                int update = 1;

                                CLIP(x, y);

                                if (update)
                                {
                                        READ(dest_addr, dest);
                                        pattern = pattern_data[(y & 7)*8 + (x & 7)];
                                        MIX();

                                        WRITE(dest_addr, out);
                                }
                                
                                x = (x + xdir) & 0x7ff;
                        }
                        while (x != (xend + xdir));

                        virge->s3d.dest_l += virge->s3d.pldx;
                        virge->s3d.dest_r += virge->s3d.prdx;
                        virge->s3d.h--;
                        virge->s3d.pystart = (virge->s3d.pystart - 1) & 0x7ff;
                }
                break;

                case CMD_SET_COMMAND_NOP:
                break;
        }
}

#define RGB15_TO_24(val, r, g, b) b = ((val & 0x001f) << 3) | ((val & 0x001f) >> 2);     \
                                  g = ((val & 0x03e0) >> 2) | ((val & 0x03e0) >> 7);     \
                                  r = ((val & 0x7c00) >> 7) | ((val & 0x7c00) >> 12);

#define RGB24_TO_24(val, r, g, b) b = val & 0xff;             \
                                  g = (val & 0xff00) >> 8;    \
                                  r = (val & 0xff0000) >> 16

#define RGB15(r, g, b, dest) \
        if (virge->dithering_enabled)                           \
        {                                                       \
                int add = dither[_y & 3][_x & 3];               \
                int _r = (r > 248) ? 248 : r+add;               \
                int _g = (g > 248) ? 248 : g+add;               \
                int _b = (b > 248) ? 248 : b+add;               \
                dest = ((_b >> 3) & 0x1f) | (((_g >> 3) & 0x1f) << 5) | (((_r >> 3) & 0x1f) << 10);     \
        }                                                                                               \
        else                                                                                            \
                dest = ((b >> 3) & 0x1f) | (((g >> 3) & 0x1f) << 5) | (((r >> 3) & 0x1f) << 10)

#define RGB24(r, g, b) ((b) | ((g) << 8) | ((r) << 16))

typedef struct rgba_t
{
        int r, g, b, a;
} rgba_t;

typedef struct s3d_state_t
{
        int32_t r, g, b, a, u, v, d, w;

        int32_t base_r, base_g, base_b, base_a, base_u, base_v, base_d, base_w;
        
        uint32_t base_z;

        uint32_t tbu, tbv;

        uint32_t cmd_set;
        int max_d;
        
        uint16_t *texture[10];
        
        uint32_t tex_bdr_clr;
        
        int32_t x1, x2;
        int y;
        
        rgba_t dest_rgba;
} s3d_state_t;

typedef struct s3d_texture_state_t
{
        int level;
        int texture_shift;
        
        int32_t u, v;
} s3d_texture_state_t;

static void (*tex_read)(s3d_state_t *state, s3d_texture_state_t *texture_state, rgba_t *out);
static void (*tex_sample)(s3d_state_t *state);
static void (*dest_pixel)(s3d_state_t *state);

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int _x, _y;

static void tex_ARGB1555(s3d_state_t *state, s3d_texture_state_t *texture_state, rgba_t *out)
{
        int offset = ((texture_state->u & 0x7fc0000) >> texture_state->texture_shift) +
                     (((texture_state->v & 0x7fc0000) >> texture_state->texture_shift) << texture_state->level);
        uint16_t val = state->texture[texture_state->level][offset];

        out->r = ((val & 0x7c00) >> 7) | ((val & 0x7000) >> 12);
        out->g = ((val & 0x03e0) >> 2) | ((val & 0x0380) >> 7);
        out->b = ((val & 0x001f) << 3) | ((val & 0x001c) >> 2);
        out->a = (val & 0x8000) ? 0xff : 0;
}

static void tex_ARGB1555_nowrap(s3d_state_t *state, s3d_texture_state_t *texture_state, rgba_t *out)
{
        int offset = ((texture_state->u & 0x7fc0000) >> texture_state->texture_shift) +
                     (((texture_state->v & 0x7fc0000) >> texture_state->texture_shift) << texture_state->level);
        uint16_t val = state->texture[texture_state->level][offset];

        if (((texture_state->u | texture_state->v) & 0xf8000000) == 0xf8000000)
                val = state->tex_bdr_clr;

        out->r = ((val & 0x7c00) >> 7) | ((val & 0x7000) >> 12);
        out->g = ((val & 0x03e0) >> 2) | ((val & 0x0380) >> 7);
        out->b = ((val & 0x001f) << 3) | ((val & 0x001c) >> 2);
        out->a = (val & 0x8000) ? 0xff : 0;
}

static void tex_ARGB4444(s3d_state_t *state, s3d_texture_state_t *texture_state, rgba_t *out)
{
        int offset = ((texture_state->u & 0x7fc0000) >> texture_state->texture_shift) +
                     (((texture_state->v & 0x7fc0000) >> texture_state->texture_shift) << texture_state->level);
        uint16_t val = state->texture[texture_state->level][offset];

        out->r = ((val & 0x0f00) >> 4) | ((val & 0x0f00) >> 8);
        out->g = (val & 0x00f0) | ((val & 0x00f0) >> 4);
        out->b = ((val & 0x000f) << 4) | (val & 0x000f);
        out->a = ((val & 0xf000) >> 8) | ((val & 0xf000) >> 12);
}

static void tex_ARGB4444_nowrap(s3d_state_t *state, s3d_texture_state_t *texture_state, rgba_t *out)
{
        int offset = ((texture_state->u & 0x7fc0000) >> texture_state->texture_shift) +
                     (((texture_state->v & 0x7fc0000) >> texture_state->texture_shift) << texture_state->level);
        uint16_t val = state->texture[texture_state->level][offset];

        if (((texture_state->u | texture_state->v) & 0xf8000000) == 0xf8000000)
                val = state->tex_bdr_clr;

        out->r = ((val & 0x0f00) >> 4) | ((val & 0x0f00) >> 8);
        out->g = (val & 0x00f0) | ((val & 0x00f0) >> 4);
        out->b = ((val & 0x000f) << 4) | (val & 0x000f);
        out->a = ((val & 0xf000) >> 8) | ((val & 0xf000) >> 12);
}

static void tex_ARGB8888(s3d_state_t *state, s3d_texture_state_t *texture_state, rgba_t *out)
{
        int offset = ((texture_state->u & 0x7fc0000) >> texture_state->texture_shift) +
                     (((texture_state->v & 0x7fc0000) >> texture_state->texture_shift) << texture_state->level);
        uint32_t val = ((uint32_t *)state->texture[texture_state->level])[offset];

        out->r = (val >> 16) & 0xff;
        out->g = (val >> 8)  & 0xff;
        out->b =  val        & 0xff;
        out->a = (val >> 24) & 0xff;
}
static void tex_ARGB8888_nowrap(s3d_state_t *state, s3d_texture_state_t *texture_state, rgba_t *out)
{
        int offset = ((texture_state->u & 0x7fc0000) >> texture_state->texture_shift) +
                     (((texture_state->v & 0x7fc0000) >> texture_state->texture_shift) << texture_state->level);
        uint32_t val = ((uint32_t *)state->texture[texture_state->level])[offset];

        if (((texture_state->u | texture_state->v) & 0xf8000000) == 0xf8000000)
                val = state->tex_bdr_clr;

        out->r = (val >> 16) & 0xff;
        out->g = (val >> 8)  & 0xff;
        out->b =  val        & 0xff;
        out->a = (val >> 24) & 0xff;
}

static void tex_sample_normal(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        
        texture_state.level = state->max_d;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        texture_state.u = state->u + state->tbu;
        texture_state.v = state->v + state->tbv;

        tex_read(state, &texture_state, &state->dest_rgba);
}

static void tex_sample_normal_filter(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int tex_offset;
        rgba_t tex_samples[4];
        int du, dv;
        int d[4];

        texture_state.level = state->max_d;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        tex_offset = 1 << texture_state.texture_shift;

        texture_state.u = state->u + state->tbu;
        texture_state.v = state->v + state->tbv;
        tex_read(state, &texture_state, &tex_samples[0]);
        du = (texture_state.u >> (texture_state.texture_shift - 8)) & 0xff;
        dv = (texture_state.v >> (texture_state.texture_shift - 8)) & 0xff;

        texture_state.u = state->u + state->tbu + tex_offset;
        texture_state.v = state->v + state->tbv;
        tex_read(state, &texture_state, &tex_samples[1]);

        texture_state.u = state->u + state->tbu;
        texture_state.v = state->v + state->tbv + tex_offset;
        tex_read(state, &texture_state, &tex_samples[2]);

        texture_state.u = state->u + state->tbu + tex_offset;
        texture_state.v = state->v + state->tbv + tex_offset;
        tex_read(state, &texture_state, &tex_samples[3]);
        
        d[0] = (256 - du) * (256 - dv);
        d[1] =  du * (256 - dv);
        d[2] = (256 - du) * dv;
        d[3] = du * dv;
        
        state->dest_rgba.r = (tex_samples[0].r * d[0] + tex_samples[1].r * d[1] + tex_samples[2].r * d[2] + tex_samples[3].r * d[3]) >> 16;
        state->dest_rgba.g = (tex_samples[0].g * d[0] + tex_samples[1].g * d[1] + tex_samples[2].g * d[2] + tex_samples[3].g * d[3]) >> 16;
        state->dest_rgba.b = (tex_samples[0].b * d[0] + tex_samples[1].b * d[1] + tex_samples[2].b * d[2] + tex_samples[3].b * d[3]) >> 16;
        state->dest_rgba.a = (tex_samples[0].a * d[0] + tex_samples[1].a * d[1] + tex_samples[2].a * d[2] + tex_samples[3].a * d[3]) >> 16;
}

static void tex_sample_mipmap(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;

        texture_state.level = (state->d < 0) ? state->max_d : state->max_d - ((state->d >> 27) & 0xf);
        if (texture_state.level < 0)
                texture_state.level = 0;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        texture_state.u = state->u + state->tbu;
        texture_state.v = state->v + state->tbv;

        tex_read(state, &texture_state, &state->dest_rgba);
}

static void tex_sample_mipmap_filter(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int tex_offset;
        rgba_t tex_samples[4];
        int du, dv;
        int d[4];

        texture_state.level = (state->d < 0) ? state->max_d : state->max_d - ((state->d >> 27) & 0xf);
        if (texture_state.level < 0)
                texture_state.level = 0;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        tex_offset = 1 << texture_state.texture_shift;
        
        texture_state.u = state->u + state->tbu;
        texture_state.v = state->v + state->tbv;
        tex_read(state, &texture_state, &tex_samples[0]);
        du = (texture_state.u >> (texture_state.texture_shift - 8)) & 0xff;
        dv = (texture_state.v >> (texture_state.texture_shift - 8)) & 0xff;

        texture_state.u = state->u + state->tbu + tex_offset;
        texture_state.v = state->v + state->tbv;
        tex_read(state, &texture_state, &tex_samples[1]);

        texture_state.u = state->u + state->tbu;
        texture_state.v = state->v + state->tbv + tex_offset;
        tex_read(state, &texture_state, &tex_samples[2]);

        texture_state.u = state->u + state->tbu + tex_offset;
        texture_state.v = state->v + state->tbv + tex_offset;
        tex_read(state, &texture_state, &tex_samples[3]);

        d[0] = (256 - du) * (256 - dv);
        d[1] =  du * (256 - dv);
        d[2] = (256 - du) * dv;
        d[3] = du * dv;
        
        state->dest_rgba.r = (tex_samples[0].r * d[0] + tex_samples[1].r * d[1] + tex_samples[2].r * d[2] + tex_samples[3].r * d[3]) >> 16;
        state->dest_rgba.g = (tex_samples[0].g * d[0] + tex_samples[1].g * d[1] + tex_samples[2].g * d[2] + tex_samples[3].g * d[3]) >> 16;
        state->dest_rgba.b = (tex_samples[0].b * d[0] + tex_samples[1].b * d[1] + tex_samples[2].b * d[2] + tex_samples[3].b * d[3]) >> 16;
        state->dest_rgba.a = (tex_samples[0].a * d[0] + tex_samples[1].a * d[1] + tex_samples[2].a * d[2] + tex_samples[3].a * d[3]) >> 16;
}

static void tex_sample_persp_normal(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0;

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);
        
        texture_state.level = state->max_d;
        texture_state.texture_shift = 18 + (9 - texture_state.level);      
        texture_state.u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (12 + state->max_d)) + state->tbu;
        texture_state.v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (12 + state->max_d)) + state->tbv;

        tex_read(state, &texture_state, &state->dest_rgba);
}

static void tex_sample_persp_normal_filter(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0, u, v;
        int tex_offset;
        rgba_t tex_samples[4];
        int du, dv;
        int d[4];

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);

        u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (12 + state->max_d)) + state->tbu;
        v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (12 + state->max_d)) + state->tbv;

        texture_state.level = state->max_d;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        tex_offset = 1 << texture_state.texture_shift;
        
        texture_state.u = u;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[0]);
        du = (u >> (texture_state.texture_shift - 8)) & 0xff;
        dv = (v >> (texture_state.texture_shift - 8)) & 0xff;

        texture_state.u = u + tex_offset;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[1]);

        texture_state.u = u;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[2]);

        texture_state.u = u + tex_offset;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[3]);

        d[0] = (256 - du) * (256 - dv);
        d[1] =  du * (256 - dv);
        d[2] = (256 - du) * dv;
        d[3] = du * dv;
        
        state->dest_rgba.r = (tex_samples[0].r * d[0] + tex_samples[1].r * d[1] + tex_samples[2].r * d[2] + tex_samples[3].r * d[3]) >> 16;
        state->dest_rgba.g = (tex_samples[0].g * d[0] + tex_samples[1].g * d[1] + tex_samples[2].g * d[2] + tex_samples[3].g * d[3]) >> 16;
        state->dest_rgba.b = (tex_samples[0].b * d[0] + tex_samples[1].b * d[1] + tex_samples[2].b * d[2] + tex_samples[3].b * d[3]) >> 16;
        state->dest_rgba.a = (tex_samples[0].a * d[0] + tex_samples[1].a * d[1] + tex_samples[2].a * d[2] + tex_samples[3].a * d[3]) >> 16;
}

static void tex_sample_persp_normal_375(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0;

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);
        
        texture_state.level = state->max_d;
        texture_state.texture_shift = 18 + (9 - texture_state.level);      
        texture_state.u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (8 + state->max_d)) + state->tbu;
        texture_state.v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (8 + state->max_d)) + state->tbv;

        tex_read(state, &texture_state, &state->dest_rgba);
}

static void tex_sample_persp_normal_filter_375(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0, u, v;
        int tex_offset;
        rgba_t tex_samples[4];
        int du, dv;
        int d[4];

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);

        u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (8 + state->max_d)) + state->tbu;
        v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (8 + state->max_d)) + state->tbv;
        
        texture_state.level = state->max_d;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        tex_offset = 1 << texture_state.texture_shift;

        texture_state.u = u;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[0]);
        du = (u >> (texture_state.texture_shift - 8)) & 0xff;
        dv = (v >> (texture_state.texture_shift - 8)) & 0xff;

        texture_state.u = u + tex_offset;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[1]);

        texture_state.u = u;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[2]);

        texture_state.u = u + tex_offset;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[3]);

        d[0] = (256 - du) * (256 - dv);
        d[1] =  du * (256 - dv);
        d[2] = (256 - du) * dv;
        d[3] = du * dv;
        
        state->dest_rgba.r = (tex_samples[0].r * d[0] + tex_samples[1].r * d[1] + tex_samples[2].r * d[2] + tex_samples[3].r * d[3]) >> 16;
        state->dest_rgba.g = (tex_samples[0].g * d[0] + tex_samples[1].g * d[1] + tex_samples[2].g * d[2] + tex_samples[3].g * d[3]) >> 16;
        state->dest_rgba.b = (tex_samples[0].b * d[0] + tex_samples[1].b * d[1] + tex_samples[2].b * d[2] + tex_samples[3].b * d[3]) >> 16;
        state->dest_rgba.a = (tex_samples[0].a * d[0] + tex_samples[1].a * d[1] + tex_samples[2].a * d[2] + tex_samples[3].a * d[3]) >> 16;
}


static void tex_sample_persp_mipmap(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0;

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);
        
        texture_state.level = (state->d < 0) ? state->max_d : state->max_d - ((state->d >> 27) & 0xf);
        if (texture_state.level < 0)
                texture_state.level = 0;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        texture_state.u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (12 + state->max_d)) + state->tbu;
        texture_state.v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (12 + state->max_d)) + state->tbv;

        tex_read(state, &texture_state, &state->dest_rgba);
}

static void tex_sample_persp_mipmap_filter(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0, u, v;
        int tex_offset;
        rgba_t tex_samples[4];
        int du, dv;
        int d[4];

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);

        u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (12 + state->max_d)) + state->tbu;
        v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (12 + state->max_d)) + state->tbv;
        
        texture_state.level = (state->d < 0) ? state->max_d : state->max_d - ((state->d >> 27) & 0xf);
        if (texture_state.level < 0)
                texture_state.level = 0;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        tex_offset = 1 << texture_state.texture_shift;

        texture_state.u = u;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[0]);
        du = (u >> (texture_state.texture_shift - 8)) & 0xff;
        dv = (v >> (texture_state.texture_shift - 8)) & 0xff;

        texture_state.u = u + tex_offset;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[1]);

        texture_state.u = u;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[2]);

        texture_state.u = u + tex_offset;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[3]);

        d[0] = (256 - du) * (256 - dv);
        d[1] =  du * (256 - dv);
        d[2] = (256 - du) * dv;
        d[3] = du * dv;
        
        state->dest_rgba.r = (tex_samples[0].r * d[0] + tex_samples[1].r * d[1] + tex_samples[2].r * d[2] + tex_samples[3].r * d[3]) >> 16;
        state->dest_rgba.g = (tex_samples[0].g * d[0] + tex_samples[1].g * d[1] + tex_samples[2].g * d[2] + tex_samples[3].g * d[3]) >> 16;
        state->dest_rgba.b = (tex_samples[0].b * d[0] + tex_samples[1].b * d[1] + tex_samples[2].b * d[2] + tex_samples[3].b * d[3]) >> 16;
        state->dest_rgba.a = (tex_samples[0].a * d[0] + tex_samples[1].a * d[1] + tex_samples[2].a * d[2] + tex_samples[3].a * d[3]) >> 16;
}

static void tex_sample_persp_mipmap_375(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0;

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);
        
        texture_state.level = (state->d < 0) ? state->max_d : state->max_d - ((state->d >> 27) & 0xf);
        if (texture_state.level < 0)
                texture_state.level = 0;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        texture_state.u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (8 + state->max_d)) + state->tbu;
        texture_state.v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (8 + state->max_d)) + state->tbv;

        tex_read(state, &texture_state, &state->dest_rgba);
}

static void tex_sample_persp_mipmap_filter_375(s3d_state_t *state)
{
        s3d_texture_state_t texture_state;
        int32_t w = 0, u, v;
        int tex_offset;
        rgba_t tex_samples[4];
        int du, dv;
        int d[4];

        if (state->w)
                w = (int32_t)(((1ULL << 27) << 19) / (int64_t)state->w);

        u = (int32_t)(((int64_t)state->u * (int64_t)w) >> (8 + state->max_d)) + state->tbu;
        v = (int32_t)(((int64_t)state->v * (int64_t)w) >> (8 + state->max_d)) + state->tbv;
        
        texture_state.level = (state->d < 0) ? state->max_d : state->max_d - ((state->d >> 27) & 0xf);
        if (texture_state.level < 0)
                texture_state.level = 0;
        texture_state.texture_shift = 18 + (9 - texture_state.level);
        tex_offset = 1 << texture_state.texture_shift;
        
        texture_state.u = u;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[0]);
        du = (u >> (texture_state.texture_shift - 8)) & 0xff;
        dv = (v >> (texture_state.texture_shift - 8)) & 0xff;

        texture_state.u = u + tex_offset;
        texture_state.v = v;
        tex_read(state, &texture_state, &tex_samples[1]);

        texture_state.u = u;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[2]);

        texture_state.u = u + tex_offset;
        texture_state.v = v + tex_offset;
        tex_read(state, &texture_state, &tex_samples[3]);

        d[0] = (256 - du) * (256 - dv);
        d[1] =  du * (256 - dv);
        d[2] = (256 - du) * dv;
        d[3] = du * dv;
        
        state->dest_rgba.r = (tex_samples[0].r * d[0] + tex_samples[1].r * d[1] + tex_samples[2].r * d[2] + tex_samples[3].r * d[3]) >> 16;
        state->dest_rgba.g = (tex_samples[0].g * d[0] + tex_samples[1].g * d[1] + tex_samples[2].g * d[2] + tex_samples[3].g * d[3]) >> 16;
        state->dest_rgba.b = (tex_samples[0].b * d[0] + tex_samples[1].b * d[1] + tex_samples[2].b * d[2] + tex_samples[3].b * d[3]) >> 16;
        state->dest_rgba.a = (tex_samples[0].a * d[0] + tex_samples[1].a * d[1] + tex_samples[2].a * d[2] + tex_samples[3].a * d[3]) >> 16;
}


#define CLAMP(x) do                                     \
        {                                               \
                if ((x) & ~0xff)                        \
                        x = ((x) < 0) ? 0 : 0xff;       \
        }                               \
        while (0)

#define CLAMP_RGBA(r, g, b, a)        \
                if ((r) & ~0xff)                        \
                        r = ((r) < 0) ? 0 : 0xff;       \
                if ((g) & ~0xff)                        \
                        g = ((g) < 0) ? 0 : 0xff;       \
                if ((b) & ~0xff)                        \
                        b = ((b) < 0) ? 0 : 0xff;       \
                if ((a) & ~0xff)                        \
                        a = ((a) < 0) ? 0 : 0xff;
        
#define CLAMP_RGB(r, g, b) do           \
        {                               \
                if ((r) < 0)            \
                        r = 0;          \
                if ((r) > 0xff)         \
                        r = 0xff;       \
                if ((g) < 0)            \
                        g = 0;          \
                if ((g) > 0xff)         \
                        g = 0xff;       \
                if ((b) < 0)            \
                        b = 0;          \
                if ((b) > 0xff)         \
                        b = 0xff;       \
        }                               \
        while (0)

static void dest_pixel_gouraud_shaded_triangle(s3d_state_t *state)
{
        state->dest_rgba.r = state->r >> 7;
        CLAMP(state->dest_rgba.r);

        state->dest_rgba.g = state->g >> 7;
        CLAMP(state->dest_rgba.g);

        state->dest_rgba.b = state->b >> 7;
        CLAMP(state->dest_rgba.b);

        state->dest_rgba.a = state->a >> 7;
        CLAMP(state->dest_rgba.a);
}

static void dest_pixel_unlit_texture_triangle(s3d_state_t *state)
{
        tex_sample(state);

        if (state->cmd_set & CMD_SET_ABC_SRC)
                state->dest_rgba.a = state->a >> 7;
}

static void dest_pixel_lit_texture_decal(s3d_state_t *state)
{
        tex_sample(state);

        if (state->cmd_set & CMD_SET_ABC_SRC)
                state->dest_rgba.a = state->a >> 7;
}

static void dest_pixel_lit_texture_reflection(s3d_state_t *state)
{
        tex_sample(state);

        state->dest_rgba.r += (state->r >> 7);
        state->dest_rgba.g += (state->g >> 7);
        state->dest_rgba.b += (state->b >> 7);
        if (state->cmd_set & CMD_SET_ABC_SRC)
                state->dest_rgba.a += (state->a >> 7);

        CLAMP_RGBA(state->dest_rgba.r, state->dest_rgba.g, state->dest_rgba.b, state->dest_rgba.a);
}

static void dest_pixel_lit_texture_modulate(s3d_state_t *state)
{
        int r = state->r >> 7, g = state->g >> 7, b = state->b >> 7, a = state->a >> 7;
        
        tex_sample(state);
        
        CLAMP_RGBA(r, g, b, a);
        
        state->dest_rgba.r = ((state->dest_rgba.r) * r) >> 8;
        state->dest_rgba.g = ((state->dest_rgba.g) * g) >> 8;
        state->dest_rgba.b = ((state->dest_rgba.b) * b) >> 8;

        if (state->cmd_set & CMD_SET_ABC_SRC)
                state->dest_rgba.a = a;
}

static void tri(virge_t *virge, s3d_t *s3d_tri, s3d_state_t *state, int yc, int32_t dx1, int32_t dx2)
{
	svga_t *svga = &virge->svga;
        uint8_t *vram = (uint8_t *)svga->vram;

        int x_dir = s3d_tri->tlr ? 1 : -1;
        
        int use_z = !(s3d_tri->cmd_set & CMD_SET_ZB_MODE);

        int y_count = yc;
        
        int bpp = (s3d_tri->cmd_set >> 2) & 7;
        
        uint32_t dest_offset = 0, z_offset = 0;

	uint32_t src_col;
	int src_r = 0, src_g = 0, src_b = 0;

	int x;
	int xe;
	uint32_t z;

	uint32_t dest_addr;
	uint32_t z_addr;
	int dx;
	int x_offset;
	int xz_offset;

	int update;
	uint16_t src_z = 0;
	
        if (s3d_tri->cmd_set & CMD_SET_HC)
        {
                if (state->y < s3d_tri->clip_t)
                        return;
                if (state->y > s3d_tri->clip_b)
                {
                        int diff_y = state->y - s3d_tri->clip_b;
                        
                        if (diff_y > y_count)
                                diff_y = y_count;
                        
                        state->base_u += (s3d_tri->TdUdY * diff_y);
                        state->base_v += (s3d_tri->TdVdY * diff_y);
                        state->base_z += (s3d_tri->TdZdY * diff_y);
                        state->base_r += (s3d_tri->TdRdY * diff_y);
                        state->base_g += (s3d_tri->TdGdY * diff_y);
                        state->base_b += (s3d_tri->TdBdY * diff_y);
                        state->base_a += (s3d_tri->TdAdY * diff_y);
                        state->base_d += (s3d_tri->TdDdY * diff_y);
                        state->base_w += (s3d_tri->TdWdY * diff_y);
                        state->x1 += (dx1 * diff_y);
                        state->x2 += (dx2 * diff_y);
                        state->y -= diff_y;
                        dest_offset -= s3d_tri->dest_str * diff_y;
                        z_offset -= s3d_tri->z_str;
                        y_count -= diff_y;
                }
                if ((state->y - y_count) < s3d_tri->clip_t)
                        y_count = (state->y - s3d_tri->clip_t) + 1;
        }

        dest_offset = s3d_tri->dest_base + (state->y * s3d_tri->dest_str);
        z_offset = s3d_tri->z_base + (state->y * s3d_tri->z_str);
        
        while (y_count > 0)
        {
                x  = (state->x1 + ((1 << 20) - 1)) >> 20;
                xe = (state->x2 + ((1 << 20) - 1)) >> 20;
                z = (state->base_z > 0) ? (state->base_z << 1) : 0;
                if (x_dir < 0)
                {
					x--;
					xe--;
                }

                if (((x != xe) && ((x_dir > 0) && (x < xe))) || ((x_dir < 0) && (x > xe)))
                {
                        dx = (x_dir > 0) ? ((31 - ((state->x1-1) >> 15)) & 0x1f) : (((state->x1-1) >> 15) & 0x1f);
                        x_offset = x_dir * (bpp + 1);
                        xz_offset = x_dir << 1;
                        if (x_dir > 0)
                                dx += 1;
                        state->r = state->base_r + ((s3d_tri->TdRdX * dx) >> 5);
                        state->g = state->base_g + ((s3d_tri->TdGdX * dx) >> 5);
                        state->b = state->base_b + ((s3d_tri->TdBdX * dx) >> 5);
                        state->a = state->base_a + ((s3d_tri->TdAdX * dx) >> 5);
                        state->u = state->base_u + ((s3d_tri->TdUdX * dx) >> 5);
                        state->v = state->base_v + ((s3d_tri->TdVdX * dx) >> 5);
                        state->w = state->base_w + ((s3d_tri->TdWdX * dx) >> 5);
                        state->d = state->base_d + ((s3d_tri->TdDdX * dx) >> 5);
                        z += ((s3d_tri->TdZdX * dx) >> 5);

                        if (s3d_tri->cmd_set & CMD_SET_HC)
                        {
                                if (x_dir > 0)
                                {
                                        if (x > s3d_tri->clip_r)
                                                goto tri_skip_line;
                                        if (xe < s3d_tri->clip_l)
                                                goto tri_skip_line;
                                        if (xe > s3d_tri->clip_r)
                                                xe = s3d_tri->clip_r + 1;
                                        if (x < s3d_tri->clip_l)
                                        {
                                                int diff_x = s3d_tri->clip_l - x;
                                                
                                                z += (s3d_tri->TdZdX * diff_x);
                                                state->u += (s3d_tri->TdUdX * diff_x);
                                                state->v += (s3d_tri->TdVdX * diff_x);
                                                state->r += (s3d_tri->TdRdX * diff_x);
                                                state->g += (s3d_tri->TdGdX * diff_x);
                                                state->b += (s3d_tri->TdBdX * diff_x);
                                                state->a += (s3d_tri->TdAdX * diff_x);
                                                state->d += (s3d_tri->TdDdX * diff_x);
                                                state->w += (s3d_tri->TdWdX * diff_x);
                                                
                                                x = s3d_tri->clip_l;
                                        }
                                }
                                else
                                {
                                        if (x < s3d_tri->clip_l)
                                                goto tri_skip_line;
                                        if (xe > s3d_tri->clip_r)
                                                goto tri_skip_line;
                                        if (xe < s3d_tri->clip_l)
                                                xe = s3d_tri->clip_l - 1;
                                        if (x > s3d_tri->clip_r)
                                        {
                                                int diff_x = x - s3d_tri->clip_r;
                                                
                                                z += (s3d_tri->TdZdX * diff_x);
                                                state->u += (s3d_tri->TdUdX * diff_x);
                                                state->v += (s3d_tri->TdVdX * diff_x);
                                                state->r += (s3d_tri->TdRdX * diff_x);
                                                state->g += (s3d_tri->TdGdX * diff_x);
                                                state->b += (s3d_tri->TdBdX * diff_x);
                                                state->a += (s3d_tri->TdAdX * diff_x);
                                                state->d += (s3d_tri->TdDdX * diff_x);
                                                state->w += (s3d_tri->TdWdX * diff_x);
                                                
                                                x = s3d_tri->clip_r;
                                        }
                                }
                        }

						svga->changedvram[(dest_offset & virge->vram_mask) >> 12] = changeframecount;

                        dest_addr = dest_offset + (x * (bpp + 1));
                        z_addr = z_offset + (x << 1);

                        x &= 0xfff;
                        xe &= 0xfff;
						
						while (x != xe) {
                                update = 1;
                                _x = x; _y = state->y;

                                if (use_z)
                                {
									src_z = *(uint16_t *)&vram[z_addr & virge->vram_mask];
									switch ((s3d_tri->cmd_set >> 20) & 7) {
										case 0:
											update = 0;
											break;
										case 1:
											if ((z >> 16) > src_z) {
												src_z = (z >> 16);
											} else
												update = 0;
											break;
										case 2:
											if ((z >> 16) == src_z) {
												src_z = (z >> 16);
											} else
												update = 0;
											break;
										case 3:
											if ((z >> 16) >= src_z) {
												src_z = (z >> 16);
											} else
												update = 0;
											break;
										case 4:
											if ((z >> 16) < src_z) {
												src_z = (z >> 16);
											} else
												update = 0;
											break;
										case 5:
											if ((z >> 16) != src_z) {
												src_z = (z >> 16);
											} else
												update = 0;
											break;
										case 6:
											if ((z >> 16) <= src_z) {
												src_z = (z >> 16);
											} else
												update = 0;
											break;
										case 7:
											src_z = (z >> 16);
											break;
									}
								}

                                if (update)
                                {
									uint32_t dest_col;

									dest_pixel(state);

									if (s3d_tri->cmd_set & CMD_SET_FE)
									{
											int a = state->a >> 7;
											state->dest_rgba.r = ((state->dest_rgba.r * a) + (s3d_tri->fog_r * (255 - a))) / 255;
											state->dest_rgba.g = ((state->dest_rgba.g * a) + (s3d_tri->fog_g * (255 - a))) / 255;
											state->dest_rgba.b = ((state->dest_rgba.b * a) + (s3d_tri->fog_b * (255 - a))) / 255;
									}

									if (s3d_tri->cmd_set & CMD_SET_ABC_ENABLE)
									{
											switch (bpp)
											{
													case 0: /*8 bpp*/
													/*Not implemented yet*/
													break;
													case 1: /*16 bpp*/
													src_col = *(uint16_t *)&vram[dest_addr & virge->vram_mask];
													RGB15_TO_24(src_col, src_r, src_g, src_b);
													break;
													case 2: /*24 bpp*/
													src_col = (*(uint32_t *)&vram[dest_addr & virge->vram_mask]) & 0xffffff;
													RGB24_TO_24(src_col, src_r, src_g, src_b);
													break;
											}

											state->dest_rgba.r = ((state->dest_rgba.r * state->dest_rgba.a) + (src_r * (255 - state->dest_rgba.a))) / 255;
											state->dest_rgba.g = ((state->dest_rgba.g * state->dest_rgba.a) + (src_g * (255 - state->dest_rgba.a))) / 255;
											state->dest_rgba.b = ((state->dest_rgba.b * state->dest_rgba.a) + (src_b * (255 - state->dest_rgba.a))) / 255;
									}

									switch (bpp)
									{
											case 0: /*8 bpp*/ 
											/*Not implemented yet*/
											break;
											case 1: /*16 bpp*/
											RGB15(state->dest_rgba.r, state->dest_rgba.g, state->dest_rgba.b, dest_col);
											*(uint16_t *)&vram[dest_addr & virge->vram_mask] = dest_col;
											svga->changedvram[(dest_addr & virge->vram_mask) >> 12] = changeframecount;
											break;
											case 2: /*24 bpp*/
											dest_col = RGB24(state->dest_rgba.r, state->dest_rgba.g, state->dest_rgba.b);
											*(uint8_t *)&vram[dest_addr & virge->vram_mask] = dest_col & 0xff;
											*(uint8_t *)&vram[(dest_addr + 1) & virge->vram_mask] = (dest_col >> 8) & 0xff;
											*(uint8_t *)&vram[(dest_addr + 2) & virge->vram_mask] = (dest_col >> 16) & 0xff;
											svga->changedvram[(dest_addr & virge->vram_mask) >> 12] = changeframecount;
											break;
									}
								}

								if (use_z && (s3d_tri->cmd_set & CMD_SET_ZUP)) {
									*(uint16_t *)&vram[z_addr & virge->vram_mask] = src_z;
									svga->changedvram[(z_addr & virge->vram_mask) >> 12] = changeframecount;
								}

                                z += s3d_tri->TdZdX;
                                state->u += s3d_tri->TdUdX;
                                state->v += s3d_tri->TdVdX;
                                state->r += s3d_tri->TdRdX;
                                state->g += s3d_tri->TdGdX;
                                state->b += s3d_tri->TdBdX;
                                state->a += s3d_tri->TdAdX;
                                state->d += s3d_tri->TdDdX;
                                state->w += s3d_tri->TdWdX;
                                dest_addr += x_offset;
                                z_addr += xz_offset;
	
								x = (x + x_dir) & 0xfff;
                        }
                }

				y_count--;
				
tri_skip_line:
                state->x1 += dx1;
                state->x2 += dx2;
                state->base_u += s3d_tri->TdUdY;
                state->base_v += s3d_tri->TdVdY;
                state->base_z += s3d_tri->TdZdY;
                state->base_r += s3d_tri->TdRdY;
                state->base_g += s3d_tri->TdGdY;
                state->base_b += s3d_tri->TdBdY;
                state->base_a += s3d_tri->TdAdY;
                state->base_d += s3d_tri->TdDdY;
                state->base_w += s3d_tri->TdWdY;
                state->y--;
                dest_offset -= s3d_tri->dest_str;
                z_offset -= s3d_tri->z_str;
        }
}

static int tex_size[8] =
{
        4*2,
        2*2,
        2*2,
        1*2,
        2/1,
        2/1,
        1*2,
        1*2
};

static void s3_virge_triangle(virge_t *virge, s3d_t *s3d_tri)
{
        s3d_state_t state;

        uint32_t tex_base;
        int c;

        uint64_t start_time = plat_timer_read();
        uint64_t end_time;

        state.tbu = s3d_tri->tbu << 11;
        state.tbv = s3d_tri->tbv << 11;
        
        state.max_d = (s3d_tri->cmd_set >> 8) & 15;
        
        state.tex_bdr_clr = s3d_tri->tex_bdr_clr;
        
        state.cmd_set = s3d_tri->cmd_set;

        state.base_u = s3d_tri->tus;
        state.base_v = s3d_tri->tvs;
        state.base_z = s3d_tri->tzs;
        state.base_r = (int32_t)s3d_tri->trs;
        state.base_g = (int32_t)s3d_tri->tgs;
        state.base_b = (int32_t)s3d_tri->tbs;
        state.base_a = (int32_t)s3d_tri->tas;
        state.base_d = s3d_tri->tds;
        state.base_w = s3d_tri->tws;
        
        tex_base = s3d_tri->tex_base;
        for (c = 9; c >= 0; c--)
        {
                state.texture[c] = (uint16_t *)&virge->svga.vram[tex_base];
                if (c <= state.max_d)
                        tex_base += ((1 << (c*2)) * tex_size[(s3d_tri->cmd_set >> 5) & 7]) / 2;
        }

        switch ((s3d_tri->cmd_set >> 27) & 0xf)
        {
                case 0:
                dest_pixel = dest_pixel_gouraud_shaded_triangle;
                break;
                case 1:
                case 5:
                switch ((s3d_tri->cmd_set >> 15) & 0x3)
                {
                        case 0:
                        dest_pixel = dest_pixel_lit_texture_reflection;
                        break;
                        case 1:
                        dest_pixel = dest_pixel_lit_texture_modulate;
                        break;
                        case 2:
                        dest_pixel = dest_pixel_lit_texture_decal;
                        break;
                        default:
                        s3_virge_log("bad triangle type %x\n", (s3d_tri->cmd_set >> 27) & 0xf);
                        return;
                }
                break;
                case 2:
                case 6:
                dest_pixel = dest_pixel_unlit_texture_triangle;
                break;
                default:
                s3_virge_log("bad triangle type %x\n", (s3d_tri->cmd_set >> 27) & 0xf);
                return;
        }        
        
        switch (((s3d_tri->cmd_set >> 12) & 7) | ((s3d_tri->cmd_set & (1 << 29)) ? 8 : 0))
        {
                case 0: case 1:
                tex_sample = tex_sample_mipmap;
                break;
                case 2: case 3:
                tex_sample = virge->bilinear_enabled ? tex_sample_mipmap_filter : tex_sample_mipmap;
                break;
                case 4: case 5:
                tex_sample = tex_sample_normal;
                break;
                case 6: case 7:
                tex_sample = virge->bilinear_enabled ? tex_sample_normal_filter : tex_sample_normal;
                break;
                case (0 | 8): case (1 | 8):
		if (virge->chip == S3_VIRGEDX || virge->chip >= S3_VIRGEGX2)
                        tex_sample = tex_sample_persp_mipmap_375;
                else
                        tex_sample = tex_sample_persp_mipmap;
                break;
                case (2 | 8): case (3 | 8):
		if (virge->chip == S3_VIRGEDX || virge->chip >= S3_VIRGEGX2)
                        tex_sample = virge->bilinear_enabled ? tex_sample_persp_mipmap_filter_375 : tex_sample_persp_mipmap_375;
                else
                        tex_sample = virge->bilinear_enabled ? tex_sample_persp_mipmap_filter : tex_sample_persp_mipmap;
                break;
                case (4 | 8): case (5 | 8):
		if (virge->chip == S3_VIRGEDX || virge->chip >= S3_VIRGEGX2)
                        tex_sample = tex_sample_persp_normal_375;
                else
                        tex_sample = tex_sample_persp_normal;
                break;
                case (6 | 8): case (7 | 8):
		if (virge->chip == S3_VIRGEDX || virge->chip >= S3_VIRGEGX2)
                        tex_sample = virge->bilinear_enabled ? tex_sample_persp_normal_filter_375 : tex_sample_persp_normal_375;
                else
                        tex_sample = virge->bilinear_enabled ? tex_sample_persp_normal_filter : tex_sample_persp_normal;
                break;
        }
        
        switch ((s3d_tri->cmd_set >> 5) & 7)
        {
                case 0:
                tex_read = (s3d_tri->cmd_set & CMD_SET_TWE) ? tex_ARGB8888 : tex_ARGB8888_nowrap;
                break;
                case 1:
                tex_read = (s3d_tri->cmd_set & CMD_SET_TWE) ? tex_ARGB4444 : tex_ARGB4444_nowrap;
                break;
                case 2:
                tex_read = (s3d_tri->cmd_set & CMD_SET_TWE) ? tex_ARGB1555 : tex_ARGB1555_nowrap;
                break;
                default:
                s3_virge_log("bad texture type %i\n", (s3d_tri->cmd_set >> 5) & 7);
                tex_read = (s3d_tri->cmd_set & CMD_SET_TWE) ? tex_ARGB1555 : tex_ARGB1555_nowrap;
                break;
        }

        state.y  = s3d_tri->tys;
        state.x1 = s3d_tri->txs;
        state.x2 = s3d_tri->txend01;
        tri(virge, s3d_tri, &state, s3d_tri->ty01, s3d_tri->TdXdY02, s3d_tri->TdXdY01);
        state.x2 = s3d_tri->txend12;
        tri(virge, s3d_tri, &state, s3d_tri->ty12, s3d_tri->TdXdY02, s3d_tri->TdXdY12);
		
        end_time = plat_timer_read();
        
        virge_time += end_time - start_time;
}

static void s3_virge_hwcursor_draw(svga_t *svga, int displine)
{
        virge_t *virge = (virge_t *)svga->p;
        int x;
        uint16_t dat[2];
        int xx;
        int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
        uint32_t fg, bg;

        if (svga->interlace && svga->hwcursor_oddeven)
                svga->hwcursor_latch.addr += 16;

        switch (svga->bpp)
        {               
                case 15:
                fg = video_15to32[virge->hwc_fg_col & 0xffff];
                bg = video_15to32[virge->hwc_bg_col & 0xffff];
                break;
                
                case 16:
                fg = video_16to32[virge->hwc_fg_col & 0xffff];
                bg = video_16to32[virge->hwc_bg_col & 0xffff];
                break;
                
                case 24: case 32:
                fg = virge->hwc_fg_col;
                bg = virge->hwc_bg_col;
                break;

                default:
                fg = svga->pallook[virge->hwc_fg_col & 0xff];
                bg = svga->pallook[virge->hwc_bg_col & 0xff];
                break;
        }

        for (x = 0; x < 64; x += 16)
        {
                dat[0] = (svga->vram[svga->hwcursor_latch.addr & virge->vram_mask]     << 8) | svga->vram[(svga->hwcursor_latch.addr + 1) & virge->vram_mask];
                dat[1] = (svga->vram[(svga->hwcursor_latch.addr + 2) & virge->vram_mask] << 8) | svga->vram[(svga->hwcursor_latch.addr + 3) & virge->vram_mask];
                if (svga->crtc[0x55] & 0x10)
                {
                        /*X11*/
                        for (xx = 0; xx < 16; xx++)
                        {
                                if (offset >= 0)
                                {
                                        if (dat[0] & 0x8000)
						buffer32->line[displine][offset + svga->x_add]  = (dat[1] & 0x8000) ? fg : bg;
                                }
                           
                                offset++;
                                dat[0] <<= 1;
                                dat[1] <<= 1;
                        }
                }
                else
                {
                        /*Windows*/
                        for (xx = 0; xx < 16; xx++)
                        {
                                if (offset >= 0)
                                {
                                        if (!(dat[0] & 0x8000))
						buffer32->line[displine][offset + svga->x_add]  = (dat[1] & 0x8000) ? fg : bg;
                                        else if (dat[1] & 0x8000)
						buffer32->line[displine][offset + svga->x_add] ^= 0xffffff;
                                }
                           
                                offset++;
                                dat[0] <<= 1;
                                dat[1] <<= 1;
                        }
                }
                svga->hwcursor_latch.addr += 4;
        }
        if (svga->interlace && !svga->hwcursor_oddeven)
                svga->hwcursor_latch.addr += 16;
}

#define DECODE_YCbCr()                                                  \
        do                                                              \
        {                                                               \
                int c;                                                  \
                                                                        \
                for (c = 0; c < 2; c++)                                 \
                {                                                       \
                        uint8_t y1, y2;                                 \
                        int8_t Cr, Cb;                                  \
                        int dR, dG, dB;                                 \
                                                                        \
                        y1 = src[0];                                    \
                        Cr = src[1] - 0x80;                             \
                        y2 = src[2];                                    \
                        Cb = src[3] - 0x80;                             \
                        src += 4;                                       \
                                                                        \
                        dR = (359*Cr) >> 8;                             \
                        dG = (88*Cb + 183*Cr) >> 8;                     \
                        dB = (453*Cb) >> 8;                             \
                                                                        \
                        r[x_write] = y1 + dR;                           \
                        CLAMP(r[x_write]);                              \
                        g[x_write] = y1 - dG;                           \
                        CLAMP(g[x_write]);                              \
                        b[x_write] = y1 + dB;                           \
                        CLAMP(b[x_write]);                              \
                                                                        \
                        r[x_write+1] = y2 + dR;                         \
                        CLAMP(r[x_write+1]);                            \
                        g[x_write+1] = y2 - dG;                         \
                        CLAMP(g[x_write+1]);                            \
                        b[x_write+1] = y2 + dB;                         \
                        CLAMP(b[x_write+1]);                            \
                                                                        \
                        x_write = (x_write + 2) & 7;                    \
                }                                                       \
        } while (0)

/*Both YUV formats are untested*/
#define DECODE_YUV211()                                         \
        do                                                      \
        {                                                       \
                uint8_t y1, y2, y3, y4;                         \
                int8_t U, V;                                    \
                int dR, dG, dB;                                 \
                                                                \
                U = src[0] - 0x80;                              \
                y1 = (298 * (src[1] - 16)) >> 8;                \
                y2 = (298 * (src[2] - 16)) >> 8;                \
                V = src[3] - 0x80;                              \
                y3 = (298 * (src[4] - 16)) >> 8;                \
                y4 = (298 * (src[5] - 16)) >> 8;                \
                src += 6;                                       \
                                                                \
                dR = (309*V) >> 8;                              \
                dG = (100*U + 208*V) >> 8;                      \
                dB = (516*U) >> 8;                              \
                                                                \
                r[x_write] = y1 + dR;                           \
                CLAMP(r[x_write]);                              \
                g[x_write] = y1 - dG;                           \
                CLAMP(g[x_write]);                              \
                b[x_write] = y1 + dB;                           \
                CLAMP(b[x_write]);                              \
                                                                \
                r[x_write+1] = y2 + dR;                         \
                CLAMP(r[x_write+1]);                            \
                g[x_write+1] = y2 - dG;                         \
                CLAMP(g[x_write+1]);                            \
                b[x_write+1] = y2 + dB;                         \
                CLAMP(b[x_write+1]);                            \
                                                                \
                r[x_write+2] = y3 + dR;                         \
                CLAMP(r[x_write+2]);                            \
                g[x_write+2] = y3 - dG;                         \
                CLAMP(g[x_write+2]);                            \
                b[x_write+2] = y3 + dB;                         \
                CLAMP(b[x_write+2]);                            \
                                                                \
                r[x_write+3] = y4 + dR;                         \
                CLAMP(r[x_write+3]);                            \
                g[x_write+3] = y4 - dG;                         \
                CLAMP(g[x_write+3]);                            \
                b[x_write+3] = y4 + dB;                         \
                CLAMP(b[x_write+3]);                            \
                                                                \
                x_write = (x_write + 4) & 7;                    \
        } while (0)

#define DECODE_YUV422()                                                 \
        do                                                              \
        {                                                               \
                int c;                                                  \
                                                                        \
                for (c = 0; c < 2; c++)                                 \
                {                                                       \
                        uint8_t y1, y2;                                 \
                        int8_t U, V;                                    \
                        int dR, dG, dB;                                 \
                                                                        \
                        U = src[0] - 0x80;                              \
                        y1 = (298 * (src[1] - 16)) >> 8;                \
                        V = src[2] - 0x80;                              \
                        y2 = (298 * (src[3] - 16)) >> 8;                \
                        src += 4;                                       \
                                                                        \
                        dR = (309*V) >> 8;                              \
                        dG = (100*U + 208*V) >> 8;                      \
                        dB = (516*U) >> 8;                              \
                                                                        \
                        r[x_write] = y1 + dR;                           \
                        CLAMP(r[x_write]);                              \
                        g[x_write] = y1 - dG;                           \
                        CLAMP(g[x_write]);                              \
                        b[x_write] = y1 + dB;                           \
                        CLAMP(b[x_write]);                              \
                                                                        \
                        r[x_write+1] = y2 + dR;                         \
                        CLAMP(r[x_write+1]);                            \
                        g[x_write+1] = y2 - dG;                         \
                        CLAMP(g[x_write+1]);                            \
                        b[x_write+1] = y2 + dB;                         \
                        CLAMP(b[x_write+1]);                            \
                                                                        \
                        x_write = (x_write + 2) & 7;                    \
                }                                                       \
        } while (0)

#define DECODE_RGB555()                                                 \
        do                                                              \
        {                                                               \
                int c;                                                  \
                                                                        \
                for (c = 0; c < 4; c++)                                 \
                {                                                       \
                        uint16_t dat;                                   \
                                                                        \
                        dat = *(uint16_t *)src;                         \
                        src += 2;                                       \
                                                                        \
                        r[x_write + c] = ((dat & 0x001f) << 3) | ((dat & 0x001f) >> 2); \
                        g[x_write + c] = ((dat & 0x03e0) >> 2) | ((dat & 0x03e0) >> 7); \
                        b[x_write + c] = ((dat & 0x7c00) >> 7) | ((dat & 0x7c00) >> 12); \
                }                                                       \
                x_write = (x_write + 4) & 7;                            \
        } while (0)

#define DECODE_RGB565()                                                 \
        do                                                              \
        {                                                               \
                int c;                                                  \
                                                                        \
                for (c = 0; c < 4; c++)                                 \
                {                                                       \
                        uint16_t dat;                                   \
                                                                        \
                        dat = *(uint16_t *)src;                         \
                        src += 2;                                       \
                                                                        \
                        r[x_write + c] = ((dat & 0x001f) << 3) | ((dat & 0x001f) >> 2); \
                        g[x_write + c] = ((dat & 0x07e0) >> 3) | ((dat & 0x07e0) >> 9); \
                        b[x_write + c] = ((dat & 0xf800) >> 8) | ((dat & 0xf800) >> 13); \
                }                                                       \
                x_write = (x_write + 4) & 7;                            \
        } while (0)

#define DECODE_RGB888()                                                 \
        do                                                              \
        {                                                               \
                int c;                                                  \
                                                                        \
                for (c = 0; c < 4; c++)                                 \
                {                                                       \
                        r[x_write + c] = src[0];                        \
                        g[x_write + c] = src[1];                        \
                        b[x_write + c] = src[2];                        \
                        src += 3;                                       \
                }                                                       \
                x_write = (x_write + 4) & 7;                            \
        } while (0)

#define DECODE_XRGB8888()                                               \
        do                                                              \
        {                                                               \
                int c;                                                  \
                                                                        \
                for (c = 0; c < 4; c++)                                 \
                {                                                       \
                        r[x_write + c] = src[0];                        \
                        g[x_write + c] = src[1];                        \
                        b[x_write + c] = src[2];                        \
                        src += 4;                                       \
                }                                                       \
                x_write = (x_write + 4) & 7;                            \
        } while (0)

#define OVERLAY_SAMPLE()                        \
        do                                      \
        {                                       \
                switch (virge->streams.sdif)    \
                {                               \
                        case 1:                 \
                        DECODE_YCbCr();         \
                        break;                  \
                        case 2:                 \
                        DECODE_YUV422();        \
                        break;                  \
                        case 3:                 \
                        DECODE_RGB555();        \
                        break;                  \
                        case 4:                 \
                        DECODE_YUV211();        \
                        break;                  \
                        case 5:                 \
                        DECODE_RGB565();        \
                        break;                  \
                        case 6:                 \
                        DECODE_RGB888();        \
                        break;                  \
                        case 7:                 \
                        default:                \
                        DECODE_XRGB8888();      \
                        break;                  \
                }                               \
        } while (0)

static void s3_virge_overlay_draw(svga_t *svga, int displine)
{
        virge_t *virge = (virge_t *)svga->p;
        int offset = (virge->streams.sec_x - virge->streams.pri_x) + 1;
        int h_acc = virge->streams.dda_horiz_accumulator;
        int r[8], g[8], b[8];
        int x_size, x_read = 4, x_write = 4;
        int x;
        uint32_t *p;
        uint8_t *src = &svga->vram[svga->overlay_latch.addr];
        
        p = &(buffer32->line[displine][offset + svga->x_add]);

        if ((offset + virge->streams.sec_w) > virge->streams.pri_w)
                x_size = (virge->streams.pri_w - virge->streams.sec_x) + 1;
        else
                x_size = virge->streams.sec_w + 1;

        OVERLAY_SAMPLE();
        
        for (x = 0; x < x_size; x++)
        {
                *p++ = r[x_read] | (g[x_read] << 8) | (b[x_read] << 16);

                h_acc += virge->streams.k1_horiz_scale;
                if (h_acc >= 0)
                {
                        if ((x_read ^ (x_read + 1)) & ~3)
                                OVERLAY_SAMPLE();
                        x_read = (x_read + 1) & 7;

                        h_acc += (virge->streams.k2_horiz_scale - virge->streams.k1_horiz_scale);
                }
        }

        svga->overlay_latch.v_acc += virge->streams.k1_vert_scale;
        if (svga->overlay_latch.v_acc >= 0)
        {
                svga->overlay_latch.v_acc += (virge->streams.k2_vert_scale - virge->streams.k1_vert_scale);
                svga->overlay_latch.addr += virge->streams.sec_stride;
        }
}

static uint8_t s3_virge_pci_read(int func, int addr, void *p)
{
        virge_t *virge = (virge_t *)p;
        svga_t *svga = &virge->svga;
	uint8_t ret = 0;

        switch (addr) {
                case 0x00: ret = 0x33; break; /*'S3'*/
                case 0x01: ret = 0x53; break;
                
                case 0x02: ret = virge->virge_id_low; break;
                case 0x03: ret = virge->virge_id_high; break;

                case PCI_REG_COMMAND: ret = virge->pci_regs[PCI_REG_COMMAND] & 0x27; break;
                
                case 0x07: ret = virge->pci_regs[0x07] & 0x36; break;
                                
                case 0x08: ret = 0; break; /*Revision ID*/
                case 0x09: ret = 0; break; /*Programming interface*/
                
                case 0x0a: ret = 0x00; break; /*Supports VGA interface*/
                case 0x0b: ret = 0x03; break;

                case 0x0d: ret = virge->pci_regs[0x0d] & 0xf8; break;
                                
                case 0x10: ret = 0x00; break;/*Linear frame buffer address*/
                case 0x11: ret = 0x00; break;
                case 0x12: ret = 0x00; break;
                case 0x13: ret = svga->crtc[0x59] & 0xfc; break;

				case 0x2c: ret = 0x33;	break;	/* Subsystem vendor ID */
				case 0x2d: ret = 0x53;	break;
				case 0x2e: ret = virge->virge_id_low; break;
				case 0x2f: ret = virge->virge_id_high; break;

                case 0x30: ret = virge->pci_regs[0x30] & 0x01; break; /*BIOS ROM address*/
                case 0x31: ret = 0x00; break;
                case 0x32: ret = virge->pci_regs[0x32]; break;
                case 0x33: ret = virge->pci_regs[0x33]; break;

                case 0x3c: ret = virge->pci_regs[0x3c]; break;
                                
                case 0x3d: ret = PCI_INTA; break; /*INTA*/
                
                case 0x3e: ret = 0x04; break;
                case 0x3f: ret = 0xff; break;
                
        }
        return ret;
}

static void s3_virge_pci_write(int func, int addr, uint8_t val, void *p)
{
        virge_t *virge = (virge_t *)p;
        svga_t *svga = &virge->svga;
        switch (addr)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                case 0x3d: case 0x3e: case 0x3f:
		return;		
		
                case PCI_REG_COMMAND:
                if (val & PCI_COMMAND_IO)
                {
                        io_removehandler(0x03c0, 0x0020, s3_virge_in, NULL, NULL, s3_virge_out, NULL, NULL, virge);
                        io_sethandler(0x03c0, 0x0020, s3_virge_in, NULL, NULL, s3_virge_out, NULL, NULL, virge);
                }
                else
                        io_removehandler(0x03c0, 0x0020, s3_virge_in, NULL, NULL, s3_virge_out, NULL, NULL, virge);
		virge->pci_regs[PCI_REG_COMMAND] = val & 0x27;
		s3_virge_updatemapping(virge); 
                return;
                case 0x07:
                virge->pci_regs[0x07] = val & 0x3e;
                return;
		case 0x0d: 
                virge->pci_regs[0x0d] = val & 0xf8;
                return;

		case 0x13:
		svga->crtc[0x59] = val & 0xfc;
		s3_virge_updatemapping(virge);
		return;

                case 0x30: case 0x32: case 0x33:
                virge->pci_regs[addr] = val;
                if (virge->pci_regs[0x30] & 0x01)
                {
                        uint32_t biosaddr = (virge->pci_regs[0x32] << 16) | (virge->pci_regs[0x33] << 24);
                        if (virge->chip == S3_VIRGEGX2)
							mem_mapping_set_addr(&virge->bios_rom.mapping, biosaddr, 0x10000);
						else
							mem_mapping_set_addr(&virge->bios_rom.mapping, biosaddr, 0x8000);
                }
                else
                {
                        mem_mapping_disable(&virge->bios_rom.mapping);
                }
                return;
                case 0x3c: 
                virge->pci_regs[0x3c] = val;
                return;
        }
}

static void s3_virge_reset(void *priv)
{
	virge_t *virge = (virge_t *) priv;
    svga_t *svga = &virge->svga;

    memset(svga->crtc, 0x00, sizeof(svga->crtc));
    svga->crtc[0] = 63;
    svga->crtc[6] = 255;
    svga->dispontime = 1000ull << 32;
    svga->dispofftime = 1000ull << 32;
    svga->bpp = 8;

    io_removehandler(0x03c0, 0x0020, s3_virge_in, NULL, NULL, s3_virge_out, NULL, NULL, virge);
    io_sethandler(0x03c0, 0x0020, s3_virge_in, NULL, NULL, s3_virge_out, NULL, NULL, virge);

    memset(virge->pci_regs, 0x00, 256);

       	virge->pci_regs[PCI_REG_COMMAND] = 3;
       	virge->pci_regs[0x05] = 0;
        virge->pci_regs[0x06] = 0;
       	virge->pci_regs[0x07] = 2;
        virge->pci_regs[0x32] = 0x0c;
       	virge->pci_regs[0x3d] = 1; 
        virge->pci_regs[0x3e] = 4;
       	virge->pci_regs[0x3f] = 0xff;

	switch(virge->local) {
		case S3_VIRGE_325:
		case S3_DIAMOND_STEALTH3D_2000:
			virge->svga.crtc[0x59] = 0x70;
			break;
		case S3_DIAMOND_STEALTH3D_3000:
			virge->svga.crtc[0x59] = 0x70;
			break;
		case S3_VIRGE_GX2:
		case S3_DIAMOND_STEALTH3D_4000:
			virge->svga.crtc[0x6c] = 1;
			virge->svga.crtc[0x59] = 0x70;
			break;

		case S3_TRIO_3D2X:
			virge->svga.crtc[0x6c] = 1;
			virge->svga.crtc[0x59] = 0x70;
			break;

		default:
			virge->svga.crtc[0x6c] = 1;
			virge->svga.crtc[0x59] = 0x70;
			break;
	}

	if (virge->chip >= S3_VIRGEGX2)
		virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (0 << 5);
	else {
		switch (virge->memory_size) {
			case 2:
				virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (4 << 5);
				break;
			case 8:
				virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (3 << 5);
				break;
			case 4:
				virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (0 << 5);
				break;
		}	
	}

	virge->svga.crtc[0x37] = 1 | (7 << 5);
	virge->svga.crtc[0x53] = 8;

    mem_mapping_disable(&virge->bios_rom.mapping);

	memset(virge->dmabuffer, 0, 65536);

    s3_virge_updatemapping(virge);

	mem_mapping_disable(&virge->mmio_mapping);
	mem_mapping_disable(&virge->new_mmio_mapping);
}

static void *s3_virge_init(const device_t *info)
{
	const char *bios_fn;
        virge_t *virge = malloc(sizeof(virge_t));

        memset(virge, 0, sizeof(virge_t));

        virge->bilinear_enabled = device_get_config_int("bilinear");
        virge->dithering_enabled = device_get_config_int("dithering");
        if (info->local >= S3_VIRGE_GX2)
			virge->memory_size = 4;
		else
			virge->memory_size = device_get_config_int("memory");
		
        
	switch(info->local) {
		case S3_VIRGE_325:
			bios_fn = ROM_VIRGE_325;
			break;
		case S3_DIAMOND_STEALTH3D_2000:
			bios_fn = ROM_DIAMOND_STEALTH3D_2000;
			break;
		case S3_DIAMOND_STEALTH3D_3000:
			bios_fn = ROM_DIAMOND_STEALTH3D_3000;
			break;
		case S3_VIRGE_DX:
			bios_fn = ROM_VIRGE_DX;
			break;
		case S3_DIAMOND_STEALTH3D_2000PRO:
			bios_fn = ROM_DIAMOND_STEALTH3D_2000PRO;
			break;
		case S3_VIRGE_GX:
			bios_fn = ROM_VIRGE_GX;
			break;
		case S3_VIRGE_GX2:
			bios_fn = ROM_VIRGE_GX2;
			break;
		case S3_DIAMOND_STEALTH3D_4000:
			bios_fn = ROM_DIAMOND_STEALTH3D_4000;
			break;
		case S3_TRIO_3D2X:
			bios_fn = ROM_TRIO3D2X;
			break;
		default:
			free(virge);
			return NULL;
	}

        svga_init(info, &virge->svga, virge, virge->memory_size << 20,
                   s3_virge_recalctimings,
                   s3_virge_in, s3_virge_out,
                   s3_virge_hwcursor_draw,
                   s3_virge_overlay_draw);
	virge->svga.hwcursor.ysize = 64;

	if (info->local == S3_VIRGE_GX2)
		rom_init(&virge->bios_rom, (char *) bios_fn, 0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
	else
		rom_init(&virge->bios_rom, (char *) bios_fn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
	
	mem_mapping_disable(&virge->bios_rom.mapping);

        mem_mapping_add(&virge->linear_mapping,   0, 0, svga_read_linear,
                                                        svga_readw_linear,
                                                        svga_readl_linear,
                                                        svga_write_linear,
                                                        svga_writew_linear,
                                                        svga_writel_linear,
                                                        NULL,
                                                        MEM_MAPPING_EXTERNAL,
                                                        &virge->svga);
        mem_mapping_add(&virge->mmio_mapping,     0, 0, s3_virge_mmio_read,
                                                        s3_virge_mmio_read_w,
                                                        s3_virge_mmio_read_l,
                                                        s3_virge_mmio_write,
                                                        s3_virge_mmio_write_w,
                                                        s3_virge_mmio_write_l,
                                                        NULL,
                                                        MEM_MAPPING_EXTERNAL,
                                                        virge);
        mem_mapping_add(&virge->new_mmio_mapping, 0, 0, s3_virge_mmio_read,
                                                        s3_virge_mmio_read_w,
                                                        s3_virge_mmio_read_l,
                                                        s3_virge_mmio_write,
                                                        s3_virge_mmio_write_w,
                                                        s3_virge_mmio_write_l,
                                                        NULL,
                                                        MEM_MAPPING_EXTERNAL,
                                                        virge);

        io_sethandler(0x03c0, 0x0020, s3_virge_in, NULL, NULL, s3_virge_out, NULL, NULL, virge);

        virge->pci_regs[PCI_REG_COMMAND] = 3;
       	virge->pci_regs[0x05] = 0;
        virge->pci_regs[0x06] = 0;
       	virge->pci_regs[0x07] = 2;
        virge->pci_regs[0x32] = 0x0c;
       	virge->pci_regs[0x3d] = 1; 
        virge->pci_regs[0x3e] = 4;
       	virge->pci_regs[0x3f] = 0xff;
        
        virge->virge_rev = 0;
        virge->virge_id = 0xe1;

	switch(info->local) {
		case S3_VIRGE_325:
		case S3_DIAMOND_STEALTH3D_2000:
			virge->svga.decode_mask = (4 << 20) - 1;
			virge->virge_id_high = 0x56;
			virge->virge_id_low = 0x31;
			virge->svga.crtc[0x59] = 0x70;
			virge->chip = S3_VIRGE;
			video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_diamond_stealth3d_2000_pci);
			break;
		case S3_DIAMOND_STEALTH3D_3000:
			virge->svga.decode_mask = (8 << 20) - 1;
			virge->virge_id_high = 0x88;
			virge->virge_id_low = 0x3d;
			virge->svga.crtc[0x59] = 0x70;
			virge->chip = S3_VIRGEVX;
			video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_diamond_stealth3d_3000_pci);
			break;
		case S3_VIRGE_GX2:
		case S3_DIAMOND_STEALTH3D_4000:
			virge->svga.decode_mask = (4 << 20) - 1;
			virge->virge_id_high = 0x8a;
			virge->virge_id_low = 0x10;
			virge->svga.crtc[0x6c] = 1;
			virge->svga.crtc[0x59] = 0x70;
			virge->svga.vblank_start = s3_virge_vblank_start;
			virge->chip = S3_VIRGEGX2;
			video_inform(VIDEO_FLAG_TYPE_SPECIAL, (info->flags & DEVICE_AGP) ? &timing_virge_agp : &timing_virge_dx_pci);
			break;

		case S3_TRIO_3D2X:
			virge->svga.decode_mask = (4 << 20) - 1;
			virge->virge_id_high = 0x8a;
			virge->virge_id_low = 0x13;
			virge->virge_rev = 0x01;
			virge->svga.crtc[0x6c] = 1;
			virge->svga.crtc[0x59] = 0x70;
			virge->svga.vblank_start = s3_virge_vblank_start;
			virge->chip = S3_TRIO3D2X;
			video_inform(VIDEO_FLAG_TYPE_SPECIAL, (info->flags & DEVICE_AGP) ? &timing_virge_agp : &timing_virge_dx_pci);
			break;

		default:
			virge->svga.decode_mask = (4 << 20) - 1;
			virge->virge_id_high = 0x8a;
			virge->virge_id_low = 0x01;
			virge->svga.crtc[0x6c] = 1;
			virge->svga.crtc[0x59] = 0x70;
			virge->svga.vblank_start = s3_virge_vblank_start;
			virge->chip = S3_VIRGEDX;
			video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_virge_dx_pci);
			break;
	}

	if (virge->chip >= S3_VIRGEGX2) {
		virge->vram_mask = (4 << 20) - 1;
		virge->svga.vram_mask = (4 << 20) - 1;
		virge->svga.vram_max = 4 << 20;
		virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (0 << 5);
	} else {
		switch (virge->memory_size) {
			case 2:
				virge->vram_mask = (2 << 20) - 1;
				virge->svga.vram_mask = (2 << 20) - 1;
				virge->svga.vram_max = 2 << 20;
				virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (4 << 5);
				break;
			case 8:
				virge->vram_mask = (8 << 20) - 1;
				virge->svga.vram_mask = (8 << 20) - 1;
				virge->svga.vram_max = 8 << 20;
				virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (3 << 5);
				break;
			case 4:
				virge->vram_mask = (4 << 20) - 1;
				virge->svga.vram_mask = (4 << 20) - 1;
				virge->svga.vram_max = 4 << 20;
				virge->svga.crtc[0x36] = 2 | (0 << 2) | (1 << 4) | (0 << 5);
				break;
		}	
	}

	virge->svga.crtc[0x37] = 1 | (7 << 5);
	virge->svga.crtc[0x53] = 8;

	memset(virge->dmabuffer, 0, 65536);

    virge->card = pci_add_card((info->flags & DEVICE_AGP) ? PCI_ADD_AGP : PCI_ADD_VIDEO, s3_virge_pci_read, s3_virge_pci_write, virge);

	virge->i2c = i2c_gpio_init("ddc_s3_virge");
	virge->ddc = ddc_init(i2c_gpio_get_bus(virge->i2c));
 
	virge->svga.packed_chain4 = 1;

	virge->wake_render_thread = thread_create_event();
	virge->wake_main_thread = thread_create_event();
	virge->not_full_event = thread_create_event();
	virge->render_thread_run = 1;
	virge->render_thread = thread_create(render_thread, virge);

	timer_add(&virge->tri_timer, s3_virge_tri_timer, virge, 0);

	virge->local = info->local;
 
	return virge;
}

static void s3_virge_close(void *p)
{
        virge_t *virge = (virge_t *)p;

		virge->render_thread_run = 0;
        thread_set_event(virge->wake_render_thread);
        thread_wait(virge->render_thread, -1);
		thread_destroy_event(virge->not_full_event);
		thread_destroy_event(virge->wake_main_thread);
        thread_destroy_event(virge->wake_render_thread);

        svga_close(&virge->svga);

        ddc_close(virge->ddc);
        i2c_gpio_close(virge->i2c);
        
        free(virge);
}

static int s3_virge_325_diamond_available(void)
{
        return rom_present(ROM_DIAMOND_STEALTH3D_2000);
}

static int s3_virge_325_available(void)
{
        return rom_present(ROM_VIRGE_325);
}

static int s3_virge_988_diamond_available(void)
{
        return rom_present(ROM_DIAMOND_STEALTH3D_3000);
}

static int s3_virge_375_available(void)
{
        return rom_present(ROM_VIRGE_DX);
}

static int s3_virge_375_diamond_available(void)
{
        return rom_present(ROM_DIAMOND_STEALTH3D_2000PRO);
}

static int s3_virge_385_available(void)
{
        return rom_present(ROM_VIRGE_GX);
}

static int s3_virge_357_available(void)
{
        return rom_present(ROM_VIRGE_GX2);
}

static int s3_virge_357_diamond_available(void)
{
        return rom_present(ROM_DIAMOND_STEALTH3D_4000);
}

static int s3_trio3d2x_available(void)
{
        return rom_present(ROM_TRIO3D2X);
}

static void s3_virge_speed_changed(void *p)
{
        virge_t *virge = (virge_t *)p;
        
        svga_recalctimings(&virge->svga);
}

static void s3_virge_force_redraw(void *p)
{
        virge_t *virge = (virge_t *)p;

        virge->svga.fullchange = changeframecount;
}

static const device_config_t s3_virge_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 4, "", { 0 },
                {
                        {
                                "2 MB", 2
                        },
                        {
                                "4 MB", 4
                        },
                        {
                                ""
                        }
                }
        },
        {
                "bilinear", "Bilinear filtering", CONFIG_BINARY, "", 1
        },
        {
                "dithering", "Dithering", CONFIG_BINARY, "", 1
        },
        {
                "", "", -1
        }
};

static const device_config_t s3_virge_357_config[] =
{
        {
                "bilinear", "Bilinear filtering", CONFIG_BINARY, "", 1
        },
        {
                "dithering", "Dithering", CONFIG_BINARY, "", 1
        },
        {
                "", "", -1
        }
};

const device_t s3_virge_325_pci_device =
{
        "S3 ViRGE (325) PCI",
        DEVICE_PCI,
        S3_VIRGE_325,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_325_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_config
};

const device_t s3_diamond_stealth_2000_pci_device =
{
        "S3 ViRGE (Diamond Stealth 3D 2000) PCI",
        DEVICE_PCI,
        S3_DIAMOND_STEALTH3D_2000,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_325_diamond_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_config
};

const device_t s3_diamond_stealth_3000_pci_device =
{
        "S3 ViRGE/VX (Diamond Stealth 3D 3000) PCI",
        DEVICE_PCI,
        S3_DIAMOND_STEALTH3D_3000,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_988_diamond_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_config
};

const device_t s3_virge_375_pci_device =
{
        "S3 ViRGE/DX (375) PCI",
        DEVICE_PCI,
        S3_VIRGE_DX,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_375_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_config
};

const device_t s3_diamond_stealth_2000pro_pci_device =
{
        "S3 ViRGE/DX (Diamond Stealth 3D 2000 Pro) PCI",
        DEVICE_PCI,
        S3_DIAMOND_STEALTH3D_2000PRO,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_375_diamond_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_config
};

const device_t s3_virge_385_pci_device =
{
        "S3 ViRGE/GX (385) PCI",
        DEVICE_PCI,
        S3_VIRGE_GX,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_385_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_config
};

const device_t s3_virge_357_pci_device =
{
        "S3 ViRGE/GX2 (357) PCI",
        DEVICE_PCI,
        S3_VIRGE_GX2,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_357_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_357_config
};

const device_t s3_virge_357_agp_device =
{
        "S3 ViRGE/GX2 (357) AGP",
        DEVICE_AGP,
        S3_VIRGE_GX2,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_357_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_357_config
};

const device_t s3_diamond_stealth_4000_pci_device =
{
        "S3 ViRGE/GX2 (Diamond Stealth 3D 4000) PCI",
        DEVICE_PCI,
        S3_DIAMOND_STEALTH3D_4000,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_357_diamond_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_357_config
};

const device_t s3_diamond_stealth_4000_agp_device =
{
        "S3 ViRGE/GX2 (Diamond Stealth 3D 4000) AGP",
        DEVICE_AGP,
        S3_DIAMOND_STEALTH3D_4000,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_virge_357_diamond_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_357_config
};

const device_t s3_trio3d2x_pci_device =
{
        "S3 Trio3D/2X (362) PCI",
        DEVICE_PCI,
        S3_TRIO_3D2X,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_trio3d2x_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_357_config
};

const device_t s3_trio3d2x_agp_device =
{
        "S3 Trio3D/2X (362) AGP",
        DEVICE_AGP,
        S3_TRIO_3D2X,
        s3_virge_init,
        s3_virge_close,
	s3_virge_reset,
        { s3_trio3d2x_available },
        s3_virge_speed_changed,
        s3_virge_force_redraw,
        s3_virge_357_config
};