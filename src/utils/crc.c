/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CRC implementation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/nvr.h>
#include <86box/random.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/crc.h>

#ifdef ENABLE_CRC_LOG
int d86f_do_log = ENABLE_CRC_LOG;

static void
crc_log(const char *fmt, ...)
{
    va_list ap;

    if (crc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define crc_log(fmt, ...)
#endif

void
crc16_setup(uint16_t *crc_table, uint16_t poly)
{
    int      c = 256;
    int      bc;
    uint16_t temp;

    while (c--) {
        temp = c << 8;
        bc   = 8;

        while (bc--) {
            if (temp & 0x8000)
                temp = (temp << 1) ^ poly;
            else
                temp <<= 1;

            crc_table[c] = temp;
        }
    }
}

void
crc16_calc(uint16_t *crc_table, uint8_t byte, crc_t *crc_var)
{
    crc_var->word = (crc_var->word << 8) ^
                    crc_table[(crc_var->word >> 8) ^ byte];
}
