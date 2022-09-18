/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the UMC UM8669F Super I/O chip.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2008-2021 Sarah Walker.
 *		Copyright 2016-2021 Miran Grca.
 *		Copyright 2021 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
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

/* This ROM was reconstructed out of many assumptions, some of which based on the IT8671F. */
static uint8_t um8669f_pnp_rom[] = {
    0x55, 0xa3, 0x86, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, /* UMC8669, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x10,                                     /* PnP version 1.0, vendor version 1.0 */

    0x15, 0x41, 0xd0, 0x07, 0x00, 0x01,             /* logical device PNP0700, can participate in boot */
    0x22, 0xfa, 0x1f,                               /* IRQ 1/3/4/5/6/7/8/9/10/11/12 */
    0x2a, 0x0f, 0x0c,                               /* DMA 0/1/2/3, compatibility, no count by word, count by byte, is bus master, 8-bit only */
    0x47, 0x00, 0x00, 0x01, 0xf8, 0x03, 0x08, 0x08, /* I/O 0x100-0x3F8, decodes 10-bit, 8-byte alignment, 8 addresses */

    0x15, 0x41, 0xd0, 0x05, 0x01, 0x01,             /* logical device PNP0501, can participate in boot */
    0x22, 0xfa, 0x1f,                               /* IRQ 1/3/4/5/6/7/8/9/10/11/12 */
    0x47, 0x00, 0x00, 0x01, 0xf8, 0x03, 0x08, 0x08, /* I/O 0x100-0x3F8, decodes 10-bit, 8-byte alignment, 8 addresses */

    0x15, 0x41, 0xd0, 0x05, 0x01, 0x01,             /* logical device PNP0501, can participate in boot */
    0x22, 0xfa, 0x1f,                               /* IRQ 1/3/4/5/6/7/8/9/10/11/12 */
    0x47, 0x00, 0x00, 0x01, 0xf8, 0x03, 0x08, 0x08, /* I/O 0x100-0x3F8, decodes 10-bit, 8-byte alignment, 8 addresses */

    0x15, 0x41, 0xd0, 0x04, 0x00, 0x01,             /* logical device PNP0400, can participate in boot */
    0x22, 0xfa, 0x1f,                               /* IRQ 1/3/4/5/6/7/8/9/10/11/12 */
    0x47, 0x00, 0x00, 0x01, 0xf8, 0x03, 0x08, 0x08, /* I/O 0x100-0x3F8, decodes 10-bit, 8-byte alignment, 8 addresses */

    0x15, 0x41, 0xd0, 0xff, 0xff, 0x00, /* logical device PNPFFFF (just a dummy to create a gap in LDNs) */

    0x15, 0x41, 0xd0, 0xb0, 0x2f, 0x01,             /* logical device PNPB02F, can participate in boot */
    0x47, 0x00, 0x00, 0x01, 0xf8, 0x03, 0x08, 0x08, /* I/O 0x100-0x3F8, decodes 10-bit, 8-byte alignment, 8 addresses */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};
static const isapnp_device_config_t um8669f_pnp_defaults[] = {
    {
	.activate = 1,
	.io = { { .base = FDC_PRIMARY_ADDR }, },
	.irq = { { .irq = FDC_PRIMARY_IRQ }, },
	.dma = { { .dma = FDC_PRIMARY_DMA }, }
    }, {
	.activate = 1,
	.io = { { .base = COM1_ADDR }, },
	.irq = { { .irq = COM1_IRQ }, }
    }, {
	.activate = 1,
	.io = { { .base = COM2_ADDR }, },
	.irq = { { .irq = COM2_IRQ }, }
    }, {
	.activate = 1,
	.io = { { .base = LPT1_ADDR }, },
	.irq = { { .irq = LPT1_IRQ }, }
    }, {
	.activate = 0
    }, {
	.activate = 0,
	.io = { { .base = 0x200 }, }
    }
};

#ifdef ENABLE_UM8669F_LOG
int um8669f_do_log = ENABLE_UM8669F_LOG;

static void
um8669f_log(const char *fmt, ...)
{
    va_list ap;

    if (um8669f_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define um8669f_log(fmt, ...)
#endif

typedef struct um8669f_t {
    int                     locked, cur_reg_108;
    void                   *pnp_card;
    isapnp_device_config_t *pnp_config[5];

    uint8_t regs_108[256];

    fdc_t    *fdc;
    serial_t *uart[2];
    void     *gameport;
} um8669f_t;

static void
um8669f_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    if (ld > 5) {
        um8669f_log("UM8669F: Unknown logical device %d\n", ld);
        return;
    }

    um8669f_t *dev = (um8669f_t *) priv;

    switch (ld) {
        case 0:
            fdc_remove(dev->fdc);

            if (config->activate) {
                um8669f_log("UM8669F: FDC enabled at port %04X IRQ %d DMA %d\n", config->io[0].base, config->irq[0].irq, (config->dma[0].dma == ISAPNP_DMA_DISABLED) ? -1 : config->dma[0].dma);

                if (config->io[0].base != ISAPNP_IO_DISABLED)
                    fdc_set_base(dev->fdc, config->io[0].base);

                fdc_set_irq(dev->fdc, config->irq[0].irq);
                fdc_set_dma_ch(dev->fdc, (config->dma[0].dma == ISAPNP_DMA_DISABLED) ? -1 : config->dma[0].dma);
            } else {
                um8669f_log("UM8669F: FDC disabled\n");
            }

            break;

        case 1:
        case 2:
            serial_remove(dev->uart[ld - 1]);

            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
                um8669f_log("UM8669F: UART %d enabled at port %04X IRQ %d\n", ld - 1, config->io[0].base, config->irq[0].irq);
                serial_setup(dev->uart[ld - 1], config->io[0].base, config->irq[0].irq);
            } else {
                um8669f_log("UM8669F: UART %d disabled\n", ld - 1);
            }

            break;

        case 3:
            lpt1_remove();

            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
                um8669f_log("UM8669F: LPT enabled at port %04X IRQ %d\n", config->io[0].base, config->irq[0].irq);
                lpt1_init(config->io[0].base);
            } else {
                um8669f_log("UM8669F: LPT disabled\n");
            }

            break;

        case 5:
            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
                um8669f_log("UM8669F: Game port enabled at port %04X\n", config->io[0].base);
                gameport_remap(dev->gameport, config->io[0].base);
            } else {
                um8669f_log("UM8669F: Game port disabled\n");
                gameport_remap(dev->gameport, 0);
            }
    }
}

void
um8669f_write(uint16_t port, uint8_t val, void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;

    um8669f_log("UM8669F: write(%04X, %02X)\n", port, val);

    if (dev->locked) {
        if ((port == 0x108) && (val == 0xaa))
            dev->locked = 0;
    } else {
        if (port == 0x108) {
            if (val == 0x55)
                dev->locked = 1;
            else
                dev->cur_reg_108 = val;
        } else {
            dev->regs_108[dev->cur_reg_108] = val;

            if (dev->cur_reg_108 == 0xc1) {
                um8669f_log("UM8669F: ISAPnP %sabled\n", (val & 0x80) ? "en" : "dis");
                isapnp_enable_card(dev->pnp_card, (val & 0x80) ? ISAPNP_CARD_FORCE_CONFIG : ISAPNP_CARD_DISABLE);
            }
        }
    }
}

uint8_t
um8669f_read(uint16_t port, void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;
    uint8_t    ret = 0xff;

    if (!dev->locked) {
        if (port == 0x108)
            ret = dev->cur_reg_108; /* ??? */
        else
            ret = dev->regs_108[dev->cur_reg_108];
    }

    um8669f_log("UM8669F: read(%04X) = %02X\n", port, ret);

    return ret;
}

void
um8669f_reset(um8669f_t *dev)
{
    um8669f_log("UM8669F: reset()\n");

    fdc_reset(dev->fdc);

    serial_remove(dev->uart[0]);

    serial_remove(dev->uart[1]);

    lpt1_remove();

    isapnp_enable_card(dev->pnp_card, ISAPNP_CARD_DISABLE);

    dev->locked = 1;

    isapnp_reset_card(dev->pnp_card);
}

static void
um8669f_close(void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;

    um8669f_log("UM8669F: close()\n");

    free(dev);
}

static void *
um8669f_init(const device_t *info)
{
    um8669f_log("UM8669F: init()\n");

    um8669f_t *dev = (um8669f_t *) malloc(sizeof(um8669f_t));
    memset(dev, 0, sizeof(um8669f_t));

    dev->pnp_card = isapnp_add_card(um8669f_pnp_rom, sizeof(um8669f_pnp_rom), um8669f_pnp_config_changed, NULL, NULL, NULL, dev);
    for (uint8_t i = 0; i < (sizeof(um8669f_pnp_defaults) / sizeof(isapnp_device_config_t)); i++)
        isapnp_set_device_defaults(dev->pnp_card, i, &um8669f_pnp_defaults[i]);

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->gameport = gameport_add(&gameport_sio_device);

    io_sethandler(0x0108, 0x0002,
                  um8669f_read, NULL, NULL, um8669f_write, NULL, NULL, dev);

    um8669f_reset(dev);

    return dev;
}

const device_t um8669f_device = {
    .name          = "UMC UM8669F Super I/O",
    .internal_name = "um8669f",
    .flags         = 0,
    .local         = 0,
    .init          = um8669f_init,
    .close         = um8669f_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
