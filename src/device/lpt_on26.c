/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           OnSpec 90c26 parallel port to IDE adapter (the ValuStore and
 *           similar parallel port hard disks and CD-ROM drives).
 *
 *           There is no public datasheet for the chip. The protocol is
 *           the one Linux's paride driver on26.c (Grant R. Guenther,
 *           drivers/block/paride, later drivers/ata/pata_parport) speaks
 *           in its 4-bit and 8-bit modes, with the register notes of
 *           evillabs.net's OnSpec 90c26 page, and what ValuStor's DOS
 *           drivers (VSMOBILE.SYS 2.00 and 3.26, run against the model)
 *           need on top of it:
 *
 *           - The adapter wakes up on an IEEE 1284.3 style command packet,
 *             AA 55 00 FF 87 78 <command> after any byte (Linux sends FE,
 *             ValuStor FF), with the command strobed in: 20h connects,
 *             30h disconnects. The status lines answer B, 5, B in the high
 *             nibble after FF, 87 and 78, and F once the command is
 *             strobed (on26_test_port() checks "0xb5f").
 *           - CDBLAZER.SYS assigns the adapter daisy chain address 0
 *             (command 00h) and wants nFault low after 78 from then on, or
 *             it takes the chain for busy and drops the request. It counts
 *             a second drive if 21h (address 1) connects as well.
 *           - Once connected, SELECT pulses with STROBE high latch a
 *             register index off the data lines (P1), and AUTOFD pulses
 *             with STROBE high write the data lines into that register
 *             (P2). Register 1 holds the IDE address, (reg << 2) + 1 for
 *             the command block and + 2 for the control block; register 2
 *             the mode: bit 0 8-bit reads, bit 1 the drive's reset line,
 *             bit 4 block (streaming) mode. Register 0 is the window onto
 *             the IDE register register 1 points at. Register 4 is the
 *             ASIC version, register 5 a loopback buffer the drivers test
 *             the link with (register 3 rewinds it), and the other
 *             registers read back what was written.
 *           - The IDE bus is 16 bits wide and the window 8: every IDE
 *             cycle is two bytes, low first. Register writes therefore
 *             send their value twice, and reads clock out a whole word:
 *             four nibbles on status bits 7-4 (4-bit mode) or two bytes on
 *             the data lines with the port reversed (8-bit mode). AUTOFD
 *             edges with STROBE low clock the reads; in register mode only
 *             the rising edges count, in block mode both, for writes too.
 *             An internal register reads as its value and a second byte
 *             with bit 5 set.
 *
 *           The drive behind it is a regular hard disk of 86Box's IDE
 *           emulation, put on the parallel port in the hard disk settings
 *           (bus LPT): the adapter is added for it, with an IDE board of
 *           its own that has no I/O ports and no IRQ, and the disk is the
 *           master on its cable.
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

#ifdef ENABLE_ON26_LOG
int on26_do_log = ENABLE_ON26_LOG;

static void
on26_log(const char *fmt, ...)
{
    va_list ap;

    if (on26_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define on26_log(fmt, ...)
#endif

#define LPT_CTRL_STROBE 0x01
#define LPT_CTRL_AUTOFD 0x02
#define LPT_CTRL_INIT   0x04
#define LPT_CTRL_SELECT 0x08
#define LPT_CTRL_DIR    0x20

#define LPT_STAT_MASK   0xf8
#define LPT_STAT_IDLE   0xd8

/* The command packet, and the status the adapter shows part-way through.
   Linux sends FE ahead of it, ValuStor's driver FF: the byte before AA
   does not count. */
static const uint8_t on26_cpp[6] = { 0xaa, 0x55, 0x00, 0xff, 0x87, 0x78 };

#define ON26_CPP_CONNECT    0x20
#define ON26_CPP_DISCONNECT 0x30
#define ON26_CPP_ASSIGN     0x00 /* 00h-03h: the chain address */

#define ON26_CHK_1          0xb8 /* after FE AA 55 00 FF */
#define ON26_CHK_2          0x58 /* after 87 */
#define ON26_CHK_78         0xb8 /* after 78 */
#define ON26_CHK_78_ADDR    0xb0 /* after 78, with a chain address */
#define ON26_CHK_3          0xf8 /* the command strobed */

/* After a register write, until a read is clocked, the status lines show
   the adapter: PError set, and BUSY (bit 7) the drive's INTRQ.
   CDBLAZER.SYS finds the adapter by PError and polls BUSY for the end of
   a command. */
#define ON26_STAT_REG       0x28
#define ON26_STAT_INTRQ     0x80

/* An internal register reads as a word: the register, then a second byte
   in which ValuStor's drivers want bit 5 set. Register 4, read only, is
   the ASIC version (the drivers take 04h, 11h and 12h). */
#define ON26_RD_HIGH        0x20
#define ON26_VERSION        0x12

/* Internal register 5 is a loopback buffer: what is written into it reads
   back in order. Register 3 = 01h rewinds it for writing, 05h for reading. */
#define ON26_LOOP_WRITE     0x01
#define ON26_LOOP_READ      0x05
#define ON26_LOOP_SIZE      16

/* Mode register (internal register 2) bits. */
#define ON26_MODE_8BIT      0x01
#define ON26_MODE_RESET     0x02
#define ON26_MODE_BLOCK     0x10

typedef struct on26_t {
    void    *lpt;
    uint8_t  port;

    /* Parallel port side. */
    uint8_t  dat;        /* data lines, as the host last wrote them */
    uint8_t  ctrl;       /* control lines, as the host last wrote them */
    uint8_t  cpp_pos;    /* bytes of the command packet matched so far */
    uint8_t  cpp_stat;   /* checkpoint status while a packet runs */
    uint8_t  chain_addr; /* a daisy chain address was assigned */
    uint8_t  connected;
    uint8_t  index;      /* internal register addressed */
    uint8_t  regs[16];   /* internal registers; 1 = IDE address, 2 = mode */
    uint8_t  wr_lo;      /* low byte of an IDE write cycle */
    uint8_t  wr_half;    /* the low byte is in */
    uint16_t rd_word;    /* the IDE read cycle being clocked out */
    uint8_t  rd_step;    /* nibbles (or bytes) of it handed out */
    uint8_t  status_out; /* what the status lines show while connected */
    uint8_t  reg_stat;   /* they show the adapter (ON26_STAT_REG) instead */
    uint8_t  loop[ON26_LOOP_SIZE]; /* the loopback buffer (register 5) */
    uint8_t  loop_wr;
    uint8_t  loop_rd;

    /* The IDE board behind it. */
    void    *ide;
} on26_t;

/* --------------------------------------------------------------------- */
/* The IDE bus                                                           */
/* --------------------------------------------------------------------- */

/* An IDE cycle at the address register 1 holds: (reg << 2) + 1 for the
   command block, + 2 for the control block. */
static uint16_t
ata_read(on26_t *dev, uint8_t addr)
{
    const int reg = (addr >> 2) & 7;

    if (dev->ide == NULL)
        return 0xffff;

    if ((addr & 3) == 2)
        return (reg == 6) ? ide_read_alt_status(0, dev->ide) : 0xff;

    if (reg == 0)
        return ide_readw(0, dev->ide);

    return ide_readb(reg, dev->ide);
}

static void
ata_write(on26_t *dev, uint8_t addr, uint16_t val)
{
    const int reg = (addr >> 2) & 7;

    if (dev->ide == NULL)
        return;

    if ((addr & 3) == 2) {
        if (reg == 6)
            ide_write_devctl(0, (uint8_t) val, dev->ide);
        return;
    }

    if (reg == 0)
        ide_writew(0, val, dev->ide);
    else
        ide_writeb(reg, (uint8_t) val, dev->ide);
}

/* --------------------------------------------------------------------- */
/* The adapter                                                           */
/* --------------------------------------------------------------------- */

static void
on26_write_reg(on26_t *dev, uint8_t index, uint8_t val)
{
    if (index == 0) {
        /* The window: two bytes make an IDE write cycle. */
        if (!dev->wr_half) {
            dev->wr_lo   = val;
            dev->wr_half = 1;
        } else {
            dev->wr_half = 0;
            on26_log("ON26: IDE write %02X = %04X\n", dev->regs[1], dev->wr_lo | (val << 8));
            ata_write(dev, dev->regs[1], (uint16_t) (dev->wr_lo | (val << 8)));
        }
        return;
    }

    on26_log("ON26: register %X = %02X\n", index, val);

    if (index == 5) {
        dev->loop[dev->loop_wr++ % ON26_LOOP_SIZE] = val;
        return;
    }
    if (index == 3) {
        if (val == ON26_LOOP_WRITE)
            dev->loop_wr = 0;
        else if (val == ON26_LOOP_READ)
            dev->loop_rd = 0;
    }

    /* The drive's reset line, as a soft reset while it is held. */
    if (index == 2) {
        if ((val & ON26_MODE_RESET) && !(dev->regs[2] & ON26_MODE_RESET))
            ata_write(dev, (6 << 2) + 2, 0x0c);
        else if (!(val & ON26_MODE_RESET) && (dev->regs[2] & ON26_MODE_RESET))
            ata_write(dev, (6 << 2) + 2, 0x08);
    }

    dev->regs[index & 15] = val;
    dev->rd_step         = 0;
    dev->wr_half         = 0;
    dev->reg_stat        = 1;
}

/* The next nibble (4-bit) or byte (8-bit) of the window. */
static void
on26_clock_read(on26_t *dev)
{
    int     bytes = !!(dev->ctrl & LPT_CTRL_DIR);
    int     steps = bytes ? 2 : 4;
    uint8_t byte;

    dev->reg_stat = 0;
    if (dev->rd_step == 0) {
        if (dev->index == 4)
            dev->rd_word = ON26_VERSION | (ON26_RD_HIGH << 8);
        else if (dev->index == 5) {
            dev->rd_word = dev->loop[dev->loop_rd++ % ON26_LOOP_SIZE];
            dev->rd_word |= dev->loop[dev->loop_rd++ % ON26_LOOP_SIZE] << 8;
        } else if (dev->index != 0)
            dev->rd_word = dev->regs[dev->index] | (ON26_RD_HIGH << 8);
        else {
            dev->rd_word = ata_read(dev, dev->regs[1]);
            on26_log("ON26: IDE read %02X -> %04X\n", dev->regs[1], dev->rd_word);
        }
    }

    if (bytes) {
        byte = (uint8_t) (dev->rd_word >> (dev->rd_step * 8));
        if (dev->lpt != NULL)
            lpt_write_to_dat(dev->lpt, byte);
    } else {
        byte            = (uint8_t) (dev->rd_word >> (dev->rd_step * 4));
        dev->status_out = (uint8_t) (((byte & 0x0f) << 4) | 0x08);
    }

    if (++dev->rd_step >= steps)
        dev->rd_step = 0;
}

static void
on26_connect(on26_t *dev, uint8_t cmd)
{
    if (cmd == ON26_CPP_CONNECT) {
        if (!dev->connected)
            on26_log("ON26: connected\n");
        dev->connected  = 1;
        dev->index      = 0;
        dev->rd_step    = 0;
        dev->wr_half    = 0;
        dev->status_out = 0x08;
        dev->reg_stat   = 0;
    } else {
        /* Only address 0, the one adapter on the chain. */
        if ((cmd & 0xfc) == ON26_CPP_ASSIGN)
            dev->chain_addr = (cmd == ON26_CPP_ASSIGN);
        if (dev->connected)
            on26_log("ON26: disconnected (%02X)\n", cmd);
        dev->connected = 0;
    }
}

static void
on26_write_data(uint8_t val, void *priv)
{
    on26_t *dev = (on26_t *) priv;

    dev->dat = val;

    /* The command packet, byte by byte; the command itself is strobed. */
    if (dev->cpp_pos < sizeof(on26_cpp)) {
        if (val == on26_cpp[dev->cpp_pos])
            dev->cpp_pos++;
        else
            dev->cpp_pos = (val == on26_cpp[0]) ? 1 : 0;

        if (dev->cpp_pos == 4)
            dev->cpp_stat = ON26_CHK_1;
        else if (dev->cpp_pos == 5)
            dev->cpp_stat = ON26_CHK_2;
        else if (dev->cpp_pos == 6)
            dev->cpp_stat = dev->chain_addr ? ON26_CHK_78_ADDR : ON26_CHK_78;
        else
            dev->cpp_stat = 0;
    } else if (dev->cpp_pos == sizeof(on26_cpp))
        dev->cpp_pos++; /* the command byte, waiting for its strobe */
    else {
        dev->cpp_pos  = (val == on26_cpp[0]) ? 1 : 0;
        dev->cpp_stat = 0;
    }

    /* The host reads its own latch back unless the port is reversed. */
    if (dev->lpt != NULL)
        lpt_write_to_dat(dev->lpt, val);
}

static void
on26_write_ctrl(uint8_t val, void *priv)
{
    on26_t       *dev  = (on26_t *) priv;
    const uint8_t old  = dev->ctrl;
    const uint8_t rise = (uint8_t) (val & ~old);
    const uint8_t chg  = (uint8_t) (val ^ old);

    dev->ctrl = val;

    /* The command byte of a packet is taken on STROBE. */
    if ((dev->cpp_pos == sizeof(on26_cpp) + 1) && (rise & LPT_CTRL_STROBE)) {
        dev->cpp_stat = ON26_CHK_3;
        dev->cpp_pos  = 0;
        on26_connect(dev, dev->dat);
        return;
    }

    if (!dev->connected)
        return;

    if (val & LPT_CTRL_STROBE) {
        /* P1: SELECT latches an index. */
        if (rise & LPT_CTRL_SELECT) {
            dev->index   = dev->dat & 15;
            dev->rd_step = 0;
            if (dev->index != 0)
                dev->wr_half = 0;
            return;
        }

        /* P2, or a byte of a block write. */
        if (chg & LPT_CTRL_AUTOFD) {
            if ((rise & LPT_CTRL_AUTOFD) || (dev->regs[2] & ON26_MODE_BLOCK))
                on26_write_reg(dev, dev->index, dev->dat);
        }
        return;
    }

    /* STROBE low: AUTOFD clocks reads. */
    if (chg & LPT_CTRL_AUTOFD) {
        if ((rise & LPT_CTRL_AUTOFD) || (dev->regs[2] & ON26_MODE_BLOCK))
            on26_clock_read(dev);
    }
}

static uint8_t
on26_read_status(void *priv)
{
    const on26_t *dev = (on26_t *) priv;

    if (dev->cpp_stat)
        return dev->cpp_stat & LPT_STAT_MASK;
    if (dev->connected && dev->reg_stat)
        return ON26_STAT_REG | (ide_lpt_intrq(dev->port) ? ON26_STAT_INTRQ : 0x00);
    if (dev->connected)
        return dev->status_out & LPT_STAT_MASK;

    return LPT_STAT_IDLE;
}

/* --------------------------------------------------------------------- */
/* 86Box device plumbing                                                 */
/* --------------------------------------------------------------------- */

static void *
on26_init(UNUSED(const device_t *info))
{
    on26_t *dev = calloc(1, sizeof(on26_t));

    if (dev == NULL)
        return NULL;

    dev->port    = (uint8_t) (device_get_instance() - 1);
    dev->regs[2] = 0x08;

    dev->lpt = lpt_attach(on26_write_data, on26_write_ctrl, NULL,
                          on26_read_status, NULL, NULL, NULL, dev);
    if (dev->lpt == NULL) {
        free(dev);
        return NULL;
    }

    dev->ide = ide_lpt_init(dev->port);
    if (dev->ide == NULL)
        on26_log("ON26: no IDE board for LPT%i\n", dev->port + 1);

    return dev;
}

static void
on26_close(void *priv)
{
    on26_t *dev = (on26_t *) priv;

    if (dev->ide != NULL)
        ide_lpt_close(dev->port);
    free(dev);
}

const device_t lpt_on26_device = {
    .name          = "OnSpec 90c26 parallel port IDE adapter",
    .internal_name = "on26",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = on26_init,
    .close         = on26_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
