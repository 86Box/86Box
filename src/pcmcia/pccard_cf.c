/*
 * 86Box       CompactFlash storage in a 16-bit PC Card socket.
 *
 *             CF-ATA register mapping and CIS follow the SanDisk
 *             CompactFlash Memory Card Product Manual, sections 4-6 and 8.
 *
 *             Released under the GNU General Public License version 2 or
 *             later. See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/pcmcia.h>
#include <86box/hdc_ide.h>

#define CF_CONFIG_BASE 0x200

typedef struct cf_card_t {
    pccard_t  card;
    ide_cf_t *ide;
    int       socket;
    int       intr;
    uint8_t   cis[256];
    uint8_t   cor, ccsr, prr, copy;
} cf_card_t;

static void
cf_build_cis(cf_card_t *dev)
{
    static const char *const names[]     = { "SunDisk", "SDP" };
    static const uint8_t     device[]    = { 0xd9, 0x01, 0xff };
    static const uint8_t     function[]  = { 0x04, 0x01 }; /* fixed disk, POST */
    static const uint8_t     interface[] = { 0x01, 0x01 }; /* ATA interface */
    static const uint8_t     features[]  = { 0x02, 0x0c, 0x0f };
    /* Index 0: common memory. Index 1: 16 contiguous relocatable I/O ports.
       Indices 2 and 3: two ranges, for the legacy IDE command/control ports. */
    static const uint8_t memory[]  = { 0x80, 0xc0, 0x20, 0x08, 0x00 };
    static const uint8_t reloc[]   = { 0xc1, 0x41, 0x18, 0x64, 0x30, 0xfc, 0xde };
    static const uint8_t primary[] = {
        0x82, 0x41, 0x18, 0xea, 0x61, 0xf0, 0x01, 0x07, 0xf6, 0x03, 0x01, 0x30, 0xfc, 0xde
    };
    static const uint8_t secondary[] = {
        0x83, 0x41, 0x18, 0xea, 0x61, 0x70, 0x01, 0x07, 0x76, 0x03, 0x01, 0x30, 0xfc, 0xde
    };
    pccard_cis_t cis;

    pccard_cis_init(&cis, dev->cis, sizeof(dev->cis));
    pccard_cis_tuple(&cis, CISTPL_DEVICE, device, sizeof(device));
    pccard_cis_tuple(&cis, CISTPL_NO_LINK, NULL, 0);
    pccard_cis_vers1(&cis, names, 2);
    pccard_cis_manfid(&cis, 0x0045, 0x0401);
    pccard_cis_tuple(&cis, CISTPL_FUNCID, function, sizeof(function));
    pccard_cis_tuple(&cis, CISTPL_FUNCE, interface, sizeof(interface));
    pccard_cis_tuple(&cis, CISTPL_FUNCE, features, sizeof(features));
    pccard_cis_config(&cis, CF_CONFIG_BASE, 0x0f, 3);
    pccard_cis_tuple(&cis, CISTPL_CFTABLE_ENTRY, memory, sizeof(memory));
    pccard_cis_tuple(&cis, CISTPL_CFTABLE_ENTRY, reloc, sizeof(reloc));
    pccard_cis_tuple(&cis, CISTPL_CFTABLE_ENTRY, primary, sizeof(primary));
    pccard_cis_tuple(&cis, CISTPL_CFTABLE_ENTRY, secondary, sizeof(secondary));
    pccard_cis_end(&cis);
    /* Match the SunDisk SDP entry in Windows' MSHDC.INF. */
    pccard_cis_match_id(&cis, 0x6dc7);
}

static void
cf_irq_update(cf_card_t *dev)
{
    const int mode = dev->cor & 0xbf;

    pcmcia_card_irq(dev->socket, dev->intr && (mode >= 1) && (mode <= 3));
}

static void
cf_interrupt(int level, void *priv)
{
    cf_card_t *dev = priv;

    dev->intr = level;
    cf_irq_update(dev);
}

static void
cf_reset(void *priv)
{
    cf_card_t *dev = priv;

    dev->cor = dev->ccsr = dev->prr = dev->copy = 0;
    ide_cf_reset(dev->ide);
}

static uint8_t
cf_attr_read(uint32_t addr, void *priv)
{
    const cf_card_t *dev = priv;

    if (!(addr & 1) && (addr < CF_CONFIG_BASE))
        return dev->cis[addr >> 1];

    switch (addr) {
        case CF_CONFIG_BASE:
            return dev->cor;
        case CF_CONFIG_BASE + 2:
            return dev->ccsr | (dev->prr ? 0x80 : 0) | (dev->intr ? 2 : 0);
        case CF_CONFIG_BASE + 4:
            {
                const uint8_t status = ide_cf_read(dev->ide, 14);
                return dev->prr | (((status != 0xff) && (status & 0x80)) ? 0x0c : 0x0e);
            }
        case CF_CONFIG_BASE + 6:
            return dev->copy;
        default:
            return 0xff;
    }
}

static void
cf_attr_write(uint32_t addr, uint8_t val, void *priv)
{
    cf_card_t *dev = priv;

    switch (addr) {
        case CF_CONFIG_BASE:
            if (val & 0x80)
                cf_reset(dev);
            dev->cor = val;
            cf_irq_update(dev);
            break;
        case CF_CONFIG_BASE + 2:
            dev->ccsr = val & 0x64;
            break;
        case CF_CONFIG_BASE + 4:
            {
                const uint8_t mask = (val & 3) << 4;
                dev->prr           = (dev->prr & ~mask) | (val & mask);
                break;
            }
        case CF_CONFIG_BASE + 6:
            dev->copy = val & 0x10;
            break;
        default:
            break;
    }
}

static int
cf_io_reg(const cf_card_t *dev, uint16_t port)
{
    switch (dev->cor & 0xbf) {
        case 1:
            return port & 0x0f;
        case 2:
        case 3:
            {
                const uint16_t base = (dev->cor & 0x3f) == 2 ? 0x1f0 : 0x170;

                if ((port & 0x3f8) == base)
                    return port & 7;
                if ((port & 0x3fe) == (base + 0x206))
                    return 14 + (port & 1);
                break;
            }
        default:
            break;
    }
    return -1;
}

static int
cf_mem_reg(const cf_card_t *dev, uint32_t addr)
{
    if (dev->cor & 0xbf)
        return -1;
    if (addr < 16)
        return addr;
    if ((addr >= 0x400) && (addr < 0x800))
        return 8 + (addr & 1);
    return -1;
}

static uint8_t
cf_read_reg(cf_card_t *dev, int reg)
{
    const uint8_t val = ide_cf_read(dev->ide, reg);

    if (val == 0xff || !dev->copy)
        return val;
    if (reg == 6 && !(ide_cf_read(dev->ide, 14) & 0x80))
        return val ^ 0x10;
    if (reg == 15 && !(ide_cf_read(dev->ide, 14) & 0x80))
        return val ^ 3;
    return val;
}

static void
cf_write_reg(cf_card_t *dev, int reg, uint8_t val)
{
    ide_cf_write(dev->ide, reg, reg == 6 ? val ^ dev->copy : val);
}

static uint16_t
cf_read_word(cf_card_t *dev, int reg, int next)
{
    if ((reg == 0) || (reg == 8) || (reg == 9))
        return ide_cf_read_data(dev->ide);

    const uint8_t lo = cf_read_reg(dev, reg);
    return lo | (cf_read_reg(dev, next) << 8);
}

static void
cf_write_word(cf_card_t *dev, int reg, int next, uint16_t val)
{
    if ((reg == 0) || (reg == 8) || (reg == 9)) {
        ide_cf_write_data(dev->ide, val);
        return;
    }

    cf_write_reg(dev, reg, val & 0xff);
    cf_write_reg(dev, next, val >> 8);
}

static uint8_t
cf_io_read(uint16_t port, void *priv)
{
    return cf_read_reg(priv, cf_io_reg(priv, port));
}

static void
cf_io_write(uint16_t port, uint8_t val, void *priv)
{
    cf_write_reg(priv, cf_io_reg(priv, port), val);
}

static uint16_t
cf_io_readw(uint16_t port, void *priv)
{
    return cf_read_word(priv, cf_io_reg(priv, port), cf_io_reg(priv, port + 1));
}

static void
cf_io_writew(uint16_t port, uint16_t val, void *priv)
{
    cf_write_word(priv, cf_io_reg(priv, port), cf_io_reg(priv, port + 1), val);
}

static uint8_t
cf_mem_read(uint32_t addr, void *priv)
{
    return cf_read_reg(priv, cf_mem_reg(priv, addr));
}

static void
cf_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    cf_write_reg(priv, cf_mem_reg(priv, addr), val);
}

static uint16_t
cf_mem_readw(uint32_t addr, void *priv)
{
    return cf_read_word(priv, cf_mem_reg(priv, addr), cf_mem_reg(priv, addr + 1));
}

static void
cf_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    cf_write_word(priv, cf_mem_reg(priv, addr), cf_mem_reg(priv, addr + 1), val);
}

static void *
cf_init(const device_t *info)
{
    const int  socket = device_get_instance() - 1;
    int        wp;
    cf_card_t *dev;

    const char *fn = pcmcia_cf_target_image(socket, &wp);
    if (!fn[0])
        return NULL;

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;
    dev->socket = socket;
    dev->ide = ide_cf_open(fn, wp, cf_interrupt, dev);
    if (!dev->ide) {
        pclog("CompactFlash: cannot use image \"%s\"\n", fn);
        free(dev);
        return NULL;
    }

    dev->card = (pccard_t) {
        .name          = info->name,
        .attr_read     = cf_attr_read,
        .attr_write    = cf_attr_write,
        .common_read   = cf_mem_read,
        .common_write  = cf_mem_write,
        .io_read       = cf_io_read,
        .io_readw      = cf_io_readw,
        .io_write      = cf_io_write,
        .io_writew     = cf_io_writew,
        .reset         = cf_reset,
        .priv          = dev,
        .common_readw  = cf_mem_readw,
        .common_writew = cf_mem_writew
    };

    cf_build_cis(dev);
    pcmcia_insert(socket, &dev->card);
    return dev;
}

static void
cf_close(void *priv)
{
    cf_card_t *dev = priv;

    pcmcia_insert(dev->socket, NULL);
    ide_cf_close(dev->ide);
    free(dev);
}

const device_t pccard_cf_device = {
    .name          = "CompactFlash",
    .internal_name = "compactflash",
    .init          = cf_init,
    .close         = cf_close
};
