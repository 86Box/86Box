/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Advanced Power Management emulation.
 *
 * Version:	@(#)apm.c	1.0.0	2019/05/12
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "cpu_new/cpu.h"
#include "device.h"
#include "io.h"


typedef struct
{
    uint8_t cmd,
	    stat;
} apm_t;


#ifdef ENABLE_APM_LOG
int apm_do_log = ENABLE_APM_LOG;


static void
apm_log(const char *fmt, ...)
{
    va_list ap;

    if (apm_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define apm_log(fmt, ...)
#endif


static void
apm_out(uint16_t port, uint8_t val, void *p)
{
    apm_t *apm = (apm_t *) p;

    apm_log("[%04X:%08X] APM write: %04X = %02X (BX = %04X, CX = %04X)\n", CS, cpu_state.pc, port, val, BX, CX);

    port &= 0x0001;

    if (port == 0x0000) apm->cmd = val;
	else apm->stat = val;

    smi_line = 1;
}


static uint8_t
apm_in(uint16_t port, void *p)
{
    apm_t *apm = (apm_t *) p;

    apm_log("[%04X:%08X] APM read: %04X = FF\n", CS, cpu_state.pc, port);

    port &= 0x0001;

    if (port == 0x0000)
	return apm->cmd;
    else
	return apm->stat;
}


static void
apm_close(void *p)
{
    apm_t *dev = (apm_t *)p;

    free(dev);
}


static void
*apm_init(const device_t *info)
{
    apm_t *apm = (apm_t *) malloc(sizeof(apm_t));
    memset(apm, 0, sizeof(apm_t));

    io_sethandler(0x00b2, 0x0002, apm_in, NULL, NULL, apm_out, NULL, NULL, apm);

    return apm;
}


const device_t apm_device =
{
    "Advanced Power Management",
    0,
    0,
    apm_init,
    apm_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
