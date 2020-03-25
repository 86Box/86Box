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
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		GreatPsycho, <greatpsycho@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "86box.h"
#include "86box_io.h"
#include "mca.h"
#include "mem.h"
#include "rom.h"
#include "device.h"
#include "timer.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


#define BIOS_ROM_PATH		L"roms/video/et4000/et4000.bin"
#define KOREAN_BIOS_ROM_PATH 	L"roms/video/et4000/tgkorvga.bin"
#define KOREAN_FONT_ROM_PATH 	L"roms/video/et4000/tg_ksc5601.rom"


typedef struct {
    const char		*name;
    int			type;

    svga_t		svga;

    uint8_t		pos_regs[8];

    rom_t		bios_rom;

    uint8_t		banking;
    uint32_t		vram_size,
			vram_mask;

    uint8_t		port_22cb_val;
    uint8_t		port_32cb_val;
    int			get_korean_font_enabled;
    int			get_korean_font_index;
    uint16_t		get_korean_font_base;
} et4000_t;


static const uint8_t crtc_mask[0x40] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static video_timings_t timing_et4000_isa = {VIDEO_ISA, 3,  3,  6,   5,  5, 10};
static video_timings_t timing_et4000_mca = {VIDEO_MCA, 4,  5, 10,   5,  5, 10};


static uint8_t
et4000_in(uint16_t addr, void *priv)
{
    et4000_t *dev = (et4000_t *)priv;
    svga_t *svga = &dev->svga;

    if (((addr & 0xfff0) == 0x3d0 ||
	 (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) addr ^= 0x60;

    switch (addr) {
	case 0x3c2:
		if (dev->type == 1) {
			if ((svga->vgapal[0].r + svga->vgapal[0].g + svga->vgapal[0].b) >= 0x4e)
				return 0;
			else
				return 0x10;					
		}
		break;

	case 0x3c5:
		if ((svga->seqaddr & 0xf) == 7)
			return svga->seqregs[svga->seqaddr & 0xf] | 4;
                break;

	case 0x3c6:
	case 0x3c7:
	case 0x3c8:
	case 0x3c9:
                return sc1502x_ramdac_in(addr, svga->ramdac, svga);

	case 0x3cd: /*Banking*/
                return dev->banking;

	case 0x3d4:
                return svga->crtcreg;

	case 0x3d5:
                return svga->crtc[svga->crtcreg];
    }

    return svga_in(addr, svga);
}


static uint8_t
et4000k_in(uint16_t addr, void *priv)
{
    et4000_t *dev = (et4000_t *)priv;
    uint8_t val = 0xff;
        
    switch (addr) {
	case 0x22cb:
		return dev->port_22cb_val;

	case 0x22cf:
		val = 0;
		switch(dev->get_korean_font_enabled) {
			case 3:
				if ((dev->port_32cb_val & 0x30) == 0x30) {
					val = fontdatksc5601[dev->get_korean_font_base].chr[dev->get_korean_font_index++];
					dev->get_korean_font_index &= 0x1f;
				} else
				if ((dev->port_32cb_val & 0x30) == 0x20 &&
				    (dev->get_korean_font_base & 0x7f) > 0x20 &&
				    (dev->get_korean_font_base & 0x7f) < 0x7f) {
					switch(dev->get_korean_font_base & 0x3f80) {
						case 0x2480:
							if (dev->get_korean_font_index < 16)
								val = fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index];
							else
							if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
								val = fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8];
							break;

						case 0x3f00:
							if (dev->get_korean_font_index < 16)
								val = fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index];
							else
							if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
								val = fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8];
							break;

						default:
							break;
					}
					dev->get_korean_font_index++;
					dev->get_korean_font_index %= 72;
				}
				break;

			case 4:
				val = 0x0f;
				break;

			default:
				break;
		}
                return val;

	case 0x32cb:
		return dev->port_32cb_val;

	default:
		return et4000_in(addr, priv);
    }
}


static void
et4000_out(uint16_t addr, uint8_t val, void *priv)
{
    et4000_t *dev = (et4000_t *)priv;
    svga_t *svga = &dev->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 ||
	 (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) addr ^= 0x60;

    switch (addr) {
	case 0x3c6:
	case 0x3c7:
	case 0x3c8:
	case 0x3c9:
		sc1502x_ramdac_out(addr, val, svga->ramdac, svga);
		return;

	case 0x3cd: /*Banking*/
		if (!(svga->crtc[0x36] & 0x10) && !(svga->gdcreg[6] & 0x08)) {
			svga->write_bank = (val & 0xf) * 0x10000;
			svga->read_bank = ((val >> 4) & 0xf) * 0x10000;
		}
		dev->banking = val;
		return;

	case 0x3cf:
		if ((svga->gdcaddr & 15) == 6) {
			if (!(svga->crtc[0x36] & 0x10) && !(val & 0x08)) {
				svga->write_bank = (dev->banking & 0x0f) * 0x10000;
				svga->read_bank = ((dev->banking >> 4) & 0x0f) * 0x10000;
			} else
				svga->write_bank = svga->read_bank = 0;
		}
		break;

	case 0x3d4:
		svga->crtcreg = val & 0x3f;
		return;

	case 0x3d5:
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
				svga->write_bank = (dev->banking & 0x0f) * 0x10000;
				svga->read_bank = ((dev->banking >> 4) & 0x0f) * 0x10000;
			} else
				svga->write_bank = svga->read_bank = 0;
		}

		if (old != val) {
			if (svga->crtcreg < 0x0e || svga->crtcreg > 0x10) {
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}				

		/*
		 * Note - Silly hack to determine video memory
		 * size automatically by ET4000 BIOS.
		 */
		if ((svga->crtcreg == 0x37) && (dev->type != 1)) {
			switch (val & 0x0b) {
				case 0x00:
				case 0x01:
					if (svga->vram_max == 64 * 1024)
						mem_mapping_enable(&svga->mapping);
					else
						mem_mapping_disable(&svga->mapping);
					break;

				case 0x02:
					if (svga->vram_max == 128 * 1024)
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

				case 0x0a:
					if (svga->vram_max == 512 * 1024)
						mem_mapping_enable(&svga->mapping);
					else
						mem_mapping_disable(&svga->mapping);
					break;

				case 0x0b:
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


static void
et4000k_out(uint16_t addr, uint8_t val, void *priv)
{
    et4000_t *dev = (et4000_t *)priv;

    switch (addr) {
	case 0x22cb:
		dev->port_22cb_val = (dev->port_22cb_val & 0xf0) | (val & 0x0f);
		dev->get_korean_font_enabled = val & 7;
		if (dev->get_korean_font_enabled == 3)
			dev->get_korean_font_index = 0;
		break;

	case 0x22cf:
		switch(dev->get_korean_font_enabled) {
			case 1:
				dev->get_korean_font_base = ((val & 0x7f) << 7) | (dev->get_korean_font_base & 0x7f);
				break;

			case 2:
				dev->get_korean_font_base = (dev->get_korean_font_base & 0x3f80) | (val & 0x7f) | (((val ^ 0x80) & 0x80) << 8);
				break;

			case 3:
				if ((dev->port_32cb_val & 0x30) == 0x20 &&
				    (dev->get_korean_font_base & 0x7f) > 0x20 &&
				    (dev->get_korean_font_base & 0x7f) < 0x7f) {
					switch (dev->get_korean_font_base & 0x3f80) {
						case 0x2480:
							if (dev->get_korean_font_index < 16)
								fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index] = val;
							else
							if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
								fontdatksc5601_user[(dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8] = val;
							break;

						case 0x3f00:
							if (dev->get_korean_font_index < 16)
								fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index] = val;
							else
							if (dev->get_korean_font_index >= 24 && dev->get_korean_font_index < 40)
								fontdatksc5601_user[96 + (dev->get_korean_font_base & 0x7f) - 0x20].chr[dev->get_korean_font_index - 8] = val;
							break;

						default:
							break;
					}
					dev->get_korean_font_index++;
				}
				break;

			default:
				break;
		}
		break;

	case 0x32cb:
		dev->port_32cb_val = val;
		svga_recalctimings(&dev->svga);
		break;

	default:
		et4000_out(addr, val, priv);
		break;
    }
}


static void
et4000_recalctimings(svga_t *svga)
{
    et4000_t *dev = (et4000_t *)svga->p;

    svga->ma_latch |= (svga->crtc[0x33]&3)<<16;
    if (svga->crtc[0x35] & 1)    svga->vblankstart += 0x400;
    if (svga->crtc[0x35] & 2)    svga->vtotal += 0x400;
    if (svga->crtc[0x35] & 4)    svga->dispend += 0x400;
    if (svga->crtc[0x35] & 8)    svga->vsyncstart += 0x400;
    if (svga->crtc[0x35] & 0x10) svga->split += 0x400;
    if (!svga->rowoffset)        svga->rowoffset = 0x100;
    if (svga->crtc[0x3f] & 1)    svga->htotal += 256;
    if (svga->attrregs[0x16] & 0x20) svga->hdisp <<= 1;

    switch (((svga->miscout >> 2) & 3) | ((svga->crtc[0x34] << 1) & 4)) {
	case 0:
	case 1:
		break;

	case 3:
		svga->clock = (cpuclock * (double)(1ull << 32)) / 40000000.0;
		break;

	case 5:
		svga->clock = (cpuclock * (double)(1ull << 32)) / 65000000.0;
		break;

	default:
		svga->clock = (cpuclock * (double)(1ull << 32)) / 36000000.0;
		break;
    }
        
    switch (svga->bpp) {
	case 15:
	case 16:
		svga->hdisp /= 2;
		break;

	case 24:
		svga->hdisp /= 3;
		break;
    }

    if (dev->type == 2 || dev->type == 3) {
#if NOT_YET
	if ((svga->render == svga_render_text_80) && ((svga->crtc[0x37] & 0x0A) == 0x0A)) {
		if ((dev->port_32cb_val & 0xB4) == ((svga->crtc[0x37] & 3) == 2 ? 0xB4 : 0xB0)) {
			svga->render = svga_render_text_80_ksc5601;
		}
	}
#endif
    }
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


static uint8_t
et4000_mca_feedb(void *priv)
{
    return 1;
}


static void *
et4000_init(const device_t *info)
{
    const wchar_t *fn;
    et4000_t *dev;

    dev = (et4000_t *)malloc(sizeof(et4000_t));
    memset(dev, 0x00, sizeof(et4000_t));
    dev->name = info->name;
    dev->type = info->local;
    fn = BIOS_ROM_PATH;

    switch(dev->type) {
	case 0:		/* ISA ET4000AX */
		dev->vram_size = device_get_config_int("memory") << 10;
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000_isa);
		svga_init(&dev->svga, dev, dev->vram_size,
			  et4000_recalctimings, et4000_in, et4000_out,
			  NULL, NULL);
		io_sethandler(0x03c0, 32,
			      et4000_in,NULL,NULL, et4000_out,NULL,NULL, dev);
		break;

	case 1:		/* MCA ET4000AX */
		dev->vram_size = 1024 << 10;
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000_mca);
		svga_init(&dev->svga, dev, dev->vram_size,
			  et4000_recalctimings, et4000_in, et4000_out,
			  NULL, NULL);
		io_sethandler(0x03c0, 32,
			      et4000_in,NULL,NULL, et4000_out,NULL,NULL, dev);
		dev->pos_regs[0] = 0xf2;	/* ET4000 MCA board ID */
		dev->pos_regs[1] = 0x80;	
		mca_add(et4000_mca_read, et4000_mca_write, et4000_mca_feedb, NULL, dev);
		break;

	case 2:		/* Korean ET4000 */
	case 3:		/* Trigem 286M ET4000 */
		dev->vram_size = device_get_config_int("memory") << 10;
		dev->port_22cb_val = 0x60;
		dev->port_32cb_val = 0;
		dev->svga.ksc5601_sbyte_mask = 0x80;
		video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_et4000_isa);
		svga_init(&dev->svga, dev, dev->vram_size,
			  et4000_recalctimings, et4000k_in, et4000k_out,
			  NULL, NULL);
		io_sethandler(0x03c0, 32,
			      et4000k_in,NULL,NULL, et4000k_out,NULL,NULL, dev);
		io_sethandler(0x22cb, 1,
			      et4000k_in,NULL,NULL, et4000k_out,NULL,NULL, dev);
		io_sethandler(0x22cf, 1,
			      et4000k_in,NULL,NULL, et4000k_out,NULL,NULL, dev);
		io_sethandler(0x32cb, 1,
			      et4000k_in,NULL,NULL, et4000k_out,NULL,NULL, dev);
		loadfont(KOREAN_FONT_ROM_PATH, 6);
        	fn = KOREAN_BIOS_ROM_PATH;
		break;
    }

    dev->svga.ramdac = device_add(&sc1502x_ramdac_device);

    dev->vram_mask = dev->vram_size - 1;

    rom_init(&dev->bios_rom, (wchar_t *) fn,
	     0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    return(dev);
}


static void
et4000_close(void *priv)
{
    et4000_t *dev = (et4000_t *)priv;

    svga_close(&dev->svga);

    free(dev);
}


static void
et4000_speed_changed(void *priv)
{
    et4000_t *dev = (et4000_t *)priv;

    svga_recalctimings(&dev->svga);
}


static void
et4000_force_redraw(void *priv)
{
    et4000_t *dev = (et4000_t *)priv;

    dev->svga.fullchange = changeframecount;
}


static int
et4000_available(void)
{
    return rom_present(BIOS_ROM_PATH);
}


static int
et4000k_available(void)
{
    return rom_present(KOREAN_BIOS_ROM_PATH) &&
	   rom_present(KOREAN_FONT_ROM_PATH);
}


static const device_config_t et4000_config[] =
{
	{
		"memory", "Memory size", CONFIG_SELECTION, "", 1024,
		{
			{
				"256 KB", 256
			},
			{
				"512 KB", 512
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

const device_t et4000_isa_device = {
    "Tseng Labs ET4000AX (ISA)",
    DEVICE_ISA,
    0,
    et4000_init, et4000_close, NULL,
    et4000_available,
    et4000_speed_changed,
    et4000_force_redraw,
    et4000_config
};

const device_t et4000_mca_device = {
    "Tseng Labs ET4000AX (MCA)",
    DEVICE_MCA,
    1,
    et4000_init, et4000_close, NULL,
    et4000_available,
    et4000_speed_changed,
    et4000_force_redraw,
    et4000_config
};

const device_t et4000k_isa_device = {
    "Trigem Korean VGA (Tseng Labs ET4000AX Korean)",
    DEVICE_ISA,
    2,
    et4000_init, et4000_close, NULL,
    et4000k_available,
    et4000_speed_changed,
    et4000_force_redraw,
    et4000_config
};

const device_t et4000k_tg286_isa_device = {
    "Trigem Korean VGA (Trigem 286M)",
    DEVICE_ISA,
    3,
    et4000_init, et4000_close, NULL,
    et4000k_available,
    et4000_speed_changed,
    et4000_force_redraw,
    et4000_config
};
