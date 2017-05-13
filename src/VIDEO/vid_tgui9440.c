/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/*Trident TGUI9440 emulation*/
#include <stdlib.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../pci.h"
#include "../rom.h"
#include "../device.h"
#include "../thread.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_tkd8001_ramdac.h"
#include "vid_tgui9440.h"


#define FIFO_SIZE 65536
#define FIFO_MASK (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE (1 << 31)

#define FIFO_ENTRIES (tgui->fifo_write_idx - tgui->fifo_read_idx)
#define FIFO_FULL    ((tgui->fifo_write_idx - tgui->fifo_read_idx) >= FIFO_SIZE)
#define FIFO_EMPTY   (tgui->fifo_read_idx == tgui->fifo_write_idx)

#define FIFO_TYPE 0xff000000
#define FIFO_ADDR 0x00ffffff

enum
{
        FIFO_INVALID       = (0x00 << 24),
        FIFO_WRITE_BYTE    = (0x01 << 24),
        FIFO_WRITE_FB_BYTE = (0x04 << 24),
        FIFO_WRITE_FB_WORD = (0x05 << 24),
        FIFO_WRITE_FB_LONG = (0x06 << 24)
};

typedef struct
{
        uint32_t addr_type;
        uint32_t val;
} fifo_entry_t;

typedef struct tgui_t
{
        mem_mapping_t linear_mapping;
        mem_mapping_t accel_mapping;

        rom_t bios_rom;
        
        svga_t svga;

        struct
        {
        	uint16_t src_x, src_y;
        	uint16_t dst_x, dst_y;
        	uint16_t size_x, size_y;
        	uint16_t fg_col, bg_col;
        	uint8_t rop;
        	uint16_t flags;
        	uint8_t pattern[0x80];
        	int command;
        	int offset;
        	uint8_t ger22;
	
        	int x, y;
        	uint32_t src, dst, src_old, dst_old;
        	int pat_x, pat_y;
        	int use_src;
	
        	int pitch, bpp;

                uint16_t tgui_pattern[8][8];
        } accel;

        uint8_t tgui_3d8, tgui_3d9;
        int oldmode;
        uint8_t oldctrl2,newctrl2;

        uint32_t linear_base, linear_size;
        
        int ramdac_state;
        uint8_t ramdac_ctrl;
        
        int clock_m, clock_n, clock_k;
        
        uint32_t vram_size, vram_mask;

        fifo_entry_t fifo[FIFO_SIZE];
        volatile int fifo_read_idx, fifo_write_idx;

        thread_t *fifo_thread;
        event_t *wake_fifo_thread;
        event_t *fifo_not_full_event;
        
        int blitter_busy;
        uint64_t blitter_time;
        uint64_t status_time;
} tgui_t;

void tgui_recalcmapping(tgui_t *tgui);

static void fifo_thread(void *param);

uint8_t tgui_accel_read(uint32_t addr, void *priv);
uint16_t tgui_accel_read_w(uint32_t addr, void *priv);
uint32_t tgui_accel_read_l(uint32_t addr, void *priv);

void tgui_accel_write(uint32_t addr, uint8_t val, void *priv);
void tgui_accel_write_w(uint32_t addr, uint16_t val, void *priv);
void tgui_accel_write_l(uint32_t addr, uint32_t val, void *priv);


void tgui_accel_write_fb_l(uint32_t addr, uint32_t val, void *priv);

void tgui_out(uint16_t addr, uint8_t val, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
        svga_t *svga = &tgui->svga;

        uint8_t old;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout & 1)) addr ^= 0x60;

        switch (addr)
        {
                case 0x3C5:
                switch (svga->seqaddr & 0xf)
                {
                        case 0xB: 
                        tgui->oldmode=1; 
                        break;
                        case 0xC: 
                        if (svga->seqregs[0xe] & 0x80) 
                        svga->seqregs[0xc] = val; 
                        break;
                        case 0xd: 
                        if (tgui->oldmode) 
                                tgui->oldctrl2 = val; 
                        else 
                                tgui->newctrl2=val; 
                        break;
                        case 0xE:
                        svga->seqregs[0xe] = val ^ 2;
                        svga->write_bank = (svga->seqregs[0xe] & 0xf) * 65536;
                        if (!(svga->gdcreg[0xf] & 1)) 
                                svga->read_bank = svga->write_bank;
                        return;
                }
                break;

                case 0x3C6:
                if (tgui->ramdac_state == 4)
                {
                        tgui->ramdac_state = 0;
                        tgui->ramdac_ctrl = val;
                        switch (tgui->ramdac_ctrl & 0xf0)
                        {
                                case 0x10:
                                svga->bpp = 15;
                                break;
                                case 0x30:
                                svga->bpp = 16;
                                break;
                                case 0xd0:
                                svga->bpp = 24;
                                break;
                                default:
                                svga->bpp = 8;
                                break;
                        }
                        return;
                }
                case 0x3C7: case 0x3C8: case 0x3C9:
                tgui->ramdac_state = 0;
		break;

                case 0x3CF:
                switch (svga->gdcaddr & 15)
                {
			case 0x6:
			if (svga->gdcreg[6] != val)
			{
				svga->gdcreg[6] = val;
				tgui_recalcmapping(tgui);
			}
			return;
			
                        case 0xE:
                        svga->gdcreg[0xe] = val ^ 2;
                        if ((svga->gdcreg[0xf] & 1) == 1)
                           svga->read_bank = (svga->gdcreg[0xe] & 0xf) * 65536;
                        break;
                        case 0xF:
                        if (val & 1) svga->read_bank = (svga->gdcreg[0xe]  & 0xf)  *65536;
                        else         svga->read_bank = (svga->seqregs[0xe] & 0xf)  *65536;
                        svga->write_bank = (svga->seqregs[0xe] & 0xf) * 65536;
                        break;
                }
                break;
                case 0x3D4:
		svga->crtcreg = val & 0x7f;
                return;
                case 0x3D5:
		if (svga->crtcreg <= 0x18)
			val &= mask_crtc[svga->crtcreg];
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;
                if (old != val)
                {
                        if (svga->crtcreg < 0xE || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                switch (svga->crtcreg)
                {
			case 0x21:
			if (old != val)
			{
                                if (!PCI)
                                {
                                        tgui->linear_base = ((val & 0xf) | ((val >> 2) & 0x30)) << 20;
                                        tgui->linear_size = (val & 0x10) ? 0x200000 : 0x100000;
                                }
        			tgui_recalcmapping(tgui);
                        }
			break;

			case 0x40: case 0x41: case 0x42: case 0x43:
			case 0x44: case 0x45: case 0x46: case 0x47:
			svga->hwcursor.x = (svga->crtc[0x40] | (svga->crtc[0x41] << 8)) & 0x7ff;
			svga->hwcursor.y = (svga->crtc[0x42] | (svga->crtc[0x43] << 8)) & 0x7ff;
			svga->hwcursor.xoff = svga->crtc[0x46] & 0x3f;
			svga->hwcursor.yoff = svga->crtc[0x47] & 0x3f;
			svga->hwcursor.addr = (svga->crtc[0x44] << 10) | ((svga->crtc[0x45] & 0x7) << 18) | (svga->hwcursor.yoff * 8);
			break;
			
			case 0x50:
			svga->hwcursor.ena = val & 0x80;
			break;
		}
                return;
                case 0x3D8:
                tgui->tgui_3d8 = val;
                if (svga->gdcreg[0xf] & 4)
                {
                        svga->write_bank = (val & 0x1f) * 65536;
                        if (!(svga->gdcreg[0xf] & 1))
                        {
                                svga->read_bank = (val & 0x1f) * 65536;
                        }
                }
                return;
                case 0x3D9:
                tgui->tgui_3d9 = val;
                if ((svga->gdcreg[0xf] & 5) == 5)
                {
                        svga->read_bank = (val & 0x1F) * 65536;
                }
                return;
                
                case 0x43c8:
                tgui->clock_n = val & 0x7f;
                tgui->clock_m = (tgui->clock_m & ~1) | (val >> 7);
                break;
                case 0x43c9:
                tgui->clock_m = (tgui->clock_m & ~0x1e) | ((val << 1) & 0x1e);
                tgui->clock_k = (val & 0x10) >> 4;
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t tgui_in(uint16_t addr, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
        svga_t *svga = &tgui->svga;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout & 1)) addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3C5:
                if ((svga->seqaddr & 0xf) == 0xb)
                {
                        tgui->oldmode = 0;
                        return 0xe3; /*TGUI9440AGi*/
                }
                if ((svga->seqaddr & 0xf) == 0xd)
                {
                        if (tgui->oldmode) return tgui->oldctrl2;
                        return tgui->newctrl2;
                }
                break;
                case 0x3C6:
                if (tgui->ramdac_state == 4)
                        return tgui->ramdac_ctrl;
                tgui->ramdac_state++;
                break;
                case 0x3C7: case 0x3C8: case 0x3C9:
                tgui->ramdac_state = 0;
                break;
                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                return svga->crtc[svga->crtcreg];
                case 0x3d8:
		return tgui->tgui_3d8;
		case 0x3d9:
		return tgui->tgui_3d9;
        }
        return svga_in(addr, svga);
}

void tgui_recalctimings(svga_t *svga)
{
        tgui_t *tgui = (tgui_t *)svga->p;

        if (svga->crtc[0x29] & 0x10)
                svga->rowoffset += 0x100;

        if (svga->bpp == 24)
                svga->hdisp = (svga->crtc[1] + 1) * 8;
        
        if ((svga->crtc[0x1e] & 0xA0) == 0xA0) svga->ma_latch |= 0x10000;
        if ((svga->crtc[0x27] & 0x01) == 0x01) svga->ma_latch |= 0x20000;
        if ((svga->crtc[0x27] & 0x02) == 0x02) svga->ma_latch |= 0x40000;
        
        if (tgui->oldctrl2 & 0x10)
        {
                svga->rowoffset <<= 1;
                svga->ma_latch <<= 1;
        }
        if (tgui->oldctrl2 & 0x10) /*I'm not convinced this is the right register for this function*/
           svga->lowres=0;

	svga->lowres = !(svga->crtc[0x2a] & 0x40); 

        if (svga->crtc[0x1e] & 4)
	{
		svga->vtotal *= 2;
		svga->dispend *= 2;
		svga->vblankstart *= 2;
		svga->vsyncstart *= 2;
		svga->split *= 2;
	}
        
        if (svga->miscout & 8)
                svga->clock = cpuclock / (((tgui->clock_n + 8) * 14318180.0) / ((tgui->clock_m + 2) * (1 << tgui->clock_k)));

        if (svga->gdcreg[0xf] & 0x08)
                svga->clock *= 2;
        else if (svga->gdcreg[0xf] & 0x40)
                svga->clock *= 3;
                                
        if ((tgui->oldctrl2 & 0x10) || (svga->crtc[0x2a] & 0x40))
        {
                switch (svga->bpp)
                {
                        case 8: 
                        svga->render = svga_render_8bpp_highres; 
                        break;
                        case 15: 
                        svga->render = svga_render_15bpp_highres; 
                        break;
                        case 16: 
                        svga->render = svga_render_16bpp_highres; 
                        break;
                        case 24: 
                        svga->render = svga_render_24bpp_highres;
                        break;
                }
        }
}

void tgui_recalcmapping(tgui_t *tgui)
{
        svga_t *svga = &tgui->svga;

	if (svga->crtc[0x21] & 0x20)
	{
                mem_mapping_disable(&svga->mapping);
                mem_mapping_set_addr(&tgui->linear_mapping, tgui->linear_base, tgui->linear_size);
		svga->linear_base = tgui->linear_base;
                mem_mapping_enable(&tgui->accel_mapping);
	}
	else
	{
                mem_mapping_disable(&tgui->linear_mapping);
                mem_mapping_disable(&tgui->accel_mapping);
                switch (svga->gdcreg[6] & 0xC)
                {
                        case 0x0: /*128k at A0000*/
                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                        svga->banked_mask = 0xffff;
                        break;
                        case 0x4: /*64k at A0000*/
                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                        mem_mapping_enable(&tgui->accel_mapping);
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
        }
}

void tgui_hwcursor_draw(svga_t *svga, int displine)
{
        uint32_t dat[2];
        int xx;
        int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
	int y_add = enable_overscan ? 16 : 0;
	int x_add = enable_overscan ? 8 : 0;
        
        dat[0] = (svga->vram[svga->hwcursor_latch.addr]     << 24) | (svga->vram[svga->hwcursor_latch.addr + 1] << 16) | (svga->vram[svga->hwcursor_latch.addr + 2] << 8) | svga->vram[svga->hwcursor_latch.addr + 3];
        dat[1] = (svga->vram[svga->hwcursor_latch.addr + 4] << 24) | (svga->vram[svga->hwcursor_latch.addr + 5] << 16) | (svga->vram[svga->hwcursor_latch.addr + 6] << 8) | svga->vram[svga->hwcursor_latch.addr + 7];
        for (xx = 0; xx < 32; xx++)
        {
                if (offset >= svga->hwcursor_latch.x)
                {
                        if (!(dat[0] & 0x80000000))
                                ((uint32_t *)buffer32->line[displine + y_add])[offset + 32 + x_add]  = (dat[1] & 0x80000000) ? 0xffffff : 0;
                        else if (dat[1] & 0x80000000)
                                ((uint32_t *)buffer32->line[displine + y_add])[offset + 32 + x_add] ^= 0xffffff;
                }
                           
                offset++;
                dat[0] <<= 1;
                dat[1] <<= 1;
        }
        svga->hwcursor_latch.addr += 8;
}

uint8_t tgui_pci_read(int func, int addr, void *p)
{
        tgui_t *tgui = (tgui_t *)p;

        switch (addr)
        {
                case 0x00: return 0x23; /*Trident*/
                case 0x01: return 0x10;
                
                case 0x02: return 0x40; /*TGUI9440 (9682)*/
                case 0x03: return 0x94;
                
                case 0x04: return 0x03; /*Respond to IO and memory accesses*/

                case 0x07: return 1 << 1; /*Medium DEVSEL timing*/
                
                case 0x08: return 0; /*Revision ID*/
                case 0x09: return 0; /*Programming interface*/
                
                case 0x0a: return 0x01; /*Supports VGA interface, XGA compatible*/
                case 0x0b: return 0x03;
                
                case 0x10: return 0x00; /*Linear frame buffer address*/
                case 0x11: return 0x00;
                case 0x12: return tgui->linear_base >> 16;
                case 0x13: return tgui->linear_base >> 24;

                case 0x30: return 0x01; /*BIOS ROM address*/
                case 0x31: return 0x00;
                case 0x32: return 0x0C;
                case 0x33: return 0x00;
        }
        return 0;
}

void tgui_pci_write(int func, int addr, uint8_t val, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
        svga_t *svga = &tgui->svga;

        switch (addr)
        {
                case 0x12:
                tgui->linear_base = (tgui->linear_base & 0xff000000) | ((val & 0xe0) << 16);
                tgui->linear_size = 2 << 20;
                svga->crtc[0x21] = (svga->crtc[0x21] & ~0xf) | (val >> 4);
                tgui_recalcmapping(tgui);
                break;
                case 0x13:
                tgui->linear_base = (tgui->linear_base & 0xe00000) | (val << 24);
                tgui->linear_size = 2 << 20;
                svga->crtc[0x21] = (svga->crtc[0x21] & ~0xc0) | (val >> 6);
                tgui_recalcmapping(tgui);
                break;
        }
}

void *tgui9440_init()
{
        tgui_t *tgui = malloc(sizeof(tgui_t));
        memset(tgui, 0, sizeof(tgui_t));
        
        tgui->vram_size = device_get_config_int("memory") << 20;
        tgui->vram_mask = tgui->vram_size - 1;

        rom_init(&tgui->bios_rom, L"roms/9440.vbi", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        svga_init(&tgui->svga, tgui, tgui->vram_size,
                   tgui_recalctimings,
                   tgui_in, tgui_out,
                   tgui_hwcursor_draw,
                   NULL);

        mem_mapping_add(&tgui->linear_mapping, 0,       0,      svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear, NULL, 0, &tgui->svga);
        mem_mapping_add(&tgui->accel_mapping,  0xbc000, 0x4000, tgui_accel_read,  tgui_accel_read_w, tgui_accel_read_l, tgui_accel_write,  tgui_accel_write_w, tgui_accel_write_l, NULL, 0,  tgui);
        mem_mapping_disable(&tgui->accel_mapping);

        io_sethandler(0x03c0, 0x0020, tgui_in, NULL, NULL, tgui_out, NULL, NULL, tgui);
        io_sethandler(0x43c8, 0x0002, tgui_in, NULL, NULL, tgui_out, NULL, NULL, tgui);

        pci_add(tgui_pci_read, tgui_pci_write, tgui);

        tgui->wake_fifo_thread = thread_create_event();
        tgui->fifo_not_full_event = thread_create_event();
        tgui->fifo_thread = thread_create(fifo_thread, tgui);

        return tgui;
}

static int tgui9440_available()
{
        return rom_present(L"roms/9440.vbi");
}

void tgui_close(void *p)
{
        tgui_t *tgui = (tgui_t *)p;
        
        svga_close(&tgui->svga);
        
        thread_kill(tgui->fifo_thread);
        thread_destroy_event(tgui->wake_fifo_thread);
        thread_destroy_event(tgui->fifo_not_full_event);

        free(tgui);
}

void tgui_speed_changed(void *p)
{
        tgui_t *tgui = (tgui_t *)p;
        
        svga_recalctimings(&tgui->svga);
}

void tgui_force_redraw(void *p)
{
        tgui_t *tgui = (tgui_t *)p;

        tgui->svga.fullchange = changeframecount;
}

enum
{
	TGUI_BITBLT = 1
};

enum
{
        TGUI_SRCCPU = 0,
        
	TGUI_SRCDISP = 0x04,	/*Source is from display*/
	TGUI_PATMONO = 0x20,	/*Pattern is monochrome and needs expansion*/
	TGUI_SRCMONO = 0x40,	/*Source is monochrome from CPU and needs expansion*/
	TGUI_TRANSENA  = 0x1000, /*Transparent (no draw when source == bg col)*/
	TGUI_TRANSREV  = 0x2000, /*Reverse fg/bg for transparent*/
	TGUI_SOLIDFILL = 0x4000	/*Pattern all zero?*/
};

#define READ(addr, dat) if (tgui->accel.bpp == 0) dat = svga->vram[addr & 0x1fffff]; \
                        else                     dat = vram_w[addr & 0xfffff];
                        
#define MIX() do \
	{								\
		out = 0;						\
        	for (c=0;c<16;c++)					\
	        {							\
			d=(dst_dat & (1<<c)) ? 1:0;			\
                       	if (src_dat & (1<<c)) d|=2;			\
               	        if (pat_dat & (1<<c)) d|=4;			\
                        if (tgui->accel.rop & (1<<d)) out|=(1<<c);	\
	        }							\
	} while (0)

#define WRITE(addr, dat)        if (tgui->accel.bpp == 0)                                                \
                                {                                                                       \
                                        svga->vram[addr & 0x1fffff] = dat;                                    \
                                        svga->changedvram[((addr) & 0x1fffff) >> 12] = changeframecount;      \
                                }                                                                       \
                                else                                                                    \
                                {                                                                       \
                                        vram_w[addr & 0xfffff] = dat;                                   \
                                        svga->changedvram[((addr) & 0xfffff) >> 11] = changeframecount;        \
                                }
                                
void tgui_accel_write_fb_b(uint32_t addr, uint8_t val, void *priv);
void tgui_accel_write_fb_w(uint32_t addr, uint16_t val, void *priv);

void tgui_accel_command(int count, uint32_t cpu_dat, tgui_t *tgui)
{
        svga_t *svga = &tgui->svga;
	int x, y;
	int c, d;
	uint16_t src_dat, dst_dat, pat_dat;
	uint16_t out;
	int xdir = (tgui->accel.flags & 0x200) ? -1 : 1;
	int ydir = (tgui->accel.flags & 0x100) ? -1 : 1;
	uint16_t trans_col = (tgui->accel.flags & TGUI_TRANSREV) ? tgui->accel.fg_col : tgui->accel.bg_col;
        uint16_t *vram_w = (uint16_t *)svga->vram;
        
	if (tgui->accel.bpp == 0)
                trans_col &= 0xff;
	
	if (count != -1 && !tgui->accel.x)
	{
		count -= tgui->accel.offset;
		cpu_dat <<= tgui->accel.offset;
	}
	if (count == -1)
	{
		tgui->accel.x = tgui->accel.y = 0;
	}
	if (tgui->accel.flags & TGUI_SOLIDFILL)
	{
		for (y = 0; y < 8; y++)
		{
			for (x = 0; x < 8; x++)
			{
				tgui->accel.tgui_pattern[y][x] = tgui->accel.fg_col;
			}
		}
	}
	else if (tgui->accel.flags & TGUI_PATMONO)
	{
		for (y = 0; y < 8; y++)
		{
			for (x = 0; x < 8; x++)
			{
				tgui->accel.tgui_pattern[y][x] = (tgui->accel.pattern[y] & (1 << x)) ? tgui->accel.fg_col : tgui->accel.bg_col;
			}
		}
	}
	else
	{
                if (tgui->accel.bpp == 0)
                {
        		for (y = 0; y < 8; y++)
        		{
        			for (x = 0; x < 8; x++)
        			{
        				tgui->accel.tgui_pattern[y][x] = tgui->accel.pattern[x + y*8];
        			}
                        }
		}
		else
                {
        		for (y = 0; y < 8; y++)
        		{
        			for (x = 0; x < 8; x++)
        			{
        				tgui->accel.tgui_pattern[y][x] = tgui->accel.pattern[x*2 + y*16] | (tgui->accel.pattern[x*2 + y*16 + 1] << 8);
        			}
                        }
		}
	}
        switch (tgui->accel.command)
	{
		case TGUI_BITBLT:
		if (count == -1)
		{
			tgui->accel.src = tgui->accel.src_old = tgui->accel.src_x + (tgui->accel.src_y * tgui->accel.pitch);
			tgui->accel.dst = tgui->accel.dst_old = tgui->accel.dst_x + (tgui->accel.dst_y * tgui->accel.pitch);
			tgui->accel.pat_x = tgui->accel.dst_x;
			tgui->accel.pat_y = tgui->accel.dst_y;
		}

		switch (tgui->accel.flags & (TGUI_SRCMONO|TGUI_SRCDISP))
		{
			case TGUI_SRCCPU:
			if (count == -1)
			{
				if (svga->crtc[0x21] & 0x20)
                                        mem_mapping_set_handler(&tgui->linear_mapping, svga_read_linear, svga_readw_linear, svga_readl_linear, tgui_accel_write_fb_b, tgui_accel_write_fb_w, tgui_accel_write_fb_l);

				if (tgui->accel.use_src)
                                        return;
			}
			else
			     count >>= 3;
			while (count)
			{
				if (tgui->accel.bpp == 0)
				{
                                        src_dat = cpu_dat >> 24;
                                        cpu_dat <<= 8;
                                }
                                else
                                {
                                        src_dat = (cpu_dat >> 24) | ((cpu_dat >> 8) & 0xff00);
                                        cpu_dat <<= 16;
                                        count--;
                                }
				READ(tgui->accel.dst, dst_dat);
				pat_dat = tgui->accel.tgui_pattern[tgui->accel.pat_y & 7][tgui->accel.pat_x & 7];
	
                                if (!(tgui->accel.flags & TGUI_TRANSENA) || src_dat != trans_col)
                                {
        				MIX();
	
                                        WRITE(tgui->accel.dst, out);
                                }

				tgui->accel.src += xdir;
				tgui->accel.dst += xdir;
				tgui->accel.pat_x += xdir;
	
				tgui->accel.x++;
				if (tgui->accel.x > tgui->accel.size_x)
				{
					tgui->accel.x = 0;
					tgui->accel.y++;
					
					tgui->accel.pat_x = tgui->accel.dst_x;
	
					tgui->accel.src = tgui->accel.src_old = tgui->accel.src_old + (ydir * tgui->accel.pitch);
					tgui->accel.dst = tgui->accel.dst_old = tgui->accel.dst_old + (ydir * tgui->accel.pitch);
					tgui->accel.pat_y += ydir;
					
					if (tgui->accel.y > tgui->accel.size_y)
					{
						if (svga->crtc[0x21] & 0x20)
						{
                                                        mem_mapping_set_handler(&tgui->linear_mapping, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear);
						}
						return;
					}
					if (tgui->accel.use_src)
                                                return;
				}
				count--;
			}
			break;
						
			case TGUI_SRCMONO | TGUI_SRCCPU:
			if (count == -1)
			{
				if (svga->crtc[0x21] & 0x20)
                                        mem_mapping_set_handler(&tgui->linear_mapping, svga_read_linear, svga_readw_linear, svga_readl_linear, tgui_accel_write_fb_b, tgui_accel_write_fb_w, tgui_accel_write_fb_l);

				if (tgui->accel.use_src)
                                        return;
			}
			while (count)
			{
				src_dat = ((cpu_dat >> 31) ? tgui->accel.fg_col : tgui->accel.bg_col);
				if (tgui->accel.bpp == 0)
				    src_dat &= 0xff;
				    
				READ(tgui->accel.dst, dst_dat);
				pat_dat = tgui->accel.tgui_pattern[tgui->accel.pat_y & 7][tgui->accel.pat_x & 7];

                                if (!(tgui->accel.flags & TGUI_TRANSENA) || src_dat != trans_col)
                                {
        				MIX();

				        WRITE(tgui->accel.dst, out);
                                }
				cpu_dat <<= 1;
				tgui->accel.src += xdir;
				tgui->accel.dst += xdir;
				tgui->accel.pat_x += xdir;
	
				tgui->accel.x++;
				if (tgui->accel.x > tgui->accel.size_x)
				{
					tgui->accel.x = 0;
					tgui->accel.y++;
					
					tgui->accel.pat_x = tgui->accel.dst_x;
	
					tgui->accel.src = tgui->accel.src_old = tgui->accel.src_old + (ydir * tgui->accel.pitch);
					tgui->accel.dst = tgui->accel.dst_old = tgui->accel.dst_old + (ydir * tgui->accel.pitch);
					tgui->accel.pat_y += ydir;
					
					if (tgui->accel.y > tgui->accel.size_y)
					{
						if (svga->crtc[0x21] & 0x20)
						{
                                                        mem_mapping_set_handler(&tgui->linear_mapping, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear);
						}
						return;
					}
					if (tgui->accel.use_src)
                                                return;
				}
				count--;
			}
			break;

			default:
			while (count)
			{
				READ(tgui->accel.src, src_dat);
				READ(tgui->accel.dst, dst_dat);                                                                
				pat_dat = tgui->accel.tgui_pattern[tgui->accel.pat_y & 7][tgui->accel.pat_x & 7];
	
                                if (!(tgui->accel.flags & TGUI_TRANSENA) || src_dat != trans_col)
                                {
        				MIX();
	
                                        WRITE(tgui->accel.dst, out);
                                }
	
				tgui->accel.src += xdir;
				tgui->accel.dst += xdir;
				tgui->accel.pat_x += xdir;
	
				tgui->accel.x++;
				if (tgui->accel.x > tgui->accel.size_x)
				{
					tgui->accel.x = 0;
					tgui->accel.y++;
					
					tgui->accel.pat_x = tgui->accel.dst_x;
	
					tgui->accel.src = tgui->accel.src_old = tgui->accel.src_old + (ydir * tgui->accel.pitch);
					tgui->accel.dst = tgui->accel.dst_old = tgui->accel.dst_old + (ydir * tgui->accel.pitch);
					tgui->accel.pat_y += ydir;
					
					if (tgui->accel.y > tgui->accel.size_y)
						return;
				}
				count--;
			}
			break;
		}
		break;
	}
}

static void tgui_accel_write_fifo(tgui_t *tgui, uint32_t addr, uint8_t val)
{
	switch (addr & 0xff)
	{
                case 0x22:
                tgui->accel.ger22 = val;
                tgui->accel.pitch = 512 << ((val >> 2) & 3);
                tgui->accel.bpp = (val & 3) ? 1 : 0;
                tgui->accel.pitch >>= tgui->accel.bpp;
                break;
                
		case 0x24: /*Command*/
		tgui->accel.command = val;
		tgui_accel_command(-1, 0, tgui);
		break;
		
		case 0x27: /*ROP*/
		tgui->accel.rop = val;
		tgui->accel.use_src = (val & 0x33) ^ ((val >> 2) & 0x33);
		break;
		
		case 0x28: /*Flags*/
		tgui->accel.flags = (tgui->accel.flags & 0xff00) | val;
		break;
		case 0x29: /*Flags*/
		tgui->accel.flags = (tgui->accel.flags & 0xff) | (val << 8);
		break;

		case 0x2b:
		tgui->accel.offset = val & 7;
		break;
		
		case 0x2c: /*Foreground colour*/
		tgui->accel.fg_col = (tgui->accel.fg_col & 0xff00) | val;
		break;
		case 0x2d: /*Foreground colour*/
		tgui->accel.fg_col = (tgui->accel.fg_col & 0xff) | (val << 8);
		break;

		case 0x30: /*Background colour*/
		tgui->accel.bg_col = (tgui->accel.bg_col & 0xff00) | val;
		break;
		case 0x31: /*Background colour*/
		tgui->accel.bg_col = (tgui->accel.bg_col & 0xff) | (val << 8);
		break;

		case 0x38: /*Dest X*/
		tgui->accel.dst_x = (tgui->accel.dst_x & 0xff00) | val;
		break;
		case 0x39: /*Dest X*/
		tgui->accel.dst_x = (tgui->accel.dst_x & 0xff) | (val << 8);
		break;
		case 0x3a: /*Dest Y*/
		tgui->accel.dst_y = (tgui->accel.dst_y & 0xff00) | val;
		break;
		case 0x3b: /*Dest Y*/
		tgui->accel.dst_y = (tgui->accel.dst_y & 0xff) | (val << 8);
		break;

		case 0x3c: /*Src X*/
		tgui->accel.src_x = (tgui->accel.src_x & 0xff00) | val;
		break;
		case 0x3d: /*Src X*/
		tgui->accel.src_x = (tgui->accel.src_x & 0xff) | (val << 8);
		break;
		case 0x3e: /*Src Y*/
		tgui->accel.src_y = (tgui->accel.src_y & 0xff00) | val;
		break;
		case 0x3f: /*Src Y*/
		tgui->accel.src_y = (tgui->accel.src_y & 0xff) | (val << 8);
		break;

		case 0x40: /*Size X*/
		tgui->accel.size_x = (tgui->accel.size_x & 0xff00) | val;
		break;
		case 0x41: /*Size X*/
		tgui->accel.size_x = (tgui->accel.size_x & 0xff) | (val << 8);
		break;
		case 0x42: /*Size Y*/
		tgui->accel.size_y = (tgui->accel.size_y & 0xff00) | val;
		break;
		case 0x43: /*Size Y*/
		tgui->accel.size_y = (tgui->accel.size_y & 0xff) | (val << 8);
		break;
		
		case 0x80: case 0x81: case 0x82: case 0x83:
		case 0x84: case 0x85: case 0x86: case 0x87:
		case 0x88: case 0x89: case 0x8a: case 0x8b:
		case 0x8c: case 0x8d: case 0x8e: case 0x8f:
		case 0x90: case 0x91: case 0x92: case 0x93:
		case 0x94: case 0x95: case 0x96: case 0x97:
		case 0x98: case 0x99: case 0x9a: case 0x9b:
		case 0x9c: case 0x9d: case 0x9e: case 0x9f:
		case 0xa0: case 0xa1: case 0xa2: case 0xa3:
		case 0xa4: case 0xa5: case 0xa6: case 0xa7:
		case 0xa8: case 0xa9: case 0xaa: case 0xab:
		case 0xac: case 0xad: case 0xae: case 0xaf:
		case 0xb0: case 0xb1: case 0xb2: case 0xb3:
		case 0xb4: case 0xb5: case 0xb6: case 0xb7:
		case 0xb8: case 0xb9: case 0xba: case 0xbb:
		case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		case 0xc0: case 0xc1: case 0xc2: case 0xc3:
		case 0xc4: case 0xc5: case 0xc6: case 0xc7:
		case 0xc8: case 0xc9: case 0xca: case 0xcb:
		case 0xcc: case 0xcd: case 0xce: case 0xcf:
		case 0xd0: case 0xd1: case 0xd2: case 0xd3:
		case 0xd4: case 0xd5: case 0xd6: case 0xd7:
		case 0xd8: case 0xd9: case 0xda: case 0xdb:
		case 0xdc: case 0xdd: case 0xde: case 0xdf:
		case 0xe0: case 0xe1: case 0xe2: case 0xe3:
		case 0xe4: case 0xe5: case 0xe6: case 0xe7:
		case 0xe8: case 0xe9: case 0xea: case 0xeb:
		case 0xec: case 0xed: case 0xee: case 0xef:
		case 0xf0: case 0xf1: case 0xf2: case 0xf3:
		case 0xf4: case 0xf5: case 0xf6: case 0xf7:
		case 0xf8: case 0xf9: case 0xfa: case 0xfb:
		case 0xfc: case 0xfd: case 0xfe: case 0xff:
		tgui->accel.pattern[addr & 0x7f] = val;
		break;
	}
}

static void tgui_accel_write_fifo_fb_b(tgui_t *tgui, uint32_t addr, uint8_t val)
{
	tgui_accel_command(8, val << 24, tgui);
}
static void tgui_accel_write_fifo_fb_w(tgui_t *tgui, uint32_t addr, uint16_t val)
{
        tgui_accel_command(16, (((val & 0xff00) >> 8) | ((val & 0x00ff) << 8)) << 16, tgui);
}
static void tgui_accel_write_fifo_fb_l(tgui_t *tgui, uint32_t addr, uint32_t val)
{
	tgui_accel_command(32, ((val & 0xff000000) >> 24) | ((val & 0x00ff0000) >> 8) | ((val & 0x0000ff00) << 8) | ((val & 0x000000ff) << 24), tgui);
}

static void fifo_thread(void *param)
{
        tgui_t *tgui = (tgui_t *)param;
	uint64_t start_time;
	uint64_t end_time;
	fifo_entry_t *fifo;
        
        while (1)
        {
                thread_set_event(tgui->fifo_not_full_event);
                thread_wait_event(tgui->wake_fifo_thread, -1);
                thread_reset_event(tgui->wake_fifo_thread);
                tgui->blitter_busy = 1;
                while (!FIFO_EMPTY)
                {
                        start_time = timer_read();
                        fifo = &tgui->fifo[tgui->fifo_read_idx & FIFO_MASK];

                        switch (fifo->addr_type & FIFO_TYPE)
                        {
                                case FIFO_WRITE_BYTE:
                                tgui_accel_write_fifo(tgui, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                                case FIFO_WRITE_FB_BYTE:
                                tgui_accel_write_fifo_fb_b(tgui, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                                case FIFO_WRITE_FB_WORD:
                                tgui_accel_write_fifo_fb_w(tgui, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                                case FIFO_WRITE_FB_LONG:
                                tgui_accel_write_fifo_fb_l(tgui, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                        }
                                                
                        tgui->fifo_read_idx++;
                        fifo->addr_type = FIFO_INVALID;

                        if (FIFO_ENTRIES > 0xe000)
                                thread_set_event(tgui->fifo_not_full_event);

                        end_time = timer_read();
                        tgui->blitter_time += end_time - start_time;
                }
                tgui->blitter_busy = 0;
        }
}

static __inline void wake_fifo_thread(tgui_t *tgui)
{
        thread_set_event(tgui->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

static void tgui_wait_fifo_idle(tgui_t *tgui)
{
        while (!FIFO_EMPTY)
        {
                wake_fifo_thread(tgui);
                thread_wait_event(tgui->fifo_not_full_event, 1);
        }
}

static void tgui_queue(tgui_t *tgui, uint32_t addr, uint32_t val, uint32_t type)
{
        fifo_entry_t *fifo = &tgui->fifo[tgui->fifo_write_idx & FIFO_MASK];

        if (FIFO_FULL)
        {
                thread_reset_event(tgui->fifo_not_full_event);
                if (FIFO_FULL)
                {
                        thread_wait_event(tgui->fifo_not_full_event, -1); /*Wait for room in ringbuffer*/
                }
        }

        fifo->val = val;
        fifo->addr_type = (addr & FIFO_ADDR) | type;

        tgui->fifo_write_idx++;
        
        if (FIFO_ENTRIES > 0xe000 || FIFO_ENTRIES < 8)
                wake_fifo_thread(tgui);
}


void tgui_accel_write(uint32_t addr, uint8_t val, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
	if ((addr & ~0xff) != 0xbff00)
		return;
	tgui_queue(tgui, addr, val, FIFO_WRITE_BYTE);
}

void tgui_accel_write_w(uint32_t addr, uint16_t val, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
	tgui_accel_write(addr, val, tgui);
	tgui_accel_write(addr + 1, val >> 8, tgui);
}

void tgui_accel_write_l(uint32_t addr, uint32_t val, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
	tgui_accel_write(addr, val, tgui);
	tgui_accel_write(addr + 1, val >> 8, tgui);
	tgui_accel_write(addr + 2, val >> 16, tgui);
	tgui_accel_write(addr + 3, val >> 24, tgui);
}

uint8_t tgui_accel_read(uint32_t addr, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
	if ((addr & ~0xff) != 0xbff00)
		return 0xff;
	if ((addr & 0xff) != 0x20)
	       tgui_wait_fifo_idle(tgui);
	switch (addr & 0xff)
	{
		case 0x20: /*Status*/
		if (!FIFO_EMPTY)
		      return 1 << 5;
		return 0;
		
		case 0x27: /*ROP*/
		return tgui->accel.rop;
		
		case 0x28: /*Flags*/
		return tgui->accel.flags & 0xff;
		case 0x29: /*Flags*/
		return tgui->accel.flags >> 8;

		case 0x2b:
		return tgui->accel.offset;
		
		case 0x2c: /*Background colour*/
		return tgui->accel.bg_col & 0xff;
		case 0x2d: /*Background colour*/
		return tgui->accel.bg_col >> 8;

		case 0x30: /*Foreground colour*/
		return tgui->accel.fg_col & 0xff;
		case 0x31: /*Foreground colour*/
		return tgui->accel.fg_col >> 8;

		case 0x38: /*Dest X*/
		return tgui->accel.dst_x & 0xff;
		case 0x39: /*Dest X*/
		return tgui->accel.dst_x >> 8;
		case 0x3a: /*Dest Y*/
		return tgui->accel.dst_y & 0xff;
		case 0x3b: /*Dest Y*/
		return tgui->accel.dst_y >> 8;

		case 0x3c: /*Src X*/
		return tgui->accel.src_x & 0xff;
		case 0x3d: /*Src X*/
		return tgui->accel.src_x >> 8;
		case 0x3e: /*Src Y*/
		return tgui->accel.src_y & 0xff;
		case 0x3f: /*Src Y*/
		return tgui->accel.src_y >> 8;

		case 0x40: /*Size X*/
		return tgui->accel.size_x & 0xff;
		case 0x41: /*Size X*/
		return tgui->accel.size_x >> 8;
		case 0x42: /*Size Y*/
		return tgui->accel.size_y & 0xff;
		case 0x43: /*Size Y*/
		return tgui->accel.size_y >> 8;
		
		case 0x80: case 0x81: case 0x82: case 0x83:
		case 0x84: case 0x85: case 0x86: case 0x87:
		case 0x88: case 0x89: case 0x8a: case 0x8b:
		case 0x8c: case 0x8d: case 0x8e: case 0x8f:
		case 0x90: case 0x91: case 0x92: case 0x93:
		case 0x94: case 0x95: case 0x96: case 0x97:
		case 0x98: case 0x99: case 0x9a: case 0x9b:
		case 0x9c: case 0x9d: case 0x9e: case 0x9f:
		case 0xa0: case 0xa1: case 0xa2: case 0xa3:
		case 0xa4: case 0xa5: case 0xa6: case 0xa7:
		case 0xa8: case 0xa9: case 0xaa: case 0xab:
		case 0xac: case 0xad: case 0xae: case 0xaf:
		case 0xb0: case 0xb1: case 0xb2: case 0xb3:
		case 0xb4: case 0xb5: case 0xb6: case 0xb7:
		case 0xb8: case 0xb9: case 0xba: case 0xbb:
		case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		case 0xc0: case 0xc1: case 0xc2: case 0xc3:
		case 0xc4: case 0xc5: case 0xc6: case 0xc7:
		case 0xc8: case 0xc9: case 0xca: case 0xcb:
		case 0xcc: case 0xcd: case 0xce: case 0xcf:
		case 0xd0: case 0xd1: case 0xd2: case 0xd3:
		case 0xd4: case 0xd5: case 0xd6: case 0xd7:
		case 0xd8: case 0xd9: case 0xda: case 0xdb:
		case 0xdc: case 0xdd: case 0xde: case 0xdf:
		case 0xe0: case 0xe1: case 0xe2: case 0xe3:
		case 0xe4: case 0xe5: case 0xe6: case 0xe7:
		case 0xe8: case 0xe9: case 0xea: case 0xeb:
		case 0xec: case 0xed: case 0xee: case 0xef:
		case 0xf0: case 0xf1: case 0xf2: case 0xf3:
		case 0xf4: case 0xf5: case 0xf6: case 0xf7:
		case 0xf8: case 0xf9: case 0xfa: case 0xfb:
		case 0xfc: case 0xfd: case 0xfe: case 0xff:
		return tgui->accel.pattern[addr & 0x7f];
	}
	return 0xff;
}

uint16_t tgui_accel_read_w(uint32_t addr, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
	return tgui_accel_read(addr, tgui) | (tgui_accel_read(addr + 1, tgui) << 8);
}

uint32_t tgui_accel_read_l(uint32_t addr, void *p)
{
        tgui_t *tgui = (tgui_t *)p;
	return tgui_accel_read_w(addr, tgui) | (tgui_accel_read_w(addr + 2, tgui) << 16);
}

void tgui_accel_write_fb_b(uint32_t addr, uint8_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        tgui_t *tgui = (tgui_t *)svga->p;
	tgui_queue(tgui, addr, val, FIFO_WRITE_FB_BYTE);
}

void tgui_accel_write_fb_w(uint32_t addr, uint16_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        tgui_t *tgui = (tgui_t *)svga->p;
	tgui_queue(tgui, addr, val, FIFO_WRITE_FB_WORD);
}

void tgui_accel_write_fb_l(uint32_t addr, uint32_t val, void *p)
{
        svga_t *svga = (svga_t *)p;
        tgui_t *tgui = (tgui_t *)svga->p;
	tgui_queue(tgui, addr, val, FIFO_WRITE_FB_LONG);
}

void tgui_add_status_info(char *s, int max_len, void *p)
{
        tgui_t *tgui = (tgui_t *)p;        
        char temps[256];
        uint64_t new_time = timer_read();
        uint64_t status_diff = new_time - tgui->status_time;
        tgui->status_time = new_time;
        
        svga_add_status_info(s, max_len, &tgui->svga);

        sprintf(temps, "%f%% CPU\n%f%% CPU (real)\n\n", ((double)tgui->blitter_time * 100.0) / timer_freq, ((double)tgui->blitter_time * 100.0) / status_diff);
        strncat(s, temps, max_len);

        tgui->blitter_time = 0;
}

static device_config_t tgui9440_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 2,
                {
                        {
                                "1 MB", 1
                        },
                        {
                                "2 MB", 2
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

device_t tgui9440_device =
{
        "Trident TGUI 9440",
        0,
        tgui9440_init,
        tgui_close,
        tgui9440_available,
        tgui_speed_changed,
        tgui_force_redraw,
        tgui_add_status_info,
        tgui9440_config
};
