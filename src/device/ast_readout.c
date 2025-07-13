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
#include <86box/chipset.h>
#include <86box/plat_unused.h>

/*
    Bit 7 = Force flash;
    Bit 6 = Password disable;
    Bit 5 = Mono/Color primary video (0=Color/1=Mono);
    Bit 4 = Setup disable (0=Enable Setup/1=Disable Setup);
    Bit 3 = Enable onboard video (0=Enable/1=Disable);
    Bit 2 = ????;
    Bit 1 = ????;
    Bit 0 = ????.
*/

typedef struct ast_readout_t {
    uint8_t jumper;
} ast_readout_t;

#ifdef ENABLE_AST_READOUT_LOG
int ast_readout_do_log = ENABLE_AST_READOUT_LOG;

static void
ast_readout_log(const char *fmt, ...)
{
    va_list ap;

    if (ast_readout_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ast_readout_log(fmt, ...)
#endif

static void
ast_readout_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    ast_readout_t *dev = (ast_readout_t *) priv;
    ast_readout_log("AST Bravo Readout: Write %02x\n", val);
    //dev->jumper = val;
}

static uint8_t
ast_readout_read(UNUSED(uint16_t addr), void *priv)
{
    const ast_readout_t *dev = (ast_readout_t *) priv;

    ast_readout_log("AST Bravo Readout: Read %02x\n", dev->jumper);
    return dev->jumper;
}

static void
ast_readout_reset(void *priv)
{
    ast_readout_t *dev = (ast_readout_t *) priv;

    dev->jumper = 0x06;
    if (gfxcard[0] != 0x01)
        dev->jumper |= 0x08;
}

static void
ast_readout_close(void *priv)
{
    ast_readout_t *dev = (ast_readout_t *) priv;

    free(dev);
}

static void *
ast_readout_init(const device_t *info)
{
    ast_readout_t *dev = (ast_readout_t *) calloc(1, sizeof(ast_readout_t));

    ast_readout_reset(dev);

    io_sethandler(0x00E1, 0x0001, ast_readout_read, NULL, NULL, ast_readout_write, NULL, NULL, dev);

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
