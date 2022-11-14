/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x86 CPU segment emulation.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2017 Miran Grca.
 */

extern void do_seg_load(x86seg *s, uint16_t *segdat);

extern void cyrix_write_seg_descriptor(uint32_t addr, x86seg *seg);
extern void cyrix_load_seg_descriptor(uint32_t addr, x86seg *seg);
