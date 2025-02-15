/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the PCjr cartridge emulation.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Miran Grca.
 */
#ifndef EMU_CARTRIDGE_H
#define EMU_CARTRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#define CART_IMAGE_HISTORY    10

extern char cart_fns[2][512];
extern char *cart_image_history[2][CART_IMAGE_HISTORY];

extern void cart_load(int drive, char *fn);
extern void cart_close(int drive);

extern void cart_reset(void);

#ifdef __cplusplus
}
#endif

#endif /*EMU_CARTRIDGE_H*/
