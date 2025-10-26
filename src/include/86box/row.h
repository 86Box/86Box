/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the SMRAM interface.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_ROW_H
# define EMU_ROW_H

typedef struct _row_ {
    struct _smram_  *prev;
    struct _smram_  *next;

    uint8_t         *buf;

    mem_mapping_t   mapping;

    uint32_t        host_base;
    uint32_t        host_size;
    uint32_t        ram_base;
    uint32_t        ram_size;
    uint32_t        ram_mask;
    uint32_t        boundary;
} row_t;

extern void         row_disable(uint8_t row_id);
extern void         row_set_boundary(uint8_t row_id, uint32_t boundary);

extern device_t     row_device;

#endif /*EMU_ROW_H*/
