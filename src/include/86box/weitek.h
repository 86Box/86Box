/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Weitek WTL 3167 and 4167 floating-point coprocessor.
 */
#ifndef EMU_WEITEK_H
#define EMU_WEITEK_H

/* Optional [Machine] settings for the 386DX and 486 buses respectively. */
extern int weitek_3167;
extern int weitek_4167;

#ifdef EMU_CPU_H
extern int weitek_cpu_family_ok(const cpu_family_t *family);
extern int weitek_4167_supported(const cpu_family_t *family, int machine_id);
#endif
extern int weitek_fitted(void);
extern int weitek_4167_fitted(void);

#ifdef EMU_DEVICE_H
extern const device_t weitek_3167_device;
extern const device_t weitek_4167_device;
#endif

#endif /*EMU_WEITEK_H*/
