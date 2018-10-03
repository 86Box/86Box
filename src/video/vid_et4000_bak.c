/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Tseng Labs ET4000.
 *
 * Version:	@(#)vid_et4000.c	1.0.15	2018/08/26
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../mca.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_sc1502x_ramdac.h"
#include "vid_et4000.h"


#define BIOS_ROM_PATH			L"roms/video/et4000/et4000.bin"
#define KOREAN_BIOS_ROM_PATH 	L"roms/video/et4000/tgkorvga.bin"
#define KOREAN_FONT_ROM_PATH 	L"roms/video/et4000/tg_ksc5601.rom"


typedef struct et4000_t
{
        svga_t svga;
        sc1502x_ramdac_t ramdac;
        
        rom_t bios_rom;
        
        uint8_t banking;
		
		uint8_t pos_regs[8];
		
		int is_mca;
		
        uint8_t port_22cb_val;
        uint8_t port_32cb_val;
        int get_korean_font_enabled;
        int get_korean_font_index;
        uint16_t get_korean_font_base;
	uint32_t vram_mask;
	uint8_t hcr, mcr;
} et4000_t;

static uint8_t crtc_mask[0x40] =
{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

uint8_t et4000_in(uint16_t addr, void *p);

void et4000_out(uint16_t addr, uint8_t val, void *p)
{
        et4000_t *et4000 = (et4000_t *)p;
        svga_t *svga = &et4000->svga;
                
        uint8_t old;
        
        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                sc1502x_ramdac_out(addr, val, &et4000->ramdac, svga);
                return;

                case 0x3CD: /*Banking*/
		if (!(svga->crtc[0x36] & 0x10) && !(svga->gdcreg[6] & 0x08)) {
               		svga->write_bank = (val & 0xf) * 0x10000;
               		svga->read_bank = ((val >> 4) & 0xf) * 0x10000;
		}
                et4000->banking = val;
                return;
		case 0x3cf:
		if ((svga->gdcaddr & 15) == 6) {
			if (!(svga->crtc[0x36] & 0x10) && !(val & 0x08)) {
               			svga->write_bank = (et4000->banking & 0xf) * 0x10000;
               			svga->read_bank = ((et4000->banking >> 4) & 0xf) * 0x10000;
			} else
				svga->write_bank = svga->read_bank = 0;
		}
		break;
                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                        return;
                if ((svga->crtcreg == 0x35) && (svga->crtc[0x11] & 0x80))
                        return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                        val = (svga->crtc[7] & ~0x10) | (val & 0x10);
                old = svga->crtc[svga->crtcreg];
                val &= crtc_mask[svga->crtcreg];
                svga->crtc[svga->crtcreg] = val;

		if (svga->crtcreg == 0x36) {
			if (!(val & 0x10) && !(svga->gdcreg[6] & 0x08)) {
               			svga->write_bank = (et4000->banking & 0xf) * 0x10000;
               			svga->read_bank = ((et4000->banking >> 4) & 0xf) * 0x10000;
			} else
				svga->write_bank = svga->read_bank = 0;
		}

                if (old != val)
                {
                        if (svga->crtcreg < 0xE || svga->crtcreg > 0x10)
                        {
                                svga->fullchange = changeframecount;
                                svga_recalctimings(svga);
                        }
                }
				
		/*Note - Silly hack to determine video memory size automatically by ET4000 BIOS.*/
                if ((svga->crtcreg == 0x37) && !et4000->is_mca)
                {
                        switch(val & 0x0B)
                        {
                                case 0x00:
                                case 0x01:
                                if(svga->vram_max == 64 * 1024)
                                mem_mapping_enable(&svga->mapping);
                                else
                                mem_mapping_disable(&svga->mapping);
                                break;
                                case 0x02:
                                if(svga->vram_max == 128 * 1024)
                                mem_mapping_enable(&svga->mapping);
                                else
                                mem_mapping_disable(&svga->mapping);
                                break;
                                case 0x03:
                                case 0x08:
                                case 0x09:
                                if (svga->vram_max == 256 * 1024)
                                	mem_mapping_enable(&svga->mapping);
                                else
                                	mem_mapping_disable(&svga->mapping);
                                break;
                                case 0x0A:
                                if (svga->vram_max == 512 * 1024)
                                	mem_mapping_enable(&svga->mapping);
                                else
                                	mem_mapping_disable(&svga->mapping);
                                break;
                                case 0x0B:
                                if (svga->vram_max == 1024 * 1024)
                                	mem_mapping_enable(&svga->mapping);
                                else
                                	mem_mapping_disable(&svga->mapping);
                                break;
                                default:
                                	mem_mapping_enable(&svga->mapping);
                                break;
                        }
                }				
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t et4000_in(uint16_t addr, void *p)
{
        et4000_t *et4000 = (et4000_t *)p;
        svga_t *svga = &et4000->svga;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
		case 0x3c2:
		if (et4000->is_mca)
		{
			if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x4e)
				return 0;
			else
				return 0x10;					
		}
		break;
			
                case 0x3C5:
                if ((svga->seqaddr & 0xf) == 7) return svga->seqregs[svga->seqaddr & 0xf] | 4;
                break;

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                return sc1502x_ramdac_in(addr, &et4000->ramdac, svga);
                
                case 0x3CD: /*Banking*/
               	return et4000->banking;
                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void et4000k_out(uint16_t addr, uint8_t val, void *p)
{
        et4000_t *et4000 = (et4000_t *)p;

//        pclog("ET4000k out %04X %02X\n", addr, val);

        switch (addr)
        {
                case 0x22CB:
                et4000->port_22cb_val = (et4000->port_22cb_val & 0xF0) | (val & 0x0F);
                et4000->get_korean_font_enabled = val & 7;
                if (et4000->get_korean_font_enabled == 3)
                        et4000->get_korean_font_index = 0;
                break;
                case 0x22CF:
		switch(et4000->get_korean_font_enabled)
                {
                        case 1:
                        et4000->get_korean_font_base = ((val & 0x7F) << 7) | (et4000->get_korean_font_base & 0x7F);
                        break;
                        case 2:
                        et4000->get_korean_font_base = (et4000->get_korean_font_base & 0x3F80) | (val & 0x7F) | (((val ^ 0x80) & 0x80) << 8);
                        break;
                        case 3:
                        if((et4000->port_32cb_val & 0x30) == 0x20 && (et4000->get_korean_font_base & 0x7F) > 0x20 && (et4000->get_korean_font_base & 0x7F) < 0x7F)
                        {
                                switch(et4000->get_korean_font_base & 0x3F80)
                                {
                                        case 0x2480:
                                        if(et4000->get_korean_font_index < 16)
                                                fontdatksc5601_user[(et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index] = val;
                                        else if(et4000->get_korean_font_index >= 24 && et4000->get_korean_font_index < 40)
                                                fontdatksc5601_user[(et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index - 8] = val;
                                        break;
                                        case 0x3F00:
                                        if(et4000->get_korean_font_index < 16)
                                                fontdatksc5601_user[96 + (et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index] = val;
                                        else if(et4000->get_korean_font_index >= 24 && et4000->get_korean_font_index < 40)
                                                fontdatksc5601_user[96 + (et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index - 8] = val;
                                        break;
                                        default:
                                        break;
                                }
                                et4000->get_korean_font_index++;
                        }
                        break;
                        default:
                        break;
                }
                break;
                case 0x32CB:
                et4000->port_32cb_val = val;
                svga_recalctimings(&et4000->svga);
                break;
                default:
                et4000_out(addr, val, p);
                break;
        }
}

uint8_t et4000k_in(uint16_t addr, void *p)
{
        uint8_t val = 0xFF;
        et4000_t *et4000 = (et4000_t *)p;
        
//        if (addr != 0x3da) pclog("IN ET4000 %04X\n", addr);
        
        switch (addr)
        {
                case 0x22CB:
                return et4000->port_22cb_val;
                case 0x22CF:
                val = 0;
                switch(et4000->get_korean_font_enabled)
                {
                        case 3:
                        if((et4000->port_32cb_val & 0x30) == 0x30)
                        {
                                val = fontdatksc5601[et4000->get_korean_font_base].chr[et4000->get_korean_font_index++];
                                et4000->get_korean_font_index &= 0x1F;
                        }
                        else if((et4000->port_32cb_val & 0x30) == 0x20 && (et4000->get_korean_font_base & 0x7F) > 0x20 && (et4000->get_korean_font_base & 0x7F) < 0x7F)
                        {
                                switch(et4000->get_korean_font_base & 0x3F80)
                                {
                                        case 0x2480:
                                        if(et4000->get_korean_font_index < 16)
                                                val = fontdatksc5601_user[(et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index];
                                        else if(et4000->get_korean_font_index >= 24 && et4000->get_korean_font_index < 40)
                                                val = fontdatksc5601_user[(et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index - 8];
                                        break;
                                        case 0x3F00:
                                        if(et4000->get_korean_font_index < 16)
                                                val = fontdatksc5601_user[96 + (et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index];
                                        else if(et4000->get_korean_font_index >= 24 && et4000->get_korean_font_index < 40)
                                                val = fontdatksc5601_user[96 + (et4000->get_korean_font_base & 0x7F) - 0x20].chr[et4000->get_korean_font_index - 8];
                                        break;
                                        default:
                                        break;
                                }
                                et4000->get_korean_font_index++;
                                et4000->get_korean_font_index %= 72;
                        }
                        break;
                        case 4:
                        val = 0x0F;
                        break;
                        default:
                        break;
                }
                return val;
                case 0x32CB:
                return et4000->port_32cb_val;
                default:
                return et4000_in(addr, p);
        }
}

void et4000_recalctimings(svga_t *svga)
{
        svga->ma_latch |= (svga->crtc[0x33]&3)<<16;
        if (svga->crtc[0x35] & 1)    svga->vblankstart += 0x400;
        if (svga->crtc[0x35] & 2)    svga->vtotal += 0x400;
        if (svga->crtc[0x35] & 4)    svga->dispend += 0x400;
        if (svga->crtc[0x35] & 8)    svga->vsyncstart += 0x400;
        if (svga->crtc[0x35] & 0x10) svga->split += 0x400;
        if (!svga->rowoffset)        svga->rowoffset = 0x100;
        if (svga->crtc[0x3f] & 1)    svga->htotal += 256;
        if (svga->attrregs[0x16] & 0x20) svga->hdisp <<= 1;

        switch (((svga->miscout >> 2) & 3) | ((svga->crtc[0x34] << 1) & 4))
        {
                case 0: case 1: break;
                case 3: svga->clock = cpuclock / 40000000.0; break;
                case 5: svga->clock = cpuclock / 65000000.0; break;
                default: svga->clock = cpuclock / 36000000.0; break;
        }
        
        switch (svga->bpp)
        {
                case 15: case 16:
                svga->hdisp /= 2;
                break;
                case 24:
                svga->hdisp /= 3;
                break;
        }
}

void et4000k_recalctimings(svga_t *svga)
{
        et4000_t *et4000 = (et4000_t *)svga->p;

        et4000_recalctimings(svga);

        if (svga->render == svga_render_text_80 && ((svga->crtc[0x37] & 0x0A) == 0x0A))
        {
                if((et4000->port_32cb_val & 0xB4) == ((svga->crtc[0x37] & 3) == 2 ? 0xB4 : 0xB0))
                {
                        svga->render = svga_render_text_80_ksc5601;
                }
        }
}

void *et4000_isa_init(const device_t *info)
{
        et4000_t *et4000 = malloc(sizeof(et4000_t));
        memset(et4000, 0, sizeof(et4000_t));

		et4000->is_mca = 0;
		
        rom_init(&et4000->bios_rom, BIOS_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
                
        io_sethandler(0x03c0, 0x0020, et4000_in, NULL, NULL, et4000_out, NULL, NULL, et4000);

        svga_init(&et4000->svga, et4000,  device_get_config_int("memory") << 10, /*1mb default*/
                   et4000_recalctimings,
                   et4000_in, et4000_out,
                   NULL,
                   NULL);
	et4000->vram_mask = (device_get_config_int("memory") << 10) - 1;
        
        return et4000;
}

void *et4000k_isa_init(const device_t *info)
{
        et4000_t *et4000 = malloc(sizeof(et4000_t));
        memset(et4000, 0, sizeof(et4000_t));

        rom_init(&et4000->bios_rom, KOREAN_BIOS_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        loadfont(KOREAN_FONT_ROM_PATH, 6);
                
        io_sethandler(0x03c0, 0x0020, et4000_in, NULL, NULL, et4000_out, NULL, NULL, et4000);

        io_sethandler(0x22cb, 0x0001, et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, et4000);
        io_sethandler(0x22cf, 0x0001, et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, et4000);
        io_sethandler(0x32cb, 0x0001, et4000k_in, NULL, NULL, et4000k_out, NULL, NULL, et4000);
        et4000->port_22cb_val = 0x60;
        et4000->port_32cb_val = 0;

        svga_init(&et4000->svga, et4000, device_get_config_int("memory") << 10,
                   et4000k_recalctimings,
                   et4000k_in, et4000k_out,
                   NULL,
                   NULL);
	et4000->vram_mask = (device_get_config_int("memory") << 10) - 1;

        et4000->svga.ksc5601_sbyte_mask = 0x80;

        return et4000;	
}

static uint8_t
et4000_mca_read(int port, void *priv)
{
    et4000_t *et4000 = (et4000_t *)priv;

    return(et4000->pos_regs[port & 7]);
}

static void
et4000_mca_write(int port, uint8_t val, void *priv)
{
    et4000_t *et4000 = (et4000_t *)priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102) return;

    /* Save the MCA register value. */
    et4000->pos_regs[port & 7] = val;
}

void *et4000_mca_init(const device_t *info)
{
        et4000_t *et4000 = malloc(sizeof(et4000_t));
        memset(et4000, 0, sizeof(et4000_t));

	et4000->is_mca = 1;

	/* Enable MCA. */
	et4000->pos_regs[0] = 0xF2;	/* ET4000 MCA board ID */
	et4000->pos_regs[1] = 0x80;	
	mca_add(et4000_mca_read, et4000_mca_write, et4000);

	rom_init(&et4000->bios_rom, BIOS_ROM_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

	svga_init(&et4000->svga, et4000, 1 << 20, /*1mb*/
			   et4000_recalctimings,
			   et4000_in, et4000_out,
			   NULL,
			   NULL);	
	et4000->vram_mask = (1 << 20) - 1;

	io_sethandler(0x03c0, 0x0020, et4000_in, NULL, NULL, et4000_out, NULL, NULL, et4000);		
		
        return et4000;
}

static int et4000_available(void)
{
        return rom_present(BIOS_ROM_PATH);
}

static int et4000k_available()
{
        return rom_present(KOREAN_BIOS_ROM_PATH) && rom_present(KOREAN_FONT_ROM_PATH);
}

void et4000_close(void *p)
{
        et4000_t *et4000 = (et4000_t *)p;

        svga_close(&et4000->svga);
        
        free(et4000);
}

void et4000_speed_changed(void *p)
{
        et4000_t *et4000 = (et4000_t *)p;
        
        svga_recalctimings(&et4000->svga);
}

void et4000_force_redraw(void *p)
{
        et4000_t *et4000 = (et4000_t *)p;

        et4000->svga.fullchange = changeframecount;
}

static device_config_t et4000_config[] =
{
        {
                .name = "memory",
                .description = "Memory size",
                .type = CONFIG_SELECTION,
                .selection =
		{
                        {
                                .description = "256 kB",
                                .value = 256
                        },
                        {
                                .description = "512 kB",
                                .value = 512
                        },
                        {
                                .description = "1 MB",
                                .value = 1024
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 1024
        },
        {
                .type = -1
        }
};

const device_t et4000_isa_device =
{
        "Tseng Labs ET4000AX (ISA)",
        DEVICE_ISA, 0,
        et4000_isa_init, et4000_close, NULL,
        et4000_available,
        et4000_speed_changed,
        et4000_force_redraw,
	et4000_config
};

const device_t et4000k_isa_device =
{
        "Trigem Korean VGA (Tseng Labs ET4000AX Korean)",
        DEVICE_ISA, 0,
        et4000k_isa_init, et4000_close, NULL,
        et4000k_available,
        et4000_speed_changed,
        et4000_force_redraw,
	et4000_config
};

const device_t et4000k_tg286_isa_device =
{
        "Trigem Korean VGA (Trigem 286M)",
        DEVICE_ISA, 0,
        et4000k_isa_init, et4000_close, NULL,
        et4000k_available,
        et4000_speed_changed,
        et4000_force_redraw,
	et4000_config
};

const device_t et4000_mca_device =
{
        "Tseng Labs ET4000AX (MCA)",
        DEVICE_MCA, 0,
        et4000_mca_init, et4000_close, NULL,
        et4000_available,
        et4000_speed_changed,
        et4000_force_redraw,
	NULL
};

