/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the ITE IT86x1F Super I/O chips.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/gameport.h>
#include <86box/sio.h>
#include <86box/isapnp.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

enum {
    ITE_IT8661F = 0x8661,
    ITE_IT8671F = 0x8681
};

#define CHIP_ID *((uint16_t *) &dev->global_regs[0])

static void it8671f_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv);
static void it8661f_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv);

static const struct {
    uint16_t chip_id;
    uint16_t unlock_id;
    uint8_t  gpio_ldn;
    /* Fake ROMs to delegate all the logical device register handling over to the ISAPnP subsystem.
       The actual ROMs/IDs used by real chips when those are set to ISAPnP mode remain to be seen. */
    uint8_t *pnp_rom;

    const isapnp_device_config_t *pnp_defaults;

    void (*pnp_config_changed)(uint8_t ld, isapnp_device_config_t *config, void *priv);
} it86x1f_models[] = {
    {
        .chip_id = ITE_IT8661F,
        .unlock_id = 0x8661,
        .gpio_ldn = 0x05,
        .pnp_rom = (uint8_t[]) {
            0x26, 0x85, 0x86, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, /* ITE8661, dummy checksum (filled in by isapnp_add_card) */
            0x0a, 0x10, 0x10,                                     /* PnP version 1.0, vendor version 1.0 */

            0x15, 0x41, 0xd0, 0x07, 0x00, 0x01,             /* logical device PNP0700, can participate in boot */
            0x23, 0xf8, 0x0f, 0x02,                         /* IRQ 3/4/5/6/7/8/9/10/11, low true edge sensitive */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

            0x15, 0x41, 0xd0, 0x05, 0x01, 0x01,             /* logical device PNP0501, can participate in boot */
            0x23, 0xf8, 0x0f, 0x02,                         /* IRQ 3/4/5/6/7/8/9/10/11, low true edge sensitive */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

            0x15, 0x41, 0xd0, 0x05, 0x01, 0x01,             /* logical device PNP0501, can participate in boot */
            0x23, 0xf8, 0x0f, 0x02,                         /* IRQ 3/4/5/6/7/8/9/10/11, low true edge sensitive */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

            0x15, 0x41, 0xd0, 0x04, 0x00, 0x01,             /* logical device PNP0400, can participate in boot */
            0x23, 0xf8, 0x0f, 0x02,                         /* IRQ 3/4/5/6/7/8/9/10/11, low true edge sensitive */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
            0x47, 0x01, 0x00, 0x01, 0xfc, 0x0f, 0x04, 0x04, /* I/O 0x100-0xFFC, decodes 16-bit, 4-byte alignment, 4 addresses */

            0x15, 0x41, 0xd0, 0x05, 0x10, 0x01,             /* logical device PNP0510, can participate in boot */
            0x23, 0xf8, 0x0f, 0x02,                         /* IRQ 3/4/5/6/7/8/9/10/11, low true edge sensitive */
            0x23, 0xf8, 0x0f, 0x02,                         /* IRQ 3/4/5/6/7/8/9/10/11, low true edge sensitive */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

            0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
        },
        .pnp_defaults = (const isapnp_device_config_t[]) {
            {
                .activate = 0,
                .io = { { .base = FDC_PRIMARY_ADDR }, },
                .irq = { { .irq = FDC_PRIMARY_IRQ }, },
                .dma = { { .dma = FDC_PRIMARY_DMA }, }
            }, {
                .activate = 0,
                .io = { { .base = COM1_ADDR }, },
                .irq = { { .irq = COM1_IRQ }, }
            }, {
                .activate = 0,
                .io = { { .base = COM2_ADDR }, },
                .irq = { { .irq = COM2_IRQ }, }
            }, {
                .activate = 0,
                .io = { { .base = LPT1_ADDR }, { .base = 0x778 }, },
                .irq = { { .irq = LPT1_IRQ }, },
                .dma = { { .dma = 3 }, }
            }, {
                .activate = 0,
                .io = { { .base = COM4_ADDR }, { .base = 0x300 }, },
                .irq = { { .irq = 10 }, { .irq = 11 }, },
                .dma = { { .dma = 1 }, { .dma = 0 }, }
            }, {
                .activate = -1
            }
        },
        .pnp_config_changed = it8661f_pnp_config_changed
    }, {
        .chip_id = ITE_IT8671F,
        .unlock_id = 0x8680,
        .gpio_ldn = 0x07,
        .pnp_rom = (uint8_t[]) {
            0x26, 0x85, 0x86, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, /* ITE8671, dummy checksum (filled in by isapnp_add_card) */
            0x0a, 0x10, 0x10,                                     /* PnP version 1.0, vendor version 1.0 */

            0x15, 0x41, 0xd0, 0x07, 0x00, 0x01,             /* logical device PNP0700, can participate in boot */
            0x23, 0xfa, 0x1f, 0x02,                         /* IRQ 1/3/4/5/6/7/8/9/10/11/12, low true edge sensitive */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

            0x15, 0x41, 0xd0, 0x05, 0x01, 0x01,             /* logical device PNP0501, can participate in boot */
            0x23, 0xfa, 0x1f, 0x02,                         /* IRQ 1/3/4/5/6/7/8/9/10/11/12, low true edge sensitive */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

            0x15, 0x41, 0xd0, 0x05, 0x10, 0x01,             /* logical device PNP0510, can participate in boot */
            0x23, 0xfa, 0x1f, 0x02,                         /* IRQ 1/3/4/5/6/7/8/9/10/11/12, low true edge sensitive */
            0x23, 0xfa, 0x1f, 0x02,                         /* IRQ 1/3/4/5/6/7/8/9/10/11/12, low true edge sensitive */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */

            0x15, 0x41, 0xd0, 0x04, 0x00, 0x01,             /* logical device PNP0400, can participate in boot */
            0x23, 0xfa, 0x1f, 0x02,                         /* IRQ 1/3/4/5/6/7/8/9/10/11/12, low true edge sensitive */
            0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
            0x47, 0x01, 0x00, 0x01, 0xf8, 0x0f, 0x08, 0x08, /* I/O 0x100-0xFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
            0x47, 0x01, 0x00, 0x01, 0xfc, 0x0f, 0x04, 0x04, /* I/O 0x100-0xFFC, decodes 16-bit, 4-byte alignment, 4 addresses */

            0x15, 0x41, 0xd0, 0xff, 0xff, 0x00, /* logical device PNPFFFF (dummy to create APC gap in LDNs) */

            0x15, 0x41, 0xd0, 0x03, 0x03, 0x01,             /* logical device PNP0303, can participate in boot */
            0x23, 0xfa, 0x1f, 0x02,                         /* IRQ 1/3/4/5/6/7/8/9/10/11/12, low true edge sensitive */
            0x47, 0x01, 0x00, 0x00, 0xff, 0x0f, 0x01, 0x01, /* I/O 0x0-0xFFF, decodes 16-bit, 1-byte alignment, 1 address */
            0x47, 0x01, 0x00, 0x00, 0xff, 0x0f, 0x01, 0x01, /* I/O 0x0-0xFFF, decodes 16-bit, 1-byte alignment, 1 address */

            0x15, 0x41, 0xd0, 0x0f, 0x13, 0x01, /* logical device PNP0F13, can participate in boot */
            0x23, 0xfa, 0x1f, 0x02,             /* IRQ 1/3/4/5/6/7/8/9/10/11/12, low true edge sensitive */

            0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
        },
        .pnp_defaults = (const isapnp_device_config_t[]) {
            {
                .activate = 0,
                .io = { { .base = FDC_PRIMARY_ADDR }, },
                .irq = { { .irq = FDC_PRIMARY_IRQ }, },
                .dma = { { .dma = FDC_PRIMARY_DMA }, }
            }, {
                .activate = 0,
                .io = { { .base = COM1_ADDR }, },
                .irq = { { .irq = COM1_IRQ }, }
            }, {
                .activate = 0,
                .io = { { .base = COM2_ADDR }, { .base = 0x300 }, },
                .irq = { { .irq = COM2_IRQ }, { .irq = 10 }, },
                .dma = { { .dma = 0 }, { .dma = 1 }, }
            }, {
                .activate = 0,
                .io = { { .base = LPT1_ADDR }, { .base = 0x778 }, },
                .irq = { { .irq = LPT1_IRQ }, },
                .dma = { { .dma = 3 }, }
            }, {
                .activate = 0
            }, {
                .activate = 1,
                .io = { { .base = 0x60 }, { .base = 0x64 }, },
                .irq = { { .irq = 1 }, }
            }, {
                .activate = 0,
                .irq = { { .irq = 12 }, }
            }, {
                .activate = -1
            }
        },
        .pnp_config_changed = it8671f_pnp_config_changed
    }
};

#ifdef ENABLE_IT86X1F_LOG
int it86x1f_do_log = ENABLE_IT86X1F_LOG;

static void
it86x1f_log(const char *fmt, ...)
{
    va_list ap;

    if (it86x1f_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define it86x1f_log(fmt, ...)
#endif

typedef struct it86x1f_t {
    uint8_t instance;
    uint8_t locked;
    uint8_t cur_ldn;
    uint8_t cur_reg;
    void   *pnp_card;
    uint8_t global_regs[16]; /* [0x20:0x2f] */
    uint8_t ldn_regs[8][16]; /* [0xf0:0xff] */
    uint8_t gpio_regs[36]; /* [0x60:0x7f] then [0xe0:0xe3] */
    uint8_t gpio_ldn;

    uint16_t unlock_id;
    uint16_t addr_port;
    uint16_t data_port;
    uint8_t  unlock_val;
    uint8_t  unlock_pos : 2;
    uint8_t  key_pos : 5;

    fdc_t    *fdc;
    serial_t *uart[2];
    void     *gameport;
} it86x1f_t;

static void it86x1f_remap(it86x1f_t *dev, uint16_t addr_port, uint16_t data_port);

static void
it8661f_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    if (ld > 5) {
        it86x1f_log("IT86x1F: Unknown logical device %d\n", ld);
        return;
    }

    it86x1f_t *dev = (it86x1f_t *) priv;

    switch (ld) {
        case 0:
            fdc_remove(dev->fdc);

            if (config->activate) {
                it86x1f_log("IT86x1F: FDC enabled at port %04X IRQ %d DMA %d\n", config->io[0].base, config->irq[0].irq, (config->dma[0].dma == ISAPNP_DMA_DISABLED) ? -1 : config->dma[0].dma);

                if (config->io[0].base != ISAPNP_IO_DISABLED)
                    fdc_set_base(dev->fdc, config->io[0].base);

                fdc_set_irq(dev->fdc, config->irq[0].irq);
                fdc_set_dma_ch(dev->fdc, (config->dma[0].dma == ISAPNP_DMA_DISABLED) ? -1 : config->dma[0].dma);
            } else {
                it86x1f_log("IT86x1F: FDC disabled\n");
            }

            break;

        case 1:
        case 2:
            serial_remove(dev->uart[ld - 1]);

            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
                it86x1f_log("IT86x1F: UART %d enabled at port %04X IRQ %d\n", ld - 1, config->io[0].base, config->irq[0].irq);
                serial_setup(dev->uart[ld - 1], config->io[0].base, config->irq[0].irq);
            } else {
                it86x1f_log("IT86x1F: UART %d disabled\n", ld - 1);
            }

            break;

        case 3:
            lpt1_remove();

            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
                it86x1f_log("IT86x1F: LPT enabled at port %04X IRQ %d\n", config->io[0].base, config->irq[0].irq);
                lpt1_setup(config->io[0].base);
            } else {
                it86x1f_log("IT86x1F: LPT disabled\n");
            }

            break;

        case 4:
            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
                it86x1f_log("IT86x1F: IR enabled at ports %04X %04X IRQs %d %d DMAs %d %d\n", config->io[0].base, config->io[1].base, config->irq[0].irq, config->irq[1].irq, (config->dma[0].dma == ISAPNP_DMA_DISABLED) ? -1 : config->dma[0].dma, (config->dma[1].dma == ISAPNP_DMA_DISABLED) ? -1 : config->dma[1].dma);
            } else {
                it86x1f_log("IT86x1F: IR disabled\n");
            }
            break;

        default:
            break;
    }
}

static void
it8671f_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;

    switch (ld) {
        case 2:
            it8661f_pnp_config_changed(4, config, dev); /* just for logging, should change if IR UART is implemented */
            fallthrough;

        case 0 ... 1:
        case 3:
            it8661f_pnp_config_changed(ld, config, dev);
            break;

        case 5:
            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED) && (config->io[1].base != ISAPNP_IO_DISABLED)) {
                it86x1f_log("IT86x1F: KBC enabled at ports %04X %04X IRQ %d\n", config->io[0].base, config->io[1].base, config->irq[0].irq);
            } else {
                it86x1f_log("IT86x1F: KBC disabled\n");
            }
            break;

        case 6:
            if (config->activate) {
                it86x1f_log("IT86x1F: KBC mouse enabled at IRQ %d\n", config->irq[0].irq);
            } else {
                it86x1f_log("IT86x1F: KBC mouse disabled\n");
            }
            break;

        default:
            break;
    }
}

static uint8_t
it86x1f_pnp_read_vendor_reg(uint8_t ld, uint8_t reg, void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;
    uint8_t    ret = 0xff;

    switch (reg) {
        case 0x20 ... 0x2f:
            ret = dev->global_regs[reg & 0x0f];
            break;

        case 0x60 ... 0x7f:
            if (ld != dev->gpio_ldn)
                break;

            ret = dev->gpio_regs[reg & 0x1f];
            break;

        case 0xe0 ... 0xe3:
            if (ld != dev->gpio_ldn)
                break;

            ret = dev->gpio_regs[0x20 | (reg & 0x03)];
            break;

        case 0xf0 ... 0xff:
            if (ld > dev->gpio_ldn)
                break;

            ret = dev->ldn_regs[ld][reg & 0x0f];
            break;

        default:
            break;
    }

    it86x1f_log("IT86x1F: read_vendor_reg(%X, %02X) = %02X\n", ld, reg, ret);

    return ret;
}

static void
it86x1f_pnp_write_vendor_reg(uint8_t ld, uint8_t reg, uint8_t val, void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;
    uint8_t effective_ldn;

    it86x1f_log("IT86x1F: write_vendor_reg(%X, %02X, %02X)\n", ld, reg, val);

    switch (reg) {
        case 0x22:
            if (CHIP_ID == ITE_IT8661F) {
                dev->global_regs[reg & 0x0f] = (val & 0x30) | (dev->global_regs[reg & 0x0f] & ~0x30);
                uint8_t mcc = (val & 0x30) >> 4;
                if (mcc != dev->instance) {
                    it86x1f_log("IT86x1F: Instance %d unmapping as ID %d was written\n", dev->instance, mcc);
                    it86x1f_remap(dev, 0, 0);
                }
            }
            break;

        case 0x23:
            val &= (1 << dev->gpio_ldn) - 1;
            dev->global_regs[reg & 0x0f] = val;
#ifdef ENABLE_IT86X1F_LOG
            if (val)
                it86x1f_log("IT86x1F: Warning: ISAPnP mode enabled.\n");
#endif
            break;

        case 0x24:
            dev->global_regs[reg & 0x0f] = val & ((CHIP_ID == ITE_IT8661F) ? 0x03 : 0x5f);
            break;

        case 0x25:
            val &= (CHIP_ID == ITE_IT8661F) ? 0x1f : 0xf0;
            fallthrough;

        case 0x26:
            if (ld == dev->gpio_ldn)
                dev->global_regs[reg & 0x0f] = val;
            break;

        case 0x2e ... 0x2f:
            if ((CHIP_ID == ITE_IT8671F) && (ld == 0xf4))
                dev->global_regs[reg & 0x0f] = val;
            break;

        case 0x60 ... 0x7f:
            if (ld != dev->gpio_ldn)
                break;

            dev->gpio_regs[reg & 0x1f] = val;
            break;

        case 0xe0 ... 0xe3:
            if (ld != dev->gpio_ldn)
                break;

            dev->gpio_regs[0x20 | (reg & 0x0f)] = val;
            break;

        case 0xf0 ... 0xff:
            /* Translate GPIO LDN to 7 for the switch block. */
            if (ld == dev->gpio_ldn)
                effective_ldn = 7;
            else if (ld == 7)
                effective_ldn = 8; /* dummy */
            else
                effective_ldn = ld;

            switch ((effective_ldn << 8) | reg) {
                case 0x0f0:
                    dev->ldn_regs[ld][reg & 0x0f] = val & 0x0f;
                    fdc_set_swwp(dev->fdc, !!(val & 0x01));
                    fdc_set_swap(dev->fdc, !!(val & 0x04));
                    break;

                case 0x1f0:
                    dev->ldn_regs[ld][reg & 0x0f] = val & 0x03;
                    break;

                case 0x2f0:
                    dev->ldn_regs[ld][reg & 0x0f] = val & ((CHIP_ID == ITE_IT8661F) ? 0x03 : 0xf3);
                    break;

                case 0x2f1:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->ldn_regs[ld][reg & 0x0f] = val & 0xb7;
                    break;

                case 0x3f0:
                    dev->ldn_regs[ld][reg & 0x0f] = val & 0x07;
                    break;

                case 0x4f0:
                    if (CHIP_ID == ITE_IT8661F)
                        val &= 0x3f;
                    dev->ldn_regs[ld][reg & 0x0f] = val;
                    break;

                case 0x4f1:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->ldn_regs[ld][reg & 0x0f] = val & 0x7f;
                    break;

                case 0x4f2:
                case 0x4f6:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->ldn_regs[ld][reg & 0x0f] = val;
                    break;

                case 0x4f7:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->ldn_regs[ld][reg & 0x0f] = val & 0x7f;
                    break;

                case 0x4f8:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->ldn_regs[ld][reg & 0x0f] = val & 0x07;
                    break;

                case 0x5f0:
                    dev->ldn_regs[ld][reg & 0x0f] = val & 0x1f;
                    break;

                case 0x6f0:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->ldn_regs[ld][reg & 0x0f] = val & 0x03;
                    break;

                case 0x760:
                case 0x762:
                case 0x764:
                case 0x766:
                    dev->gpio_regs[reg & 0x1f] = val & 0x0f;
                    break;

                case 0x772:
                    if (CHIP_ID != ITE_IT8671F)
                        break;
                    fallthrough;

                case 0x761:
                case 0x763:
                case 0x765:
                case 0x767:
                case 0x770:
                    dev->gpio_regs[reg & 0x1f] = val;

                case 0x771:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->gpio_regs[reg & 0x1f] = val & 0xde;
                    break;

                case 0x7e0:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->gpio_regs[0x20 | (reg & 0x03)] = val & 0xef;
                    break;

                case 0x7e1:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->gpio_regs[0x20 | (reg & 0x03)] = val & 0x7f;
                    break;

                case 0x7e3:
                    if ((CHIP_ID == ITE_IT8671F) && (val & 0x80))
                        *((uint16_t *) &dev->gpio_regs[0x22]) = 0x0000;
                    break;

                case 0x7fb:
                    if (CHIP_ID == ITE_IT8671F)
                        val &= 0x7f;
                    fallthrough;

                case 0x7f0 ... 0x7f5:
                    dev->ldn_regs[ld][reg & 0x0f] = val;
                    break;

                case 0x7f6:
                    dev->ldn_regs[ld][reg & 0x0f] = val & ((CHIP_ID == ITE_IT8661F) ? 0x3f : 0xcf);
                    break;

                case 0x7f7:
                    dev->ldn_regs[ld][reg & 0x0f] = val & ((CHIP_ID == ITE_IT8661F) ? 0x9f : 0xdf);
                    break;

                case 0x7f8 ... 0x7fa:
                    dev->ldn_regs[ld][reg & 0x0f] = val & ((CHIP_ID == ITE_IT8661F) ? 0x1f : 0x0f);
                    break;

                case 0x7fc:
                    if (CHIP_ID == ITE_IT8661F)
                        dev->ldn_regs[ld][reg & 0x0f] = val;
                    break;

                case 0x7ff:
                    if (CHIP_ID == ITE_IT8671F)
                        dev->ldn_regs[ld][reg & 0x0f] = val & 0x2f;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void
it86x1f_write_addr(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;

    it86x1f_log("IT86x1F: write_addr(%04X, %02X)\n", port, val);

    if (dev->locked) {
        if (val == isapnp_init_key[dev->key_pos]) {
            if (++dev->key_pos == 0) {
                it86x1f_log("IT86x1F: Unlocked\n");
                dev->locked = 0;
            }
        } else {
            dev->key_pos = 0;
        }
    } else {
        dev->cur_reg = val;
    }
}

static void
it86x1f_write_data(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;

    it86x1f_log("IT86x1F: write_data(%04X, %02X)\n", port, val);

    if (dev->locked)
        return;

    switch (dev->cur_reg) {
        case 0x00 ... 0x01:
        case 0x03 ... 0x06:
        case 0x31:
        case 0x71:
        case 0x73:
            break; /* ISAPnP-only */

        case 0x07:
            dev->cur_ldn = val;
            break;

        case 0x02:
            if (val & 0x02) {
                it86x1f_log("IT86x1F: Locked => ");
                dev->locked = 1;
                it86x1f_remap(dev, 0, 0);
            }
            fallthrough;

        default:
            isapnp_write_reg(dev->pnp_card, dev->cur_ldn, dev->cur_reg, val);
            break;
    }
}

static uint8_t
it86x1f_read_addr(UNUSED(uint16_t port), void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;
    uint8_t    ret = dev->locked ? 0xff : dev->cur_reg;

    it86x1f_log("IT86x1F: read_addr(%04X) = %02X\n", port, ret);

    return ret;
}

static uint8_t
it86x1f_read_data(UNUSED(uint16_t port), void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;
    uint8_t    ret = 0xff;

    switch (dev->cur_reg) {
        case 0x00 ... 0x01:
        case 0x03 ... 0x06:
        case 0x31:
        case 0x71:
        case 0x73:
            break; /* ISAPnP-only */

        case 0x07:
            ret = dev->cur_ldn;
            break;

        default:
            ret = isapnp_read_reg(dev->pnp_card, dev->cur_ldn, dev->cur_reg);
            break;
    }

    it86x1f_log("IT86x1F: read_data(%04X) = %02X\n", port, ret);

    return ret;
}

static void
it86x1f_remap(it86x1f_t *dev, uint16_t addr_port, uint16_t data_port)
{
    if (dev->addr_port)
        io_removehandler(dev->addr_port, 1, it86x1f_read_addr, NULL, NULL, it86x1f_write_addr, NULL, NULL, dev);
    if (dev->data_port)
        io_removehandler(dev->data_port, 1, it86x1f_read_data, NULL, NULL, it86x1f_write_data, NULL, NULL, dev);

    it86x1f_log("IT86x1F: remap(%04X, %04X)\n", addr_port, data_port);
    dev->addr_port = addr_port;
    dev->data_port = data_port;

    if (dev->addr_port)
        io_sethandler(dev->addr_port, 1, it86x1f_read_addr, NULL, NULL, it86x1f_write_addr, NULL, NULL, dev);
    if (dev->data_port)
        io_sethandler(dev->data_port, 1, it86x1f_read_data, NULL, NULL, it86x1f_write_data, NULL, NULL, dev);
}

static void
it86x1f_write_unlock(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;

    it86x1f_log("IT86x1F: write_unlock(%04X, %02X)\n", port, val);

    if (!dev->locked)
        dev->unlock_pos = 0;

    switch (dev->unlock_pos++) {
        case 0:
            if (val != (dev->unlock_id >> 8))
                dev->unlock_pos = 0;
            break;

        case 1:
            if (val != (dev->unlock_id & 0xff))
                dev->unlock_pos = 0;
            break;

        case 2:
            if ((val != 0x55) && (val != 0xaa))
                dev->unlock_pos = 0;
            else
                dev->unlock_val = val;
            break;

        case 3:
            switch ((dev->unlock_val << 8) | val) {
                case 0x5555:
                    it86x1f_remap(dev, 0x3f0, 0x3f1);
                    break;

                case 0x55aa:
                    it86x1f_remap(dev, 0x3bd, 0x3bf);
                    break;

                case 0xaa55:
                    it86x1f_remap(dev, 0x370, 0x371);
                    break;

                default:
                    it86x1f_remap(dev, 0, 0);
                    break;
            }
            dev->unlock_pos = 0;
            break;
    }
}

void
it86x1f_reset(it86x1f_t *dev)
{
    it86x1f_log("IT86x1F: reset()\n");

    fdc_reset(dev->fdc);

    serial_remove(dev->uart[0]);

    serial_remove(dev->uart[1]);

    lpt1_remove();

    isapnp_enable_card(dev->pnp_card, ISAPNP_CARD_DISABLE);

    dev->locked = 1;

    isapnp_reset_card(dev->pnp_card);
}

static void
it86x1f_close(void *priv)
{
    it86x1f_t *dev = (it86x1f_t *) priv;

    it86x1f_log("IT86x1F: close()\n");

    free(dev);
}

static void *
it86x1f_init(UNUSED(const device_t *info))
{
    it86x1f_t *dev = (it86x1f_t *) calloc(1, sizeof(it86x1f_t));

    uint8_t i;
    for (i = 0; i < (sizeof(it86x1f_models) / sizeof(it86x1f_models[0])); i++) {
        if (it86x1f_models[i].chip_id == info->local)
            break;
    }
    if (i >= (sizeof(it86x1f_models) / sizeof(it86x1f_models[0]))) {
        fatal("IT86x1F: Unknown type %04" PRIXPTR " selected\n", info->local);
        return NULL;
    }
    it86x1f_log("IT86x1F: init(%04" PRIXPTR ")\n", info->local);

    /* Let the resource data parser figure out the ROM size. */
    dev->pnp_card = isapnp_add_card(it86x1f_models[i].pnp_rom, -1, it86x1f_models[i].pnp_config_changed, NULL, it86x1f_pnp_read_vendor_reg, it86x1f_pnp_write_vendor_reg, dev);
    for (uint8_t j = 0; it86x1f_models[i].pnp_defaults[j].activate != (uint8_t) -1; j++)
        isapnp_set_device_defaults(dev->pnp_card, j, &it86x1f_models[i].pnp_defaults[j]);

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->gameport = gameport_add(&gameport_sio_device);

    dev->instance = device_get_instance();
    dev->gpio_ldn = it86x1f_models[i].gpio_ldn;
    CHIP_ID = it86x1f_models[i].chip_id;
    dev->unlock_id = it86x1f_models[i].unlock_id;
    io_sethandler(0x279, 1, NULL, NULL, NULL, it86x1f_write_unlock, NULL, NULL, dev);

    it86x1f_reset(dev);

    return dev;
}

const device_t it8661f_device = {
    .name          = "ITE IT8661F Super I/O",
    .internal_name = "it8661f",
    .flags         = 0,
    .local         = ITE_IT8661F,
    .init          = it86x1f_init,
    .close         = it86x1f_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t it8671f_device = {
    .name          = "ITE IT8671F Super I/O",
    .internal_name = "it8671f",
    .flags         = 0,
    .local         = ITE_IT8671F,
    .init          = it86x1f_init,
    .close         = it86x1f_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
