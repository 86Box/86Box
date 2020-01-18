/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		ATI 18800 emulation (VGA Edge-16)
 *
 * Version:	@(#)vid_ati18800.c	1.0.15	2020/01/18
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
#include "../timer.h"
#include "video.h"
#include "vid_ati18800.h"
#include "vid_ati_eeprom.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
#define BIOS_ROM_PATH_WONDER	L"roms/video/ati18800/VGA_Wonder_V3-1.02.bin"
#endif
#define BIOS_ROM_PATH_VGA88	L"roms/video/ati18800/vga88.bin"
#define BIOS_ROM_PATH_EDGE16	L"roms/video/ati18800/vgaedge16.vbi"
#define BIOS_ROM_PATH_EGAWONDER800P	L"roms/video/ati18800/ATI EGA Wonder 800+ N1.00.BIN"

enum {
#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
	ATI18800_WONDER = 0,
	ATI18800_VGA88,
	ATI18800_EDGE16,
	ATI18800_EGAWONDER800P
#else
	ATI18800_VGA88 = 0,
	ATI18800_EDGE16,
	ATI18800_EGAWONDER800P
#endif
};


typedef struct ati18800_t
{
        svga_t svga;
        ati_eeprom_t eeprom;

        rom_t bios_rom;
        
        uint8_t regs[256];
        int index;

        int is_ega;
        int ega_switches;
        int ega_switch_read;
} ati18800_t;

static video_timings_t timing_ati18800 = {VIDEO_ISA, 8, 16, 32,   8, 16, 32};


static void ati18800_out(uint16_t addr, uint8_t val, void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;
        svga_t *svga = &ati18800->svga;
        uint8_t old;
		int c;
        
        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
                addr ^= 0x60;

        switch (addr)
        {
                case 0x1ce:
                ati18800->index = val;
                break;
                case 0x1cf:
                ati18800->regs[ati18800->index] = val;
                switch (ati18800->index)
                {
                        case 0xb0:
                        svga_recalctimings(svga);
			break;
                        case 0xb2:
                        case 0xbe:
                        if (ati18800->regs[0xbe] & 8) /*Read/write bank mode*/
                        {
                                svga->read_bank  = ((ati18800->regs[0xb2] >> 5) & 7) * 0x10000;
                                svga->write_bank = ((ati18800->regs[0xb2] >> 1) & 7) * 0x10000;
                        }
                        else                    /*Single bank mode*/
                                svga->read_bank = svga->write_bank = ((ati18800->regs[0xb2] >> 1) & 7) * 0x10000;
                        break;
                        case 0xb3:
                        ati_eeprom_write(&ati18800->eeprom, val & 8, val & 2, val & 1);
                        break;
                }
                break;
                
                case 0x3c2:
                if (ati18800->is_ega)
                {
                        if ((val & 0x80) != (svga->miscout & 0x80))
                        {
                                if (val & 0x80)
                                {
                                        for (c = 0; c < 256; c++)
                                        {
                                                svga->pallook[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                                                svga->pallook[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
                                        }
                                        svga->vertical_linedbl = 0;
                                }
                                else
                                {
                                        for (c = 0; c < 256; c++)
                                        {
                                                if ((c & 0x17) == 6)
                                                        svga->pallook[c]  = makecol32(0xaa, 0x55, 0x00);
                                                else
                                                {
                                                        svga->pallook[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                                                        svga->pallook[c] += makecol32(((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 4) & 1) * 0x55);
                                                }
                                        }
                                        svga->vertical_linedbl = 1;
                                }
                                
                                svga->fullchange = changeframecount;
                        }
                        ati18800->ega_switch_read = val & 0xc;
                }
                break;

                case 0x3D4:
                svga->crtcreg = val & 0x3f;
                return;
                case 0x3D5:
                if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80) && !(ati18800->regs[0xb4] & 0x80))
                        return;
                if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80) && !(ati18800->regs[0xb4] & 0x80))
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

static uint8_t ati18800_in(uint16_t addr, void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;
        svga_t *svga = &ati18800->svga;
        uint8_t temp;

        if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;
             
        switch (addr)
        {
                case 0x1ce:
                temp = ati18800->index;
                break;
                case 0x1cf:
                switch (ati18800->index)
                {
                        case 0xb7:
                        temp = ati18800->regs[ati18800->index] & ~8;
                        if (ati_eeprom_read(&ati18800->eeprom))
                                temp |= 8;
                        break;
                        default:
                        temp = ati18800->regs[ati18800->index];
                        break;
                }
                break;

                case 0x3c2:
                if (ati18800->is_ega)
                {
                        switch (ati18800->ega_switch_read)
                        {
                                case 0xc: return (ati18800->ega_switches & 1) ? 0x10 : 0;
                                case 0x8: return (ati18800->ega_switches & 2) ? 0x10 : 0;
                                case 0x4: return (ati18800->ega_switches & 4) ? 0x10 : 0;
                                case 0x0: return (ati18800->ega_switches & 8) ? 0x10 : 0;
                        }
                }
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
        return temp;
}

static void ati18800_recalctimings(svga_t *svga)
{
        ati18800_t *ati18800 = (ati18800_t *)svga->p;

        if(svga->crtc[0x17] & 4)
        {
                svga->vtotal <<= 1;
                svga->dispend <<= 1;
                svga->vsyncstart <<= 1;
                svga->split <<= 1;
                svga->vblankstart <<= 1;
        }

        if (!svga->scrblank && ((ati18800->regs[0xb0] & 0x02) || (ati18800->regs[0xb0] & 0x04))) /*Extended 256 colour modes*/
        {
                svga->render = svga_render_8bpp_highres;
				svga->bpp = 8;
                svga->rowoffset <<= 1;
                svga->ma <<= 1;
        }
}

static void ega_wonder_800_recalctimings(svga_t *svga)
{
        ati18800_t *ati18800 = (ati18800_t *)svga->p;
        int clksel = ((svga->miscout & 0xc) >> 2) | ((ati18800->regs[0xbe] & 0x10) ? 4 : 0);

        switch (clksel)
        {
                case 0: svga->clock = (cpuclock * (double)(1ull << 32)) / 25175000.0; break;
                case 1: svga->clock = (cpuclock * (double)(1ull << 32)) / 28322000.0; break;
                case 4: svga->clock = (cpuclock * (double)(1ull << 32)) / 14318181.0; break;
                case 5: svga->clock = (cpuclock * (double)(1ull << 32)) / 16257000.0; break;
                case 7: default: svga->clock = (cpuclock * (double)(1ull << 32)) / 36000000.0; break;
        }
}


static void *ati18800_init(const device_t *info)
{
		int c;
        ati18800_t *ati18800 = malloc(sizeof(ati18800_t));
        memset(ati18800, 0, sizeof(ati18800_t));

	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati18800);

	ati18800->is_ega = (info->local == ATI18800_EGAWONDER800P);

	switch (info->local) {
#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
		case ATI18800_WONDER:
#endif
		default:
#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
		        rom_init(&ati18800->bios_rom, BIOS_ROM_PATH_WONDER, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
			break;
#endif
		case ATI18800_VGA88:
		        rom_init(&ati18800->bios_rom, BIOS_ROM_PATH_VGA88, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
			break;
		case ATI18800_EDGE16:
		        rom_init(&ati18800->bios_rom, BIOS_ROM_PATH_EDGE16, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
			break;
		case ATI18800_EGAWONDER800P:
		        rom_init(&ati18800->bios_rom, BIOS_ROM_PATH_EGAWONDER800P, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
			break;
	};
        
    if (info->local != ATI18800_EGAWONDER800P) {
        svga_init(&ati18800->svga, ati18800, 1 << 20, /*512kb*/
                   ati18800_recalctimings,
                   ati18800_in, ati18800_out,
                   NULL,
                   NULL);
    } else {
        svga_init(&ati18800->svga, ati18800, 1 << 18, /*256kb*/
                   ega_wonder_800_recalctimings,
                   ati18800_in, ati18800_out,
                   NULL,
                   NULL);	
	}

        io_sethandler(0x01ce, 0x0002, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);
		if (info->local != ATI18800_EGAWONDER800P)
		    io_sethandler(0x03c0, 0x0020, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);
		else {
		    io_sethandler(0x03c6, 0x0006, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);
			io_sethandler(0x03ca, 0x0016, ati18800_in, NULL, NULL, ati18800_out, NULL, NULL, ati18800);
			
			for (c = 0; c < 256; c++) {
                ati18800->svga.pallook[c]  = makecol32(((c >> 2) & 1) * 0xaa, ((c >> 1) & 1) * 0xaa, (c & 1) * 0xaa);
                ati18800->svga.pallook[c] += makecol32(((c >> 5) & 1) * 0x55, ((c >> 4) & 1) * 0x55, ((c >> 3) & 1) * 0x55);
			}
		}

        ati18800->svga.miscout = 1;

        if (info->local != ATI18800_EGAWONDER800P)
            ati_eeprom_load(&ati18800->eeprom, L"ati18800.nvr", 0);
		else {
            ati18800->ega_switches = 9;
            ati_eeprom_load(&ati18800->eeprom, L"egawonder800.nvr", 0);
		}

        return ati18800;
}

#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
static int ati18800_wonder_available(void)
{
        return rom_present(BIOS_ROM_PATH_WONDER);
}
#endif

static int ati18800_vga88_available(void)
{
        return rom_present(BIOS_ROM_PATH_VGA88);
}

static int ati18800_available(void)
{
        return rom_present(BIOS_ROM_PATH_EDGE16);
}

static int ega_wonder_800_available(void)
{
        return rom_present(BIOS_ROM_PATH_EGAWONDER800P);
}

static void ati18800_close(void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;

        svga_close(&ati18800->svga);
        
        free(ati18800);
}

static void ati18800_speed_changed(void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;
        
        svga_recalctimings(&ati18800->svga);
}

static void ati18800_force_redraw(void *p)
{
        ati18800_t *ati18800 = (ati18800_t *)p;

        ati18800->svga.fullchange = changeframecount;
}

#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
const device_t ati18800_wonder_device =
{
        "ATI-18800",
        DEVICE_ISA, ATI18800_WONDER,
        ati18800_init,
        ati18800_close,
	NULL,
        ati18800_wonder_available,
        ati18800_speed_changed,
        ati18800_force_redraw,
	NULL
};
#endif

const device_t ati18800_vga88_device =
{
        "ATI-18800-1",
        DEVICE_ISA, ATI18800_VGA88,
        ati18800_init,
        ati18800_close,
	NULL,
        ati18800_vga88_available,
        ati18800_speed_changed,
        ati18800_force_redraw,
	NULL
};

const device_t ati18800_device =
{
        "ATI-18800-5",
        DEVICE_ISA, ATI18800_EDGE16,
        ati18800_init,
        ati18800_close,
	NULL,
        ati18800_available,
        ati18800_speed_changed,
        ati18800_force_redraw,
	NULL
};

const device_t ati_ega_wonder_800_device =
{
        "ATI EGA Wonder 800+",
        DEVICE_ISA, ATI18800_EGAWONDER800P,
        ati18800_init,
        ati18800_close,
	NULL,
        ega_wonder_800_available,
        ati18800_speed_changed,
        ati18800_force_redraw,
	NULL
};
