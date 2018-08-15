/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Oak OTI037C/67/077 emulation.
 *
 * Version:	@(#)vid_oak_oti.c	1.0.12	2018/08/14
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
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "video.h"
#include "vid_oak_oti.h"
#include "vid_svga.h"
#include "../machine/machine.h"

#define BIOS_37C_PATH			L"roms/video/oti/bios.bin"
#define BIOS_67_AMA932J_PATH	L"roms/machines/ama932j/oti067.bin"
#define BIOS_77_PATH			L"roms/video/oti/oti077.vbi"


typedef struct {
    svga_t svga;

    rom_t bios_rom;

    int index;
    uint8_t regs[32];

    uint8_t pos;

	uint8_t enable_register;
	
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
	uint8_t idx;

	if (!(oti->enable_register & 1) && addr != 0x3C3)
			return;	
	
    if ((((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && addr < 0x3de) &&
	!(svga->miscout & 1)) addr ^= 0x60;

    switch (addr) {
	case 0x3C3:
		oti->enable_register = val & 1;
		return;
		
	case 0x3D4:
		svga->crtcreg = val;
		return;

	case 0x3D5:
		if (svga->crtcreg & 0x20)
			return;
		if (((svga->crtcreg & 31) < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if (((svga->crtcreg & 31) == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);
		old = svga->crtc[svga->crtcreg & 31];
		svga->crtc[svga->crtcreg & 31] = val;
		if (old != val) {
			if ((svga->crtcreg & 31) < 0xE || (svga->crtcreg & 31) > 0x10) {
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}
		break;

	case 0x3DE: 
		oti->index = val; 
		return;

	case 0x3DF:
		idx = oti->index & 0x1f;
		oti->regs[idx] = val;
		switch (idx) {
			case 0xD:
				if (oti->chip_id)
				{
					svga->vram_display_mask = (val & 0xc) ? oti->vram_mask : 0x3ffff;
					if ((val & 0x80) && oti->vram_size == 256)
						mem_mapping_disable(&svga->mapping);
					else
						mem_mapping_enable(&svga->mapping);
					if (!(val & 0x80))
						svga->vram_display_mask = 0x3ffff;
				}
				else
				{
					if (val & 0x80)
							mem_mapping_disable(&svga->mapping);
					else
							mem_mapping_enable(&svga->mapping);
				}
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
	
	if (!(oti->enable_register & 1) && addr != 0x3C3)
			return 0xff;	
	
    if ((((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && addr < 0x3de) &&
	!(svga->miscout & 1)) addr ^= 0x60;
	
    switch (addr) {
	case 0x3C3:
		temp = oti->enable_register;
		break;
		
	case 0x3D4:
		temp = svga->crtcreg;
		break;

	case 0x3D5:
		if (svga->crtcreg & 0x20)
			temp = 0xff;
		else
			temp = svga->crtc[svga->crtcreg & 31];
		break;

	case 0x3DA:
                svga->attrff = 0;
                svga->attrff = 0;
                svga->cgastat &= ~0x30;
                /* copy color diagnostic info from the overscan color register */
                switch (svga->attrregs[0x12] & 0x30)
                {
                        case 0x00: /* P0 and P2 */
                        if (svga->attrregs[0x11] & 0x01)
                                svga->cgastat |= 0x10;
                        if (svga->attrregs[0x11] & 0x04)
                                svga->cgastat |= 0x20;
                        break;
                        case 0x10: /* P4 and P5 */
                        if (svga->attrregs[0x11] & 0x10)
                                svga->cgastat |= 0x10;
                        if (svga->attrregs[0x11] & 0x20)
                                svga->cgastat |= 0x20;
                        break;
                        case 0x20: /* P1 and P3 */
                        if (svga->attrregs[0x11] & 0x02)
                                svga->cgastat |= 0x10;
                        if (svga->attrregs[0x11] & 0x08)
                                svga->cgastat |= 0x20;
                        break;
                        case 0x30: /* P6 and P7 */
                        if (svga->attrregs[0x11] & 0x40)
                                svga->cgastat |= 0x10;
                        if (svga->attrregs[0x11] & 0x80)
                                svga->cgastat |= 0x20;
                        break;
                }
                return svga->cgastat;

	case 0x3DE: 
		temp = oti->index | (oti->chip_id << 5);
		break;	       

	case 0x3DF: 
		if ((oti->index & 0x1f)==0x10)
			temp = 0x18;
		  else
			temp = oti->regs[oti->index & 0x1f];
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

	svga->interlace = oti->regs[0x14] & 0x80;
}


static void *
oti_init(const device_t *info)
{
    oti_t *oti = malloc(sizeof(oti_t));
    wchar_t *romfn = NULL;

    memset(oti, 0x00, sizeof(oti_t));
    oti->chip_id = info->local;

    switch(oti->chip_id) {
	case 0:
		romfn = BIOS_37C_PATH;
		break;		
		
	case 2:
		if (romset == ROM_AMA932J) /*In case of any other future board uses another variant*/
		{
			romfn = BIOS_67_AMA932J_PATH;
			break;
		}
	case 5:
		romfn = BIOS_77_PATH;
		break;
    }

    rom_init(&oti->bios_rom, romfn,
	     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    oti->vram_size = device_get_config_int("memory");
    oti->vram_mask = (oti->vram_size << 10) - 1;
	
    svga_init(&oti->svga, oti, oti->vram_size << 10,
	      oti_recalctimings, oti_in, oti_out, NULL, NULL);

    io_sethandler(0x03c0, 32,
		  oti_in, NULL, NULL, oti_out, NULL, NULL, oti);
	io_sethandler(0x46e8, 1, oti_pos_in,NULL,NULL, oti_pos_out,NULL,NULL, oti);
    
	oti->svga.miscout = 1;

	oti->regs[0] = 0x08; /* fixme: bios wants to read this at index 0? this index is undocumented */
	
    return(oti);
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


static int
oti037c_available(void)
{
    return(rom_present(BIOS_37C_PATH));
}

static int
oti067_ama932j_available(void)
{
    return(rom_present(BIOS_67_AMA932J_PATH));
}

static int
oti067_077_available(void)
{
    return(rom_present(BIOS_77_PATH));
}


static const device_config_t oti067_config[] =
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


static const device_config_t oti077_config[] =
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

const device_t oti037c_device =
{
	"Oak OTI-037C",
	DEVICE_ISA,
	0,
	oti_init, oti_close, NULL,
	oti037c_available,
	oti_speed_changed,
	oti_force_redraw,
	oti067_config
};

const device_t oti067_device =
{
	"Oak OTI-067",
	DEVICE_ISA,
	2,
	oti_init, oti_close, NULL,
	oti067_077_available,
	oti_speed_changed,
	oti_force_redraw,
	oti067_config
};

const device_t oti067_ama932j_device =
{
	"Oak OTI-067 (AMA-932J)",
	DEVICE_ISA,
	2,
	oti_init, oti_close, NULL,
	oti067_ama932j_available,
	oti_speed_changed,
	oti_force_redraw,
	oti067_config
};

const device_t oti077_device =
{
	"Oak OTI-077",
	DEVICE_ISA,
	5,
	oti_init, oti_close, NULL,
	oti067_077_available,
	oti_speed_changed,
	oti_force_redraw,
	oti077_config
};
