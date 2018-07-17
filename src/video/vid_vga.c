/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		IBM VGA emulation.
 *
 * Version:	@(#)vid_vga.c	1.0.5	2018/04/26
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
#include "vid_vga.h"


typedef struct vga_t
{
        svga_t svga;
        
        rom_t bios_rom;
} vga_t;

void vga_out(uint16_t addr, uint8_t val, void *p)
{
        vga_t *vga = (vga_t *)p;
        svga_t *svga = &vga->svga;
        uint8_t old;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
		if (svga->crtcreg & 0x20)
			return;
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

uint8_t vga_in(uint16_t addr, void *p)
{
        vga_t *vga = (vga_t *)p;
        svga_t *svga = &vga->svga;
        uint8_t temp;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
             
        switch (addr)
        {
                case 0x3D4:
                temp = svga->crtcreg;
                break;
                case 0x3D5:
		if (svga->crtcreg & 0x20)
			temp = 0xff;
		else
                	temp = svga->crtc[svga->crtcreg];
                break;
                default:
                temp = svga_in(addr, svga);
                break;
        }
        return temp;
}


static void *vga_init(const device_t *info)
{
        vga_t *vga = malloc(sizeof(vga_t));
        memset(vga, 0, sizeof(vga_t));

        rom_init(&vga->bios_rom, L"roms/video/vga/ibm_vga.bin", 0xc0000, 0x8000, 0x7fff, 0x2000, MEM_MAPPING_EXTERNAL);

        svga_init(&vga->svga, vga, 1 << 18, /*256kb*/
                   NULL,
                   vga_in, vga_out,
                   NULL,
                   NULL);

        io_sethandler(0x03c0, 0x0020, vga_in, NULL, NULL, vga_out, NULL, NULL, vga);

        vga->svga.bpp = 8;
        vga->svga.miscout = 1;
        
        return vga;
}


#ifdef DEV_BRANCH
static void *trigem_unk_init(const device_t *info)
{
        vga_t *vga = malloc(sizeof(vga_t));
        memset(vga, 0, sizeof(vga_t));

        rom_init(&vga->bios_rom, L"roms/video/vga/ibm_vga.bin", 0xc0000, 0x8000, 0x7fff, 0x2000, MEM_MAPPING_EXTERNAL);

        svga_init(&vga->svga, vga, 1 << 18, /*256kb*/
                   NULL,
                   vga_in, vga_out,
                   NULL,
                   NULL);

        io_sethandler(0x03c0, 0x0020, vga_in, NULL, NULL, vga_out, NULL, NULL, vga);

	io_sethandler(0x22ca, 0x0002, svga_in, NULL, NULL, vga_out, NULL, NULL, vga);
	io_sethandler(0x22ce, 0x0002, svga_in, NULL, NULL, vga_out, NULL, NULL, vga);
	io_sethandler(0x32ca, 0x0002, svga_in, NULL, NULL, vga_out, NULL, NULL, vga);

        vga->svga.bpp = 8;
        vga->svga.miscout = 1;
        
        return vga;
}
#endif

/*PS/1 uses a standard VGA controller, but with no option ROM*/
void *ps1vga_init(const device_t *info)
{
        vga_t *vga = malloc(sizeof(vga_t));
        memset(vga, 0, sizeof(vga_t));
       
        svga_init(&vga->svga, vga, 1 << 18, /*256kb*/
                   NULL,
                   vga_in, vga_out,
                   NULL,
                   NULL);

        io_sethandler(0x03c0, 0x0020, vga_in, NULL, NULL, vga_out, NULL, NULL, vga);

        vga->svga.bpp = 8;
        vga->svga.miscout = 1;
        
        return vga;
}

static int vga_available(void)
{
        return rom_present(L"roms/video/vga/ibm_vga.bin");
}

void vga_close(void *p)
{
        vga_t *vga = (vga_t *)p;

        svga_close(&vga->svga);
        
        free(vga);
}

void vga_speed_changed(void *p)
{
        vga_t *vga = (vga_t *)p;
        
        svga_recalctimings(&vga->svga);
}

void vga_force_redraw(void *p)
{
        vga_t *vga = (vga_t *)p;

        vga->svga.fullchange = changeframecount;
}

const device_t vga_device =
{
        "VGA",
        DEVICE_ISA,
	0,
        vga_init,
        vga_close,
	NULL,
        vga_available,
        vga_speed_changed,
        vga_force_redraw,
        NULL
};
#ifdef DEV_BRANCH
const device_t trigem_unk_device =
{
        "VGA",
        DEVICE_ISA,
	0,
        trigem_unk_init,
        vga_close,
	NULL,
        vga_available,
        vga_speed_changed,
        vga_force_redraw,
        NULL
};
#endif
const device_t ps1vga_device =
{
        "PS/1 VGA",
        0,
	0,
        ps1vga_init,
        vga_close,
	NULL,
        vga_available,
        vga_speed_changed,
        vga_force_redraw,
        NULL
};
