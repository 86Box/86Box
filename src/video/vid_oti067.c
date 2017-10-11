/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Oak OTI067/077 emulation.
 *
 * Version:	@(#)vid_oti067.c	1.0.1	2017/10/10
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
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_oti067.h"
#include "vid_svga.h"


typedef struct {
    svga_t svga;

    rom_t bios_rom;

    int index;
    uint8_t regs[32];

    uint8_t pos;

    uint32_t vram_size;
    uint32_t vram_mask;

    uint8_t chip_id;
} oti_t;


static void
oti_out(uint16_t addr, uint8_t val, void *p)
{
    oti_t *oti = (oti_t *)p;
    svga_t *svga = &oti->svga;
    uint8_t old;

    if ((((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && addr < 0x3de) &&
	!(svga->miscout & 1)) addr ^= 0x60;

    switch (addr) {
	case 0x3D4:
		svga->crtcreg = val & 31;
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
		if (old != val) {
			if (svga->crtcreg < 0xE || svga->crtcreg > 0x10) {
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}
		break;

	case 0x3DE: 
		oti->index = val & 0x1f; 
		return;

	case 0x3DF:
		oti->regs[oti->index] = val;
		switch (oti->index) {
			case 0xD:
				svga->vrammask = (val & 0xc) ? oti->vram_mask : 0x3ffff;
				if ((val & 0x80) && oti->vram_size == 256)
					mem_mapping_disable(&svga->mapping);
				else
					mem_mapping_enable(&svga->mapping);
				if (!(val & 0x80))
					svga->vrammask = 0x3ffff;
				break;

			case 0x11:
				svga->read_bank = (val & 0xf) * 65536;
				svga->write_bank = (val >> 4) * 65536;
				break;
		}
		return;
    }

    svga_out(addr, val, svga);
}


static uint8_t
oti_in(uint16_t addr, void *p)
{
    oti_t *oti = (oti_t *)p;
    svga_t *svga = &oti->svga;
    uint8_t temp;
	
    if ((((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && addr < 0x3de) &&
	!(svga->miscout & 1)) addr ^= 0x60;
	
    switch (addr) {
	case 0x3D4:
		temp = svga->crtcreg;
		break;

	case 0x3D5:
		temp = svga->crtc[svga->crtcreg];
		break;
		
	case 0x3DE: 
		temp = oti->index | (oti->chip_id << 5);
		break;	       

	case 0x3DF: 
		if (oti->index==0x10)
			temp = 0x18;
		  else
			temp = oti->regs[oti->index];
		break;

	default:
		temp = svga_in(addr, svga);
		break;
    }

    return(temp);
}


static void
oti_pos_out(uint16_t addr, uint8_t val, void *p)
{
    oti_t *oti = (oti_t *)p;

    if ((val & 8) != (oti->pos & 8)) {
	if (val & 8)
		io_sethandler(0x03c0, 32, oti_in, NULL, NULL,
			      oti_out, NULL, NULL, oti);
	else
		io_removehandler(0x03c0, 32, oti_in, NULL, NULL,
				 oti_out, NULL, NULL, oti);
    }

    oti->pos = val;
}


static uint8_t
oti_pos_in(uint16_t addr, void *p)
{
    oti_t *oti = (oti_t *)p;

    return(oti->pos);
}	


static void
oti_recalctimings(svga_t *svga)
{
    oti_t *oti = (oti_t *)svga->p;

    if (oti->regs[0x14] & 0x08) svga->ma_latch |= 0x10000;

    if (oti->regs[0x0d] & 0x0c) svga->rowoffset <<= 1;

    if (oti->regs[0x14] & 0x80) {
	svga->vtotal *= 2;
	svga->dispend *= 2;
	svga->vblankstart *= 2;
	svga->vsyncstart *=2;
	svga->split *= 2;			
    }
}


static void *
oti_common_init(wchar_t *bios_fn, int vram_size, int chip_id)
{
    oti_t *oti = malloc(sizeof(oti_t));

    memset(oti, 0x00, sizeof(oti_t));

    rom_init(&oti->bios_rom, bios_fn,
	     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    oti->vram_size = vram_size;
    oti->vram_mask = (vram_size << 10) - 1;

    oti->chip_id = chip_id;
	
    svga_init(&oti->svga, oti, vram_size << 10,
	      oti_recalctimings, oti_in, oti_out, NULL, NULL);

    io_sethandler(0x03c0, 32,
		  oti_in, NULL, NULL, oti_out, NULL, NULL, oti);
    io_sethandler(0x46e8, 1, oti_pos_in,NULL,NULL, oti_pos_out,NULL,NULL, oti);
	
    oti->svga.miscout = 1;

    return(oti);
}


static void *
oti067_init(device_t *info)
{
    int vram_size = device_get_config_int("memory");

    return(oti_common_init(L"roms/video/oti/bios.bin", vram_size, 2));
}


static void *
oti077_init(device_t *info)
{
    int vram_size = device_get_config_int("memory");

    return(oti_common_init(L"roms/video/oti/oti077.vbi", vram_size, 5));
}


static int
oti067_available(void)
{
    return(rom_present(L"roms/video/oti/bios.bin"));
}


static int
oti077_available(void)
{
    return(rom_present(L"roms/video/oti/oti077.vbi"));
}


static void
oti_close(void *p)
{
    oti_t *oti = (oti_t *)p;

    svga_close(&oti->svga);

    free(oti);
}


static void
oti_speed_changed(void *p)
{
    oti_t *oti = (oti_t *)p;

    svga_recalctimings(&oti->svga);
}
	

static void
oti_force_redraw(void *p)
{
    oti_t *oti = (oti_t *)p;

    oti->svga.fullchange = changeframecount;
}


static void
oti_add_status_info(char *s, int max_len, void *p)
{
    oti_t *oti = (oti_t *)p;

    svga_add_status_info(s, max_len, &oti->svga);
}


static device_config_t oti067_config[] =
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
				""
			}
		}
	},
	{
		"", "", -1
	}
};

static device_config_t oti077_config[] =
{
	{
		"memory", "Memory size", CONFIG_SELECTION, "", 1024,
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


device_t oti067_device =
{
	"Oak OTI-067",
	DEVICE_ISA,
	0,
	oti067_init,
	oti_close,
	NULL,
	oti067_available,
	oti_speed_changed,
	oti_force_redraw,
	oti_add_status_info,
	oti067_config
};

device_t oti077_device =
{
	"Oak OTI-077",
	DEVICE_ISA,
	0,
	oti077_init,
	oti_close,
	NULL,
	oti077_available,
	oti_speed_changed,
	oti_force_redraw,
	oti_add_status_info,
	oti077_config
};
