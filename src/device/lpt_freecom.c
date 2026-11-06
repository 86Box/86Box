/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Freecom parallel port to IDE adapters: the "Power" adapter
 *           (FRPW, the Xilinx one) and the "IQ" cable (FRIQ, ASIC-2), as in
 *           Freecom's Traveller and Power CD-ROM drives and LS-120 drives.
 *
 *           There is no public datasheet for either chip. The protocol is
 *           the one Linux's paride drivers frpw.c and friq.c (Grant R.
 *           Guenther, drivers/block/paride, later drivers/ata/pata_parport)
 *           speak:
 *
 *           - A command byte selects what the following transfers go to.
 *             The Power adapter takes it off the data lines when AUTOFD
 *             rises with SELECT high (the control lines go C, E, E, C, 4).
 *             The IQ cable takes the data byte that follows FF 73 C9 26 on
 *             the data lines (changes count: Linux writes each twice).
 *           - Bits 4-0 of the command are the register: 08h-0Fh the IDE
 *             command block, 10h-17h the control block (16h the device
 *             control register and alternate status). 10h reads a test
 *             pattern, byte k of it being k. Bits 7-6: 00h transfers in
 *             4-bit mode, C0h reads in 8-bit mode with the port reversed,
 *             80h is EPP. 20h ends the connection. The IQ cable also has
 *             3Dh (power the drive on), 9Eh (no sleep timer), 1Dh and 1Eh
 *             (power off).
 *           - With SELECT low, AUTOFD rising with STROBE high writes the
 *             data lines to the register; AUTOFD rising with STROBE low
 *             reads the next byte of it, which the status lines carry, bits
 *             7-4: the low nibble while AUTOFD is high, then the high one.
 *             In 8-bit mode every AUTOFD edge reads the next byte onto the
 *             data lines. The data register gives a 16-bit IDE cycle as
 *             two bytes, low first. Freecom's own drivers also write with
 *             C0h set: STROBE says the direction, not the command.
 *
 *           The drive behind it is a regular drive of 86Box's IDE
 *           emulation, on an IDE board of its own that has no I/O ports
 *           and no IRQ: a hard disk put on the parallel port (bus LPT,
 *           bridge Freecom), or an ATAPI CD-ROM drive (bus LPT, bridge
 *           Freecom). It is the master on its cable.
 *
 * Authors:  Mario Fetka
 *
 *           Copyright 2026 Mario Fetka.
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
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_FREECOM_LOG
int freecom_do_log = ENABLE_FREECOM_LOG;

static void
freecom_log(const char *fmt, ...)
{
    va_list ap;

    if (freecom_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define freecom_log(fmt, ...)
#endif

#define LPT_CTRL_STROBE 0x01
#define LPT_CTRL_AUTOFD 0x02
#define LPT_CTRL_INIT   0x04
#define LPT_CTRL_SELECT 0x08
#define LPT_CTRL_DIR    0x20

#define LPT_STAT_IDLE   0xd8

#define FREECOM_FRPW    0 /* the Power adapter (Xilinx) */
#define FREECOM_FRIQ    1 /* the IQ cable (ASIC-2) */

/* What the IQ cable takes as the announcement of a command byte. It counts
   changes on the data lines: Linux writes every byte twice, Freecom's
   POWER-CD.SYS once. */
static const uint8_t friq_prefix[4] = { 0xff, 0x73, 0xc9, 0x26 };

#define FC_CMD_MODE      0xc0
#define FC_MODE_4BIT     0x00
#define FC_MODE_EPP      0x80
#define FC_MODE_8BIT     0xc0
#define FC_CMD_REG       0x1f
#define FC_REG_TASKFILE  0x08 /* 08h-0Fh: the IDE command block */
#define FC_REG_CONTROL   0x10 /* 10h-17h: the control block */
#define FC_REG_TEST      0x10 /* reads the test pattern */
#define FC_CMD_END       0x20

#define FRIQ_CMD_POWER_ON  0x3d
#define FRIQ_CMD_NO_SLEEP  0x9e
#define FRIQ_CMD_POWER_OFF 0x1d
#define FRIQ_CMD_POWER_OF2 0x1e

typedef struct freecom_t {
    void    *lpt;
    uint8_t  port;
    uint8_t  type;       /* FREECOM_* */

    uint8_t  dat;        /* data lines, as the host last wrote them */
    uint8_t  ctrl;       /* control lines, as the host last wrote them */
    uint8_t  hist_n;

    uint8_t  active;     /* a command selected a register */
    uint8_t  cmd;
    uint8_t  power;      /* IQ: the drive's power */
    uint8_t  rd_val;     /* the byte a read hands out */

    uint16_t word;       /* the IDE data register, a byte at a time */
    uint16_t pos;        /* byte of it, or of the test pattern */

    void    *ide;
} freecom_t;

/* --------------------------------------------------------------------- */
/* The IDE bus                                                           */
/* --------------------------------------------------------------------- */

static uint8_t
freecom_get(freecom_t *dev)
{
    const uint8_t reg = dev->cmd & FC_CMD_REG;
    uint8_t       ret = 0xff;

    if (reg == FC_REG_TEST)
        ret = (uint8_t) dev->pos;
    else if (dev->ide == NULL)
        ret = 0xff;
    else if (reg == FC_REG_TASKFILE) {
        if (!(dev->pos & 1))
            dev->word = ide_readw(0, dev->ide);
        ret = (uint8_t) (dev->word >> ((dev->pos & 1) * 8));
    } else if ((reg > FC_REG_TASKFILE) && (reg < FC_REG_CONTROL))
        ret = ide_readb(reg - FC_REG_TASKFILE, dev->ide);
    else if (reg == (FC_REG_CONTROL + 6))
        ret = ide_read_alt_status(0, dev->ide);

    dev->pos++;

    return ret;
}

static void
freecom_put(freecom_t *dev, uint8_t val)
{
    const uint8_t reg = dev->cmd & FC_CMD_REG;

    freecom_log("Freecom: W %02X = %02X\n", reg, val);

    if (dev->ide == NULL)
        return;

    if (reg == FC_REG_TASKFILE) {
        if (!(dev->pos & 1))
            dev->word = val;
        else
            ide_writew(0, (uint16_t) (dev->word | (val << 8)), dev->ide);
        dev->pos++;
    } else if ((reg > FC_REG_TASKFILE) && (reg < FC_REG_CONTROL))
        ide_writeb(reg - FC_REG_TASKFILE, val, dev->ide);
    else if (reg == (FC_REG_CONTROL + 6))
        ide_write_devctl(0, val, dev->ide);
}

/* --------------------------------------------------------------------- */
/* The adapter                                                           */
/* --------------------------------------------------------------------- */

static void
freecom_command(freecom_t *dev, uint8_t cmd)
{
    freecom_log("Freecom: command %02X\n", cmd);

    if (dev->type == FREECOM_FRIQ) {
        switch (cmd) {
            case FRIQ_CMD_POWER_ON:
                dev->power  = 1;
                dev->active = 0;
                return;
            case FRIQ_CMD_POWER_OFF:
            case FRIQ_CMD_POWER_OF2:
                dev->power  = 0;
                dev->active = 0;
                return;
            case FRIQ_CMD_NO_SLEEP:
                dev->active = 0;
                return;
            default:
                break;
        }
    }

    if (cmd == FC_CMD_END) {
        dev->active = 0;
        return;
    }

    dev->cmd    = cmd;
    dev->pos    = 0;
    dev->active = 1;
}

/* An 8-bit read: the byte goes onto the data lines. */
static void
freecom_present(freecom_t *dev)
{
    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, dev->rd_val);
}

static void
freecom_write_data(uint8_t val, void *priv)
{
    freecom_t *dev = (freecom_t *) priv;

    /* FF starts the prefix even when the lines already were FF: after an
       8-bit read they are. */
    if ((dev->type == FREECOM_FRIQ) && ((val != dev->dat) || (val == friq_prefix[0]))) {
        if (dev->hist_n == sizeof(friq_prefix)) {
            dev->hist_n = 0;
            freecom_command(dev, val);
        } else if (val == friq_prefix[dev->hist_n])
            dev->hist_n++;
        else
            dev->hist_n = (val == friq_prefix[0]) ? 1 : 0;
    }
    dev->dat = val;

    /* The host reads its own latch back unless the port is reversed. */
    if ((dev->lpt != NULL) && !(dev->ctrl & LPT_CTRL_DIR))
        lpt_write_to_dat(dev->lpt, val);
}

static void
freecom_write_ctrl(uint8_t val, void *priv)
{
    freecom_t    *dev  = (freecom_t *) priv;
    const uint8_t old  = dev->ctrl;
    const uint8_t rise = (uint8_t) (val & ~old);
    const uint8_t fall = (uint8_t) (old & ~val);

    dev->ctrl = val;

    /* The Power adapter: AUTOFD rising with SELECT high is a command. */
    if (val & LPT_CTRL_SELECT) {
        if ((dev->type == FREECOM_FRPW) && (rise & LPT_CTRL_AUTOFD))
            freecom_command(dev, dev->dat);
        return;
    }

    if (!dev->active)
        return;

    /* A write, whatever the mode: Freecom's drivers write with C8h too. */
    if ((rise & LPT_CTRL_AUTOFD) && (val & LPT_CTRL_STROBE)) {
        freecom_put(dev, dev->dat);
        return;
    }

    /* 8-bit reads: every AUTOFD edge with the port reversed. */
    if ((dev->cmd & FC_CMD_MODE) == FC_MODE_8BIT) {
        if (((rise | fall) & LPT_CTRL_AUTOFD) && (val & LPT_CTRL_DIR)) {
            dev->rd_val = freecom_get(dev);
            freecom_present(dev);
        }
        return;
    }

    if ((rise & LPT_CTRL_AUTOFD) && !(val & LPT_CTRL_STROBE))
        dev->rd_val = freecom_get(dev);
}

static uint8_t
freecom_read_status(void *priv)
{
    const freecom_t *dev = (freecom_t *) priv;
    uint8_t          v   = dev->rd_val;

    if (!dev->active || ((dev->cmd & FC_CMD_MODE) != FC_MODE_4BIT)) {
        return LPT_STAT_IDLE;
    }

    /* AUTOFD high: the low nibble. */
    if (dev->ctrl & LPT_CTRL_AUTOFD)
        v <<= 4;

    /* nERROR follows STROBE: Freecom's drivers look for that after
       selecting the test pattern. */
    return (uint8_t) ((v & 0xf0) | ((dev->ctrl & LPT_CTRL_STROBE) ? 0x08 : 0x00));
}

/* EPP: the data cycles move bytes of the register the command selected. */
static void
freecom_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    freecom_t *dev = (freecom_t *) priv;

    if (!is_addr && dev->active)
        freecom_put(dev, val);
}

static void
freecom_epp_request_read(uint8_t is_addr, void *priv)
{
    freecom_t *dev = (freecom_t *) priv;
    uint8_t    val = 0xff;

    if (!is_addr && dev->active)
        val = freecom_get(dev);

    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
}

/* --------------------------------------------------------------------- */
/* 86Box device plumbing                                                 */
/* --------------------------------------------------------------------- */

static void *
freecom_init(const device_t *info)
{
    freecom_t *dev = calloc(1, sizeof(freecom_t));

    if (dev == NULL)
        return NULL;

    dev->port  = (uint8_t) (device_get_instance() - 1);
    dev->type  = (uint8_t) info->local;
    dev->power = (dev->type != FREECOM_FRIQ);

    dev->lpt = lpt_attach(freecom_write_data, freecom_write_ctrl, NULL,
                          freecom_read_status, NULL,
                          freecom_epp_write_data, freecom_epp_request_read, dev);
    if (dev->lpt == NULL) {
        free(dev);
        return NULL;
    }

    dev->ide = ide_lpt_init(dev->port);
    if (dev->ide == NULL)
        freecom_log("Freecom: no IDE board for LPT%i\n", dev->port + 1);

    return dev;
}

static void
freecom_close(void *priv)
{
    freecom_t *dev = (freecom_t *) priv;

    if (dev->ide != NULL)
        ide_lpt_close(dev->port);
    free(dev);
}

const device_t lpt_frpw_device = {
    .name          = "Freecom Power parallel port IDE adapter",
    .internal_name = "frpw",
    .flags         = DEVICE_LPT,
    .local         = FREECOM_FRPW,
    .init          = freecom_init,
    .close         = freecom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t lpt_friq_device = {
    .name          = "Freecom IQ parallel port IDE adapter",
    .internal_name = "friq",
    .flags         = DEVICE_LPT,
    .local         = FREECOM_FRIQ,
    .init          = freecom_init,
    .close         = freecom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
