/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a generic ALi M7101-compatible SMBus host
 *		controller.
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020,2021 RichardG.
 *		Copyright 2021 Miran Grca.
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
#include <86box/smbus.h>

#ifdef ENABLE_SMBUS_ALI7101_LOG
int smbus_ali7101_do_log = ENABLE_SMBUS_ALI7101_LOG;

static void
smbus_ali7101_log(const char *fmt, ...)
{
    va_list ap;

    if (smbus_ali7101_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define smbus_ali7101_log(fmt, ...)
#endif

static uint8_t
smbus_ali7101_read(uint16_t addr, void *priv)
{
    smbus_ali7101_t *dev = (smbus_ali7101_t *) priv;
    uint8_t          ret = 0x00;

    switch (addr - dev->io_base) {
        case 0x00:
            ret = dev->stat;
            break;

        case 0x03:
            ret = dev->addr;
            break;

        case 0x04:
            ret = dev->data0;
            break;

        case 0x05:
            ret = dev->data1;
            break;

        case 0x06:
            ret = dev->data[dev->index++];
            if (dev->index >= SMBUS_ALI7101_BLOCK_DATA_SIZE)
                dev->index = 0;
            break;

        case 0x07:
            ret = dev->cmd;
            break;
    }

    smbus_ali7101_log("SMBus ALI7101: read(%02X) = %02x\n", addr, ret);

    return ret;
}

static void
smbus_ali7101_write(uint16_t addr, uint8_t val, void *priv)
{
    smbus_ali7101_t *dev = (smbus_ali7101_t *) priv;
    uint8_t          smbus_addr, cmd, read, prev_stat;
    uint16_t         timer_bytes = 0;

    smbus_ali7101_log("SMBus ALI7101: write(%02X, %02X)\n", addr, val);

    prev_stat      = dev->next_stat;
    dev->next_stat = 0x04;
    switch (addr - dev->io_base) {
        case 0x00:
            dev->stat &= ~(val & 0xf2);
            /* Make sure IDLE is set if we're not busy or errored. */
            if (dev->stat == 0x00)
                dev->stat = 0x04;
            break;

        case 0x01:
            dev->ctl = val & 0xfc;
            if (val & 0x04) {    /* cancel an in-progress command if KILL is set */
                if (prev_stat) { /* cancel only if a command is in progress */
                    timer_disable(&dev->response_timer);
                    dev->stat = 0x80; /* raise FAILED */
                }
            } else if (val & 0x08) { /* T_OUT_CMD */
                if (prev_stat) {     /* cancel only if a command is in progress */
                    timer_disable(&dev->response_timer);
                    dev->stat = 0x20; /* raise DEVICE_ERR */
                }
            }

            if (val & 0x80)
                dev->index = 0;
            break;

        case 0x02:
            /* dispatch command if START is set */
            timer_bytes++; /* address */

            smbus_addr = (dev->addr >> 1);
            read       = dev->addr & 0x01;

            cmd = (dev->ctl >> 4) & 0x7;
            smbus_ali7101_log("SMBus ALI7101: addr=%02X read=%d protocol=%X cmd=%02X data0=%02X data1=%02X\n", smbus_addr, read, cmd, dev->cmd, dev->data0, dev->data1);

            /* Raise DEV_ERR if no device is at this address, or if the device returned NAK when starting the transfer. */
            if (!i2c_start(i2c_smbus, smbus_addr, read)) {
                dev->next_stat = 0x40;
                break;
            }

            dev->next_stat = 0x10; /* raise INTER (command completed) by default */

            /* Decode the command protocol. */
            switch (cmd) {
                case 0x0: /* quick R/W */
                    break;

                case 0x1:     /* byte R/W */
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

                case 0x4:          /* block R/W */
                    timer_bytes++; /* count the SMBus length byte now */

                    /* fall-through */

                default:                   /* unknown */
                    dev->next_stat = 0x20; /* raise DEV_ERR */
                    timer_bytes    = 0;
                    break;
            }

            /* Finish transfer. */
            i2c_stop(i2c_smbus, smbus_addr);
            break;

        case 0x03:
            dev->addr = val;
            break;

        case 0x04:
            dev->data0 = val;
            break;

        case 0x05:
            dev->data1 = val;
            break;

        case 0x06:
            dev->data[dev->index++] = val;
            if (dev->index >= SMBUS_ALI7101_BLOCK_DATA_SIZE)
                dev->index = 0;
            break;

        case 0x07:
            dev->cmd = val;
            break;
    }

    if (dev->next_stat != 0x04) { /* schedule dispatch of any pending status register update */
        dev->stat = 0x08;         /* raise HOST_BUSY while waiting */
        timer_disable(&dev->response_timer);
        /* delay = ((half clock for start + half clock for stop) + (bytes * (8 bits + ack))) * 60us period measured on real VIA 686B */
        timer_set_delay_u64(&dev->response_timer, (1 + (timer_bytes * 9)) * 60 * TIMER_USEC);
    }
}

static void
smbus_ali7101_response(void *priv)
{
    smbus_ali7101_t *dev = (smbus_ali7101_t *) priv;

    /* Dispatch the status register update. */
    dev->stat = dev->next_stat;
}

void
smbus_ali7101_remap(smbus_ali7101_t *dev, uint16_t new_io_base, uint8_t enable)
{
    if (dev->io_base)
        io_removehandler(dev->io_base, 0x10, smbus_ali7101_read, NULL, NULL, smbus_ali7101_write, NULL, NULL, dev);

    dev->io_base = new_io_base;
    smbus_ali7101_log("SMBus ALI7101: remap to %04Xh (%sabled)\n", dev->io_base, enable ? "en" : "dis");

    if (enable && dev->io_base)
        io_sethandler(dev->io_base, 0x10, smbus_ali7101_read, NULL, NULL, smbus_ali7101_write, NULL, NULL, dev);
}

static void
smbus_ali7101_reset(void *priv)
{
    smbus_ali7101_t *dev = (smbus_ali7101_t *) priv;

    timer_disable(&dev->response_timer);
    dev->stat = 0x04;
}

static void *
smbus_ali7101_init(const device_t *info)
{
    smbus_ali7101_t *dev = (smbus_ali7101_t *) malloc(sizeof(smbus_ali7101_t));
    memset(dev, 0, sizeof(smbus_ali7101_t));

    dev->local = info->local;
    dev->stat  = 0x04;
    /* We save the I2C bus handle on dev but use i2c_smbus for all operations because
       dev and therefore dev->i2c will be invalidated if a device triggers a hard reset. */
    i2c_smbus = dev->i2c = i2c_addbus("smbus_ali7101");

    timer_add(&dev->response_timer, smbus_ali7101_response, dev, 0);

    return dev;
}

static void
smbus_ali7101_close(void *priv)
{
    smbus_ali7101_t *dev = (smbus_ali7101_t *) priv;

    if (i2c_smbus == dev->i2c)
        i2c_smbus = NULL;
    i2c_removebus(dev->i2c);

    free(dev);
}

const device_t ali7101_smbus_device = {
    .name          = "ALi M7101-compatible SMBus Host Controller",
    .internal_name = "ali7101_smbus",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = smbus_ali7101_init,
    .close         = smbus_ali7101_close,
    .reset         = smbus_ali7101_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
