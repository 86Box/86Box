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
 *
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
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>


#define ROM_PATH_XT	"roms/hdd/xtide/ide_xt.bin"
#define ROM_PATH_AT	"roms/hdd/xtide/ide_at.bin"
#define ROM_PATH_PS2	"roms/hdd/xtide/SIDE1V12.BIN"
#define ROM_PATH_PS2AT	"roms/hdd/xtide/ide_at_1_1_5.bin"
#define ROM_PATH_AT_386	"roms/hdd/xtide/ide_386.bin"


typedef struct {
    void	*ide_board;
    uint8_t	data_high;
    rom_t	bios_rom;
} xtide_t;


static void
xtide_write(uint16_t port, uint8_t val, void *priv)
{
    xtide_t *xtide = (xtide_t *)priv;

    switch (port & 0xf) {
	case 0x0:
		ide_writew(0x0, val | (xtide->data_high << 8), xtide->ide_board);
		return;

	case 0x1:
	case 0x2:
	case 0x3:
	case 0x4:
	case 0x5:
	case 0x6:
	case 0x7:
		ide_writeb((port  & 0xf), val, xtide->ide_board);
		return;

	case 0x8:
		xtide->data_high = val;
		return;

	case 0xe:
		ide_write_devctl(0x0, val, xtide->ide_board);
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
		tempw = ide_readw(0x0, xtide->ide_board);
		xtide->data_high = tempw >> 8;
		break;

	case 0x1:
	case 0x2:
	case 0x3:
	case 0x4:
	case 0x5:
	case 0x6:
	case 0x7:
		tempw = ide_readb((port  & 0xf), xtide->ide_board);
		break;

	case 0x8:
		tempw = xtide->data_high;
		break;

	case 0xe:
		tempw = ide_read_alt_status(0x0, xtide->ide_board);
		break;

	default:
		break;
    }

    return(tempw & 0xff);
}


static void *
xtide_init(const device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_XT,
	     0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    xtide->ide_board = ide_xtide_init();

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
xtide_at_init(const device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    if (info->local == 1) {
	rom_init(&xtide->bios_rom, ROM_PATH_AT_386,
		 0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);
    } else {
	rom_init(&xtide->bios_rom, ROM_PATH_AT,
		 0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);
    }

    device_add(&ide_isa_2ch_device);

    return(xtide);
}


static int
xtide_at_available(void)
{
    return(rom_present(ROM_PATH_AT));
}


static int
xtide_at_386_available(void)
{
    return(rom_present(ROM_PATH_AT_386));
}


static void *
xtide_acculogic_init(const device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_PS2,
	     0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    xtide->ide_board = ide_xtide_init();

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


static void
xtide_close(void *priv)
{
    xtide_t *xtide = (xtide_t *)priv;

    free(xtide);

    ide_xtide_close();
}


static void *
xtide_at_ps2_init(const device_t *info)
{
    xtide_t *xtide = malloc(sizeof(xtide_t));

    memset(xtide, 0x00, sizeof(xtide_t));

    rom_init(&xtide->bios_rom, ROM_PATH_PS2AT,
	     0xc8000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    device_add(&ide_isa_2ch_device);

    return(xtide);
}


static int
xtide_at_ps2_available(void)
{
    return(rom_present(ROM_PATH_PS2AT));
}


static void
xtide_at_close(void *priv)
{
    xtide_t *xtide = (xtide_t *)priv;

    free(xtide);
}


const device_t xtide_device = {
    .name = "PC/XT XTIDE",
    .internal_name = "xtide",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = xtide_init,
    .close = xtide_close,
    .reset = NULL,
    { .available = xtide_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t xtide_at_device = {
    .name = "PC/AT XTIDE",
    .internal_name = "xtide_at",
    .flags = DEVICE_ISA | DEVICE_AT,
    .local = 0,
    .init = xtide_at_init,
    .close = xtide_at_close,
    .reset = NULL,
    { .available = xtide_at_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t xtide_at_386_device = {
    .name = "PC/AT XTIDE (386)",
    .internal_name = "xtide_at_386",
    .flags = DEVICE_ISA | DEVICE_AT,
    .local = 1,
    .init = xtide_at_init,
    .close = xtide_at_close,
    .reset = NULL,
    { .available = xtide_at_386_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t xtide_acculogic_device = {
    .name = "Acculogic XT IDE",
    .internal_name = "xtide_acculogic",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = xtide_acculogic_init,
    .close = xtide_close,
    .reset = NULL,
    { .available = xtide_acculogic_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t xtide_at_ps2_device = {
    .name = "PS/2 AT XTIDE (1.1.5)",
    .internal_name = "xtide_at_ps2",
    .flags = DEVICE_ISA | DEVICE_AT,
    .local = 0,
    .init = xtide_at_ps2_init,
    .close = xtide_at_close,
    .reset = NULL,
    { .available = xtide_at_ps2_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
