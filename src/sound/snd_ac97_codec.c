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

#define AC97_CODEC_ID(f, s, t, dev)	((((f) & 0xff) << 24) | (((s) & 0xff) << 16) | (((t) & 0xff) << 8) | ((dev) & 0xff))


enum {
    AC97_CODEC_ALC100 = AC97_CODEC_ID('A', 'L', 'C', 0x20),
    AC97_CODEC_CS4297A = AC97_CODEC_ID('C', 'R', 'Y', 0x11)
};

#define ENABLE_AC97_CODEC_LOG 1
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

ac97_codec_t	**ac97_codec = NULL, **ac97_modem_codec = NULL;
int		ac97_codec_count = 0, ac97_modem_codec_count = 0;


uint8_t
ac97_codec_read(ac97_codec_t *dev, uint8_t reg)
{
    uint8_t ret = dev->regs[reg & 0x7f];

    ac97_codec_log("AC97 Codec: read(%02X) = %02X\n", reg, ret);

    return ret;
}


void
ac97_codec_write(ac97_codec_t *dev, uint8_t reg, uint8_t val)
{
    ac97_codec_log("AC97 Codec: write(%02X, %02X)\n", reg, val);

    reg &= 0x7f;

    switch (reg) {
	case 0x00: case 0x01: /* Reset / ID code */
		ac97_codec_reset(dev);
		/* fall-through */

	case 0x08: case 0x09: /* Master Tone Control (optional) */
	case 0x0d: /* Phone Volume MSB */
	case 0x0f: /* Mic Volume MSB */
	case 0x1e: case 0x1f: /* Record Gain Mic (optional) */
	case 0x22: case 0x23: /* 3D Control (optional) */
	case 0x24: case 0x25: /* Audio Interrupt and Paging Mechanism (optional) */
	case 0x26: /* Powerdown Ctrl/Stat LSB */
	case 0x28: case 0x29: /* Extended Audio ID */
	//case 0x2a ... 0x59: /* Linux tests for audio capability by writing to 38-39 */
	case 0x5a ... 0x5f: /* Vendor Reserved */
	//case 0x60 ... 0x6f:
	case 0x70 ... 0x7f: /* Vendor Reserved */
		/* Read-only registers. */
		return;

	case 0x03: /* Master Volume MSB */
	case 0x05: /* Aux Out Volume MSB */
		val &= 0xbf;
		break;

	case 0x07: /* Mono Volume MSB */
	case 0x20: /* General Purpose LSB */
		val &= 0x80;
		break;

	case 0x02: /* Master Volume LSB */
	case 0x04: /* Aux Out Volume LSB */
	case 0x06: /* Mono Volume LSB */
	case 0x0b: /* PC Beep Volume MSB */
		val &= 0x3f;
		break;

	case 0x0a: /* PC Beep Volume LSB */
		val &= 0xfe;
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
    }

    dev->regs[reg] = val;
}


void
ac97_codec_reset(void *priv)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;

    ac97_codec_log("AC97 Codec: reset()\n");

    memset(dev->regs, 0, sizeof(dev->regs));

    /* Mute outputs by default. */
    dev->regs[0x02] = dev->regs[0x04] = dev->regs[0x06] = 0x80;

    /* Flag codec as ready. */
    dev->regs[0x26] = 0x0f;

    /* Set Vendor ID. */
    dev->regs[0x7c] = dev->id >> 16;
    dev->regs[0x7d] = dev->id >> 24;
    dev->regs[0x7e] = dev->id;
    dev->regs[0x7f] = dev->id >> 8;
}


static void *
ac97_codec_init(const device_t *info)
{
    ac97_codec_t *dev = malloc(sizeof(ac97_codec_t));
    memset(dev, 0, sizeof(ac97_codec_t));

    dev->id = info->local;
    ac97_codec_log("AC97 Codec: init(%c%c%c%02X)\n", (dev->id >> 24) & 0xff, (dev->id >> 16) & 0xff, (dev->id >> 8) & 0xff, dev->id & 0xff);

    /* Associate this codec to the current controller. */
    if (!ac97_codec || (ac97_codec_count <= 0)) {
    	fatal("AC97 Codec: No controller to associate codec");
    	return NULL;
    }
    *ac97_codec = dev;
    if (--ac97_codec_count == 0)
	ac97_codec = NULL;
    else
	ac97_codec += sizeof(ac97_codec_t *);

    return dev;
}


static void
ac97_codec_close(void *priv)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;

    ac97_codec_log("AC97 Codec: close()\n");

    free(dev);
}


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
