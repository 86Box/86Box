/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Tandy VIS memory card interface and the
 *          Dallas DS6417 CyberCard used as its memory card.
 *
 *          The card is a 32 KB serial memory accessed over a three wire
 *          (reset, clock, data) interface. A command is a sync byte (0xE8
 *          for reads, 0x17 for writes), a 19-bit address, a 5-bit command,
 *          a 16-bit select field and a command CRC, all sent LSB first.
 *          After that, data bytes are clocked in or out and the card keeps
 *          a running CRC of them that can be read back with a separate
 *          command.
 *
 *          The VIS drives the card through two registers:
 *
 *          0x318: data register. In automatic mode, accessing it starts
 *                 shifting the next byte in or out.
 *          0x31A: control register.
 *                 Write: bit 0 = clock, bit 1 = data, bit 2 = card reset
 *                        (active high), bit 3 = automatic read (1) or
 *                        write (0), bit 4 = automatic mode (the lines are
 *                        driven directly while it is clear).
 *                 Read:  bit 7 = automatic transfer busy.
 *
 *          The system services in ROM (bank 1) use this to read and write
 *          the card and check the result against the card's CRC.
 *
 *          The card image is always tvis_memcard.bin in the machine's NVR
 *          directory.
 *
 *          Based on the MAME ds6417 device and VIS memory card handlers by
 *          Carl.
 *
 * Authors: Cacodemon345
 *          Carl
 *
 *          Copyright 2026 Cacodemon345.
 *          Copyright Carl.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_TVIS_MEMCARD_LOG
int tvis_memcard_do_log = ENABLE_TVIS_MEMCARD_LOG;

static void
tvis_memcard_log(const char *fmt, ...)
{
    va_list ap;

    if (tvis_memcard_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tvis_memcard_log(fmt, ...)
#endif

#define DS6417_SIZE     32768
#define DS6417_FILENAME "tvis_memcard.bin"

enum {
    CMD_READ      = 0x06,
    CMD_WRITE     = 0x11,
    CMD_READPROT  = 0x05,
    CMD_WRITEPROT = 0x0e,
    CMD_READMASK  = 0x18,
    CMD_READCRC   = 0x03
};

typedef struct ds6417_t {
    uint8_t  mem[DS6417_SIZE];
    uint8_t  present;
    uint8_t  dirty;

    uint8_t  reset;
    uint8_t  clk;
    uint8_t  data;
    uint8_t  read;
    uint8_t  start;
    uint8_t  count;
    uint8_t  shiftreg;
    uint8_t  command;
    uint8_t  crc;
    uint8_t  selbits;
    uint16_t ptr;
    uint32_t addr;
    uint16_t select;
    uint16_t selectval;
} ds6417_t;

typedef struct tvis_memcard_t {
    ds6417_t card;

    uint8_t cardreg;
    uint8_t cardval;
    uint8_t cardcnt;
} tvis_memcard_t;

static void
ds6417_save(ds6417_t *dev)
{
    FILE *fp;

    if (!dev->present || !dev->dirty)
        return;

    fp = nvr_fopen(DS6417_FILENAME, "wb");
    if (fp == NULL) {
        tvis_memcard_log("DS6417: Unable to write image\n");
        return;
    }

    fwrite(dev->mem, 1, sizeof(dev->mem), fp);
    fclose(fp);
    dev->dirty = 0;
}

static void
ds6417_load(ds6417_t *dev)
{
    FILE *fp;

    memset(dev->mem, 0x00, sizeof(dev->mem));

    fp = nvr_fopen(DS6417_FILENAME, "rb");
    if (fp != NULL) {
        size_t len = fread(dev->mem, 1, sizeof(dev->mem), fp);

        if (len != sizeof(dev->mem))
            tvis_memcard_log("DS6417: Image is %u bytes, expected %u\n",
                             (unsigned) len, DS6417_SIZE);
        fclose(fp);
        dev->dirty = (len != sizeof(dev->mem));
    } else {
        /* New, blank card. */
        tvis_memcard_log("DS6417: Creating new image\n");
        dev->dirty = 1;
    }

    dev->present = 1;
    ds6417_save(dev);
}

static void
ds6417_reset(ds6417_t *dev)
{
    /* The ROM always resets the card after a write before reading back
       the CRC, so this is a good point to flush the image. */
    ds6417_save(dev);

    dev->read    = 0;
    dev->start   = 0;
    dev->count   = 0;
    dev->command = 0;
}

static void
ds6417_power_on(ds6417_t *dev)
{
    dev->reset = 0;
    dev->clk   = 0;
    dev->data  = 0;
    ds6417_reset(dev);
}

static void
ds6417_data_w(ds6417_t *dev, int state)
{
    if (!dev->read)
        dev->data = !!state;
}

static void
ds6417_reset_w(ds6417_t *dev, int state)
{
    if (!state && dev->reset)
        ds6417_reset(dev);
    dev->reset = !!state;
}

static int
ds6417_data_r(const ds6417_t *dev)
{
    /* With no card inserted, the data line floats high. */
    if (!dev->present)
        return 1;

    return dev->read ? dev->data : 0;
}

static uint8_t
ds6417_calccrc(uint8_t bit, uint8_t crc)
{
    bit = (crc ^ bit) & 1;
    if (bit)
        return ((crc >> 1) | (bit << 7)) ^ 0x66;
    else
        return crc >> 1;
}

static void
ds6417_clock_w(ds6417_t *dev, int state)
{
    if (!dev->reset || !dev->present)
        return;

    if (dev->clk == (state != 0))
        return;

    dev->clk = (state != 0);

    if (dev->read && !dev->clk && dev->start) {
        /* Data is shifted out on the falling edge. */
        if (!(dev->count & 7)) {
            switch (dev->command) {
                case CMD_READ:
                case CMD_READMASK:
                    dev->shiftreg = dev->mem[dev->ptr];
                    dev->ptr      = (dev->ptr + 1) & (DS6417_SIZE - 1);
                    break;
                case CMD_READPROT:
                    dev->shiftreg = dev->selectval;
                    break;
                case CMD_READCRC:
                    dev->shiftreg = dev->crc;
                    break;
                default:
                    break;
            }
        }

        dev->data = dev->shiftreg & 1;
        dev->shiftreg >>= 1;
        dev->count++;
        dev->crc = ds6417_calccrc(dev->data, dev->crc);
    } else if (dev->clk && !dev->read) {
        /* Data is shifted in on the rising edge. */
        dev->shiftreg = (dev->shiftreg >> 1) | (dev->data ? 0x80 : 0);
        dev->count++;

        if (dev->start) {
            dev->crc = ds6417_calccrc(dev->data, dev->crc);
            if (!(dev->count & 7)) {
                switch (dev->command) {
                    case CMD_WRITE:
                        if (dev->mem[dev->ptr] != dev->shiftreg) {
                            dev->mem[dev->ptr] = dev->shiftreg;
                            dev->dirty         = 1;
                        }
                        dev->ptr = (dev->ptr + 1) & (DS6417_SIZE - 1);
                        break;
                    case CMD_WRITEPROT:
                        dev->selectval = dev->shiftreg;
                        break;
                    default:
                        break;
                }
            }
        } else {
            switch (dev->count) {
                case 8:
                    if ((dev->shiftreg != 0xe8) && (dev->shiftreg != 0x17))
                        ds6417_reset(dev);
                    break;
                case 16:
                    dev->addr = dev->shiftreg;
                    break;
                case 24:
                    dev->addr |= dev->shiftreg << 8;
                    break;
                case 32:
                    dev->addr |= (dev->shiftreg & 7) << 16;
                    dev->command = dev->shiftreg >> 3;
                    break;
                case 40:
                    dev->select = dev->shiftreg;
                    break;
                case 48:
                    dev->select |= dev->shiftreg << 8;
                    break;
                case 56:
                    /* Command CRC. */
                    if ((dev->command & CMD_READMASK) == CMD_READMASK) {
                        dev->selbits = dev->command & 7;
                        dev->command &= 0x18;
                    }
                    switch (dev->command) {
                        case CMD_READ:
                        case CMD_READMASK:
                            dev->crc = 0;
                            fallthrough;
                        case CMD_READPROT:
                        case CMD_READCRC:
                            dev->read = 1;
                            break;
                        case CMD_WRITE:
                            dev->crc = 0;
                            fallthrough;
                        case CMD_WRITEPROT:
                            break;
                        default:
                            tvis_memcard_log("DS6417: Unknown command %02X\n", dev->command);
                            ds6417_reset(dev);
                            return;
                    }
                    tvis_memcard_log("DS6417: Command %02X, address %05X, select %04X\n",
                                     dev->command, dev->addr, dev->select);
                    dev->start = 1;
                    dev->ptr   = dev->addr & (DS6417_SIZE - 1);
                    break;
                default:
                    break;
            }
        }
    }
}

static uint8_t
tvis_memcard_read(uint16_t addr, void *priv)
{
    tvis_memcard_t *dev  = (tvis_memcard_t *) priv;
    ds6417_t       *card = &dev->card;

    if (addr & 2) {
        /* Automatic mode: every status read shifts one bit while busy. */
        if (dev->cardreg & 0x10) {
            if (dev->cardcnt == 8)
                return 0x00;

            if (dev->cardreg & 0x08) {
                ds6417_clock_w(card, 1);
                ds6417_clock_w(card, 0);
                dev->cardval = (dev->cardval >> 1) | (ds6417_data_r(card) ? 0x80 : 0x00);
            } else {
                ds6417_clock_w(card, 0);
                ds6417_data_w(card, dev->cardval & 1);
                ds6417_clock_w(card, 1);
                dev->cardval >>= 1;
            }
            dev->cardcnt++;

            return 0x80;
        }

        return 0x00;
    }

    dev->cardcnt = 0;
    return dev->cardval;
}

static void
tvis_memcard_write(uint16_t addr, uint8_t val, void *priv)
{
    tvis_memcard_t *dev  = (tvis_memcard_t *) priv;
    ds6417_t       *card = &dev->card;

    if (addr & 2) {
        if (!(val & 0x10) && !(dev->cardreg & 0x10)) {
            ds6417_data_w(card, !!(val & 0x02));
            ds6417_clock_w(card, val & 0x01);
            ds6417_reset_w(card, !(val & 0x04));
        }
        dev->cardreg = val;
        dev->cardcnt = (val & 0x08) ? 0 : 8;
    } else {
        dev->cardcnt = 0;
        dev->cardval = val;
    }
}

static void
tvis_memcard_reset(void *priv)
{
    tvis_memcard_t *dev = (tvis_memcard_t *) priv;

    dev->cardreg = 0x00;
    dev->cardval = 0x00;
    dev->cardcnt = 0;
    ds6417_power_on(&dev->card);
}

static void *
tvis_memcard_init(UNUSED(const device_t *info))
{
    tvis_memcard_t *dev = (tvis_memcard_t *) calloc(1, sizeof(tvis_memcard_t));

    if (machine_get_config_int("memcard"))
        ds6417_load(&dev->card);

    ds6417_power_on(&dev->card);

    io_sethandler(0x0318, 1, tvis_memcard_read, NULL, NULL, tvis_memcard_write, NULL, NULL, dev);
    io_sethandler(0x031a, 1, tvis_memcard_read, NULL, NULL, tvis_memcard_write, NULL, NULL, dev);

    return dev;
}

static void
tvis_memcard_close(void *priv)
{
    tvis_memcard_t *dev = (tvis_memcard_t *) priv;

    ds6417_save(&dev->card);
    free(dev);
}

const device_t tvis_memcard_device = {
    .name          = "Tandy VIS Memory Card",
    .internal_name = "tvis_memcard",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = tvis_memcard_init,
    .close         = tvis_memcard_close,
    .reset         = tvis_memcard_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
