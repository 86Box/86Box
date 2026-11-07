/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Mitsumi CD-ROM emulation for the ISA bus.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Miran Grca.
 *          Copyright 2024-2025 Jasmine Iwanek.
 */
#ifndef CDROM_MITSUMI_H
#define CDROM_MITSUMI_H

#ifndef IS_TEST
extern const device_t mitsumi_cdrom_device;
extern const device_t mitsumi_cdrom_vis_device;
#endif

/* No I/O ports, IRQ or DMA: the host reaches registers 0 and 1. */
#define MITSUMI_HOSTED 2
#ifndef IS_TEST
extern const device_t mitsumi_cdrom_hosted_device;
#endif

extern uint8_t mitsumi_cdrom_host_read(void *priv, int reg);
extern void    mitsumi_cdrom_host_write(void *priv, int reg, uint8_t val);

#endif /*CDROM_MITSUMI_H*/
