/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the TI CF62011 SVGA chip.
 *
 *		This chip was used in several of IBM's later machines, such
 *		as the PS/1 Model 2121, and a number of PS/2 models. As noted
 *		in an article on Usenet:
 *
 *		"In the early 90s IBM looked for some cheap VGA card to
 *		 substitute the (relatively) expensive XGA-2 adapter for
 *		 *servers*, where the primary purpose is supervision of the
 *		 machine rather than real *work* with it in Hi-Res. It was
 *		 just to supply a base video, where a XGA-2 were a waste of
 *		 potential. They had a contract with TI for some DSPs in
 *		 multimedia already (the MWave for instance is based on
 *		 TI-DSPs as well as many Thinkpad internal chipsets) and TI
 *		 offered them a rather cheap – and inexpensive – chipset
 *		 and combined it with a cheap clock oscillator and an Inmos
 *		 RAMDAC. That chipset was already pretty much outdated at
 *		 that time but IBM decided it would suffice for that low
 *		 end purpose.
 *
 *		 Driver support was given under DOS and OS/2 only for base
 *		 functions like selection of the vertical refresh and few
 *		 different modes only. Not even the Win 3.x support has
 *		 been finalized. Technically the adapter could do better
 *		 than VGA, but its video BIOS is largely undocumented and
 *		 intentionally crippled down to a few functions."
 *
 *		This chip is reportedly the same one as used in the MCA
 *		IBM SVGA Adapter/A (ID 090EEh), which mostly had faster
 *		VRAM and RAMDAC. The VESA DOS graphics driver for that
 *		card can be used: m95svga.exe
 *
 *		The controller responds at ports in the range 0x2100-0x210F,
 *		which are the same as the XGA. It supports up to 1MB of VRAM,
 *		but we lock it down to 512K. The PS/1 2122 had 256K.
 *
 * Version:	@(#)vid_ti_cf62011.c	1.0.2	2017/11/05
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../video/video.h"
#include "../video/vid_vga.h"
#include "../video/vid_svga.h"
#include "vid_ti_cf62011.h"


typedef struct {
    svga_t	svga;

    rom_t	bios_rom;

    uint32_t	vram_size;

    uint8_t	banking;
    uint8_t	reg_2100;
    uint8_t	reg_210a;
} tivga_t;


static void
vid_out(uint16_t addr, uint8_t val, void *priv)
{
    tivga_t *ti = (tivga_t *)priv;
    svga_t *svga = &ti->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 ||
	 (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) addr ^= 0x60;

    switch (addr) {
	case 0x3D4:
		svga->crtcreg = val & 0x1f;
		return;

	case 0x3D5:
		if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);
		old = svga->crtc[svga->crtcreg];
		svga->crtc[svga->crtcreg] = val;
		if (old != val) {
			if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
				svga->fullchange = changeframecount;
				svga_recalctimings(svga);
			}
		}
		break;

	case 0x2100:
		ti->reg_2100 = val;
		if ((val & 7) < 4)
			svga->write_bank = 0;
		  else
			svga->write_bank = (ti->banking & 0x7) * 0x10000;
		svga->read_bank = svga->write_bank;
		break;

	case 0x2108:
		if ((ti->reg_2100 & 7) >= 4)
			svga->write_bank = (val & 0x7) * 0x10000;
		svga->read_bank = svga->write_bank;
		ti->banking = val;
		break;

	case 0x210a:
		ti->reg_210a = val;
		break;
    }

    svga_out(addr, val, svga);
}


static uint8_t
vid_in(uint16_t addr, void *priv)
{
    tivga_t *ti = (tivga_t *)priv;
    svga_t *svga = &ti->svga;
    uint8_t ret;

    if (((addr & 0xfff0) == 0x3d0 ||
	 (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) addr ^= 0x60;

    switch (addr) {
	case 0x100:
		ret = 0xfe;
		break;

	case 0x101:
		ret = 0xe8;
		break;

	case 0x3D4:
		ret = svga->crtcreg;
		break;

	case 0x3D5:
		ret = svga->crtc[svga->crtcreg];
		break;

	case 0x2108:
		ret = ti->banking;
		break;

	case 0x210a:
		ret = ti->reg_210a;
		break;

	default:
		ret = svga_in(addr, svga);
		break;
    }

    return(ret);
}


static void
vid_speed_changed(void *priv)
{
    tivga_t *ti = (tivga_t *)priv;

    svga_recalctimings(&ti->svga);
}


static void
vid_force_redraw(void *priv)
{
    tivga_t *ti = (tivga_t *)priv;

    ti->svga.fullchange = changeframecount;
}


static void
vid_add_status_info(char *s, int max_len, void *priv)
{
    tivga_t *ti = (tivga_t *)priv;

    svga_add_status_info(s, max_len, &ti->svga);
}


static void
vid_close(void *priv)
{
    tivga_t *ti = (tivga_t *)priv;

    svga_close(&ti->svga);

    free(ti);
}


static void *
vid_init(device_t *info)
{
    tivga_t *ti;

    /* Allocate control block and initialize. */
    ti = (tivga_t *)malloc(sizeof(tivga_t));
    memset(ti, 0x00, sizeof(tivga_t));

    /* Set amount of VRAM in KB. */
    if (info->local == 0)
	ti->vram_size = device_get_config_int("vram_size");
      else
	ti->vram_size = info->local;

    pclog("VIDEO: initializing %s, %dK VRAM\n", info->name, ti->vram_size);

    svga_init(&ti->svga, ti, ti->vram_size<<10, NULL, vid_in, vid_out, NULL, NULL);

    io_sethandler(0x0100, 2, vid_in, NULL, NULL, vid_out, NULL, NULL, ti);
    io_sethandler(0x03c0, 32, vid_in, NULL, NULL, vid_out, NULL, NULL, ti);
    io_sethandler(0x2100, 16, vid_in, NULL, NULL, vid_out, NULL, NULL, ti);

    ti->svga.bpp = 8;
    ti->svga.miscout = 1;

    return(ti);
}


static device_config_t vid_config[] =
{
        {
                "vram_size", "Memory Size", CONFIG_SELECTION, "", 256,
                {
                        {
                                "256K", 256
                        },
                        {
                                "512K", 512
                        },
                        {
                                "1024K", 1024
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


device_t ti_cf62011_device = {
    "TI CF62011 SVGA",
    0,
    0,
    vid_init, vid_close, NULL,
    NULL,
    vid_speed_changed,
    vid_force_redraw,
    vid_add_status_info,
    vid_config
};


device_t ibm_ps1_2121_device = {
    "IBM PS/1 Model 2121 SVGA",
    0,
    256,
    vid_init, vid_close, NULL,
    NULL,
    vid_speed_changed,
    vid_force_redraw,
    vid_add_status_info,
    NULL
};
