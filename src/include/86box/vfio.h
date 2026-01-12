/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for Virtual Function I/O PCI passthrough.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021-2025 RichardG.
 */
#if !defined(EMU_VFIO_H) && defined(USE_VFIO)
#    define EMU_VFIO_H

extern void vfio_init(void);

#endif /*EMU_VFIO_H*/
