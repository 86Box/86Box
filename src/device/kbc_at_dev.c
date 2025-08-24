/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          AT / PS/2 attached device emulation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat_fallthrough.h>
#include <86box/keyboard.h>

#ifdef ENABLE_KBC_AT_DEV_LOG
int kbc_at_dev_do_log = ENABLE_KBC_AT_DEV_LOG;

static void
kbc_at_dev_log(const char *fmt, ...)
{
    va_list ap;

    if (kbc_at_dev_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define kbc_at_dev_log(fmt, ...)
#endif

void
kbc_at_dev_queue_reset(atkbc_dev_t *dev, uint8_t reset_main)
{
    if (reset_main) {
        dev->queue_start = dev->queue_end = 0;
        memset(dev->queue, 0x00, sizeof(dev->queue));
    }

    dev->cmd_queue_start = dev->cmd_queue_end = 0;
    memset(dev->cmd_queue, 0x00, sizeof(dev->cmd_queue));
}

uint8_t
kbc_at_dev_queue_pos(atkbc_dev_t *dev, uint8_t main)
{
    uint8_t ret;

    if (main)
        ret = ((dev->queue_end - dev->queue_start) & dev->fifo_mask);
    else
        ret = ((dev->cmd_queue_end - dev->cmd_queue_start) & 0xf);

    return ret;
}

void
kbc_at_dev_queue_add(atkbc_dev_t *dev, uint8_t val, uint8_t main)
{
    if (main) {
        kbc_at_dev_log("%s: dev->queue[%02X]     = %02X;\n", dev->name, dev->queue_end, val);
        dev->queue[dev->queue_end]         = val;
        dev->queue_end                     = (dev->queue_end + 1) & dev->fifo_mask;
    } else {
        kbc_at_dev_log("%s: dev->cmd_queue[%02X] = %02X;\n", dev->name, dev->cmd_queue_end, val);
        dev->cmd_queue[dev->cmd_queue_end] = val;
        dev->cmd_queue_end                 = (dev->cmd_queue_end + 1) & 0xf;
    }
}

static void
kbc_at_dev_poll(void *priv)
{
    atkbc_dev_t *dev = (atkbc_dev_t *) priv;

    switch (dev->state) {
        case DEV_STATE_MAIN_1:
            /* Process the command if needed and then return to main loop #2. */
            if (dev->port->wantcmd) {
                kbc_at_dev_log("%s: Processing keyboard command %02X...\n", dev->name, dev->port->dat);
                kbc_at_dev_queue_reset(dev, 0);
                dev->process_cmd(dev);
                dev->port->wantcmd    = 0;
            } else
                dev->state = DEV_STATE_MAIN_2;
            break;
        case DEV_STATE_MAIN_2:
            /* Output from scan queue if needed and then return to main loop #1. */
            if (!dev->ignore && *dev->scan && (dev->port->out_new == -1) &&
                (dev->queue_start != dev->queue_end)) {
                kbc_at_dev_log("%s: %02X (DATA) on channel 1\n", dev->name, dev->queue[dev->queue_start]);
                dev->port->out_new   = dev->queue[dev->queue_start];
                if (dev->port->out_new != 0xfe)
                    dev->last_scan_code = dev->port->out_new;
                dev->queue_start     = (dev->queue_start + 1) & dev->fifo_mask;
            }
            if (dev->ignore || !(*dev->scan) || dev->port->wantcmd)
                dev->state = DEV_STATE_MAIN_1;
            break;
        case DEV_STATE_MAIN_OUT:
            /* If host wants to send command while we're sending a byte to host, process the command. */
            if (dev->port->wantcmd) {
                kbc_at_dev_log("%s: Processing keyboard command %02X...\n", dev->name, dev->port->dat);
                kbc_at_dev_queue_reset(dev, 0);
                dev->process_cmd(dev);
                dev->port->wantcmd    = 0;
                break;
            }
            fallthrough;
        case DEV_STATE_MAIN_WANT_IN:
            /* Output command response and then return to main loop #2. */
            if ((dev->port->out_new == -1) && (dev->cmd_queue_start != dev->cmd_queue_end)) {
                kbc_at_dev_log("%s: %02X (CMD ) on channel 1\n", dev->name, dev->cmd_queue[dev->cmd_queue_start]);
                dev->port->out_new   = dev->cmd_queue[dev->cmd_queue_start];
                if (dev->port->out_new != 0xfe)
                    dev->last_scan_code = dev->port->out_new;
                dev->cmd_queue_start = (dev->cmd_queue_start + 1) & 0xf;
            }
            if (dev->cmd_queue_start == dev->cmd_queue_end)
                dev->state++;
            break;
        case DEV_STATE_MAIN_IN:
            /* Wait for host data. */
            if (dev->port->wantcmd) {
                kbc_at_dev_log("%s: Processing keyboard command %02X parameter %02X...\n", dev->name, dev->command, dev->port->dat);
                kbc_at_dev_queue_reset(dev, 0);
                dev->process_cmd(dev);
                dev->port->wantcmd    = 0;
            }
            break;
        case DEV_STATE_EXECUTE_BAT:
            dev->state = DEV_STATE_MAIN_OUT;
            dev->execute_bat(dev);
            break;
        case DEV_STATE_MAIN_WANT_EXECUTE_BAT:
            /* Output command response and then return to main loop #2. */
            if ((dev->port->out_new == -1) && (dev->cmd_queue_start != dev->cmd_queue_end)) {
                kbc_at_dev_log("%s: %02X (CMD ) on channel 1\n", dev->name, dev->cmd_queue[dev->cmd_queue_start]);
                dev->port->out_new   = dev->cmd_queue[dev->cmd_queue_start];
                if (dev->port->out_new != 0xfe)
                    dev->last_scan_code = dev->port->out_new;
                dev->cmd_queue_start = (dev->cmd_queue_start + 1) & 0xf;
            }
            if (dev->cmd_queue_start == dev->cmd_queue_end)
                dev->state = DEV_STATE_EXECUTE_BAT;
            break;
        default:
            break;
    }
}

void
kbc_at_dev_reset(atkbc_dev_t *dev, int do_fa)
{
    dev->port->out_new = -1;
    dev->port->wantcmd = 0;

    kbc_at_dev_queue_reset(dev, 1);

    dev->last_scan_code = 0x00;

    *dev->scan = 1;

    if (do_fa) {
        kbc_at_dev_queue_add(dev, 0xfa, 0);
        dev->state = DEV_STATE_MAIN_WANT_EXECUTE_BAT;
    } else
        dev->state = DEV_STATE_EXECUTE_BAT;
}

atkbc_dev_t *
kbc_at_dev_init(uint8_t inst)
{
    atkbc_dev_t *dev;

    dev = (atkbc_dev_t *) calloc(1, sizeof(atkbc_dev_t));

    dev->port = kbc_at_ports[inst];

    if (dev->port != NULL) {
        dev->port->priv = dev;
        dev->port->poll = kbc_at_dev_poll;
    }

    /* Return our private data to the I/O layer. */
    return dev;
}
