/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the National Semiconductor PC87366 (NSC366)
 *          Super I/O chip.
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
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
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/hwm.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/nsc366.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdd_common.h>
#include <86box/port_92.h>
#include <86box/sio.h>

typedef struct {
    fdc_t        *fdc;
    serial_t     *uart[2];
    nsc366_hwm_t *hwm;

    uint8_t index;
    uint8_t ldn;
    uint8_t sio_config[14];
    uint8_t ld_activate[15];
    uint8_t io_base0[2][15];
    uint8_t io_base1[2][15];
    uint8_t int_num_irq[15];
    uint8_t irq[15];
    uint8_t dma_select0[15];
    uint8_t dma_select1[15];
    uint8_t dev_specific_config[3][15];

    int siofc_lock;
} nsc366_t;

#ifdef ENABLE_NSC366_LOG
int nsc366_do_log = ENABLE_NSC366_LOG;

void
nsc366_log(const char *fmt, ...)
{
    va_list ap;

    if (nsc366_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nsc366_log(fmt, ...)
#endif

static void
nsc366_fdc(nsc366_t *dev)
{
    fdc_remove(dev->fdc);
    int base   = ((dev->io_base0[0][0] & 7) << 8) | (dev->io_base0[1][0] & 0xf8);
    int irq    = dev->int_num_irq[0] & 0x0f;
    int dma_ch = dev->dma_select0[0] & 7;

    if (dev->ld_activate[0]) {
        nsc366_log("NSC 366 FDC: Reconfigured with Base: 0x%04x IRQ: %d DMA Channel: %d\n", base, irq, dma_ch);
        fdc_set_base(dev->fdc, base);
        fdc_set_irq(dev->fdc, irq);
        fdc_set_dma_ch(dev->fdc, dma_ch);
        fdc_update_densel_polarity(dev->fdc, !!(dev->dev_specific_config[0][0] & 0x20));
        if (dev->dev_specific_config[0][0] & 8)
            fdc_writeprotect(dev->fdc);
    }
}

void
nsc366_lpt(nsc366_t *dev)
{
    lpt1_remove();
    int base = ((dev->io_base0[0][1] & 7) << 8) | (dev->io_base0[1][1] & 0xfc);
    int irq  = (dev->int_num_irq[1] & 0x0f);

    if (dev->ld_activate[1]) {
        nsc366_log("NSC 366 LPT: Reconfigured with Base 0x%04x IRQ: %d\n", base, irq);
        lpt1_init(base);
        lpt1_irq(irq);
    }
}

static void
nsc366_uart(int uart, nsc366_t *dev)
{
    serial_remove(dev->uart[uart]);
    uint16_t base = ((dev->io_base0[0][2 + uart] & 7) << 8) | (dev->io_base0[1][2 + uart] & 0xf8);
    uint8_t irq  = (dev->int_num_irq[2 + uart] & 0x0f);

    if (dev->ld_activate[2 + uart]) {
        nsc366_log("NSC 366 UART Serial %d: Reconfigured with Base 0x%04x IRQ: %d\n", uart, base, irq);
        serial_setup(dev->uart[uart], base, irq);
    }
}

static void
nsc366_fscm_enable(nsc366_t *dev)
{
    dev->hwm->fscm_enable = (!!(dev->dev_specific_config[1][9] & 1) << 2) | (!!(dev->dev_specific_config[0][9] & 0x20) << 1) | !!(dev->dev_specific_config[0][9] & 4);

    /*
     *   Register F1h Bit 0: Fan Monitor 2 Enable
     *   Register F0h Bit 5: Fan Monitor 1 Enable
     *   Register F0h Bit 2: Fan Monitor 0 Enable
     *   Configuration Enables are not really needed
     */
}

static void
nsc366_fscm(nsc366_t *dev)
{
    uint16_t base = (dev->io_base0[0][9] << 8) | (dev->io_base0[1][9] & 0xf0);

    if (dev->ld_activate[9])
        nsc366_log("NSC 366 Fan Control: Reconfigured with Base 0x%04x\n", base);

    nsc366_update_fscm_io(dev->ld_activate[9], base, dev->hwm);
}

static void
nsc366_vlm(nsc366_t *dev)
{
    uint16_t base = (dev->io_base0[0][13] << 8) | (dev->io_base0[1][13] & 0xf0);

    if (dev->ld_activate[13])
        nsc366_log("NSC 366 Voltage Monitor: Reconfigured with Base 0x%04x\n", base);

    nsc366_update_vlm_io(dev->ld_activate[13], base, dev->hwm);
}

static void
nsc366_tms(nsc366_t *dev)
{
    uint16_t base = (dev->io_base0[0][14] << 8) | (dev->io_base0[1][14] & 0xf0);

    if (dev->ld_activate[14])
        nsc366_log("NSC 366 Temperature Monitor: Reconfigured with Base 0x%04x\n", base);

    nsc366_update_tms_io(dev->ld_activate[14], base, dev->hwm);
}

static void
nsc366_ldn_redirect(nsc366_t *dev)
{
    switch (dev->ldn) {
        case 0:
            nsc366_fdc(dev);
            break;

        case 1:
            nsc366_lpt(dev);
            break;

        case 2 ... 3:
            nsc366_uart(dev->ldn == 3, dev);
            break;

        case 9:
            nsc366_fscm_enable(dev);
            nsc366_fscm(dev);
            break;

        case 13:
            nsc366_vlm(dev);
            break;

        case 14:
            nsc366_tms(dev);
            break;

        default:
            break;
    }
}

static void
nsc366_write(uint16_t addr, uint8_t val, void *priv)
{
    nsc366_t *dev = (nsc366_t *) priv;

    if (addr & 1)
        switch (dev->index) {
            /* LDN */
            case 0x07:
                if (val <= 0x0e)
                    dev->ldn = val;
                break;

            /* Super I/O Configuration */
            case 0x20 ... 0x2d:
                switch (dev->index - 0x20) {
                    case 0x01:
                        if (!dev->siofc_lock) {
                            if (val & 0x80) {
                                dev->sio_config[dev->index - 0x20] = val | 0x80;
                                dev->siofc_lock                    = 1;
                            } else {
                                dev->sio_config[dev->index - 0x20] = val;
                            }
                        }
                        break;

                    case 0x02:
                        if (!dev->siofc_lock)
                            dev->sio_config[dev->index - 0x20] = val;
                        break;

                    case 0x03:
                        if (!dev->siofc_lock)
                            dev->sio_config[dev->index - 0x20] = val & 0xf7;
                        break;

                    case 0x04:
                        if (!dev->siofc_lock)
                            dev->sio_config[dev->index - 0x20] = val;
                        break;

                    case 0x05:
                        if (!dev->siofc_lock)
                            dev->sio_config[dev->index - 0x20] = val & 0xf3;
                        break;

                    case 0x08:
                        dev->sio_config[dev->index - 0x20] = val & 0xf3;
                        break;

                    case 0x0a:
                        if (!dev->siofc_lock)
                            dev->sio_config[dev->index - 0x20] = val;
                        break;

                    case 0x0b:
                        if (!dev->siofc_lock)
                            dev->sio_config[dev->index - 0x20] = val & 0x4f; // Force Case Intrusion to always off
                        break;

                    case 0x0c ... 0x0d:
                        dev->sio_config[dev->index - 0x20] = val & 0xf3;
                        break;

                    default:
                        break;
                }
                break;

            /* Logical Devices */
            case 0x30:
                dev->ld_activate[dev->ldn] = (val & 1) && (dev->sio_config[0] & 1);
                nsc366_ldn_redirect(dev);
                break;

            case 0x60 ... 0x61:
                dev->io_base0[dev->index & 1][dev->ldn] = val;
                nsc366_ldn_redirect(dev);
                break;

            case 0x62 ... 0x63:
                dev->io_base1[dev->index & 1][dev->ldn] = val;
                nsc366_ldn_redirect(dev);
                break;

            case 0x70:
                dev->int_num_irq[dev->ldn] = val & 0x1f;
                nsc366_ldn_redirect(dev);
                break;

            case 0x71:
                dev->irq[dev->ldn] = val;
                nsc366_ldn_redirect(dev);
                break;

            case 0x74:
                dev->dma_select0[dev->ldn] = val & 0x1f;
                nsc366_ldn_redirect(dev);
                break;

            case 0x75:
                dev->dma_select1[dev->ldn] = val & 0x1f;
                nsc366_ldn_redirect(dev);
                break;

            case 0xf0 ... 0xf2:
                dev->dev_specific_config[dev->index - 0xf0][dev->ldn] = val;
                nsc366_ldn_redirect(dev);
                break;

            default:
                break;
        }
    else
        dev->index = val;
}

static uint8_t
nsc366_read(uint16_t addr, void *priv)
{
    const nsc366_t *dev = (nsc366_t *) priv;

    if (addr & 1) {
        switch (dev->index) {
            case 0x07:
                return dev->ldn;

            case 0x20 ... 0x2d:
                return dev->sio_config[dev->index - 0x20];

            case 0x30:
                return dev->ld_activate[dev->ldn];

            case 0x60 ... 0x61:
                return dev->io_base0[dev->index & 1][dev->ldn];

            case 0x62 ... 0x63:
                return dev->io_base1[dev->index & 1][dev->ldn];

            case 0x70:
                return dev->int_num_irq[dev->ldn];

            case 0x71:
                return dev->irq[dev->ldn];

            case 0x74:
                return dev->dma_select0[dev->ldn];

            case 0x75:
                return dev->dma_select1[dev->ldn];

            case 0xf0 ... 0xf2:
                return dev->dev_specific_config[dev->index - 0xf0][dev->ldn];

            default:
                return 0;
        }
    } else
        return dev->index;
}

static void
nsc366_reset(void *priv)
{
    nsc366_t *dev = (nsc366_t *) priv;

    /* Basic Configuration */
    dev->ldn        = 0;
    dev->siofc_lock = 0;
    memset(dev->sio_config, 0, sizeof(dev->sio_config));
    memset(dev->ld_activate, 0, sizeof(dev->ld_activate));
    memset(dev->io_base0, 0, sizeof(dev->io_base0));
    memset(dev->io_base1, 0, sizeof(dev->io_base1));
    memset(dev->int_num_irq, 0, sizeof(dev->int_num_irq));
    memset(dev->irq, 0, sizeof(dev->irq));
    memset(dev->dma_select0, 0, sizeof(dev->dma_select0));
    memset(dev->dma_select1, 0, sizeof(dev->dma_select1));
    memset(dev->dev_specific_config, 0, sizeof(dev->dev_specific_config));

    /* SIO Config */
    dev->sio_config[0x00] = 0xe9; /* National Semiconductor NSC366 */
    dev->sio_config[0x07] = 0x01;

    /* FDC */
    fdc_reset(dev->fdc);
    dev->io_base0[0][0]            = 0x03;
    dev->io_base0[1][0]            = 0xf2;
    dev->int_num_irq[0]            = 0x06;
    dev->irq[0]                    = 0x03;
    dev->dma_select0[0]            = 0x02;
    dev->dma_select1[0]            = 0x04;
    dev->dev_specific_config[0][0] = 0x24;

    nsc366_fdc(dev);

    /* LPT */
    dev->io_base0[0][1]            = 0x02;
    dev->io_base0[1][1]            = 0x78;
    dev->int_num_irq[1]            = 0x07;
    dev->irq[1]                    = 0x02;
    dev->dma_select0[1]            = 0x04;
    dev->dma_select1[1]            = 0x04;
    dev->dev_specific_config[0][1] = 0xf2;

    /* UART Serial 2 */
    dev->io_base0[0][2]            = 0x02;
    dev->io_base0[1][2]            = 0xf8;
    dev->int_num_irq[2]            = 0x03;
    dev->irq[2]                    = 0x03;
    dev->dma_select0[2]            = 0x04;
    dev->dma_select1[2]            = 0x04;
    dev->dev_specific_config[0][2] = 0x02;

    nsc366_uart(1, dev);

    /* UART Serial 1 */
    dev->io_base0[0][3]            = 0x03;
    dev->io_base0[1][3]            = 0xf8;
    dev->int_num_irq[3]            = 0x04;
    dev->irq[3]                    = 0x03;
    dev->dma_select0[3]            = 0x04;
    dev->dma_select1[3]            = 0x04;
    dev->dev_specific_config[0][3] = 0x02;

    /* SWC */
    dev->irq[4]         = 0x03;
    dev->dma_select0[4] = 0x04;

    /* Keyboard Controller */
    dev->int_num_irq[5] = 0x0c;
    dev->irq[5]         = 0x02;

    /* Mouse Controller */
    dev->io_base0[1][6]            = 0x60;
    dev->io_base1[1][6]            = 0x64;
    dev->int_num_irq[6]            = 0x01;
    dev->irq[6]                    = 0x02;
    dev->dma_select0[6]            = 0x04;
    dev->dma_select1[6]            = 0x04;
    dev->dev_specific_config[0][6] = 0x40;

    /* GPIO */
    dev->irq[7]         = 0x03;
    dev->dma_select0[7] = 0x04;
    dev->dma_select1[7] = 0x04;

    /* ACB */
    dev->irq[8]         = 0x03;
    dev->dma_select0[8] = 0x04;
    dev->dma_select1[8] = 0x04;

    /* Fan Speed Monitor & Control */
    dev->irq[9]         = 0x03;
    dev->dma_select0[9] = 0x04;
    dev->dma_select1[9] = 0x04;
    nsc366_fscm_enable(dev);
    nsc366_fscm(dev);

    /* Voltage Level Monitor */
    dev->irq[13]         = 0x03;
    dev->dma_select0[13] = 0x04;
    dev->dma_select1[13] = 0x04;
    nsc366_vlm(dev);

    /* Temperature Monitor */
    dev->irq[14]         = 0x03;
    dev->dma_select0[14] = 0x04;
    dev->dma_select1[14] = 0x04;
    nsc366_tms(dev);
}

static void
nsc366_close(void *priv)
{
    nsc366_t *dev = (nsc366_t *) priv;

    free(dev);
}

static void *
nsc366_init(const device_t *info)
{
    nsc366_t *dev = (nsc366_t *) malloc(sizeof(nsc366_t));
    memset(dev, 0, sizeof(nsc366_t));

    io_sethandler(info->local, 2, nsc366_read, NULL, NULL, nsc366_write, NULL, NULL, dev); /* Ports 2E-2Fh(4E-4Fh if BADDR High): National Semiconductor NSC366 */

    /* FDC */
    dev->fdc = device_add(&fdc_at_nsc_device);

    /* Hardware Monitor Setup */
    dev->hwm = device_add(&nsc366_hwm_device);

    /* Keyboard Controller */
    device_add(&keyboard_ps2_ami_pci_device);

    /* Port 92h */
    device_add(&port_92_pci_device);

    /* Serial */
    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    nsc366_reset(dev);

    return dev;
}

const device_t nsc366_device = {
    .name          = "National Semiconductor NSC366",
    .internal_name = "nsc366",
    .flags         = 0,
    .local         = 0x2e,
    .init          = nsc366_init,
    .close         = nsc366_close,
    .reset         = nsc366_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t nsc366_4f_device = {
    .name          = "National Semiconductor NSC366 (With BADDR Pin High)",
    .internal_name = "nsc366",
    .flags         = 0,
    .local         = 0x4e,
    .init          = nsc366_init,
    .close         = nsc366_close,
    .reset         = nsc366_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
