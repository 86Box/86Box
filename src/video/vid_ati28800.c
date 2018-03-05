/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ATI 28800 emulation (VGA Charger and Korean VGA)
 *
 * Version:	@(#)vid_ati28800.c	1.0.9	2018/03/05
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
#include "../cpu/cpu.h"
#include "../io.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../timer.h"
#include "video.h"
#include "vid_ati28800.h"
#include "vid_ati_eeprom.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


#define BIOS_ATIKOR_PATH	L"roms/video/ati28800/atikorvga.bin"

#define BIOS_VGAXL_EVEN_PATH	L"roms/video/ati28800/xleven.bin"
#define BIOS_VGAXL_ODD_PATH	L"roms/video/ati28800/xlodd.bin"

#if defined(DEV_BRANCH) && defined(USE_XL24)
#define BIOS_XL24_EVEN_PATH	L"roms/video/ati28800/112-14318-102.bin"
#define BIOS_XL24_ODD_PATH	L"roms/video/ati28800/112-14319-102.bin"
#endif

#define BIOS_ROM_PATH		L"roms/video/ati28800/bios.bin"


typedef struct ati28800_t
{
        svga_t svga;
        ati_eeprom_t eeprom;
        
        rom_t bios_rom;
        
        uint8_t regs[256];
        int index;
		
		uint32_t memory;
} ati28800_t;


uint8_t		port_03dd_val;
uint16_t	get_korean_font_kind;
int		in_get_korean_font_kind_set;
int		get_korean_font_enabled;
int		get_korean_font_index;
uint16_t	get_korean_font_base;
extern int	dbcs_mode_enabled;


static void ati28800_out(uint16_t addr, uint8_t val, void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        svga_t *svga = &ati28800->svga;
        uint8_t old;
        
/*        pclog("ati28800_out : %04X %02X  %04X:%04X\n", addr, val, CS,pc); */
                
        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x1ce:
                ati28800->index = val;
                break;
                case 0x1cf:
				old=ati28800->regs[ati28800->index];
                ati28800->regs[ati28800->index] = val;
                switch (ati28800->index)
                {
                        case 0xb2:
                        case 0xbe:
                        if (ati28800->regs[0xbe] & 8) /*Read/write bank mode*/
                        {
                                svga->read_bank  = ((ati28800->regs[0xb2] >> 5) & 7) * 0x10000;
                                svga->write_bank = ((ati28800->regs[0xb2] >> 1) & 7) * 0x10000;
                        }
                        else                    /*Single bank mode*/
                                svga->read_bank = svga->write_bank = ((ati28800->regs[0xb2] >> 1) & 7) * 0x10000;
                        break;
                        case 0xb3:
                        ati_eeprom_write(&ati28800->eeprom, val & 8, val & 2, val & 1);
                        break;
						case 0xb6:
						if((old ^ val) & 0x10) svga_recalctimings(svga);
						break;
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
                break;
        }
        svga_out(addr, val, svga);
}

void ati28800k_out(uint16_t addr, uint8_t val, void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        svga_t *svga = &ati28800->svga;
        uint16_t oldaddr = addr;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) 
                addr ^= 0x60;
 
        switch (addr)
        {
                case 0x1CF:
                if(ati28800->index == 0xBF && ((ati28800->regs[0xBF] ^ val) & 0x20))
                {
                        dbcs_mode_enabled = val & 0x20;
                        svga_recalctimings(svga);

                }
                ati28800_out(oldaddr, val, p);
                break;
                case 0x3DD:
                port_03dd_val = val;
                if(val == 1) get_korean_font_enabled = 0;
                if(in_get_korean_font_kind_set)
                {
                        get_korean_font_kind = (val << 8) | (get_korean_font_kind & 0xFF);
                        get_korean_font_enabled = 1;
                        get_korean_font_index = 0;
						in_get_korean_font_kind_set = 0;
                }
                break;
                case 0x3DE:
                in_get_korean_font_kind_set = 0;
                switch(port_03dd_val)
                {
                        case 0x10:
                        get_korean_font_base = ((val & 0x7F) << 7) | (get_korean_font_base & 0x7F);
                        break;
                        case 8:
                        get_korean_font_base = (get_korean_font_base & 0x3F80) | (val & 0x7F);
                        break;
                        case 1:
                        get_korean_font_kind = (get_korean_font_kind & 0xFF00) | val;
                        if(val & 2) in_get_korean_font_kind_set = 1;
                        break;
                        default:
                        break;
                }
                break;
                default:
                ati28800_out(oldaddr, val, p);
                break;
        }
}

static uint8_t ati28800_in(uint16_t addr, void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        svga_t *svga = &ati28800->svga;
        uint8_t temp;

/*        if (addr != 0x3da) pclog("ati28800_in : %04X ", addr); */
                
        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;
             
        switch (addr)
        {
                case 0x1ce:
                temp = ati28800->index;
                break;
                case 0x1cf:
                switch (ati28800->index)
                {
						case 0xb0:
						if (ati28800->memory == 256)
							return 0x08;
						else if (ati28800->memory == 512)
							return 0x10;
						else
							return 0x18;
						break;
					
                        case 0xb7:
                        temp = ati28800->regs[ati28800->index] & ~8;
                        if (ati_eeprom_read(&ati28800->eeprom))
                                temp |= 8;
                        break;
                        
                        default:
                        temp = ati28800->regs[ati28800->index];
                        break;
                }
                break;

                case 0x3c2:
                if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x50)
                        temp = 0;
                else
                        temp = 0x10;
                break;
                case 0x3D4:
                temp = svga->crtcreg;
                break;
                case 0x3D5:
                temp = svga->crtc[svga->crtcreg];
                break;
                default:
                temp = svga_in(addr, svga);
                break;
        }
        /* if (addr != 0x3da) pclog("%02X  %04X:%04X\n", temp, CS,cpu_state.pc); */
        return temp;
}

uint8_t ati28800k_in(uint16_t addr, void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        svga_t *svga = &ati28800->svga;
        uint16_t oldaddr = addr;
        uint8_t temp = 0xFF;

//        if (addr != 0x3da) pclog("ati28800_in : %04X ", addr);
                
        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;
             
        switch (addr)
        {
                case 0x3DE:
                if(get_korean_font_enabled && (ati28800->regs[0xBF] & 0x20))
                {
                        switch(get_korean_font_kind >> 8)
                        {
                                case 4: /* ROM font */
                                temp = fontdatksc5601[get_korean_font_base][get_korean_font_index++];
                                break;
                                case 2: /* User defined font - TODO : Should be implemented later */
                                temp = 0;
                                break;
                                default:
                                break;
                        }
                        get_korean_font_index &= 0x1F;
                }
                break;
                default:
                temp = ati28800_in(oldaddr, p);
                break;
        }
        if (addr != 0x3da) pclog("%02X  %04X:%04X\n", temp, CS,cpu_state.pc);
        return temp;
}
 
static void ati28800_recalctimings(svga_t *svga)
{
        ati28800_t *ati28800 = (ati28800_t *)svga->p;

		if (ati28800->regs[0xb6] & 0x10)
		{
                svga->hdisp <<= 1;
                svga->htotal <<= 1;
                svga->rowoffset <<= 1;			
		}
		
        if (!svga->scrblank && (ati28800->regs[0xb0] & 0x20)) /*Extended 256 colour modes*/
        {
                svga->render = svga_render_8bpp_highres;
				svga->bpp = 8;
                svga->rowoffset <<= 1;
                svga->ma <<= 1;
        }
}

void *
ati28800k_init(device_t *info)
{
        ati28800_t *ati28800 = malloc(sizeof(ati28800_t));
        memset(ati28800, 0, sizeof(ati28800_t));

		ati28800->memory = device_get_config_int("memory");
		
        port_03dd_val = 0;
        get_korean_font_base = 0;
        get_korean_font_index = 0;
        get_korean_font_enabled = 0;
        get_korean_font_kind = 0;
        in_get_korean_font_kind_set = 0;
        dbcs_mode_enabled = 0;
        
        rom_init(&ati28800->bios_rom, BIOS_ATIKOR_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		loadfont(FONT_ATIKOR_PATH, 6);
        
        svga_init(&ati28800->svga, ati28800, ati28800->memory << 10, /*Memory size, default 512KB*/
                   ati28800_recalctimings,
                   ati28800k_in, ati28800k_out,
                   NULL,
                   NULL);

        io_sethandler(0x01ce, 0x0002, ati28800k_in, NULL, NULL, ati28800k_out, NULL, NULL, ati28800);
        io_sethandler(0x03c0, 0x0020, ati28800k_in, NULL, NULL, ati28800k_out, NULL, NULL, ati28800);

        ati28800->svga.miscout = 1;

        ati_eeprom_load(&ati28800->eeprom, L"atikorvga.nvr", 0);

        return ati28800;
}

static void *
ati28800_init(device_t *info)
{
    ati28800_t *ati;
    ati = malloc(sizeof(ati28800_t));
    memset(ati, 0x00, sizeof(ati28800_t));

	ati->memory = device_get_config_int("memory");	
	
    switch(info->local) {
	case GFX_VGAWONDERXL:
		rom_init_interleaved(&ati->bios_rom,
				     BIOS_VGAXL_EVEN_PATH,
				     BIOS_VGAXL_ODD_PATH,
				     0xc0000, 0x10000, 0xffff,
				     0, MEM_MAPPING_EXTERNAL);
		break;

#if defined(DEV_BRANCH) && defined(USE_XL24)
	case GFX_VGAWONDERXL24:
		rom_init_interleaved(&ati->bios_rom,
				     BIOS_XL24_EVEN_PATH,
				     BIOS_XL24_ODD_PATH,
				     0xc0000, 0x10000, 0xffff,
				     0, MEM_MAPPING_EXTERNAL);
		break;
#endif

	default:
		rom_init(&ati->bios_rom,
			 BIOS_ROM_PATH,
			 0xc0000, 0x8000, 0x7fff,
			 0, MEM_MAPPING_EXTERNAL);
		break;
    }

    svga_init(&ati->svga, ati, ati->memory << 10, /*default: 512kb*/
	      ati28800_recalctimings,
                   ati28800_in, ati28800_out,
                   NULL,
                   NULL);

    io_sethandler(0x01ce, 2,
		  ati28800_in, NULL, NULL,
		  ati28800_out, NULL, NULL, ati);
    io_sethandler(0x03c0, 32,
		  ati28800_in, NULL, NULL,
		  ati28800_out, NULL, NULL, ati);

    ati->svga.miscout = 1;

    ati_eeprom_load(&ati->eeprom, L"ati28800.nvr", 0);

    return(ati);
}


static int
ati28800_available(void)
{
    return(rom_present(BIOS_ROM_PATH));
}


static int
ati28800k_available()
{
    return ((rom_present(BIOS_ATIKOR_PATH) && rom_present(FONT_ATIKOR_PATH)));
}


static int
compaq_ati28800_available(void)
{
    return((rom_present(BIOS_VGAXL_EVEN_PATH) && rom_present(BIOS_VGAXL_ODD_PATH)));
}


#if defined(DEV_BRANCH) && defined(USE_XL24)
static int
ati28800_wonderxl24_available(void)
{
    return((rom_present(BIOS_XL24_EVEN_PATH) && rom_present(BIOS_XL24_ODD_PATH)));
}
#endif


static void
ati28800_close(void *priv)
{
    ati28800_t *ati = (ati28800_t *)priv;

    svga_close(&ati->svga);

    free(ati);
}


static void
ati28800_speed_changed(void *p)
{
        ati28800_t *ati28800 = (ati28800_t *)p;
        
        svga_recalctimings(&ati28800->svga);
}


static void
ati28800_force_redraw(void *priv)
{
    ati28800_t *ati = (ati28800_t *)priv;

    ati->svga.fullchange = changeframecount;
}

static void ati28800_add_status_info(char *s, int max_len, void *priv)
{
    ati28800_t *ati = (ati28800_t *)priv;

    svga_add_status_info(s, max_len, &ati->svga);
}


static device_config_t ati28800_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 512,
                {
                        {
                                "256 kB", 256
                        },
                        {
                                "512 kB", 512
                        },
                        {
                                "1024 kB", 1024
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

#if defined(DEV_BRANCH) && defined(USE_XL24)
static device_config_t ati28800_wonderxl_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 512,
                {
                        {
                                "256 kB", 256
                        },
                        {
                                "512 kB", 512
                        },
                        {
                                "1 MB", 1024
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
#endif

device_t ati28800_device =
{
        "ATI-28800",
        DEVICE_ISA,
	0,
        ati28800_init, ati28800_close, NULL,
        ati28800_available,
        ati28800_speed_changed,
        ati28800_force_redraw,
        ati28800_add_status_info,
	ati28800_config
};

device_t ati28800k_device =
{
        "ATI Korean VGA",
        DEVICE_ISA,
	0,
        ati28800k_init, ati28800_close, NULL,
        ati28800k_available,
        ati28800_speed_changed,
        ati28800_force_redraw,
        ati28800_add_status_info,
	ati28800_config
};

device_t compaq_ati28800_device =
{
        "Compaq ATI-28800",
        DEVICE_ISA,
	GFX_VGAWONDERXL,
        ati28800_init, ati28800_close, NULL,
        compaq_ati28800_available,
        ati28800_speed_changed,
        ati28800_force_redraw,
        ati28800_add_status_info,
	ati28800_config
};

#if defined(DEV_BRANCH) && defined(USE_XL24)
device_t ati28800_wonderxl24_device =
{
        "ATI-28800 (VGA Wonder XL24)",
        DEVICE_ISA,
	GFX_VGAWONDERXL24,
        ati28800_init, ati28800_close, NULL,
        ati28800_wonderxl24_available,
        ati28800_speed_changed,
        ati28800_force_redraw,
        ati28800_add_status_info,
	ati28800_wonderxl_config
};
#endif
