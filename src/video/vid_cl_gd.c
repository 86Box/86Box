/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of select Cirrus Logic cards.
 *
 * Version:	@(#)vid_cl_gd.c	1.0.4	2017/11/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_cl_ramdac.h"
#include "vid_cl_gd.h"
#include "vid_cl_gd_blit.h"


#define BIOS_GD5422_PATH	L"roms/video/cirruslogic/cl5422.rom"
#define BIOS_GD5429_PATH	L"roms/video/cirruslogic/5429.vbi"
#define BIOS_GD5430_PATH	L"roms/video/cirruslogic/pci.bin"
#define BIOS_GD5430VL_PATH	L"roms/video/cirruslogic/diamondvlbus.bin"
#define BIOS_GD5434_PATH	L"roms/video/cirruslogic/japan.bin"
#define BIOS_GD5436_PATH	L"roms/video/cirruslogic/5436.vbi"
#define BIOS_GD5440_PATH	L"roms/video/cirruslogic/5440bios.bin"
#define BIOS_GD5446_PATH	L"roms/video/cirruslogic/5446bv.vbi"
#define BIOS_GD6235_PATH	L"roms/video/cirruslogic/vga6235.rom"


void	cirrus_update_bank_ptr(clgd_t *clgd, uint8_t bank_index);
void	clgd_recalctimings(svga_t *svga);

void	svga_write_cirrus(uint32_t addr, uint8_t val, void *p);
void	svga_write_cirrus_linear(uint32_t addr, uint8_t val, void *p);
void	svga_write_cirrus_linear_bitblt(uint32_t addr, uint8_t val, void *p);
uint8_t	svga_read_cirrus(uint32_t addr, void *p);
uint8_t	svga_read_cirrus_linear(uint32_t addr, void *p);
uint8_t	svga_read_cirrus_linear_bitblt(uint32_t addr, void *p);


void clgd_out(uint16_t addr, uint8_t val, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
        uint8_t old;
		
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        // pclog("clgd out %04X %02X\n", addr, val);
                
        switch (addr)
        {
                case 0x3c4:
                svga->seqaddr = val;
                break;
                case 0x3c5:
				if (svga->seqaddr > 5)
				{
					old = svga->seqregs[svga->seqaddr & 0x1f];
					switch (svga->seqaddr & 0x1f)
					{
						case 0x06:
						val &= 0x17;
						if (val == 0x12)
						{
							svga->seqregs[svga->seqaddr & 0x1f] = 0x12;
						}
						else
						{
							svga->seqregs[svga->seqaddr & 0x1f] = 0xf;
						}
						break;
						
						case 0x10: case 0x30: case 0x50: case 0x70:
						case 0x90: case 0xb0: case 0xd0: case 0xf0:
						svga->seqregs[0x10] = val;
						svga->hwcursor.x = (val << 3) | (svga->seqaddr >> 5);
						// pclog("svga->hwcursor.x = %i\n", svga->hwcursor.x);
						break;
						
						case 0x11: case 0x31: case 0x51: case 0x71:
						case 0x91: case 0xb1: case 0xd1: case 0xf1:
						svga->seqregs[0x11] = val;
						svga->hwcursor.y = (val << 3) | (svga->seqaddr >> 5);
						// pclog("svga->hwcursor.y = %i\n", svga->hwcursor.y);
						break;
						
						case 0x07:
						cirrus_update_memory_access(clgd);
						clgd_recalctimings(svga);
						case 0x08: case 0x09: case 0x0a: case 0x0b:
						case 0x0c: case 0x0d: case 0x0e: case 0x0f:
						case 0x14: case 0x15: case 0x16:
						case 0x18: case 0x19: case 0x1a: case 0x1b:
						case 0x1c: case 0x1d: case 0x1e: case 0x1f:
						svga->seqregs[svga->seqaddr & 0x1f] = val;
						break;
						
						case 0x13:
						svga->seqregs[svga->seqaddr & 0x1f] = val;
						svga->hwcursor.addr = 0x1fc000 + ((val & 0x3f) * 256);
						// pclog("svga->hwcursor.addr = %x\n", svga->hwcursor.addr);
						break;                                
								
						case 0x12:
						svga->seqregs[svga->seqaddr & 0x1f] = val;
						svga->hwcursor.ena = val & 1;
						// pclog("svga->hwcursor.ena = %i\n", svga->hwcursor.ena);
						break;
								
						case 0x17:
						old = svga->seqregs[svga->seqaddr & 0x1f];
						svga->seqregs[svga->seqaddr & 0x1f] = (svga->seqregs[svga->seqaddr & 0x1f] & 0x38) | (val & 0xc7);
						cirrus_update_memory_access(clgd);
						break;
					}
					return;
				}
				break;

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
				// pclog("Write RAMDAC %04X %02X %04X:%04X\n", addr, val, CS, pc);
				cl_ramdac_out(addr, val, &clgd->ramdac, clgd, svga);
				return;

                case 0x3cf:
				if (svga->gdcaddr == 5)
				{
					svga->gdcreg[5] = val & 0x7f;
					if (svga->gdcreg[0xb] & 0x04)
						svga->writemode = svga->gdcreg[5] & 7;
					else
						svga->writemode = svga->gdcreg[5] & 3;
					cirrus_update_memory_access(clgd);
					svga_out(addr, val, svga);
					return;					
				}
				if (svga->gdcaddr == 6)
				{
					if ((svga->gdcreg[6] & 0xc) != (val & 0xc))
					{
						svga->gdcreg[6] = val;
						cirrus_update_memory_access(clgd);
					}
					svga->gdcreg[6] = val;
					return;					
				}
				if (svga->gdcaddr > 8)
				{
					switch (svga->gdcaddr)
					{
						case 0x09: case 0x0A: case 0x0B:
						svga->gdcreg[svga->gdcaddr & 0x3f] = val;
						cirrus_update_bank_ptr(clgd, 0);
						cirrus_update_bank_ptr(clgd, 1);
                        if (svga->gdcreg[0xb] & 0x04)
                                svga->writemode = svga->gdcreg[5] & 7;
                        else
                                svga->writemode = svga->gdcreg[5] & 3;
						cirrus_update_memory_access(clgd);
						break;
						
						case 0x21: case 0x23: case 0x25: case 0x27:
						svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x1f;
						break;
						
						case 0x2a:
						svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;
						/* if auto start mode, starts bit blt now */
						if (svga->gdcreg[0x31] & CIRRUS_BLT_AUTOSTART)  cirrus_bitblt_start(clgd, svga);
						break;
						
						case 0x2e:
						svga->gdcreg[svga->gdcaddr & 0x3f] = val & 0x3f;
						break;
						
						case 0x31:
						cirrus_write_bitblt(clgd, svga, val);
						break;
				
						default:
						svga->gdcreg[svga->gdcaddr & 0x3f] = val;
						break;
					}                        
					return;					
				}
				break;
                
                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
				if (svga->crtcreg <= 0x18)
					val &= mask_crtc[svga->crtcreg];
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                if ((svga->crtcreg != 0x22) && (svga->crtcreg != 0x24) && (svga->crtcreg != 0x26) && (svga->crtcreg != 0x27))  svga->crtc[svga->crtcreg] = val;
                if ((svga->crtcreg == 0x22) || (svga->crtcreg == 0x24) || (svga->crtcreg == 0x26) || (svga->crtcreg == 0x27))  return;

                if (old != val)
                {
					if (svga->crtcreg == 0x1b)
					{
						svga->vram_display_mask = (val & 2) ? (clgd->vram_size - 1) : 0x3ffff;
						clgd->linear_mmio_mask = (val & 2) ? (clgd->vram_size - 256) : (0x40000 - 256);
					}
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t clgd_in(uint16_t addr, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;

        if ((((addr & 0xfff0) == 0x3d0) || (addr & 0xfff0) == 0x3d0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
        // if (addr != 0x3da) pclog("IN clgd %04X\n", addr);
        
        switch (addr)
        {
                case 0x3c5:
                if (svga->seqaddr > 5)
                {
					switch (svga->seqaddr)
					{
						case 0x06:
						return ((svga->seqregs[6] & 0x17) == 0x12) ? 0x12 : 0x0f;
						
						case 0x10: case 0x30: case 0x50: case 0x70:
						case 0x90: case 0xb0: case 0xd0: case 0xf0:
						return svga->seqregs[0x10];
				
						case 0x11: case 0x31: case 0x51: case 0x71:
						case 0x91: case 0xb1: case 0xd1: case 0xf1:
						return svga->seqregs[0x11];
						
						case 0x15:
						return clgd->vram_code;
						
						case 0x05: case 0x07: case 0x08: case 0x09:
						case 0x0a: case 0x0b: case 0x0c: case 0x0d:
						case 0x0e: case 0x0f: case 0x12: case 0x13:
						case 0x14: case 0x16: case 0x17: case 0x18: 
						case 0x19: case 0x1a: case 0x1b: case 0x1c: 
						case 0x1d: case 0x1e: case 0x1f:
						return svga->seqregs[svga->seqaddr];
					}
					return svga->seqregs[svga->seqaddr & 0x3f];
                }
                break;

                case 0x3cf:
				if (svga->gdcaddr >= 0x3a)
				{
					return 0xff;
				}
                if (svga->gdcaddr > 8)
                {
                        return svga->gdcreg[svga->gdcaddr & 0x3f];
                }
                break;

                case 0x3c6: case 0x3c7: case 0x3c8: case 0x3c9:
//                pclog("Read RAMDAC %04X  %04X:%04X\n", addr, CS, pc);
                return cl_ramdac_in(addr, &clgd->ramdac, clgd, svga);

                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                switch (svga->crtcreg)
                {
					case 0x24: /*Attribute controller toggle readback (R)*/
					return svga->attrff << 7;
					case 0x26: /*Attribute controller index readback (R)*/
					return svga->attraddr & 0x3f;
                }
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

/***************************************
 *
 * bank memory
 *
 ***************************************/
void cirrus_update_bank_ptr(clgd_t *clgd, uint8_t bank_index)
{
	svga_t *svga = &clgd->svga;
	
    uint32_t offset;
    uint32_t limit;

    if ((svga->gdcreg[0x0b] & 0x01) != 0)	/* dual bank */
	offset = svga->gdcreg[0x09 + bank_index];
    else									/* single bank */
	offset = svga->gdcreg[0x09];

    if ((svga->gdcreg[0x0b] & 0x20) != 0)
	offset <<= 14;
    else
	offset <<= 12;

    if (clgd->vram_size <= offset)
	limit = 0;
    else
	limit = clgd->vram_size - offset;

    if (((svga->gdcreg[0x0b] & 0x01) == 0) && (bank_index != 0)) {
	if (limit > 0x8000) {
	    offset += 0x8000;
	    limit -= 0x8000;
	} else {
	    limit = 0;
	}
    }

    if (limit > 0) {
	clgd->bank[bank_index] = offset;
	clgd->limit[bank_index] = limit;
    } else {
	clgd->bank[bank_index] = 0;
	clgd->limit[bank_index] = 0;
    }
}

void clgd_recalctimings(svga_t *svga)
{
	clgd_t *clgd = (clgd_t *)svga->p;

	uint32_t iWidth, iHeight;
	uint8_t iDispBpp;

	svga->ma_latch = (svga->crtc[0x0c] << 8)
		+ svga->crtc[0x0d]
		+ ((svga->crtc[0x1b] & 0x01) << 16)
		+ ((svga->crtc[0x1b] & 0x0c) << 15)
		+ ((svga->crtc[0x1d] & 0x80) << 12);
	svga->ma_latch <<= 2;

	iHeight = 1 + svga->crtc[0x12]
		+ ((svga->crtc[0x07] & 0x02) << 7)
		+ ((svga->crtc[0x07] & 0x40) << 3);
	
	if ((svga->crtc[0x1a] & 0x01) > 0) {
		iHeight <<= 1;
		svga->vtotal *= 2;
		svga->dispend *= 2;
		svga->vblankstart *= 2;
		svga->vsyncstart *= 2;
		svga->split *= 2;
	}
	
	iWidth = (svga->crtc[0x01] + 1) * 8;
	iDispBpp = 4;
	
	if ((svga->seqregs[0x07] & 0x1) == CIRRUS_SR7_BPP_SVGA) 
	{
		pclog("Cirrus SVGA extended sequencer mode %x\n", svga->seqregs[0x07]);
		switch (svga->seqregs[0x07] & CIRRUS_SR7_BPP_MASK) 
		{
			case CIRRUS_SR7_BPP_8:
			iDispBpp = 8;
			svga->render = svga_render_8bpp_highres;
			break;
			
			case CIRRUS_SR7_BPP_16_DOUBLEVCLK:
			case CIRRUS_SR7_BPP_16:
			case 0x7:
			if (clgd->ramdac.ctrl & 0x1)
			{
				iDispBpp = 16;
				svga->render = svga_render_16bpp_highres;
			}
			else
			{
				iDispBpp = 15;
				svga->render = svga_render_15bpp_highres;			
			}
			break;
		
			case 0x3:
			case CIRRUS_SR7_BPP_24:
			case 0x5:
			case 0xe:
			iDispBpp = 24;
			svga->render = svga_render_24bpp_highres;	
			break;
		
			case CIRRUS_SR7_BPP_32:
			iDispBpp = 32;
			svga->render = svga_render_32bpp_highres;	
			break;
		}
	}
  
	if ((iWidth != svga->video_res_x) || (iHeight != svga->video_res_y)
		|| (iDispBpp != svga->bpp)) {
		pclog("Cirrus switched to %u x %u x %u\n", iWidth, iHeight, iDispBpp);
	}
	
	svga->video_res_x = iWidth;
	svga->video_res_y = iHeight;
	svga->bpp = iDispBpp;
    // pclog("MA now %05X %02X\n", svga->ma_latch, svga->crtc[0x1b]);
}

static void clgd_hwcursor_draw(svga_t *svga, int displine)
{
        int x;
        uint8_t dat[2];
        int xx;
        int offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;
		int largecur = (svga->seqregs[0x12] & 4);
		int cursize = (largecur) ? 64 : 32;
		int y_add = (enable_overscan && !suppress_overscan) ? 16 : 0;
		int x_add = (enable_overscan && !suppress_overscan) ? 8 : 0;
        
        for (x = 0; x < cursize; x += 8)
        {
                dat[0] = svga->vram[svga->hwcursor_latch.addr];
                dat[1] = svga->vram[svga->hwcursor_latch.addr + 0x80];
                for (xx = 0; xx < 8; xx++)
                {
                        if (offset >= svga->hwcursor_latch.x)
                        {
                                if (dat[1] & 0x80)
                                        ((uint32_t *)buffer32->line[displine + y_add])[offset + cursize + x_add] = 0;
                                if (dat[0] & 0x80)
                                        ((uint32_t *)buffer32->line[displine + y_add])[offset + cursize + x_add] ^= 0xffffff;
                        }
                           
                        offset++;
                        dat[0] <<= 1;
                        dat[1] <<= 1;
                }
                svga->hwcursor_latch.addr++;
        }
}

void cirrus_update_memory_access(clgd_t *clgd)
{
	svga_t *svga = &clgd->svga;

	if ((svga->seqregs[0x17] & 0x44) == 0x44)
	{
		goto generic_io;
	}
	else if (clgd->src_ptr != clgd->src_ptr_end)
	{
		goto generic_io;
	}
	else
	{
		if ((svga->gdcreg[0x0B] & 0x14) == 0x14)
		{
			goto generic_io;
		}
		else if (svga->gdcreg[0x0B] & 0x02)
		{
			goto generic_io;
		}

		svga->writemode = svga->gdcreg[0x05] & 7;
		if (svga->writemode < 4 || svga->writemode > 5 || ((svga->gdcreg[0x0B] & 0x4) == 0))
		{
			//pclog("Write mapping %02X %i\n", svga->gdcreg[6], svga->seqregs[0x17] & 0x04);
			switch (svga->gdcreg[6] & 0x0C)
			{
					case 0x0: /*128k at A0000*/
					mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
					mem_mapping_disable(&clgd->mmio_mapping);
					svga->banked_mask = 0xffff;
					break;
					case 0x4: /*64k at A0000*/
					mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
					if (svga->seqregs[0x17] & 0x04)
							mem_mapping_set_addr(&clgd->mmio_mapping, 0xb8000, 0x00100);
					svga->banked_mask = 0xffff;
					break;
					case 0x8: /*32k at B0000*/
					mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
					mem_mapping_disable(&clgd->mmio_mapping);
					svga->banked_mask = 0x7fff;
					break;
					case 0xC: /*32k at B8000*/
					mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
					mem_mapping_disable(&clgd->mmio_mapping);
					svga->banked_mask = 0x7fff;
					break;
			}
		}
		else
		{
generic_io:
			mem_mapping_disable(&svga->mapping);
			mem_mapping_disable(&clgd->mmio_mapping);
		}
	}
}

static void svga_write_mode45_8bpp(clgd_t *clgd, uint8_t mode, uint32_t offset, uint8_t mem_value)
{
	int x;
	uint8_t val = mem_value;
	uint8_t *dst;
	svga_t *svga = &clgd->svga;

	offset &= svga->decode_mask;
	if (offset >= svga->vram_max)
		return;
	offset &= svga->vram_mask;

	dst = &(svga->vram[offset]);

	svga->changedvram[offset >> 12] = changeframecount;

	for (x = 0; x < 8; x++)
	{
		if (val & 0x80)
		{
			*dst = clgd->blt.fg_col;
		}
		else
		{
			*dst = clgd->blt.bg_col;
		}
		val <<= 1;
		dst++;
	}
}

static void svga_write_mode45_16bpp(clgd_t *clgd, unsigned mode, unsigned offset, uint32_t mem_value)
{
	int x;
	unsigned val = mem_value;
	uint8_t *dst;
	svga_t *svga = &clgd->svga;

	offset &= svga->decode_mask;
	if (offset >= svga->vram_max)
		return;
	offset &= svga->vram_mask;

	dst = &(svga->vram[offset]);

	svga->changedvram[offset >> 12] = changeframecount;

	for (x = 0; x < 8; x++)
	{
		if (val & 0x80)
		{
			*dst = clgd->blt.fg_col;
			*(dst + 1) = svga->gdcreg[0x11];
		}
		else
		{
			*dst = clgd->blt.bg_col;
			*(dst + 1) = svga->gdcreg[0x10];
		}
		val <<= 1;
		dst += 2;
	}
}

static uint8_t cirrus_mmio_blt_read(uint32_t address, void *p)
{
	clgd_t *clgd = (clgd_t *)p;
	svga_t *svga = &clgd->svga;
	uint8_t value;

	switch(address & 0xff)
	{
		case (CIRRUS_MMIO_BLTBGCOLOR + 0):
			value = svga->gdcreg[0x00];
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 1):
			value = svga->gdcreg[0x10];
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 2):
			value = svga->gdcreg[0x12];
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 3):
			value = svga->gdcreg[0x14];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 0):
			value = svga->gdcreg[0x01];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 1):
			value = svga->gdcreg[0x11];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 2):
			value = svga->gdcreg[0x13];
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 3):
			value = svga->gdcreg[0x15];
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 0):
			value = svga->gdcreg[0x20];
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 1):
			value = svga->gdcreg[0x21];
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 0):
			value = svga->gdcreg[0x22];
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 1):
			value = svga->gdcreg[0x23];
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 0):
			value = svga->gdcreg[0x24];
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 1):
			value = svga->gdcreg[0x25];
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 0):
			value = svga->gdcreg[0x26];
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 1):
			value = svga->gdcreg[0x27];
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 0):
			value = svga->gdcreg[0x28];
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 1):
			value = svga->gdcreg[0x29];
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 2):
			value = svga->gdcreg[0x2a];
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 0):
			value = svga->gdcreg[0x2c];
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 1):
			value = svga->gdcreg[0x2d];
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 2):
			value = svga->gdcreg[0x2e];
			break;
		case (CIRRUS_MMIO_BLTWRITEMASK):
			value = svga->gdcreg[0x2f];
			break;
		case (CIRRUS_MMIO_BLTMODE):
			value = svga->gdcreg[0x30];
			break;
		case (CIRRUS_MMIO_BLTROP):
			value = svga->gdcreg[0x32];
			break;
		case (CIRRUS_MMIO_BLTMODEEXT):
			value = svga->gdcreg[0x33];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
			value = svga->gdcreg[0x34];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
			value = svga->gdcreg[0x35];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
			value = svga->gdcreg[0x38];
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
			value = svga->gdcreg[0x39];
			break;
		default:
			value = 0xff;
			break;
	}

	return value;
}

static void cirrus_mmio_blt_write(uint32_t address, uint8_t value, void *p)
{
	clgd_t *clgd = (clgd_t *)p;
	svga_t *svga = &clgd->svga;

	switch(address & 0xff)
	{
		case (CIRRUS_MMIO_BLTBGCOLOR + 0):
			svga->gdcreg[0x00] = value;
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 1):
			svga->gdcreg[0x10] = value;
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 2):
			svga->gdcreg[0x12] = value;
			break;
		case (CIRRUS_MMIO_BLTBGCOLOR + 3):
			svga->gdcreg[0x14] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 0):
			svga->gdcreg[0x01] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 1):
			svga->gdcreg[0x11] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 2):
			svga->gdcreg[0x13] = value;
			break;
		case (CIRRUS_MMIO_BLTFGCOLOR + 3):
			svga->gdcreg[0x15] = value;
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 0):
			svga->gdcreg[0x20] = value;
			break;
		case (CIRRUS_MMIO_BLTWIDTH + 1):
			svga->gdcreg[0x21] = value;
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 0):
			svga->gdcreg[0x22] = value;
			break;
		case (CIRRUS_MMIO_BLTHEIGHT + 1):
			svga->gdcreg[0x23] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 0):
			svga->gdcreg[0x24] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTPITCH + 1):
			svga->gdcreg[0x25] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 0):
			svga->gdcreg[0x26] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCPITCH + 1):
			svga->gdcreg[0x27] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 0):
			svga->gdcreg[0x28] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 1):
			svga->gdcreg[0x29] = value;
			break;
		case (CIRRUS_MMIO_BLTDESTADDR + 2):
			svga->gdcreg[0x2a] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 0):
			svga->gdcreg[0x2c] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 1):
			svga->gdcreg[0x2d] = value;
			break;
		case (CIRRUS_MMIO_BLTSRCADDR + 2):
			svga->gdcreg[0x2e] = value;
			break;
		case (CIRRUS_MMIO_BLTWRITEMASK):
			svga->gdcreg[0x2f] = value;
			break;
		case (CIRRUS_MMIO_BLTMODE):
			svga->gdcreg[0x30] = value;
			break;
		case (CIRRUS_MMIO_BLTROP):
			svga->gdcreg[0x32] = value;
			break;
		case (CIRRUS_MMIO_BLTMODEEXT):
			svga->gdcreg[0x33] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 0):
			svga->gdcreg[0x34] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLOR + 1):
			svga->gdcreg[0x35] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 0):
			svga->gdcreg[0x38] = value;
			break;
		case (CIRRUS_MMIO_BLTTRANSPARENTCOLORMASK + 1):
			svga->gdcreg[0x39] = value;
			break;
	}
}

void cirrus_write(uint32_t addr, uint8_t val, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
	
//        pclog("gd5429_write : %05X %02X  ", addr, val);
		if (clgd->src_ptr != clgd->src_ptr_end)
		{
			/* bitblt */
			*clgd->src_ptr++ = (uint8_t) val;
			if (clgd->src_ptr >= clgd->src_ptr_end)
			{
				cirrus_bitblt_cputovideo_next(clgd, svga);
			}
		}

        addr &= svga->banked_mask;
        addr = (addr & 0x7fff) + clgd->bank[(addr >> 15) & 1];
//        pclog("%08X\n", addr);
//        svga_write_linear(addr, val, &clgd->svga);
		svga->writemode = svga->gdcreg[0x05] & 0x7;
		if (svga->writemode < 4 || svga->writemode > 5 || ((svga->gdcreg[0x0B] & 0x4) == 0)) {
		    svga_write_linear(addr, val, &clgd->svga);
		} else {
		    if ((svga->gdcreg[0x0B] & 0x14) != 0x14) {
			svga_write_mode45_8bpp(clgd, svga->writemode,
							 addr,
							 val);
		    } else {
			svga_write_mode45_16bpp(clgd, svga->writemode,
							  addr,
							  val);
		    }
		}
}

uint8_t cirrus_read(uint32_t addr, void *p)
{
        clgd_t *clgd = (clgd_t *)p;
        svga_t *svga = &clgd->svga;
        uint8_t ret;

//        pclog("gd5429_read : %05X ", addr);
        addr &= svga->banked_mask;
        addr = (addr & 0x7fff) + clgd->bank[(addr >> 15) & 1];
        ret = svga_read_linear(addr, &clgd->svga);
//        pclog("%08X %02X\n", addr, ret);  
        return ret;
}


static void *clgd_init(device_t *info)
{
        clgd = malloc(sizeof(clgd_t));
        svga_t *svga = &clgd->svga;
	wchar_t *romfn = NULL;
	int id = info->local;

        memset(clgd, 0x00, sizeof(clgd_t));

	switch(id) {
		case CIRRUS_ID_CLGD5422:
			romfn = BIOS_GD5422_PATH;
			break;

		case CIRRUS_ID_CLGD5429:
			romfn = BIOS_GD5429_PATH;
			break;

		case CIRRUS_ID_CLGD5430:
			romfn = BIOS_GD5430_PATH;
			break;

		case CIRRUS_ID_CLGD5430VL:
			romfn = BIOS_GD5430VL_PATH;
			id = CIRRUS_ID_CLGD5430;
			break;

		case CIRRUS_ID_CLGD5434:
			romfn = BIOS_GD5434_PATH;
			break;

		case CIRRUS_ID_CLGD5436:
			romfn = BIOS_GD5436_PATH;
			break;

		case CIRRUS_ID_CLGD5440:
			romfn = BIOS_GD5440_PATH;
			break;

		case CIRRUS_ID_CLGD5446:
			romfn = BIOS_GD5446_PATH;
			break;

		case CIRRUS_ID_CLGD6235:
			romfn = BIOS_GD6235_PATH;
			break;
	}

        rom_init(&clgd->bios_rom, romfn,
		 0xc0000, 0x8000, 0x7fff,
		 0, MEM_MAPPING_EXTERNAL);

        svga_init(&clgd->svga, clgd, 1 << 21, /*2mb*/
                  clgd_recalctimings,
                  clgd_in, clgd_out,
                  clgd_hwcursor_draw,
                  NULL);

        mem_mapping_set_handler(&svga->mapping,
				cirrus_read, NULL, NULL,
				cirrus_write, NULL, NULL);
        mem_mapping_set_p(&svga->mapping, clgd);

        mem_mapping_add(&clgd->mmio_mapping, 0, 0,
			cirrus_mmio_blt_read, NULL, NULL,
			cirrus_mmio_blt_write, NULL, NULL,  NULL, 0, clgd);

        io_sethandler(0x03c0, 32,
		      clgd_in, NULL, NULL,
		      clgd_out, NULL, NULL, clgd);

	if (id < CIRRUS_ID_CLGD5428) {
		/* 1 MB */
		clgd->vram_size = (1 << 20);
		clgd->vram_code = 2;

		svga->seqregs[0xf] = 0x18;
		svga->seqregs[0x1f] = 0x22;
	} else if ((id >= CIRRUS_ID_CLGD5428) && (id <= CIRRUS_ID_CLGD5430)) {
		/* 2 MB */
		clgd->vram_size = (1 << 21);
		clgd->vram_code = 3;

		svga->seqregs[0xf] = 0x18;
		svga->seqregs[0x1f] = 0x22;
	} else if (id >= CIRRUS_ID_CLGD5434) {
		/* 4 MB */
		clgd->vram_size = (1 << 22);
		clgd->vram_code = 4;

		svga->seqregs[0xf] = 0x98;
		svga->seqregs[0x1f] = 0x2d;
		svga->seqregs[0x17] = 0x20;
		svga->gdcreg[0x18] = 0xf;
	}

	// Seems the 5436 and 5446 BIOS'es never turn on that bit until it's actually needed,
	// therefore they also don't turn it back off on 640x480x4bpp,
	// therefore, we need to make sure the VRAM mask is correct at start.
	svga->vram_display_mask = (svga->crtc[0x1b] & 2) ? (clgd->vram_size - 1) : 0x3ffff;
	clgd->linear_mmio_mask = (svga->crtc[0x1b] & 2) ? (clgd->vram_size - 256) : (0x40000 - 256);

	svga->seqregs[0x15] = clgd->vram_code;
	if ((id >= CIRRUS_ID_CLGD5422) && (id <= CIRRUS_ID_CLGD5429))
		svga->seqregs[0xa] = (clgd->vram_code << 3);

	svga->crtc[0x27] = id;

	// clgd_recalc_mapping(clgd);
	/* force refresh */
	// cirrus_update_bank_ptr(s, 0);
	// cirrus_update_bank_ptr(s, 1);

	init_rops();

        return clgd;
}


static void clgd_close(void *p)
{
        clgd_t *clgd = (clgd_t *)p;

        svga_close(&clgd->svga);

        free(clgd);
}


static void clgd_speed_changed(void *p)
{
        clgd_t *clgd = (clgd_t *)p;

        svga_recalctimings(&clgd->svga);
}


static void clgd_force_redraw(void *p)
{
        clgd_t *clgd = (clgd_t *)p;

        clgd->svga.fullchange = changeframecount;
}


static void clgd_add_status_info(char *s, int max_len, void *p)
{
        clgd_t *clgd = (clgd_t *)p;

        svga_add_status_info(s, max_len, &clgd->svga);
}


static int gd5422_available(void)
{
        return rom_present(BIOS_GD5422_PATH);
}


static int gd5429_available(void)
{
        return rom_present(BIOS_GD5429_PATH);
}


static int gd5430_available(void)
{
        return rom_present(BIOS_GD5430_PATH);
}


static int dia5430_available(void)
{
        return rom_present(BIOS_GD5430VL_PATH);
}


static int gd5434_available(void)
{
        return rom_present(BIOS_GD5434_PATH);
}


static int gd5436_available(void)
{
        return rom_present(BIOS_GD5436_PATH);
}


static int gd5440_available(void)
{
        return rom_present(BIOS_GD5440_PATH);
}


static int gd5446_available(void)
{
        return rom_present(BIOS_GD5446_PATH);
}


static int gd6235_available(void)
{
        return rom_present(BIOS_GD6235_PATH);
}


device_t gd5422_device =
{
        "Cirrus Logic GD5422",
        DEVICE_ISA | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5422,
        clgd_init, clgd_close, NULL,
        gd5422_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t gd5429_device =
{
        "Cirrus Logic GD5429",
        DEVICE_VLB | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5429,
        clgd_init, clgd_close, NULL,
        gd5429_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t gd5430_device =
{
        "Cirrus Logic GD5430",
        DEVICE_ISA | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5430,
        clgd_init, clgd_close, NULL,
        gd5430_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t dia5430_device =
{
        "Diamond CL-GD5430 VLB",
        DEVICE_VLB | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5430VL,
        clgd_init, clgd_close, NULL,
        dia5430_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t gd5434_device =
{
        "Cirrus Logic GD5434",
        DEVICE_ISA | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5434,
        clgd_init, clgd_close, NULL,
        gd5434_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t gd5436_device =
{
        "Cirrus Logic GD5436",
        DEVICE_ISA | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5436,
        clgd_init, clgd_close, NULL,
        gd5436_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t gd5440_device =
{
        "Cirrus Logic GD5440",
        DEVICE_ISA | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5440,
        clgd_init, clgd_close, NULL,
        gd5440_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t gd5446_device =
{
        "Cirrus Logic GD5446",
        DEVICE_VLB | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD5446,
        clgd_init, clgd_close, NULL,
        gd5446_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};

device_t gd6235_device =
{
        "Cirrus Logic GD6235",
        DEVICE_ISA | DEVICE_NOT_WORKING,
	CIRRUS_ID_CLGD6235,
        clgd_init, clgd_close, NULL,
        gd6235_available,
        clgd_speed_changed,
        clgd_force_redraw,
        clgd_add_status_info,
	NULL
};
