/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of ISA ROM card Expansions.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025 Jasmine Iwanek.
 */
#ifndef EMU_ISAROM_H
#define EMU_ISAROM_H

#define ISAROM_MAX 4 /* max #cards in system */

#ifdef __cplusplus
extern "C" {
#endif

/* Functions. */
extern void isarom_reset(void);

extern const char     *isarom_get_name(int t);
extern const char     *isarom_get_internal_name(int t);
extern int             isarom_get_from_internal_name(const char *str);
extern const device_t *isarom_get_device(int t);
extern int             isarom_has_config(int board);

#ifdef __cplusplus
}
#endif

#endif /*EMU_ISAROM_H*/
