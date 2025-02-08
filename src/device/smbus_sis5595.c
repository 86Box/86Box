/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic SiS 5595-compatible SMBus host
 *          controller.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2021 RichardG.
 *          Copyright 2021 Miran Grca.
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
#include <86box/pci.h>
#include <86box/smbus.h>
#include <86box/plat_fallthrough.h>

#ifdef ENABLE_SMBUS_SIS5595_LOG
int smbus_sis5595_do_log = ENABLE_SMBUS_SIS5595_LOG;

static void
smbus_sis5595_log(const char *fmt, ...)
{
    va_list ap;

    if (smbus_sis5595_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define smbus_sis5595_log(fmt, ...)
#endif

static void
smbus_sis5595_irq(smbus_sis5595_t *dev, int raise)
{
    if (dev->irq_enable) {
        if (raise)
            pci_set_mirq(6, 1, &dev->irq_state);
        else
            pci_clear_mirq(6, 1, &dev->irq_state);
    }
}

void
smbus_sis5595_irq_enable(void *priv, uint8_t enable)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;

    if (!enable && dev->irq_enable)
        pci_clear_mirq(6, 1, &dev->irq_state);

    dev->irq_enable = enable;
}

uint8_t
smbus_sis5595_read_index(void *priv)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;

    return dev->index;
}

uint8_t
smbus_sis5595_read_data(void *priv)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;
    uint8_t          ret = 0x00;

    switch (dev->index) {
        case 0x00:
            ret = dev->stat & 0xff;
            break;
        case 0x01:
            ret = dev->stat >> 8;
            break;

        case 0x02:
            ret = dev->ctl & 0xff;
            break;
        case 0x03:
            ret = dev->ctl >> 8;
            break;

        case 0x04:
            ret = dev->addr;
            break;

        case 0x05:
            ret = dev->cmd;
            break;

        case 0x06:
            ret = dev->block_ptr;
            break;

        case 0x07:
            ret = dev->count;
            break;

        case 0x08 ... 0x0f:
            ret = dev->data[(dev->index & 0x07) + (dev->block_ptr << 3)];
            if (dev->index == 0x0f) {
                dev->block_ptr = (dev->block_ptr + 1) & 3;
                smbus_sis5595_irq(dev, dev->block_ptr != 0x00);
            }
            break;

        case 0x10:
            ret = dev->saved_addr;
            break;

        case 0x11:
            ret = dev->data0;
            break;

        case 0x12:
            ret = dev->data1;
            break;

        case 0x13:
            ret = dev->alias;
            break;

        case 0xff:
            ret = dev->reg_ff & 0xc0;
            break;

        default:
            break;
    }

    smbus_sis5595_log("SMBus SIS5595: read(%02X) = %02x\n", dev->addr, ret);

    return ret;
}

void
smbus_sis5595_write_index(void *priv, uint8_t val)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;

    dev->index = val;
}

void
smbus_sis5595_write_data(void *priv, uint8_t val)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;
    uint8_t          smbus_addr;
    uint8_t          cmd;
    uint8_t          read;
    uint16_t         prev_stat;
    uint16_t         timer_bytes = 0;

    smbus_sis5595_log("SMBus SIS5595: write(%02X, %02X)\n", dev->addr, val);

    prev_stat      = dev->next_stat;
    dev->next_stat = 0x0000;
    switch (dev->index) {
        case 0x00:
            dev->stat &= ~(val & 0xf0);
            /* Make sure IDLE is set if we're not busy or errored. */
            if (dev->stat == 0x04)
                dev->stat = 0x00;
            break;
        case 0x01:
            dev->stat &= ~(val & 0x07);
            break;

        case 0x02:
            dev->ctl = (dev->ctl & 0xff00) | val;
            if (val & 0x20) {    /* cancel an in-progress command if KILL is set */
                if (prev_stat) { /* cancel only if a command is in progress */
                    timer_disable(&dev->response_timer);
                    dev->stat = 0x80; /* raise FAILED */
                }
            } else if (val & 0x10) {
                /* dispatch command if START is set */
                timer_bytes++; /* address */

                smbus_addr = (dev->addr >> 1);
                read       = dev->addr & 0x01;

                cmd = (dev->ctl >> 1) & 0x7;
                smbus_sis5595_log("SMBus SIS5595: addr=%02X read=%d protocol=%X cmd=%02X "
                                  "data0=%02X data1=%02X\n", smbus_addr, read, cmd, dev->cmd,
                                  dev->data0, dev->data1);

                /* Raise DEV_ERR if no device is at this address, or if the device returned
                   NAK when starting the transfer. */
                if (!i2c_start(i2c_smbus, smbus_addr, read)) {
                    dev->next_stat = 0x0020;
                    break;
                }

                dev->next_stat = 0x0040; /* raise INTER (command completed) by default */

                /* Decode the command protocol. */
                dev->block_ptr = 0x01;
                switch (cmd) {
                    case 0x0: /* quick R/W */
                        break;

                    case 0x1:     /* byte R/W */
                        if (read) /* byte read */
                            dev->data[0] = i2c_read(i2c_smbus, smbus_addr);
                        else /* byte write */
                            i2c_write(i2c_smbus, smbus_addr, dev->data[0]);
                        timer_bytes++;

                        break;

                    case 0x2: /* byte data R/W */
                        /* command write */
                        i2c_write(i2c_smbus, smbus_addr, dev->cmd);
                        timer_bytes++;

                        if (read) /* byte read */
                            dev->data[0] = i2c_read(i2c_smbus, smbus_addr);
                        else /* byte write */
                            i2c_write(i2c_smbus, smbus_addr, dev->data[0]);
                        timer_bytes++;

                        break;

                    case 0x3: /* word data R/W */
                        /* command write */
                        i2c_write(i2c_smbus, smbus_addr, dev->cmd);
                        timer_bytes++;

                        if (read) { /* word read */
                            dev->data[0] = i2c_read(i2c_smbus, smbus_addr);
                            dev->data[1] = i2c_read(i2c_smbus, smbus_addr);
                        } else { /* word write */
                            i2c_write(i2c_smbus, smbus_addr, dev->data[0]);
                            i2c_write(i2c_smbus, smbus_addr, dev->data[1]);
                        }
                        timer_bytes += 2;

                    break;

                    case 0x5:                    /* block R/W */
                        dev->block_ptr = 0x00;
                        timer_bytes++;           /* count the SMBus length byte now */
                        fallthrough;

                    default:                     /* unknown */
                        dev->next_stat = 0x0010; /* raise DEV_ERR */
                        timer_bytes    = 0;
                        break;
                }

                /* Finish transfer. */
                i2c_stop(i2c_smbus, smbus_addr);
            }
            break;
        case 0x03:
            dev->ctl = (dev->ctl & 0x00ff) | (val << 8);
            break;

        case 0x04:
            dev->addr = val;
            break;

        case 0x05:
            dev->cmd = val;
            break;

        case 0x08 ... 0x0f:
            dev->data[dev->index & 0x07] = val;
            break;

        case 0x10:
            dev->saved_addr = val;
            break;

        case 0x11:
            dev->data0 = val;
            break;

        case 0x12:
            dev->data1 = val;
            break;

        case 0x13:
            dev->alias = val & 0xfe;
            break;

        case 0xff:
            dev->reg_ff = val & 0x3f;
            break;

        default:
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
smbus_sis5595_response(void *priv)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;

    /* Dispatch the status register update. */
    dev->stat = dev->next_stat;
}

static void
smbus_sis5595_reset(void *priv)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;

    timer_disable(&dev->response_timer);
    dev->stat         = 0x0000;
    dev->block_ptr    = 0x01;
}

static void *
smbus_sis5595_init(const device_t *info)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) calloc(1, sizeof(smbus_sis5595_t));

    dev->local        = info->local;

    /* We save the I2C bus handle on dev but use i2c_smbus for all operations because
       dev and therefore dev->i2c will be invalidated if a device triggers a hard reset. */
    i2c_smbus = dev->i2c = i2c_addbus("smbus_sis5595");

    timer_add(&dev->response_timer, smbus_sis5595_response, dev, 0);

    smbus_sis5595_reset(dev);

    return dev;
}

static void
smbus_sis5595_close(void *priv)
{
    smbus_sis5595_t *dev = (smbus_sis5595_t *) priv;

    if (i2c_smbus == dev->i2c)
        i2c_smbus = NULL;
    i2c_removebus(dev->i2c);

    free(dev);
}

const device_t sis5595_smbus_device = {
    .name          = "SiS 5595-compatible SMBus Host Controller",
    .internal_name = "sis5595_smbus",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = smbus_sis5595_init,
    .close         = smbus_sis5595_close,
    .reset         = smbus_sis5595_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
