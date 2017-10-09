/*ET4000/W32p emulation (Diamond Stealth 32)*/
/*Known bugs :

  - Accelerator doesn't work in planar modes
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../pci.h"
#include "../rom.h"
#include "../device.h"
#include "../win/plat_thread.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_icd2061.h"
#include "vid_stg_ramdac.h"


#define FIFO_SIZE 65536
#define FIFO_MASK (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE (1 << 31)

#define FIFO_ENTRIES (et4000->fifo_write_idx - et4000->fifo_read_idx)
#define FIFO_FULL    ((et4000->fifo_write_idx - et4000->fifo_read_idx) >= (FIFO_SIZE-1))
#define FIFO_EMPTY   (et4000->fifo_read_idx == et4000->fifo_write_idx)

#define FIFO_TYPE 0xff000000
#define FIFO_ADDR 0x00ffffff

enum
{
        FIFO_INVALID    = (0x00 << 24),
        FIFO_WRITE_BYTE = (0x01 << 24),
        FIFO_WRITE_MMU  = (0x02 << 24)
};

typedef struct
{
        uint32_t addr_type;
        uint32_t val;
} fifo_entry_t;

typedef struct et4000w32p_t
{
        mem_mapping_t linear_mapping;
        mem_mapping_t    mmu_mapping;
        
        rom_t bios_rom;
        
        svga_t svga;
        stg_ramdac_t ramdac;
        icd2061_t icd2061;

        int index;
	int pci;
        uint8_t regs[256];
        uint32_t linearbase, linearbase_old;

        uint8_t banking, banking2;

        uint8_t pci_regs[256];
        
        int interleaved;

        /*Accelerator*/
        struct
        {
                struct
                {
                        uint32_t pattern_addr,source_addr,dest_addr,mix_addr;
                        uint16_t pattern_off,source_off,dest_off,mix_off;
                        uint8_t pixel_depth,xy_dir;
                        uint8_t pattern_wrap,source_wrap;
                        uint16_t count_x,count_y;
                        uint8_t ctrl_routing,ctrl_reload;
                        uint8_t rop_fg,rop_bg;
                        uint16_t pos_x,pos_y;
                        uint16_t error;
                        uint16_t dmin,dmaj;
                } queued,internal;
                uint32_t pattern_addr,source_addr,dest_addr,mix_addr;
                uint32_t pattern_back,source_back,dest_back,mix_back;
                int pattern_x,source_x;
                int pattern_x_back,source_x_back;
                int pattern_y,source_y;
                uint8_t status;
                uint64_t cpu_dat;
                int cpu_dat_pos;
                int pix_pos;
        } acl;

        struct
        {
                uint32_t base[3];
                uint8_t ctrl;
        } mmu;

        fifo_entry_t fifo[FIFO_SIZE];
        volatile int fifo_read_idx, fifo_write_idx;

        thread_t *fifo_thread;
        event_t *wake_fifo_thread;
        event_t *fifo_not_full_event;
        
        int blitter_busy;
        uint64_t blitter_time;
        uint64_t status_time;
} et4000w32p_t;

void et4000w32p_recalcmapping(et4000w32p_t *et4000);

uint8_t et4000w32p_mmu_read(uint32_t addr, void *p);
void et4000w32p_mmu_write(uint32_t addr, uint8_t val, void *p);

void et4000w32_blit_start(et4000w32p_t *et4000);
void et4000w32_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input, et4000w32p_t *et4000);

void et4000w32p_out(uint16_t addr, uint8_t val, void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;
        svga_t *svga = &et4000->svga;
        uint8_t old;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3c2:
                icd2061_write(&et4000->icd2061, (val >> 2) & 3);
                break;
                
                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                stg_ramdac_out(addr, val, &et4000->ramdac, svga);
                return;
                
                case 0x3CB: /*Banking extension*/
                svga->write_bank = (svga->write_bank & 0xfffff) | ((val & 1) << 20);
                svga->read_bank  = (svga->read_bank  & 0xfffff) | ((val & 0x10) << 16);
                et4000->banking2 = val;
                return;
                case 0x3CD: /*Banking*/
                svga->write_bank = (svga->write_bank & 0x100000) | ((val & 0xf) * 65536);
                svga->read_bank  = (svga->read_bank  & 0x100000) | (((val >> 4) & 0xf) * 65536);
                et4000->banking = val;
                return;
                case 0x3CF:
                switch (svga->gdcaddr & 15)
                {
                        case 6:
                        svga->gdcreg[svga->gdcaddr & 15] = val;
                        et4000w32p_recalcmapping(et4000);
                        return;
                }
                break;
                case 0x3D4:
                svga->crtcreg = val & 63;
                return;
                case 0x3D5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;
                if (old != val)
                {
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                if (svga->crtcreg == 0x30)
                {
			if (et4000->pci)
			{
				et4000->linearbase &= 0xc0000000;
				et4000->linearbase = (val & 0xfc) << 22;
			}
			else
			{
				et4000->linearbase = val << 22;
			}
                        et4000w32p_recalcmapping(et4000);
                }
                if (svga->crtcreg == 0x32 || svga->crtcreg == 0x36)
                        et4000w32p_recalcmapping(et4000);
                break;

                case 0x210A: case 0x211A: case 0x212A: case 0x213A:
                case 0x214A: case 0x215A: case 0x216A: case 0x217A:
                et4000->index=val;
                return;
                case 0x210B: case 0x211B: case 0x212B: case 0x213B:
                case 0x214B: case 0x215B: case 0x216B: case 0x217B:
                et4000->regs[et4000->index] = val;
                svga->hwcursor.x     = et4000->regs[0xE0] | ((et4000->regs[0xE1] & 7) << 8);
                svga->hwcursor.y     = et4000->regs[0xE4] | ((et4000->regs[0xE5] & 7) << 8);
                svga->hwcursor.addr  = (et4000->regs[0xE8] | (et4000->regs[0xE9] << 8) | ((et4000->regs[0xEA] & 7) << 16)) << 2;
                svga->hwcursor.addr += (et4000->regs[0xE6] & 63) * 16;
                svga->hwcursor.ena   = et4000->regs[0xF7] & 0x80;
                svga->hwcursor.xoff  = et4000->regs[0xE2] & 63;
                svga->hwcursor.yoff  = et4000->regs[0xE6] & 63;
                return;

        }
        svga_out(addr, val, svga);
}

uint8_t et4000w32p_in(uint16_t addr, void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;
        svga_t *svga = &et4000->svga;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3c5:
                if ((svga->seqaddr & 0xf) == 7) 
                        return svga->seqregs[svga->seqaddr & 0xf] | 4;
                break;

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                return stg_ramdac_in(addr, &et4000->ramdac, svga);

                case 0x3CB:
                return et4000->banking2;
                case 0x3CD:
                return et4000->banking;
                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                return svga->crtc[svga->crtcreg];

                case 0x210A: case 0x211A: case 0x212A: case 0x213A:
                case 0x214A: case 0x215A: case 0x216A: case 0x217A:
                return et4000->index;
                case 0x210B: case 0x211B: case 0x212B: case 0x213B:
                case 0x214B: case 0x215B: case 0x216B: case 0x217B:
                if (et4000->index==0xec) 
                        return (et4000->regs[0xec] & 0xf) | 0x60; /*ET4000/W32p rev D*/
                if (et4000->index == 0xef) 
                {
                        if (et4000->pci) return et4000->regs[0xef] | 0xe0;       /*PCI*/
                        else             return et4000->regs[0xef] | 0x60;       /*VESA local bus*/
                }
                return et4000->regs[et4000->index];
        }
        return svga_in(addr, svga);
}

void et4000w32p_recalctimings(svga_t *svga)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)svga->p;
        svga->ma_latch |= (svga->crtc[0x33] & 0x7) << 16;
        if (svga->crtc[0x35] & 0x01)     svga->vblankstart += 0x400;
        if (svga->crtc[0x35] & 0x02)     svga->vtotal      += 0x400;
        if (svga->crtc[0x35] & 0x04)     svga->dispend     += 0x400;
        if (svga->crtc[0x35] & 0x08)     svga->vsyncstart  += 0x400;
        if (svga->crtc[0x35] & 0x10)     svga->split       += 0x400;
        if (svga->crtc[0x3F] & 0x80)     svga->rowoffset   += 0x100;
        if (svga->crtc[0x3F] & 0x01)     svga->htotal      += 256;
        if (svga->attrregs[0x16] & 0x20) svga->hdisp <<= 1;
        
        switch ((svga->miscout >> 2) & 3)
        {
                case 0: case 1: break;
                case 2: case 3: svga->clock = cpuclock / icd2061_getfreq(&et4000->icd2061, 2); break;
        }
        
        switch (svga->bpp)
        {
                case 15: case 16:
                svga->hdisp >>= 1;
                break;
                case 24:
                svga->hdisp /= 3;
                break;
        }
}

void et4000w32p_recalcmapping(et4000w32p_t *et4000)
{
        svga_t *svga = &et4000->svga;
        
        if (!(et4000->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))
        {
                mem_mapping_disable(&svga->mapping);
                mem_mapping_disable(&et4000->linear_mapping);
                mem_mapping_disable(&et4000->mmu_mapping);
                return;
        }

        if (svga->crtc[0x36] & 0x10) /*Linear frame buffer*/
        {
                mem_mapping_set_addr(&et4000->linear_mapping, et4000->linearbase, 0x200000);
		svga->linear_base = et4000->linearbase;
                mem_mapping_disable(&svga->mapping);
                mem_mapping_disable(&et4000->mmu_mapping);
        }
        else
        {
                int map = (svga->gdcreg[6] & 0xc) >> 2;
                if (svga->crtc[0x36] & 0x20) map |= 4;
                if (svga->crtc[0x36] & 0x08) map |= 8;
                switch (map)
                {
                        case 0x0: case 0x4: case 0x8: case 0xC: /*128k at A0000*/
                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
                        mem_mapping_disable(&et4000->mmu_mapping);
                        svga->banked_mask = 0xffff;
                        break;
                        case 0x1: /*64k at A0000*/
                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                        mem_mapping_disable(&et4000->mmu_mapping);
                        svga->banked_mask = 0xffff;
                        break;
                        case 0x2: /*32k at B0000*/
                        mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                        mem_mapping_disable(&et4000->mmu_mapping);
                        svga->banked_mask = 0x7fff;
                        break;
                        case 0x3: /*32k at B8000*/
                        mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                        mem_mapping_disable(&et4000->mmu_mapping);
                        svga->banked_mask = 0x7fff;
                        break;
                        case 0x5: case 0x9: case 0xD: /*64k at A0000, MMU at B8000*/
                        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
                        mem_mapping_set_addr(&et4000->mmu_mapping, 0xb8000, 0x08000);
                        svga->banked_mask = 0xffff;
                        break;
                        case 0x6: case 0xA: case 0xE: /*32k at B0000, MMU at A8000*/
                        mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
                        mem_mapping_set_addr(&et4000->mmu_mapping, 0xa8000, 0x08000);
                        svga->banked_mask = 0x7fff;
                        break;
                        case 0x7: case 0xB: case 0xF: /*32k at B8000, MMU at A8000*/
                        mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
                        mem_mapping_set_addr(&et4000->mmu_mapping, 0xa8000, 0x08000);
                        svga->banked_mask = 0x7fff;
                        break;
                }
                
                mem_mapping_disable(&et4000->linear_mapping);
        }
        et4000->linearbase_old = et4000->linearbase;
        
        if (!et4000->interleaved && (et4000->svga.crtc[0x32] & 0x80))
                mem_mapping_disable(&svga->mapping);
}

#define ACL_WRST 1
#define ACL_RDST 2
#define ACL_XYST 4
#define ACL_SSO  8

static void et4000w32p_accel_write_fifo(et4000w32p_t *et4000, uint32_t addr, uint8_t val)
{
        switch (addr & 0x7fff)
        {
                case 0x7f80: et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0xFFFFFF00) | val;         break;
                case 0x7f81: et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0xFFFF00FF) | (val << 8);  break;
                case 0x7f82: et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0xFF00FFFF) | (val << 16); break;
                case 0x7f83: et4000->acl.queued.pattern_addr = (et4000->acl.queued.pattern_addr & 0x00FFFFFF) | (val << 24); break;
                case 0x7f84: et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0xFFFFFF00) | val;         break;
                case 0x7f85: et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0xFFFF00FF) | (val << 8);  break;
                case 0x7f86: et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0xFF00FFFF) | (val << 16); break;
                case 0x7f87: et4000->acl.queued.source_addr  = (et4000->acl.queued.source_addr  & 0x00FFFFFF) | (val << 24); break;
                case 0x7f88: et4000->acl.queued.pattern_off  = (et4000->acl.queued.pattern_off  & 0xFF00) | val;        break;
                case 0x7f89: et4000->acl.queued.pattern_off  = (et4000->acl.queued.pattern_off  & 0x00FF) | (val << 8); break;
                case 0x7f8a: et4000->acl.queued.source_off   = (et4000->acl.queued.source_off   & 0xFF00) | val;        break;
                case 0x7f8b: et4000->acl.queued.source_off   = (et4000->acl.queued.source_off   & 0x00FF) | (val << 8); break;
                case 0x7f8c: et4000->acl.queued.dest_off     = (et4000->acl.queued.dest_off     & 0xFF00) | val;        break;
                case 0x7f8d: et4000->acl.queued.dest_off     = (et4000->acl.queued.dest_off     & 0x00FF) | (val << 8); break;
                case 0x7f8e: et4000->acl.queued.pixel_depth = val; break;
                case 0x7f8f: et4000->acl.queued.xy_dir = val; break;
                case 0x7f90: et4000->acl.queued.pattern_wrap = val; break;
                case 0x7f92: et4000->acl.queued.source_wrap = val; break;
                case 0x7f98: et4000->acl.queued.count_x    = (et4000->acl.queued.count_x & 0xFF00) | val;        break;
                case 0x7f99: et4000->acl.queued.count_x    = (et4000->acl.queued.count_x & 0x00FF) | (val << 8); break;
                case 0x7f9a: et4000->acl.queued.count_y    = (et4000->acl.queued.count_y & 0xFF00) | val;        break;
                case 0x7f9b: et4000->acl.queued.count_y    = (et4000->acl.queued.count_y & 0x00FF) | (val << 8); break;
                case 0x7f9c: et4000->acl.queued.ctrl_routing = val; break;
                case 0x7f9d: et4000->acl.queued.ctrl_reload  = val; break;
                case 0x7f9e: et4000->acl.queued.rop_bg       = val; break;
                case 0x7f9f: et4000->acl.queued.rop_fg       = val; break;
                case 0x7fa0: et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0xFFFFFF00) | val;         break;
                case 0x7fa1: et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0xFFFF00FF) | (val << 8);  break;
                case 0x7fa2: et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0xFF00FFFF) | (val << 16); break;
                case 0x7fa3: et4000->acl.queued.dest_addr = (et4000->acl.queued.dest_addr & 0x00FFFFFF) | (val << 24);
                et4000->acl.internal = et4000->acl.queued;
                et4000w32_blit_start(et4000);
                if (!(et4000->acl.queued.ctrl_routing & 0x43))
                {
                        et4000w32_blit(0xFFFFFF, ~0, 0, 0, et4000);
                }
                if ((et4000->acl.queued.ctrl_routing & 0x40) && !(et4000->acl.internal.ctrl_routing & 3))
                        et4000w32_blit(4, ~0, 0, 0, et4000);
                break;
                case 0x7fa4: et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFFFFFF00) | val;         break;
                case 0x7fa5: et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFFFF00FF) | (val << 8);  break;
                case 0x7fa6: et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0xFF00FFFF) | (val << 16); break;
                case 0x7fa7: et4000->acl.queued.mix_addr = (et4000->acl.queued.mix_addr & 0x00FFFFFF) | (val << 24); break;
                case 0x7fa8: et4000->acl.queued.mix_off = (et4000->acl.queued.mix_off & 0xFF00) | val;        break;
                case 0x7fa9: et4000->acl.queued.mix_off = (et4000->acl.queued.mix_off & 0x00FF) | (val << 8); break;
                case 0x7faa: et4000->acl.queued.error   = (et4000->acl.queued.error   & 0xFF00) | val;        break;
                case 0x7fab: et4000->acl.queued.error   = (et4000->acl.queued.error   & 0x00FF) | (val << 8); break;
                case 0x7fac: et4000->acl.queued.dmin    = (et4000->acl.queued.dmin    & 0xFF00) | val;        break;
                case 0x7fad: et4000->acl.queued.dmin    = (et4000->acl.queued.dmin    & 0x00FF) | (val << 8); break;
                case 0x7fae: et4000->acl.queued.dmaj    = (et4000->acl.queued.dmaj    & 0xFF00) | val;        break;
                case 0x7faf: et4000->acl.queued.dmaj    = (et4000->acl.queued.dmaj    & 0x00FF) | (val << 8); break;
        }
}

static void et4000w32p_accel_write_mmu(et4000w32p_t *et4000, uint32_t addr, uint8_t val)
{
        if (!(et4000->acl.status & ACL_XYST)) return;
        if (et4000->acl.internal.ctrl_routing & 3)
        {
                if ((et4000->acl.internal.ctrl_routing & 3) == 2)
                {
                        if (et4000->acl.mix_addr & 7)
                                et4000w32_blit(8 - (et4000->acl.mix_addr & 7), val >> (et4000->acl.mix_addr & 7), 0, 1, et4000);
                        else
                                et4000w32_blit(8, val, 0, 1, et4000);
                }
                else if ((et4000->acl.internal.ctrl_routing & 3) == 1)
                                et4000w32_blit(1, ~0, val, 2, et4000);
        }
}

static void fifo_thread(void *param)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)param;

	uint64_t start_time = 0;
	uint64_t end_time = 0;

	fifo_entry_t *fifo;
        
        while (1)
        {
                thread_set_event(et4000->fifo_not_full_event);
                thread_wait_event(et4000->wake_fifo_thread, -1);
                thread_reset_event(et4000->wake_fifo_thread);
                et4000->blitter_busy = 1;
                while (!FIFO_EMPTY)
                {
                        start_time = timer_read();
                        fifo = &et4000->fifo[et4000->fifo_read_idx & FIFO_MASK];

                        switch (fifo->addr_type & FIFO_TYPE)
                        {
                                case FIFO_WRITE_BYTE:
                                et4000w32p_accel_write_fifo(et4000, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                                case FIFO_WRITE_MMU:
                                et4000w32p_accel_write_mmu(et4000, fifo->addr_type & FIFO_ADDR, fifo->val);
                                break;
                        }
                                                
                        et4000->fifo_read_idx++;
                        fifo->addr_type = FIFO_INVALID;

                        if (FIFO_ENTRIES > 0xe000)
                                thread_set_event(et4000->fifo_not_full_event);

                        end_time = timer_read();
                        et4000->blitter_time += end_time - start_time;
                }
                et4000->blitter_busy = 0;
        }
}

static __inline void wake_fifo_thread(et4000w32p_t *et4000)
{
        thread_set_event(et4000->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

static void et4000w32p_wait_fifo_idle(et4000w32p_t *et4000)
{
        while (!FIFO_EMPTY)
        {
                wake_fifo_thread(et4000);
                thread_wait_event(et4000->fifo_not_full_event, 1);
        }
}

static void et4000w32p_queue(et4000w32p_t *et4000, uint32_t addr, uint32_t val, uint32_t type)
{
        fifo_entry_t *fifo = &et4000->fifo[et4000->fifo_write_idx & FIFO_MASK];

        if (FIFO_FULL)
        {
                thread_reset_event(et4000->fifo_not_full_event);
                if (FIFO_FULL)
                {
                        thread_wait_event(et4000->fifo_not_full_event, -1); /*Wait for room in ringbuffer*/
                }
        }

        fifo->val = val;
        fifo->addr_type = (addr & FIFO_ADDR) | type;

        et4000->fifo_write_idx++;
        
        if (FIFO_ENTRIES > 0xe000 || FIFO_ENTRIES < 8)
                wake_fifo_thread(et4000);
}

void et4000w32p_mmu_write(uint32_t addr, uint8_t val, void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;
        svga_t *svga = &et4000->svga;
        int bank;
        switch (addr & 0x6000)
        {
                case 0x0000: /*MMU 0*/
                case 0x2000: /*MMU 1*/
                case 0x4000: /*MMU 2*/
                bank = (addr >> 13) & 3;
                if (et4000->mmu.ctrl & (1 << bank))
                {
                        et4000w32p_queue(et4000, addr & 0x7fff, val, FIFO_WRITE_MMU);
                }
                else
                {
                        if ((addr&0x1fff) + et4000->mmu.base[bank] < svga->vram_limit)
                        {
                                svga->vram[(addr & 0x1fff) + et4000->mmu.base[bank]] = val;
                                svga->changedvram[((addr & 0x1fff) + et4000->mmu.base[bank]) >> 12] = changeframecount;
                        }
                }
                break;
                case 0x6000:
                if ((addr & 0x7fff) >= 0x7f80)
                {
                        et4000w32p_queue(et4000, addr & 0x7fff, val, FIFO_WRITE_BYTE);
                }
                else switch (addr & 0x7fff)
                {
                        case 0x7f00: et4000->mmu.base[0] = (et4000->mmu.base[0] & 0xFFFFFF00) | val;         break;
                        case 0x7f01: et4000->mmu.base[0] = (et4000->mmu.base[0] & 0xFFFF00FF) | (val << 8);  break;
                        case 0x7f02: et4000->mmu.base[0] = (et4000->mmu.base[0] & 0xFF00FFFF) | (val << 16); break;
                        case 0x7f03: et4000->mmu.base[0] = (et4000->mmu.base[0] & 0x00FFFFFF) | (val << 24); break;
                        case 0x7f04: et4000->mmu.base[1] = (et4000->mmu.base[1] & 0xFFFFFF00) | val;         break;
                        case 0x7f05: et4000->mmu.base[1] = (et4000->mmu.base[1] & 0xFFFF00FF) | (val << 8);  break;
                        case 0x7f06: et4000->mmu.base[1] = (et4000->mmu.base[1] & 0xFF00FFFF) | (val << 16); break;
                        case 0x7f07: et4000->mmu.base[1] = (et4000->mmu.base[1] & 0x00FFFFFF) | (val << 24); break;
                        case 0x7f08: et4000->mmu.base[2] = (et4000->mmu.base[2] & 0xFFFFFF00) | val;         break;
                        case 0x7f09: et4000->mmu.base[2] = (et4000->mmu.base[2] & 0xFFFF00FF) | (val << 8);  break;
                        case 0x7f0a: et4000->mmu.base[2] = (et4000->mmu.base[2] & 0xFF00FFFF) | (val << 16); break;
                        case 0x7f0d: et4000->mmu.base[2] = (et4000->mmu.base[2] & 0x00FFFFFF) | (val << 24); break;
                        case 0x7f13: et4000->mmu.ctrl=val; break;
                }
                break;
        }
}

uint8_t et4000w32p_mmu_read(uint32_t addr, void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;
        svga_t *svga = &et4000->svga;
        int bank;
        uint8_t temp;
        switch (addr & 0x6000)
        {
                case 0x0000: /*MMU 0*/
                case 0x2000: /*MMU 1*/
                case 0x4000: /*MMU 2*/
                bank = (addr >> 13) & 3;
                if (et4000->mmu.ctrl & (1 << bank))
                {
                        et4000w32p_wait_fifo_idle(et4000);
                        temp = 0xff;
                        if (et4000->acl.cpu_dat_pos)
                        {
                                et4000->acl.cpu_dat_pos--;
                                temp = et4000->acl.cpu_dat & 0xff;
                                et4000->acl.cpu_dat >>= 8;
                        }
                        if ((et4000->acl.queued.ctrl_routing & 0x40) && !et4000->acl.cpu_dat_pos && !(et4000->acl.internal.ctrl_routing & 3))
                           et4000w32_blit(4, ~0, 0, 0, et4000);
                        /*???*/
                        return temp;
                }
                if ((addr&0x1fff) + et4000->mmu.base[bank] >= svga->vram_limit)
                        return 0xff;
                return svga->vram[(addr&0x1fff) + et4000->mmu.base[bank]];
                
                case 0x6000:
                if ((addr & 0x7fff) >= 0x7f80)
                        et4000w32p_wait_fifo_idle(et4000);
                switch (addr&0x7fff)
                {
                        case 0x7f00: return et4000->mmu.base[0];
                        case 0x7f01: return et4000->mmu.base[0] >> 8;
                        case 0x7f02: return et4000->mmu.base[0] >> 16;
                        case 0x7f03: return et4000->mmu.base[0] >> 24;
                        case 0x7f04: return et4000->mmu.base[1];
                        case 0x7f05: return et4000->mmu.base[1] >> 8;
                        case 0x7f06: return et4000->mmu.base[1] >> 16;
                        case 0x7f07: return et4000->mmu.base[1] >> 24;
                        case 0x7f08: return et4000->mmu.base[2];
                        case 0x7f09: return et4000->mmu.base[2] >> 8;
                        case 0x7f0a: return et4000->mmu.base[2] >> 16;
                        case 0x7f0b: return et4000->mmu.base[2] >> 24;
                        case 0x7f13: return et4000->mmu.ctrl;

                        case 0x7f36:
                        temp = et4000->acl.status;
                        temp &= ~0x03;
                        if (!FIFO_EMPTY)
                                temp |= 0x02;
                        if (FIFO_FULL)
                                temp |= 0x01;
                        return temp;
                        case 0x7f80: return et4000->acl.internal.pattern_addr;
                        case 0x7f81: return et4000->acl.internal.pattern_addr >> 8;
                        case 0x7f82: return et4000->acl.internal.pattern_addr >> 16;
                        case 0x7f83: return et4000->acl.internal.pattern_addr >> 24;
                        case 0x7f84: return et4000->acl.internal.source_addr;
                        case 0x7f85: return et4000->acl.internal.source_addr >> 8;
                        case 0x7f86: return et4000->acl.internal.source_addr >> 16;
                        case 0x7f87: return et4000->acl.internal.source_addr >> 24;
                        case 0x7f88: return et4000->acl.internal.pattern_off;
                        case 0x7f89: return et4000->acl.internal.pattern_off >> 8;
                        case 0x7f8a: return et4000->acl.internal.source_off;
                        case 0x7f8b: return et4000->acl.internal.source_off >> 8;
                        case 0x7f8c: return et4000->acl.internal.dest_off;
                        case 0x7f8d: return et4000->acl.internal.dest_off >> 8;
                        case 0x7f8e: return et4000->acl.internal.pixel_depth;
                        case 0x7f8f: return et4000->acl.internal.xy_dir;
                        case 0x7f90: return et4000->acl.internal.pattern_wrap;
                        case 0x7f92: return et4000->acl.internal.source_wrap;
                        case 0x7f98: return et4000->acl.internal.count_x;
                        case 0x7f99: return et4000->acl.internal.count_x >> 8;
                        case 0x7f9a: return et4000->acl.internal.count_y;
                        case 0x7f9b: return et4000->acl.internal.count_y >> 8;
                        case 0x7f9c: return et4000->acl.internal.ctrl_routing;
                        case 0x7f9d: return et4000->acl.internal.ctrl_reload;
                        case 0x7f9e: return et4000->acl.internal.rop_bg;
                        case 0x7f9f: return et4000->acl.internal.rop_fg;
                        case 0x7fa0: return et4000->acl.internal.dest_addr;
                        case 0x7fa1: return et4000->acl.internal.dest_addr >> 8;
                        case 0x7fa2: return et4000->acl.internal.dest_addr >> 16;
                        case 0x7fa3: return et4000->acl.internal.dest_addr >> 24;
                }
                return 0xff;
        }
        return 0xff;
}

static int et4000w32_max_x[8]={0,0,4,8,16,32,64,0x70000000};
static int et4000w32_wrap_x[8]={0,0,3,7,15,31,63,0xFFFFFFFF};
static int et4000w32_wrap_y[8]={1,2,4,8,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF};

int bltout=0;
void et4000w32_blit_start(et4000w32p_t *et4000)
{
        if (!(et4000->acl.queued.xy_dir & 0x20))
           et4000->acl.internal.error = et4000->acl.internal.dmaj / 2;
        et4000->acl.pattern_addr= et4000->acl.internal.pattern_addr;
        et4000->acl.source_addr = et4000->acl.internal.source_addr;
        et4000->acl.mix_addr    = et4000->acl.internal.mix_addr;
        et4000->acl.mix_back    = et4000->acl.mix_addr;
        et4000->acl.dest_addr   = et4000->acl.internal.dest_addr;
        et4000->acl.dest_back   = et4000->acl.dest_addr;
        et4000->acl.internal.pos_x = et4000->acl.internal.pos_y = 0;
        et4000->acl.pattern_x = et4000->acl.source_x = et4000->acl.pattern_y = et4000->acl.source_y = 0;
        et4000->acl.status |= ACL_XYST;
        if ((!(et4000->acl.internal.ctrl_routing & 7) || (et4000->acl.internal.ctrl_routing & 4)) && !(et4000->acl.internal.ctrl_routing & 0x40)) 
                et4000->acl.status |= ACL_SSO;
        
        if (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7])
        {
                et4000->acl.pattern_x = et4000->acl.pattern_addr & et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
                et4000->acl.pattern_addr &= ~et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7];
        }
        et4000->acl.pattern_back = et4000->acl.pattern_addr;
        if (!(et4000->acl.internal.pattern_wrap & 0x40))
        {
                et4000->acl.pattern_y = (et4000->acl.pattern_addr / (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1);
                et4000->acl.pattern_back &= ~(((et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] + 1) * et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7]) - 1);
        }
        et4000->acl.pattern_x_back = et4000->acl.pattern_x;
        
        if (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7])
        {
                et4000->acl.source_x = et4000->acl.source_addr & et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7];
                et4000->acl.source_addr &= ~et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7];
        }
        et4000->acl.source_back = et4000->acl.source_addr;
        if (!(et4000->acl.internal.source_wrap & 0x40))
        {
                et4000->acl.source_y = (et4000->acl.source_addr / (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1)) & (et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1);
                et4000->acl.source_back &= ~(((et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] + 1) * et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7]) - 1);
        }
        et4000->acl.source_x_back = et4000->acl.source_x;

        et4000w32_max_x[2] = ((et4000->acl.internal.pixel_depth & 0x30) == 0x20) ? 3 : 4;
        
        et4000->acl.internal.count_x += (et4000->acl.internal.pixel_depth >> 4) & 3;
        et4000->acl.cpu_dat_pos = 0;
        et4000->acl.cpu_dat = 0;
        
        et4000->acl.pix_pos = 0;
}

void et4000w32_incx(int c, et4000w32p_t *et4000)
{
        et4000->acl.dest_addr += c;
        et4000->acl.pattern_x += c;
        et4000->acl.source_x  += c;
        et4000->acl.mix_addr  += c;
        if (et4000->acl.pattern_x >= et4000w32_max_x[et4000->acl.internal.pattern_wrap & 7])
           et4000->acl.pattern_x  -= et4000w32_max_x[et4000->acl.internal.pattern_wrap & 7];
        if (et4000->acl.source_x  >= et4000w32_max_x[et4000->acl.internal.source_wrap  & 7])
           et4000->acl.source_x   -= et4000w32_max_x[et4000->acl.internal.source_wrap  & 7];
}
void et4000w32_decx(int c, et4000w32p_t *et4000)
{
        et4000->acl.dest_addr -= c;
        et4000->acl.pattern_x -= c;
        et4000->acl.source_x  -= c;
        et4000->acl.mix_addr  -= c;
        if (et4000->acl.pattern_x < 0)
           et4000->acl.pattern_x  += et4000w32_max_x[et4000->acl.internal.pattern_wrap & 7];
        if (et4000->acl.source_x  < 0)
           et4000->acl.source_x   += et4000w32_max_x[et4000->acl.internal.source_wrap  & 7];
}
void et4000w32_incy(et4000w32p_t *et4000)
{
        et4000->acl.pattern_addr += et4000->acl.internal.pattern_off + 1;
        et4000->acl.source_addr  += et4000->acl.internal.source_off  + 1;
        et4000->acl.mix_addr     += et4000->acl.internal.mix_off     + 1;
        et4000->acl.dest_addr    += et4000->acl.internal.dest_off    + 1;
        et4000->acl.pattern_y++;
        if (et4000->acl.pattern_y == et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7])
        {
                et4000->acl.pattern_y = 0;
                et4000->acl.pattern_addr = et4000->acl.pattern_back;
        }
        et4000->acl.source_y++;
        if (et4000->acl.source_y == et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7])
        {
                et4000->acl.source_y = 0;
                et4000->acl.source_addr = et4000->acl.source_back;
        }
}
void et4000w32_decy(et4000w32p_t *et4000)
{
        et4000->acl.pattern_addr -= et4000->acl.internal.pattern_off + 1;
        et4000->acl.source_addr  -= et4000->acl.internal.source_off  + 1;
        et4000->acl.mix_addr     -= et4000->acl.internal.mix_off     + 1;
        et4000->acl.dest_addr    -= et4000->acl.internal.dest_off    + 1;
        et4000->acl.pattern_y--;
        if (et4000->acl.pattern_y < 0 && !(et4000->acl.internal.pattern_wrap & 0x40))
        {
                et4000->acl.pattern_y = et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1;
                et4000->acl.pattern_addr = et4000->acl.pattern_back + (et4000w32_wrap_x[et4000->acl.internal.pattern_wrap & 7] * (et4000w32_wrap_y[(et4000->acl.internal.pattern_wrap >> 4) & 7] - 1));
        }
        et4000->acl.source_y--;
        if (et4000->acl.source_y < 0 && !(et4000->acl.internal.source_wrap & 0x40))
        {
                et4000->acl.source_y = et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1;
                et4000->acl.source_addr = et4000->acl.source_back + (et4000w32_wrap_x[et4000->acl.internal.source_wrap & 7] *(et4000w32_wrap_y[(et4000->acl.internal.source_wrap >> 4) & 7] - 1));;
        }
}

void et4000w32_blit(int count, uint32_t mix, uint32_t sdat, int cpu_input, et4000w32p_t *et4000)
{
        svga_t *svga = &et4000->svga;
        int c,d;
        uint8_t pattern, source, dest, out;
        uint8_t rop;
        int mixdat;

        if (!(et4000->acl.status & ACL_XYST)) return;
        if (et4000->acl.internal.xy_dir & 0x80) /*Line draw*/
        {
                while (count--)
                {
                        if (bltout) pclog("%i,%i : ", et4000->acl.internal.pos_x, et4000->acl.internal.pos_y);
                        pattern = svga->vram[(et4000->acl.pattern_addr + et4000->acl.pattern_x) & 0x1fffff];
                        source  = svga->vram[(et4000->acl.source_addr  + et4000->acl.source_x)  & 0x1fffff];
                        if (bltout) pclog("%06X %06X ", (et4000->acl.pattern_addr + et4000->acl.pattern_x) & 0x1fffff, (et4000->acl.source_addr + et4000->acl.source_x) & 0x1fffff);
                        if (cpu_input == 2)
                        {
                                source = sdat & 0xff;
                                sdat >>= 8;
                        }
                        dest = svga->vram[et4000->acl.dest_addr & 0x1fffff];
                        out = 0;
                        if (bltout) pclog("%06X   ", et4000->acl.dest_addr);
                        if ((et4000->acl.internal.ctrl_routing & 0xa) == 8)
                        {
                                mixdat = svga->vram[(et4000->acl.mix_addr >> 3) & 0x1fffff] & (1 << (et4000->acl.mix_addr & 7));
                                if (bltout) pclog("%06X %02X  ", et4000->acl.mix_addr, svga->vram[(et4000->acl.mix_addr >> 3) & 0x1fffff]);
                        }
                        else
                        {
                                mixdat = mix & 1;
                                mix >>= 1; 
                                mix |= 0x80000000;
                        }
                        et4000->acl.mix_addr++;
                        rop = mixdat ? et4000->acl.internal.rop_fg : et4000->acl.internal.rop_bg;
                        for (c = 0; c < 8; c++)
                        {
                                d = (dest & (1 << c)) ? 1 : 0;
                                if (source & (1 << c))  d |= 2;
                                if (pattern & (1 << c)) d |= 4;
                                if (rop & (1 << d)) out |= (1 << c);
                        }
                        if (bltout) pclog("%06X = %02X\n", et4000->acl.dest_addr & 0x1fffff, out);
                        if (!(et4000->acl.internal.ctrl_routing & 0x40))
                        {
                                svga->vram[et4000->acl.dest_addr & 0x1fffff] = out;
                                svga->changedvram[(et4000->acl.dest_addr & 0x1fffff) >> 12] = changeframecount;
                        }
                        else
                        {
                                et4000->acl.cpu_dat |= ((uint64_t)out << (et4000->acl.cpu_dat_pos * 8));
                                et4000->acl.cpu_dat_pos++;
                        }
                        
                        et4000->acl.pix_pos++;
                        et4000->acl.internal.pos_x++;
                        if (et4000->acl.pix_pos <= ((et4000->acl.internal.pixel_depth >> 4) & 3))
                        {
                                if (et4000->acl.internal.xy_dir & 1) et4000w32_decx(1, et4000);
                                else                                 et4000w32_incx(1, et4000);
                        }
                        else
                        {
                                if (et4000->acl.internal.xy_dir & 1) 
                                        et4000w32_incx((et4000->acl.internal.pixel_depth >> 4) & 3, et4000);
                                else                       
                                        et4000w32_decx((et4000->acl.internal.pixel_depth >> 4) & 3, et4000);
                                et4000->acl.pix_pos = 0;
                                /*Next pixel*/
                                switch (et4000->acl.internal.xy_dir & 7)
                                {
                                        case 0: case 1: /*Y+*/
                                        et4000w32_incy(et4000);
                                        et4000->acl.internal.pos_y++;
                                        et4000->acl.internal.pos_x -= ((et4000->acl.internal.pixel_depth >> 4) & 3) + 1;
                                        break;
                                        case 2: case 3: /*Y-*/
                                        et4000w32_decy(et4000);
                                        et4000->acl.internal.pos_y++;
                                        et4000->acl.internal.pos_x -= ((et4000->acl.internal.pixel_depth >> 4) & 3) + 1;
                                        break;
                                        case 4: case 6: /*X+*/
                                        et4000w32_incx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
                                        break;
                                        case 5: case 7: /*X-*/
                                        et4000w32_decx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
                                        break;
                                }
                                et4000->acl.internal.error += et4000->acl.internal.dmin;
                                if (et4000->acl.internal.error > et4000->acl.internal.dmaj)
                                {
                                        et4000->acl.internal.error -= et4000->acl.internal.dmaj;
                                        switch (et4000->acl.internal.xy_dir & 7)
                                        {
                                                case 0: case 2: /*X+*/
                                                et4000w32_incx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
                                                et4000->acl.internal.pos_x++;
                                                break;
                                                case 1: case 3: /*X-*/
                                                et4000w32_decx(((et4000->acl.internal.pixel_depth >> 4) & 3) + 1, et4000);
                                                et4000->acl.internal.pos_x++;
                                                break;
                                                case 4: case 5: /*Y+*/
                                                et4000w32_incy(et4000);
                                                et4000->acl.internal.pos_y++;
                                                break;
                                                case 6: case 7: /*Y-*/
                                                et4000w32_decy(et4000);
                                                et4000->acl.internal.pos_y++;
                                                break;
                                        }
                                }
                                if (et4000->acl.internal.pos_x > et4000->acl.internal.count_x ||
                                    et4000->acl.internal.pos_y > et4000->acl.internal.count_y)
                                {
                                        et4000->acl.status &= ~(ACL_XYST | ACL_SSO);
                                        return;
                                }
                        }
                }
        }
        else
        {
                while (count--)
                {
                        if (bltout) pclog("%i,%i : ", et4000->acl.internal.pos_x, et4000->acl.internal.pos_y);
                        
                        pattern = svga->vram[(et4000->acl.pattern_addr + et4000->acl.pattern_x) & 0x1fffff];
                        source  = svga->vram[(et4000->acl.source_addr  + et4000->acl.source_x)  & 0x1fffff];
                        if (bltout) pclog("%i %06X %06X %02X %02X  ", et4000->acl.pattern_y, (et4000->acl.pattern_addr + et4000->acl.pattern_x) & 0x1fffff, (et4000->acl.source_addr + et4000->acl.source_x) & 0x1fffff, pattern, source);

                        if (cpu_input == 2)
                        {
                                source = sdat & 0xff;
                                sdat >>= 8;
                        }
                        dest = svga->vram[et4000->acl.dest_addr & 0x1fffff];
                        out = 0;
                        if (bltout) pclog("%06X %02X  %i %08X %08X  ", dest, et4000->acl.dest_addr, mix & 1, mix, et4000->acl.mix_addr);
                        if ((et4000->acl.internal.ctrl_routing & 0xa) == 8)
                        {
                                mixdat = svga->vram[(et4000->acl.mix_addr >> 3) & 0x1fffff] & (1 << (et4000->acl.mix_addr & 7));
                                if (bltout) pclog("%06X %02X  ", et4000->acl.mix_addr, svga->vram[(et4000->acl.mix_addr >> 3) & 0x1fffff]);
                        }
                        else
                        {
                                mixdat = mix & 1;
                                mix >>= 1; 
                                mix |= 0x80000000;
                        }

                        rop = mixdat ? et4000->acl.internal.rop_fg : et4000->acl.internal.rop_bg;
                        for (c = 0; c < 8; c++)
                        {
                                d = (dest & (1 << c)) ? 1 : 0;
                                if (source & (1 << c))  d |= 2;
                                if (pattern & (1 << c)) d |= 4;
                                if (rop & (1 << d)) out |= (1 << c);
                        }
                        if (bltout) pclog("%06X = %02X\n", et4000->acl.dest_addr & 0x1fffff, out);
                        if (!(et4000->acl.internal.ctrl_routing & 0x40))
                        {
                                svga->vram[et4000->acl.dest_addr & 0x1fffff] = out;
                                svga->changedvram[(et4000->acl.dest_addr & 0x1fffff) >> 12] = changeframecount;
                        }
                        else
                        {
                                et4000->acl.cpu_dat |= ((uint64_t)out << (et4000->acl.cpu_dat_pos * 8));
                                et4000->acl.cpu_dat_pos++;
                        }

                        if (et4000->acl.internal.xy_dir & 1) et4000w32_decx(1, et4000);
                        else                                 et4000w32_incx(1, et4000);

                        et4000->acl.internal.pos_x++;
                        if (et4000->acl.internal.pos_x > et4000->acl.internal.count_x)
                        {
                                if (et4000->acl.internal.xy_dir & 2)
                                {
                                        et4000w32_decy(et4000);
                                        et4000->acl.mix_back  = et4000->acl.mix_addr  = et4000->acl.mix_back  - (et4000->acl.internal.mix_off  + 1);
                                        et4000->acl.dest_back = et4000->acl.dest_addr = et4000->acl.dest_back - (et4000->acl.internal.dest_off + 1);
                                }
                                else
                                {
                                        et4000w32_incy(et4000);
                                        et4000->acl.mix_back  = et4000->acl.mix_addr  = et4000->acl.mix_back  + et4000->acl.internal.mix_off  + 1;
                                        et4000->acl.dest_back = et4000->acl.dest_addr = et4000->acl.dest_back + et4000->acl.internal.dest_off + 1;
                                }

                                et4000->acl.pattern_x = et4000->acl.pattern_x_back;
                                et4000->acl.source_x  = et4000->acl.source_x_back;

                                et4000->acl.internal.pos_y++;
                                et4000->acl.internal.pos_x = 0;
                                if (et4000->acl.internal.pos_y > et4000->acl.internal.count_y)
                                {
                                        et4000->acl.status &= ~(ACL_XYST | ACL_SSO);
                                        return;
                                }
                                if (cpu_input) return;
                                if (et4000->acl.internal.ctrl_routing & 0x40)
                                {
                                        if (et4000->acl.cpu_dat_pos & 3) 
                                                et4000->acl.cpu_dat_pos += 4 - (et4000->acl.cpu_dat_pos & 3);
                                        return;
                                }
                        }
                }
        }
}


void et4000w32p_hwcursor_draw(svga_t *svga, int displine)
{
        int x, offset;
        uint8_t dat;
	int y_add = (enable_overscan && !suppress_overscan) ? 16 : 0;
	int x_add = (enable_overscan && !suppress_overscan) ? 8 : 0;
        offset = svga->hwcursor_latch.xoff;

        for (x = 0; x < 64 - svga->hwcursor_latch.xoff; x += 4)
        {
                dat = svga->vram[svga->hwcursor_latch.addr + (offset >> 2)];
                if (!(dat & 2))          ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 32]  = (dat & 1) ? 0xFFFFFF : 0;
                else if ((dat & 3) == 3) ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 32] ^= 0xFFFFFF;
                dat >>= 2;
                if (!(dat & 2))          ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 33 + x_add]  = (dat & 1) ? 0xFFFFFF : 0;
                else if ((dat & 3) == 3) ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 33 + x_add] ^= 0xFFFFFF;
                dat >>= 2;
                if (!(dat & 2))          ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 34]  = (dat & 1) ? 0xFFFFFF : 0;
                else if ((dat & 3) == 3) ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 34] ^= 0xFFFFFF;
                dat >>= 2;
                if (!(dat & 2))          ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 35]  = (dat & 1) ? 0xFFFFFF : 0;
                else if ((dat & 3) == 3) ((uint32_t *)buffer32->line[displine + y_add])[svga->hwcursor_latch.x + x_add + x + 35] ^= 0xFFFFFF;
                dat >>= 2;
                offset += 4;
        }
        svga->hwcursor_latch.addr += 16;
}

static void et4000w32p_io_remove(et4000w32p_t *et4000)
{
        io_removehandler(0x03c0, 0x0020, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);

        io_removehandler(0x210A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_removehandler(0x211A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_removehandler(0x212A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_removehandler(0x213A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_removehandler(0x214A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_removehandler(0x215A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_removehandler(0x216A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_removehandler(0x217A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
}

static void et4000w32p_io_set(et4000w32p_t *et4000)
{
        et4000w32p_io_remove(et4000);
        
        io_sethandler(0x03c0, 0x0020, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);

        io_sethandler(0x210A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_sethandler(0x211A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_sethandler(0x212A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_sethandler(0x213A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_sethandler(0x214A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_sethandler(0x215A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_sethandler(0x216A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
        io_sethandler(0x217A, 0x0002, et4000w32p_in, NULL, NULL, et4000w32p_out, NULL, NULL, et4000);
}

uint8_t et4000w32p_pci_read(int func, int addr, void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;

	addr &= 0xff;

        switch (addr)
        {
                case 0x00: return 0x0c; /*Tseng Labs*/
                case 0x01: return 0x10;
                
                case 0x02: return 0x06; /*ET4000W32p Rev D*/
                case 0x03: return 0x32;
                
                case PCI_REG_COMMAND:
                return et4000->pci_regs[PCI_REG_COMMAND] | 0x80; /*Respond to IO and memory accesses*/

                case 0x07: return 1 << 1; /*Medium DEVSEL timing*/
                
                case 0x08: return 0; /*Revision ID*/
                case 0x09: return 0; /*Programming interface*/
                
                case 0x0a: return 0x00; /*Supports VGA interface, XGA compatible*/
                case 0x0b: return is_pentium ? 0x03 : 0x00;	/* This has to be done in order to make this card work with the two 486 PCI machines. */
                
                case 0x10: return 0x00; /*Linear frame buffer address*/
                case 0x11: return 0x00;
		case 0x12: return 0x00;
		case 0x13: return (et4000->linearbase >> 24);

                case 0x30: return et4000->pci_regs[0x30] & 0x01; /*BIOS ROM address*/
                case 0x31: return 0x00;
                case 0x32: return 0x00;
                case 0x33: return (et4000->pci_regs[0x33]) & 0xf0;

        }
        return 0;
}

void et4000w32p_pci_write(int func, int addr, uint8_t val, void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;
        svga_t *svga = &et4000->svga;

	addr &= 0xff;

        switch (addr)
        {
                case PCI_REG_COMMAND:
                et4000->pci_regs[PCI_REG_COMMAND] = (val & 0x23) | 0x80;
                if (val & PCI_COMMAND_IO)
                        et4000w32p_io_set(et4000);
                else
                        et4000w32p_io_remove(et4000);
                et4000w32p_recalcmapping(et4000);
                break;

                case 0x13: 
		et4000->linearbase &= 0x00c00000; 
                et4000->linearbase = (et4000->pci_regs[0x13] << 24);
		svga->crtc[0x30] &= 3;
		svga->crtc[0x30] = ((et4000->linearbase & 0x3f000000) >> 22);
                et4000w32p_recalcmapping(et4000); 
                break;

                case 0x30: case 0x31: case 0x32: case 0x33:
                et4000->pci_regs[addr] = val;
		et4000->pci_regs[0x30] = 1;
		et4000->pci_regs[0x31] = 0;
		et4000->pci_regs[0x32] = 0;
		et4000->pci_regs[0x33] &= 0xf0;
                if (et4000->pci_regs[0x30] & 0x01)
                {
                        uint32_t addr = (et4000->pci_regs[0x33] << 24);
			if (!addr)
			{
				addr = 0xC0000;
			}
                        pclog("ET4000 bios_rom enabled at %08x\n", addr);
                        mem_mapping_set_addr(&et4000->bios_rom.mapping, addr, 0x8000);
                }
                else
                {
                        pclog("ET4000 bios_rom disabled\n");
                        mem_mapping_disable(&et4000->bios_rom.mapping);
                }
                return;
        }
}

void *et4000w32p_init(device_t *info)
{
        int vram_size;
        et4000w32p_t *et4000 = malloc(sizeof(et4000w32p_t));
        memset(et4000, 0, sizeof(et4000w32p_t));

        vram_size = device_get_config_int("memory");
        
        et4000->interleaved = (vram_size == 2) ? 1 : 0;
        
        svga_init(&et4000->svga, et4000, vram_size << 20,
                   et4000w32p_recalctimings,
                   et4000w32p_in, et4000w32p_out,
                   et4000w32p_hwcursor_draw,
                   NULL); 

        rom_init(&et4000->bios_rom, L"roms/video/et4000w32/et4000w32.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
	et4000->pci = !!(info->flags & DEVICE_PCI);
        if (info->flags & DEVICE_PCI)
                mem_mapping_disable(&et4000->bios_rom.mapping);

        mem_mapping_add(&et4000->linear_mapping, 0, 0, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear, NULL, 0, &et4000->svga);
        mem_mapping_add(&et4000->mmu_mapping,    0, 0, et4000w32p_mmu_read, NULL, NULL, et4000w32p_mmu_write, NULL, NULL, NULL, 0, et4000);

        et4000w32p_io_set(et4000);
        
        if (info->flags & DEVICE_PCI)
	        pci_add_card(PCI_ADD_VIDEO, et4000w32p_pci_read, et4000w32p_pci_write, et4000);

	/* Hardwired bits: 00000000 1xx0x0xx */
	/* R/W bits:                 xx xxxx */
	/* PCem bits:                    111 */
        et4000->pci_regs[0x04] = 0x83;
        
        et4000->pci_regs[0x10] = 0x00;
        et4000->pci_regs[0x11] = 0x00;
        et4000->pci_regs[0x12] = 0xff;
        et4000->pci_regs[0x13] = 0xff;
        
        et4000->pci_regs[0x30] = 0x00;
        et4000->pci_regs[0x31] = 0x00;
        et4000->pci_regs[0x32] = 0x00;
        et4000->pci_regs[0x33] = 0xf0;
        
        et4000->wake_fifo_thread = thread_create_event();
        et4000->fifo_not_full_event = thread_create_event();
        et4000->fifo_thread = thread_create(fifo_thread, et4000);

        return et4000;
}

int et4000w32p_available(void)
{
        return rom_present(L"roms/video/et4000w32/et4000w32.bin");
}

void et4000w32p_close(void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;

        svga_close(&et4000->svga);
        
        thread_kill(et4000->fifo_thread);
        thread_destroy_event(et4000->wake_fifo_thread);
        thread_destroy_event(et4000->fifo_not_full_event);

        free(et4000);
}

void et4000w32p_speed_changed(void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;
        
        svga_recalctimings(&et4000->svga);
}

void et4000w32p_force_redraw(void *p)
{
        et4000w32p_t *et4000w32p = (et4000w32p_t *)p;

        et4000w32p->svga.fullchange = changeframecount;
}

void et4000w32p_add_status_info(char *s, int max_len, void *p)
{
        et4000w32p_t *et4000 = (et4000w32p_t *)p;
        char temps[256];
        uint64_t new_time = timer_read();
        uint64_t status_diff = new_time - et4000->status_time;
        et4000->status_time = new_time;
        
        svga_add_status_info(s, max_len, &et4000->svga);

        sprintf(temps, "%f%% CPU\n%f%% CPU (real)\n\n", ((double)et4000->blitter_time * 100.0) / timer_freq, ((double)et4000->blitter_time * 100.0) / status_diff);
        strncat(s, temps, max_len);

        et4000->blitter_time = 0;
}

static device_config_t et4000w32p_config[] =
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

device_t et4000w32p_vlb_device =
{
        "Tseng Labs ET4000/w32p VLB",
        DEVICE_VLB,
	0,
        et4000w32p_init,
        et4000w32p_close,
	NULL,
        et4000w32p_available,
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_add_status_info,
        et4000w32p_config
};

device_t et4000w32p_pci_device =
{
        "Tseng Labs ET4000/w32p PCI",
        DEVICE_PCI,
	0,
        et4000w32p_init,
        et4000w32p_close,
	NULL,
        et4000w32p_available,
        et4000w32p_speed_changed,
        et4000w32p_force_redraw,
        et4000w32p_add_status_info,
        et4000w32p_config
};
