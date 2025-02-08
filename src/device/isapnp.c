/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of ISA Plug and Play.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2021 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/isapnp.h>
#include <86box/plat_unused.h>

#define CHECK_CURRENT_LD()                                  \
    if (!ld) {                                              \
        isapnp_log("ISAPnP: No logical device selected\n"); \
        goto vendor_defined;                                \
    }

#define CHECK_CURRENT_CARD()                             \
    if (!card) {                                         \
        isapnp_log("ISAPnP: No card in CONFIG state\n"); \
        break;                                           \
    }

const uint8_t         isapnp_init_key[32] = { 0x6A, 0xB5, 0xDA, 0xED, 0xF6, 0xFB, 0x7D, 0xBE,
                                              0xDF, 0x6F, 0x37, 0x1B, 0x0D, 0x86, 0xC3, 0x61,
                                              0xB0, 0x58, 0x2C, 0x16, 0x8B, 0x45, 0xA2, 0xD1,
                                              0xE8, 0x74, 0x3A, 0x9D, 0xCE, 0xE7, 0x73, 0x39 };
static const device_t isapnp_device;

#ifdef ENABLE_ISAPNP_LOG
int isapnp_do_log = ENABLE_ISAPNP_LOG;

static void
isapnp_log(const char *fmt, ...)
{
    va_list ap;

    if (isapnp_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define isapnp_log(fmt, ...)
#endif

enum {
    PNP_STATE_WAIT_FOR_KEY = 0,
    PNP_STATE_CONFIG,
    PNP_STATE_ISOLATION,
    PNP_STATE_SLEEP
};

typedef struct _isapnp_device_ {
    uint8_t                       number;
    uint8_t                       regs[256];
    uint8_t                       mem_upperlimit;
    uint8_t                       irq_types;
    uint8_t                       io_16bit;
    uint8_t                       io_len[8];
    const isapnp_device_config_t *defaults;

    struct _isapnp_device_ *next;
} isapnp_device_t;

typedef struct _isapnp_card_ {
    uint8_t  enable;
    uint8_t  state;
    uint8_t  csn;
    uint8_t  ld;
    uint8_t  id_checksum;
    uint8_t  serial_read;
    uint8_t  serial_read_pair;
    uint8_t  serial_read_pos;
    uint8_t *rom;
    uint16_t rom_pos;
    uint16_t rom_size;
    void    *priv;

    /* ISAPnP memory and I/O addresses are awkwardly big endian, so we populate this
       structure whenever something on some device changes, and pass it on instead. */
    isapnp_device_config_t config;
    void    (*config_changed)(uint8_t ld, isapnp_device_config_t *config, void *priv);
    void    (*csn_changed)(uint8_t csn, void *priv);
    uint8_t (*read_vendor_reg)(uint8_t ld, uint8_t reg, void *priv);
    void    (*write_vendor_reg)(uint8_t ld, uint8_t reg, uint8_t val, void *priv);

    isapnp_device_t      *first_ld;
    struct _isapnp_card_ *next;
} isapnp_card_t;

typedef struct {
    uint8_t  in_isolation;
    uint8_t  reg;
    uint8_t  key_pos : 5;
    uint16_t read_data_addr;

    isapnp_card_t   *first_card;
    isapnp_card_t   *isolated_card;
    isapnp_card_t   *current_ld_card;
    isapnp_device_t *current_ld;
} isapnp_t;

static isapnp_device_t *
isapnp_create_ld(isapnp_card_t *card)
{
    /* Allocate logical device. */
    isapnp_device_t *ld = calloc(1, sizeof(isapnp_device_t));

    /* Add to the end of the card's logical device list. */
    isapnp_device_t *prev_ld = card->first_ld;
    if (prev_ld) {
        while (prev_ld->next)
            prev_ld = prev_ld->next;
        prev_ld->next = ld;
    } else {
        card->first_ld = ld;
    }

    return ld;
}

static void
isapnp_device_config_changed(isapnp_card_t *card, isapnp_device_t *ld)
{
    /* Ignore card if it hasn't signed up for configuration changes. */
    if ((card == NULL) || !card->config_changed)
        return;

    /* Populate config structure, performing endianness conversion as needed. */
    card->config.activate = ld->regs[0x30] & 0x01;
    uint8_t reg_base;
    for (uint8_t i = 0; i < 4; i++) {
        reg_base                 = 0x40 + (8 * i);
        card->config.mem[i].base = (ld->regs[reg_base] << 16) | (ld->regs[reg_base + 1] << 8);
        card->config.mem[i].size = (ld->regs[reg_base + 3] << 16) | (ld->regs[reg_base + 4] << 8);
        if (ld->regs[reg_base + 2] & 0x01) /* upper limit */
            card->config.mem[i].size -= card->config.mem[i].base;
        else
            card->config.mem[i].size = (card->config.mem[i].size | 0xff) ^ 0xffffffff;
    }
    for (uint8_t i = 0; i < 4; i++) {
        reg_base                   = (i == 0) ? 0x76 : (0x80 + (16 * i));
        card->config.mem32[i].base = (ld->regs[reg_base] << 24) | (ld->regs[reg_base + 1] << 16) | (ld->regs[reg_base + 2] << 8) | ld->regs[reg_base + 3];
        card->config.mem32[i].size = (ld->regs[reg_base + 5] << 24) | (ld->regs[reg_base + 6] << 16) | (ld->regs[reg_base + 7] << 8) | ld->regs[reg_base + 8];
        if (ld->regs[reg_base + 4] & 0x01) /* upper limit */
            card->config.mem32[i].size -= card->config.mem32[i].base;
    }
    for (uint8_t i = 0; i < 8; i++) {
        reg_base = 0x60 + (2 * i);
        if (ld->regs[0x31] & 0x02)
            card->config.io[i].base = 0; /* let us handle I/O range check reads */
        else
            card->config.io[i].base = (ld->regs[reg_base] << 8) | ld->regs[reg_base + 1];
    }
    for (uint8_t i = 0; i < 2; i++) {
        reg_base                  = 0x70 + (2 * i);
        card->config.irq[i].irq   = ld->regs[reg_base];
        card->config.irq[i].level = ld->regs[reg_base + 1] & 0x02;
        card->config.irq[i].type  = ld->regs[reg_base + 1] & 0x01;
    }
    for (uint8_t i = 0; i < 2; i++) {
        reg_base                = 0x74 + i;
        card->config.dma[i].dma = ld->regs[reg_base];
    }

    /* Signal the configuration change. */
    card->config_changed(ld->number, &card->config, card->priv);
}

static void
isapnp_reset_ld_config(isapnp_device_t *ld)
{
    /* Do nothing if there's no default configuration for this device. */
    const isapnp_device_config_t *config = ld->defaults;
    if (!config)
        return;

    /* Populate configuration registers. */
    ld->regs[0x30] = !!config->activate;
    uint8_t  reg_base;
    uint32_t size;
    for (uint8_t i = 0; i < 4; i++) {
        reg_base               = 0x40 + (8 * i);
        ld->regs[reg_base]     = config->mem[i].base >> 16;
        ld->regs[reg_base + 1] = config->mem[i].base >> 8;
        size                   = config->mem[i].size;
        if (ld->regs[reg_base + 2] & 0x01) /* upper limit */
            size += config->mem[i].base;
        ld->regs[reg_base + 3] = size >> 16;
        ld->regs[reg_base + 4] = size >> 8;
    }
    for (uint8_t i = 0; i < 4; i++) {
        reg_base               = (i == 0) ? 0x76 : (0x80 + (16 * i));
        ld->regs[reg_base]     = config->mem32[i].base >> 24;
        ld->regs[reg_base + 1] = config->mem32[i].base >> 16;
        ld->regs[reg_base + 2] = config->mem32[i].base >> 8;
        ld->regs[reg_base + 3] = config->mem32[i].base;
        size                   = config->mem32[i].size;
        if (ld->regs[reg_base + 4] & 0x01) /* upper limit */
            size += config->mem32[i].base;
        ld->regs[reg_base + 5] = size >> 24;
        ld->regs[reg_base + 6] = size >> 16;
        ld->regs[reg_base + 7] = size >> 8;
        ld->regs[reg_base + 8] = size;
    }
    for (uint8_t i = 0; i < 8; i++) {
        reg_base               = 0x60 + (2 * i);
        ld->regs[reg_base]     = config->io[i].base >> 8;
        ld->regs[reg_base + 1] = config->io[i].base;
    }
    for (uint8_t i = 0; i < 2; i++) {
        reg_base               = 0x70 + (2 * i);
        ld->regs[reg_base]     = config->irq[i].irq;
        ld->regs[reg_base + 1] = (!!config->irq[i].level << 1) | !!config->irq[i].type;
    }
    for (uint8_t i = 0; i < 2; i++) {
        reg_base           = 0x74 + i;
        ld->regs[reg_base] = config->dma[i].dma;
    }
}

static void
isapnp_reset_ld_regs(isapnp_device_t *ld)
{
    memset(ld->regs, 0, sizeof(ld->regs));

    /* DMA disable uses a non-zero value. */
    ld->regs[0x74] = ld->regs[0x75] = ISAPNP_DMA_DISABLED;

    /* Set the upper limit bit on memory ranges which require it. */
    for (uint8_t i = 0; i < 4; i++)
        ld->regs[0x42 + (8 * i)] |= !!(ld->mem_upperlimit & (1 << i));
    ld->regs[0x7a] |= !!(ld->mem_upperlimit & (1 << 4));
    for (uint8_t i = 1; i < 4; i++)
        ld->regs[0x84 + (16 * i)] |= !!(ld->mem_upperlimit & (1 << (4 + i)));

    /* Set the default IRQ type bits. */
    for (uint8_t i = 0; i < 2; i++) {
        if (ld->irq_types & (0x1 << (4 * i)))
            ld->regs[0x70 + (2 * i)] = 0x02;
        else if (ld->irq_types & (0x2 << (4 * i)))
            ld->regs[0x70 + (2 * i)] = 0x00;
        else if (ld->irq_types & (0x4 << (4 * i)))
            ld->regs[0x70 + (2 * i)] = 0x03;
        else if (ld->irq_types & (0x8 << (4 * i)))
            ld->regs[0x70 + (2 * i)] = 0x01;
    }

    /* Reset configuration registers to match the default configuration. */
    isapnp_reset_ld_config(ld);
}

static uint8_t
isapnp_read_rangecheck(UNUSED(uint16_t addr), void *priv)
{
    const isapnp_device_t *dev = (isapnp_device_t *) priv;

    return (dev->regs[0x31] & 0x01) ? 0x55 : 0xaa;
}

static uint8_t
isapnp_read_common(isapnp_t *dev, isapnp_card_t *card, isapnp_device_t *ld, uint8_t reg)
{
    uint8_t ret = 0xff;
    uint8_t bit;
    uint8_t next_shift;

    switch (reg) {
        case 0x01: /* Serial Isolation */
            card = dev->first_card;
            while (card) {
                if (card->enable && card->rom && (card->state == PNP_STATE_ISOLATION))
                    break;
                card = card->next;
            }
            dev->isolated_card = card;

            if (card) {
                if (card->serial_read_pair) { /* second byte (aa/00) */
                    card->serial_read <<= 1;
                    if (!card->serial_read_pos)
                        card->rom_pos = 0x09;
                } else {                              /* first byte (55/00) */
                    if (card->serial_read_pos < 64) { /* reading 64-bit vendor/serial */
                        bit        = (card->rom[card->serial_read_pos >> 3] >> (card->serial_read_pos & 0x7)) & 0x01;
                        next_shift = (!!(card->id_checksum & 0x02) ^ !!(card->id_checksum & 0x01) ^ bit) & 0x01;
                        card->id_checksum >>= 1;
                        card->id_checksum |= (next_shift << 7);
                    } else {                             /* reading 8-bit checksum */
                        if (card->serial_read_pos == 64) /* populate ID checksum in ROM */
                            card->rom[0x08] = card->id_checksum;
                        bit = (card->id_checksum >> (card->serial_read_pos & 0x7)) & 0x01;
                    }
                    isapnp_log("ISAPnP: Read bit %d of byte %02X (%02X) = %d\n", card->serial_read_pos & 0x7, card->serial_read_pos >> 3, card->rom[card->serial_read_pos >> 3], bit);
                    card->serial_read     = bit ? 0x55 : 0x00;
                    card->serial_read_pos = (card->serial_read_pos + 1) % 72;
                }
                card->serial_read_pair ^= 1;
                ret = card->serial_read;
            }

            break;

        case 0x04: /* Resource Data */
            CHECK_CURRENT_CARD();

            isapnp_log("ISAPnP: Read resource data index %02X (%02X) from CSN %02X\n", card->rom_pos, card->rom[card->rom_pos], card->csn);
            if (card->rom_pos >= card->rom_size)
                ret = 0xff;
            else
                ret = card->rom[card->rom_pos++];

            break;

        case 0x05: /* Status */
            ret = 0x00;
            if (dev->in_isolation)
                ret = 0x01;
            CHECK_CURRENT_CARD();

            isapnp_log("ISAPnP: Query status for CSN %02X\n", card->csn);
            ret = 0x01;

            break;

        case 0x06: /* Card Select Number */
            ret = 0x00;
            CHECK_CURRENT_CARD();

            isapnp_log("ISAPnP: Query CSN %02X\n", card->csn);
            ret = card->csn;

            break;

        case 0x07: /* Logical Device Number */
            ret = 0x00;
            CHECK_CURRENT_LD();

            isapnp_log("ISAPnP: Query LDN for CSN %02X device %02X\n", card->csn, ld->number);
            ret = ld->number;

            break;

        case 0x20 ... 0x2f:
        case 0x38 ... 0x3f:
        case 0xa9 ... 0xff:
vendor_defined:
            CHECK_CURRENT_CARD();

            isapnp_log("ISAPnP: Read vendor-defined register %02X from CSN %02X device %02X\n", reg, card->csn, ld ? ld->number : -1);

            if (card->read_vendor_reg)
                ret = card->read_vendor_reg(ld ? ld->number : -1, reg, card->priv);
            break;

        default:
            if (reg >= 0x30) {
                CHECK_CURRENT_LD();
                isapnp_log("ISAPnP: Read register %02X from CSN %02X device %02X\n", reg, card->csn, ld->number);
                ret = ld->regs[reg];
            }
            break;
    }

    isapnp_log("ISAPnP: read_common(%02X) = %02X\n", reg, ret);

    return ret;
}

static uint8_t
isapnp_read_data(UNUSED(uint16_t addr), void *priv)
{
    isapnp_t      *dev  = (isapnp_t *) priv;
    isapnp_card_t *card = dev->first_card;
    while (card) {
        if (card->enable && (card->state == PNP_STATE_CONFIG))
            break;
        card = card->next;
    }

    isapnp_log("ISAPnP: read_data() => ");
    return isapnp_read_common(dev, card, dev->current_ld, dev->reg);
}

static void
isapnp_set_read_data(uint16_t addr, isapnp_t *dev)
{
    /* Remove existing READ_DATA port if set. */
    if (dev->read_data_addr) {
        io_removehandler(dev->read_data_addr, 1, isapnp_read_data, NULL, NULL, NULL, NULL, NULL, dev);
        dev->read_data_addr = 0;
    }

    /* Set new READ_DATA port if within range. */
    if ((addr >= 0x203) && (addr <= 0x3ff)) {
        dev->read_data_addr = addr;
        io_sethandler(dev->read_data_addr, 1, isapnp_read_data, NULL, NULL, NULL, NULL, NULL, dev);
    }
}

static void
isapnp_write_addr(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    isapnp_t      *dev  = (isapnp_t *) priv;
    isapnp_card_t *card = dev->first_card;

    isapnp_log("ISAPnP: write_addr(%02X)\n", val);

    if (!card) /* don't do anything if we have no PnP cards */
        return;

    dev->reg = val;

    if (card->state == PNP_STATE_WAIT_FOR_KEY) { /* checking only the first card should be fine */
        /* Check written value against LFSR key. */
        if (val == isapnp_init_key[dev->key_pos]) {
            dev->key_pos++;
            if (!dev->key_pos) {
                isapnp_log("ISAPnP: Key unlocked, putting cards to SLEEP\n");
                while (card) {
                    if (card->enable && (card->enable != ISAPNP_CARD_NO_KEY) && (card->state == PNP_STATE_WAIT_FOR_KEY))
                        card->state = PNP_STATE_SLEEP;
                    card = card->next;
                }
            }
        } else {
            dev->key_pos = 0;
        }
    }
}

static void
isapnp_write_common(isapnp_t *dev, isapnp_card_t *card, isapnp_device_t *ld, uint8_t reg, uint8_t val)
{
    uint16_t io_addr;
    uint16_t reset_cards = 0;

    isapnp_log("ISAPnP: write_common(%02X, %02X)\n", reg, val);

    switch (reg) {
        case 0x00: /* Set RD_DATA Port */
            isapnp_set_read_data((val << 2) | 3, dev);
            isapnp_log("ISAPnP: Read data port set to %04X\n", dev->read_data_addr);
            break;

        case 0x02: /* Config Control */
            if (val & 0x01) {
                isapnp_log("ISAPnP: Reset\n");

                card = dev->first_card;
                while (card) {
                    ld = card->first_ld;
                    while (ld) {
                        if (card->state != PNP_STATE_WAIT_FOR_KEY) {
                            isapnp_reset_ld_regs(ld);
                            isapnp_device_config_changed(card, ld);
                            reset_cards++;
                        }
                        ld = ld->next;
                    }
                    card = card->next;
                }

                if (reset_cards != 0) {
                    dev->current_ld      = NULL;
                    dev->current_ld_card = NULL;
                    dev->isolated_card   = NULL;
                }
            }
            if (val & 0x02) {
                isapnp_log("ISAPnP: Return to WAIT_FOR_KEY\n");
                card = dev->first_card;
                while (card) {
                    card->state = PNP_STATE_WAIT_FOR_KEY;
                    card        = card->next;
                }
            }
            if (val & 0x04) {
                isapnp_log("ISAPnP: Reset CSN\n");
                card = dev->first_card;
                while (card) {
                    isapnp_set_csn(card, 0);
                    card = card->next;
                }
            }
            break;

        case 0x03: /* Wake[CSN] */
            isapnp_log("ISAPnP: Wake[%02X]\n", val);
            card = dev->first_card;
            if (val == 0)
                dev->in_isolation |= 1;
            while (card) {
                if (card->csn == val) {
                    card->rom_pos     = 0;
                    card->id_checksum = isapnp_init_key[0];
                    if (card->state == PNP_STATE_SLEEP)
                        card->state = (val == 0) ? PNP_STATE_ISOLATION : PNP_STATE_CONFIG;
                } else
                    card->state = PNP_STATE_SLEEP;

                card = card->next;
            }
            break;

        case 0x06: /* Card Select Number */
            if (dev->isolated_card) {
                isapnp_log("ISAPnP: Set CSN %02X\n", val);
                isapnp_set_csn(dev->isolated_card, val);
                dev->isolated_card->state = PNP_STATE_CONFIG;
                dev->isolated_card        = NULL;
                dev->in_isolation         = 0;
            } else {
                isapnp_log("ISAPnP: Set CSN %02X but no card is isolated\n", val);
            }
            break;

        case 0x07: /* Logical Device Number */
            CHECK_CURRENT_CARD();

            card->ld = val;
            ld = card->first_ld;
            while (ld) {
                if (ld->number == val) {
                    isapnp_log("ISAPnP: Select CSN %02X device %02X\n", card->csn, val);
                    dev->current_ld_card = card;
                    dev->current_ld      = ld;
                    break;
                }
                ld = ld->next;
            }

            if (!ld) {
                isapnp_log("ISAPnP: CSN %02X has no device %02X, creating one\n", card->csn, val);
                dev->current_ld_card    = card;
                dev->current_ld         = isapnp_create_ld(card);
                dev->current_ld->number = val;
            }

            break;

        case 0x30: /* Activate */
            CHECK_CURRENT_LD();

            isapnp_log("ISAPnP: %sctivate CSN %02X device %02X\n", (val & 0x01) ? "A" : "Dea", card->csn, ld->number);

            ld->regs[reg] = val & 0x01;
            isapnp_device_config_changed(card, ld);

            break;

        case 0x31: /* I/O Range Check */
            CHECK_CURRENT_LD();

            for (uint8_t i = 0; i < 8; i++) {
                if (!ld->io_len[i])
                    continue;

                io_addr = (ld->regs[0x60 + (2 * i)] << 8) | ld->regs[0x61 + (2 * i)];
                if (ld->regs[reg] & 0x02)
                    io_removehandler(io_addr, ld->io_len[i], isapnp_read_rangecheck, NULL, NULL, NULL, NULL, NULL, ld);
                if (val & 0x02)
                    io_sethandler(io_addr, ld->io_len[i], isapnp_read_rangecheck, NULL, NULL, NULL, NULL, NULL, ld);
            }

            ld->regs[reg] = val & 0x03;
            isapnp_device_config_changed(card, ld);

            break;

        case 0x20 ... 0x2f:
        case 0x38 ... 0x3f:
        case 0xa9 ... 0xff:
vendor_defined:
            CHECK_CURRENT_CARD();

            isapnp_log("ISAPnP: Write %02X to vendor-defined register %02X on CSN %02X device %02X\n", val, reg, card->csn, ld ? ld->number : -1);

            if (card->write_vendor_reg)
                card->write_vendor_reg(ld ? ld->number : -1, reg, val, card->priv);
            break;

        default:
            if (reg >= 0x40) {
                CHECK_CURRENT_LD();
                isapnp_log("ISAPnP: Write %02X to register %02X on CSN %02X device %02X\n", val, reg, card->csn, ld->number);

                switch (reg) {
                    case 0x42:
                    case 0x4a:
                    case 0x52:
                    case 0x5a:
                    case 0x7a:
                    case 0x84:
                    case 0x94:
                    case 0xa4:
                        /* Read-only memory range length / upper limit bit. */
                        val = (val & 0xfe) | (ld->regs[reg] & 0x01);
                        break;

                    case 0x60:
                    case 0x62:
                    case 0x64:
                    case 0x66:
                    case 0x68:
                    case 0x6a:
                    case 0x6c:
                    case 0x6e:
                        /* Discard upper address bits if this I/O range can only decode 10-bit. */
                        if (!(ld->io_16bit & (1 << ((reg >> 1) & 0x07))))
                            val &= 0x03;
                        break;

                    case 0x71:
                    case 0x73:
                        /* Limit IRQ types to supported ones. */
                        if ((val & 0x01) && !(ld->irq_types & ((reg == 0x71) ? 0x0c : 0xc0))) /* level, not supported = force edge */
                            val &= ~0x01;
                        else if (!(val & 0x01) && !(ld->irq_types & ((reg == 0x71) ? 0x03 : 0x30))) /* edge, not supported = force level */
                            val |= 0x01;

                        if ((val & 0x02) && !(ld->irq_types & ((reg == 0x71) ? 0x05 : 0x50))) /* high, not supported = force low */
                            val &= ~0x02;
                        else if (!(val & 0x02) && !(ld->irq_types & ((reg == 0x71) ? 0x0a : 0xa0))) /* low, not supported = force high */
                            val |= 0x02;

                        break;

                    default:
                        break;
                }

                ld->regs[reg] = val;
                isapnp_device_config_changed(card, ld);
            }
            break;
    }
}

static void
isapnp_write_data(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    isapnp_t      *dev  = (isapnp_t *) priv;
    isapnp_card_t *card = NULL;
    if (!card) {
        card = dev->first_card;
        while (card) {
            if (card->enable && (card->state == PNP_STATE_CONFIG))
                break;
            card = card->next;
        }
    }

    isapnp_log("ISAPnP: write_data(%02X) => ", val);
    isapnp_write_common(dev, card, dev->current_ld, dev->reg, val);
}

static void *
isapnp_init(UNUSED(const device_t *info))
{
    isapnp_t *dev = (isapnp_t *) calloc(1, sizeof(isapnp_t));
    memset(dev, 0, sizeof(isapnp_t));

    io_sethandler(0x279, 1, NULL, NULL, NULL, isapnp_write_addr, NULL, NULL, dev);
    io_sethandler(0xa79, 1, NULL, NULL, NULL, isapnp_write_data, NULL, NULL, dev);

    return dev;
}

static void
isapnp_close(void *priv)
{
    isapnp_t        *dev  = (isapnp_t *) priv;
    isapnp_card_t   *card = dev->first_card;
    isapnp_card_t   *next_card;
    isapnp_device_t *ld;
    isapnp_device_t *next_ld;

    while (card) {
        ld = card->first_ld;
        while (ld) {
            next_ld = ld->next;
            free(ld);
            ld = next_ld;
        }

        next_card = card->next;
        free(card);
        card = next_card;
    }

    io_removehandler(0x279, 1, NULL, NULL, NULL, isapnp_write_addr, NULL, NULL, dev);
    io_removehandler(0xa79, 1, NULL, NULL, NULL, isapnp_write_data, NULL, NULL, dev);

    free(dev);
}

void *
isapnp_add_card(uint8_t *rom, uint16_t rom_size,
                void (*config_changed)(uint8_t ld, isapnp_device_config_t *config, void *priv),
                void (*csn_changed)(uint8_t csn, void *priv),
                uint8_t (*read_vendor_reg)(uint8_t ld, uint8_t reg, void *priv),
                void (*write_vendor_reg)(uint8_t ld, uint8_t reg, uint8_t val, void *priv),
                void *priv)
{
    isapnp_t *dev = (isapnp_t *) device_get_priv(&isapnp_device);
    if (!dev)
        dev = (isapnp_t *) device_add(&isapnp_device);

    isapnp_card_t *card = (isapnp_card_t *) calloc(1, sizeof(isapnp_card_t));

    card->enable           = 1;
    card->priv             = priv;
    card->config_changed   = config_changed;
    card->csn_changed      = csn_changed;
    card->read_vendor_reg  = read_vendor_reg;
    card->write_vendor_reg = write_vendor_reg;

    if (!dev->first_card) {
        dev->first_card = card;
    } else {
        isapnp_card_t *prev_card = dev->first_card;
        while (prev_card->next)
            prev_card = prev_card->next;
        prev_card->next = card;
    }

    if (rom && rom_size)
        isapnp_update_card_rom(card, rom, rom_size);

    return card;
}

void
isapnp_update_card_rom(void *priv, uint8_t *rom, uint16_t rom_size)
{
    isapnp_card_t *card = (isapnp_card_t *) priv;
    card->rom           = rom;
    card->rom_size      = rom_size;

    /* Parse resources in ROM to allocate logical devices,
       and determine the state of read-only register bits. */
#ifdef ENABLE_ISAPNP_LOG
    uint16_t vendor = (card->rom[0] << 8) | card->rom[1];
    isapnp_log("ISAPnP: Parsing ROM resources for card %c%c%c%02X%02X (serial %08X)\n", '@' + ((vendor >> 10) & 0x1f), '@' + ((vendor >> 5) & 0x1f), '@' + (vendor & 0x1f), card->rom[2], card->rom[3], (card->rom[7] << 24) | (card->rom[6] << 16) | (card->rom[5] << 8) | card->rom[4]);
    const char *df_priority[]  = { "good", "acceptable", "sub-optimal", "unknown priority" };
    const char *mem_control[]  = { "8-bit", "16-bit", "8/16-bit", "32-bit" };
    const char *dma_transfer[] = { "8-bit", "8/16-bit", "16-bit", "Reserved" };
    const char *dma_speed[]    = { "compatibility", "Type A", "Type B", "Type F" };
#endif
    uint16_t         i        = 9;
    uint8_t          existing = 0;
    uint8_t          ldn      = 0;
    uint8_t          res;
    uint8_t          in_df           = 0;
    uint8_t          irq             = 0;
    uint8_t          dma             = 0;
    uint8_t          io              = 0;
    uint8_t          mem_range       = 0;
    uint8_t          mem_range_32    = 0;
    uint8_t          irq_df          = 0;
    uint8_t          dma_df          = 0;
    uint8_t          io_df           = 0;
    uint8_t          mem_range_df    = 0;
    uint8_t          mem_range_32_df = 0;
    uint32_t         len;
    isapnp_device_t *ld = NULL;

    /* Check if this is an existing card which already has logical devices.
       Any new logical devices will be added to the list after existing ones.
       Removed LDs are not flushed as we may end up with an invalid ROM. */
    existing = !!card->first_ld;

    /* Iterate through ROM resources. */
    while (i < card->rom_size) {
        if (card->rom[i] & 0x80) { /* large resource */
            res = card->rom[i] & 0x7f;
            len = (card->rom[i + 2] << 8) | card->rom[i + 1];

            switch (res) {
                case 0x01: /* memory range */
                case 0x05: /* 32-bit memory range */
                    if (res == 0x01) {
                        if (!ld) {
                            isapnp_log("ISAPnP: >>%s Memory descriptor with no logical device\n", in_df ? ">" : "");
                            break;
                        }

                        if (mem_range > 3) {
                            isapnp_log("ISAPnP: >>%s Memory descriptor overflow (%d)\n", in_df ? ">" : "", mem_range++);
                            break;
                        }

                        isapnp_log("ISAPnP: >>%s Memory range %d with %d bytes at %06X-%06X to %06X-%06X, align %d",
                                   /* %s   */ in_df ? ">" : "",
                                   /* %d   */ mem_range,
                                   /* %d   */ *((uint16_t *) &card->rom[i + 8]),
                                   /* %06X */ *((uint16_t *) &card->rom[i + 4]) << 8,
                                   /* %06X */ ((card->rom[i + 3] & 0x4) ?
                                              /* High address. */
                                              (*((uint16_t *) &card->rom[i + 10]) << 8) :
                                              /* Range. */
                                              (*((uint16_t *) &card->rom[i + 4]) << 8)) +
                                              (*((uint16_t *) &card->rom[i + 10]) << 8),
                                   /* %06X */ *((uint16_t *) &card->rom[i + 6]) << 8,
                                   /* %06X */ ((card->rom[i + 3] & 0x4) ?
                                              /* High address. */
                                              (*((uint16_t *) &card->rom[i + 10]) << 8) :
                                              /* Range. */
                                              (*((uint16_t *) &card->rom[i + 6]) << 8)) +
                                              (*((uint16_t *) &card->rom[i + 10]) << 8),
                                   /* %d   */ *((uint16_t *) &card->rom[i + 8]));
                        res = 1 << mem_range;
                        mem_range++;
                    } else {
                        if (!ld) {
                            isapnp_log("ISAPnP: >>%s 32-bit memory descriptor with no logical device\n", in_df ? ">" : "");
                            break;
                        }

                        if (mem_range_32 > 3) {
                            isapnp_log("ISAPnP: >>%s 32-bit memory descriptor overflow (%d)\n", in_df ? ">" : "", mem_range_32++);
                            break;
                        }

                        isapnp_log("ISAPnP: >>%s 32-bit memory range %d with %d bytes at %08X-%08X, align %d",
                                   /* %s   */ in_df ? ">" : "",
                                   /* %d   */ mem_range_32,
                                   /* %d   */ *((uint32_t *) &card->rom[i + 12]),
                                   /* %08X */ *((uint32_t *) &card->rom[i + 4]),
                                   /* %08X */ ((card->rom[i + 3] & 0x4) ?
                                              /* High address. */
                                              *((uint32_t *) &card->rom[i + 16]) :
                                              /* Range. */
                                              *((uint32_t *) &card->rom[i + 4])) +
                                              *((uint32_t *) &card->rom[i + 16]),
                                   /* %08X */ *((uint32_t *) &card->rom[i + 8]),
                                   /* %08X */ ((card->rom[i + 3] & 0x4) ?
                                              /* High address. */
                                              *((uint32_t *) &card->rom[i + 16]) :
                                              /* Range. */
                                              *((uint32_t *) &card->rom[i + 8])) +
                                              *((uint32_t *) &card->rom[i + 16]),
                                   /* %d   */ *((uint32_t *) &card->rom[i + 12]));
                        res = 1 << (4 + mem_range_32);
                        mem_range_32++;
                    }

#ifdef ENABLE_ISAPNP_LOG
                    isapnp_log(" bytes, %swritable, %sread cacheable, %s, %s, %sshadowable, %sexpansion ROM\n",
                               (card->rom[i + 3] & 0x01) ? "not " : "",
                               (card->rom[i + 3] & 0x02) ? "not " : "",
                               (card->rom[i + 3] & 0x04) ? "upper limit" : "range length",
                               mem_control[(card->rom[i + 3] >> 3) & 0x03],
                               (card->rom[i + 3] & 0x20) ? "not " : "",
                               (card->rom[i + 3] & 0x40) ? "not " : "");
#endif

                    if (card->rom[i + 3] & 0x4)
                        ld->mem_upperlimit |= res;
                    else
                        ld->mem_upperlimit &= ~res;

                    break;

#ifdef ENABLE_ISAPNP_LOG
                case 0x02: /* ANSI identifier */
                    res                    = card->rom[i + 3 + len];
                    card->rom[i + 3 + len] = '\0';
                    isapnp_log("ISAPnP: >%s ANSI identifier: \"%s\"\n", ldn ? ">" : "", &card->rom[i + 3]);
                    card->rom[i + 3 + len] = res;
                    break;
#endif

                default:
                    isapnp_log("ISAPnP: >%s%s Large resource %02X (length %d)\n", ldn ? ">" : "", in_df ? ">" : "", res, (card->rom[i + 2] << 8) | card->rom[i + 1]);
                    break;
            }

            i += 3; /* header */
        } else {    /* small resource */
            res = (card->rom[i] >> 3) & 0x0f;
            len = card->rom[i] & 0x07;

            switch (res) {
#ifdef ENABLE_ISAPNP_LOG
                case 0x01: /* PnP version */
                    isapnp_log("ISAPnP: > PnP version %d.%d, vendor-specific version %02X\n", card->rom[i + 1] >> 4, card->rom[i + 1] & 0x0f, card->rom[i + 2]);
                    break;
#endif

                case 0x02: /* logical device */
#ifdef ENABLE_ISAPNP_LOG
                    vendor = (card->rom[i + 1] << 8) | card->rom[i + 2];
                    isapnp_log("ISAPnP: > Logical device %02X: %c%c%c%02X%02X\n", ldn, '@' + ((vendor >> 10) & 0x1f), '@' + ((vendor >> 5) & 0x1f), '@' + (vendor & 0x1f), card->rom[i + 3], card->rom[i + 4]);
#endif

                    /* We're done with the previous logical device. */
                    if (ld && !existing)
                        isapnp_reset_ld_regs(ld);

                    /* Look for an existing logical device with this number,
                       and create one if none exist. */
                    if (existing) {
                        ld = card->first_ld;
                        while (ld && (ld->number != ldn))
                            ld = ld->next;
                    }
                    if (ld && (ld->number == ldn)) {
                        /* Reset some logical device state. */
                        ld->mem_upperlimit = ld->io_16bit = ld->irq_types = 0;
                        memset(ld->io_len, 0, sizeof(ld->io_len));
                    } else {
                        /* Create logical device. */
                        ld = isapnp_create_ld(card);
                    }

                    /* Set and increment logical device number. */
                    ld->number = ldn++;

                    /* Start the position counts over. */
                    irq = dma = io = mem_range = mem_range_32 = irq_df = dma_df = io_df = mem_range_df = mem_range_32_df = 0;

                    break;

#ifdef ENABLE_ISAPNP_LOG
                case 0x03: /* compatible device ID */
                    if (!ld) {
                        isapnp_log("ISAPnP: >> Compatible device ID with no logical device\n");
                        break;
                    }

                    vendor = (card->rom[i + 1] << 8) | card->rom[i + 2];
                    isapnp_log("ISAPnP: >> Compatible device ID: %c%c%c%02X%02X\n", '@' + ((vendor >> 10) & 0x1f), '@' + ((vendor >> 5) & 0x1f), '@' + (vendor & 0x1f), card->rom[i + 3], card->rom[i + 4]);
                    break;
#endif

                case 0x04: /* IRQ */
                    if (!ld) {
                        isapnp_log("ISAPnP: >>%s IRQ descriptor with no logical device\n", in_df ? ">" : "");
                        break;
                    }

                    if (irq > 1) {
                        isapnp_log("ISAPnP: >>%s IRQ descriptor overflow (%d)\n", in_df ? ">" : "", irq++);
                        break;
                    }

                    if (len == 2)   /* default */
                        res = 0x01; /* high true edge sensitive */
                    else            /* specific */
                        res = card->rom[i + 3] & 0x0f;

                    isapnp_log("ISAPnP: >>%s IRQ index %d with mask %04X, types %01X\n", in_df ? ">" : "", irq, *((uint16_t *) &card->rom[i + 1]), res);

                    ld->irq_types &= ~(0x0f << (4 * irq));
                    ld->irq_types |= res << (4 * irq);

                    irq++;

                    break;

#ifdef ENABLE_ISAPNP_LOG
                case 0x05: /* DMA */
                    isapnp_log("ISAPnP: >>%s DMA index %d with mask %02X, %s, %sbus master, %scount by byte, %scount by word, %s speed\n", in_df ? ">" : "", dma++, card->rom[i + 1],
                               dma_transfer[card->rom[i + 2] & 3],
                               (card->rom[i + 2] & 0x04) ? "" : "not ",
                               (card->rom[i + 2] & 0x08) ? "" : "not ",
                               (card->rom[i + 2] & 0x10) ? "" : "not ",
                               dma_speed[(card->rom[i + 2] >> 5) & 3]);
                    break;
#endif

                case 0x06: /* start dependent function */
                    if (!ld) {
                        isapnp_log("ISAPnP: >> Start dependent function with no logical device\n");
                        break;
                    }

#ifdef ENABLE_ISAPNP_LOG
                    isapnp_log("ISAPnP: >> Start dependent function: %s\n", df_priority[(len < 1) ? 1 : (card->rom[i + 1] & 3)]);
#endif

                    if (in_df) {
                        /* We're in a dependent function and this is the next one starting.
                           Walk positions back to the saved values. */
                        irq          = irq_df;
                        dma          = dma_df;
                        io           = io_df;
                        mem_range    = mem_range_df;
                        mem_range_32 = mem_range_32_df;
                    } else {
                        /* Save current positions to restore at the next DF. */
                        irq_df          = irq;
                        dma_df          = dma;
                        io_df           = io;
                        mem_range_df    = mem_range;
                        mem_range_32_df = mem_range_32;
                        in_df           = 1;
                    }

                    break;

                case 0x07: /* end dependent function */
                    isapnp_log("ISAPnP: >> End dependent function\n");
                    in_df = 0;
                    break;

                case 0x08: /* I/O port */
                    if (!ld) {
                        isapnp_log("ISAPnP: >>%s I/O descriptor with no logical device\n", in_df ? ">" : "");
                        break;
                    }

                    if (io > 7) {
                        isapnp_log("ISAPnP: >>%s I/O descriptor overflow (%d)\n", in_df ? ">" : "", io++);
                        break;
                    }

                    isapnp_log("ISAPnP: >>%s I/O range %d with %d ports at %04X-%04X, align %d, %d-bit decode\n", in_df ? ">" : "", io, card->rom[i + 7], *((uint16_t *) &card->rom[i + 2]), *((uint16_t *) &card->rom[i + 4]), card->rom[i + 6], (card->rom[i + 1] & 0x01) ? 16 : 10);

                    if (card->rom[i + 1] & 0x01)
                        ld->io_16bit |= 1 << io;
                    else
                        ld->io_16bit &= ~(1 << io);

                    if (card->rom[i + 7] > ld->io_len[io])
                        ld->io_len[io] = card->rom[i + 7];

                    io++;

                    break;

                case 0x09: /* Fixed I/O port */
                    if (!ld) {
                        isapnp_log("ISAPnP: >>%s Fixed I/O descriptor with no logical device\n", in_df ? ">" : "");
                        break;
                    }

                    if (io > 7) {
                        isapnp_log("ISAPnP: >>%s Fixed I/O descriptor overflow (%d)\n", in_df ? ">" : "", io++);
                        break;
                    }

                    isapnp_log("ISAPnP: >>%s Fixed I/O range %d with %d ports at %04X\n", in_df ? ">" : "", io, card->rom[i + 3], *((uint16_t *) &card->rom[i + 1]));

                    /* Fixed I/O port ranges of this kind are always 10-bit. */
                    ld->io_16bit &= ~(1 << io);

                    if (card->rom[i + 3] > ld->io_len[io])
                        ld->io_len[io] = card->rom[i + 3];

                    io++;

                    break;

                case 0x0f: /* end tag */
                    /* Calculate checksum. */
                    res = 0x00;
                    for (uint16_t j = 9; j <= i; j++)
                        res += card->rom[j];
                    card->rom[i + 1] = -res;

                    isapnp_log("ISAPnP: End card resources (checksum %02X)\n", card->rom[i + 1]);

                    /* Stop parsing here. */
                    card->rom_size = i + 2;
                    break;

                default:
                    isapnp_log("ISAPnP: >%s%s Small resource %02X (length %d)\n", ldn ? ">" : "", in_df ? ">" : "", res, card->rom[i] & 0x07);
                    break;
            }

            i++; /* header */
        }
        i += len; /* specified length */
    }

    /* We're done with the last logical device. */
    if (ld && !existing)
        isapnp_reset_ld_regs(ld);
}

void
isapnp_enable_card(void *priv, uint8_t enable)
{
    isapnp_t *dev = (isapnp_t *) device_get_priv(&isapnp_device);
    if (!dev)
        return;

    /* Look for a matching card. */
    isapnp_card_t *card = dev->first_card;
    while (card) {
        if (card == priv) {
            /* Enable or disable the card. */
            if (!!enable ^ !!card->enable)
                card->state = (enable == ISAPNP_CARD_FORCE_CONFIG) ? PNP_STATE_CONFIG : PNP_STATE_WAIT_FOR_KEY;
            card->enable = enable;

            /* Invalidate other references if we're disabling this card. */
            if (!card->enable) {
                if (dev->isolated_card == card)
                    dev->isolated_card = NULL;
                if (dev->current_ld_card == card) {
                    dev->current_ld      = NULL;
                    dev->current_ld_card = NULL;
                }
            }

            break;
        }

        card = card->next;
    }
}

void
isapnp_set_csn(void *priv, uint8_t csn)
{
    isapnp_card_t *card = (isapnp_card_t *) priv;

    card->csn = csn;
    if (card->csn_changed)
        card->csn_changed(card->csn, card->priv);
}

uint8_t
isapnp_read_reg(void *priv, uint8_t ldn, uint8_t reg)
{
    isapnp_card_t   *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld   = card->first_ld;
    while (ld) {
        if (ld->number == ldn)
            break;
        ld = ld->next;
    }
    return isapnp_read_common(device_get_priv(&isapnp_device), card, ld, reg);
}

void
isapnp_write_reg(void *priv, uint8_t ldn, uint8_t reg, uint8_t val)
{
    isapnp_card_t   *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld   = card->first_ld;
    while (ld) {
        if (ld->number == ldn)
            break;
        ld = ld->next;
    }
    isapnp_write_common(device_get_priv(&isapnp_device), card, ld, reg, val);
}

void
isapnp_set_device_defaults(void *priv, uint8_t ldn, const isapnp_device_config_t *config)
{
    isapnp_card_t   *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld   = card->first_ld;

    /* Look for a logical device with this number. */
    while (ld && (ld->number != ldn))
        ld = ld->next;

    if (!ld) /* none found */
        return;

    ld->defaults = config;
}

void
isapnp_reset_card(void *priv)
{
    isapnp_card_t   *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld   = card->first_ld;

    /* Reset all logical devices. */
    while (ld) {
        /* Reset the logical device's configuration. */
        isapnp_reset_ld_config(ld);
        isapnp_device_config_changed(card, ld);

        ld = ld->next;
    }
}

void
isapnp_reset_device(void *priv, uint8_t ldn)
{
    isapnp_card_t   *card = (isapnp_card_t *) priv;
    isapnp_device_t *ld   = card->first_ld;

    /* Look for a logical device with this number. */
    while (ld && (ld->number != ldn))
        ld = ld->next;

    if (!ld) /* none found */
        return;

    /* Reset the logical device's configuration. */
    isapnp_reset_ld_config(ld);
    isapnp_device_config_changed(card, ld);
}

static const device_t isapnp_device = {
    .name          = "ISA Plug and Play",
    .internal_name = "isapnp",
    .flags         = 0,
    .local         = 0,
    .init          = isapnp_init,
    .close         = isapnp_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
