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
 *
 *
 * Authors: Jasmine Iwanek, <jasmine@iwanek.co.uk>
 *
 *          Copyright 2023 Jasmine Iwanek
 */

#ifndef EMU_PLAT_FALLTHROUGH_H
#define EMU_PLAT_FALLTHROUGH_H

#if !defined (__APPLE__) && !defined(__clang__)
#    define FALLTHROUGH_ANNOTATION
#endif

#endif /*EMU_PLAT_FALLTHROUGH_H*/
