/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ITE IT8702
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2022 Tiseno100.
 *
 */

// NOTE: We don't really utilize fans because our emulated board doesn't use them.
// NOTE 2: LPT & Serial are aligned on a different way due to CUSL2-C not using them properly.

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

#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdd_common.h>
#include <86box/port_92.h>

#include <86box/sio.h>

typedef struct
{
    int ldn, unlock;

    uint8_t	index, regs[15], sw_lock,
            enable[11],
            b_addr[4][11],
            irq[11],
            dma[11],
            d_spec[15][11];

    fdc_t *fdc;
    serial_t *uart[2];
} it8702_t;

#ifdef ENABLE_IT8702_LOG
int it8702_do_log = ENABLE_IT8702_LOG;
static void
it8702_log(const char *fmt, ...)
{
    va_list ap;

    if (it8702_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define it8702_log(fmt, ...)
#endif

static void it8702_reset(void *priv);


static void
it8702_unlock(uint8_t val, it8702_t *dev)
{
    if(val == 0x87) { /* The unlock mechanism of the CUSL2-C doesn't work as intended so we just unlock it with 87h only for now */
        it8702_log("IT8702 Unlock: Unlocking\n");
        dev->unlock = 1;
    }
}

static void
it8702_sw_lock(uint8_t val, it8702_t *dev)
{
    if(val & 0x80)
        dev->sw_lock = val;
    else
        dev->sw_lock = 0;
}

static void
it8702_fdc(it8702_t *dev)
{
    uint16_t base = ((dev->b_addr[0][0] & 0x0f) << 8) | (dev->b_addr[1][0] & 0xf8);
    int irq = dev->irq[0];
    int dma = dev->dma[0];
    fdc_remove(dev->fdc);

    if(dev->enable[0] & 1) {
        it8702_log("IT8702 FDC: Enabled with Base: 0x%x IRQ: %d and DMA: %d\n", base, irq, dma);
        fdc_set_base(dev->fdc, base);
        fdc_set_irq(dev->fdc, irq);
        fdc_set_dma_ch(dev->fdc, dma);

        if(dev->d_spec[0][0] & 1)
            fdc_writeprotect(dev->fdc);

        fdc_update_drvrate(dev->fdc, 0, dev->d_spec[1][0] & 3);
        fdc_update_drvrate(dev->fdc, 1, (dev->d_spec[1][0] >> 2) & 3);
    }
}

static void
it8702_uart(int uart, it8702_t *dev)
{
    uint16_t base = ((dev->b_addr[0][2 + uart] & 0x0f) << 8) | (dev->b_addr[1][2 + uart] & 0xf8);
    int irq = dev->irq[2 + uart];
    serial_remove(dev->uart[uart]);

    if(dev->enable[2 + uart])
    {
        it8702_log("IT8702 Serial %c: Enabled with Base: 0x%x IRQ: %d\n", 'A' + uart, base, irq);
        serial_setup(dev->uart[uart], base, irq);
        serial_set_clock_src(dev->uart[uart], 24444444 / ((dev->d_spec[0][2 + uart] >> 1) ? 12 : 13));
    }
}

static void
it8702_lpt(it8702_t *dev)
{
    uint16_t base = ((dev->b_addr[0][1] & 0x0f) << 8) | (dev->b_addr[1][1] & 0xf8);
    int irq = dev->irq[1];
    lpt1_remove();
    lpt2_remove();

    if(dev->enable[1] & 1) {
        it8702_log("IT8702 LPT1: Enabled with Base: 0x%x IRQ: %d\n", base, irq);
        lpt1_init(base);
        lpt1_irq(irq);
        lpt2_irq(irq);
    }
}

static void
it8702_ldn(it8702_t *dev)
{
    switch(dev->ldn)
    {
        case 0:
            it8702_fdc(dev);
        break;

        case 1:
            it8702_lpt(dev);
        break;

        case 2 ... 3:
            it8702_uart((dev->ldn == 3), dev);
        break;


    }
}

static void
it8702_write(uint16_t addr, uint8_t val, void *priv)
{
    it8702_t *dev = (it8702_t *) priv;

    if(addr == 0x2e) {
        if(dev->unlock)
            dev->index = val;
        else
            it8702_unlock(val, dev);
    }
    else if(addr == 0x2f) {
        switch(dev->index)
        {
            /* Global Registers */
            case 0x02: /* Configure Control */
                if(val & 2) {
                    it8702_log("IT8702 Unlock: Locking\n");
                    dev->unlock = 0;
                }

                if(val & 1) {
                    it8702_log("IT8702: Resetting\n");
                    it8702_reset(dev);
                }
            break;

            case 0x07:
                dev->ldn = val;
            break;

            case 0x23:
                if(!(dev->sw_lock & 0x80))
                    dev->regs[dev->index - 0x20] = val & 0xd9;
            break;

            case 0x24:
                dev->regs[dev->index - 0x20] = val & 0x00;
            break;

            case 0x25 ... 0x28:
                if(dev->ldn == 7)
                    dev->regs[dev->index - 0x20] = val;
            break;

            case 0x29:
                if(dev->ldn == 7)
                    dev->regs[dev->index - 0x20] = val & 0x3f;
            break;

            case 0x2a:
                if(dev->ldn == 7)
                    dev->regs[dev->index - 0x20] = val & 0x7f;
            break;

            case 0x2b:
                if(!(dev->sw_lock & 0x80)) {
                    dev->regs[dev->index - 0x20] = val;
                    it8702_sw_lock(val, dev);
                }
            break;

            case 0x2c:
                dev->regs[dev->index - 0x20] = val & 0x1c;
            break;

            case 0x30:
                if(dev->ldn < 11)
                    dev->enable[dev->ldn] = val & 1;

                it8702_ldn(dev);
            break;

            case 0x60 ... 0x63:
                if(dev->ldn < 11)
                    dev->b_addr[dev->index & 3][dev->ldn] = val;

                it8702_ldn(dev);
            break;

            case 0x70:
                if(dev->ldn < 11)
                    dev->irq[dev->ldn] = val & 0x0f;

                it8702_ldn(dev);
            break;

            case 0x74:
                if(dev->ldn < 11)
                    dev->dma[dev->ldn] = val & 0x0f;

                it8702_ldn(dev);
            break;

            case 0xf0 ... 0xf0:
                if(dev->ldn < 11)
                    dev->d_spec[dev->index & 0x0f][dev->ldn] = val;

                it8702_ldn(dev);
            break;
        }
    }
}


static uint8_t
it8702_read(uint16_t addr, void *priv)
{
    it8702_t *dev = (it8702_t *) priv;

    if(addr == 0x2e) {
        return dev->index;
    } else if(addr == 0x2f) {
        if(dev->index == 0x02) { /* The Configure Control register is Write Only */
            return 0xff;
		} else {
            switch(dev->index) {
                case 0x07:
                    return dev->ldn;
                
                case 0x20 ... 0x2f:
                    return dev->regs[dev->index - 0x20];
                
                case 0x30:
                    if(dev->ldn < 11)
                        return dev->enable[dev->ldn];
                    else
                        return 0xff;
                
                case 0x60 ... 0x63:
                    if(dev->ldn < 11)
                        return dev->b_addr[dev->index & 3][dev->ldn];
                    else
                        return 0xff;
                
                case 0x70:
                    if(dev->ldn < 11)
                        return dev->irq[dev->ldn];
                    else
                        return 0xff;

                case 0x74:
                    if(dev->ldn < 11)
                        return dev->dma[dev->ldn];
                    else
                        return 0xff;

                case 0xf0 ... 0xff:
                    if(dev->ldn < 11)
                        return dev->d_spec[dev->index & 0x0f][dev->ldn];
                    else
                        return 0xff;

                default:
                    return 0xff;
            }
        }
    }

    return 0xff;
}


static void
it8702_reset(void *priv)
{
    it8702_t *dev = (it8702_t *) priv;

    dev->ldn = 0;
    dev->sw_lock = 0; // Needs implementation

    dev->unlock = 0; /* Lock the chip */

    /* Global Super I/O Registers */
    dev->regs[0x00] = 0x87;
    dev->regs[0x01] = 0x02;
    dev->regs[0x02] = 0x07;

    /* Floppy Disk Controller */
    dev->b_addr[0][0] = 0x03;
    dev->b_addr[1][0] = 0xf0;
    dev->irq[0] = 0x06;
    dev->dma[0] = 0x02;
    fdc_reset(dev->fdc);
    it8702_fdc(dev);

    /* LPT */
    dev->b_addr[0][1] = 0x03;
    dev->b_addr[1][1] = 0x78;
    dev->irq[1] = 0x07;
    dev->dma[1] = 0x03;
    dev->d_spec[0][1] = 0x03;
    it8702_lpt(dev);

    /* UART Serial A */
    dev->b_addr[0][2] = 0x02;
    dev->b_addr[1][2] = 0xf8;
    dev->irq[2] = 0x04;
    dev->d_spec[1][2] = 0x50;
    dev->d_spec[3][2] = 0x7f;
    it8702_uart(0, dev);

    /* UART Serial B */
    dev->b_addr[0][3] = 0x02;
    dev->b_addr[1][3] = 0x78;
    dev->irq[3] = 0x04;
    dev->d_spec[1][3] = 0x50;
    dev->d_spec[3][3] = 0x7f;
    it8702_uart(1, dev);

    /* FAN Handler */
    dev->b_addr[0][4] = 0x02;
    dev->b_addr[1][4] = 0x90;
    dev->b_addr[2][4] = 0x02;
    dev->b_addr[3][4] = 0x30;
    dev->irq[4] = 0x09;

    /* Keyboard Controller */
    dev->b_addr[3][5] = 0x64;
    dev->irq[5] = 0x01;

    /* PS/2 Mouse */
    dev->irq[6] = 0x0c;

    /* MIDI */
    dev->b_addr[0][8] = 0x03;
    dev->irq[8] = 0x0a;

    /* Gameport */
    dev->b_addr[0][9] = 0x02;
    dev->b_addr[1][9] = 0x10;

    /* IR */
    dev->b_addr[0][10] = 0x03;
    dev->b_addr[1][10] = 0x10;
    dev->irq[10] = 0x0b;
}


static void
it8702_close(void *priv)
{
    it8702_t *dev = (it8702_t *) priv;

    free(dev);
}


static void *
it8702_init(const device_t *info)
{
    it8702_t *dev = (it8702_t *) malloc(sizeof(it8702_t));
    memset(dev, 0, sizeof(it8702_t));

    /* FDC */
    dev->fdc = device_add(&fdc_at_smc_device);

    /* Keyboard Controller */
    device_add(&keyboard_ps2_ami_pci_device);

    /* Port 92h */
    device_add(&port_92_pci_device);

    /* Serial */
    dev->uart[0] = device_add_inst(&ns16650_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    io_sethandler(info->local, 2, it8702_read, NULL, NULL, it8702_write, NULL, NULL, dev); /* Ports 2Eh-2Fh: ITE IT8702 */

    it8702_reset(dev);

    return dev;
}

const device_t it8702_device = {
    .name = "ITE IT8702",
    .internal_name = "it8702",
    .flags = 0,
    .local = 0x2e,
    .init = it8702_init,
    .close = it8702_close,
    .reset = it8702_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
