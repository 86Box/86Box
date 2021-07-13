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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		greatpsycho,
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 greatpsycho.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>


#define VGAWONDERXL		1
#if defined(DEV_BRANCH) && defined(USE_XL24)
#define VGAWONDERXL24		2
#endif

#define BIOS_ATIKOR_PATH	"roms/video/ati28800/atikorvga.bin"
#define BIOS_ATIKOR_4620P_PATH_L	"roms/machines/spc4620p/31005h.u8"
#define BIOS_ATIKOR_4620P_PATH_H	"roms/machines/spc4620p/31005h.u10"
#define BIOS_ATIKOR_6033P_PATH	"roms/machines/spc6033p/phoenix.bin"
#define FONT_ATIKOR_PATH	"roms/video/ati28800/ati_ksc5601.rom"
#define FONT_ATIKOR_4620P_PATH	"roms/machines/spc4620p/svb6120a_font.rom"
#define FONT_ATIKOR_6033P_PATH	"roms/machines/spc6033p/svb6120a_font.rom"

#define BIOS_VGAXL_EVEN_PATH	"roms/video/ati28800/xleven.bin"
#define BIOS_VGAXL_ODD_PATH	"roms/video/ati28800/xlodd.bin"

#if defined(DEV_BRANCH) && defined(USE_XL24)
#define BIOS_XL24_EVEN_PATH	"roms/video/ati28800/112-14318-102.bin"
#define BIOS_XL24_ODD_PATH	"roms/video/ati28800/112-14319-102.bin"
#endif

#define BIOS_ROM_PATH		"roms/video/ati28800/bios.bin"


typedef struct ati28800_t
{
    svga_t		svga;
    ati_eeprom_t	eeprom;

    rom_t		bios_rom;

    uint8_t		regs[256];
    int			index;
    uint16_t		vtotal;

    uint32_t		memory;
    uint8_t		id;

    uint8_t		port_03dd_val;
    uint16_t		get_korean_font_kind;
    int			in_get_korean_font_kind_set;
    int			get_korean_font_enabled;
    int			get_korean_font_index;
    uint16_t		get_korean_font_base;
    int			ksc5601_mode_enabled;
} ati28800_t;


static video_timings_t timing_ati28800		= {VIDEO_ISA, 3,  3,  6,   5,  5, 10};
static video_timings_t timing_ati28800_spc	= {VIDEO_ISA, 2,  2,  4,   4,  4, 8};


#ifdef ENABLE_ATI28800_LOG
int ati28800_do_log = ENABLE_ATI28800_LOG;


static void
ati28800_log(const char *fmt, ...)
{
    va_list ap;

    if (ati28800_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ati28800_log(fmt, ...)
#endif


static void ati28800_recalctimings(svga_t *svga);


static void
ati28800_out(uint16_t addr, uint8_t val, void *p)
{
    ati28800_t *ati28800 = (ati28800_t *)p;
    svga_t *svga = &ati28800->svga;
    uint8_t old;

    ati28800_log("ati28800_out : %04X %02X\n", addr, val);

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
	addr ^= 0x60;

    switch (addr) {
	case 0x1ce:
		ati28800->index = val;
		break;
	case 0x1cf:
		old = ati28800->regs[ati28800->index];
		ati28800->regs[ati28800->index] = val;
		ati28800_log("ATI 28800 write reg=0x%02X, val=0x%02X\n", ati28800->index, val);
		switch (ati28800->index) {
			case 0xa3:
				ati28800->regs[0xa3] = val & 0x1f;
				svga_recalctimings(svga);
				break;
			case 0xa6:
				ati28800->regs[0xa6] = val & 0xc9;
				break;
			case 0xab:
				ati28800->regs[0xab] = val & 0xdf;
				break;
			case 0xb0:
				ati28800->regs[0xb0] = val & 0x7d;
				svga_recalctimings(svga);
				break;
			case 0xb1:
				ati28800->regs[0xb0] = val & 0x7f;
				break;
			case 0xb2:
				if (ati28800->regs[0xbe] & 0x08) {	/* Read/write bank mode */
					svga->read_bank = (((val & 0x01) << 3) | ((val & 0xe0) >> 5)) * 0x10000;
					svga->write_bank = ((val & 0x1e) >> 1) * 0x10000;
				} else {				/* Single bank mode */
					svga->read_bank = ((val & 0x1e) >> 1) * 0x10000;
					svga->write_bank = ((val & 0x1e) >> 1) * 0x10000;
				}
				break;
			case 0xb3:
				ati28800->regs[0xb3] = val & 0xef;
				ati_eeprom_write(&ati28800->eeprom, val & 8, val & 2, val & 1);
				break;
			case 0xb6:
				if ((old ^ val) & 0x10) 
					svga_recalctimings(svga);
				break;
			case 0xb8:
				if ((old ^ val) & 0x40) 
					svga_recalctimings(svga);
				break;
			case 0xb9:
				if ((old ^ val) & 2)
					svga_recalctimings(svga);
				break;
		}
		break;

	case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
		sc1502x_ramdac_out(addr, val, svga->ramdac, svga);
		return;					

	case 0x3D4:
		svga->crtcreg = val & 0x3f;
		return;
	case 0x3D5:
		if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
			return;
		if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
			val = (svga->crtc[7] & ~0x10) | (val & 0x10);
		if ((ati28800->regs[0xb4] & 0x10) && ((svga->crtcreg == 0x0a) || (svga->crtcreg == 0x0b)))
			return;
		if ((ati28800->regs[0xb4] & 0x20) && ((svga->crtc[0x08] & 0x7f) && (svga->crtc[0x14] & 0x1f)))
			return;
		if ((ati28800->regs[0xb4] & 0x40) && ((svga->crtcreg <= 0x06) && (svga->crtc[0x07] & 0x10) != 0x10))
			return;

		old = svga->crtc[svga->crtcreg];
		svga->crtc[svga->crtcreg] = val;
                if (old != val)
                {
                        if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                        {
				if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                                	svga->fullchange = 3;
					svga->ma_latch = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
				} else {
					svga->fullchange = changeframecount;
	                                svga_recalctimings(svga);
				}
                        }
                }
		break;
    }
    svga_out(addr, val, svga);
}


static void
ati28800k_out(uint16_t addr, uint8_t val, void *p)
{
    ati28800_t *ati28800 = (ati28800_t *)p;
    svga_t *svga = &ati28800->svga;
    uint16_t oldaddr = addr;

    if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) 
	addr ^= 0x60;
 
    switch (addr) {
	case 0x1CF:
		if (ati28800->index == 0xBF && ((ati28800->regs[0xBF] ^ val) & 0x20)) {
			ati28800->ksc5601_mode_enabled = val & 0x20;
			svga_recalctimings(svga);
		}
		ati28800_out(oldaddr, val, p);
		break;
	case 0x3DD:
		ati28800->port_03dd_val = val;
		if (val == 1)
			ati28800->get_korean_font_enabled = 0;
		if (ati28800->in_get_korean_font_kind_set) {
			ati28800->get_korean_font_kind = (val << 8) | (ati28800->get_korean_font_kind & 0xFF);
			ati28800->get_korean_font_enabled = 1;
			ati28800->get_korean_font_index = 0;
			ati28800->in_get_korean_font_kind_set = 0;
		}
		break;
	case 0x3DE:
		ati28800->in_get_korean_font_kind_set = 0;
		if (ati28800->get_korean_font_enabled) {
			if ((ati28800->get_korean_font_base & 0x7F) > 0x20 && (ati28800->get_korean_font_base & 0x7F) < 0x7F) {
				fontdatksc5601_user[(ati28800->get_korean_font_kind & 4) * 24 +
						    (ati28800->get_korean_font_base & 0x7F) - 0x20].chr[ati28800->get_korean_font_index] = val;
			}
			ati28800->get_korean_font_index++;
			ati28800->get_korean_font_index &= 0x1F;
		} else {
			switch (ati28800->port_03dd_val) {
				case 0x10:
					ati28800->get_korean_font_base = ((val & 0x7F) << 7) | (ati28800->get_korean_font_base & 0x7F);
					break;
				case 8:
					ati28800->get_korean_font_base = (ati28800->get_korean_font_base & 0x3F80) | (val & 0x7F);
					break;
				case 1:
					ati28800->get_korean_font_kind = (ati28800->get_korean_font_kind & 0xFF00) | val;
					if (val & 2)
						ati28800->in_get_korean_font_kind_set = 1;
					break;
			}
			break;
		}
		break;
	default:
		ati28800_out(oldaddr, val, p);
		break;
    }
}


static uint8_t
ati28800_in(uint16_t addr, void *p)
{
    ati28800_t *ati28800 = (ati28800_t *)p;
    svga_t *svga = &ati28800->svga;
    uint8_t temp;

    if (addr != 0x3da)
	ati28800_log("ati28800_in : %04X ", addr);
        
    if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1))
	addr ^= 0x60;

    switch (addr) {
	case 0x1ce:
		temp = ati28800->index;
		break;
	case 0x1cf:
		switch (ati28800->index) {
			case 0xa0:
				temp = 0x10;
				break;
			case 0xaa:
				temp = ati28800->id;
				break;
			case 0xb0:
				if (ati28800->memory == 1024)
					temp = 0x08;
				else if (ati28800->memory == 512)
					temp = 0x10;
				else
					temp = 0x00;
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

	case 0x3C6: case 0x3C7: case 0x3C8: case 0x3C9:
		return sc1502x_ramdac_in(addr, svga->ramdac, svga);				

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
    if (addr != 0x3da)
	ati28800_log("%02X\n", temp);
    return temp;
}


static uint8_t
ati28800k_in(uint16_t addr, void *p)
{
    ati28800_t *ati28800 = (ati28800_t *)p;
    svga_t *svga = &ati28800->svga;
    uint16_t oldaddr = addr;
    uint8_t temp = 0xFF;

    if (addr != 0x3da)
	ati28800_log("ati28800k_in : %04X ", addr);

    if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1))
	addr ^= 0x60;

    switch (addr) {
	case 0x3DE:
		if (ati28800->get_korean_font_enabled) {
			switch (ati28800->get_korean_font_kind >> 8) {
				case 4: /* ROM font */
					temp = fontdatksc5601[ati28800->get_korean_font_base].chr[ati28800->get_korean_font_index++];
					break;
				case 2: /* User defined font */
					if ((ati28800->get_korean_font_base & 0x7F) > 0x20 && (ati28800->get_korean_font_base & 0x7F) < 0x7F) {
						temp = fontdatksc5601_user[(ati28800->get_korean_font_kind & 4) * 24 +
									   (ati28800->get_korean_font_base & 0x7F) - 0x20].chr[ati28800->get_korean_font_index];
					} else
						temp = 0xFF;
					ati28800->get_korean_font_index++;
					break;
				default:
					break;
			}
			ati28800->get_korean_font_index &= 0x1F;
		}
		break;
	default:
		temp = ati28800_in(oldaddr, p);
		break;
    }
    if (addr != 0x3da)
	ati28800_log("%02X\n", temp);
    return temp;
}


static void
ati28800_recalctimings(svga_t *svga)
{
    ati28800_t *ati28800 = (ati28800_t *)svga->p;

    switch (((ati28800->regs[0xbe] & 0x10) >> 1) | ((ati28800->regs[0xb9] & 2) << 1) |
	    ((svga->miscout & 0x0C) >> 2)) {
	case 0x00: svga->clock = (cpuclock * (double)(1ull << 32)) / 42954000.0; break;
	case 0x01: svga->clock = (cpuclock * (double)(1ull << 32)) / 48771000.0; break;
	case 0x02: ati28800_log ("clock 2\n"); break;
	case 0x03: svga->clock = (cpuclock * (double)(1ull << 32)) / 36000000.0; break;
	case 0x04: svga->clock = (cpuclock * (double)(1ull << 32)) / 50350000.0; break;
	case 0x05: svga->clock = (cpuclock * (double)(1ull << 32)) / 56640000.0; break;
	case 0x06: ati28800_log ("clock 2\n"); break;
	case 0x07: svga->clock = (cpuclock * (double)(1ull << 32)) / 44900000.0; break;
	case 0x08: svga->clock = (cpuclock * (double)(1ull << 32)) / 30240000.0; break;
	case 0x09: svga->clock = (cpuclock * (double)(1ull << 32)) / 32000000.0; break;
	case 0x0A: svga->clock = (cpuclock * (double)(1ull << 32)) / 37500000.0; break;
	case 0x0B: svga->clock = (cpuclock * (double)(1ull << 32)) / 39000000.0; break;
	case 0x0C: svga->clock = (cpuclock * (double)(1ull << 32)) / 50350000.0; break;
	case 0x0D: svga->clock = (cpuclock * (double)(1ull << 32)) / 56644000.0; break;
	case 0x0E: svga->clock = (cpuclock * (double)(1ull << 32)) / 75000000.0; break;
	case 0x0F: svga->clock = (cpuclock * (double)(1ull << 32)) / 65000000.0; break;
	default: break;
    }

    if (ati28800->regs[0xb8] & 0x40) 
	svga->clock *= 2;

    if (ati28800->regs[0xa3] & 0x10)
	svga->ma |= 0x10000;

    if (ati28800->regs[0xb0] & 0x40)
	svga->ma |= 0x20000;

    if (ati28800->regs[0xb6] & 0x10) {
	svga->hdisp <<= 1;
	svga->htotal <<= 1;
	svga->rowoffset <<= 1;
    }

    if (!svga->scrblank && (ati28800->regs[0xb0] & 0x20)) {	/* Extended 256 colour modes */
	switch (svga->bpp) {
		case 8:
			svga->render = svga_render_8bpp_highres;
			svga->rowoffset <<= 1;
			svga->ma <<= 1;
			break;
		case 15:
			svga->render = svga_render_15bpp_highres;
			svga->hdisp >>= 1;
			svga->rowoffset <<= 1;
			svga->ma <<= 1;
			break;
	}
    }
}


static void
ati28800k_recalctimings(svga_t *svga)
{
    ati28800_t *ati28800 = (ati28800_t *) svga->p;

    ati28800_recalctimings(svga);

    if (svga->render == svga_render_text_80 && ati28800->ksc5601_mode_enabled)
	svga->render = svga_render_text_80_ksc5601;
}


void *
ati28800k_init(const device_t *info)
{
    ati28800_t *ati28800 = (ati28800_t *) malloc(sizeof(ati28800_t));
    memset(ati28800, 0, sizeof(ati28800_t));

    if (info->local == 0) {
	ati28800->memory = device_get_config_int("memory");
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati28800);
    } else {
	ati28800->memory = 512;
	video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati28800_spc);
    }

    ati28800->port_03dd_val = 0;
    ati28800->get_korean_font_base = 0;
    ati28800->get_korean_font_index = 0;
    ati28800->get_korean_font_enabled = 0;
    ati28800->get_korean_font_kind = 0;
    ati28800->in_get_korean_font_kind_set = 0;
    ati28800->ksc5601_mode_enabled = 0;

    switch(info->local) {
	case 0:
	default:
		rom_init(&ati28800->bios_rom, BIOS_ATIKOR_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		loadfont(FONT_ATIKOR_PATH, 6);
		break;
	case 1:
		rom_init_interleaved(&ati28800->bios_rom, BIOS_ATIKOR_4620P_PATH_L, BIOS_ATIKOR_4620P_PATH_H, 0xc0000,
				     0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		loadfont(FONT_ATIKOR_4620P_PATH, 6);
		break;
	case 2:
		rom_init(&ati28800->bios_rom, BIOS_ATIKOR_6033P_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
		loadfont(FONT_ATIKOR_6033P_PATH, 6);
		break;
    }

    svga_init(info, &ati28800->svga, ati28800, ati28800->memory << 10, /*Memory size, default 512KB*/
	     ati28800k_recalctimings,
	     ati28800k_in, ati28800k_out,
	     NULL,
	     NULL);

    ati28800->svga.ramdac = device_add(&sc1502x_ramdac_device);

    io_sethandler(0x01ce, 0x0002, ati28800k_in, NULL, NULL, ati28800k_out, NULL, NULL, ati28800);
    io_sethandler(0x03c0, 0x0020, ati28800k_in, NULL, NULL, ati28800k_out, NULL, NULL, ati28800);

    ati28800->svga.miscout = 1;
    ati28800->svga.ksc5601_sbyte_mask = 0;
    ati28800->svga.ksc5601_udc_area_msb[0] = 0xC9;
    ati28800->svga.ksc5601_udc_area_msb[1] = 0xFE;
    ati28800->svga.ksc5601_swap_mode = 0;
    ati28800->svga.ksc5601_english_font_type = 0;

    ati_eeprom_load(&ati28800->eeprom, "atikorvga.nvr", 0);

    return ati28800;
}


static void *
ati28800_init(const device_t *info)
{
    ati28800_t *ati28800;
    ati28800 = malloc(sizeof(ati28800_t));
    memset(ati28800, 0x00, sizeof(ati28800_t));

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_ati28800);

    ati28800->memory = device_get_config_int("memory");

    switch(info->local) {
	case VGAWONDERXL:
		ati28800->id = 6;
		rom_init_interleaved(&ati28800->bios_rom,
				     BIOS_VGAXL_EVEN_PATH,
				     BIOS_VGAXL_ODD_PATH,
				     0xc0000, 0x10000, 0xffff,
				     0, MEM_MAPPING_EXTERNAL);
		break;

#if defined(DEV_BRANCH) && defined(USE_XL24)
	case VGAWONDERXL24:
		ati28800->id = 6;
		rom_init_interleaved(&ati28800->bios_rom,
				     BIOS_XL24_EVEN_PATH,
				     BIOS_XL24_ODD_PATH,
				     0xc0000, 0x10000, 0xffff,
				     0, MEM_MAPPING_EXTERNAL);
		break;
#endif

	default:
		ati28800->id = 5;
		rom_init(&ati28800->bios_rom,
			 BIOS_ROM_PATH,
			 0xc0000, 0x8000, 0x7fff,
			 0, MEM_MAPPING_EXTERNAL);
		break;
    }

    svga_init(info, &ati28800->svga, ati28800, ati28800->memory << 10, /*default: 512kb*/
	      ati28800_recalctimings,
                   ati28800_in, ati28800_out,
                   NULL,
                   NULL);

    ati28800->svga.ramdac = device_add(&sc1502x_ramdac_device);
				   
    io_sethandler(0x01ce, 2,
		  ati28800_in, NULL, NULL,
		  ati28800_out, NULL, NULL, ati28800);
    io_sethandler(0x03c0, 32,
		  ati28800_in, NULL, NULL,
		  ati28800_out, NULL, NULL, ati28800);

    ati28800->svga.miscout = 1;

    switch (info->local) {
	case VGAWONDERXL:
		ati_eeprom_load(&ati28800->eeprom, "ati28800xl.nvr", 0);
		break;

#if defined(DEV_BRANCH) && defined(USE_XL24)
	case VGAWONDERXL24:
		ati_eeprom_load(&ati28800->eeprom, "ati28800xl24.nvr", 0);
		break;
#endif

	default:
		ati_eeprom_load(&ati28800->eeprom, "ati28800.nvr", 0);
		break;
    }	

    return(ati28800);
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
    ati28800_t *ati28800 = (ati28800_t *)priv;

    svga_close(&ati28800->svga);

    free(ati28800);
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
    ati28800_t *ati28800 = (ati28800_t *)priv;

    ati28800->svga.fullchange = changeframecount;
}


static const device_config_t ati28800_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 512, "", { 0 },
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

#if defined(DEV_BRANCH) && defined(USE_XL24)
static const device_config_t ati28800_wonderxl_config[] =
{
        {
                "memory", "Memory size", CONFIG_SELECTION, "", 512, "", { 0 },
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

const device_t ati28800_device =
{
        "ATI-28800",
        DEVICE_ISA,
	0,
        ati28800_init, ati28800_close, NULL,
        { ati28800_available },
        ati28800_speed_changed,
        ati28800_force_redraw,
	ati28800_config
};

const device_t ati28800k_device =
{
        "ATI Korean VGA",
        DEVICE_ISA,
	0,
        ati28800k_init, ati28800_close, NULL,
        { ati28800k_available },
        ati28800_speed_changed,
        ati28800_force_redraw,
	ati28800_config
};

const device_t ati28800k_spc4620p_device =
{
        "ATI Korean VGA On-Board SPC-4620P",
        DEVICE_ISA,
	1,
        ati28800k_init, ati28800_close, NULL,
        { NULL },
        ati28800_speed_changed,
        ati28800_force_redraw
};

const device_t ati28800k_spc6033p_device =
{
        "ATI Korean VGA On-Board SPC-6033P",
        DEVICE_ISA,
	2,
        ati28800k_init, ati28800_close, NULL,
        { NULL },
        ati28800_speed_changed,
        ati28800_force_redraw
};

const device_t compaq_ati28800_device =
{
        "Compaq ATI-28800",
        DEVICE_ISA,
	VGAWONDERXL,
        ati28800_init, ati28800_close, NULL,
        { compaq_ati28800_available },
        ati28800_speed_changed,
        ati28800_force_redraw,
	ati28800_config
};

#if defined(DEV_BRANCH) && defined(USE_XL24)
const device_t ati28800_wonderxl24_device =
{
        "ATI-28800 (VGA Wonder XL24)",
        DEVICE_ISA,
	VGAWONDERXL24,
        ati28800_init, ati28800_close, NULL,
        { ati28800_wonderxl24_available },
        ati28800_speed_changed,
        ati28800_force_redraw,
	ati28800_wonderxl_config
};
#endif
