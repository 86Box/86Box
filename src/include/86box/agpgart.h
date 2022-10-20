/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          AGP Graphics Address Remapping Table remapping emulation.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021 RichardG.
 */

#ifndef EMU_AGPGART_H
#define EMU_AGPGART_H

typedef struct agpgart_s {
    int           aperture_enable;
    uint32_t      aperture_base, aperture_size, aperture_mask, gart_base;
    mem_mapping_t aperture_mapping;
} agpgart_t;

extern void agpgart_set_aperture(agpgart_t *dev, uint32_t base, uint32_t size, int enable);
extern void agpgart_set_gart(agpgart_t *dev, uint32_t base);

#ifdef EMU_DEVICE_H
/* AGP GART */
extern const device_t agpgart_device;
#endif

#endif /*EMU_AGPGART_H*/
