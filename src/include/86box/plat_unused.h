/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Define the various platform support functions.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2021 Laci bá'
 */
#ifndef EMU_PLAT_UNUSED_H
#define EMU_PLAT_UNUSED_H

#ifndef EMU_PLAT_H
#ifdef _MSC_VER
#    define UNUSED(arg) arg
#else
/* A hack (GCC-specific?) to allow us to ignore unused parameters. */
#    define UNUSED(arg) __attribute__((unused)) arg
#endif
#endif

#endif /*EMU_PLAT_UNUSED_H*/
