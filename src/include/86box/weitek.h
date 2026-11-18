/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Weitek WTL 3167 floating-point coprocessor.
 */
#ifndef EMU_WEITEK_H
#define EMU_WEITEK_H

/* The machine's "Weitek WTL 3167" option ([Machine] weitek_3167), off by
   default. It takes effect only with a CPU for the 386DX socket, beside
   which the 121-pin socket sits (weitek_fitted()). */
extern int weitek_3167;

#ifdef EMU_CPU_H
extern int weitek_cpu_family_ok(const cpu_family_t *family);
#endif
extern int weitek_fitted(void);

#ifdef EMU_DEVICE_H
extern const device_t weitek_3167_device;
#endif

#endif /*EMU_WEITEK_H*/
