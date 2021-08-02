/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		AC'97 audio codec emulation.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2021 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/snd_ac97.h>

#define AC97_VENDOR_ID(f, s, t, dev)	((((f) & 0xff) << 24) | (((s) & 0xff) << 16) | (((t) & 0xff) << 8) | ((dev) & 0xff))


enum {
    AC97_CODEC_AD1881 = AC97_VENDOR_ID('A', 'D', 'S', 0x40),
    AC97_CODEC_ALC100 = AC97_VENDOR_ID('A', 'L', 'C', 0x20),
    AC97_CODEC_CS4297 = AC97_VENDOR_ID('C', 'R', 'Y', 0x03),
    AC97_CODEC_CS4297A = AC97_VENDOR_ID('C', 'R', 'Y', 0x13),
    AC97_CODEC_WM9701A = AC97_VENDOR_ID('W', 'M', 'L', 0x00)
};


#ifdef ENABLE_AC97_CODEC_LOG
int ac97_codec_do_log = ENABLE_AC97_CODEC_LOG;

static void
ac97_codec_log(const char *fmt, ...)
{
    va_list ap;

    if (ac97_codec_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ac97_codec_log(fmt, ...)
#endif

static const int32_t codec_attn[] = {
       25,    32,    41,    51,     65,     82,    103,    130,  164,  206,   260,   327,   412,   519,   653,   822,
     1036,  1304,  1641,  2067,   2602,   3276,   4125,   5192, 6537, 8230, 10362, 13044, 16422, 20674, 26027, 32767,
    41305, 52068, 65636, 82739, 104299,	131477, 165737, 208925
};

ac97_codec_t	**ac97_codec = NULL, **ac97_modem_codec = NULL;
int		ac97_codec_count = 0, ac97_modem_codec_count = 0,
		ac97_codec_id = 0, ac97_modem_codec_id = 0;


uint8_t
ac97_codec_read(ac97_codec_t *dev, uint8_t reg)
{
    uint8_t ret = dev->regs[reg & 0x7f];

    ac97_codec_log("AC97 Codec %d: read(%02X) = %02X\n", dev->codec_id, reg, ret);

    return ret;
}


void
ac97_codec_write(ac97_codec_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t i;

    ac97_codec_log("AC97 Codec %d: write(%02X, %02X)\n", dev->codec_id, reg, val);

    reg &= 0x7f;

    switch (reg) {
	case 0x00: case 0x01: /* Reset / ID code */
		ac97_codec_reset(dev);
		return;

	case 0x08: case 0x09: /* Master Tone Control (optional) */
	case 0x0d: /* Phone Volume MSB */
	case 0x0f: /* Mic Volume MSB */
	case 0x1e: case 0x1f: /* Record Gain Mic (optional) */
	case 0x22: case 0x23: /* 3D Control (optional) */
	case 0x24: case 0x25: /* Audio Interrupt and Paging Mechanism (optional) */
	case 0x26: /* Powerdown Ctrl/Stat LSB */
	case 0x28: case 0x29: /* Extended Audio ID */
	case 0x2b: /* Extended Audio Status/Control MSB */
	//case 0x36 ... 0x59: /* Linux tests for audio capability by writing to 38-39 */
	case 0x5a ... 0x5f: /* Vendor Reserved */
	//case 0x60 ... 0x6f:
	case 0x70 ... 0x7f: /* Vendor Reserved */
		/* Read-only registers. */
		return;

	case 0x02: /* Master Volume LSB */
	case 0x04: /* Aux Out Volume LSB */
	case 0x06: /* Mono Volume LSB */
		val &= 0x3f;
		/* fall-through */

	case 0x03: /* Master Volume MSB */
	case 0x05: /* Aux Out Volume MSB */
		val &= 0xbf;

		/* Limit level to a maximum of 011111. */
		if (val & 0x20) {
			val &= ~0x20;
			val |= 0x1f;
		}
		break;

	case 0x07: /* Mono Volume MSB */
	case 0x0b: /* PC Beep Volume MSB */
	case 0x20: /* General Purpose LSB */
		val &= 0x80;
		break;

	case 0x0a: /* PC Beep Volume LSB */
		val &= 0x1e;
		break;

	case 0x0c: /* Phone Volume LSB */
	case 0x10: /* Line In Volume LSB */
	case 0x12: /* CD Volume LSB */
	case 0x14: /* Video Volume LSB */
	case 0x16: /* Aux In Volume LSB */
	case 0x18: /* PCM Out Volume LSB */
		val &= 0x1f;
		break;

	case 0x0e: /* Mic Volume LSB */
		val &= 0x5f;
		break;

	case 0x11: /* Line In Volume MSB */
	case 0x13: /* CD Volume MSB */
	case 0x15: /* Video Volume MSB */
	case 0x17: /* Aux In Volume MSB */
	case 0x19: /* PCM Out Volume MSB */
		val &= 0x9f;
		break;

	case 0x1a: case 0x1b: /* Record Select */
		val &= 0x07;
		break;

	case 0x1c: /* Record Gain LSB */
		val &= 0x0f;
		break;

	case 0x1d: /* Record Gain MSB */
		val &= 0x8f;
		break;

	case 0x21: /* General Purpose MSB */
		val &= 0x83;
		break;

	case 0x2a: /* Extended Audio Status/Control LSB */
#ifdef AC97_CODEC_FULL_RATE_RANGE /* enable DRA (double rate) support */
		val &= 0x0b;
#else
		val &= 0x09;
#endif
		/* Reset DAC sample rates to 48 KHz (96 KHz with DRA) if VRA is being cleared. */
		if (!(val & 0x01)) {
			for (i = 0x2c; i <= 0x30; i += 2)
				*((uint16_t *) &dev->regs[i]) = 48000;
		}

		/* Reset ADC sample rates to 48 KHz if VRM is being cleared. */
		if (!(val & 0x08)) {
			for (i = 0x32; i <= 0x34; i += 2)
				*((uint16_t *) &dev->regs[i]) = 48000;
		}
		break;

	case 0x2c ... 0x35: /* DAC/ADC Rates */
		/* Writable only if VRA/VRM is set. */
		i = (reg >= 0x32) ? 0x08 : 0x01;
		if (!(dev->regs[0x2a] & i))
			return;

#ifndef AC97_CODEC_FULL_RATE_RANGE
		/* Limit to 48 KHz on MSB write. */
		if ((reg & 1) && (((val << 8) | dev->regs[reg & 0x7e]) > 48000)) {
			*((uint16_t *) &dev->regs[reg & 0x7e]) = 48000;
			return;
		}
#endif
		break;
    }

    dev->regs[reg] = val;
}


void
ac97_codec_reset(void *priv)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;
    uint8_t i;

    ac97_codec_log("AC97 Codec %d: reset()\n", dev->codec_id);

    memset(dev->regs, 0, sizeof(dev->regs));

    /* Set default level and gain values. */
    for (i = 0x02; i <= 0x18; i += 2) {
	if (i == 0x08)
		continue;
	if (i >= 0x0c)
		dev->regs[i] = 0x08;
	dev->regs[i | 1] = (i >= 0x10) ? 0x88 : 0x80;
    }

    /* Flag codec as ready. */
    dev->regs[0x26] = 0x0f;

    /* Set up variable sample rate support. */
#ifdef AC97_CODEC_FULL_RATE_RANGE /* enable DRA (double rate) support */
    dev->regs[0x28] = 0x0b;
#else
    dev->regs[0x28] = 0x09;
#endif
    ac97_codec_write(dev, 0x2a, 0x00); /* reset DAC/ADC sample rates */

    /* Set codec and vendor IDs. */
    dev->regs[0x29] = (dev->codec_id << 6) | 0x02;
    dev->regs[0x7c] = dev->vendor_id >> 16;
    dev->regs[0x7d] = dev->vendor_id >> 24;
    dev->regs[0x7e] = dev->vendor_id;
    dev->regs[0x7f] = dev->vendor_id >> 8;
}


void
ac97_codec_getattn(void *priv, uint8_t reg, int *l, int *r)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;
    uint8_t r_val = dev->regs[reg],
	    l_val = dev->regs[reg | 1];

    if (l_val & 0x80) { /* mute */
	*l = 0;
	*r = 0;
	return;
    }

    l_val &= 0x1f;
    r_val &= 0x1f;
    if (reg < 0x10) { /* 5-bit level (converted from 6-bit on register write) */
	*l = codec_attn[0x1f - l_val];
	*r = codec_attn[0x1f - r_val];
    } else { /* 5-bit gain */
	*l = codec_attn[0x27 - l_val];
	*r = codec_attn[0x27 - r_val];
    }
}


uint32_t
ac97_codec_getrate(void *priv, uint8_t reg)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;

    /* Get configured sample rate, which is always 48000 if VRA/VRM is not set. */
    uint32_t ret = *((uint16_t *) &dev->regs[reg]);

#ifdef AC97_CODEC_FULL_RATE_RANGE
    /* If this is a DAC, double sample rate if DRA is set. */
    if ((reg < 0x32) && (dev->regs[0x2a] & 0x02))
	ret <<= 1;
#endif

    ac97_codec_log("AC97 Codec %d: getrate(%02X) = %d\n", dev->codec_id, reg, ret);

    return ret;
}


static void *
ac97_codec_init(const device_t *info)
{
    ac97_codec_t *dev = malloc(sizeof(ac97_codec_t));
    memset(dev, 0, sizeof(ac97_codec_t));

    dev->vendor_id = info->local;
    ac97_codec_log("AC97 Codec %d: init(%c%c%c%02X)\n", ac97_codec_id, (dev->vendor_id >> 24) & 0xff, (dev->vendor_id >> 16) & 0xff, (dev->vendor_id >> 8) & 0xff, dev->vendor_id & 0xff);

    /* Associate this codec to the current controller. */
    if (!ac97_codec || (ac97_codec_count <= 0)) {
	fatal("AC97 Codec %d: No controller to associate codec\n", ac97_codec_id);
	return NULL;
    }
    *ac97_codec = dev;
    if (--ac97_codec_count == 0)
	ac97_codec = NULL;
    else
	ac97_codec += sizeof(ac97_codec_t *);
    dev->codec_id = ac97_codec_id++;

    /* Initialize codec registers. */
    ac97_codec_reset(dev);

    return dev;
}


static void
ac97_codec_close(void *priv)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;

    ac97_codec_log("AC97 Codec %d: close()\n", dev->codec_id);

    free(dev);
}


const device_t ad1881_device =
{
    "Analog Devices AD1881",
    DEVICE_AC97,
    AC97_CODEC_AD1881,
    ac97_codec_init, ac97_codec_close, ac97_codec_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};

const device_t alc100_device =
{
    "Avance Logic ALC100",
    DEVICE_AC97,
    AC97_CODEC_ALC100,
    ac97_codec_init, ac97_codec_close, ac97_codec_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};

const device_t cs4297_device =
{
    "Crystal CS4297",
    DEVICE_AC97,
    AC97_CODEC_CS4297,
    ac97_codec_init, ac97_codec_close, ac97_codec_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};

const device_t cs4297a_device =
{
    "Crystal CS4297A",
    DEVICE_AC97,
    AC97_CODEC_CS4297A,
    ac97_codec_init, ac97_codec_close, ac97_codec_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};

const device_t wm9701a_device =
{
    "Wolfson WM9701A",
    DEVICE_AC97,
    AC97_CODEC_WM9701A,
    ac97_codec_init, ac97_codec_close, ac97_codec_reset,
    { NULL },
    NULL,
    NULL,
    NULL
};
