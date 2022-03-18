/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ATi Mach64 graphics card emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_ati_eeprom.h>

#ifdef CLAMP
#undef CLAMP
#endif

#define BIOS_ROM_PATH		"roms/video/mach64/bios.bin"
#define BIOS_ISA_ROM_PATH	"roms/video/mach64/M64-1994.VBI"
#define BIOS_VLB_ROM_PATH	"roms/video/mach64/mach64_vlb_vram.bin"
#define BIOS_ROMVT2_PATH	"roms/video/mach64/atimach64vt2pci.bin"


#define FIFO_SIZE 65536
#define FIFO_MASK (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE (1 << 31)

#define FIFO_ENTRIES (mach64->fifo_write_idx - mach64->fifo_read_idx)
#define FIFO_FULL    ((mach64->fifo_write_idx - mach64->fifo_read_idx) >= FIFO_SIZE)
#define FIFO_EMPTY   (mach64->fifo_read_idx == mach64->fifo_write_idx)

#define FIFO_TYPE 0xff000000
#define FIFO_ADDR 0x00ffffff

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

enum
{
        MACH64_GX = 0,
        MACH64_VT2
};

typedef struct mach64_t
{
        mem_mapping_t linear_mapping;
        mem_mapping_t mmio_mapping;
        mem_mapping_t mmio_linear_mapping;
        mem_mapping_t mmio_linear_mapping_2;

        ati_eeprom_t eeprom;
        svga_t svga;

        rom_t bios_rom;

        uint8_t regs[256];
        int index;

        int type, pci;

        uint8_t pci_regs[256];
        uint8_t int_line;
        int card;

        int bank_r[2];
        int bank_w[2];

        uint32_t vram_size;
        uint32_t vram_mask;

        uint32_t config_cntl;

        uint32_t context_load_cntl;
        uint32_t context_mask;

        uint32_t crtc_gen_cntl;
        uint8_t  crtc_int_cntl;
        uint32_t crtc_h_total_disp;
        uint32_t crtc_v_sync_strt_wid;
        uint32_t crtc_v_total_disp;
        uint32_t crtc_off_pitch;

        uint32_t clock_cntl;

        uint32_t clr_cmp_clr;
        uint32_t clr_cmp_cntl;
        uint32_t clr_cmp_mask;

        uint32_t cur_horz_vert_off;
        uint32_t cur_horz_vert_posn;
        uint32_t cur_offset;

        uint32_t dac_cntl;

        uint32_t dp_bkgd_clr;
        uint32_t dp_frgd_clr;
        uint32_t dp_mix;
        uint32_t dp_pix_width;
        uint32_t dp_src;

        uint32_t dst_bres_lnth;
        uint32_t dst_bres_dec;
        uint32_t dst_bres_err;
        uint32_t dst_bres_inc;

        uint32_t dst_cntl;
        uint32_t dst_height_width;
        uint32_t dst_off_pitch;
        uint32_t dst_y_x;

        uint32_t gen_test_cntl;

        uint32_t gui_traj_cntl;

        uint32_t host_cntl;

        uint32_t mem_cntl;

        uint32_t ovr_clr;
        uint32_t ovr_wid_left_right;
        uint32_t ovr_wid_top_bottom;

        uint32_t pat_cntl;
        uint32_t pat_reg0, pat_reg1;

        uint32_t sc_left_right, sc_top_bottom;

        uint32_t scratch_reg0, scratch_reg1;

        uint32_t src_cntl;
        uint32_t src_off_pitch;
        uint32_t src_y_x;
        uint32_t src_y_x_start;
        uint32_t src_height1_width1, src_height2_width2;

        uint32_t write_mask;
        uint32_t chain_mask;

        uint32_t linear_base, old_linear_base;
        uint32_t io_base;

        struct
        {
                int op;

                int dst_x, dst_y;
                int dst_x_start, dst_y_start;
                int src_x, src_y;
                int src_x_start, src_y_start;
                int xinc, yinc;
                int x_count, y_count;
                int src_x_count, src_y_count;
                int src_width1, src_height1;
                int src_width2, src_height2;
                uint32_t src_offset, src_pitch;
                uint32_t dst_offset, dst_pitch;
                int mix_bg, mix_fg;
                int source_bg, source_fg, source_mix;
                int source_host;
                int dst_width, dst_height;
                int busy;
                int pattern[8][8];
				uint8_t pattern_clr4x2[2][4];
				uint8_t pattern_clr8x1[8];
                int sc_left, sc_right, sc_top, sc_bottom;
                int dst_pix_width, src_pix_width, host_pix_width;
                int dst_size, src_size, host_size;

                uint32_t dp_bkgd_clr;
                uint32_t dp_frgd_clr;
                uint32_t write_mask;

                uint32_t clr_cmp_clr;
                uint32_t clr_cmp_mask;
                int clr_cmp_fn;
                int clr_cmp_src;

                int err;
                int poly_draw;
        } accel;

        fifo_entry_t fifo[FIFO_SIZE];
        volatile int fifo_read_idx, fifo_write_idx;

        thread_t *fifo_thread;
        event_t *wake_fifo_thread;
        event_t *fifo_not_full_event;

        int blitter_busy;
        uint64_t blitter_time;
        uint64_t status_time;

        uint16_t pci_id;
        uint32_t config_chip_id;
        uint32_t block_decoded_io;
        int use_block_decoded_io;

        int pll_addr;
        uint8_t pll_regs[16];
        double pll_freq[4];

        uint32_t config_stat0;

        uint32_t cur_clr0, cur_clr1;

        uint32_t overlay_dat[1024];
        uint32_t overlay_graphics_key_clr, overlay_graphics_key_msk;
        uint32_t overlay_video_key_clr, overlay_video_key_msk;
        uint32_t overlay_key_cntl;
        uint32_t overlay_scale_inc;
        uint32_t overlay_scale_cntl;
        uint32_t overlay_y_x_start, overlay_y_x_end;

        uint32_t scaler_height_width;
        int scaler_format;
        int scaler_update;

        uint32_t buf_offset[2], buf_pitch[2];

        int overlay_v_acc;

        uint8_t thread_run;
        void *i2c, *ddc;
} mach64_t;

static video_timings_t timing_mach64_isa	= {VIDEO_ISA, 3,  3,  6,   5,  5, 10};
static video_timings_t timing_mach64_vlb	= {VIDEO_BUS, 2,  2,  1,  20, 20, 21};
static video_timings_t timing_mach64_pci	= {VIDEO_PCI, 2,  2,  1,  20, 20, 21};

enum
{
        SRC_BG      = 0,
        SRC_FG      = 1,
        SRC_HOST    = 2,
        SRC_BLITSRC = 3,
        SRC_PAT     = 4
};

enum
{
        MONO_SRC_1       = 0,
        MONO_SRC_PAT     = 1,
        MONO_SRC_HOST    = 2,
        MONO_SRC_BLITSRC = 3
};

enum
{
        BPP_1  = 0,
        BPP_4  = 1,
        BPP_8  = 2,
        BPP_15 = 3,
        BPP_16 = 4,
        BPP_24 = 5,
        BPP_32 = 6
};

enum
{
        OP_RECT,
        OP_LINE
};

enum
{
        SRC_PATT_EN     = 1,
        SRC_PATT_ROT_EN = 2,
        SRC_LINEAR_EN   = 4
};

enum
{
        DP_BYTE_PIX_ORDER = (1 << 24)
};

#define WIDTH_1BIT 3

static int mach64_width[8] = {WIDTH_1BIT, 0, 0, 1, 1, 2, 2, 0};

enum
{
        DST_X_DIR      = 0x01,
        DST_Y_DIR      = 0x02,
        DST_Y_MAJOR    = 0x04,
        DST_X_TILE     = 0x08,
        DST_Y_TILE     = 0x10,
        DST_LAST_PEL   = 0x20,
        DST_POLYGON_EN = 0x40,
        DST_24_ROT_EN  = 0x80
};

enum
{
        HOST_BYTE_ALIGN = (1 << 0)
};

void mach64_write(uint32_t addr, uint8_t val, void *priv);
void mach64_writew(uint32_t addr, uint16_t val, void *priv);
void mach64_writel(uint32_t addr, uint32_t val, void *priv);
uint8_t mach64_read(uint32_t addr, void *priv);
uint16_t mach64_readw(uint32_t addr, void *priv);
uint32_t mach64_readl(uint32_t addr, void *priv);
void mach64_updatemapping(mach64_t *mach64);
void mach64_recalctimings(svga_t *svga);
void mach64_start_fill(mach64_t *mach64);
void mach64_start_line(mach64_t *mach64);
void mach64_blit(uint32_t cpu_dat, int count, mach64_t *mach64);
void mach64_load_context(mach64_t *mach64);

uint8_t  mach64_ext_readb(uint32_t addr, void *priv);
uint16_t mach64_ext_readw(uint32_t addr, void *priv);
uint32_t mach64_ext_readl(uint32_t addr, void *priv);
void     mach64_ext_writeb(uint32_t addr, uint8_t val, void *priv);
void     mach64_ext_writew(uint32_t addr, uint16_t val, void *priv);
void     mach64_ext_writel(uint32_t addr, uint32_t val, void *priv);


#ifdef ENABLE_MACH64_LOG
int mach64_do_log = ENABLE_MACH64_LOG;


static void
mach64_log(const char *fmt, ...)
{
    va_list ap;

    if (mach64_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define mach64_log(fmt, ...)
#endif


void mach64_out(uint16_t addr, uint8_t val, void *p)
{
        mach64_t *mach64 = p;
        svga_t *svga = &mach64->svga;
        uint8_t old;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout & 1))
                addr ^= 0x60;

        switch (addr)
        {
                case 0x1ce:
                mach64->index = val;
                break;
                case 0x1cf:
                mach64->regs[mach64->index & 0x3f] = val;
                if ((mach64->index & 0x3f) == 0x36)
                        svga_recalctimings(svga);
                break;

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                if (mach64->type == MACH64_GX)
                        ati68860_ramdac_out((addr & 3) | ((mach64->dac_cntl & 3) << 2), val, mach64->svga.ramdac, svga);
                else
                        svga_out(addr, val, svga);
                return;

                case 0x3cf:
                if (svga->gdcaddr == 6)
                {
                        uint8_t old_val = svga->gdcreg[6];
                        svga->gdcreg[6] = val;
                        if ((svga->gdcreg[6] & 0xc) != (old_val & 0xc))
                                mach64_updatemapping(mach64);
                        return;
                }
                break;

                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                if (svga->crtcreg > 0x18)
                        return;
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;

                if (old != val)
                {
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
				if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                                	svga->fullchange = 3;
					svga->ma_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
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

uint8_t mach64_in(uint16_t addr, void *p)
{
        mach64_t *mach64 = p;
        svga_t *svga = &mach64->svga;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1))
                addr ^= 0x60;

        switch (addr)
        {
                case 0x1ce:
                return mach64->index;
                case 0x1cf:
                return mach64->regs[mach64->index & 0x3f];

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                if (mach64->type == MACH64_GX)
                        return ati68860_ramdac_in((addr & 3) | ((mach64->dac_cntl & 3) << 2), mach64->svga.ramdac, svga);
                return svga_in(addr, svga);

                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                if (svga->crtcreg > 0x18)
                        return 0xff;
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void mach64_recalctimings(svga_t *svga)
{
        mach64_t *mach64 = (mach64_t *)svga->p;

        if (((mach64->crtc_gen_cntl >> 24) & 3) == 3)
        {
                svga->vtotal = (mach64->crtc_v_total_disp & 2047) + 1;
                svga->dispend = ((mach64->crtc_v_total_disp >> 16) & 2047) + 1;
                svga->htotal = (mach64->crtc_h_total_disp & 255) + 1;
                svga->hdisp_time = svga->hdisp = ((mach64->crtc_h_total_disp >> 16) & 255) + 1;
                svga->vsyncstart = (mach64->crtc_v_sync_strt_wid & 2047) + 1;
                svga->rowoffset = (mach64->crtc_off_pitch >> 22);
                svga->clock = (cpuclock * (double)(1ull << 32)) / ics2595_getclock(svga->clock_gen);
                svga->ma_latch = (mach64->crtc_off_pitch & 0x1fffff) * 2;
                svga->linedbl = svga->rowcount = 0;
                svga->split = 0xffffff;
                svga->vblankstart = svga->dispend;
                svga->rowcount = mach64->crtc_gen_cntl & 1;
                svga->rowoffset <<= 1;
                if (mach64->type == MACH64_GX)
			ati68860_ramdac_set_render(svga->ramdac, svga);
                switch ((mach64->crtc_gen_cntl >> 8) & 7)
                {
                        case BPP_4:
                        if (mach64->type != MACH64_GX)
                                svga->render = svga_render_4bpp_highres;
                        svga->hdisp *= 8;
                        break;
                        case BPP_8:
                        if (mach64->type != MACH64_GX)
                                svga->render = svga_render_8bpp_highres;
                        svga->hdisp *= 8;
                        svga->rowoffset /= 2;
                        break;
                        case BPP_15:
                        if (mach64->type != MACH64_GX)
                                svga->render = svga_render_15bpp_highres;
                        svga->hdisp *= 8;
                        break;
                        case BPP_16:
                        if (mach64->type != MACH64_GX)
                                svga->render = svga_render_16bpp_highres;
                        svga->hdisp *= 8;
                        break;
                        case BPP_24:
                        if (mach64->type != MACH64_GX)
                                svga->render = svga_render_24bpp_highres;
                        svga->hdisp *= 8;
                        svga->rowoffset = (svga->rowoffset * 3) / 2;
                        break;
                        case BPP_32:
                        if (mach64->type != MACH64_GX)
                                svga->render = svga_render_32bpp_highres;
                        svga->hdisp *= 8;
                        svga->rowoffset *= 2;
                        break;
                }

                svga->vram_display_mask = mach64->vram_mask;
        }
        else
        {
                svga->vram_display_mask = (mach64->regs[0x36] & 0x01) ? mach64->vram_mask : 0x3ffff;
        }
}

void mach64_updatemapping(mach64_t *mach64)
{
        svga_t *svga = &mach64->svga;

        if (!(mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))
        {
                mach64_log("Update mapping - PCI disabled\n");
                mem_mapping_disable(&svga->mapping);
                mem_mapping_disable(&mach64->linear_mapping);
                mem_mapping_disable(&mach64->mmio_mapping);
                mem_mapping_disable(&mach64->mmio_linear_mapping);
                mem_mapping_disable(&mach64->mmio_linear_mapping_2);
                return;
        }

        mem_mapping_disable(&mach64->mmio_mapping);
        switch (svga->gdcreg[6] & 0xc)
        {
                case 0x0: /*128k at A0000*/
				mem_mapping_set_handler(&mach64->svga.mapping, mach64_read, mach64_readw, mach64_readl, mach64_write, mach64_writew, mach64_writel);
                mem_mapping_set_p(&mach64->svga.mapping, mach64);
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                mem_mapping_enable(&mach64->mmio_mapping);
                svga->banked_mask = 0xffff;
                break;
                case 0x4: /*64k at A0000*/
				mem_mapping_set_handler(&mach64->svga.mapping, mach64_read, mach64_readw, mach64_readl, mach64_write, mach64_writew, mach64_writel);
                mem_mapping_set_p(&mach64->svga.mapping, mach64);
                mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                svga->banked_mask = 0xffff;
                break;
                case 0x8: /*32k at B0000*/
				mem_mapping_set_handler(&mach64->svga.mapping, svga_read, svga_readw, svga_readl, svga_write, svga_writew, svga_writel);
                mem_mapping_set_p(&mach64->svga.mapping, svga);
                mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                svga->banked_mask = 0x7fff;
                break;
                case 0xC: /*32k at B8000*/
				mem_mapping_set_handler(&mach64->svga.mapping, svga_read, svga_readw, svga_readl, svga_write, svga_writew, svga_writel);
                mem_mapping_set_p(&mach64->svga.mapping, svga);
                mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                svga->banked_mask = 0x7fff;
                break;
        }
        if (mach64->linear_base)
        {
                if (mach64->type == MACH64_GX)
                {
                        if ((mach64->config_cntl & 3) == 2)
                        {
                                /*8 MB aperture*/
                                mem_mapping_set_addr(&mach64->linear_mapping, mach64->linear_base, (8 << 20) - 0x4000);
                                mem_mapping_set_addr(&mach64->mmio_linear_mapping, mach64->linear_base + ((8 << 20) - 0x4000), 0x4000);
                        }
                        else
                        {
                                /*4 MB aperture*/
                                mem_mapping_set_addr(&mach64->linear_mapping, mach64->linear_base, (4 << 20) - 0x4000);
                                mem_mapping_set_addr(&mach64->mmio_linear_mapping, mach64->linear_base + ((4 << 20) - 0x4000), 0x4000);
                        }
                }
                else
                {
                        /*2*8 MB aperture*/
                        mem_mapping_set_addr(&mach64->linear_mapping, mach64->linear_base, (8 << 20) - 0x4000);
                        mem_mapping_set_addr(&mach64->mmio_linear_mapping, mach64->linear_base + ((8 << 20) - 0x4000), 0x4000);
                        mem_mapping_set_addr(&mach64->mmio_linear_mapping_2, mach64->linear_base + ((16 << 20) - 0x4000), 0x4000);
                }
        }
        else
        {
                mem_mapping_disable(&mach64->linear_mapping);
                mem_mapping_disable(&mach64->mmio_linear_mapping);
                mem_mapping_disable(&mach64->mmio_linear_mapping_2);
        }
}

static void mach64_update_irqs(mach64_t *mach64)
{
	if (!mach64->pci)
	{
		return;
	}

        if ((mach64->crtc_int_cntl & 0xaa0024) & ((mach64->crtc_int_cntl << 1) & 0xaa0024))
                pci_set_irq(mach64->card, PCI_INTA);
        else
                pci_clear_irq(mach64->card, PCI_INTA);
}

static __inline void wake_fifo_thread(mach64_t *mach64)
{
        thread_set_event(mach64->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

static void mach64_wait_fifo_idle(mach64_t *mach64)
{
        while (!FIFO_EMPTY)
        {
                wake_fifo_thread(mach64);
                thread_wait_event(mach64->fifo_not_full_event, 1);
        }
}

#define READ8(addr, var)        switch ((addr) & 3)                                     \
                                {                                                       \
                                        case 0: ret = (var) & 0xff;         break;      \
                                        case 1: ret = ((var) >> 8) & 0xff;  break;      \
                                        case 2: ret = ((var) >> 16) & 0xff; break;      \
                                        case 3: ret = ((var) >> 24) & 0xff; break;      \
                                }

#define WRITE8(addr, var, val)  switch ((addr) & 3)                                             \
                                {                                                               \
                                        case 0: var = (var & 0xffffff00) | (val);         break;  \
                                        case 1: var = (var & 0xffff00ff) | ((val) << 8);  break;  \
                                        case 2: var = (var & 0xff00ffff) | ((val) << 16); break;  \
                                        case 3: var = (var & 0x00ffffff) | ((val) << 24); break;  \
                                }

static void mach64_accel_write_fifo(mach64_t *mach64, uint32_t addr, uint8_t val)
{
        switch (addr & 0x3ff)
        {
                case 0x100: case 0x101: case 0x102: case 0x103:
                WRITE8(addr, mach64->dst_off_pitch, val);
                break;
                case 0x104: case 0x105: case 0x11c: case 0x11d:
                WRITE8(addr + 2, mach64->dst_y_x, val);
                break;
                case 0x108: case 0x109:
                WRITE8(addr, mach64->dst_y_x, val);
                break;
                case 0x10c: case 0x10d: case 0x10e: case 0x10f:
                WRITE8(addr, mach64->dst_y_x, val);
                break;
                case 0x110: case 0x111:
                WRITE8(addr + 2, mach64->dst_height_width, val);
                break;
                case 0x114: case 0x115:
                case 0x118: case 0x119: case 0x11a: case 0x11b:
                case 0x11e: case 0x11f:
                WRITE8(addr, mach64->dst_height_width, val);
		/*FALLTHROUGH*/
                case 0x113:
                if (((addr & 0x3ff) == 0x11b || (addr & 0x3ff) == 0x11f ||
                     (addr & 0x3ff) == 0x113) && !(val & 0x80))
                {
                        mach64_start_fill(mach64);
                        mach64_log("%i %i %i %i %i %08x\n", (mach64->dst_height_width & 0x7ff), (mach64->dst_height_width & 0x7ff0000),
                            ((mach64->dp_src & 7) != SRC_HOST), (((mach64->dp_src >> 8) & 7) != SRC_HOST),
                            (((mach64->dp_src >> 16) & 3) != MONO_SRC_HOST), mach64->dp_src);
                        if ((mach64->dst_height_width & 0x7ff) && (mach64->dst_height_width & 0x7ff0000) &&
                            ((mach64->dp_src & 7) != SRC_HOST) && (((mach64->dp_src >> 8) & 7) != SRC_HOST) &&
                            (((mach64->dp_src >> 16) & 3) != MONO_SRC_HOST))
                                mach64_blit(0, -1, mach64);
                }
                break;

                case 0x120: case 0x121: case 0x122: case 0x123:
                WRITE8(addr, mach64->dst_bres_lnth, val);
                if ((addr & 0x3ff) == 0x123 && !(val & 0x80))
                {
                        mach64_start_line(mach64);

                        if ((mach64->dst_bres_lnth & 0x7fff) &&
                            ((mach64->dp_src & 7) != SRC_HOST) && (((mach64->dp_src >> 8) & 7) != SRC_HOST) &&
                            (((mach64->dp_src >> 16) & 3) != MONO_SRC_HOST))
                                mach64_blit(0, -1, mach64);
                }
                break;
                case 0x124: case 0x125: case 0x126: case 0x127:
                WRITE8(addr, mach64->dst_bres_err, val);
                break;
                case 0x128: case 0x129: case 0x12a: case 0x12b:
                WRITE8(addr, mach64->dst_bres_inc, val);
                break;
                case 0x12c: case 0x12d: case 0x12e: case 0x12f:
                WRITE8(addr, mach64->dst_bres_dec, val);
                break;

                case 0x130: case 0x131: case 0x132: case 0x133:
                WRITE8(addr, mach64->dst_cntl, val);
                break;

                case 0x180: case 0x181: case 0x182: case 0x183:
                WRITE8(addr, mach64->src_off_pitch, val);
                break;
                case 0x184: case 0x185:
                WRITE8(addr, mach64->src_y_x, val);
                break;
                case 0x188: case 0x189:
                WRITE8(addr + 2, mach64->src_y_x, val);
                break;
                case 0x18c: case 0x18d: case 0x18e: case 0x18f:
                WRITE8(addr, mach64->src_y_x, val);
                break;
                case 0x190: case 0x191:
                WRITE8(addr + 2, mach64->src_height1_width1, val);
                break;
                case 0x194: case 0x195:
                WRITE8(addr, mach64->src_height1_width1, val);
                break;
                case 0x198: case 0x199: case 0x19a: case 0x19b:
                WRITE8(addr, mach64->src_height1_width1, val);
                break;
                case 0x19c: case 0x19d:
                WRITE8(addr, mach64->src_y_x_start, val);
                break;
                case 0x1a0: case 0x1a1:
                WRITE8(addr + 2, mach64->src_y_x_start, val);
                break;
                case 0x1a4: case 0x1a5: case 0x1a6: case 0x1a7:
                WRITE8(addr, mach64->src_y_x_start, val);
                break;
                case 0x1a8: case 0x1a9:
                WRITE8(addr + 2, mach64->src_height2_width2, val);
                break;
                case 0x1ac: case 0x1ad:
                WRITE8(addr, mach64->src_height2_width2, val);
                break;
                case 0x1b0: case 0x1b1: case 0x1b2: case 0x1b3:
                WRITE8(addr, mach64->src_height2_width2, val);
                break;

                case 0x1b4: case 0x1b5: case 0x1b6: case 0x1b7:
                WRITE8(addr, mach64->src_cntl, val);
                break;

                case 0x200: case 0x201: case 0x202: case 0x203:
                case 0x204: case 0x205: case 0x206: case 0x207:
                case 0x208: case 0x209: case 0x20a: case 0x20b:
                case 0x20c: case 0x20d: case 0x20e: case 0x20f:
                case 0x210: case 0x211: case 0x212: case 0x213:
                case 0x214: case 0x215: case 0x216: case 0x217:
                case 0x218: case 0x219: case 0x21a: case 0x21b:
                case 0x21c: case 0x21d: case 0x21e: case 0x21f:
                case 0x220: case 0x221: case 0x222: case 0x223:
                case 0x224: case 0x225: case 0x226: case 0x227:
                case 0x228: case 0x229: case 0x22a: case 0x22b:
                case 0x22c: case 0x22d: case 0x22e: case 0x22f:
                case 0x230: case 0x231: case 0x232: case 0x233:
                case 0x234: case 0x235: case 0x236: case 0x237:
                case 0x238: case 0x239: case 0x23a: case 0x23b:
                case 0x23c: case 0x23d: case 0x23e: case 0x23f:
                mach64_blit(val, 8, mach64);
                break;

                case 0x240: case 0x241: case 0x242: case 0x243:
                WRITE8(addr, mach64->host_cntl, val);
                break;

                case 0x280: case 0x281: case 0x282: case 0x283:
                WRITE8(addr, mach64->pat_reg0, val);
                break;
                case 0x284: case 0x285: case 0x286: case 0x287:
                WRITE8(addr, mach64->pat_reg1, val);
                break;

                case 0x288: case 0x289: case 0x28a: case 0x28b:
                WRITE8(addr, mach64->pat_cntl, val);
                break;

                case 0x2a0: case 0x2a1: case 0x2a8: case 0x2a9:
                WRITE8(addr, mach64->sc_left_right, val);
                break;
                case 0x2a4: case 0x2a5:
                addr += 2;
		/*FALLTHROUGH*/
                case 0x2aa: case 0x2ab:
                WRITE8(addr, mach64->sc_left_right, val);
                break;

                case 0x2ac: case 0x2ad: case 0x2b4: case 0x2b5:
                WRITE8(addr, mach64->sc_top_bottom, val);
                break;
                case 0x2b0: case 0x2b1:
                addr += 2;
		/*FALLTHROUGH*/
                case 0x2b6: case 0x2b7:
                WRITE8(addr, mach64->sc_top_bottom, val);
                break;

                case 0x2c0: case 0x2c1: case 0x2c2: case 0x2c3:
                WRITE8(addr, mach64->dp_bkgd_clr, val);
                break;
                case 0x2c4: case 0x2c5: case 0x2c6: case 0x2c7:
                WRITE8(addr, mach64->dp_frgd_clr, val);
                break;
                case 0x2c8: case 0x2c9: case 0x2ca: case 0x2cb:
                WRITE8(addr, mach64->write_mask, val);
                break;
                case 0x2cc: case 0x2cd: case 0x2ce: case 0x2cf:
                WRITE8(addr, mach64->chain_mask, val);
                break;

                case 0x2d0: case 0x2d1: case 0x2d2: case 0x2d3:
                WRITE8(addr, mach64->dp_pix_width, val);
                break;
                case 0x2d4: case 0x2d5: case 0x2d6: case 0x2d7:
                WRITE8(addr, mach64->dp_mix, val);
                break;
                case 0x2d8: case 0x2d9: case 0x2da: case 0x2db:
                WRITE8(addr, mach64->dp_src, val);
                break;

                case 0x300: case 0x301: case 0x302: case 0x303:
                WRITE8(addr, mach64->clr_cmp_clr, val);
                break;
                case 0x304: case 0x305: case 0x306: case 0x307:
                WRITE8(addr, mach64->clr_cmp_mask, val);
                break;
                case 0x308: case 0x309: case 0x30a: case 0x30b:
                WRITE8(addr, mach64->clr_cmp_cntl, val);
                break;

                case 0x320: case 0x321: case 0x322: case 0x323:
                WRITE8(addr, mach64->context_mask, val);
                break;

                case 0x330: case 0x331:
                WRITE8(addr, mach64->dst_cntl, val);
                break;
                case 0x332:
                WRITE8(addr - 2, mach64->src_cntl, val);
                break;
                case 0x333:
                WRITE8(addr - 3, mach64->pat_cntl, val & 7);
                if (val & 0x10)
                        mach64->host_cntl |= HOST_BYTE_ALIGN;
                else
                        mach64->host_cntl &= ~HOST_BYTE_ALIGN;
                break;
        }
}
static void mach64_accel_write_fifo_w(mach64_t *mach64, uint32_t addr, uint16_t val)
{
        switch (addr & 0x3fe)
        {
                case 0x200: case 0x202: case 0x204: case 0x206:
                case 0x208: case 0x20a: case 0x20c: case 0x20e:
                case 0x210: case 0x212: case 0x214: case 0x216:
                case 0x218: case 0x21a: case 0x21c: case 0x21e:
                case 0x220: case 0x222: case 0x224: case 0x226:
                case 0x228: case 0x22a: case 0x22c: case 0x22e:
                case 0x230: case 0x232: case 0x234: case 0x236:
                case 0x238: case 0x23a: case 0x23c: case 0x23e:
                mach64_blit(val, 16, mach64);
                break;

                case 0x32c:
                mach64->context_load_cntl = (mach64->context_load_cntl & 0xffff0000) | val;
		break;

                case 0x32e:
                mach64->context_load_cntl = (mach64->context_load_cntl & 0x0000ffff) | (val << 16);
                if (val & 0x30000)
                        mach64_load_context(mach64);
                break;

                default:
                mach64_accel_write_fifo(mach64, addr, val);
                mach64_accel_write_fifo(mach64, addr + 1, val >> 8);
                break;
        }
}
static void mach64_accel_write_fifo_l(mach64_t *mach64, uint32_t addr, uint32_t val)
{
        switch (addr & 0x3fc)
        {
                case 0x32c:
                mach64->context_load_cntl = val;
                if (val & 0x30000)
                        mach64_load_context(mach64);
                break;

                case 0x200: case 0x204: case 0x208: case 0x20c:
                case 0x210: case 0x214: case 0x218: case 0x21c:
                case 0x220: case 0x224: case 0x228: case 0x22c:
                case 0x230: case 0x234: case 0x238: case 0x23c:
                if (mach64->accel.source_host || (mach64->dp_pix_width & DP_BYTE_PIX_ORDER))
                        mach64_blit(val, 32, mach64);
                else
                        mach64_blit(((val & 0xff000000) >> 24) | ((val & 0x00ff0000) >> 8) | ((val & 0x0000ff00) << 8) | ((val & 0x000000ff) << 24), 32, mach64);
                break;

                default:
                mach64_accel_write_fifo_w(mach64, addr, val);
                mach64_accel_write_fifo_w(mach64, addr + 2, val >> 16);
                break;
        }
}

static void fifo_thread(void *param)
{
        mach64_t *mach64 = (mach64_t *)param;

        while (mach64->thread_run)
        {
                thread_set_event(mach64->fifo_not_full_event);
                thread_wait_event(mach64->wake_fifo_thread, -1);
                thread_reset_event(mach64->wake_fifo_thread);
                mach64->blitter_busy = 1;
                while (!FIFO_EMPTY)
                {
                        uint64_t start_time = plat_timer_read();
                        uint64_t end_time;
                        fifo_entry_t *fifo = &mach64->fifo[mach64->fifo_read_idx & FIFO_MASK];

                        switch (fifo->addr_type & FIFO_TYPE)
                        {
                                case FIFO_WRITE_BYTE:
                                mach64_accel_write_fifo(mach64, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                                case FIFO_WRITE_WORD:
                                mach64_accel_write_fifo_w(mach64, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                                case FIFO_WRITE_DWORD:
                                mach64_accel_write_fifo_l(mach64, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                        }

                        mach64->fifo_read_idx++;
                        fifo->addr_type = FIFO_INVALID;

                        if (FIFO_ENTRIES > 0xe000)
                                thread_set_event(mach64->fifo_not_full_event);

                        end_time = plat_timer_read();
                        mach64->blitter_time += end_time - start_time;
                }
                mach64->blitter_busy = 0;
        }
}

static void mach64_queue(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type)
{
        fifo_entry_t *fifo = &mach64->fifo[mach64->fifo_write_idx & FIFO_MASK];

        if (FIFO_FULL)
        {
                thread_reset_event(mach64->fifo_not_full_event);
                if (FIFO_FULL)
                {
                        thread_wait_event(mach64->fifo_not_full_event, -1); /*Wait for room in ringbuffer*/
                }
        }

        fifo->val = val;
        fifo->addr_type = (addr & FIFO_ADDR) | type;

        mach64->fifo_write_idx++;

        if (FIFO_ENTRIES > 0xe000 || FIFO_ENTRIES < 8)
                wake_fifo_thread(mach64);
}


void mach64_start_fill(mach64_t *mach64)
{
        int x, y;

        mach64->accel.dst_x = 0;
        mach64->accel.dst_y = 0;
        mach64->accel.dst_x_start = (mach64->dst_y_x >> 16) & 0xfff;
        mach64->accel.dst_y_start =  mach64->dst_y_x        & 0xfff;

        mach64->accel.dst_width  = (mach64->dst_height_width >> 16) & 0x1fff;
        mach64->accel.dst_height =  mach64->dst_height_width        & 0x1fff;

	if (((mach64->dp_src >> 16) & 7) == MONO_SRC_BLITSRC)
	{
		if (mach64->accel.dst_width & 7)
			mach64->accel.dst_width = (mach64->accel.dst_width & ~7) + 8;
	}

        mach64->accel.x_count = mach64->accel.dst_width;

        mach64->accel.src_x = 0;
        mach64->accel.src_y = 0;
        mach64->accel.src_x_start = (mach64->src_y_x >> 16) & 0xfff;
        mach64->accel.src_y_start =  mach64->src_y_x        & 0xfff;
        if (mach64->src_cntl & SRC_LINEAR_EN)
                mach64->accel.src_x_count = 0x7ffffff; /*Essentially infinite*/
        else
                mach64->accel.src_x_count = (mach64->src_height1_width1 >> 16) & 0x7fff;
        if (!(mach64->src_cntl & SRC_PATT_EN))
                mach64->accel.src_y_count = 0x7ffffff; /*Essentially infinite*/
        else
                mach64->accel.src_y_count = mach64->src_height1_width1 & 0x1fff;

        mach64->accel.src_width1  = (mach64->src_height1_width1 >> 16) & 0x7fff;
        mach64->accel.src_height1 =  mach64->src_height1_width1        & 0x1fff;
        mach64->accel.src_width2  = (mach64->src_height2_width2 >> 16) & 0x7fff;
        mach64->accel.src_height2 =  mach64->src_height2_width2        & 0x1fff;

        mach64_log("src %i %i  %i %i  %08X %08X\n", mach64->accel.src_x_count,
                                               mach64->accel.src_y_count,
                                               mach64->accel.src_width1,
                                               mach64->accel.src_height1,
                                               mach64->src_height1_width1,
                                               mach64->src_height2_width2);

        mach64->accel.src_pitch  = (mach64->src_off_pitch >> 22) * 8;
        mach64->accel.src_offset = (mach64->src_off_pitch & 0xfffff) * 8;

        mach64->accel.dst_pitch  = (mach64->dst_off_pitch >> 22) * 8;
        mach64->accel.dst_offset = (mach64->dst_off_pitch & 0xfffff) * 8;

        mach64->accel.mix_fg = (mach64->dp_mix >> 16) & 0x1f;
        mach64->accel.mix_bg = mach64->dp_mix & 0x1f;

        mach64->accel.source_bg  =  mach64->dp_src & 7;
        mach64->accel.source_fg  = (mach64->dp_src >> 8) & 7;
        mach64->accel.source_mix = (mach64->dp_src >> 16) & 7;

        mach64->accel.dst_pix_width  =  mach64->dp_pix_width        & 7;
        mach64->accel.src_pix_width  = (mach64->dp_pix_width >>  8) & 7;
        mach64->accel.host_pix_width = (mach64->dp_pix_width >> 16) & 7;

        mach64->accel.dst_size = mach64_width[mach64->accel.dst_pix_width];
        mach64->accel.src_size = mach64_width[mach64->accel.src_pix_width];
        mach64->accel.host_size = mach64_width[mach64->accel.host_pix_width];

        if (mach64->accel.src_size == WIDTH_1BIT)
                mach64->accel.src_offset <<= 3;
        else
                mach64->accel.src_offset >>= mach64->accel.src_size;

        if (mach64->accel.dst_size == WIDTH_1BIT)
                mach64->accel.dst_offset <<= 3;
        else
                mach64->accel.dst_offset >>= mach64->accel.dst_size;

        mach64->accel.xinc = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
        mach64->accel.yinc = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;

        mach64->accel.source_host = ((mach64->dp_src & 7) == SRC_HOST) || (((mach64->dp_src >> 8) & 7) == SRC_HOST);


        for (y = 0; y < 8; y++)
        {
                for (x = 0; x < 8; x++)
                {
                        uint32_t temp = (y & 4) ? mach64->pat_reg1 : mach64->pat_reg0;
                        mach64->accel.pattern[y][7 - x] = (temp >> (x + ((y & 3) * 8))) & 1;
                }
        }

		if (mach64->pat_cntl & 2) {
			mach64->accel.pattern_clr4x2[0][0] = (mach64->pat_reg0 & 0xff);
			mach64->accel.pattern_clr4x2[0][1] = ((mach64->pat_reg0 >> 8) & 0xff);
			mach64->accel.pattern_clr4x2[0][2] = ((mach64->pat_reg0 >> 16) & 0xff);
			mach64->accel.pattern_clr4x2[0][3] = ((mach64->pat_reg0 >> 24) & 0xff);
			mach64->accel.pattern_clr4x2[1][0] = (mach64->pat_reg1 & 0xff);
			mach64->accel.pattern_clr4x2[1][1] = ((mach64->pat_reg1 >> 8) & 0xff);
			mach64->accel.pattern_clr4x2[1][2] = ((mach64->pat_reg1 >> 16) & 0xff);
			mach64->accel.pattern_clr4x2[1][3] = ((mach64->pat_reg1 >> 24) & 0xff);
		} else if (mach64->pat_cntl & 4) {
			mach64->accel.pattern_clr8x1[0] = (mach64->pat_reg0 & 0xff);
			mach64->accel.pattern_clr8x1[1] = ((mach64->pat_reg0 >> 8) & 0xff);
			mach64->accel.pattern_clr8x1[2] = ((mach64->pat_reg0 >> 16) & 0xff);
			mach64->accel.pattern_clr8x1[3] = ((mach64->pat_reg0 >> 24) & 0xff);
			mach64->accel.pattern_clr8x1[4] = (mach64->pat_reg1 & 0xff);
			mach64->accel.pattern_clr8x1[5] = ((mach64->pat_reg1 >> 8) & 0xff);
			mach64->accel.pattern_clr8x1[6] = ((mach64->pat_reg1 >> 16) & 0xff);
			mach64->accel.pattern_clr8x1[7] = ((mach64->pat_reg1 >> 24) & 0xff);
		}

        mach64->accel.sc_left   =  mach64->sc_left_right & 0x1fff;
        mach64->accel.sc_right  = (mach64->sc_left_right >> 16) & 0x1fff;
        mach64->accel.sc_top    =  mach64->sc_top_bottom & 0x7fff;
        mach64->accel.sc_bottom = (mach64->sc_top_bottom >> 16) & 0x7fff;

        mach64->accel.dp_frgd_clr = mach64->dp_frgd_clr;
        mach64->accel.dp_bkgd_clr = mach64->dp_bkgd_clr;
        mach64->accel.write_mask = mach64->write_mask;

        mach64->accel.clr_cmp_clr = mach64->clr_cmp_clr & mach64->clr_cmp_mask;
        mach64->accel.clr_cmp_mask = mach64->clr_cmp_mask;
        mach64->accel.clr_cmp_fn = mach64->clr_cmp_cntl & 7;
        mach64->accel.clr_cmp_src = mach64->clr_cmp_cntl & (1 << 24);

        mach64->accel.poly_draw = 0;

        mach64->accel.busy = 1;
        mach64_log("mach64_start_fill : dst %i, %i  src %i, %i  size %i, %i  src pitch %i offset %X  dst pitch %i offset %X  scissor %i %i %i %i  src_fg %i  mix %02X %02X\n", mach64->accel.dst_x_start, mach64->accel.dst_y_start, mach64->accel.src_x_start, mach64->accel.src_y_start, mach64->accel.dst_width, mach64->accel.dst_height, mach64->accel.src_pitch, mach64->accel.src_offset, mach64->accel.dst_pitch, mach64->accel.dst_offset, mach64->accel.sc_left, mach64->accel.sc_right, mach64->accel.sc_top, mach64->accel.sc_bottom, mach64->accel.source_fg, mach64->accel.mix_fg, mach64->accel.mix_bg);

        mach64->accel.op = OP_RECT;
}

void mach64_start_line(mach64_t *mach64)
{
        int x, y;

        mach64->accel.dst_x = (mach64->dst_y_x >> 16) & 0xfff;
        mach64->accel.dst_y =  mach64->dst_y_x        & 0xfff;

        mach64->accel.src_x = (mach64->src_y_x >> 16) & 0xfff;
        mach64->accel.src_y =  mach64->src_y_x        & 0xfff;

        mach64->accel.src_pitch  = (mach64->src_off_pitch >> 22) * 8;
        mach64->accel.src_offset = (mach64->src_off_pitch & 0xfffff) * 8;

        mach64->accel.dst_pitch  = (mach64->dst_off_pitch >> 22) * 8;
        mach64->accel.dst_offset = (mach64->dst_off_pitch & 0xfffff) * 8;

        mach64->accel.mix_fg = (mach64->dp_mix >> 16) & 0x1f;
        mach64->accel.mix_bg = mach64->dp_mix & 0x1f;

        mach64->accel.source_bg  =  mach64->dp_src & 7;
        mach64->accel.source_fg  = (mach64->dp_src >> 8) & 7;
        mach64->accel.source_mix = (mach64->dp_src >> 16) & 7;

        mach64->accel.dst_pix_width  =  mach64->dp_pix_width        & 7;
        mach64->accel.src_pix_width  = (mach64->dp_pix_width >>  8) & 7;
        mach64->accel.host_pix_width = (mach64->dp_pix_width >> 16) & 7;

        mach64->accel.dst_size = mach64_width[mach64->accel.dst_pix_width];
        mach64->accel.src_size = mach64_width[mach64->accel.src_pix_width];
        mach64->accel.host_size = mach64_width[mach64->accel.host_pix_width];

        if (mach64->accel.src_size == WIDTH_1BIT)
                mach64->accel.src_offset <<= 3;
        else
                mach64->accel.src_offset >>= mach64->accel.src_size;

        if (mach64->accel.dst_size == WIDTH_1BIT)
                mach64->accel.dst_offset <<= 3;
        else
                mach64->accel.dst_offset >>= mach64->accel.dst_size;

/*        mach64->accel.src_pitch *= mach64_inc[mach64->accel.src_pix_width];
        mach64->accel.dst_pitch *= mach64_inc[mach64->accel.dst_pix_width];*/

        mach64->accel.source_host = ((mach64->dp_src & 7) == SRC_HOST) || (((mach64->dp_src >> 8) & 7) == SRC_HOST);

        for (y = 0; y < 8; y++)
        {
                for (x = 0; x < 8; x++)
                {
                        uint32_t temp = (y & 4) ? mach64->pat_reg1 : mach64->pat_reg0;
                        mach64->accel.pattern[y][7 - x] = (temp >> (x + ((y & 3) * 8))) & 1;
                }
        }

        mach64->accel.sc_left   =  mach64->sc_left_right & 0x1fff;
        mach64->accel.sc_right  = (mach64->sc_left_right >> 16) & 0x1fff;
        mach64->accel.sc_top    =  mach64->sc_top_bottom & 0x7fff;
        mach64->accel.sc_bottom = (mach64->sc_top_bottom >> 16) & 0x7fff;

        mach64->accel.dp_frgd_clr = mach64->dp_frgd_clr;
        mach64->accel.dp_bkgd_clr = mach64->dp_bkgd_clr;
        mach64->accel.write_mask = mach64->write_mask;

        mach64->accel.x_count = mach64->dst_bres_lnth & 0x7fff;
        mach64->accel.err = (mach64->dst_bres_err & 0x3ffff) | ((mach64->dst_bres_err & 0x40000) ? 0xfffc0000 : 0);

        mach64->accel.clr_cmp_clr = mach64->clr_cmp_clr & mach64->clr_cmp_mask;
        mach64->accel.clr_cmp_mask = mach64->clr_cmp_mask;
        mach64->accel.clr_cmp_fn = mach64->clr_cmp_cntl & 7;
        mach64->accel.clr_cmp_src = mach64->clr_cmp_cntl & (1 << 24);

        mach64->accel.xinc = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
        mach64->accel.yinc = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;

        mach64->accel.busy = 1;
        mach64_log("mach64_start_line\n");

        mach64->accel.op = OP_LINE;
}

#define READ(addr, dat, width) if (width == 0)      dat =               svga->vram[((addr))      & mach64->vram_mask]; \
                               else if (width == 1) dat = *(uint16_t *)&svga->vram[((addr) << 1) & mach64->vram_mask]; \
                               else if (width == 2) dat = *(uint32_t *)&svga->vram[((addr) << 2) & mach64->vram_mask]; \
                   else if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)  dat = (svga->vram[((addr) >> 3) & mach64->vram_mask] >> ((addr) & 7)) & 1; \
                               else                 dat = (svga->vram[((addr) >> 3) & mach64->vram_mask] >> (7 - ((addr) & 7))) & 1;

#define MIX     switch (mix ? mach64->accel.mix_fg : mach64->accel.mix_bg)                                \
                {                                                                                       \
                        case 0x0: dest_dat =             ~dest_dat;  break;                             \
                        case 0x1: dest_dat =  0;                     break;                             \
                        case 0x2: dest_dat =  0xffffffff;            break;                             \
                        case 0x3: dest_dat =              dest_dat;  break;                             \
                        case 0x4: dest_dat =  ~src_dat;              break;                             \
                        case 0x5: dest_dat =   src_dat ^  dest_dat;  break;                             \
                        case 0x6: dest_dat = ~(src_dat ^  dest_dat); break;                             \
                        case 0x7: dest_dat =   src_dat;              break;                             \
                        case 0x8: dest_dat = ~(src_dat &  dest_dat); break;                             \
                        case 0x9: dest_dat =  ~src_dat |  dest_dat;  break;                             \
                        case 0xa: dest_dat =   src_dat | ~dest_dat;  break;                             \
                        case 0xb: dest_dat =   src_dat |  dest_dat;  break;                             \
                        case 0xc: dest_dat =   src_dat &  dest_dat;  break;                             \
                        case 0xd: dest_dat =   src_dat & ~dest_dat;  break;                             \
                        case 0xe: dest_dat =  ~src_dat &  dest_dat;  break;                             \
                        case 0xf: dest_dat = ~(src_dat |  dest_dat); break;                             \
                        case 0x17: dest_dat = (dest_dat + src_dat) >> 1; \
                }

#define WRITE(addr, width)      if (width == 0)                                                         \
                                {                                                                       \
                                        svga->vram[(addr) & mach64->vram_mask] = dest_dat;                             \
                                        svga->changedvram[((addr) & mach64->vram_mask) >> 12] = changeframecount;      \
                                }                                                                       \
                                else if (width == 1)                                                    \
                                {                                                                       \
                                        *(uint16_t *)&svga->vram[((addr) << 1) & mach64->vram_mask] = dest_dat;          \
                                        svga->changedvram[(((addr) << 1) & mach64->vram_mask) >> 12] = changeframecount; \
                                }                                                                       \
                                else if (width == 2)                                                    \
                                {                                                                       \
                                        *(uint32_t *)&svga->vram[((addr) << 2) & mach64->vram_mask] = dest_dat;          \
                                        svga->changedvram[(((addr) << 2) & mach64->vram_mask) >> 12] = changeframecount; \
                                }                                                                                               \
                                else                                                                                            \
                                {                                                                                               \
                                        if (dest_dat & 1) {                                                                     \
						if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)                   		\
                                                    svga->vram[((addr) >> 3) & mach64->vram_mask] |= 1 << ((addr) & 7);         \
						else                                        					\
                                                    svga->vram[((addr) >> 3) & mach64->vram_mask] |= 1 << (7 - ((addr) & 7));   \
                                        } else {                                                                                \
                        			if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)                   		\
                                                    svga->vram[((addr) >> 3) & mach64->vram_mask] &= ~(1 << ((addr) & 7));  	\
						else                                        					\
                                                    svga->vram[((addr) >> 3) & mach64->vram_mask] &= ~(1 << (7 - ((addr) & 7)));\
					}                                           						\
                                        svga->changedvram[(((addr) >> 3) & mach64->vram_mask) >> 12] = changeframecount;        \
                                }

void mach64_blit(uint32_t cpu_dat, int count, mach64_t *mach64)
{
        svga_t *svga = &mach64->svga;
        int cmp_clr = 0;

        if (!mach64->accel.busy)
        {
                mach64_log("mach64_blit : return as not busy\n");
                return;
        }
        switch (mach64->accel.op)
        {
                case OP_RECT:
				while (count)
                {
                        uint32_t src_dat, dest_dat;
                        uint32_t host_dat = 0;
                        uint32_t old_dest_dat;
                        int mix = 0;
                        int dst_x = (mach64->accel.dst_x + mach64->accel.dst_x_start) & 0xfff;
                        int dst_y = (mach64->accel.dst_y + mach64->accel.dst_y_start) & 0xfff;
                        int src_x;
                        int src_y = (mach64->accel.src_y + mach64->accel.src_y_start) & 0xfff;

                        if (mach64->src_cntl & SRC_LINEAR_EN)
                                src_x = mach64->accel.src_x;
                        else
                                src_x = (mach64->accel.src_x + mach64->accel.src_x_start) & 0xfff;

                        if (mach64->accel.source_host)
                        {
                                host_dat = cpu_dat;
                                switch (mach64->accel.host_size)
                                {
                                        case 0:
                                        cpu_dat >>= 8;
                                        count -= 8;
                                        break;
                                        case 1:
                                        cpu_dat >>= 16;
                                        count -= 16;
                                        break;
                                        case 2:
                                        count -= 32;
                                        break;
                                }
                        }
                        else
                           count--;

                        switch (mach64->accel.source_mix)
                        {
                                case MONO_SRC_HOST:
                                if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)
                                {
                                        mix = cpu_dat & 1;
                                        cpu_dat >>= 1;
                                }
                                else
                                {
                                        mix = cpu_dat >> 31;
                                        cpu_dat <<= 1;
                                }
                                break;
                                case MONO_SRC_PAT:
                                mix = mach64->accel.pattern[dst_y & 7][dst_x & 7];
                                break;
                                case MONO_SRC_1:
                                mix = 1;
                                break;
                                case MONO_SRC_BLITSRC:
                                if (mach64->src_cntl & SRC_LINEAR_EN)
                                {
                                        READ(mach64->accel.src_offset + src_x, mix, WIDTH_1BIT);
                                }
                                else
                                {
                                        READ(mach64->accel.src_offset + (src_y * mach64->accel.src_pitch) + src_x, mix, WIDTH_1BIT);
                                }
                                break;
                        }

                        if (dst_x >= mach64->accel.sc_left && dst_x <= mach64->accel.sc_right &&
                            dst_y >= mach64->accel.sc_top  && dst_y <= mach64->accel.sc_bottom)
                        {
                                switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg)
                                {
                                        case SRC_HOST:
                                        src_dat = host_dat;
                                        break;
                                        case SRC_BLITSRC:
                                        READ(mach64->accel.src_offset + (src_y * mach64->accel.src_pitch) + src_x, src_dat, mach64->accel.src_size);
                                        break;
                                        case SRC_FG:
                                        if ((mach64->dst_cntl & (DST_LAST_PEL | DST_X_DIR | DST_Y_DIR | DST_24_ROT_EN)) == (DST_LAST_PEL | DST_X_DIR | DST_Y_DIR | DST_24_ROT_EN)) {
                                            if ((mach64->accel.x_count % 3) == 2)
                                                src_dat = mach64->accel.dp_frgd_clr & 0xff;
                                            else if ((mach64->accel.x_count % 3) == 1)
                                                src_dat = (mach64->accel.dp_frgd_clr >> 8) & 0xff;
                                            else if ((mach64->accel.x_count % 3) == 0)
                                                src_dat = (mach64->accel.dp_frgd_clr >> 16) & 0xff;
                                        } else
                                            src_dat = mach64->accel.dp_frgd_clr;
                                        break;
                                        case SRC_BG:
                                        if ((mach64->dst_cntl & (DST_LAST_PEL | DST_X_DIR | DST_Y_DIR | DST_24_ROT_EN)) == (DST_LAST_PEL | DST_X_DIR | DST_Y_DIR | DST_24_ROT_EN)) {
                                            if ((mach64->accel.x_count % 3) == 2)
                                                src_dat = mach64->accel.dp_bkgd_clr & 0xff;
                                            else if ((mach64->accel.x_count % 3) == 1)
                                                src_dat = (mach64->accel.dp_bkgd_clr >> 8) & 0xff;
                                            else if ((mach64->accel.x_count % 3) == 0)
                                                src_dat = (mach64->accel.dp_bkgd_clr >> 16) & 0xff;
                                        } else
                                            src_dat = mach64->accel.dp_bkgd_clr;
                                        break;
										case SRC_PAT:
										if (mach64->pat_cntl & 2) {
											src_dat = mach64->accel.pattern_clr4x2[dst_y & 1][dst_x & 3];
											break;
										} else if (mach64->pat_cntl & 4) {
											src_dat = mach64->accel.pattern_clr8x1[dst_x & 7];
											break;
										}
                                        default:
                                        src_dat = 0;
                                        break;
                                }

                                if (mach64->dst_cntl & DST_POLYGON_EN)
                                {
                                        int poly_src;
                                        READ(mach64->accel.src_offset + (src_y * mach64->accel.src_pitch) + src_x, poly_src, mach64->accel.src_size);
                                        if (poly_src)
                                                mach64->accel.poly_draw = !mach64->accel.poly_draw;
                                }

                                if (!(mach64->dst_cntl & DST_POLYGON_EN) || mach64->accel.poly_draw)
                                {
                                        READ(mach64->accel.dst_offset + (dst_y * mach64->accel.dst_pitch) + dst_x, dest_dat, mach64->accel.dst_size);

                                        switch (mach64->accel.clr_cmp_fn)
                                        {
                                                case 1: /*TRUE*/
                                                cmp_clr = 1;
                                                break;
                                                case 4: /*DST_CLR != CLR_CMP_CLR*/
                                                cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) != mach64->accel.clr_cmp_clr;
                                                break;
                                                case 5: /*DST_CLR == CLR_CMP_CLR*/
                                                cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) == mach64->accel.clr_cmp_clr;
                                                break;
                                        }

                                        if (!cmp_clr) {
                                            old_dest_dat = dest_dat;
                                            MIX
                                            dest_dat = (dest_dat & mach64->accel.write_mask) | (old_dest_dat & ~mach64->accel.write_mask);
                                        }

                                        WRITE(mach64->accel.dst_offset + (dst_y * mach64->accel.dst_pitch) + dst_x, mach64->accel.dst_size);
                                }
                        }

                        if (((mach64->crtc_gen_cntl >> 8) & 7) == BPP_24) {
                            if ((mach64->dst_cntl & (DST_LAST_PEL | DST_X_DIR | DST_Y_DIR | DST_24_ROT_EN)) != (DST_LAST_PEL | DST_X_DIR | DST_Y_DIR | DST_24_ROT_EN)) {
                                mach64->accel.dp_frgd_clr = ((mach64->accel.dp_frgd_clr >> 8) & 0xffff) | (mach64->accel.dp_frgd_clr << 16);
                                mach64->accel.dp_bkgd_clr = ((mach64->accel.dp_bkgd_clr >> 8) & 0xffff) | (mach64->accel.dp_bkgd_clr << 16);
                                mach64->accel.write_mask = ((mach64->accel.write_mask >> 8) & 0xffff) | (mach64->accel.write_mask << 16);
                            }
                        }

                        mach64->accel.src_x += mach64->accel.xinc;
                        mach64->accel.dst_x += mach64->accel.xinc;
                        if (!(mach64->src_cntl & SRC_LINEAR_EN))
                        {
                                mach64->accel.src_x_count--;
                                if (mach64->accel.src_x_count <= 0)
                                {
                                        mach64->accel.src_x = 0;
                                        if ((mach64->src_cntl & (SRC_PATT_ROT_EN | SRC_PATT_EN)) == (SRC_PATT_ROT_EN | SRC_PATT_EN))
                                        {
                                                mach64->accel.src_x_start = (mach64->src_y_x_start >> 16) & 0xfff;
                                                mach64->accel.src_x_count = mach64->accel.src_width2;
                                        }
                                        else
                                                mach64->accel.src_x_count = mach64->accel.src_width1;
                                }
                        }

                        mach64->accel.x_count--;
                        if (mach64->accel.x_count <= 0)
                        {
                                mach64->accel.x_count = mach64->accel.dst_width;
                                mach64->accel.dst_x = 0;
                                mach64->accel.dst_y += mach64->accel.yinc;
                                mach64->accel.src_x_start = (mach64->src_y_x >> 16) & 0xfff;
                                mach64->accel.src_x_count = mach64->accel.src_width1;

                                if (!(mach64->src_cntl & SRC_LINEAR_EN))
                                {
                                        mach64->accel.src_x = 0;
                                        mach64->accel.src_y += mach64->accel.yinc;
                                        mach64->accel.src_y_count--;
                                        if (mach64->accel.src_y_count <= 0)
                                        {
                                                mach64->accel.src_y = 0;
                                                if ((mach64->src_cntl & (SRC_PATT_ROT_EN | SRC_PATT_EN)) == (SRC_PATT_ROT_EN | SRC_PATT_EN))
                                                {
                                                        mach64->accel.src_y_start = mach64->src_y_x_start & 0xfff;
                                                        mach64->accel.src_y_count = mach64->accel.src_height2;
                                                }
                                                else
                                                        mach64->accel.src_y_count = mach64->accel.src_height1;
                                        }
                                }

                                mach64->accel.poly_draw = 0;

                                mach64->accel.dst_height--;

                                if (mach64->accel.dst_height <= 0)
                                {
                                        /*Blit finished*/
                                        mach64_log("mach64 blit finished\n");
                                        mach64->accel.busy = 0;
                                        if (mach64->dst_cntl & DST_X_TILE)
                                                mach64->dst_y_x = (mach64->dst_y_x & 0xfff) | ((mach64->dst_y_x + (mach64->accel.dst_width << 16)) & 0xfff0000);
                                        if (mach64->dst_cntl & DST_Y_TILE)
                                                mach64->dst_y_x = (mach64->dst_y_x & 0xfff0000) | ((mach64->dst_y_x + (mach64->dst_height_width & 0x1fff)) & 0xfff);
                                        return;
                                }
                                if (mach64->host_cntl & HOST_BYTE_ALIGN)
                                {
                                        if (mach64->accel.source_mix == MONO_SRC_HOST)
                                        {
                                                if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)
                                                        cpu_dat >>= (count & 7);
                                                else
                                                        cpu_dat <<= (count & 7);
                                                count &= ~7;
                                        }
                                }
                        }
                }
                break;

                case OP_LINE:
                if (((mach64->crtc_gen_cntl >> 8) & 7) == BPP_24) {
                    int x = 0;
                    while (count) {
                        uint32_t src_dat = 0, dest_dat;
                        uint32_t host_dat = 0;
                        int mix = 0;

                        if (mach64->accel.source_host)
                        {
                                host_dat = cpu_dat;
                                switch (mach64->accel.src_size)
                                {
                                        case 0:
                                        cpu_dat >>= 8;
                                        count -= 8;
                                        break;
                                        case 1:
                                        cpu_dat >>= 16;
                                        count -= 16;
                                        break;
                                        case 2:
                                        count -= 32;
                                        break;
                                }
                        }
                        else
                           count--;

                        switch (mach64->accel.source_mix)
                        {
                                case MONO_SRC_HOST:
                                 if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)
                                {
                                        mix = cpu_dat & 1;
                                        cpu_dat >>= 1;
                                }
                                else
                                {
                                        mix = cpu_dat >> 31;
                                        cpu_dat <<= 1;
                                }
                                break;
                                case MONO_SRC_PAT:
                                mix = mach64->accel.pattern[mach64->accel.dst_y & 7][mach64->accel.dst_x & 7];
                                break;
                                case MONO_SRC_1:
                                mix = 1;
                                break;
                                case MONO_SRC_BLITSRC:
                                READ(mach64->accel.src_offset + (mach64->accel.src_y * mach64->accel.src_pitch) + mach64->accel.src_x, mix, WIDTH_1BIT);
                                break;
                        }

                        if ((mach64->accel.dst_x >= mach64->accel.sc_left) && (mach64->accel.dst_x <= mach64->accel.sc_right) &&
                            (mach64->accel.dst_y >= mach64->accel.sc_top) && (mach64->accel.dst_y <= mach64->accel.sc_bottom)) {
                            switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg)
                            {
                                    case SRC_HOST:
                                    src_dat = host_dat;
                                    break;
                                    case SRC_BLITSRC:
                                    READ(mach64->accel.src_offset + (mach64->accel.src_y * mach64->accel.src_pitch) + mach64->accel.src_x, src_dat, mach64->accel.src_size);
                                    break;
                                    case SRC_FG:
                                    src_dat = mach64->accel.dp_frgd_clr;
                                    break;
                                    case SRC_BG:
                                    src_dat = mach64->accel.dp_bkgd_clr;
                                    break;
                                    case SRC_PAT:
                                    if (mach64->pat_cntl & 2) {
                                        src_dat = mach64->accel.pattern_clr4x2[mach64->accel.dst_y & 1][mach64->accel.dst_x & 3];
                                        break;
                                    } else if (mach64->pat_cntl & 4) {
                                        src_dat = mach64->accel.pattern_clr8x1[mach64->accel.dst_x & 7];
                                        break;
                                    }
                                    default:
                                    src_dat = 0;
                                    break;
                            }

                            READ(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, dest_dat, mach64->accel.dst_size);

                            switch (mach64->accel.clr_cmp_fn) {
                                case 1: /*TRUE*/
                                cmp_clr = 1;
                                break;
                                case 4: /*DST_CLR != CLR_CMP_CLR*/
                                cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) != mach64->accel.clr_cmp_clr;
                                break;
                                case 5: /*DST_CLR == CLR_CMP_CLR*/
                                cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) == mach64->accel.clr_cmp_clr;
                                break;
                            }

                            if (!cmp_clr)
                                MIX

                            if (!(mach64->dst_cntl & DST_Y_MAJOR)) {
                                if (x == 0)
                                    dest_dat &= ~1;
                            } else {
                                if (x == (mach64->accel.x_count - 1))
                                    dest_dat &= ~1;
                            }

                            WRITE(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, mach64->accel.dst_size);
                        }

                        x++;
                        if (x >= mach64->accel.x_count) {
                            mach64->accel.busy = 0;
                            mach64_log("mach64 line24 finished\n");
                            return;
                        }

                        if (mach64->dst_cntl & DST_Y_MAJOR) {
                            mach64->accel.dst_y += mach64->accel.yinc;
                            if (mach64->accel.err >= 0) {
                                mach64->accel.err += mach64->dst_bres_dec;
                                mach64->accel.dst_x += mach64->accel.xinc;
                            } else {
                                mach64->accel.err += mach64->dst_bres_inc;
                            }
                        } else {
                            mach64->accel.dst_x += mach64->accel.xinc;
                            if (mach64->accel.err >= 0) {
                                mach64->accel.err += mach64->dst_bres_dec;
                                mach64->accel.dst_y += mach64->accel.yinc;
                            } else {
                                mach64->accel.err += mach64->dst_bres_inc;
                            }
                        }
                    }
                } else {
                    while (count)
                    {
                            uint32_t src_dat = 0, dest_dat;
                            uint32_t host_dat = 0;
                            int mix = 0;
                            int draw_pixel = !(mach64->dst_cntl & DST_POLYGON_EN);

                            if (mach64->accel.source_host)
                            {
                                    host_dat = cpu_dat;
                                    switch (mach64->accel.src_size)
                                    {
                                            case 0:
                                            cpu_dat >>= 8;
                                            count -= 8;
                                            break;
                                            case 1:
                                            cpu_dat >>= 16;
                                            count -= 16;
                                            break;
                                            case 2:
                                            count -= 32;
                                            break;
                                    }
                            }
                            else
                               count--;

                            switch (mach64->accel.source_mix)
                            {
                                    case MONO_SRC_HOST:
                                    mix = cpu_dat >> 31;
                                    cpu_dat <<= 1;
                                    break;
                                    case MONO_SRC_PAT:
                                    mix = mach64->accel.pattern[mach64->accel.dst_y & 7][mach64->accel.dst_x & 7];
                                    break;
                                    case MONO_SRC_1:
                                    default:
                                    mix = 1;
                                    break;
                            }

                            if (mach64->dst_cntl & DST_POLYGON_EN)
                            {
                                    if (mach64->dst_cntl & DST_Y_MAJOR)
                                            draw_pixel = 1;
                                    else if ((mach64->dst_cntl & DST_X_DIR) && mach64->accel.err < (mach64->dst_bres_dec + mach64->dst_bres_inc)) /*X+*/
                                            draw_pixel = 1;
                                    else if (!(mach64->dst_cntl & DST_X_DIR) && mach64->accel.err >= 0) /*X-*/
                                            draw_pixel = 1;
                            }

                            if (mach64->accel.x_count == 1 && !(mach64->dst_cntl & DST_LAST_PEL))
                                    draw_pixel = 0;

                            if (mach64->accel.dst_x >= mach64->accel.sc_left && mach64->accel.dst_x <= mach64->accel.sc_right &&
                                mach64->accel.dst_y >= mach64->accel.sc_top  && mach64->accel.dst_y <= mach64->accel.sc_bottom && draw_pixel)
                            {
                                    switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg)
                                    {
                                            case SRC_HOST:
                                            src_dat = host_dat;
                                            break;
                                            case SRC_BLITSRC:
                                            READ(mach64->accel.src_offset + (mach64->accel.src_y * mach64->accel.src_pitch) + mach64->accel.src_x, src_dat, mach64->accel.src_size);
                                            break;
                                            case SRC_FG:
                                            src_dat = mach64->accel.dp_frgd_clr;
                                            break;
                                            case SRC_BG:
                                            src_dat = mach64->accel.dp_bkgd_clr;
                                            break;
                                            default:
                                            src_dat = 0;
                                            break;
                                    }

                                    READ(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, dest_dat, mach64->accel.dst_size);

                                    switch (mach64->accel.clr_cmp_fn)
                                    {
                                            case 1: /*TRUE*/
                                            cmp_clr = 1;
                                            break;
                                            case 4: /*DST_CLR != CLR_CMP_CLR*/
                                            cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) != mach64->accel.clr_cmp_clr;
                                            break;
                                            case 5: /*DST_CLR == CLR_CMP_CLR*/
                                            cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) == mach64->accel.clr_cmp_clr;
                                            break;
                                    }

                                    if (!cmp_clr)
                                            MIX

                                    WRITE(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, mach64->accel.dst_size);
                            }

                            mach64->accel.x_count--;
                            if (mach64->accel.x_count <= 0)
                            {
                                    /*Blit finished*/
                                    mach64_log("mach64 blit finished\n");
                                    mach64->accel.busy = 0;
                                    return;
                            }

                            switch (mach64->dst_cntl & 7)
                            {
                                    case 0: case 2:
                                    mach64->accel.src_x--;
                                    mach64->accel.dst_x--;
                                    break;
                                    case 1: case 3:
                                    mach64->accel.src_x++;
                                    mach64->accel.dst_x++;
                                    break;
                                    case 4: case 5:
                                    mach64->accel.src_y--;
                                    mach64->accel.dst_y--;
                                    break;
                                    case 6: case 7:
                                    mach64->accel.src_y++;
                                    mach64->accel.dst_y++;
                                    break;
                            }
                            mach64_log("x %i y %i err %i inc %i dec %i\n", mach64->accel.dst_x, mach64->accel.dst_y, mach64->accel.err, mach64->dst_bres_inc, mach64->dst_bres_dec);
                            if (mach64->accel.err >= 0)
                            {
                                    mach64->accel.err += mach64->dst_bres_dec;

                                    switch (mach64->dst_cntl & 7)
                                    {
                                            case 0: case 1:
                                            mach64->accel.src_y--;
                                            mach64->accel.dst_y--;
                                            break;
                                            case 2: case 3:
                                            mach64->accel.src_y++;
                                            mach64->accel.dst_y++;
                                            break;
                                            case 4: case 6:
                                            mach64->accel.src_x--;
                                            mach64->accel.dst_x--;
                                            break;
                                            case 5: case 7:
                                            mach64->accel.src_x++;
                                            mach64->accel.dst_x++;
                                            break;
                                    }
                            }
                            else
                                    mach64->accel.err += mach64->dst_bres_inc;
                    }
                }
                break;
        }
}

void mach64_load_context(mach64_t *mach64)
{
        svga_t *svga = &mach64->svga;
        uint32_t addr;

        while (mach64->context_load_cntl & 0x30000)
        {
                addr = ((0x3fff - (mach64->context_load_cntl & 0x3fff)) * 256) & mach64->vram_mask;
                mach64->context_mask = *(uint32_t *)&svga->vram[addr];
                mach64_log("mach64_load_context %08X from %08X : mask %08X\n", mach64->context_load_cntl, addr, mach64->context_mask);

                if (mach64->context_mask & (1 << 2))
                        mach64_accel_write_fifo_l(mach64, 0x100, *(uint32_t *)&svga->vram[addr + 0x08]);
                if (mach64->context_mask & (1 << 3))
                        mach64_accel_write_fifo_l(mach64, 0x10c, *(uint32_t *)&svga->vram[addr + 0x0c]);
                if (mach64->context_mask & (1 << 4))
                        mach64_accel_write_fifo_l(mach64, 0x118, *(uint32_t *)&svga->vram[addr + 0x10]);
                if (mach64->context_mask & (1 << 5))
                        mach64_accel_write_fifo_l(mach64, 0x124, *(uint32_t *)&svga->vram[addr + 0x14]);
                if (mach64->context_mask & (1 << 6))
                        mach64_accel_write_fifo_l(mach64, 0x128, *(uint32_t *)&svga->vram[addr + 0x18]);
                if (mach64->context_mask & (1 << 7))
                        mach64_accel_write_fifo_l(mach64, 0x12c, *(uint32_t *)&svga->vram[addr + 0x1c]);
                if (mach64->context_mask & (1 << 8))
                        mach64_accel_write_fifo_l(mach64, 0x180, *(uint32_t *)&svga->vram[addr + 0x20]);
                if (mach64->context_mask & (1 << 9))
                        mach64_accel_write_fifo_l(mach64, 0x18c, *(uint32_t *)&svga->vram[addr + 0x24]);
                if (mach64->context_mask & (1 << 10))
                        mach64_accel_write_fifo_l(mach64, 0x198, *(uint32_t *)&svga->vram[addr + 0x28]);
                if (mach64->context_mask & (1 << 11))
                        mach64_accel_write_fifo_l(mach64, 0x1a4, *(uint32_t *)&svga->vram[addr + 0x2c]);
                if (mach64->context_mask & (1 << 12))
                        mach64_accel_write_fifo_l(mach64, 0x1b0, *(uint32_t *)&svga->vram[addr + 0x30]);
                if (mach64->context_mask & (1 << 13))
                        mach64_accel_write_fifo_l(mach64, 0x280, *(uint32_t *)&svga->vram[addr + 0x34]);
                if (mach64->context_mask & (1 << 14))
                        mach64_accel_write_fifo_l(mach64, 0x284, *(uint32_t *)&svga->vram[addr + 0x38]);
                if (mach64->context_mask & (1 << 15))
                        mach64_accel_write_fifo_l(mach64, 0x2a8, *(uint32_t *)&svga->vram[addr + 0x3c]);
                if (mach64->context_mask & (1 << 16))
                        mach64_accel_write_fifo_l(mach64, 0x2b4, *(uint32_t *)&svga->vram[addr + 0x40]);
                if (mach64->context_mask & (1 << 17))
                        mach64_accel_write_fifo_l(mach64, 0x2c0, *(uint32_t *)&svga->vram[addr + 0x44]);
                if (mach64->context_mask & (1 << 18))
                        mach64_accel_write_fifo_l(mach64, 0x2c4, *(uint32_t *)&svga->vram[addr + 0x48]);
                if (mach64->context_mask & (1 << 19))
                        mach64_accel_write_fifo_l(mach64, 0x2c8, *(uint32_t *)&svga->vram[addr + 0x4c]);
                if (mach64->context_mask & (1 << 20))
                        mach64_accel_write_fifo_l(mach64, 0x2cc, *(uint32_t *)&svga->vram[addr + 0x50]);
                if (mach64->context_mask & (1 << 21))
                        mach64_accel_write_fifo_l(mach64, 0x2d0, *(uint32_t *)&svga->vram[addr + 0x54]);
                if (mach64->context_mask & (1 << 22))
                        mach64_accel_write_fifo_l(mach64, 0x2d4, *(uint32_t *)&svga->vram[addr + 0x58]);
                if (mach64->context_mask & (1 << 23))
                        mach64_accel_write_fifo_l(mach64, 0x2d8, *(uint32_t *)&svga->vram[addr + 0x5c]);
                if (mach64->context_mask & (1 << 24))
                        mach64_accel_write_fifo_l(mach64, 0x300, *(uint32_t *)&svga->vram[addr + 0x60]);
                if (mach64->context_mask & (1 << 25))
                        mach64_accel_write_fifo_l(mach64, 0x304, *(uint32_t *)&svga->vram[addr + 0x64]);
                if (mach64->context_mask & (1 << 26))
                        mach64_accel_write_fifo_l(mach64, 0x308, *(uint32_t *)&svga->vram[addr + 0x68]);
                if (mach64->context_mask & (1 << 27))
                        mach64_accel_write_fifo_l(mach64, 0x330, *(uint32_t *)&svga->vram[addr + 0x6c]);

                mach64->context_load_cntl = *(uint32_t *)&svga->vram[addr + 0x70];
        }
}

#define PLL_REF_DIV   0x2
#define VCLK_POST_DIV 0x6
#define VCLK0_FB_DIV  0x7

static void pll_write(mach64_t *mach64, uint32_t addr, uint8_t val)
{
        int c;

        switch (addr & 3)
        {
                case 0: /*Clock sel*/
                break;
                case 1: /*Addr*/
                mach64->pll_addr = (val >> 2) & 0xf;
                break;
                case 2: /*Data*/
                mach64->pll_regs[mach64->pll_addr] = val;
                mach64_log("pll_write %02x,%02x\n", mach64->pll_addr, val);

                for (c = 0; c < 4; c++)
                {
                        double m = (double)mach64->pll_regs[PLL_REF_DIV];
                        double n = (double)mach64->pll_regs[VCLK0_FB_DIV+c];
                        double r = 14318184.0;
                        double p = (double)(1 << ((mach64->pll_regs[VCLK_POST_DIV] >> (c*2)) & 3));

                        mach64_log("PLLfreq %i = %g  %g m=%02x n=%02x p=%02x\n", c, (2.0 * r * n) / (m * p), p, mach64->pll_regs[PLL_REF_DIV], mach64->pll_regs[VCLK0_FB_DIV+c], mach64->pll_regs[VCLK_POST_DIV]);
                        mach64->pll_freq[c] = (2.0 * r * n) / (m * p);
                        mach64_log(" %g\n", mach64->pll_freq[c]);
                }
                break;
        }
}

#define OVERLAY_EN (1 << 30)
static void mach64_vblank_start(svga_t *svga)
{
        mach64_t *mach64 = (mach64_t *)svga->p;
        int overlay_cmp_mix = (mach64->overlay_key_cntl >> 8) & 0xf;

        mach64->crtc_int_cntl |= 4;
        mach64_update_irqs(mach64);

        svga->overlay.x = (mach64->overlay_y_x_start >> 16) & 0x7ff;
        svga->overlay.y = mach64->overlay_y_x_start & 0x7ff;

        svga->overlay.xsize = ((mach64->overlay_y_x_end >> 16) & 0x7ff) - svga->overlay.x;
        svga->overlay.ysize = (mach64->overlay_y_x_end & 0x7ff) - svga->overlay.y;

        svga->overlay.addr = mach64->buf_offset[0] & 0x3ffff8;
        svga->overlay.pitch = mach64->buf_pitch[0] & 0xfff;

        svga->overlay.ena = (mach64->overlay_scale_cntl & OVERLAY_EN) && (overlay_cmp_mix != 1);

        mach64->overlay_v_acc = 0;
        mach64->scaler_update = 1;
}

uint8_t mach64_ext_readb(uint32_t addr, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        uint8_t ret = 0xff;
        if (!(addr & 0x400))
        {
                mach64_log("nmach64_ext_readb: addr=%04x\n", addr);
                switch (addr & 0x3ff)
                {
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        READ8(addr, mach64->overlay_y_x_start);
                        break;
                        case 0x04: case 0x05: case 0x06: case 0x07:
                        READ8(addr, mach64->overlay_y_x_end);
                        break;
                        case 0x08: case 0x09: case 0x0a: case 0x0b:
                        READ8(addr, mach64->overlay_video_key_clr);
                        break;
                        case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                        READ8(addr, mach64->overlay_video_key_msk);
                        break;
                        case 0x10: case 0x11: case 0x12: case 0x13:
                        READ8(addr, mach64->overlay_graphics_key_clr);
                        break;
                        case 0x14: case 0x15: case 0x16: case 0x17:
                        READ8(addr, mach64->overlay_graphics_key_msk);
                        break;
                        case 0x18: case 0x19: case 0x1a: case 0x1b:
                        READ8(addr, mach64->overlay_key_cntl);
                        break;

                        case 0x20: case 0x21: case 0x22: case 0x23:
                        READ8(addr, mach64->overlay_scale_inc);
                        break;
                        case 0x24: case 0x25: case 0x26: case 0x27:
                        READ8(addr, mach64->overlay_scale_cntl);
                        break;
                        case 0x28: case 0x29: case 0x2a: case 0x2b:
                        READ8(addr, mach64->scaler_height_width);
                        break;

                        case 0x4a:
                        ret = mach64->scaler_format;
                        break;

                        default:
                        ret = 0xff;
                        break;
                }
        }
        else switch (addr & 0x3ff)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                READ8(addr, mach64->crtc_h_total_disp);
                break;
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                READ8(addr, mach64->crtc_v_total_disp);
                break;
                case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                READ8(addr, mach64->crtc_v_sync_strt_wid);
                break;

                case 0x12: case 0x13:
                READ8(addr - 2, mach64->svga.vc);
                break;

                case 0x14: case 0x15: case 0x16: case 0x17:
                READ8(addr, mach64->crtc_off_pitch);
                break;

                case 0x18:
                ret = mach64->crtc_int_cntl & ~1;
                if (mach64->svga.cgastat & 8)
                        ret |= 1;
                break;

                case 0x1c: case 0x1d: case 0x1e: case 0x1f:
                READ8(addr, mach64->crtc_gen_cntl);
                break;

                case 0x40: case 0x41: case 0x42: case 0x43:
                READ8(addr, mach64->ovr_clr);
                break;
                case 0x44: case 0x45: case 0x46: case 0x47:
                READ8(addr, mach64->ovr_wid_left_right);
                break;
                case 0x48: case 0x49: case 0x4a: case 0x4b:
                READ8(addr, mach64->ovr_wid_top_bottom);
                break;

                case 0x60: case 0x61: case 0x62: case 0x63:
                READ8(addr, mach64->cur_clr0);
                break;
                case 0x64: case 0x65: case 0x66: case 0x67:
                READ8(addr, mach64->cur_clr1);
                break;
                case 0x68: case 0x69: case 0x6a: case 0x6b:
                READ8(addr, mach64->cur_offset);
                break;
                case 0x6c: case 0x6d: case 0x6e: case 0x6f:
                READ8(addr, mach64->cur_horz_vert_posn);
                break;
                case 0x70: case 0x71: case 0x72: case 0x73:
                READ8(addr, mach64->cur_horz_vert_off);
                break;

                case 0x79:
                ret = 0x30;
                break;

                case 0x80: case 0x81: case 0x82: case 0x83:
                READ8(addr, mach64->scratch_reg0);
                break;
                case 0x84: case 0x85: case 0x86: case 0x87:
                READ8(addr, mach64->scratch_reg1);
                break;

                case 0x90: case 0x91: case 0x92: case 0x93:
                READ8(addr, mach64->clock_cntl);
                break;

                case 0xb0: case 0xb1: case 0xb2: case 0xb3:
                READ8(addr, mach64->mem_cntl);
                break;

                case 0xc0: case 0xc1: case 0xc2: case 0xc3:
                if (mach64->type == MACH64_GX)
                        ret = ati68860_ramdac_in((addr & 3) | ((mach64->dac_cntl & 3) << 2), mach64->svga.ramdac, &mach64->svga);
                else
                        ret = ati68860_ramdac_in(addr & 3, mach64->svga.ramdac, &mach64->svga);
                break;
                case 0xc4: case 0xc5: case 0xc6:
                READ8(addr, mach64->dac_cntl);
                break;

		case 0xc7:
                READ8(addr, mach64->dac_cntl);
                if (mach64->type == MACH64_VT2) {
                        ret &= 0xf9;
                        if (i2c_gpio_get_scl(mach64->i2c))
                                ret |= 0x04;
                        if (i2c_gpio_get_sda(mach64->i2c))
                                ret |= 0x02;
                }
                break;

                case 0xd0: case 0xd1: case 0xd2: case 0xd3:
                READ8(addr, mach64->gen_test_cntl);
                break;

                case 0xdc: case 0xdd: case 0xde: case 0xdf:
                if (mach64->type == MACH64_GX)
                        mach64->config_cntl = (mach64->config_cntl & ~0x3ff0) | ((mach64->linear_base >> 22) << 4);
                else
                        mach64->config_cntl = (mach64->config_cntl & ~0x3ff0) | ((mach64->linear_base >> 24) << 4);
                READ8(addr, mach64->config_cntl);
                break;
                case 0xe0: case 0xe1: case 0xe2: case 0xe3:
                READ8(addr, mach64->config_chip_id);
                break;
                case 0xe4: case 0xe5: case 0xe6: case 0xe7:
                READ8(addr, mach64->config_stat0);
                break;

                case 0x100: case 0x101: case 0x102: case 0x103:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_off_pitch);
                break;
                case 0x104: case 0x105:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_y_x);
                break;
                case 0x108: case 0x109: case 0x11c: case 0x11d:
                mach64_wait_fifo_idle(mach64);
                READ8(addr + 2, mach64->dst_y_x);
                break;
                case 0x10c: case 0x10d: case 0x10e: case 0x10f:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_y_x);
                break;
                case 0x110: case 0x111:
                addr += 2;
		/*FALLTHROUGH*/
                case 0x114: case 0x115:
                case 0x118: case 0x119: case 0x11a: case 0x11b:
                case 0x11e: case 0x11f:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_height_width);
                break;

                case 0x120: case 0x121: case 0x122: case 0x123:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_bres_lnth);
                break;
                case 0x124: case 0x125: case 0x126: case 0x127:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_bres_err);
                break;
                case 0x128: case 0x129: case 0x12a: case 0x12b:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_bres_inc);
                break;
                case 0x12c: case 0x12d: case 0x12e: case 0x12f:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_bres_dec);
                break;

                case 0x130: case 0x131: case 0x132: case 0x133:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_cntl);
                break;

                case 0x180: case 0x181: case 0x182: case 0x183:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_off_pitch);
                break;
                case 0x184: case 0x185:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_y_x);
                break;
                case 0x188: case 0x189:
                mach64_wait_fifo_idle(mach64);
                READ8(addr + 2, mach64->src_y_x);
                break;
                case 0x18c: case 0x18d: case 0x18e: case 0x18f:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_y_x);
                break;
                case 0x190: case 0x191:
                mach64_wait_fifo_idle(mach64);
                READ8(addr + 2, mach64->src_height1_width1);
                break;
                case 0x194: case 0x195:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_height1_width1);
                break;
                case 0x198: case 0x199: case 0x19a: case 0x19b:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_height1_width1);
                break;
                case 0x19c: case 0x19d:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_y_x_start);
                break;
                case 0x1a0: case 0x1a1:
                mach64_wait_fifo_idle(mach64);
                READ8(addr + 2, mach64->src_y_x_start);
                break;
                case 0x1a4: case 0x1a5: case 0x1a6: case 0x1a7:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_y_x_start);
                break;
                case 0x1a8: case 0x1a9:
                mach64_wait_fifo_idle(mach64);
                READ8(addr + 2, mach64->src_height2_width2);
                break;
                case 0x1ac: case 0x1ad:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_height2_width2);
                break;
                case 0x1b0: case 0x1b1: case 0x1b2: case 0x1b3:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_height2_width2);
                break;

                case 0x1b4: case 0x1b5: case 0x1b6: case 0x1b7:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->src_cntl);
                break;

                case 0x240: case 0x241: case 0x242: case 0x243:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->host_cntl);
                break;

                case 0x280: case 0x281: case 0x282: case 0x283:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->pat_reg0);
                break;
                case 0x284: case 0x285: case 0x286: case 0x287:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->pat_reg1);
                break;

				case 0x288: case 0x289: case 0x28a: case 0x28b:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->pat_cntl);
                break;

                case 0x2a0: case 0x2a1: case 0x2a8: case 0x2a9:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->sc_left_right);
                break;
                case 0x2a4: case 0x2a5:
                addr += 2;
		/*FALLTHROUGH*/
                case 0x2aa: case 0x2ab:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->sc_left_right);
                break;

                case 0x2ac: case 0x2ad: case 0x2b4: case 0x2b5:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->sc_top_bottom);
                break;
                case 0x2b0: case 0x2b1:
                addr += 2;
		/*FALLTHROUGH*/
                case 0x2b6: case 0x2b7:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->sc_top_bottom);
                break;

                case 0x2c0: case 0x2c1: case 0x2c2: case 0x2c3:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dp_bkgd_clr);
                break;
                case 0x2c4: case 0x2c5: case 0x2c6: case 0x2c7:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dp_frgd_clr);
                break;

                case 0x2c8: case 0x2c9: case 0x2ca: case 0x2cb:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->write_mask);
                break;

                case 0x2cc: case 0x2cd: case 0x2ce: case 0x2cf:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->chain_mask);
                break;

                case 0x2d0: case 0x2d1: case 0x2d2: case 0x2d3:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dp_pix_width);
                break;
                case 0x2d4: case 0x2d5: case 0x2d6: case 0x2d7:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dp_mix);
                break;
                case 0x2d8: case 0x2d9: case 0x2da: case 0x2db:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dp_src);
                break;

                case 0x300: case 0x301: case 0x302: case 0x303:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->clr_cmp_clr);
                break;
                case 0x304: case 0x305: case 0x306: case 0x307:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->clr_cmp_mask);
                break;
                case 0x308: case 0x309: case 0x30a: case 0x30b:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->clr_cmp_cntl);
                break;

                case 0x310:
                case 0x311:
                if (!FIFO_EMPTY)
                        wake_fifo_thread(mach64);
                ret = 0;
                if (FIFO_FULL)
                        ret = 0xff;
                break;

                case 0x320: case 0x321: case 0x322: case 0x323:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->context_mask);
                break;

                case 0x330: case 0x331:
                mach64_wait_fifo_idle(mach64);
                READ8(addr, mach64->dst_cntl);
                break;
                case 0x332:
                mach64_wait_fifo_idle(mach64);
                READ8(addr - 2, mach64->src_cntl);
                break;
                case 0x333:
                mach64_wait_fifo_idle(mach64);
                READ8(addr - 3, mach64->pat_cntl);
                break;

                case 0x338:
                ret = FIFO_EMPTY ? 0 : 1;
                break;

                default:
                ret = 0;
                break;
        }
        if ((addr & 0x3fc) != 0x018)  mach64_log("mach64_ext_readb : addr %08X ret %02X\n", addr, ret);
        return ret;
}
uint16_t mach64_ext_readw(uint32_t addr, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        uint16_t ret;
        if (!(addr & 0x400))
        {
                mach64_log("nmach64_ext_readw: addr=%04x\n", addr);
                ret = 0xffff;
        }
        else switch (addr & 0x3ff)
        {
                case 0xb4: case 0xb6:
                ret = (mach64->bank_w[(addr & 2) >> 1] >> 15);
                break;
                case 0xb8: case 0xba:
                ret = (mach64->bank_r[(addr & 2) >> 1] >> 15);
                break;

                default:
                ret = mach64_ext_readb(addr, p);
                ret |= mach64_ext_readb(addr + 1, p) << 8;
                break;
        }
        if ((addr & 0x3fc) != 0x018)  mach64_log("mach64_ext_readw : addr %08X ret %04X\n", addr, ret);
        return ret;
}
uint32_t mach64_ext_readl(uint32_t addr, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        uint32_t ret;
        if (!(addr & 0x400))
        {
                mach64_log("nmach64_ext_readl: addr=%04x\n", addr);
                ret = 0xffffffff;
        }
        else switch (addr & 0x3ff)
        {
                case 0x18:
                ret = mach64->crtc_int_cntl & ~1;
                if (mach64->svga.cgastat & 8)
                        ret |= 1;
                break;

                case 0xb4:
                ret = (mach64->bank_w[0] >> 15) | ((mach64->bank_w[1] >> 15) << 16);
                break;
                case 0xb8:
                ret = (mach64->bank_r[0] >> 15) | ((mach64->bank_r[1] >> 15) << 16);
                break;

                default:
                ret = mach64_ext_readw(addr, p);
                ret |= mach64_ext_readw(addr + 2, p) << 16;
                break;
        }
        if ((addr & 0x3fc) != 0x018)  mach64_log("mach64_ext_readl : addr %08X ret %08X\n", addr, ret);
        return ret;
}

void mach64_ext_writeb(uint32_t addr, uint8_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        svga_t *svga = &mach64->svga;

        mach64_log("mach64_ext_writeb : addr %08X val %02X\n", addr, val);

        if (!(addr & 0x400))
        {
                switch (addr & 0x3ff)
                {
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        WRITE8(addr, mach64->overlay_y_x_start, val);
                        break;
                        case 0x04: case 0x05: case 0x06: case 0x07:
                        WRITE8(addr, mach64->overlay_y_x_end, val);
                        break;
                        case 0x08: case 0x09: case 0x0a: case 0x0b:
                        WRITE8(addr, mach64->overlay_video_key_clr, val);
                        break;
                        case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                        WRITE8(addr, mach64->overlay_video_key_msk, val);
                        break;
                        case 0x10: case 0x11: case 0x12: case 0x13:
                        WRITE8(addr, mach64->overlay_graphics_key_clr, val);
                        break;
                        case 0x14: case 0x15: case 0x16: case 0x17:
                        WRITE8(addr, mach64->overlay_graphics_key_msk, val);
                        break;
                        case 0x18: case 0x19: case 0x1a: case 0x1b:
                        WRITE8(addr, mach64->overlay_key_cntl, val);
                        break;

                        case 0x20: case 0x21: case 0x22: case 0x23:
                        WRITE8(addr, mach64->overlay_scale_inc, val);
                        break;
                        case 0x24: case 0x25: case 0x26: case 0x27:
                        WRITE8(addr, mach64->overlay_scale_cntl, val);
                        break;
                        case 0x28: case 0x29: case 0x2a: case 0x2b:
                        WRITE8(addr, mach64->scaler_height_width, val);
                        break;

                        case 0x4a:
                        mach64->scaler_format = val & 0xf;
                        break;

                        case 0x80: case 0x81: case 0x82: case 0x83:
                        WRITE8(addr, mach64->buf_offset[0], val);
                        break;

                        case 0x8c: case 0x8d: case 0x8e: case 0x8f:
                        WRITE8(addr, mach64->buf_pitch[0], val);
                        break;

                        case 0x98: case 0x99: case 0x9a: case 0x9b:
                        WRITE8(addr, mach64->buf_offset[1], val);
                        break;

                        case 0xa4: case 0xa5: case 0xa6: case 0xa7:
                        WRITE8(addr, mach64->buf_pitch[1], val);
                        break;
                }

                mach64_log("nmach64_ext_writeb: addr=%04x val=%02x\n", addr, val);
        }
        else if (addr & 0x300)
        {
                mach64_queue(mach64, addr & 0x3ff, val, FIFO_WRITE_BYTE);
        }
        else switch (addr & 0x3ff)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                WRITE8(addr, mach64->crtc_h_total_disp, val);
                svga_recalctimings(&mach64->svga);
                break;
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                WRITE8(addr, mach64->crtc_v_total_disp, val);
                svga_recalctimings(&mach64->svga);
                break;
                case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                WRITE8(addr, mach64->crtc_v_sync_strt_wid, val);
                svga_recalctimings(&mach64->svga);
                break;

                case 0x14: case 0x15: case 0x16: case 0x17:
                WRITE8(addr, mach64->crtc_off_pitch, val);
                svga_recalctimings(&mach64->svga);
                svga->fullchange = changeframecount;
                break;

                case 0x18:
                mach64->crtc_int_cntl = (mach64->crtc_int_cntl & 0x75) | (val & ~0x75);
                if (val & 4)
                        mach64->crtc_int_cntl &= ~4;
                mach64_update_irqs(mach64);
                break;

                case 0x1c: case 0x1d: case 0x1e: case 0x1f:
                WRITE8(addr, mach64->crtc_gen_cntl, val);
                if (((mach64->crtc_gen_cntl >> 24) & 3) == 3)
                        svga->fb_only = 1;
                else
                        svga->fb_only = 0;
                svga->dpms = !!(mach64->crtc_gen_cntl & 0x0c);
                svga_recalctimings(&mach64->svga);
                break;

                case 0x40: case 0x41: case 0x42: case 0x43:
                WRITE8(addr, mach64->ovr_clr, val);
                break;
                case 0x44: case 0x45: case 0x46: case 0x47:
                WRITE8(addr, mach64->ovr_wid_left_right, val);
                break;
                case 0x48: case 0x49: case 0x4a: case 0x4b:
                WRITE8(addr, mach64->ovr_wid_top_bottom, val);
                break;

                case 0x60: case 0x61: case 0x62: case 0x63:
                WRITE8(addr, mach64->cur_clr0, val);
                if (mach64->type == MACH64_VT2)
                        ati68860_ramdac_set_pallook(mach64->svga.ramdac, 0, makecol32((mach64->cur_clr0 >> 24) & 0xff, (mach64->cur_clr0 >> 16) & 0xff, (mach64->cur_clr0 >> 8) & 0xff));
                break;
                case 0x64: case 0x65: case 0x66: case 0x67:
                WRITE8(addr, mach64->cur_clr1, val);
                if (mach64->type == MACH64_VT2)
                        ati68860_ramdac_set_pallook(mach64->svga.ramdac, 1, makecol32((mach64->cur_clr1 >> 24) & 0xff, (mach64->cur_clr1 >> 16) & 0xff, (mach64->cur_clr1 >> 8) & 0xff));
                break;
                case 0x68: case 0x69: case 0x6a: case 0x6b:
                WRITE8(addr, mach64->cur_offset, val);
                svga->dac_hwcursor.addr = (mach64->cur_offset & 0xfffff) * 8;
                break;
                case 0x6c: case 0x6d: case 0x6e: case 0x6f:
                WRITE8(addr, mach64->cur_horz_vert_posn, val);
                svga->dac_hwcursor.x = mach64->cur_horz_vert_posn & 0x7ff;
                svga->dac_hwcursor.y = (mach64->cur_horz_vert_posn >> 16) & 0x7ff;
                break;
                case 0x70: case 0x71: case 0x72: case 0x73:
                WRITE8(addr, mach64->cur_horz_vert_off, val);
                svga->dac_hwcursor.xoff = mach64->cur_horz_vert_off & 0x3f;
                svga->dac_hwcursor.yoff = (mach64->cur_horz_vert_off >> 16) & 0x3f;
                break;

                case 0x80: case 0x81: case 0x82: case 0x83:
                WRITE8(addr, mach64->scratch_reg0, val);
                break;
                case 0x84: case 0x85: case 0x86: case 0x87:
                WRITE8(addr, mach64->scratch_reg1, val);
                break;

                case 0x90: case 0x91: case 0x92: case 0x93:
                WRITE8(addr, mach64->clock_cntl, val);
                if (mach64->type == MACH64_GX)
                        ics2595_write(svga->clock_gen, val & 0x40, val & 0xf);
                else
                {
                        pll_write(mach64, addr, val);
                        ics2595_setclock(svga->clock_gen, mach64->pll_freq[mach64->clock_cntl & 3]);
                }
                svga_recalctimings(&mach64->svga);
                break;

                case 0xb0: case 0xb1: case 0xb2: case 0xb3:
                WRITE8(addr, mach64->mem_cntl, val);
                break;

                case 0xb4:
                mach64->bank_w[0] = val * 32768;
                mach64_log("mach64 : write bank A0000-A7FFF set to %08X\n", mach64->bank_w[0]);
                break;
                case 0xb5: case 0xb6:
                mach64->bank_w[1] = val * 32768;
                mach64_log("mach64 : write bank A8000-AFFFF set to %08X\n", mach64->bank_w[1]);
                break;
                case 0xb8:
                mach64->bank_r[0] = val * 32768;
                mach64_log("mach64 :  read bank A0000-A7FFF set to %08X\n", mach64->bank_r[0]);
                break;
                case 0xb9: case 0xba:
                mach64->bank_r[1] = val * 32768;
                mach64_log("mach64 :  read bank A8000-AFFFF set to %08X\n", mach64->bank_r[1]);
                break;

                case 0xc0: case 0xc1: case 0xc2: case 0xc3:
                if (mach64->type == MACH64_GX)
                        ati68860_ramdac_out((addr & 3) | ((mach64->dac_cntl & 3) << 2), val, mach64->svga.ramdac, &mach64->svga);
                else
                        ati68860_ramdac_out(addr & 3, val, mach64->svga.ramdac, &mach64->svga);
                break;
                case 0xc4: case 0xc5: case 0xc6: case 0xc7:
                WRITE8(addr, mach64->dac_cntl, val);
                svga_set_ramdac_type(svga, (mach64->dac_cntl & 0x100) ? RAMDAC_8BIT : RAMDAC_6BIT);
                ati68860_set_ramdac_type(mach64->svga.ramdac, (mach64->dac_cntl & 0x100) ? RAMDAC_8BIT : RAMDAC_6BIT);
                i2c_gpio_set(mach64->i2c, !(mach64->dac_cntl & 0x20000000) || (mach64->dac_cntl & 0x04000000), !(mach64->dac_cntl & 0x10000000) || (mach64->dac_cntl & 0x02000000));
                break;

                case 0xd0: case 0xd1: case 0xd2: case 0xd3:
                WRITE8(addr, mach64->gen_test_cntl, val);
                ati_eeprom_write(&mach64->eeprom, mach64->gen_test_cntl & 0x10, mach64->gen_test_cntl & 2, mach64->gen_test_cntl & 1);
                mach64->gen_test_cntl = (mach64->gen_test_cntl & ~8) | (ati_eeprom_read(&mach64->eeprom) ? 8 : 0);
                svga->dac_hwcursor.ena = mach64->gen_test_cntl & 0x80;
                break;

                case 0xdc: case 0xdd: case 0xde: case 0xdf:
                WRITE8(addr, mach64->config_cntl, val);
                mach64_updatemapping(mach64);
                break;

                case 0xe4: case 0xe5: case 0xe6: case 0xe7:
                if (mach64->type != MACH64_GX)
                        WRITE8(addr, mach64->config_stat0, val);
                break;
        }
}
void mach64_ext_writew(uint32_t addr, uint16_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        mach64_log("mach64_ext_writew : addr %08X val %04X\n", addr, val);
        if (!(addr & 0x400))
        {
                mach64_log("mach64_ext_writew: addr=%04x val=%04x\n", addr, val);

                mach64_ext_writeb(addr, val, p);
                mach64_ext_writeb(addr + 1, val >> 8, p);
        }
        else if (addr & 0x300)
        {
                mach64_queue(mach64, addr & 0x3fe, val, FIFO_WRITE_WORD);
        }
        else switch (addr & 0x3fe)
        {
                default:
                mach64_ext_writeb(addr, val, p);
                mach64_ext_writeb(addr + 1, val >> 8, p);
                break;
        }
}
void mach64_ext_writel(uint32_t addr, uint32_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        if ((addr & 0x3c0) != 0x200)
                mach64_log("mach64_ext_writel : addr %08X val %08X\n", addr, val);
        if (!(addr & 0x400))
        {
                mach64_log("mach64_ext_writel: addr=%04x val=%08x\n", addr, val);

                mach64_ext_writew(addr, val, p);
                mach64_ext_writew(addr + 2, val >> 16, p);
        }
        else if (addr & 0x300)
        {
                mach64_queue(mach64, addr & 0x3fc, val, FIFO_WRITE_DWORD);
        }
        else switch (addr & 0x3fc)
        {
                default:
                mach64_ext_writew(addr, val, p);
                mach64_ext_writew(addr + 2, val >> 16, p);
                break;
        }
}

uint8_t mach64_ext_inb(uint16_t port, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        uint8_t ret;

        switch (port)
        {
                case 0x02ec: case 0x02ed: case 0x02ee: case 0x02ef:
                case 0x7eec: case 0x7eed: case 0x7eee: case 0x7eef:
                ret = mach64_ext_readb(0x400 | 0x00 | (port & 3), p);
                break;
                case 0x0aec: case 0x0aed: case 0x0aee: case 0x0aef:
                ret = mach64_ext_readb(0x400 | 0x08 | (port & 3), p);
                break;
                case 0x0eec: case 0x0eed: case 0x0eee: case 0x0eef:
                ret = mach64_ext_readb(0x400 | 0x0c | (port & 3), p);
                break;

                case 0x12ec: case 0x12ed: case 0x12ee: case 0x12ef:
                ret = mach64_ext_readb(0x400 | 0x10 | (port & 3), p);
                break;

                case 0x16ec: case 0x16ed: case 0x16ee: case 0x16ef:
                ret = mach64_ext_readb(0x400 | 0x14 | (port & 3), p);
                break;

                case 0x1aec:
                ret = mach64_ext_readb(0x400 | 0x18, p);
                break;

                case 0x1eec: case 0x1eed: case 0x1eee: case 0x1eef:
                ret = mach64_ext_readb(0x400 | 0x1c | (port & 3), p);
                break;

                case 0x22ec: case 0x22ed: case 0x22ee: case 0x22ef:
                ret = mach64_ext_readb(0x400 | 0x40 | (port & 3), p);
                break;
                case 0x26ec: case 0x26ed: case 0x26ee: case 0x26ef:
                ret = mach64_ext_readb(0x400 | 0x44 | (port & 3), p);
                break;
                case 0x2aec: case 0x2aed: case 0x2aee: case 0x2aef:
                ret = mach64_ext_readb(0x400 | 0x48 | (port & 3), p);
                break;
                case 0x2eec: case 0x2eed: case 0x2eee: case 0x2eef:
                ret = mach64_ext_readb(0x400 | 0x60 | (port & 3), p);
                break;

                case 0x32ec: case 0x32ed: case 0x32ee: case 0x32ef:
                ret = mach64_ext_readb(0x400 | 0x64 | (port & 3), p);
                break;
                case 0x36ec: case 0x36ed: case 0x36ee: case 0x36ef:
                ret = mach64_ext_readb(0x400 | 0x68 | (port & 3), p);
                break;
                case 0x3aec: case 0x3aed: case 0x3aee: case 0x3aef:
                ret = mach64_ext_readb(0x400 | 0x6c | (port & 3), p);
                break;
                case 0x3eec: case 0x3eed: case 0x3eee: case 0x3eef:
                ret = mach64_ext_readb(0x400 | 0x70 | (port & 3), p);
                break;

                case 0x42ec: case 0x42ed: case 0x42ee: case 0x42ef:
                ret = mach64_ext_readb(0x400 | 0x80 | (port & 3), p);
                break;
                case 0x46ec: case 0x46ed: case 0x46ee: case 0x46ef:
                ret = mach64_ext_readb(0x400 | 0x84 | (port & 3), p);
                break;
                case 0x4aec: case 0x4aed: case 0x4aee: case 0x4aef:
                ret = mach64_ext_readb(0x400 | 0x90 | (port & 3), p);
                break;

                case 0x52ec: case 0x52ed: case 0x52ee: case 0x52ef:
                ret = mach64_ext_readb(0x400 | 0xb0 | (port & 3), p);
                break;

                case 0x56ec:
                ret = mach64_ext_readb(0x400 | 0xb4, p);
                break;
                case 0x56ed: case 0x56ee:
                ret = mach64_ext_readb(0x400 | 0xb5, p);
                break;
                case 0x5aec:
                ret = mach64_ext_readb(0x400 | 0xb8, p);
                break;
                case 0x5aed: case 0x5aee:
                ret = mach64_ext_readb(0x400 | 0xb9, p);
                break;

                case 0x5eec: case 0x5eed: case 0x5eee: case 0x5eef:
                if (mach64->type == MACH64_GX)
                        ret = ati68860_ramdac_in((port & 3) | ((mach64->dac_cntl & 3) << 2), mach64->svga.ramdac, &mach64->svga);
                else
                        ret = ati68860_ramdac_in(port & 3, mach64->svga.ramdac, &mach64->svga);
                break;

                case 0x62ec: case 0x62ed: case 0x62ee: case 0x62ef:
                ret = mach64_ext_readb(0x400 | 0xc4 | (port & 3), p);
                break;

                case 0x66ec: case 0x66ed: case 0x66ee: case 0x66ef:
                ret = mach64_ext_readb(0x400 | 0xd0 | (port & 3), p);
                break;

                case 0x6aec: case 0x6aed: case 0x6aee: case 0x6aef:
                mach64->config_cntl = (mach64->config_cntl & ~0x3ff0) | ((mach64->linear_base >> 22) << 4);
                READ8(port, mach64->config_cntl);
                break;

                case 0x6eec: case 0x6eed: case 0x6eee: case 0x6eef:
                ret = mach64_ext_readb(0x400 | 0xe0 | (port & 3), p);
                break;

                case 0x72ec: case 0x72ed: case 0x72ee: case 0x72ef:
                ret = mach64_ext_readb(0x400 | 0xe4 | (port & 3), p);
                break;

                default:
                ret = 0;
                break;
        }
        mach64_log("mach64_ext_inb : port %04X ret %02X\n", port, ret);
        return ret;
}
uint16_t mach64_ext_inw(uint16_t port, void *p)
{
        uint16_t ret;
        switch (port)
        {
                default:
                ret = mach64_ext_inb(port, p);
                ret |= (mach64_ext_inb(port + 1, p) << 8);
                break;
        }
        mach64_log("mach64_ext_inw : port %04X ret %04X\n", port, ret);
        return ret;
}
uint32_t mach64_ext_inl(uint16_t port, void *p)
{
        uint32_t ret;
        switch (port)
        {
                case 0x56ec:
                ret = mach64_ext_readl(0x400 | 0xb4, p);
                break;
                case 0x5aec:
                ret = mach64_ext_readl(0x400 | 0xb8, p);
                break;

                default:
                ret = mach64_ext_inw(port, p);
                ret |= (mach64_ext_inw(port + 2, p) << 16);
                break;
        }
        mach64_log("mach64_ext_inl : port %04X ret %08X\n", port, ret);
        return ret;
}

void mach64_ext_outb(uint16_t port, uint8_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        mach64_log("mach64_ext_outb : port %04X val %02X\n", port, val);
        switch (port)
        {
                case 0x02ec: case 0x02ed: case 0x02ee: case 0x02ef:
                case 0x7eec: case 0x7eed: case 0x7eee: case 0x7eef:
                mach64_ext_writeb(0x400 | 0x00 | (port & 3), val, p);
                break;
                case 0x0aec: case 0x0aed: case 0x0aee: case 0x0aef:
                mach64_ext_writeb(0x400 | 0x08 | (port & 3), val, p);
                break;
                case 0x0eec: case 0x0eed: case 0x0eee: case 0x0eef:
                mach64_ext_writeb(0x400 | 0x0c | (port & 3), val, p);
                break;

                case 0x16ec: case 0x16ed: case 0x16ee: case 0x16ef:
                mach64_ext_writeb(0x400 | 0x14 | (port & 3), val, p);
                break;

                case 0x1aec:
                mach64_ext_writeb(0x400 | 0x18, val, p);
                break;

                case 0x1eec: case 0x1eed: case 0x1eee: case 0x1eef:
                mach64_ext_writeb(0x400 | 0x1c | (port & 3), val, p);
                break;

                case 0x22ec: case 0x22ed: case 0x22ee: case 0x22ef:
                mach64_ext_writeb(0x400 | 0x40 | (port & 3), val, p);
                break;
                case 0x26ec: case 0x26ed: case 0x26ee: case 0x26ef:
                mach64_ext_writeb(0x400 | 0x44 | (port & 3), val, p);
                break;
                case 0x2aec: case 0x2aed: case 0x2aee: case 0x2aef:
                mach64_ext_writeb(0x400 | 0x48 | (port & 3), val, p);
                break;
                case 0x2eec: case 0x2eed: case 0x2eee: case 0x2eef:
                mach64_ext_writeb(0x400 | 0x60 | (port & 3), val, p);
                break;

                case 0x32ec: case 0x32ed: case 0x32ee: case 0x32ef:
                mach64_ext_writeb(0x400 | 0x64 | (port & 3), val, p);
                break;
                case 0x36ec: case 0x36ed: case 0x36ee: case 0x36ef:
                mach64_ext_writeb(0x400 | 0x68 | (port & 3), val, p);
                break;
                case 0x3aec: case 0x3aed: case 0x3aee: case 0x3aef:
                mach64_ext_writeb(0x400 | 0x6c | (port & 3), val, p);
                break;
                case 0x3eec: case 0x3eed: case 0x3eee: case 0x3eef:
                mach64_ext_writeb(0x400 | 0x70 | (port & 3), val, p);
                break;

                case 0x42ec: case 0x42ed: case 0x42ee: case 0x42ef:
                mach64_ext_writeb(0x400 | 0x80 | (port & 3), val, p);
                break;
                case 0x46ec: case 0x46ed: case 0x46ee: case 0x46ef:
                mach64_ext_writeb(0x400 | 0x84 | (port & 3), val, p);
                break;
                case 0x4aec: case 0x4aed: case 0x4aee: case 0x4aef:
                mach64_ext_writeb(0x400 | 0x90 | (port & 3), val, p);
                break;

                case 0x52ec: case 0x52ed: case 0x52ee: case 0x52ef:
                mach64_ext_writeb(0x400 | 0xb0 | (port & 3), val, p);
                break;

                case 0x56ec:
                mach64_ext_writeb(0x400 | 0xb4, val, p);
                break;
                case 0x56ed: case 0x56ee:
                mach64_ext_writeb(0x400 | 0xb5, val, p);
                break;
                case 0x5aec:
                mach64_ext_writeb(0x400 | 0xb8, val, p);
                break;
                case 0x5aed: case 0x5aee:
                mach64_ext_writeb(0x400 | 0xb9, val, p);
                break;

                case 0x5eec: case 0x5eed: case 0x5eee: case 0x5eef:
                if (mach64->type == MACH64_GX)
                        ati68860_ramdac_out((port & 3) | ((mach64->dac_cntl & 3) << 2), val, mach64->svga.ramdac, &mach64->svga);
                else
                        ati68860_ramdac_out(port & 3, val, mach64->svga.ramdac, &mach64->svga);
                break;

                case 0x62ec: case 0x62ed: case 0x62ee: case 0x62ef:
                mach64_ext_writeb(0x400 | 0xc4 | (port & 3), val, p);
                break;

                case 0x66ec: case 0x66ed: case 0x66ee: case 0x66ef:
                mach64_ext_writeb(0x400 | 0xd0 | (port & 3), val, p);
                break;

                case 0x6aec: case 0x6aed: case 0x6aee: case 0x6aef:
                WRITE8(port, mach64->config_cntl, val);
                mach64_updatemapping(mach64);
                break;
        }
}
void mach64_ext_outw(uint16_t port, uint16_t val, void *p)
{
        mach64_log("mach64_ext_outw : port %04X val %04X\n", port, val);
        switch (port)
        {
                default:
                mach64_ext_outb(port, val, p);
                mach64_ext_outb(port + 1, val >> 8, p);
                break;
        }
}
void mach64_ext_outl(uint16_t port, uint32_t val, void *p)
{
        mach64_log("mach64_ext_outl : port %04X val %08X\n", port, val);
        switch (port)
        {
                default:
                mach64_ext_outw(port, val, p);
                mach64_ext_outw(port + 2, val >> 16, p);
                break;
        }
}

static uint8_t mach64_block_inb(uint16_t port, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        uint8_t ret;

        ret = mach64_ext_readb(0x400 | (port & 0x3ff), mach64);
        mach64_log("mach64_block_inb : port %04X ret %02X\n", port, ret);
        return ret;
}
static uint16_t mach64_block_inw(uint16_t port, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        uint16_t ret;

        ret = mach64_ext_readw(0x400 | (port & 0x3ff), mach64);
        mach64_log("mach64_block_inw : port %04X ret %04X\n", port, ret);
        return ret;
}
static uint32_t mach64_block_inl(uint16_t port, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        uint32_t ret;

        ret = mach64_ext_readl(0x400 | (port & 0x3ff), mach64);
        mach64_log("mach64_block_inl : port %04X ret %08X\n", port, ret);
        return ret;
}

static void mach64_block_outb(uint16_t port, uint8_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        mach64_log("mach64_block_outb : port %04X val %02X\n ", port, val);
        mach64_ext_writeb(0x400 | (port & 0x3ff), val, mach64);
}
static void mach64_block_outw(uint16_t port, uint16_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        mach64_log("mach64_block_outw : port %04X val %04X\n ", port, val);
        mach64_ext_writew(0x400 | (port & 0x3ff), val, mach64);
}
static void mach64_block_outl(uint16_t port, uint32_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        mach64_log("mach64_block_outl : port %04X val %08X\n ", port, val);
        mach64_ext_writel(0x400 | (port & 0x3ff), val, mach64);
}

void mach64_write(uint32_t addr, uint8_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        svga_t *svga = &mach64->svga;
        addr = (addr & 0x7fff) + mach64->bank_w[(addr >> 15) & 1];
        svga_write_linear(addr, val, svga);
}
void mach64_writew(uint32_t addr, uint16_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        svga_t *svga = &mach64->svga;

        addr = (addr & 0x7fff) + mach64->bank_w[(addr >> 15) & 1];
        svga_writew_linear(addr, val, svga);
}
void mach64_writel(uint32_t addr, uint32_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        svga_t *svga = &mach64->svga;

        addr = (addr & 0x7fff) + mach64->bank_w[(addr >> 15) & 1];
        svga_writel_linear(addr, val, svga);
}

uint8_t mach64_read(uint32_t addr, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        svga_t *svga = &mach64->svga;
        uint8_t ret;
        addr = (addr & 0x7fff) + mach64->bank_r[(addr >> 15) & 1];
        ret = svga_read_linear(addr, svga);
        return ret;
}
uint16_t mach64_readw(uint32_t addr, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        svga_t *svga = &mach64->svga;

        addr = (addr & 0x7fff) + mach64->bank_r[(addr >> 15) & 1];
        return svga_readw_linear(addr, svga);
}
uint32_t mach64_readl(uint32_t addr, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;
        svga_t *svga = &mach64->svga;

        addr = (addr & 0x7fff) + mach64->bank_r[(addr >> 15) & 1];
        return svga_readl_linear(addr, svga);
}


#define CLAMP(x) do                                     \
        {                                               \
                if ((x) & ~0xff)                        \
                        x = ((x) < 0) ? 0 : 0xff;       \
        }                               \
        while (0)

#define DECODE_ARGB1555()                                               \
        do                                                              \
        {                                                               \
                for (x = 0; x < mach64->svga.overlay_latch.xsize; x++)  \
                {                                                       \
                        uint16_t dat = ((uint16_t *)src)[x];            \
                                                                        \
                        int b = dat & 0x1f;                             \
                        int g = (dat >> 5) & 0x1f;                      \
                        int r = (dat >> 10) & 0x1f;                     \
                                                                        \
                        b = (b << 3) | (b >> 2);                        \
                        g = (g << 3) | (g >> 2);                        \
                        r = (r << 3) | (r >> 2);                        \
                                                                        \
                        mach64->overlay_dat[x] = (r << 16) | (g << 8) | b; \
                }                                                       \
        } while (0)

#define DECODE_RGB565()                                                 \
        do                                                              \
        {                                                               \
                for (x = 0; x < mach64->svga.overlay_latch.xsize; x++)  \
                {                                                       \
                        uint16_t dat = ((uint16_t *)src)[x];            \
                                                                        \
                        int b = dat & 0x1f;                             \
                        int g = (dat >> 5) & 0x3f;                      \
                        int r = (dat >> 11) & 0x1f;                     \
                                                                        \
                        b = (b << 3) | (b >> 2);                        \
                        g = (g << 2) | (g >> 4);                        \
                        r = (r << 3) | (r >> 2);                        \
                                                                        \
                        mach64->overlay_dat[x] = (r << 16) | (g << 8) | b; \
                }                                                       \
        } while (0)

#define DECODE_ARGB8888()                                               \
        do                                                              \
        {                                                               \
                for (x = 0; x < mach64->svga.overlay_latch.xsize; x++)  \
                {                                                       \
                        int b = src[0];                                 \
                        int g = src[1];                                 \
                        int r = src[2];                                 \
                        src += 4;                                       \
                                                                        \
                        mach64->overlay_dat[x] = (r << 16) | (g << 8) | b; \
                }                                                       \
        } while (0)

#define DECODE_VYUY422()                                                  \
        do                                                              \
        {                                                               \
                for (x = 0; x < mach64->svga.overlay_latch.xsize; x += 2)  \
                {                                                       \
                        uint8_t y1, y2;                                 \
                        int8_t u, v;                                    \
                        int dR, dG, dB;                                 \
                        int r, g, b;                                    \
                                                                        \
                        y1 = src[0];                                    \
                        u = src[1] - 0x80;                              \
                        y2 = src[2];                                    \
                        v = src[3] - 0x80;                              \
                        src += 4;                                       \
                                                                        \
                        dR = (359*v) >> 8;                              \
                        dG = (88*u + 183*v) >> 8;                       \
                        dB = (453*u) >> 8;                              \
                                                                        \
                        r = y1 + dR;                                    \
                        CLAMP(r);                                       \
                        g = y1 - dG;                                    \
                        CLAMP(g);                                       \
                        b = y1 + dB;                                    \
                        CLAMP(b);                                       \
                        mach64->overlay_dat[x] = (r << 16) | (g << 8) | b;          \
                                                                        \
                        r = y2 + dR;                                    \
                        CLAMP(r);                                       \
                        g = y2 - dG;                                    \
                        CLAMP(g);                                       \
                        b = y2 + dB;                                    \
                        CLAMP(b);                                       \
                        mach64->overlay_dat[x+1] = (r << 16) | (g << 8) | b;        \
                }                                                       \
        } while (0)

#define DECODE_YVYU422()                                                  \
        do                                                              \
        {                                                               \
                for (x = 0; x < mach64->svga.overlay_latch.xsize; x += 2)  \
                {                                                       \
                        uint8_t y1, y2;                                 \
                        int8_t u, v;                                    \
                        int dR, dG, dB;                                 \
                        int r, g, b;                                    \
                                                                        \
                        u = src[0] - 0x80;                              \
                        y1 = src[1];                                    \
                        v = src[2] - 0x80;                              \
                        y2 = src[3];                                    \
                        src += 4;                                       \
                                                                        \
                        dR = (359*v) >> 8;                              \
                        dG = (88*u + 183*v) >> 8;                       \
                        dB = (453*u) >> 8;                              \
                                                                        \
                        r = y1 + dR;                                    \
                        CLAMP(r);                                       \
                        g = y1 - dG;                                    \
                        CLAMP(g);                                       \
                        b = y1 + dB;                                    \
                        CLAMP(b);                                       \
                        mach64->overlay_dat[x] = (r << 16) | (g << 8) | b;          \
                                                                        \
                        r = y2 + dR;                                    \
                        CLAMP(r);                                       \
                        g = y2 - dG;                                    \
                        CLAMP(g);                                       \
                        b = y2 + dB;                                    \
                        CLAMP(b);                                       \
                        mach64->overlay_dat[x+1] = (r << 16) | (g << 8) | b;        \
                }                                                       \
        } while (0)

void mach64_overlay_draw(svga_t *svga, int displine)
{
        mach64_t *mach64 = (mach64_t *)svga->p;
        int x;
        int h_acc = 0;
        int h_max = (mach64->scaler_height_width >> 16) & 0x3ff;
        int h_inc = mach64->overlay_scale_inc >> 16;
        int v_max = mach64->scaler_height_width & 0x3ff;
        int v_inc = mach64->overlay_scale_inc & 0xffff;
        uint32_t *p;
        uint8_t *src = &svga->vram[svga->overlay.addr];
        int old_y = mach64->overlay_v_acc;
        int y_diff;
        int video_key_fn = mach64->overlay_key_cntl & 5;
        int graphics_key_fn = (mach64->overlay_key_cntl >> 4) & 5;
        int overlay_cmp_mix = (mach64->overlay_key_cntl >> 8) & 0xf;

        p = &buffer32->line[displine][svga->x_add + mach64->svga.overlay_latch.x];

        if (mach64->scaler_update)
        {
                switch (mach64->scaler_format)
                {
                        case 0x3:
                        DECODE_ARGB1555();
                        break;
                        case 0x4:
                        DECODE_RGB565();
                        break;
                        case 0x6:
                        DECODE_ARGB8888();
                        break;
                        case 0xb:
                        DECODE_VYUY422();
                        break;
                        case 0xc:
                        DECODE_YVYU422();
                        break;

                        default:
                        mach64_log("Unknown Mach64 scaler format %x\n", mach64->scaler_format);
                        /*Fill buffer with something recognisably wrong*/
                        for (x = 0; x < mach64->svga.overlay_latch.xsize; x++)
                                mach64->overlay_dat[x] = 0xff00ff;
                        break;
                }
        }

        if (overlay_cmp_mix == 2)
        {
                for (x = 0; x < mach64->svga.overlay_latch.xsize; x++)
                {
                        int h = h_acc >> 12;

                        p[x] = mach64->overlay_dat[h];

                        h_acc += h_inc;
                        if (h_acc > (h_max << 12))
                                h_acc = (h_max << 12);
                }
        }
        else
        {
                for (x = 0; x < mach64->svga.overlay_latch.xsize; x++)
                {
                        int h = h_acc >> 12;
                        int gr_cmp = 0, vid_cmp = 0;
                        int use_video = 0;

                        switch (video_key_fn)
                        {
                                case 0: vid_cmp = 0; break;
                                case 1: vid_cmp = 1; break;
                                case 4: vid_cmp =  ((mach64->overlay_dat[h] ^ mach64->overlay_video_key_clr) & mach64->overlay_video_key_msk); break;
                                case 5: vid_cmp = !((mach64->overlay_dat[h] ^ mach64->overlay_video_key_clr) & mach64->overlay_video_key_msk); break;
                        }
                        switch (graphics_key_fn)
                        {
                                case 0: gr_cmp = 0; break;
                                case 1: gr_cmp = 1; break;
                                case 4: gr_cmp =  (((p[x]) ^ mach64->overlay_graphics_key_clr) & mach64->overlay_graphics_key_msk & 0xffffff); break;
                                case 5: gr_cmp = !(((p[x]) ^ mach64->overlay_graphics_key_clr) & mach64->overlay_graphics_key_msk & 0xffffff); break;
                        }
                        vid_cmp = vid_cmp ? -1 : 0;
                        gr_cmp = gr_cmp ? -1 : 0;

                        switch (overlay_cmp_mix)
                        {
                                case 0x0: use_video =  gr_cmp;            break;
                                case 0x1: use_video =                  0; break;
                                case 0x2: use_video =                 ~0; break;
                                case 0x3: use_video = ~gr_cmp;            break;
                                case 0x4: use_video =           ~vid_cmp; break;
                                case 0x5: use_video =  gr_cmp ^  vid_cmp; break;
                                case 0x6: use_video = ~gr_cmp ^  vid_cmp; break;
                                case 0x7: use_video =            vid_cmp; break;
                                case 0x8: use_video = ~gr_cmp | ~vid_cmp; break;
                                case 0x9: use_video =  gr_cmp | ~vid_cmp; break;
                                case 0xa: use_video = ~gr_cmp |  vid_cmp; break;
                                case 0xb: use_video =  gr_cmp |  vid_cmp; break;
                                case 0xc: use_video =  gr_cmp &  vid_cmp; break;
                                case 0xd: use_video = ~gr_cmp &  vid_cmp; break;
                                case 0xe: use_video =  gr_cmp & ~vid_cmp; break;
                                case 0xf: use_video = ~gr_cmp & ~vid_cmp; break;
                        }

                        if (use_video)
                                p[x] = mach64->overlay_dat[h];

                        h_acc += h_inc;
                        if (h_acc > (h_max << 12))
                                h_acc = (h_max << 12);
                }
        }

        mach64->overlay_v_acc += v_inc;
        if (mach64->overlay_v_acc > (v_max << 12))
                mach64->overlay_v_acc = v_max << 12;

        y_diff = (mach64->overlay_v_acc >> 12) - (old_y >> 12);

        if (mach64->scaler_format == 6)
                svga->overlay.addr += svga->overlay.pitch*4*y_diff;
        else
                svga->overlay.addr += svga->overlay.pitch*2*y_diff;

        mach64->scaler_update = y_diff;
}

static void mach64_io_remove(mach64_t *mach64)
{
        int c;
	uint16_t io_base = 0x02ec;

	switch (mach64->io_base)
	{
		case 0:
		default:
			io_base = 0x02ec;
			break;
		case 1:
			io_base = 0x01cc;
			break;
		case 2:
			io_base = 0x01c8;
			break;
		case 3:
			fatal("Attempting to use the reserved value for I/O Base\n");
			return;
	}

        io_removehandler(0x03c0, 0x0020, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

        for (c = 0; c < 8; c++)
        {
                io_removehandler((c * 0x1000) + 0x0000 + io_base, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
                io_removehandler((c * 0x1000) + 0x0400 + io_base, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
                io_removehandler((c * 0x1000) + 0x0800 + io_base, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
                io_removehandler((c * 0x1000) + 0x0c00 + io_base, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
        }

        io_removehandler(0x01ce, 0x0002, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

        if (mach64->block_decoded_io && mach64->block_decoded_io < 0x10000)
                io_removehandler(mach64->block_decoded_io, 0x0400, mach64_block_inb, mach64_block_inw, mach64_block_inl, mach64_block_outb, mach64_block_outw, mach64_block_outl, mach64);
}

static void mach64_io_set(mach64_t *mach64)
{
        int c;

        mach64_io_remove(mach64);

        io_sethandler(0x03c0, 0x0020, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

        if (!mach64->use_block_decoded_io)
        {
                for (c = 0; c < 8; c++)
                {
                        io_sethandler((c * 0x1000) + 0x2ec, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
                        io_sethandler((c * 0x1000) + 0x6ec, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
                        io_sethandler((c * 0x1000) + 0xaec, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
                        io_sethandler((c * 0x1000) + 0xeec, 0x0004, mach64_ext_inb, mach64_ext_inw, mach64_ext_inl, mach64_ext_outb, mach64_ext_outw, mach64_ext_outl, mach64);
                }
        }

        io_sethandler(0x01ce, 0x0002, mach64_in, NULL, NULL, mach64_out, NULL, NULL, mach64);

        if (mach64->use_block_decoded_io && mach64->block_decoded_io && mach64->block_decoded_io < 0x10000)
                io_sethandler(mach64->block_decoded_io, 0x0400, mach64_block_inb, mach64_block_inw, mach64_block_inl, mach64_block_outb, mach64_block_outw, mach64_block_outl, mach64);
}

uint8_t mach64_pci_read(int func, int addr, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        switch (addr)
        {
                case 0x00: return 0x02; /*ATi*/
                case 0x01: return 0x10;

                case 0x02: return mach64->pci_id & 0xff;
                case 0x03: return mach64->pci_id >> 8;

                case PCI_REG_COMMAND:
                return mach64->pci_regs[PCI_REG_COMMAND]; /*Respond to IO and memory accesses*/

                case 0x07: return 1 << 1; /*Medium DEVSEL timing*/

                case 0x08: /*Revision ID*/
                if (mach64->type == MACH64_GX)
                        return 0;
                return 0x40;

                case 0x09: return 0; /*Programming interface*/

                case 0x0a: return 0x01; /*Supports VGA interface, XGA compatible*/
                case 0x0b: return 0x03;

                case 0x10: return 0x00; /*Linear frame buffer address*/
                case 0x11: return 0x00;
                case 0x12: return mach64->linear_base >> 16;
                case 0x13: return mach64->linear_base >> 24;

                case 0x14:
                if (mach64->type == MACH64_VT2)
                        return 0x01; /*Block decoded IO address*/
                return 0x00;
                case 0x15:
                if (mach64->type == MACH64_VT2)
			return mach64->block_decoded_io >> 8;
		return 0x00;
                case 0x16:
                if (mach64->type == MACH64_VT2)
			return mach64->block_decoded_io >> 16;
		return 0x00;
                case 0x17:
                if (mach64->type == MACH64_VT2)
			return mach64->block_decoded_io >> 24;
		return 0x00;

                case 0x30: return mach64->pci_regs[0x30] & 0x01; /*BIOS ROM address*/
                case 0x31: return 0x00;
                case 0x32: return mach64->pci_regs[0x32];
                case 0x33: return mach64->pci_regs[0x33];

                case 0x3c: return mach64->int_line;
                case 0x3d: return PCI_INTA;

                case 0x40: return mach64->use_block_decoded_io | mach64->io_base;
        }
        return 0;
}

void mach64_pci_write(int func, int addr, uint8_t val, void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        switch (addr)
        {
                case PCI_REG_COMMAND:
                mach64->pci_regs[PCI_REG_COMMAND] = val & 0x27;
                if (val & PCI_COMMAND_IO)
                        mach64_io_set(mach64);
                else
                        mach64_io_remove(mach64);
                mach64_updatemapping(mach64);
                break;

                case 0x12:
                if (mach64->type == MACH64_VT2)
                        val = 0;
                mach64->linear_base = (mach64->linear_base & 0xff000000) | ((val & 0x80) << 16);
                mach64_updatemapping(mach64);
                break;
                case 0x13:
                mach64->linear_base = (mach64->linear_base & 0x800000) | (val << 24);
                mach64_updatemapping(mach64);
                break;

                case 0x15:
                if (mach64->type == MACH64_VT2)
                {
                        if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                                mach64_io_remove(mach64);
                        mach64->block_decoded_io = (mach64->block_decoded_io & 0xffff0000) | ((val & 0xfc) << 8);
                        if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                                mach64_io_set(mach64);
                }
                break;
                case 0x16:
                if (mach64->type == MACH64_VT2)
                {
                        if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                                mach64_io_remove(mach64);
                        mach64->block_decoded_io = (mach64->block_decoded_io & 0xff00fc00) | (val << 16);
                        if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                                mach64_io_set(mach64);
                }
                break;
                case 0x17:
                if (mach64->type == MACH64_VT2)
                {
                        if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                                mach64_io_remove(mach64);
                        mach64->block_decoded_io = (mach64->block_decoded_io & 0x00fffc00) | (val << 24);
                        if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
                                mach64_io_set(mach64);
                }
                break;

                case 0x30: case 0x32: case 0x33:
                mach64->pci_regs[addr] = val;
                if (mach64->pci_regs[0x30] & 0x01)
                {
                        uint32_t addr = (mach64->pci_regs[0x32] << 16) | (mach64->pci_regs[0x33] << 24);
                        mach64_log("Mach64 bios_rom enabled at %08x\n", addr);
                        mem_mapping_set_addr(&mach64->bios_rom.mapping, addr, 0x8000);
                }
                else
                {
                        mach64_log("Mach64 bios_rom disabled\n");
                        mem_mapping_disable(&mach64->bios_rom.mapping);
                }
                return;

                case 0x3c:
                mach64->int_line = val;
                break;

                case 0x40:
		if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
			mach64_io_remove(mach64);
		mach64->io_base = val & 0x03;
                if (mach64->type == MACH64_VT2)
                        mach64->use_block_decoded_io = val & 0x04;
		if (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
			mach64_io_set(mach64);
                break;
        }
}

static void *mach64_common_init(const device_t *info)
{
        mach64_t *mach64 = malloc(sizeof(mach64_t));
        memset(mach64, 0, sizeof(mach64_t));

        mach64->vram_size = device_get_config_int("memory");
        mach64->vram_mask = (mach64->vram_size << 20) - 1;

        svga_init(info, &mach64->svga, mach64, mach64->vram_size << 20,
                   mach64_recalctimings,
                   mach64_in, mach64_out,
                   NULL,
                   mach64_overlay_draw);
	mach64->svga.dac_hwcursor.ysize = 64;

        if (info->flags & DEVICE_PCI)
                mem_mapping_disable(&mach64->bios_rom.mapping);

			mem_mapping_add(&mach64->linear_mapping,        0,       0,       svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear, NULL, MEM_MAPPING_EXTERNAL, &mach64->svga);
        	mem_mapping_add(&mach64->mmio_linear_mapping,   0,       0,       mach64_ext_readb, mach64_ext_readw,  mach64_ext_readl,  mach64_ext_writeb, mach64_ext_writew,  mach64_ext_writel,  NULL, MEM_MAPPING_EXTERNAL,  mach64);
	        mem_mapping_add(&mach64->mmio_linear_mapping_2, 0,       0,       mach64_ext_readb, mach64_ext_readw,  mach64_ext_readl,  mach64_ext_writeb, mach64_ext_writew,  mach64_ext_writel,  NULL, MEM_MAPPING_EXTERNAL,  mach64);
        	mem_mapping_add(&mach64->mmio_mapping,          0xbc000, 0x04000, mach64_ext_readb, mach64_ext_readw,  mach64_ext_readl,  mach64_ext_writeb, mach64_ext_writew,  mach64_ext_writel,  NULL, MEM_MAPPING_EXTERNAL,  mach64);
        mem_mapping_disable(&mach64->mmio_mapping);

        mach64_io_set(mach64);

	if (info->flags & DEVICE_PCI)
	{
	        mach64->card = pci_add_card(PCI_ADD_VIDEO, mach64_pci_read, mach64_pci_write, mach64);
	}

        mach64->pci_regs[PCI_REG_COMMAND] = 3;
       	mach64->pci_regs[0x30] = 0x00;
        mach64->pci_regs[0x32] = 0x0c;
       	mach64->pci_regs[0x33] = 0x00;

        mach64->svga.ramdac = device_add(&ati68860_ramdac_device);
        mach64->svga.dac_hwcursor_draw = ati68860_hwcursor_draw;

        mach64->svga.clock_gen = device_add(&ics2595_device);

        mach64->dst_cntl = 3;

        mach64->i2c = i2c_gpio_init("ddc_ati_mach64");
        mach64->ddc = ddc_init(i2c_gpio_get_bus(mach64->i2c));

        mach64->wake_fifo_thread = thread_create_event();
        mach64->fifo_not_full_event = thread_create_event();
        mach64->thread_run = 1;
        mach64->fifo_thread = thread_create(fifo_thread, mach64);

        return mach64;
}

static void *mach64gx_init(const device_t *info)
{
        mach64_t *mach64 = mach64_common_init(info);

        if (info->flags & DEVICE_ISA)
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_isa);
        else if (info->flags & DEVICE_PCI)
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_pci);
	else
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_vlb);

        mach64->type = MACH64_GX;
	mach64->pci = !!(info->flags & DEVICE_PCI);
        mach64->pci_id = (int)'X' | ((int)'G' << 8);
        mach64->config_chip_id = 0x020000d7;
        mach64->dac_cntl = 5 << 16; /*ATI 68860 RAMDAC*/
        mach64->config_stat0 = (5 << 9) | (3 << 3); /*ATI-68860, 256Kx16 DRAM*/
        if (info->flags & DEVICE_PCI)
                mach64->config_stat0 |= 0; /*PCI, 256Kx16 DRAM*/
        else if (info->flags & DEVICE_VLB)
                mach64->config_stat0 |= 1; /*VLB, 256Kx16 DRAM*/
		else if (info->flags & DEVICE_ISA)
				mach64->config_stat0 |= 7; /*ISA 16-bit, 256k16 DRAM*/

        ati_eeprom_load(&mach64->eeprom, "mach64.nvr", 1);

        if (info->flags & DEVICE_PCI)
	        rom_init(&mach64->bios_rom, BIOS_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
	else if (info->flags & DEVICE_VLB)
	        rom_init(&mach64->bios_rom, BIOS_VLB_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
	else if (info->flags & DEVICE_ISA)
	        rom_init(&mach64->bios_rom, BIOS_ISA_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        return mach64;
}
static void *mach64vt2_init(const device_t *info)
{
        mach64_t *mach64 = mach64_common_init(info);
        svga_t *svga = &mach64->svga;

        if (info->flags & DEVICE_PCI)
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_pci);
	else
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_mach64_vlb);

        mach64->type = MACH64_VT2;
	mach64->pci = 1;
        mach64->pci_id = 0x5654;
        mach64->config_chip_id = 0x40005654;
        mach64->dac_cntl = 1 << 16; /*Internal 24-bit DAC*/
        mach64->config_stat0 = 4;
        mach64->use_block_decoded_io = 4;

        ati_eeprom_load(&mach64->eeprom, "mach64vt.nvr", 1);

        rom_init(&mach64->bios_rom, BIOS_ROMVT2_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        svga->vblank_start = mach64_vblank_start;

        return mach64;
}

int mach64gx_available(void)
{
        return rom_present(BIOS_ROM_PATH);
}
int mach64gx_isa_available(void)
{
        return rom_present(BIOS_ISA_ROM_PATH);
}
int mach64gx_vlb_available(void)
{
        return rom_present(BIOS_VLB_ROM_PATH);
}
int mach64vt2_available(void)
{
        return rom_present(BIOS_ROMVT2_PATH);
}

void mach64_close(void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        mach64->thread_run = 0;
        thread_set_event(mach64->wake_fifo_thread);
        thread_wait(mach64->fifo_thread);
        thread_destroy_event(mach64->fifo_not_full_event);
        thread_destroy_event(mach64->wake_fifo_thread);

        svga_close(&mach64->svga);

        ddc_close(mach64->ddc);
        i2c_gpio_close(mach64->i2c);

        free(mach64);
}

void mach64_speed_changed(void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        svga_recalctimings(&mach64->svga);
}

void mach64_force_redraw(void *p)
{
        mach64_t *mach64 = (mach64_t *)p;

        mach64->svga.fullchange = changeframecount;
}

// clang-format off
static const device_config_t mach64gx_config[] = {
    {
        "memory", "Memory size", CONFIG_SELECTION, "", 4, "", { 0 },
        {
            { "1 MB", 1 },
            { "2 MB", 2 },
            { "4 MB", 4 },
            { ""        }
        }
    },
    { "", "", -1 }
};

static const device_config_t mach64vt2_config[] = {
    {
        "memory", "Memory size", CONFIG_SELECTION, "", 4, "", { 0 },
        {
            { "2 MB", 2 },
            { "4 MB", 4 },
            { ""        }
        }
    },
    { "", "", -1 }
};
// clang-format on

const device_t mach64gx_isa_device = {
    .name = "ATI Mach64GX ISA",
    .internal_name = "mach64gx_isa",
    .flags = DEVICE_AT | DEVICE_ISA,
    .local = 0,
    .init = mach64gx_init,
    .close = mach64_close,
    .reset = NULL,
    { .available = mach64gx_isa_available },
    .speed_changed = mach64_speed_changed,
    .force_redraw = mach64_force_redraw,
    .config = mach64gx_config
};

const device_t mach64gx_vlb_device = {
    .name = "ATI Mach64GX VLB",
    .internal_name = "mach64gx_vlb",
    .flags = DEVICE_VLB,
    .local = 0,
    .init = mach64gx_init,
    .close = mach64_close,
    .reset = NULL,
    { .available = mach64gx_vlb_available },
    .speed_changed = mach64_speed_changed,
    .force_redraw = mach64_force_redraw,
    .config = mach64gx_config
};

const device_t mach64gx_pci_device = {
    .name = "ATI Mach64GX PCI",
    .internal_name = "mach64gx_pci",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = mach64gx_init,
    .close = mach64_close,
    .reset = NULL,
    { .available = mach64gx_available },
    .speed_changed = mach64_speed_changed,
    .force_redraw = mach64_force_redraw,
    .config = mach64gx_config
};

const device_t mach64vt2_device = {
    .name = "ATI Mach64VT2",
    .internal_name = "mach64vt2",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = mach64vt2_init,
    .close = mach64_close,
    .reset = NULL,
    { .available = mach64vt2_available },
    .speed_changed = mach64_speed_changed,
    .force_redraw = mach64_force_redraw,
    .config = mach64vt2_config
};
