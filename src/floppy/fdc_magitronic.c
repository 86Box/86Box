/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Magitronic B215 XT-FDC Controller.
 *
 *      Authors: Tiseno100
 *
 *		Copyright 2021 Tiseno100
 *
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>

#define ROM_B215 L"roms/floppy/magitronic/Magitronic B215 - BIOS ROM.bin"
#define ROM_ADDR (uint32_t)(device_get_config_hex20("bios_addr") & 0x000fffff)

typedef struct
{
    fdc_t *fdc_controller;
    rom_t rom;
} b215_t;

static void
b215_close(void *priv)
{
    b215_t *dev = (b215_t *)priv;

    free(dev);
}

static void *
b215_init(const device_t *info)
{
    b215_t *dev = (b215_t *)malloc(sizeof(b215_t));
    memset(dev, 0, sizeof(b215_t));

    rom_init(&dev->rom, ROM_B215, ROM_ADDR, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);

    device_add(&fdc_at_device);

    return dev;
}

static int b215_available(void)
{
    return rom_present(ROM_B215);
}

static const device_config_t b215_config[] = {
    {
	"bios_addr", "BIOS Address:", CONFIG_HEX20, "", 0xca000, "", { 0 },
	{
		{
			"CA00H", 0xca000
		},
		{
			"CC00H", 0xcc000
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

const device_t fdc_b215_device = {
    "Magitronic B215",
    DEVICE_ISA,
    0,
    b215_init,
    b215_close,
    NULL,
    {b215_available},
    NULL,
    NULL,
    b215_config};
