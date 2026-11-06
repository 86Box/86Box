/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          AGP Graphics Address Remapping Table remapping emulation.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021 RichardG.
 */
#ifndef EMU_AGPGART_H
#define EMU_AGPGART_H

#include <stdatomic.h>

typedef struct agpgart_s {
    int           aperture_enable;
    uint32_t      aperture_base;
    uint32_t      aperture_size;
    uint32_t      aperture_mask;
    uint32_t      gart_base;
    /* The GART as host memory, good while mem_mapping_gen is gart_gen:
       the whole table inside the RAM array at the table's own address.
       NULL where it is not, and the entries are read through the bus.
       Written on the CPU thread only, ptr before gen; any thread may read
       them, gen first. */
    _Atomic uint32_t         gart_gen;
    _Atomic(const uint8_t *) gart_ptr;
    /* The end of the RAM array, a bound on the entry read for a reader
       whose offset came from an aperture size the guest has since
       changed; the same value under every generation of one machine. */
    _Atomic(const uint8_t *) gart_end;
    mem_mapping_t aperture_mapping;
} agpgart_t;

extern void agpgart_set_aperture(agpgart_t *dev, uint32_t base, uint32_t size, int enable);
extern void agpgart_set_gart(agpgart_t *dev, uint32_t base);
extern const uint8_t *agpgart_bus_ptr(uint32_t addr, uint32_t len);

#ifdef EMU_DEVICE_H
/* AGP GART */
extern const device_t agpgart_device;
#endif

#endif /*EMU_AGPGART_H*/
