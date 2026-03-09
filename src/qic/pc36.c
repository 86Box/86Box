/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Wangtek PC-36 tape controller.
 *
 *
 * Authors: 

 *          Copyright 2025 seal331.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/wangtek_qic.h>
#include <86box/wangtek_qic.h>

/* for debugging; to be removed when upstreaming */
#define ENABLE_PC36_LOG 1

/* temporary: TODO: make configurable */
#define PC36_IO_BASE 0x300
#define PC36_IRQ 5
#define PC36_DMA_CHANNEL 1

/* I'm going to forget this math a billion times if I don't declare it here */
#define PC36_CHK_BIT_SET(var, pos) ((var) & (1<<(pos)))
#define PC36_SET_BIT(var, pos) ((var) |= (1 << (pos)))
#define PC36_CLR_BIT(var, pos) ((var) &= ~((1) << (pos)))

typedef struct wangtek_qic_t {
    uint8_t status; /* Status port (R/O) - address I/O base + 0x00 */
    uint8_t control; /* Control port (W/O) - address I/O base + 0x00 */
    uint8_t command; /* Command port (W/O) - address I/O base + 0x01 */
    uint8_t data; /* Data port (R/W) - address I/O base + 0x01 */

    uint16_t devstatus; /* Device status */

    int qic11flag; /* FLAG: is our tape QIC-11? */
    int qic24flag; /* FLAG: is our tape QIC-24? */
} wangtek_qic_t;

#ifdef ENABLE_PC36_LOG
int wangtek_qic_do_log = ENABLE_PC36_LOG;

static void
wangtek_qic_log(const char *fmt, ...)
{
    va_list ap;

    if (wangtek_qic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define wangtek_qic_log(fmt, ...)
#endif

static void
wangtek_qic_sel_qic_11_format()
{
    fatal("Unimplemented QIC command SELECT QIC-11 FORMAT");
}

static void
wangtek_qic_sel_qic_24_format()
{
    fatal("Unimplemented QIC command SELECT QIC-24 FORMAT");
}

static void
wangtek_qic_reset(void *priv)
{
    wangtek_qic_t* wangtek_qic = (wangtek_qic_t*)priv;
    wangtek_qic->status = 0;
    wangtek_qic->control = 0;
    wangtek_qic->command = 0;
    wangtek_qic->data = 0;
    wangtek_qic->devstatus = 0;
    wangtek_qic->qic11flag = 0;
    wangtek_qic->qic24flag = 1;
    PC36_SET_BIT(wangtek_qic->status, 1);
}

#define PC36_LOG_CONTROL_PORT_WRITES 1

static void
wangtek_qic_write(uint16_t port, uint8_t val, void *priv)
{
    wangtek_qic_t* wangtek_qic = (wangtek_qic_t*)priv;
    if (port == PC36_IO_BASE + 0x00) {
        /* Control port */
        #ifdef PC36_LOG_CONTROL_PORT_WRITES
        wangtek_qic_log("PC-36: control port write started\n");
        if (PC36_CHK_BIT_SET(val, 0)) {
            wangtek_qic_log("PC-36: online bit set writing to control port\n");
        } else {
            wangtek_qic_log("PC-36: online bit unset writing to control port\n");
        }
        if (PC36_CHK_BIT_SET(val, 1)) {
            wangtek_qic_log("PC-36: reset bit set writing to control port\n");
        } else {
            wangtek_qic_log("PC-36: reset bit unset writing to control port\n");
        }
        if (PC36_CHK_BIT_SET(val, 2)) {
            wangtek_qic_log("PC-36: request bit set writing to control port\n");
        } else {
            wangtek_qic_log("PC-36: request bit unset writing to control port\n");
        }
        wangtek_qic_log("PC-36: control port write ended\n");
        #endif
        if (PC36_CHK_BIT_SET(val, 1)) {
            wangtek_qic_reset(priv);
        }
        if (PC36_CHK_BIT_SET(val, 3)) {
            fatal("PC-36: \"enable DMA 1/2 and interrupt\" issued, not implemented");
        }
        if (PC36_CHK_BIT_SET(val, 4)) {
            fatal("PC-36: \"enable DMA 3 and interrupt\" issued, not implemented");
        }
        wangtek_qic->control = val;
    } else if (port == PC36_IO_BASE + 0x01) {
        /* Command/Data port */
        switch (val) {
            case QIC_SEL_DRIVE_0:
                wangtek_qic_sel_drive_0();
                break;
            
            case QIC_SEL_DRIVE_1:
                wangtek_qic_sel_drive_1();
                break;

            case QIC_SEL_DRIVE_2:
                /* controller manual labels this "not used", 
                 * so probably should never be called but still
                 */
                wangtek_qic_sel_drive_2();
                break;

            case QIC_REW_TO_BOT:
                wangtek_qic_rew_to_bot();
                break;

            case QIC_ERASE_TAPE:
                wangtek_qic_erase_tape();
                break;

            case QIC_INIT_TAPE:
                wangtek_qic_init_tape();
                break;

            case QIC_WRITE_DATA:
                wangtek_qic_write_data();
                break;

            case QIC_WRITE_FILE_MARK:
                wangtek_qic_write_file_mark();
                break;

            case QIC_READ_DATA:
                wangtek_qic_read_data();
                break;

            case QIC_READ_FILE_MARK:
                wangtek_qic_read_file_mark();
                break;

            case QIC_READ_STATUS:
                wangtek_qic_read_status();
                break;

            case PC36_SEL_QIC_11_FORMAT:
                wangtek_qic_sel_qic_11_format();
                break;

            case PC36_SEL_QIC_24_FORMAT:
                wangtek_qic_sel_qic_24_format();
                break;

            default:
                /* TODO: implement illegal command status */
                pclog("PC-36: unknown command %x\n", val);
                fatal("PC-36: unknown command, see log");
                break;
        }

    } else {
        /* alright now what even happened */
        wangtek_qic_log("PC-36: fell down wangtek_qic_write switch case to unreachable code\n");
    }
}

static uint8_t
wangtek_qic_read(uint16_t port, void *priv)
{
    wangtek_qic_t* wangtek_qic = (wangtek_qic_t*)priv;
    if (port == PC36_IO_BASE + 0x00) {
        /* Status port */
        wangtek_qic_log("PC-36: status port read\n");
        return wangtek_qic->status;
    } else if (port == PC36_IO_BASE + 0x01) {
        /* Data port */
        fatal("PC-36: data port read");
    } else {
        /* alright now what even happened */
        wangtek_qic_log("PC-36: fell down wangtek_qic_read switch case to unreachable code, returning 0xC3\n");
        return 0xC3;
    }
}

static void *
wangtek_qic_init(UNUSED(const device_t *info))
{
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) calloc(1, sizeof(wangtek_qic_t));

    io_sethandler(PC36_IO_BASE, 2, wangtek_qic_read, NULL, NULL, wangtek_qic_write, NULL, NULL, wangtek_qic);
    wangtek_qic_reset(&wangtek_qic);
    wangtek_qic_log("PC-36 controller initialized\n");

    return wangtek_qic;
}


static void
wangtek_qic_close(void *priv)
{

    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;

    io_removehandler(PC36_IO_BASE, 2, wangtek_qic_read, NULL, NULL, wangtek_qic_write, NULL, NULL, wangtek_qic);

    if (priv) {
        free(priv);
    }

    wangtek_qic_log("PC-36 controller de-initialized\n");
}

const device_t wangtek_qic_device = {
    .name          = "Wangtek PC-36",
    .internal_name = "wangtek_qic",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = wangtek_qic_init,
    .close         = wangtek_qic_close,
    .reset         = wangtek_qic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
