/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		DDC monitor emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2020 RichardG.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include <86box/i2c.h>


typedef struct {
    uint8_t addr_register;
} ddc_t;


static uint8_t edid_data[128] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, /* Fixed header pattern */
    0x09, 0xf8, /* Manufacturer "BOX" - apparently unassigned by UEFI - and it has to be big endian */
    0x00, 0x00, /* Product code */
    0x12, 0x34, 0x56, 0x78, /* Serial number */
    47, 30, /* Manufacturer week and year */
    0x01, 0x03, /* EDID version (1.3) */

    0x08, /* Analog input, separate sync */
    34, 0, /* Landscape, 4:3 */
    0, /* Gamma */
    0x08, /* RGB colour */
    0x81, 0xf1, 0xa3, 0x57, 0x53, 0x9f, 0x27, 0x0a, 0x50, /* Chromaticity */

    0xff, 0xff, 0xff, /* Established timing bitmap */
    0x00, 0x00, /* Standard timing information */
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,

    /* Detailed mode descriptions */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, /* No extensions */
    0x00
};


uint8_t
ddc_read_byte_cmd(void *bus, uint8_t addr, uint8_t cmd, void *priv)
{
    return edid_data[cmd & 0x7f];
}


uint8_t
ddc_read_byte(void *bus, uint8_t addr, void *priv)
{
    ddc_t *dev = (ddc_t *) priv;
    return ddc_read_byte_cmd(bus, addr, dev->addr_register++, priv);
}


uint16_t
ddc_read_word_cmd(void *bus, uint8_t addr, uint8_t cmd, void *priv)
{
    return (ddc_read_byte_cmd(bus, addr, cmd + 1, priv) << 8) | ddc_read_byte_cmd(bus, addr, cmd, priv);
}


uint8_t
ddc_read_block_cmd(void *bus, uint8_t addr, uint8_t cmd, uint8_t *data, uint8_t len, void *priv)
{
    uint8_t read = 0;
    for (uint8_t i = cmd; (i < len) && (i < 0x80); i++)
	data[read++] = ddc_read_byte_cmd(bus, addr, i, priv);
    return read;
}


void
ddc_write_byte(void *bus, uint8_t addr, uint8_t val, void *priv)
{
    ddc_t *dev = (ddc_t *) priv;
    dev->addr_register = val;
}


void
ddc_init(void *i2c)
{
    ddc_t *dev = (ddc_t *) malloc(sizeof(ddc_t));
    memset(dev, 0, sizeof(ddc_t));

    uint8_t checksum = 0;
    for (int c = 0; c < 127; c++)
        checksum += edid_data[c];
    edid_data[127] = 256 - checksum;

    i2c_sethandler(i2c, 0x50, 1,
		   NULL, ddc_read_byte, ddc_read_byte_cmd, ddc_read_word_cmd, ddc_read_block_cmd,
		   NULL, ddc_write_byte, NULL, NULL, NULL,
		   dev);
}
