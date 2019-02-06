/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Trident TVGA (8900D) emulation.
 *
 * Version:	@(#)vid_tvga.c	1.0.9	2018/12/28
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
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_tkd8001_ramdac.h"
#include "vid_tvga.h"

#define TVGA8900B_ID		0x03
#define TVGA8900CLD_ID		0x33

#define ROM_TVGA_8900B		L"roms/video/tvga/tvga8900B.VBI"
#define ROM_TVGA_8900CLD	L"roms/video/tvga/trident.bin"

typedef struct tvga_t
{
        mem_mapping_t linear_mapping;
        mem_mapping_t accel_mapping;

        svga_t svga;

        rom_t bios_rom;
	uint8_t	card_id;

        uint8_t tvga_3d8, tvga_3d9;
        int oldmode;
        uint8_t oldctrl1;
        uint8_t oldctrl2, newctrl2;
        
        int vram_size;
        uint32_t vram_mask;
} tvga_t;

video_timings_t timing_tvga = {VIDEO_ISA, 3,  3,  6,   8,  8, 12};

static uint8_t crtc_mask[0x40] =
{
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x7f, 0xff, 0x3f, 0x7f, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xef,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
        0x7f, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x00, 0x03,
        0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void tvga_recalcbanking(tvga_t *tvga);
void tvga_out(uint16_t addr, uint8_t val, void *p)
{
        tvga_t *tvga = (tvga_t *)p;
        svga_t *svga = &tvga->svga;

        uint8_t old;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout & 1)) addr ^= 0x60;

        switch (addr)
        {
                case 0x3C5:
                switch (svga->seqaddr & 0xf)
                {
                        case 0xB: 
                        tvga->oldmode=1; 
                        break;
                        case 0xC: 
                        if (svga->seqregs[0xe] & 0x80) 
                                svga->seqregs[0xc] = val; 
                        break;
                        case 0xd: 
                        if (tvga->oldmode) 
                                tvga->oldctrl2 = val;                                
                        else
                        {
                                tvga->newctrl2 = val;
                                svga_recalctimings(svga);
                        }
                        break;
                        case 0xE:
                        if (tvga->oldmode) 
                                tvga->oldctrl1 = val; 
                        else 
                        {
                                svga->seqregs[0xe] = val ^ 2;
                                tvga->tvga_3d8 = svga->seqregs[0xe] & 0xf;
                                tvga_recalcbanking(tvga);
                        }
                        return;
                }
                break;

                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                tkd8001_ramdac_out(addr, val, svga->ramdac, svga);
                return;

                case 0x3CF:
                switch (svga->gdcaddr & 15)
                {
                        case 0xE:
                        svga->gdcreg[0xe] = val ^ 2;
                        tvga->tvga_3d9 = svga->gdcreg[0xe] & 0xf;
                        tvga_recalcbanking(tvga);
                        break;
                        case 0xF:
                        svga->gdcreg[0xf] = val;
                        tvga_recalcbanking(tvga);
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
                val &= crtc_mask[svga->crtcreg];
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
                        case 0x1e:
                        svga->vram_display_mask = (val & 0x80) ? tvga->vram_mask : 0x3ffff;
                        break;
                }
                return;
                case 0x3D8:
                if (svga->gdcreg[0xf] & 4)
                {
                        tvga->tvga_3d8 = val;
                        tvga_recalcbanking(tvga);
                }
                return;
                case 0x3D9:
                if (svga->gdcreg[0xf] & 4)
                {
                        tvga->tvga_3d9 = val;
                        tvga_recalcbanking(tvga);
                }
                return;
        }
        svga_out(addr, val, svga);
}

uint8_t tvga_in(uint16_t addr, void *p)
{
        tvga_t *tvga = (tvga_t *)p;
        svga_t *svga = &tvga->svga;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout & 1)) addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3C5:
                if ((svga->seqaddr & 0xf) == 0xb)
                {
                        tvga->oldmode = 0;
                        return tvga->card_id; /*Must be at least a TVGA8900*/
                }
                if ((svga->seqaddr & 0xf) == 0xd)
                {
                        if (tvga->oldmode) return tvga->oldctrl2;
                        return tvga->newctrl2;
                }
                if ((svga->seqaddr & 0xf) == 0xe)
                {
                        if (tvga->oldmode) 
                                return tvga->oldctrl1; 
                }
                break;
                case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
                return tkd8001_ramdac_in(addr, svga->ramdac, svga);
                case 0x3D4:
                return svga->crtcreg;
                case 0x3D5:
                if (svga->crtcreg > 0x18 && svga->crtcreg < 0x1e)
                        return 0xff;
                return svga->crtc[svga->crtcreg];
                case 0x3d8:
		return tvga->tvga_3d8;
		case 0x3d9:
		return tvga->tvga_3d9;
        }
        return svga_in(addr, svga);
}

static void tvga_recalcbanking(tvga_t *tvga)
{
        svga_t *svga = &tvga->svga;
        
        svga->write_bank = (tvga->tvga_3d8 & 0x1f) * 65536;

        if (svga->gdcreg[0xf] & 1)
                svga->read_bank = (tvga->tvga_3d9 & 0x1f) * 65536;
        else
                svga->read_bank = svga->write_bank;
}

void tvga_recalctimings(svga_t *svga)
{
        tvga_t *tvga = (tvga_t *)svga->p;
        if (!svga->rowoffset) svga->rowoffset = 0x100; /*This is the only sensible way I can see this being handled,
                                                         given that TVGA8900D has no overflow bits.
                                                         Some sort of overflow is required for 320x200x24 and 1024x768x16*/
        if (svga->crtc[0x29] & 0x10)
                svga->rowoffset += 0x100;

        if (svga->bpp == 24)
                svga->hdisp = (svga->crtc[1] + 1) * 8;
        
	if ((svga->crtc[0x1e] & 0xA0) == 0xA0) svga->ma_latch |= 0x10000;
	if ((svga->crtc[0x27] & 0x01) == 0x01) svga->ma_latch |= 0x20000;
	if ((svga->crtc[0x27] & 0x02) == 0x02) svga->ma_latch |= 0x40000;
	
        if (tvga->oldctrl2 & 0x10)
        {
                svga->rowoffset <<= 1;
                svga->ma_latch <<= 1;
        }
	
        if (svga->gdcreg[0xf] & 0x08)
        {
                svga->htotal *= 2;
                svga->hdisp *= 2;
                svga->hdisp_time *= 2;
        }
	   
	svga->interlace = (svga->crtc[0x1e] & 4);
	   
        if (svga->interlace)
                svga->rowoffset >>= 1;

        switch (((svga->miscout >> 2) & 3) | ((tvga->newctrl2 << 2) & 4))
        {
                case 2: svga->clock = cpuclock/44900000.0; break;
                case 3: svga->clock = cpuclock/36000000.0; break;
                case 4: svga->clock = cpuclock/57272000.0; break;
                case 5: svga->clock = cpuclock/65000000.0; break;
                case 6: svga->clock = cpuclock/50350000.0; break;
                case 7: svga->clock = cpuclock/40000000.0; break;
        }

        if (tvga->oldctrl2 & 0x10)
        {
                switch (svga->bpp)
                {
                        case 8: 
                        svga->render = svga_render_8bpp_highres;
                        break;
                        case 15: 
                        svga->render = svga_render_15bpp_highres; 
                        svga->hdisp /= 2;
                        break;
                        case 16: 
                        svga->render = svga_render_16bpp_highres; 
                        svga->hdisp /= 2;
                        break;
                        case 24: 
                        svga->render = svga_render_24bpp_highres;
                        svga->hdisp /= 3;
                        break;
                }
                svga->lowres = 0;
        }
}


static void *tvga_init(const device_t *info)
{
	const wchar_t *bios_fn;
        tvga_t *tvga = malloc(sizeof(tvga_t));
        memset(tvga, 0, sizeof(tvga_t));

	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_tvga);
        
        tvga->vram_size = device_get_config_int("memory") << 10;
        tvga->vram_mask = tvga->vram_size - 1;

	tvga->card_id = info->local;
	
	switch (info->local)
	{
		case TVGA8900B_ID:
			bios_fn = ROM_TVGA_8900B;
			break;
		case TVGA8900CLD_ID:
			bios_fn = ROM_TVGA_8900CLD;
			break;
		default:
			free(tvga);
			return NULL;
	}
	
        rom_init(&tvga->bios_rom, (wchar_t *) bios_fn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        
        svga_init(&tvga->svga, tvga, tvga->vram_size,
                   tvga_recalctimings,
                   tvga_in, tvga_out,
                   NULL,
                   NULL);

	tvga->svga.ramdac = device_add(&tkd8001_ramdac_device);
       
        io_sethandler(0x03c0, 0x0020, tvga_in, NULL, NULL, tvga_out, NULL, NULL, tvga);

        return tvga;
}

static int tvga8900b_available(void)
{
        return rom_present(ROM_TVGA_8900B);
}

static int tvga8900d_available(void)
{
        return rom_present(ROM_TVGA_8900CLD);
}

void tvga_close(void *p)
{
        tvga_t *tvga = (tvga_t *)p;
        
        svga_close(&tvga->svga);
        
        free(tvga);
}

void tvga_speed_changed(void *p)
{
        tvga_t *tvga = (tvga_t *)p;
        
        svga_recalctimings(&tvga->svga);
}

void tvga_force_redraw(void *p)
{
        tvga_t *tvga = (tvga_t *)p;

        tvga->svga.fullchange = changeframecount;
}

static const device_config_t tvga_config[] =
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
                         /*Chip supports 2mb, but drivers are buggy*/
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

const device_t tvga8900b_device =
{
        "Trident TVGA 8900B",
        DEVICE_ISA,
	TVGA8900B_ID,
        tvga_init,
        tvga_close,
	NULL,
        tvga8900b_available,
        tvga_speed_changed,
        tvga_force_redraw,
        tvga_config
};

const device_t tvga8900d_device =
{
        "Trident TVGA 8900D",
        DEVICE_ISA,
	TVGA8900CLD_ID,
        tvga_init,
        tvga_close,
	NULL,
        tvga8900d_available,
        tvga_speed_changed,
        tvga_force_redraw,
        tvga_config
};
