/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the HighPoint HPT370 IDE controller.
 *
 * Authors: James Weidner, <jamesr@theweidners.us>
 *
 *          Copyright 2026 James Weidner.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/award_lh5.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include "hdc_ide_hpt370.h"

typedef struct hpt370_t hpt370_t;

typedef struct hpt370_func_t {
    hpt370_t *dev;
    int       id;
} hpt370_func_t;

struct hpt370_t {
    uint8_t     pci_slot;
    uint8_t     regs[2][256];
    sff8038i_t *bm[2];
    uint16_t    bm_base[2];
    hpt370_func_t func[2];
    uint32_t    rom_bar_size;
    uint8_t    *rom_image;
    uint16_t     oscillator_reads;
    mem_mapping_t rom_mapping;
};

static void    hpt370_pci_write(int func, int addr, int len, uint8_t val, void *priv);
static uint8_t hpt370_pci_read(int func, int addr, int len, void *priv);
static uint8_t hpt370_io_read(uint16_t port, void *priv);
static void    hpt370_io_write(uint16_t port, uint8_t val, void *priv);
static uint8_t hpt370_bm_read(uint16_t port, uint8_t val, void *priv);

static void
hpt370_rom_mapping_update(hpt370_t *dev)
{
    uint32_t rom_addr;


    rom_addr = dev->regs[0][0x30] | (dev->regs[0][0x31] << 8) |
               (dev->regs[0][0x32] << 16) | (dev->regs[0][0x33] << 24);

    if (dev->rom_image && hdc_onboard_enabled && (dev->regs[0][0x04] & 0x02) && (rom_addr & 0x01))
        mem_mapping_set_addr(&dev->rom_mapping, rom_addr & ~(dev->rom_bar_size - 1), dev->rom_bar_size);
    else
        mem_mapping_disable(&dev->rom_mapping);
}

static void
hpt370_rom_bar_write(hpt370_t *dev, int addr, uint8_t val)
{
    uint32_t rom_addr;

    dev->regs[0][addr] = val;
    rom_addr = dev->regs[0][0x30] | (dev->regs[0][0x31] << 8) |
               (dev->regs[0][0x32] << 16) | (dev->regs[0][0x33] << 24);
    rom_addr &= ~(dev->rom_bar_size - 1);
    rom_addr |= dev->regs[0][0x30] & 0x01;

    dev->regs[0][0x30] = rom_addr & 0xff;
    dev->regs[0][0x31] = (rom_addr >> 8) & 0xff;
    dev->regs[0][0x32] = (rom_addr >> 16) & 0xff;
    dev->regs[0][0x33] = (rom_addr >> 24) & 0xff;
    hpt370_rom_mapping_update(dev);
}

static int
hpt370_header_contains(const uint8_t *header, size_t length, const char *text)
{
    const size_t text_len = strlen(text);

    for (size_t i = 0; i + text_len <= length; i++)
        if (!memcmp(header + i, text, text_len))
            return 1;
    return 0;
}

static int
hpt370_load_award_rom(hpt370_t *dev)
{
    const size_t bios_size = biosmask + 1;

    for (size_t pos = 0; pos + 32 < bios_size; pos++) {
        const size_t header_size = rom[pos] + 2;
        uint32_t packed_size;
        uint32_t original_size;
        uint32_t declared_size;
        uint8_t checksum = 0;

        if (header_size < 32 || pos + header_size > bios_size || memcmp(rom + pos + 2, "-lh5-", 5))
            continue;
        if (!hpt370_header_contains(rom + pos + 22, header_size - 22, "h37095b.bin"))
            continue;

        packed_size = rom[pos + 7] | (rom[pos + 8] << 8) | (rom[pos + 9] << 16) | (rom[pos + 10] << 24);
        original_size = rom[pos + 11] | (rom[pos + 12] << 8) | (rom[pos + 13] << 16) | (rom[pos + 14] << 24);
        if (pos + header_size + packed_size > bios_size || original_size > dev->rom_bar_size)
            return 0;

        dev->rom_image = malloc(dev->rom_bar_size);
        if (!dev->rom_image)
            return 0;
        memset(dev->rom_image, 0xff, dev->rom_bar_size);
        if (!award_lh5_decode(rom + pos + header_size, packed_size, dev->rom_image, original_size)) {
            free(dev->rom_image);
            dev->rom_image = NULL;
            return 0;
        }
        if (dev->rom_image[0] != 0x55 || dev->rom_image[1] != 0xaa)
            return 0;

        declared_size = dev->rom_image[2] << 9;
        if (!declared_size || declared_size > dev->rom_bar_size || original_size > declared_size)
            return 0;

        /*
         * Award stores only the initialized portion of the option ROM module.
         * The remainder of its declared image is BSS/workspace and must start
         * at zero; 0xff here makes the HPT BIOS treat every probe state as set.
         */
        memset(dev->rom_image + original_size, 0x00, declared_size - original_size);
        for (uint32_t i = 0; i < declared_size; i++)
            checksum += dev->rom_image[i];
        dev->rom_image[declared_size - 1] -= checksum;
        return 1;
    }
    return 0;
}

#ifdef ENABLE_HPT370_LOG
int hpt370_do_log = ENABLE_HPT370_LOG;

static void
hpt370_log(const char *fmt, ...)
{
    va_list ap;

    if (hpt370_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define hpt370_log(fmt, ...)
#endif

static void
hpt370_ide_handler(hpt370_t *dev, int func)
{
    const int channel = func + 2;
    const int channel_enabled = dev->regs[0][0x50 + (func << 2)] & 0x04;
    const int bar = func << 3;
    uint16_t  main;
    uint16_t  side;

    ide_handlers(channel, 0);

    main = (dev->regs[0][0x11 + bar] << 8) |
           (dev->regs[0][0x10 + bar] & 0xf8);
    side = ((dev->regs[0][0x15 + bar] << 8) |
            (dev->regs[0][0x14 + bar] & 0xfc)) + 2;

    ide_set_base(channel, main);
    ide_set_side(channel, side);

    if (hdc_onboard_enabled && channel_enabled && (dev->regs[0][0x04] & 0x01) && main && side)
        ide_handlers(channel, 1);

    hpt370_log("HPT370 function %i: IDE %i at %04X/%04X enabled=%i command=%02X control=%02X onboard=%i\n",
               func, channel, main, side,
               hdc_onboard_enabled && channel_enabled && (dev->regs[0][0x04] & 0x01) && main && side,
               dev->regs[0][0x04], dev->regs[0][0x50 + (func << 2)], hdc_onboard_enabled);
}

static void
hpt370_bm_handler(hpt370_t *dev, int func)
{
    const uint16_t base = ((dev->regs[0][0x21] << 8) | (dev->regs[0][0x20] & 0xf0)) + (func << 3);
    const int      enabled = hdc_onboard_enabled && ((dev->regs[0][0x04] & 0x05) == 0x05);

    if (dev->bm_base[func] && !func)
        io_removehandler(dev->bm_base[func] + 0x10, 0x8a,
                         hpt370_io_read, NULL, NULL,
                         hpt370_io_write, NULL, NULL, &dev->func[func]);

    sff_bus_master_handler(dev->bm[func], enabled, base);

    dev->bm_base[func] = base;
    if (hdc_onboard_enabled && (dev->regs[0][0x04] & 0x01) && base && !func)
        io_sethandler(base + 0x10, 0x8a,
                      hpt370_io_read, NULL, NULL,
                      hpt370_io_write, NULL, NULL, &dev->func[func]);
}

static uint8_t
hpt370_io_read(uint16_t port, void *priv)
{
    hpt370_func_t *ctx  = (hpt370_func_t *) priv;
    const uint8_t   offset = port - ctx->dev->bm_base[ctx->id];
    const uint8_t   addr   = (offset < 0x20) ? offset : (offset - 0x20);
    uint8_t         ret  = hpt370_pci_read(ctx->id, addr, 1, ctx->dev);

    hpt370_log("HPT370 F%i I/O read %04X [%02X] = %02X\n",
               ctx->id, port, addr, ret);
    return ret;
}

static void
hpt370_io_write(uint16_t port, uint8_t val, void *priv)
{
    hpt370_func_t *ctx  = (hpt370_func_t *) priv;
    const uint8_t   offset = port - ctx->dev->bm_base[ctx->id];
    const uint8_t   addr   = (offset < 0x20) ? offset : (offset - 0x20);

    hpt370_log("HPT370 F%i I/O write %04X [%02X] = %02X\n",
               ctx->id, port, addr, val);
    hpt370_pci_write(ctx->id, addr, 1, val, ctx->dev);
}

static uint8_t
hpt370_bm_read(uint16_t port, uint8_t val, void *priv)
{
    hpt370_func_t *ctx = (hpt370_func_t *) priv;

    /*
     * The generic SFF device's reset callback signals an inactive IDE IRQ by
     * setting status bit 0.  On the HPT370 that bit is the bus-master active
     * flag, and its option ROM waits for it to clear before every transfer.
     * Keep the status coherent with the command register after reset.
     */
    if (((port & 7) == 2) && !(ctx->dev->bm[ctx->id]->command & 1))
        val &= ~1;

    return val;
}

static void
hpt370_set_irq_0(uint8_t status, void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    sff_bus_master_set_irq(status, dev->bm[0]);
}

static void
hpt370_set_irq_1(uint8_t status, void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    sff_bus_master_set_irq(status, dev->bm[1]);
}

static int
hpt370_bus_master_dma_0(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    return sff_bus_master_dma(data, transfer_length, total_length, out, dev->bm[0]);
}

static int
hpt370_bus_master_dma_1(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    return sff_bus_master_dma(data, transfer_length, total_length, out, dev->bm[1]);
}

static void
hpt370_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    if (func > 0)
        return;

    hpt370_log("HPT370 F%i PCI write %02X = %02X\n", func, addr, val);

    switch (addr) {
        case 0x04:
            dev->regs[func][addr] = val & 0x07;
            hpt370_ide_handler(dev, func);
            hpt370_bm_handler(dev, func);
            hpt370_rom_mapping_update(dev);
            break;
        case 0x05:
            dev->regs[func][addr] = val & 0x01;
            break;
        case 0x07:
            dev->regs[func][addr] &= ~(val & 0xb8);
            break;
        case 0x0d:
            dev->regs[func][addr] = val;
            break;
        case 0x10:
            dev->regs[func][addr] = (val & 0xf8) | 0x01;
            hpt370_ide_handler(dev, func);
            break;
        case 0x11 ... 0x13:
            dev->regs[func][addr] = val;
            hpt370_ide_handler(dev, func);
            break;
        case 0x14:
            dev->regs[func][addr] = (val & 0xfc) | 0x01;
            hpt370_ide_handler(dev, func);
            break;
        case 0x15 ... 0x17:
            dev->regs[func][addr] = val;
            hpt370_ide_handler(dev, func);
            break;
        case 0x18:
            dev->regs[0][addr] = (val & 0xf8) | 0x01;
            hpt370_ide_handler(dev, 1);
            break;
        case 0x19 ... 0x1b:
            dev->regs[0][addr] = val;
            hpt370_ide_handler(dev, 1);
            break;
        case 0x1c:
            dev->regs[0][addr] = (val & 0xfc) | 0x01;
            hpt370_ide_handler(dev, 1);
            break;
        case 0x1d ... 0x1f:
            dev->regs[0][addr] = val;
            hpt370_ide_handler(dev, 1);
            break;
        case 0x20:
            dev->regs[func][addr] = (val & 0xf0) | 0x01;
            hpt370_bm_handler(dev, 0);
            hpt370_bm_handler(dev, 1);
            break;
        case 0x21 ... 0x23:
            dev->regs[func][addr] = val;
            hpt370_bm_handler(dev, 0);
            hpt370_bm_handler(dev, 1);
            break;
        case 0x2e ... 0x2f:
            if (dev->regs[0][0x5a] & 0x04)
                dev->regs[0][addr] = val;
            break;
        case 0x3c:
            dev->regs[func][addr] = val;
            break;
        case 0x30 ... 0x33:
            if (!func)
                hpt370_rom_bar_write(dev, addr, val);
            break;
        case 0x50:
        case 0x54:
            dev->regs[0][addr] = val;
            hpt370_ide_handler(dev, (addr - 0x50) >> 2);
            break;
        case 0x51:
            /* Reset/flush bits are pulses; only persistent control bits read back. */
            dev->regs[0][addr] = dev->regs[1][addr] = val & 0xf0;
            break;
        case 0x52 ... 0x53:
        case 0x55 ... 0x5a:
            dev->regs[0][addr] = dev->regs[1][addr] = val;
            break;
        case 0x5b:
            /* Bit 7 is generated by the DPLL and is never software-writable. */
            dev->regs[0][addr] = val & 0x7f;
            dev->oscillator_reads = 0;
            break;
        case 0x78 ... 0x79:
            /* f_CNT is generated from the PCI clock and is read-only. */
            break;
        case 0x40 ... 0x4f:
        case 0x5c ... 0x77:
        case 0x7a ... 0xff:
            dev->regs[func][addr] = val;
            break;
        default:
            break;
    }
}

static uint8_t
hpt370_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    if (func > 0)
        return 0xff;

    uint8_t ret = dev->regs[func][addr];

    if (addr == 0x5b) {
        if ((ret & 0x7f) == 0x28) {
            /*
             * BIOS 0.95b measures the DPLL period by waiting for both edges
             * of osc_ok.  A fixed-high value makes it declare the chip dead.
             */
            if ((dev->oscillator_reads++ & 0x0f) >= 8)
                ret |= 0x80;
            else
                ret &= ~0x80;
        } else {
            ret |= 0x80;
        }
    }

    hpt370_log("HPT370 F%i PCI read %02X = %02X\n", func, addr, ret);
    return ret;
}

void
hpt370_reset(void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    for (int func = 0; func < 2; func++) {
        sff_bus_master_reset(dev->bm[func]);
        memset(dev->regs[func], 0x00, sizeof(dev->regs[func]));

        dev->regs[func][0x00] = 0x03; /* HighPoint Technologies */
        dev->regs[func][0x01] = 0x11;
        dev->regs[func][0x02] = 0x04; /* HPT370 */
        dev->regs[func][0x03] = 0x00;
        dev->regs[func][0x06] = 0x10;
        dev->regs[func][0x07] = 0x02;
        dev->regs[func][0x08] = 0x03;
        dev->regs[func][0x09] = 0x00;
        dev->regs[func][0x0a] = 0x04; /* RAID controller. */
        dev->regs[func][0x0b] = 0x01;
        dev->regs[func][0x0e] = 0x00;
        dev->regs[func][0x10] = 0x01;
        dev->regs[func][0x14] = 0x01;
        dev->regs[func][0x18] = 0x01;
        dev->regs[func][0x1c] = 0x01;
        dev->regs[func][0x20] = 0x01;
        dev->regs[func][0x2c] = 0x03;
        dev->regs[func][0x2d] = 0x11;
        dev->regs[func][0x2e] = 0x04;
        dev->regs[func][0x2f] = 0x00;
        dev->regs[func][0x3d] = PCI_INTA;
        dev->regs[func][0x34] = 0x60;
        dev->regs[func][0x60] = 0x01;

        /* 33 MHz safe PIO defaults for both devices on each channel. */
        dev->regs[func][0x40] = 0xa7;
        dev->regs[func][0x41] = 0xa7;
        dev->regs[func][0x42] = 0x20;
        dev->regs[func][0x43] = 0x01;
        dev->regs[func][0x44] = 0xa7;
        dev->regs[func][0x45] = 0xa7;
        dev->regs[func][0x46] = 0x20;
        dev->regs[func][0x47] = 0x01;

        sff_set_slot(dev->bm[func], dev->pci_slot);
        sff_set_irq_pin(dev->bm[func], PCI_INTA);
        sff_set_irq_mode(dev->bm[func], IRQ_MODE_PCI_IRQ_PIN);

        hpt370_ide_handler(dev, func);
        hpt370_bm_handler(dev, func);
    }

    dev->regs[0][0x50] = 0x04;
    dev->regs[0][0x54] = 0x04;
    dev->regs[0][0x5b] = 0x23;
    dev->regs[0][0x78] = 0x9c; /* 33 MHz PCI clock. */
    dev->regs[0][0x79] = 0x00;
    hpt370_ide_handler(dev, 0);
    hpt370_ide_handler(dev, 1);
    hpt370_rom_mapping_update(dev);
}

void
hpt370_close(void *priv)
{
    hpt370_t *dev = (hpt370_t *) priv;

    for (int func = 0; func < 2; func++)
        if (dev->bm_base[func] && !func)
            io_removehandler(dev->bm_base[func] + 0x10, 0x8a,
                             hpt370_io_read, NULL, NULL,
                             hpt370_io_write, NULL, NULL, &dev->func[func]);

    free(dev->rom_image);
    free(dev);
}

void *
hpt370_init(UNUSED(const device_t *info))
{
    hpt370_t *dev = (hpt370_t *) calloc(1, sizeof(hpt370_t));

    dev->rom_bar_size = (info->local == HPT370_ROM_BAR_64K) ? 0x00010000 : 0x00008000;
    hpt370_load_award_rom(dev);
    hpt370_log("HPT370: embedded Award option ROM %s (%u-byte PCI window)\n",
               dev->rom_image ? "loaded" : "not found", dev->rom_bar_size);
    mem_mapping_add(&dev->rom_mapping, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL,
                    dev->rom_image, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM, dev);
    mem_mapping_disable(&dev->rom_mapping);
    dev->func[0].dev = dev;
    dev->func[0].id  = 0;
    dev->func[1].dev = dev;
    dev->func[1].id  = 1;

    device_add(&ide_pci_ter_qua_2ch_device);
    pci_add_card(PCI_ADD_IDE, hpt370_pci_read, hpt370_pci_write, dev, &dev->pci_slot);

    dev->bm[0] = device_add_inst(&sff8038i_device, 3);
    dev->bm[1] = device_add_inst(&sff8038i_device, 4);

    sff_set_ven_handlers(dev->bm[0], NULL, hpt370_bm_read, &dev->func[0]);
    sff_set_ven_handlers(dev->bm[1], NULL, hpt370_bm_read, &dev->func[1]);

    ide_set_bus_master(2, hpt370_bus_master_dma_0, hpt370_set_irq_0, dev);
    ide_set_bus_master(3, hpt370_bus_master_dma_1, hpt370_set_irq_1, dev);

    hpt370_reset(dev);

    return dev;
}

const device_t ide_hpt370_ter_qua_onboard_device = {
    .name          = "HighPoint HPT370 (Tertiary and Quaternary) On-Board",
    .internal_name = "ide_hpt370_ter_qua_onboard",
    .flags         = DEVICE_PCI,
    .local         = HPT370_ROM_BAR_32K,
    .init          = hpt370_init,
    .close         = hpt370_close,
    .reset         = hpt370_reset
};
