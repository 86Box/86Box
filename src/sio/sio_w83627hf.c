/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Winbond W83627HF chipset.
 *
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020,2021 Tiseno100.
 *		Copyright 2021,2021 Miran Grca.
 */

/*

Winbond W83627HF Register Summary for ISAPnP (potentially)

Found on: https://github.com/koitsu/bsdhwmon/issues/6

Register dump:
idx 02 20 21 22 23 24 25 26  28 29 2a 2b 2c 2e 2f
val ff 52 41 ff fe c4 00 00  00 50 fc 00 ff 00 ff
def 00 52 NA ff 00 MM 00 00  00 00 7c c0 00 00 00
LDN 0x00 (Floppy)
idx 30 60 61 70 74 f0 f1 f2  f4 f5
val 00 00 00 06 02 0e 00 ff  00 00
def 01 03 f0 06 02 0e 00 ff  00 00
LDN 0x01 (Parallel port)
idx 30 60 61 70 74 f0
val 00 00 00 07 03 3f
def 01 03 78 07 04 3f
LDN 0x02 (COM1)
idx 30 60 61 70 f0
val 01 03 f8 04 00
def 01 03 f8 04 00
LDN 0x03 (COM2)
idx 30 60 61 70 f0 f1
val 01 02 f8 03 00 40
def 01 02 f8 03 00 00
LDN 0x05 (Keyboard)
idx 30 60 61 62 63 70 72 f0
val 01 00 60 00 64 01 00 80
def 01 00 60 00 64 01 0c 80
LDN 0x06 (Consumer IR)
idx 30 60 61 70
val 00 00 00 00
def 00 00 00 00
LDN 0x07 (Game port, MIDI port, GPIO 1)
idx 30 60 61 62 63 70 f0 f1  f2
val 01 00 00 00 00 00 ff ff  00
def 00 02 01 03 30 09 ff 00  00
LDN 0x08 (GPIO 2, watchdog timer)
idx 30 f0 f1 f2 f3 f5 f6 f6  f7
val 00 ff ff ff 00 08 00 00  c0
def 00 ff 00 00 00 00 00 00  00
LDN 0x09 (GPIO 3)
idx 30 f0 f1 f2 f3
val 01 ff 14 00 40
def 00 ff 00 00 00
LDN 0x0a (ACPI)
idx 30 70 e0 e1 e2 e3 e4 e5  e6 e7 f0 f1 f3 f4 f6 f7  f9 fe ff
val 01 00 00 00 f2 00 40 00  00 00 00 af 32 00 00 00  00 00 00
def 00 00 00 00 NA NA 00 00  00 00 00 00 00 00 00 00  00 00 00
LDN 0x0b (Hardware monitor)
idx 30 60 61 70 f0
val 01 02 95 00 01
def 00 00 00 00 00
Hardware monitor (0x029a)

*/

/*

Notes : ISAPnP is missing and the Hardware Monitor I2C is not implemented.

*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/i2c.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/port_92.h>
#include <86box/serial.h>

#include <86box/hwm.h>
#include <86box/sio.h>

#ifdef ENABLE_W83627HF_LOG
int w83627hf_do_log = ENABLE_W83627HF_LOG;


static void
w83627hf_log(const char *fmt, ...)
{
    va_list ap;

    if (w83627hf_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define w83627hf_log(fmt, ...)
#endif


typedef struct
{
    uint8_t hwm_index, hwm_regs[256];

    uint8_t	index, cfg_unlocked,
		    regs[48], dev_regs[12][256];

    int has_hwm;
    fdc_t *fdc_controller;
    port_92_t *port_92;
    serial_t *uart[2];
} w83627hf_t;

/* These differ per board and must be programmed manually */
int fan1_rpm, fan2_rpm, fan3_rpm, vcorea_voltage, vcoreb_voltage;

void
w83627hf_stabilizer(int vcoreb, int fan1, int fan2, int fan3)
{
    vcoreb_voltage = vcoreb;
    fan1_rpm = fan1;
    fan2_rpm = fan2;
    fan3_rpm = fan3;
}

static void
w83627hf_hwm_write(uint16_t addr, uint8_t val, void *priv)
{
    w83627hf_t *dev = (w83627hf_t *)priv;

    switch(addr)
    {
        case 0x295:
            dev->hwm_index = val;
        break;

        case 0x296:
            w83627hf_log("W83627HF-HWM: dev->regs[%02x] = %02x\n", dev->hwm_index, val);
            switch(dev->hwm_index)
            {
                case 0x2b ... 0x3f:
                case 0x6b ... 0x7f:
                     dev->hwm_regs[dev->hwm_index & 0x1f] = val;
                break;

                case 0x40:
                    dev->hwm_regs[dev->hwm_index] = val & 0x8b;
                break;

                case 0x43:
                    dev->hwm_regs[dev->hwm_index] = val;
                break;

                case 0x44:
                    dev->hwm_regs[dev->hwm_index] = val & 0x3f;
                break;

                case 0x46:
                    dev->hwm_regs[dev->hwm_index] = val & 0x80;
                        if(val & 0x80)
                            dev->hwm_regs[dev->hwm_index] &= 0x10;
                break;

                case 0x47:
                    dev->hwm_regs[dev->hwm_index] = val & 0x3f;
                break;

                case 0x48: /* Serial Bus Address */
                    dev->hwm_regs[dev->hwm_index] = val & 0x7f;
                break;

                case 0x49:
                    dev->hwm_regs[dev->hwm_index] = val & 1;
                break;

                case 0x4a:
                    dev->hwm_regs[dev->hwm_index] = val;
                break;

                case 0x4b:
                    dev->hwm_regs[dev->hwm_index] = val & 0xfc;
                break;

                case 0x4c:
                    dev->hwm_regs[dev->hwm_index] = val & 0x5c;
                break;

                case 0x4d:
                    dev->hwm_regs[dev->hwm_index] = val & 0xbf;
                break;

                case 0x4e:
                    dev->hwm_regs[dev->hwm_index] = val;
                break;

                case 0x56:
                    dev->hwm_regs[dev->hwm_index] = val;
                break;

                case 0x57:
                    dev->hwm_regs[dev->hwm_index] = val & 0xbf;
                break;

                case 0x59:
                    dev->hwm_regs[dev->hwm_index] = val & 0x70;
                break;

                case 0x5a ... 0x5b:
                    dev->hwm_regs[dev->hwm_index] = val;
                break;

                case 0x5c:
                    dev->hwm_regs[dev->hwm_index] = val & 0x77;
                break;
            }
        break;
    }
}

static uint8_t
w83627hf_hwm_read(uint16_t addr, void *priv)
{
    w83627hf_t *dev = (w83627hf_t *)priv;

    switch(addr)
    {
        case 0x295:
            return dev->hwm_index;

        case 0x296:
            switch(dev->hwm_index)
            {
                case 0x20 ... 0x3f:
                case 0x60 ... 0x7f:
                    switch(dev->hwm_index & 0x1f)
                    {
                        case 0x00: /* VCOREA */
                            return hwm_get_vcore() + 0x78;

                        case 0x01: /* VCOREB */
                            return vcoreb_voltage;

                        case 0x02: /* +3.3V */
                            return 0xd0;

                        case 0x03: /* +5V */
                            return 0xb9;

                        case 0x04: /* +12V */
                            return 0xc4;

                        case 0x05: /* -12V */
                            return 0x23;

                        case 0x06: /* -5V */
                            return 0x34;

                        case 0x08: /* Fan 1 */
                            return fan1_rpm;

                        case 0x09: /* Fan 2 */
                            return fan2_rpm;

                        case 0x0a: /* Fan 3 */
                            return fan3_rpm;

                        case 0x0b ... 0x1f:
                            return dev->hwm_regs[dev->hwm_index & 0x1f];
                    }

                case 0x4f:
                    if(dev->hwm_regs[0x4e] & 0x80)
                        return 0x5c;
                    else
                        return 0xa3;

                case 0x40 ... 0x4e:
                case 0x50 ... 0x5c:
                    return dev->hwm_regs[dev->hwm_index];

                default:
                    return 0xff;
            }

        default:
            return 0xff;
    }
}

static void
w83627hf_fdc_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{

    fdc_remove(dev->fdc_controller);

    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[0][cur_reg] = val & 1;
        break;

        case 0x60 ... 0x61:
            dev->dev_regs[0][cur_reg] = val;
        break;

        case 0x70:
            dev->dev_regs[0][cur_reg] = val & 0x0f;
        break;

        case 0x74:
            dev->dev_regs[0][cur_reg] = val & 7;
        break;

        case 0xf0:
            dev->dev_regs[0][cur_reg] = val;
        break;

        case 0xf1:
            dev->dev_regs[0][cur_reg] = val;
            fdc_update_boot_drive(dev->fdc_controller, (val & 0xc0) >> 6);

            if(val & 2)
                fdc_writeprotect(dev->fdc_controller);

            fdc_set_swwp(dev->fdc_controller, val & 1);
        break;

        case 0xf2:
            dev->dev_regs[0][cur_reg] = val;
        break;

        case 0xf4:
        case 0xf5:
            dev->dev_regs[0][cur_reg] = val & 0x5b;
            fdc_update_drvrate(dev->fdc_controller, cur_reg & 1, (val & 0x18) >> 3);
        break;
    }

    if(dev->dev_regs[0][0x30] & 1)
    {
        fdc_set_irq(dev->fdc_controller, dev->dev_regs[0][0x70]);
        fdc_set_dma_ch(dev->fdc_controller, dev->dev_regs[0][0x74]);
        fdc_set_base(dev->fdc_controller, (dev->dev_regs[0][0x60] << 8) | (dev->dev_regs[0][0x61]));

        w83627hf_log("W83627HF-FDC: BASE: %04x IRQ: %d DMA: %d\n", (dev->dev_regs[0][0x60] << 8) | (dev->dev_regs[0][0x61]), dev->dev_regs[0][0x70], dev->dev_regs[0][0x74]);
    }
}

static void
w83627hf_lpt_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    lpt1_remove();

    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[1][cur_reg] = val & 1;
        break;

        case 0x60 ... 0x61:
            dev->dev_regs[1][cur_reg] = val;
        break;

        case 0x70:
            dev->dev_regs[1][cur_reg] = val & 0x0f;
        break;

        case 0xf0:
            dev->dev_regs[1][cur_reg] = val & 0x7f;
        break;
    }

    if(dev->dev_regs[1][0x30] & 1)
    {
        lpt1_init((dev->dev_regs[1][0x60] << 8) | (dev->dev_regs[1][0x61]));
        lpt1_irq(dev->dev_regs[1][0x70]);
        w83627hf_log("W83627HF-LPT: BASE: %04x IRQ: %d\n", (dev->dev_regs[1][0x60] << 8) | (dev->dev_regs[1][0x61]), dev->dev_regs[1][0x70]);
    }
}

static void
w83627hf_uart_write(int uart, uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    double uart_clock = 24000000.0 / 13.0;

    serial_remove(dev->uart[uart]);

    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[2 + uart][cur_reg] = val & 1;
        break;

        case 0x60 ... 0x61:
            dev->dev_regs[2 + uart][cur_reg] = val;
        break;

        case 0x70:
            dev->dev_regs[2 + uart][cur_reg] = val & 0x0f;
        break;

        case 0xf0:
            dev->dev_regs[2 + uart][cur_reg] = val & 3;
            switch(val & 3)
            {
                case 0:
                    uart_clock = 24000000.0 / 13.0;
                break;

                case 1:
                    uart_clock = 24000000.0 / 12.0;
                break;

                case 2:
                    uart_clock = 24000000.0 / 1.625;
                break;

                case 3:
                    uart_clock = 24000000.0;
                break;
            }
        break;

        case 0xf1:
            if(uart)
                dev->dev_regs[2 + uart][cur_reg] = val & 0x7f;
        break;
    }

    if(dev->dev_regs[2 + uart][0x30] & 1)
    {
        serial_setup(dev->uart[uart], (dev->dev_regs[2 + uart][0x60] << 8) | (dev->dev_regs[2 + uart][0x61]), dev->dev_regs[2 + uart][0x70]);
        serial_set_clock_src(dev->uart[uart], uart_clock);
        w83627hf_log("W83627HF-UART%s: BASE: %04x IRQ: %d\n", uart ? "B" : "A", (dev->dev_regs[2 + uart][0x60] << 8) | (dev->dev_regs[2 + uart][0x61]), dev->dev_regs[2 + uart][0x70]);
    }
}

static void
w83627hf_kbc_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[5][cur_reg] = val & 1;
        break;

        case 0x60 ... 0x61: /* See Notes on init */
            dev->dev_regs[5][cur_reg] = val;
        break;

        case 0x62 ... 0x63: /* See Notes on init */
            dev->dev_regs[5][cur_reg] = val;
        break;

        case 0x70:
            dev->dev_regs[5][cur_reg] = val & 0x0f;
        break;

        case 0x72:
            dev->dev_regs[5][cur_reg] = val & 0x0f;
        break;

        case 0xf0:
            dev->dev_regs[5][cur_reg] = val & 0xc7;
        break;
    }

    if(dev->dev_regs[5][0x30] & 1)
    {
        /* We don't disable Port 92h as intended because the BIOSes never enable it back, causing issues. */
        port_92_set_features(dev->port_92, !!(dev->dev_regs[5][0xf0] & 1), !!(dev->dev_regs[5][0xf0] & 2));
        w83627hf_log("W83627HF-PORT92: FASTA20: %d FASTRESET: %d\n", !!(dev->dev_regs[5][0xf0] & 2), !!(dev->dev_regs[5][0xf0] & 1));
    }
}

static void
w83627hf_cir_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    /* Unimplemented Functionality */
    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[6][cur_reg] = val & 1;
        break;

        case 0x60 ... 0x61:
            dev->dev_regs[6][cur_reg] = val;
        break;

        case 0x70:
            dev->dev_regs[6][cur_reg] = val & 0x0f;
        break;
    }
}

static void
w83627hf_gameport_midi_gpio1_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[7][cur_reg] = val & 7;
        break;

        case 0x60 ... 0x63:
            dev->dev_regs[7][cur_reg] = val;
        break;

        case 0x70:
            dev->dev_regs[7][cur_reg] = val & 0x0f;
        break;

        case 0xf0 ... 0xf2:
            dev->dev_regs[7][cur_reg] = val;
        break;
    }
}

static void
w83627hf_watchdog_timer_gpio2_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[8][cur_reg] = val & 1;
        break;

        case 0xf0 ... 0xf2:
            dev->dev_regs[8][cur_reg] = val;
        break;

        case 0xf5:
            dev->dev_regs[8][cur_reg] = val & 0xcc;
        break;

        case 0xf6 ... 0xf7:
            dev->dev_regs[8][cur_reg] = val;
        break;
    }
}

static void
w83627hf_gpio3_vsb_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[9][cur_reg] = val & 1;
        break;

        case 0xf0 ... 0xf2:
            dev->dev_regs[9][cur_reg] = val;
        break;

        case 0xf3:
            dev->dev_regs[9][cur_reg] = val & 0xc0;
        break;
    }
}

static void
w83627hf_acpi_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[0x0a][cur_reg] = val & 1;
        break;

        case 0x70:
            dev->dev_regs[0x0a][cur_reg] = val & 0x0f;
        break;

        case 0xe0:
            dev->dev_regs[0x0a][cur_reg] = val & 0xc0;
        break;

        case 0xe1 ... 0xe2:
            dev->dev_regs[0x0a][cur_reg] = val;
        break;

        case 0xe4:
            dev->dev_regs[0x0a][cur_reg] = val & 0xfc;
        break;

        case 0xe5 ... 0xe6:
            dev->dev_regs[0x0a][cur_reg] = val & 0x7f;

            if(cur_reg == 0xe6)
                if(val & 0x40)
                    dev->hwm_regs[0x42] &= 0x10;
        break;

        case 0xe7:
            dev->dev_regs[0x0a][cur_reg] = val & 0x0f;
        break;

        case 0xf0:
            dev->dev_regs[0x0a][cur_reg] = val;
        break;

        case 0xf1:
            dev->dev_regs[0x0a][cur_reg] = val & 0xef;
        break;

        case 0xf3 ... 0xf4:
            dev->dev_regs[0x0a][cur_reg] &= val & 0x3f;
        break;

        case 0xf5:
        case 0xf7:
            dev->dev_regs[0x0a][cur_reg] = val & 0x3f;
        break;
    }
}

static void
w83627hf_hwm_lpc_write(uint16_t cur_reg, uint8_t val, w83627hf_t *dev)
{
    switch(cur_reg)
    {
        case 0x30:
            dev->dev_regs[0x0b][cur_reg] = val & 1;
        break;

        case 0x70:
            dev->dev_regs[0x0b][cur_reg] = val & 0x0f;
        break;

        case 0xf0:
            dev->dev_regs[0x0b][cur_reg] = val & 1;
        break;
    }
}

static void
w83627hf_hwm_reset(w83627hf_t *dev)
{
    /* W83627HF Hardware Monitor */
    dev->hwm_regs[0x40] = 1;
    dev->hwm_regs[0x47] = 0xa0;
    dev->hwm_regs[0x48] = 0x2d;
    dev->hwm_regs[0x49] = 1;
    dev->hwm_regs[0x4a] = 1;
    dev->hwm_regs[0x4b] = 0x44;
    dev->hwm_regs[0x4d] = 0x15;
    dev->hwm_regs[0x4e] = 0x80;
    dev->hwm_regs[0x57] = 0x80;
    dev->hwm_regs[0x58] = 0x21;
    dev->hwm_regs[0x59] = 0x70;
    dev->hwm_regs[0x5a] = 0xff;
    dev->hwm_regs[0x5b] = 0xff;
    dev->hwm_regs[0x5c] = 0x11;
}

static void
w83627hf_reset(void *priv)
{
    w83627hf_t *dev = (w83627hf_t *)priv;
    memset(dev->regs, 0, sizeof(dev->regs));
    dev->cfg_unlocked = 0;

    /* Proper W83627HF Registers */
    dev->regs[0x20] = 0x52;
    dev->regs[0x21] = 0x17;

    /* FDC Registers */
    dev->dev_regs[0][0x30] = 1;
    dev->dev_regs[0][0x60] = 3;
    dev->dev_regs[0][0x61] = 0xf0;
    dev->dev_regs[0][0x70] = 6;
    dev->dev_regs[0][0x74] = 2;
    dev->dev_regs[0][0xf0] = 0x0e;
    dev->dev_regs[0][0xf2] = 0xff;
    fdc_reset(dev->fdc_controller);
    w83627hf_fdc_write(0, 0, dev);

    /* LPT Registers */
    dev->dev_regs[1][0x30] = 1;
    dev->dev_regs[1][0x60] = 3;
    dev->dev_regs[1][0x61] = 0x78;
    dev->dev_regs[1][0x70] = 7;
    dev->dev_regs[1][0x74] = 4;
    dev->dev_regs[1][0xf0] = 0x3f;
    w83627hf_lpt_write(0, 0, dev);

    /* UART A Registers */
    dev->dev_regs[2][0x30] = 1;
    dev->dev_regs[2][0x60] = 3;
    dev->dev_regs[2][0x61] = 0xf8;
    dev->dev_regs[2][0x70] = 4;
    w83627hf_uart_write(0, 0, 0, dev);

    /* UART B Registers */
    dev->dev_regs[3][0x30] = 1;
    dev->dev_regs[3][0x60] = 2;
    dev->dev_regs[3][0x61] = 0xf8;
    dev->dev_regs[3][0x70] = 3;
    w83627hf_uart_write(0, 0, 0, dev);

    /* Keyboard Controller */
    dev->dev_regs[5][0x30] = 1;
    dev->dev_regs[5][0x61] = 0x60;
    dev->dev_regs[5][0x63] = 0x64;
    dev->dev_regs[5][0x70] = 1;
    dev->dev_regs[5][0x72] = 0x0c;
    dev->dev_regs[5][0xf0] = 0x80;
    w83627hf_kbc_write(0, 0, dev);

    /* CIR */
    dev->dev_regs[6][0x30] = 1;
    w83627hf_cir_write(0, 0, dev);

    /* Gameport, MIDI port and GPIO1 */
    dev->dev_regs[7][0x60] = 2;
    dev->dev_regs[7][0x61] = 1;
    dev->dev_regs[7][0x62] = 3;
    dev->dev_regs[7][0x63] = 0x30;
    dev->dev_regs[7][0x70] = 9;
    dev->dev_regs[7][0xf0] = 0xff;

    /* GPIO3, VSB */
    dev->dev_regs[9][0xf0] = 0xff;

    /* W83627HF Hardware Monitor */
    if(dev->has_hwm)
        w83627hf_hwm_reset(dev);
}

static void
w83627hf_write(uint16_t addr, uint8_t val, void *priv)
{
    w83627hf_t *dev = (w83627hf_t *)priv;

    switch(addr & 0x0f)
    {
        case 0x0e:
            if(!dev->cfg_unlocked)
            {
                dev->cfg_unlocked = (val == 0x87) && (dev->index == 0x87);
                dev->index = val;
            }
            else dev->index = val;
        break;

        case 0x0f:
            if(dev->cfg_unlocked)
                switch(dev->index)
                {
                    case 0x02: /* LDN */
                        if(val & 1)
                            w83627hf_reset(dev);
                        break;

                    case 0x07:
                        dev->regs[dev->index] = val;
                        break;

                    case 0x22: /* Manual Power Down */
                        dev->regs[dev->index] = val & 0x7f;
                        break;

                    case 0x23: /* Full Power Down */
                        dev->regs[dev->index] = val & 1;
                        break;

                    case 0x24:
                        dev->regs[dev->index] = val & 0xca;
                        break;

                    case 0x25:
                        dev->regs[dev->index] = val & 0x39;
                        break;

                    case 0x26:
                        dev->regs[dev->index] = val & 0xef;
                        break;

                    case 0x28:
                        dev->regs[dev->index] = val & 7;
                        break;

                    case 0x29:
                        dev->regs[dev->index] = val & 0xfc;
                        break;

                    case 0x2a:
                    case 0x2b:
                        dev->regs[dev->index] = val;
                    break;

                    case 0x30: /* Device Specific Registers */
                    case 0x60 ... 0x63:
                    case 0x70: case 0x72:case 0x74:
                    case 0xe0 ... 0xe7:
                    case 0xf0 ... 0xf6:
                        switch(dev->regs[7])
                        {
                            case 0: /* FDC */
                                w83627hf_fdc_write(dev->index, val, dev);
                            break;

                            case 1: /* LPT */
                                w83627hf_lpt_write(dev->index, val, dev);
                            break;

                            case 2: /* UART A */
                            case 3: /* UART B */
                                w83627hf_uart_write(dev->regs[7] & 1, dev->index, val, dev);
                            break;

                            case 5: /* KBC */
                                w83627hf_kbc_write(dev->index, val, dev);
                            break;

                            case 6: /* CIR */
                                w83627hf_cir_write(dev->index, val, dev);
                            break;

                            case 7: /* GAMEPORT, MIDI & GPIO1 */
                                w83627hf_gameport_midi_gpio1_write(dev->index, val, dev);
                            break;

                            case 8: /* WATCHDOG TIMER & GPIO2 */
                                w83627hf_watchdog_timer_gpio2_write(dev->index, val, dev);
                            break;

                            case 9: /* GPIO3 & VSB */
                                w83627hf_gpio3_vsb_write(dev->index, val, dev);
                            break;

                            case 0x0a: /* ACPI */
                                w83627hf_acpi_write(dev->index, val, dev);
                            break;

                            case 0x0b: /* HWM LPC */
                                w83627hf_hwm_lpc_write(dev->index, val, dev);
                            break;

                            default:
                                w83627hf_log("W83627HF: Writings to unknown LDN: %02x\n", dev->regs[7]);
                            break;
                        }
                    break;
                }
        break;
    }
}


static uint8_t
w83627hf_read(uint16_t addr, void *priv)
{
    w83627hf_t *dev = (w83627hf_t *)priv;

    if((dev->index >= 0x00) && (dev->index <= 0x2f))
        return dev->regs[dev->index];
    else if((dev->index >= 0x30) && (dev->index <= 0xff) && (dev->regs[7] >= 0) && (dev->regs[7] <= 0x0b))
        return dev->dev_regs[dev->regs[7]][dev->index];
    else
        return 0xff;
}


static void
w83627hf_close(void *priv)
{
    w83627hf_t *dev = (w83627hf_t *)priv;

    free(dev);
}


static void *
w83627hf_init(const device_t *info)
{
    w83627hf_t *dev = (w83627hf_t *)malloc(sizeof(w83627hf_t));
    memset(dev, 0, sizeof(w83627hf_t));

    /* Knock out the Hardware Monitor if needed(Mainly for ASUS TUSL2-C) */
    dev->has_hwm = info->local;

    /* I/O Ports */
    io_sethandler(0x002e, 2, w83627hf_read, NULL, NULL, w83627hf_write, NULL, NULL, dev);
    io_sethandler(0x004e, 2, w83627hf_read, NULL, NULL, w83627hf_write, NULL, NULL, dev);

    if(dev->has_hwm)
        io_sethandler(0x0295, 2, w83627hf_hwm_read, NULL, NULL, w83627hf_hwm_write, NULL, NULL, dev);

    /* Floppy Disk Controller */
    dev->fdc_controller = device_add(&fdc_at_smc_device);

    /* Hardware Monitor */
    fan1_rpm = fan2_rpm = fan3_rpm = vcorea_voltage = vcoreb_voltage = 0;

    /* Keyboard Controller (Based on AMIKEY-2) */
    /* Note: The base addresses and IRQ's of the Keyboard & PS/2 Mouse are remappable. Due to 86Box limitations we can't do that just yet */
    device_add(&keyboard_ps2_ami_pci_device);

    /* Port 92h */
    dev->port_92 = device_add(&port_92_device);

    /* UART */
    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    return dev;
}

const device_t w83627hf_device = {
    .name = "Winbond W83627HF",
    .internal_name = "w83627hf",
    .flags = 0,
    .local = 1,
    .init = w83627hf_init,
    .close = w83627hf_close,
    .reset = w83627hf_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t w83627hf_no_hwm_device = {
    .name = "Winbond W83627HF with no Hardware Monitor",
    .internal_name = "w83627hf_nohwm",
    .flags = 0,
    .local = 0,
    .init = w83627hf_init,
    .close = w83627hf_close,
    .reset = w83627hf_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
