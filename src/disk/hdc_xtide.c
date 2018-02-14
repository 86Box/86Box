/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		XT-IDE controller emulation.
 *
 *		The XT-IDE project is intended to allow 8-bit ("XT") systems
 *		to use regular IDE drives. IDE is a standard based on the
 *		16b PC/AT design, and so a special board (with its own BIOS)
 *		had to be created for this.
 *
 *		XT-IDE is *NOT* the same as XTA, or X-IDE, which is an older
 *		standard where the actual MFM/RLL controller for the PC/XT
 *		was placed on the hard drive (hard drives where its drive
 *		type would end in "X" or "XT", such as the 8425XT.) This was
 *		more or less the original IDE, but since those systems were
 *		already on their way out, the newer IDE standard based on the
 *		PC/AT controller and 16b design became the IDE we now know.
 *
 * Version:	@(#)hdc_xtide.c	1.0.11	2018/02/14
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#include "hdc.h"
#include "hdc_ide.h"


#define ROM_PATH_XT	L"roms/hdd/xtide/ide_xt.bin"
#define ROM_PATH_AT	L"roms/hdd/xtide/ide_at.bin"
#define ROM_PATH_PS2	L"roms/hdd/xtide/SIDE1V12.BIN"
#define ROM_PATH_PS2AT	L"roms/hdd/xtide/ide_at_1_1_5.bin"


typedef struct {
    uint8_t	data_high;
    rom_t	bios_rom;
} xtide_t;


static void
xtide_write(uint16_t port, uint8_t val, void *priv)
{
    xtide_t *xtide = (xtide_t *)priv;

    switch (port & 0xf) {
	case 0x0:
		writeidew(4, val | (xtide->data_high << 8));
		return;

	case 0x1:
	case 0x2:
	case 0x3:
	case 0x4:
	case 0x5:
	case 0x6:
	case 0x7:
		writeide(4, (port  & 0xf) | 0x1f0, val);
		return;

	case 0x8:
		xtide->data_high = val;
		return;

	case 0xe:
		writeide(4, 0x3f6, val);
		return;
    }
}


static uint8_t
xtide_read(uint16_t port, void *priv)
{
    xtide_t *xtide = (xtide_t *)priv;
    uint16_t tempw = 0xffff;

    switch (port & 0xf) {
	case 0x0:
		tempw = readidew(4);
		xtide->data_high = tempw >> 8;
		break;

	case 0x1:
	case 0x2:
	case 0x3:
	case 0x4:
	case 0x5:
	case 0x6:
	case 0x7:
		tempw = readide(4, (port  & 0xf) | 0x1f0);
		break;

	case 0x8:
		tempw = xtide->data_high;
		break;

	case 0xe:
		tempw = readide(4, 0x3f6);
		break;

	default:
		break;
    }

    return(tempw & 0xff);
}


static void *
xtide_init(device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_XT,
	     0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    ide_xtide_init();

    io_sethandler(0x0300, 16,
		  xtide_read, NULL, NULL,
		  xtide_write, NULL, NULL, xtide);

    return(xtide);
}


static int
xtide_available(void)
{
    return(rom_present(ROM_PATH_XT));
}


static void *
xtide_at_init(device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_AT,
	     0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    device_add(&ide_isa_2ch_device);

    return(xtide);
}


static int
xtide_at_available(void)
{
    return(rom_present(ROM_PATH_AT));
}


static void *
xtide_acculogic_init(device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_PS2,
	     0xc8000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

    ide_xtide_init();

    io_sethandler(0x0360, 16,
		  xtide_read, NULL, NULL,
		  xtide_write, NULL, NULL, xtide);

    return(xtide);
}


static int
xtide_acculogic_available(void)
{
    return(rom_present(ROM_PATH_PS2));
}


static void *
xtide_at_ps2_init(device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_PS2AT,
	     0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    device_add(&ide_isa_2ch_device);

    return(xtide);
}


static int
xtide_at_ps2_available(void)
{
    return(rom_present(ROM_PATH_PS2AT));
}


static void
xtide_close(void *priv)
{
    xtide_t *xtide = (xtide_t *)priv;

    free(xtide);
}


device_t xtide_device = {
    "XTIDE",
    DEVICE_ISA,
    0,
    xtide_init, xtide_close, NULL,
    xtide_available, NULL, NULL, NULL,
    NULL
};

device_t xtide_at_device = {
    "XTIDE (AT)",
    DEVICE_ISA | DEVICE_AT,
    0,
    xtide_at_init, xtide_close, NULL,
    xtide_at_available, NULL, NULL, NULL,
    NULL
};

device_t xtide_acculogic_device = {
    "XTIDE (Acculogic)",
    DEVICE_ISA,
    0,
    xtide_acculogic_init, xtide_close, NULL,
    xtide_acculogic_available, NULL, NULL, NULL,
    NULL
};

device_t xtide_at_ps2_device = {
    "XTIDE (AT) (1.1.5)",
    DEVICE_ISA | DEVICE_PS2,
    0,
    xtide_at_ps2_init, xtide_close, NULL,
    xtide_at_ps2_available, NULL, NULL, NULL,
    NULL
};
