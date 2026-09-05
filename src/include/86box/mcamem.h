/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the MCAMEM cards.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          WNT50
 *
 *          Copyright 2018 Fred N. van Kempen.
 *          Copyright 2026 WNT50.
 */
#ifndef EMU_MCAMEM_H
#define EMU_MCAMEM_H

#define MCAMEM_MAX 4 /* max #cards in system */

#ifdef __cplusplus
extern "C" {
#endif

/* Functions. */
extern void mcamem_reset(void);

extern const char     *mcamem_get_name(int t);
extern const char     *mcamem_get_internal_name(int t);
extern int             mcamem_get_from_internal_name(const char *str);
extern int             mcamem_has_config(int board);

#ifdef EMU_DEVICE_H
extern const device_t *mcamem_get_device(int t);

/* MCA Memory Expansion Boards. */
extern const device_t ibm_xma_mca_2mb_device;
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_MCAMEM_H*/
