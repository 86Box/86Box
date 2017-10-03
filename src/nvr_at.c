/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		IBM PC/AT RTC/NVRAM ("CMOS") emulation.
 *
 *		The original PC/AT series had DS12885 series modules; later
 *		versions and clones used the 12886 and/or 1288(C)7 series,
 *		or the MC146818 series, all with an external battery. Many
 *		of those batteries would create corrosion issues later on
 *		in mainboard life...
 *
 * Version:	@(#)nvr_at.c	1.0.5	2017/10/02
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "ibm.h"
#include "cpu/cpu.h"
#include "io.h"
#include "device.h"
#include "machine/machine.h"
#include "nvr.h"


static void
nvr_write(uint16_t addr, uint8_t val, void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;

    if (! (addr & 1)) {
	nvr->addr = (val & nvr->mask);
#if 0
	nvr->nmi_mask = (~val & 0x80);
#endif

	return;
    }

    /* Write the chip's registers. */
    (*nvr->set)(nvr, nvr->addr, val);
}


static uint8_t
nvr_read(uint16_t addr, void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;
    uint8_t ret;

    if (addr & 1) {
	/* Read from the chip's registers. */
	ret = (*nvr->get)(nvr, nvr->addr);
    } else {
	ret = nvr->addr;
    }

    return(ret);
}


void
nvr_at_close(void *priv)
{
    nvr_t *nvr = (nvr_t *)priv;

    if (nvr->fname != NULL)
	free(nvr->fname);

    free(nvr);
}


void
nvr_at_init(int irq)
{
    nvr_t *nvr;

    /* Allocate an NVR for this machine. */
    nvr = (nvr_t *)malloc(sizeof(nvr_t));
    if (nvr == NULL) return;
    memset(nvr, 0x00, sizeof(nvr_t));

    /* This is machine specific. */
    nvr->mask = machines[machine].nvrmask;
    nvr->irq = irq;

    /* Set up any local handlers here. */

    /* Initialize the actual NVR. */
    nvr_init(nvr);

    /* Set up the PC/AT handler for this device. */
    io_sethandler(0x0070, 2,
		  nvr_read, NULL, NULL, nvr_write, NULL, NULL, nvr);
}
