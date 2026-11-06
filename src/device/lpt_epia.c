/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Shuttle EPIA parallel port to IDE adapter, the chip before the
 *           EPAT, in Shuttle's parallel port hard disk cases and SyQuest's
 *           parallel port drives (Shuttle's EPIAHD.SYS).
 *
 *           There is no public datasheet for the chip. The protocol is
 *           the one Linux's paride driver epia.c (Grant R. Guenther,
 *           drivers/block/paride, later drivers/ata/pata_parport) speaks,
 *           with what Shuttle's EPIAHD.SYS 4.31 needs on top of it (marked
 *           EPIAHD below):
 *
 *           - Writing A0 50 C0 30 A0 00 to the data lines with the control
 *             lines idle, then a STROBE pulse, connects the adapter.
 *             EPIAHD writes every byte more than once: only changes count.
 *           - Once connected, a STROBE rising edge with AUTOFD and nINIT
 *             both low latches a command byte off the data lines; AUTOFD
 *             (or the port turned around) then runs it. Bits 5-3 say what
 *             it is, bits 7 and 2-0 (bit 0 inverted) are the register for
 *             the single register commands:
 *               38h  read a register, 4-bit (status bits 7-4, AUTOFD
 *                    high for the low nibble)
 *               30h  read a register, 5/3 (status bits 7-3 and control
 *                    bits 3-1)
 *               28h  read a register, 8-bit (the port reversed)
 *               18h  write a register, the value strobed in when STROBE
 *                    falls
 *               00h, 10h, 08h  read a block, 4-bit, 5/3 or 8-bit; every
 *                    other STROBE edge moves to the next byte
 *               20h  write a block, a byte taken on every STROBE edge
 *               80h  (EPIAHD) hand the data lines back as a 4-bit read,
 *                    the link test
 *           - A STROBE pulse with no AUTOFD after it is no command. Linux
 *             ends a connection with one. EPIAHD sends 88h, 00h to reset
 *             the drive, and A0h or 90h to put the drive's INTRQ on the
 *             port: the interrupt, and nERROR low. 00h turns that off.
 *           - EPP: an address write selects the register, 40h set for a
 *             write; address 0 is the data register, read or written a
 *             byte at a time.
 *           - Registers 0-7 are the IDE command block, 80h-87h the control
 *             block: 86h is the device control register and alternate
 *             status. 84h is the adapter's own: bit 3 puts a test pattern
 *             on block reads (bytes k+1, -2-k of word k), the other bits
 *             set block sizes Linux does not use. 82h is the chip version
 *             (EPIAHD reads it; it takes A2h and up the newer way).
 *
 *           The drive behind it is a regular hard disk of 86Box's IDE
 *           emulation, put on the parallel port in the hard disk settings
 *           (bus LPT, bridge Shuttle EPIA): the adapter is added for it,
 *           with an IDE board of its own that has no I/O ports and no IRQ,
 *           and the disk is the master on its cable.
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

#ifdef ENABLE_EPIA_LOG
int epia_do_log = ENABLE_EPIA_LOG;

static void
epia_log(const char *fmt, ...)
{
    va_list ap;

    if (epia_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define epia_log(fmt, ...)
#endif

#define LPT_CTRL_STROBE 0x01
#define LPT_CTRL_AUTOFD 0x02
#define LPT_CTRL_INIT   0x04
#define LPT_CTRL_SELECT 0x08
#define LPT_CTRL_IRQ    0x10
#define LPT_CTRL_DIR    0x20

#define LPT_STAT_MASK   0xf8
#define LPT_STAT_IDLE   0xd8
#define LPT_STAT_NERROR 0x08 /* low: the drive's INTRQ, once enabled */

/* The wake-up sequence on the data lines; STROBE then connects. */
static const uint8_t epia_wake[6] = { 0xa0, 0x50, 0xc0, 0x30, 0xa0, 0x00 };

/* Command bits 5-3. */
#define EPIA_OP_MASK      0x38
#define EPIA_OP_RD_4BIT   0x38
#define EPIA_OP_RD_53     0x30
#define EPIA_OP_RD_8BIT   0x28
#define EPIA_OP_WRITE     0x18
#define EPIA_OP_BRD_4BIT  0x00
#define EPIA_OP_BRD_53    0x10
#define EPIA_OP_BRD_8BIT  0x08
#define EPIA_OP_BWR       0x20

/* Command 80h hands the data lines back, as a 4-bit read: the link test
   of Shuttle's drivers. */
#define EPIA_CMD_ECHO     0x80

/* A STROBE pulse with no AUTOFD after it is not a command: Linux ends a
   connection with one, Shuttle's drivers send 00h after connecting, 88h,
   00h to reset the drive, and A0h (or 90h) to have the drive's INTRQ
   raise the port's interrupt, which 00h turns off again. */
#define EPIA_BARE_RESET   0x88
#define EPIA_BARE_IRQ     0x80

/* EPP address bit 6: a write. */
#define EPIA_EPP_WRITE    0x40

#define EPIA_REG_CONTROL  0x80 /* the IDE control block */
#define EPIA_REG_CONFIG   0x84 /* the adapter's own */
#define EPIA_CFG_TEST     0x08 /* block reads give the test pattern */
#define EPIA_REG_VERSION  0x82 /* the chip's version, read only */
#define EPIA_VERSION      0xa2

enum {
    EPIA_IDLE = 0,
    EPIA_PENDING,  /* a command byte latched, not yet run */
    EPIA_ECHO,     /* command 80h: the data lines read back */
    EPIA_REG_READ,
    EPIA_REG_WRITE,
    EPIA_BLK_READ,
    EPIA_BLK_WRITE
};

typedef struct epia_t {
    void    *lpt;
    uint8_t  port;

    /* Parallel port side. */
    uint8_t  dat;        /* data lines, as the host last wrote them */
    uint8_t  ctrl;       /* control lines, as the host last wrote them */
    uint8_t  hist[6];    /* the last data bytes, for the wake-up sequence */
    uint8_t  connected;
    uint8_t  state;      /* EPIA_* */
    uint8_t  cmd;        /* the command byte latched */
    uint8_t  reset;      /* the drive held in reset (bare 88h pulse) */
    uint8_t  irq_en;     /* INTRQ goes to the port (bare A0h/90h pulse) */
    uint8_t  irq_out;    /* what the port's interrupt line is driven to */
    pc_timer_t irq_timer; /* raises a pending INTRQ once the port takes it */
    uint8_t  op;         /* command bits 5-3 of the running command */
    uint8_t  reg;        /* register of a single register command */
    uint8_t  rd_val;     /* what a register read hands out */
    uint8_t  epp_reg;    /* EPP: register addressed */
    uint8_t  config;     /* register 84h */

    /* Blocks: the IDE data register, a byte at a time. */
    uint16_t blk_word;
    uint16_t blk_pos;    /* byte of the block being handed out or taken */

    /* The IDE board behind it. */
    void    *ide;
} epia_t;

/* --------------------------------------------------------------------- */
/* The IDE bus                                                           */
/* --------------------------------------------------------------------- */

static uint8_t
epia_ide_read(epia_t *dev, uint8_t reg)
{
    if (dev->ide == NULL)
        return 0xff;

    if (reg & EPIA_REG_CONTROL) {
        if ((reg & 7) == 6)
            return ide_read_alt_status(0, dev->ide);
        if (reg == EPIA_REG_CONFIG)
            return dev->config;
        if (reg == EPIA_REG_VERSION)
            return EPIA_VERSION;
        return 0xff;
    }

    if ((reg & 7) == 0)
        return (uint8_t) ide_readw(0, dev->ide);

    return ide_readb(reg & 7, dev->ide);
}

static void
epia_ide_write(epia_t *dev, uint8_t reg, uint8_t val)
{
    if (reg == EPIA_REG_CONFIG) {
        dev->config = val;
        return;
    }

    if (dev->ide == NULL)
        return;

    if (reg & EPIA_REG_CONTROL) {
        if ((reg & 7) == 6)
            ide_write_devctl(0, val, dev->ide);
        return;
    }

    if ((reg & 7) == 0)
        ide_writew(0, val, dev->ide);
    else
        ide_writeb(reg & 7, val, dev->ide);
}

/* Byte pos of a block read: the IDE data register a word at a time, or
   the test pattern. */
static uint8_t
epia_blk_byte(epia_t *dev)
{
    const uint16_t k = dev->blk_pos >> 1;

    if (dev->config & EPIA_CFG_TEST)
        return (dev->blk_pos & 1) ? (uint8_t) (0xfe - k) : (uint8_t) (k + 1);

    if (!(dev->blk_pos & 1))
        dev->blk_word = (dev->ide != NULL) ? ide_readw(0, dev->ide) : 0xffff;

    return (uint8_t) (dev->blk_word >> ((dev->blk_pos & 1) * 8));
}

/* A byte of a block write; a word goes out once both halves are in. */
static void
epia_blk_put(epia_t *dev, uint8_t val)
{
    if (!(dev->blk_pos & 1))
        dev->blk_word = val;
    else {
        dev->blk_word |= val << 8;
        if (dev->ide != NULL)
            ide_writew(0, dev->blk_word, dev->ide);
    }
    dev->blk_pos++;
}

/* --------------------------------------------------------------------- */
/* The adapter                                                           */
/* --------------------------------------------------------------------- */

/* The drive's INTRQ on the port's interrupt, once the driver asked. */
static void
epia_update_irq(epia_t *dev)
{
    const int out = dev->irq_en && (dev->ide != NULL) && ide_lpt_intrq(dev->port);

    if (out != dev->irq_out) {
        dev->irq_out = out;
        if (dev->lpt != NULL)
            lpt_irq(dev->lpt, out);
    }
}

/* The port's interrupt enable is taken after the control write reaches
   the adapter: an INTRQ already pending is raised a moment later. */
static void
epia_irq_timer(void *priv)
{
    epia_t *dev = (epia_t *) priv;

    if (dev->irq_out && (dev->lpt != NULL))
        lpt_irq(dev->lpt, 1);
}

static void
epia_ide_irq(UNUSED(int set), void *priv)
{
    epia_update_irq((epia_t *) priv);
}

/* What a read puts on the port: the data lines in 8-bit mode, else
   nothing (the status lines carry it, see epia_read_status). */
static void
epia_present(epia_t *dev)
{
    if ((dev->lpt != NULL) && (dev->op == EPIA_OP_RD_8BIT || dev->op == EPIA_OP_BRD_8BIT))
        lpt_write_to_dat(dev->lpt, dev->rd_val);
}

static void
epia_command(epia_t *dev, uint8_t cmd)
{
    dev->op = cmd & EPIA_OP_MASK;

    if (cmd == EPIA_CMD_ECHO) {
        dev->op    = EPIA_OP_RD_4BIT;
        dev->state = EPIA_ECHO;
        return;
    }

    switch (dev->op) {
        case EPIA_OP_RD_4BIT:
        case EPIA_OP_RD_53:
        case EPIA_OP_RD_8BIT:
            dev->reg    = (cmd & 0x87) ^ 0x01;
            dev->rd_val = epia_ide_read(dev, dev->reg);
            dev->state  = EPIA_REG_READ;
            epia_log("EPIA: R %02X -> %02X\n", dev->reg, dev->rd_val);
            epia_present(dev);
            break;

        case EPIA_OP_WRITE:
            dev->reg   = (cmd & 0x87) ^ 0x01;
            dev->state = EPIA_REG_WRITE;
            break;

        case EPIA_OP_BRD_4BIT:
        case EPIA_OP_BRD_53:
        case EPIA_OP_BRD_8BIT:
            dev->blk_pos = 0;
            dev->rd_val  = epia_blk_byte(dev);
            dev->state   = EPIA_BLK_READ;
            epia_present(dev);
            break;

        case EPIA_OP_BWR:
            dev->blk_pos = 0;
            dev->state   = EPIA_BLK_WRITE;
            break;

        default:
            epia_log("EPIA: command %02X?\n", cmd);
            dev->state = EPIA_IDLE;
            break;
    }
}

static void
epia_write_data(uint8_t val, void *priv)
{
    epia_t *dev = (epia_t *) priv;

    /* The wake-up sequence counts changes: Shuttle's drivers write every
       byte more than once. */
    if (val != dev->hist[sizeof(dev->hist) - 1]) {
        memmove(dev->hist, dev->hist + 1, sizeof(dev->hist) - 1);
        dev->hist[sizeof(dev->hist) - 1] = val;
    }
    dev->dat = val;

    /* The host reads its own latch back unless the port is reversed. */
    if ((dev->lpt != NULL) && !(dev->ctrl & LPT_CTRL_DIR))
        lpt_write_to_dat(dev->lpt, val);
}

static void
epia_write_ctrl(uint8_t val, void *priv)
{
    epia_t       *dev  = (epia_t *) priv;
    const uint8_t old  = dev->ctrl;
    const uint8_t rise = (uint8_t) (val & ~old);
    const uint8_t fall = (uint8_t) (old & ~val);

    dev->ctrl = val;

    /* The port's interrupt just turned on: a pending INTRQ shows. */
    if ((rise & LPT_CTRL_IRQ) && dev->irq_out)
        timer_on_auto(&dev->irq_timer, 2.0);

    if (rise & LPT_CTRL_STROBE) {
        if (!memcmp(dev->hist, epia_wake, sizeof(epia_wake))) {
            if (!dev->connected)
                epia_log("EPIA: connected\n");
            dev->connected = 1;
            dev->state     = EPIA_IDLE;
            memset(dev->hist, 0xff, sizeof(dev->hist));
            dev->hist[sizeof(dev->hist) - 1] = dev->dat;
            return;
        }
        if (dev->connected && !(val & (LPT_CTRL_AUTOFD | LPT_CTRL_INIT))) {
            dev->cmd   = dev->dat;
            dev->state = EPIA_PENDING;
            return;
        }
    }

    if (!dev->connected)
        return;

    switch (dev->state) {
        case EPIA_PENDING:
            /* AUTOFD, or the port turned around, runs the command. */
            if ((rise & (LPT_CTRL_AUTOFD | LPT_CTRL_DIR)) && (val & LPT_CTRL_STROBE))
                epia_command(dev, dev->cmd);
            else if (fall & LPT_CTRL_STROBE) {
                epia_log("EPIA: bare pulse %02X\n", dev->cmd);
                if ((dev->cmd & EPIA_BARE_IRQ) && (dev->cmd != EPIA_BARE_RESET)) {
                    dev->irq_en = 1;
                    epia_update_irq(dev);
                } else if (dev->cmd == 0x00) {
                    dev->irq_en = 0;
                    epia_update_irq(dev);
                }
                if (dev->cmd == EPIA_BARE_RESET) {
                    if (!dev->reset && (dev->ide != NULL))
                        ide_write_devctl(0, 0x0c, dev->ide);
                    dev->reset = 1;
                } else if (dev->reset) {
                    if (dev->ide != NULL)
                        ide_write_devctl(0, 0x08, dev->ide);
                    dev->reset = 0;
                }
                dev->state = EPIA_IDLE;
            }
            return;

        case EPIA_ECHO:
        case EPIA_REG_READ:
            /* STROBE falling ends the read. */
            if (fall & LPT_CTRL_STROBE)
                dev->state = EPIA_IDLE;
            break;

        case EPIA_REG_WRITE:
            if (fall & LPT_CTRL_STROBE) {
                epia_log("EPIA: W %02X = %02X\n", dev->reg, dev->dat);
                epia_ide_write(dev, dev->reg, dev->dat);
                dev->state = EPIA_IDLE;
            }
            break;

        case EPIA_BLK_READ:
            if ((rise | fall) & LPT_CTRL_STROBE) {
                dev->blk_pos++;
                dev->rd_val = epia_blk_byte(dev);
                epia_present(dev);
            }
            break;

        case EPIA_BLK_WRITE:
            if ((rise | fall) & LPT_CTRL_STROBE)
                epia_blk_put(dev, dev->dat);
            break;

        default:
            break;
    }

    /* Turning the port around: an 8-bit read shows its byte. */
    if (((rise | fall) & LPT_CTRL_DIR) && (dev->state == EPIA_REG_READ || dev->state == EPIA_BLK_READ))
        epia_present(dev);
}

static uint8_t
epia_read_status(void *priv)
{
    const epia_t *dev = (epia_t *) priv;
    uint8_t       v   = (dev->state == EPIA_ECHO) ? dev->dat : dev->rd_val;

    if (!dev->connected ||
        ((dev->state != EPIA_REG_READ) && (dev->state != EPIA_BLK_READ) && (dev->state != EPIA_ECHO)))
        return dev->irq_out ? (LPT_STAT_IDLE & ~LPT_STAT_NERROR) : LPT_STAT_IDLE;

    switch (dev->op) {
        case EPIA_OP_RD_4BIT:
        case EPIA_OP_BRD_4BIT:
            /* AUTOFD high: the low nibble. */
            if (!(dev->ctrl & LPT_CTRL_AUTOFD))
                v >>= 4;
            return (uint8_t) (((v & 0x0f) << 4) | 0x08);

        case EPIA_OP_RD_53:
        case EPIA_OP_BRD_53:
            return (uint8_t) ((v << 3) & LPT_STAT_MASK);

        default:
            return LPT_STAT_IDLE;
    }
}

/* 5/3 reads: bits 7-5 of the byte on control lines 3-1. */
static uint8_t
epia_read_ctrl(void *priv)
{
    const epia_t *dev = (epia_t *) priv;

    if (dev->connected && ((dev->state == EPIA_REG_READ) || (dev->state == EPIA_BLK_READ)) &&
        ((dev->op == EPIA_OP_RD_53) || (dev->op == EPIA_OP_BRD_53)))
        return (uint8_t) ((dev->ctrl & 0xf1) | ((dev->rd_val >> 4) & 0x0e));

    return dev->ctrl;
}

static void
epia_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    epia_t *dev = (epia_t *) priv;

    if (is_addr) {
        dev->epp_reg = val;
        dev->blk_pos = 0;
        return;
    }

    if (!(dev->epp_reg & EPIA_EPP_WRITE))
        return;

    if ((dev->epp_reg & ~EPIA_EPP_WRITE) == 0)
        epia_blk_put(dev, val);
    else
        epia_ide_write(dev, dev->epp_reg & ~EPIA_EPP_WRITE, val);
}

static void
epia_epp_request_read(uint8_t is_addr, void *priv)
{
    epia_t *dev = (epia_t *) priv;
    uint8_t val = 0xff;

    if (!is_addr && !(dev->epp_reg & EPIA_EPP_WRITE)) {
        if (dev->epp_reg == 0) {
            val = epia_blk_byte(dev);
            dev->blk_pos++;
        } else
            val = epia_ide_read(dev, dev->epp_reg);
    }

    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
}

/* --------------------------------------------------------------------- */
/* 86Box device plumbing                                                 */
/* --------------------------------------------------------------------- */

static void *
epia_init(UNUSED(const device_t *info))
{
    epia_t *dev = calloc(1, sizeof(epia_t));

    if (dev == NULL)
        return NULL;

    dev->port = (uint8_t) (device_get_instance() - 1);
    memset(dev->hist, 0xff, sizeof(dev->hist));
    timer_add(&dev->irq_timer, epia_irq_timer, dev, 0);

    dev->lpt = lpt_attach(epia_write_data, epia_write_ctrl, NULL,
                          epia_read_status, epia_read_ctrl,
                          epia_epp_write_data, epia_epp_request_read, dev);
    if (dev->lpt == NULL) {
        free(dev);
        return NULL;
    }

    dev->ide = ide_lpt_init(dev->port);
    if (dev->ide == NULL)
        epia_log("EPIA: no IDE board for LPT%i\n", dev->port + 1);
    else
        ide_lpt_set_irq(dev->port, epia_ide_irq, dev);

    return dev;
}

static void
epia_close(void *priv)
{
    epia_t *dev = (epia_t *) priv;

    timer_disable(&dev->irq_timer);
    if (dev->ide != NULL)
        ide_lpt_close(dev->port);
    free(dev);
}

const device_t lpt_epia_device = {
    .name          = "Shuttle EPIA parallel port IDE adapter",
    .internal_name = "epia",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = epia_init,
    .close         = epia_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
