/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 * 		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Define the Dynamic Module Loader interface.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen
 */

#ifndef PLAT_DYNLD_H
#define PLAT_DYNLD_H

typedef struct {
    const char *name;
    void       *func;
} dllimp_t;

#ifdef __cplusplus
extern "C" {
#endif

extern void *dynld_module(const char *, dllimp_t *);
extern void  dynld_close(void *);

#ifdef __cplusplus
}
#endif

#endif /*PLAT_DYNLD_H*/
