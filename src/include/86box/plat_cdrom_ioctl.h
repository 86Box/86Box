/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Definitions for platform specific serial to host passthrough.
 *
 *
 * Authors:     Andreas J. Reichel <webmaster@6th-dimension.com>,
 *              Jasmine Iwanek <jasmine@iwanek.co.uk>
 *
 *              Copyright 2021      Andreas J. Reichel.
 *              Copyright 2021-2022 Jasmine Iwanek.
 */

#ifndef PLAT_CDROM_IOCTL_H
#define PLAT_CDROM_IOCTL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void *   ioctl_open(cdrom_t *dev, const char *drv);

#ifdef __cplusplus
}
#endif

#endif
