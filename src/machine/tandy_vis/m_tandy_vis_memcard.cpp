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
 *          Based on the MAME ds6417 device and VIS memory card handlers by
 *          Carl.
 *
 * Authors: Cacodemon345
 *          Carl
 *
 *          Copyright 2026 Cacodemon345.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
extern "C"
{
#include <86box/io.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
}
#include <86box/plat.h>

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

#define DS6417_SIZE 32768

class ds6417_memcard {
public:
    ds6417_memcard()
    {
        memset(m_mem, 0x00, sizeof(m_mem));
        m_present   = false;
        m_dirty     = false;
        m_fn[0]     = '\0';
        m_shiftreg  = 0;
        m_crc       = 0;
        m_selbits   = 0;
        m_ptr       = 0;
        m_addr      = 0;
        m_select    = 0;
        m_selectval = 0;
        power_on();
    }

    ~ds6417_memcard() { unload(); }

    void load(const char *fn);
    void unload();
    void save();

    void power_on()
    {
        m_reset = false;
        m_clk   = false;
        m_data  = false;
        reset();
    }

    void reset()
    {
        /* The ROM always resets the card after a write before reading back
           the CRC, so this is a good point to flush the image. */
        save();

        m_read    = false;
        m_start   = false;
        m_count   = 0;
        m_command = 0;
    }

    void data_w(int state)
    {
        if (!m_read)
            m_data = state;
    }
    void clock_w(int state);
    void reset_w(int state)
    {
        if (!state && m_reset)
            reset();
        m_reset = state;
    }
    int data_r() const
    {
        /* With no card inserted, the data line floats high. */
        if (!m_present)
            return 1;

        return m_read ? m_data : 0;
    }

private:
    uint8_t calccrc(uint8_t bit, uint8_t crc) const;

    enum {
        CMD_READ      = 0x06,
        CMD_WRITE     = 0x11,
        CMD_READPROT  = 0x05,
        CMD_WRITEPROT = 0x0e,
        CMD_READMASK  = 0x18,
        CMD_READCRC   = 0x03
    };

    uint8_t  m_mem[DS6417_SIZE];
    char     m_fn[1024];
    bool     m_present;
    bool     m_dirty;

    bool     m_reset;
    bool     m_clk;
    bool     m_data;
    bool     m_read;
    bool     m_start;
    uint8_t  m_count;
    uint8_t  m_shiftreg;
    uint8_t  m_command;
    uint8_t  m_crc;
    uint8_t  m_selbits;
    uint16_t m_ptr;
    uint32_t m_addr;
    uint16_t m_select;
    uint16_t m_selectval;
};

void
ds6417_memcard::load(const char *fn)
{
    FILE *fp;

    unload();

    memset(m_mem, 0x00, sizeof(m_mem));
    snprintf(m_fn, sizeof(m_fn), "%s", fn);

    fp = plat_fopen(m_fn, "rb");
    if (fp != NULL) {
        size_t len = fread(m_mem, 1, sizeof(m_mem), fp);

        if (len != sizeof(m_mem))
            tvis_memcard_log("DS6417: Image \"%s\" is %u bytes, expected %u\n",
                             m_fn, (unsigned) len, DS6417_SIZE);
        fclose(fp);
        m_dirty = (len != sizeof(m_mem));
    } else {
        /* New, blank card. */
        tvis_memcard_log("DS6417: Creating new image \"%s\"\n", m_fn);
        m_dirty = true;
    }

    m_present = true;
    save();
}

void
ds6417_memcard::unload()
{
    save();

    m_present = false;
    m_dirty   = false;
    m_fn[0]   = '\0';
}

void
ds6417_memcard::save()
{
    FILE *fp;

    if (!m_present || !m_dirty)
        return;

    fp = plat_fopen(m_fn, "wb");
    if (fp == NULL) {
        tvis_memcard_log("DS6417: Unable to write image \"%s\"\n", m_fn);
        return;
    }

    fwrite(m_mem, 1, sizeof(m_mem), fp);
    fclose(fp);
    m_dirty = false;
}

uint8_t
ds6417_memcard::calccrc(uint8_t bit, uint8_t crc) const
{
    bit = (crc ^ bit) & 1;
    if (bit)
        return ((crc >> 1) | (bit << 7)) ^ 0x66;
    else
        return crc >> 1;
}

void
ds6417_memcard::clock_w(int state)
{
    if (!m_reset || !m_present)
        return;

    if (m_clk == (state != 0))
        return;

    m_clk = state;

    if (m_read && !m_clk && m_start) {
        /* Data is shifted out on the falling edge. */
        if (!(m_count & 7)) {
            switch (m_command) {
                case CMD_READ:
                case CMD_READMASK:
                    m_shiftreg = m_mem[m_ptr];
                    m_ptr      = (m_ptr + 1) & (DS6417_SIZE - 1);
                    break;
                case CMD_READPROT:
                    m_shiftreg = m_selectval;
                    break;
                case CMD_READCRC:
                    m_shiftreg = m_crc;
                    break;
            }
        }

        m_data = m_shiftreg & 1;
        m_shiftreg >>= 1;
        m_count++;
        m_crc = calccrc(m_data, m_crc);
    } else if (m_clk && !m_read) {
        /* Data is shifted in on the rising edge. */
        m_shiftreg = (m_shiftreg >> 1) | (m_data ? 0x80 : 0);
        m_count++;

        if (m_start) {
            m_crc = calccrc(m_data, m_crc);
            if (!(m_count & 7)) {
                switch (m_command) {
                    case CMD_WRITE:
                        if (m_mem[m_ptr] != m_shiftreg) {
                            m_mem[m_ptr] = m_shiftreg;
                            m_dirty      = true;
                        }
                        m_ptr = (m_ptr + 1) & (DS6417_SIZE - 1);
                        break;
                    case CMD_WRITEPROT:
                        m_selectval = m_shiftreg;
                        break;
                }
            }
        } else {
            switch (m_count) {
                case 8:
                    if ((m_shiftreg != 0xe8) && (m_shiftreg != 0x17))
                        reset();
                    break;
                case 16:
                    m_addr = m_shiftreg;
                    break;
                case 24:
                    m_addr |= m_shiftreg << 8;
                    break;
                case 32:
                    m_addr |= (m_shiftreg & 7) << 16;
                    m_command = m_shiftreg >> 3;
                    break;
                case 40:
                    m_select = m_shiftreg;
                    break;
                case 48:
                    m_select |= m_shiftreg << 8;
                    break;
                case 56:
                    /* Command CRC. */
                    if ((m_command & CMD_READMASK) == CMD_READMASK) {
                        m_selbits = m_command & 7;
                        m_command &= 0x18;
                    }
                    switch (m_command) {
                        case CMD_READ:
                        case CMD_READMASK:
                            m_crc = 0;
                            [[fallthrough]];
                        case CMD_READPROT:
                        case CMD_READCRC:
                            m_read = true;
                            break;
                        case CMD_WRITE:
                            m_crc = 0;
                            [[fallthrough]];
                        case CMD_WRITEPROT:
                            break;
                        default:
                            tvis_memcard_log("DS6417: Unknown command %02X\n", m_command);
                            reset();
                            return;
                    }
                    tvis_memcard_log("DS6417: Command %02X, address %05X, select %04X\n",
                                     m_command, m_addr, m_select);
                    m_start = true;
                    m_ptr   = m_addr & (DS6417_SIZE - 1);
                    break;
            }
        }
    }
}

typedef struct tvis_memcard_t {
    ds6417_memcard *card;

    uint8_t cardreg;
    uint8_t cardval;
    uint8_t cardcnt;
} tvis_memcard_t;

static uint8_t
tvis_memcard_read(uint16_t addr, void *priv)
{
    tvis_memcard_t *dev = (tvis_memcard_t *) priv;

    if (addr & 2) {
        /* Automatic mode: every status read shifts one bit while busy. */
        if (dev->cardreg & 0x10) {
            if (dev->cardcnt == 8)
                return 0x00;

            if (dev->cardreg & 0x08) {
                dev->card->clock_w(1);
                dev->card->clock_w(0);
                dev->cardval = (dev->cardval >> 1) | (dev->card->data_r() ? 0x80 : 0x00);
            } else {
                dev->card->clock_w(0);
                dev->card->data_w(dev->cardval & 1);
                dev->card->clock_w(1);
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
    tvis_memcard_t *dev = (tvis_memcard_t *) priv;

    if (addr & 2) {
        if (!(val & 0x10) && !(dev->cardreg & 0x10)) {
            dev->card->data_w(!!(val & 0x02));
            dev->card->clock_w(val & 0x01);
            dev->card->reset_w(!(val & 0x04));
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
    dev->card->power_on();
}

static void *
tvis_memcard_init(UNUSED(const device_t *info))
{
    tvis_memcard_t *dev = (tvis_memcard_t *) calloc(1, sizeof(tvis_memcard_t));

    dev->card = new ds6417_memcard();

    if (machine_get_config_int((char *) "memcard")) {
        const char *fn = machine_get_config_string((char *) "memcard_image");

        if ((fn == NULL) || (fn[0] == '\0'))
            fn = nvr_path((char *) "tvis_memcard.bin");

        dev->card->load(fn);
    }

    io_sethandler(0x0318, 1, tvis_memcard_read, NULL, NULL, tvis_memcard_write, NULL, NULL, dev);
    io_sethandler(0x031a, 1, tvis_memcard_read, NULL, NULL, tvis_memcard_write, NULL, NULL, dev);

    return dev;
}

static void
tvis_memcard_close(void *priv)
{
    tvis_memcard_t *dev = (tvis_memcard_t *) priv;

    delete dev->card;
    free(dev);
}

extern "C" const device_t tvis_memcard_device = {
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
