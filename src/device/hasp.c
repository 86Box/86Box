/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		HASP parallel port copy protection dongle emulation.
 *
 *		Based on the MAME driver for Savage Quest. This incomplete
 *		emulation is enough to satisfy that game, but not Aladdin's
 *		DiagnostiX utility.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
 *		Peter Ferrie
 *
 *		Copyright 2021 RichardG.
 *		Copyright Peter Ferrie.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/lpt.h>
#include <86box/device.h>

#define HASP_BYTEARRAY(...) {__VA_ARGS__}
#define HASP_TYPE(type, password_arr, prodinfo_arr)	[type] = { \
								.password = (const uint8_t[]) password_arr, \
								.prodinfo = (const uint8_t[]) prodinfo_arr, \
								.password_size = sizeof((uint8_t[]) password_arr), \
								.prodinfo_size = sizeof((uint8_t[]) prodinfo_arr) \
							},


enum {
    HASP_STATE_NONE = 0,
    HASP_STATE_PASSWORD_BEGIN,
    HASP_STATE_PASSWORD_END,
    HASP_STATE_READ
};

enum {
    HASP_TYPE_SAVQUEST = 0
};


typedef struct {
    const uint8_t *password, *prodinfo;
    const uint8_t password_size, prodinfo_size;
} hasp_type_t;

typedef struct
{
    void	*lpt;
    const hasp_type_t *type;

    int		index, state, passindex, passmode, prodindex;
    uint8_t	tmppass[0x29], status;
} hasp_t;

static const hasp_type_t hasp_types[] = {
    HASP_TYPE(HASP_TYPE_SAVQUEST,
	      HASP_BYTEARRAY(0xc3, 0xd9, 0xd3, 0xfb, 0x9d, 0x89, 0xb9, 0xa1, 0xb3, 0xc1, 0xf1, 0xcd, 0xdf, 0x9d),
	      HASP_BYTEARRAY(0x51, 0x4c, 0x52, 0x4d, 0x53, 0x4e, 0x53, 0x4e, 0x53, 0x49, 0x53, 0x48, 0x53, 0x4b, 0x53, 0x4a,
			     0x53, 0x43, 0x53, 0x45, 0x52, 0x46, 0x53, 0x43, 0x53, 0x41, 0xac, 0x40, 0x53, 0xbc, 0x53, 0x42,
			     0x53, 0x57, 0x53, 0x5d, 0x52, 0x5e, 0x53, 0x5b, 0x53, 0x59, 0xac, 0x58, 0x53, 0xa4))
};


#ifdef ENABLE_HASP_LOG
int hasp_do_log = ENABLE_HASP_LOG;

static void
hasp_log(const char *fmt, ...)
{
    va_list ap;

    if (hasp_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define hasp_log(fmt, ...)
#endif


static void
hasp_write_data(uint8_t val, void *priv)
{
    hasp_t *dev = (hasp_t *) priv;

    hasp_log("HASP: write_data(%02X)\n", val);

    switch (dev->index) {
	case 0:
		if (val == 0xc6)
			dev->index++;
		else
			dev->index = 0;
		break;

	case 1:
		if (val == 0xc7)
			dev->index++;
		else
			dev->index = 0;
		break;

	case 2:
		if (val == 0xc6) {
			dev->index++;
		} else {
			dev->index = 0;
			dev->state = HASP_STATE_NONE;
		}
		break;

	case 3:
		dev->index = 0;
		if (val == 0x80) {
			dev->state = HASP_STATE_PASSWORD_BEGIN;
			dev->passindex = 0;
			return;
		}
		break;
    }

    dev->status = 0;

    if (dev->state == HASP_STATE_READ) {
	/* different passwords cause different values to be returned
	   but there are really only two passwords of interest
	   passmode 2 is used to verify that the dongle is responding correctly */
	if (dev->passmode == 2) {
		switch (val) {
			case 0x94: case 0x9e: case 0xa4:
			case 0xb2: case 0xbe: case 0xd0:
				return;

			case 0x8a: case 0x8e: case 0xca: case 0xd2:
			case 0xe2: case 0xf0: case 0xfc:
				/* someone with access to the actual dongle could dump the true values
				   I've never seen it so I just determined the relevant bits instead
				   from the disassembly of the software
				   some of the keys are verified explicitly, the others implicitly
				   I guessed the implicit ones with a bit of trial and error */
				dev->status = 0x20;
				return;
		}
	}

	switch (val) {
		/* in passmode 0, some values remain unknown: 8a, 8e (inconclusive), 94, 96, 9a, a4, b2, be, c4, d2, d4 (inconclusive), e2, ec, f8, fc
		   this is less of a concern since the contents seem to decrypt correctly */
		case 0x88:
		case 0x94: case 0x98: case 0x9c: case 0x9e:
		case 0xa0: case 0xa4: case 0xaa: case 0xae:
		case 0xb0: case 0xb2: case 0xbc: case 0xbe:
		case 0xc2: case 0xc6: case 0xc8: case 0xce:
		case 0xd0: case 0xd6: case 0xd8: case 0xdc:
		case 0xe0: case 0xe6: case 0xea: case 0xee:
		case 0xf2: case 0xf6:
			/* again, just the relevant bits instead of the true values */
			dev->status = 0x20;
			break;
	}
    } else if (dev->state == HASP_STATE_PASSWORD_END) {
	if (val & 1) {
		if ((dev->passmode == 1) && (val == 0x9d))
			dev->passmode = 2;
		dev->state = HASP_STATE_READ;
	} else if (dev->passmode == 1) {
		dev->tmppass[dev->passindex++] = val;

		if (dev->passindex == sizeof(dev->tmppass)) {
			if ((dev->tmppass[0] == 0x9c) && (dev->tmppass[1] == 0x9e)) {
				int i = 2;
				dev->prodindex = 0;

				do {
					dev->prodindex = (dev->prodindex << 1) + ((dev->tmppass[i] >> 6) & 1);
				} while ((i += 3) < sizeof(dev->tmppass));

				dev->prodindex = (dev->prodindex - 0xc08) << 4;

				hasp_log("HASP: Password prodindex = %d\n", dev->prodindex);

				if (dev->prodindex < (0x38 << 4))
					dev->passmode = 3;
			}

			dev->state = HASP_STATE_READ;
		}
	}
    } else if ((dev->state == HASP_STATE_PASSWORD_BEGIN) && (val & 1)) {
	dev->tmppass[dev->passindex++] = val;

	if (dev->passindex == dev->type->password_size) {
		dev->state = HASP_STATE_PASSWORD_END;
		dev->passindex = 0;
		dev->passmode = (int) !memcmp(dev->tmppass, dev->type->password, dev->type->password_size);
		hasp_log("HASP: Password comparison result = %d\n", dev->passmode);
	}
    }
}


static uint8_t
hasp_read_status(void *priv)
{
    hasp_t *dev = (hasp_t *) priv;

    if ((dev->state == HASP_STATE_READ) && (dev->passmode == 3)) {
	/* passmode 3 is used to retrieve the product(s) information
	   it comes in two parts: header and product
	   the header has this format:
	   offset  range      purpose
	   00      01         header type
	   01      01-05      count of used product slots, must be 2
	   02      01-05      count of unused product slots
	                      this is assumed to be 6-(count of used slots)
	                      but it is not enforced here
	                      however a total of 6 structures will be checked
	   03      01-02      unknown
	   04      01-46      country code
	   05-0f   00         reserved
	   the used product slots have this format:
	   (the unused product slots must be entirely zeroes)
	   00-01   0001-000a  product ID, one must be 6, the other 0a
	   02      0001-0003  unknown but must be 0001
	   04      01-05      HASP plug country ID
	   05      01-02      unknown but must be 01
	   06      05         unknown
	   07-0a   any        unknown, not used
	   0b      ff         unknown
	   0c      ff         unknown
	   0d-0f   00         reserved
	   the read is performed by accessing an array of 16-bit big-endian values
	   and returning one bit at a time into bit 5 of the result
	   the 16-bit value is then XORed with 0x534d and the register index */

	if (dev->prodindex <= (dev->type->prodinfo_size * 8))
		dev->status = ((dev->type->prodinfo[(dev->prodindex - 1) >> 3] >> ((8 - dev->prodindex) & 7)) & 1) << 5; /* return defined info */
	else
		dev->status = (((0x534d ^ ((dev->prodindex - 1) >> 4)) >> ((16 - dev->prodindex) & 15)) & 1) << 5; /* then just alternate between the two key values */

	hasp_log("HASP: Reading %02X from prodindex %d\n", dev->status, dev->prodindex);

	dev->prodindex++;
    }

    hasp_log("HASP: read_status() = %02X\n", dev->status);

    return dev->status;
}


static void *
hasp_init(void *lpt, int type)
{
    hasp_t *dev = malloc(sizeof(hasp_t));
    memset(dev, 0, sizeof(hasp_t));

    hasp_log("HASP: init(%d)\n", type);

    dev->lpt = lpt;
    dev->type = &hasp_types[type];

    dev->status = 0x80;

    return dev;
}


static void *
hasp_init_savquest(void *lpt)
{
    return hasp_init(lpt, HASP_TYPE_SAVQUEST);
}


static void
hasp_close(void *priv)
{
    hasp_t *dev = (hasp_t *) priv;

    hasp_log("HASP: close()\n");

    free(dev);
}


const lpt_device_t lpt_hasp_savquest_device = {
    .name = "Protection Dongle for Savage Quest",
    .init = hasp_init_savquest,
    .close = hasp_close,
    .write_data = hasp_write_data,
    .write_ctrl = NULL,
    .read_data = NULL,
    .read_status = hasp_read_status,
    .read_ctrl = NULL
};
