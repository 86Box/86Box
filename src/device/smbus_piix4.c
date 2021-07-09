/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a generic PIIX4-compatible SMBus host controller.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/i2c.h>
#include <86box/smbus_piix4.h>


#ifdef ENABLE_SMBUS_PIIX4_LOG
int smbus_piix4_do_log = ENABLE_SMBUS_PIIX4_LOG;


static void
smbus_piix4_log(const char *fmt, ...)
{
    va_list ap;

    if (smbus_piix4_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define smbus_piix4_log(fmt, ...)
#endif


static uint8_t
smbus_piix4_read(uint16_t addr, void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;
    uint8_t ret = 0x00;

    switch (addr - dev->io_base) {
	case 0x00:
		ret = dev->stat;
		break;

	case 0x02:
		dev->index = 0; /* reading from this resets the block data index */
		ret = dev->ctl;
		break;

	case 0x03:
		ret = dev->cmd;
		break;

	case 0x04:
		ret = dev->addr;
		break;

	case 0x05:
		ret = dev->data0;
		break;

	case 0x06:
		ret = dev->data1;
		break;

	case 0x07:
		ret = dev->data[dev->index++];
		if (dev->index >= SMBUS_PIIX4_BLOCK_DATA_SIZE)
			dev->index = 0;
		break;
    }

    smbus_piix4_log("SMBus PIIX4: read(%02X) = %02x\n", addr, ret);

    return ret;
}


static void
smbus_piix4_write(uint16_t addr, uint8_t val, void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;
    uint8_t smbus_addr, cmd, read, block_len, prev_stat;
    uint16_t timer_bytes = 0, i;

    smbus_piix4_log("SMBus PIIX4: write(%02X, %02X)\n", addr, val);

    prev_stat = dev->next_stat;
    dev->next_stat = 0x00;
    switch (addr - dev->io_base) {
	case 0x00:
		for (smbus_addr = 0x02; smbus_addr <= 0x10; smbus_addr <<= 1) { /* handle clearable bits */
			if (val & smbus_addr)
				dev->stat &= ~smbus_addr;
		}
		break;

	case 0x02:
		dev->ctl = val & ((dev->local == SMBUS_VIA) ? 0x3f : 0x1f);
		if (val & 0x02) { /* cancel an in-progress command if KILL is set */
			if (prev_stat) { /* cancel only if a command is in progress */
				timer_disable(&dev->response_timer);
				dev->stat = 0x10; /* raise FAILED */
			}
		}
		if (val & 0x40) { /* dispatch command if START is set */
			timer_bytes++; /* address */

			smbus_addr = (dev->addr >> 1);
			read = dev->addr & 0x01;

			cmd = (dev->ctl >> 2) & 0xf;
			smbus_piix4_log("SMBus PIIX4: addr=%02X read=%d protocol=%X cmd=%02X data0=%02X data1=%02X\n", smbus_addr, read, cmd, dev->cmd, dev->data0, dev->data1);

			/* Raise DEV_ERR if no device is at this address, or if the device returned NAK when starting the transfer. */
			if (!i2c_start(i2c_smbus, smbus_addr, read)) {
				dev->next_stat = 0x04;
				break;
			}

			dev->next_stat = 0x02; /* raise INTER (command completed) by default */

			/* Decode the command protocol.
			   VIA-specific modes (0x4 and [0x6:0xf]) are undocumented and required real hardware research. */
			switch (cmd) {
				case 0x0: /* quick R/W */
					break;

				case 0x1: /* byte R/W */
					if (read) /* byte read */
						dev->data0 = i2c_read(i2c_smbus, smbus_addr);
					else /* byte write */
						i2c_write(i2c_smbus, smbus_addr, dev->data0);
					timer_bytes++;

					break;

				case 0x2: /* byte data R/W */
					/* command write */
					i2c_write(i2c_smbus, smbus_addr, dev->cmd);
					timer_bytes++;

					if (read) /* byte read */
						dev->data0 = i2c_read(i2c_smbus, smbus_addr);
					else /* byte write */
						i2c_write(i2c_smbus, smbus_addr, dev->data0);
					timer_bytes++;

					break;

				case 0x3: /* word data R/W */
					/* command write */
					i2c_write(i2c_smbus, smbus_addr, dev->cmd);
					timer_bytes++;

					if (read) { /* word read */
						dev->data0 = i2c_read(i2c_smbus, smbus_addr);
						dev->data1 = i2c_read(i2c_smbus, smbus_addr);
					} else { /* word write */
						i2c_write(i2c_smbus, smbus_addr, dev->data0);
						i2c_write(i2c_smbus, smbus_addr, dev->data1);
					}
					timer_bytes += 2;

					break;

				case 0x4: /* process call */
					if (dev->local != SMBUS_VIA) /* VIA only */
						goto unknown_protocol;

					if (!read) { /* command write (only when writing) */
						i2c_write(i2c_smbus, smbus_addr, dev->cmd);
						timer_bytes++;
					}

					/* fall-through */

				case 0xc: /* I2C process call */
					if (!read) { /* word write (only when writing) */
						i2c_write(i2c_smbus, smbus_addr, dev->data0);
						i2c_write(i2c_smbus, smbus_addr, dev->data1);
						timer_bytes += 2;
					}

					/* word read */
					dev->data0 = i2c_read(i2c_smbus, smbus_addr);
					dev->data1 = i2c_read(i2c_smbus, smbus_addr);
					timer_bytes += 2;

					break;

				case 0x5: /* block R/W */
					timer_bytes++; /* count the SMBus length byte now */

					/* fall-through */

				case 0xd: /* I2C block R/W */
					i2c_write(i2c_smbus, smbus_addr, dev->cmd);
					timer_bytes++;

					if (read) {
						/* block read [data0] (I2C) or [first byte] (SMBus) bytes */
						if (cmd == 0x5)
							dev->data0 = i2c_read(i2c_smbus, smbus_addr);
						for (i = 0; i < dev->data0; i++)
							dev->data[i & SMBUS_PIIX4_BLOCK_DATA_MASK] = i2c_read(i2c_smbus, smbus_addr);
					} else {
						if (cmd == 0x5) /* send length [data0] as first byte on SMBus */
							i2c_write(i2c_smbus, smbus_addr, dev->data0);
						/* block write [data0] bytes */
						for (i = 0; i < dev->data0; i++) {
							if (!i2c_write(i2c_smbus, smbus_addr, dev->data[i & SMBUS_PIIX4_BLOCK_DATA_MASK]))
								break;
						}
					}
					timer_bytes += i;

					break;

				case 0x6: /* I2C with 10-bit address */
					if (dev->local != SMBUS_VIA) /* VIA only */
						goto unknown_protocol;

					/* command write */
					i2c_write(i2c_smbus, smbus_addr, dev->cmd);
					timer_bytes++;

					/* fall-through */

				case 0xe: /* I2C with 7-bit address */
					if (!read) { /* word write (only when writing) */
						i2c_write(i2c_smbus, smbus_addr, dev->data0);
						i2c_write(i2c_smbus, smbus_addr, dev->data1);
						timer_bytes += 2;
					}

					/* block read [first byte] bytes */
					block_len = dev->data[0];
					for (i = 0; i < block_len; i++)
						dev->data[i & SMBUS_PIIX4_BLOCK_DATA_MASK] = i2c_read(i2c_smbus, smbus_addr);
					timer_bytes += i;

					break;

				case 0xf: /* universal */
					/* block write [data0] bytes */
					for (i = 0; i < dev->data0; i++) {
						if (!i2c_write(i2c_smbus, smbus_addr, dev->data[i & SMBUS_PIIX4_BLOCK_DATA_MASK]))
							break; /* write NAK behavior is unknown */
					}
					timer_bytes += i;

					/* block read [data1] bytes */
					for (i = 0; i < dev->data1; i++)
						dev->data[i & SMBUS_PIIX4_BLOCK_DATA_MASK] = i2c_read(i2c_smbus, smbus_addr);
					timer_bytes += i;

					break;

				default: /* unknown */
unknown_protocol:
					dev->next_stat = 0x04; /* raise DEV_ERR */
					timer_bytes = 0;
					break;
			}

			/* Finish transfer. */
			i2c_stop(i2c_smbus, smbus_addr);
		}
		break;

	case 0x03:
		dev->cmd = val;
		break;

	case 0x04:
		dev->addr = val;
		break;

	case 0x05:
		dev->data0 = val;
		break;

	case 0x06:
		dev->data1 = val;
		break;

	case 0x07:
		dev->data[dev->index++] = val;
		if (dev->index >= SMBUS_PIIX4_BLOCK_DATA_SIZE)
			dev->index = 0;
		break;
    }

    if (dev->next_stat) { /* schedule dispatch of any pending status register update */
	dev->stat = 0x01; /* raise HOST_BUSY while waiting */
	timer_disable(&dev->response_timer);
	/* delay = ((half clock for start + half clock for stop) + (bytes * (8 bits + ack))) * bit period in usecs */
	timer_set_delay_u64(&dev->response_timer, (1 + (timer_bytes * 9)) * dev->bit_period * TIMER_USEC);
    }
}


static void
smbus_piix4_response(void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;

    /* Dispatch the status register update. */
    dev->stat = dev->next_stat;
}


void
smbus_piix4_remap(smbus_piix4_t *dev, uint16_t new_io_base, uint8_t enable)
{
    if (dev->io_base)
	io_removehandler(dev->io_base, 0x10, smbus_piix4_read, NULL, NULL, smbus_piix4_write, NULL, NULL, dev);

    dev->io_base = new_io_base;
    smbus_piix4_log("SMBus PIIX4: remap to %04Xh (%sabled)\n", dev->io_base, enable ? "en" : "dis");

    if (enable && dev->io_base)
	io_sethandler(dev->io_base, 0x10, smbus_piix4_read, NULL, NULL, smbus_piix4_write, NULL, NULL, dev);
}


void
smbus_piix4_setclock(smbus_piix4_t *dev, int clock)
{
    dev->clock = clock;

    /* Set the bit period in usecs. */
    dev->bit_period = 1000000.0 / dev->clock;
}


static void *
smbus_piix4_init(const device_t *info)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) malloc(sizeof(smbus_piix4_t));
    memset(dev, 0, sizeof(smbus_piix4_t));

    dev->local = info->local;
    /* We save the I2C bus handle on dev but use i2c_smbus for all operations because
       dev and therefore dev->i2c will be invalidated if a device triggers a hard reset. */
    i2c_smbus = dev->i2c = i2c_addbus((dev->local == SMBUS_VIA) ? "smbus_vt82c686b" : "smbus_piix4");

    timer_add(&dev->response_timer, smbus_piix4_response, dev, 0);

    smbus_piix4_setclock(dev, 16384); /* default to 16.384 KHz */

    return dev;
}


static void
smbus_piix4_close(void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;

    if (i2c_smbus == dev->i2c)
    	i2c_smbus = NULL;
    i2c_removebus(dev->i2c);

    free(dev);
}


const device_t piix4_smbus_device = {
    "PIIX4-compatible SMBus Host Controller",
    DEVICE_AT,
    SMBUS_PIIX4,
    smbus_piix4_init, smbus_piix4_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};

const device_t via_smbus_device = {
    "VIA VT82C686B SMBus Host Controller",
    DEVICE_AT,
    SMBUS_VIA,
    smbus_piix4_init, smbus_piix4_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
