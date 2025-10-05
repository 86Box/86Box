/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the AST Bravo MS jumper readout.
 *
 *
 *
 * Authors: win2kgamer
 *
 *          Copyright 2025 win2kgamer
 */

#ifdef ENABLE_AST_READOUT_LOG
#include <stdarg.h>
#endif

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
#include <86box/chipset.h>
#include <86box/plat_unused.h>
#include <86box/lpt.h>
#include <86box/machine.h>
#include <86box/log.h>

/*
    The AST readout device has multiple indexed registers that handle
    jumper readout, software ECP DMA configuration and other unknown functions.

    Register 0x00:
    Bits 6-4 = ECP DMA configuration
        010 (0x02) = DMA 0
        101 (0x05) = DMA 1
        111 (0x07) = DMA 3

    Register 0x03:
    Bit 7 = Force flash
    Bit 6 = Password disable
    Bit 5 = Mono/Color primary video (0=Color/1=Mono)
    Bit 4 = Setup disable (0=Enable Setup/1=Disable Setup)
    Bit 3 = Enable onboard video (0=Enable/1=Disable)
    Bit 2 = ????
    Bit 1 = ????
    Bit 0 = ????
*/

typedef struct ast_readout_t {
    uint8_t index;
    uint8_t jumper[4];

    void * log; // New logging system
} ast_readout_t;

#ifdef ENABLE_AST_READOUT_LOG
int ast_readout_do_log = ENABLE_AST_READOUT_LOG;

static void
ast_readout_log(void *priv, const char *fmt, ...)
{
    if (ast_readout_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ast_readout_log(fmt, ...)
#endif

static void
ast_readout_write(uint16_t port, uint8_t val, void *priv)
{
    ast_readout_t *dev = (ast_readout_t *) priv;
    switch (port) {
        case 0xE0:
            ast_readout_log(dev->log, "[%04X:%08X] AST Bravo Readout: Set Index %02X\n", CS, cpu_state.pc, val);
            dev->index = val;
            break;
        case 0xE1:
            ast_readout_log(dev->log, "[%04X:%08X] AST Bravo Readout: Write %02X:%02X\n", CS, cpu_state.pc, dev->index, val);
            if ((dev->index == 0x00) && (!strcmp(machine_get_internal_name(), "bravoms586"))) {
                uint8_t dmaval = ((val >> 4) & 0x07);
                dev->jumper[dev->index] = val;
                switch (dmaval) {
                    case 0x02:
                        ast_readout_log(dev->log, "ECP DMA set to 0\n");
                        lpt1_dma(0);
                        break;
                    case 0x05:
                        ast_readout_log(dev->log, "ECP DMA set to 1\n");
                        lpt1_dma(1);
                        break;
                    case 0x07:
                        ast_readout_log(dev->log, "ECP DMA set to 3\n");
                        lpt1_dma(3);
                        break;
                    default:
                        ast_readout_log(dev->log, "Unknown ECP DMA!\n");
                        break;
                }
            } else if (dev->index == 0x03) {
                dev->jumper[dev->index] = (val & 0x07);
                if (gfxcard[0] != 0x01)
                    dev->jumper[dev->index] |= 0x08;
            }
            else
                dev->jumper[dev->index] = val;
            break;
        default:
            break;
    }
}

static uint8_t
ast_readout_read(uint16_t port, void *priv)
{
    const ast_readout_t *dev = (ast_readout_t *) priv;
    uint8_t          ret = 0xff;

    switch (port) {
        case 0xE0:
            ast_readout_log(dev->log, "[%04X:%08X] AST Bravo Readout: Read Index %02X\n", CS, cpu_state.pc, dev->index);
            ret = dev->index;
            break;
        case 0xE1:
            ast_readout_log(dev->log, "[%04X:%08X] AST Bravo Readout: Read %02X:%02X\n", CS, cpu_state.pc, dev->index, dev->jumper[dev->index]);
            ret = dev->jumper[dev->index];
            break;
        default:
            break;
    }
    return ret;
}

static void
ast_readout_reset(void *priv)
{
    ast_readout_t *dev = (ast_readout_t *) priv;

    dev->jumper[0x03] = 0x06;
    if (gfxcard[0] != 0x01)
        dev->jumper[0x03] |= 0x08;
}

static void
ast_readout_close(void *priv)
{
    ast_readout_t *dev = (ast_readout_t *) priv;

    if (dev->log != NULL) {
        log_close(dev->log);
        dev->log = NULL;
    }

    free(dev);
}

static void *
ast_readout_init(const device_t *info)
{
    ast_readout_t *dev = (ast_readout_t *) calloc(1, sizeof(ast_readout_t));

    dev->log = log_open("AST Readout");

    ast_readout_reset(dev);

    io_sethandler(0x00E0, 0x0002, ast_readout_read, NULL, NULL, ast_readout_write, NULL, NULL, dev);

    return dev;
}

const device_t ast_readout_device = {
    .name          = "AST Bravo MS Readout",
    .internal_name = "ast_readout",
    .flags         = 0,
    .local         = 0,
    .init          = ast_readout_init,
    .close         = ast_readout_close,
    .reset         = ast_readout_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
