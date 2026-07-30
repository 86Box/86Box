/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PS/2 Model 25 MCGA video subsystem.
 */
#ifndef EMU_VID_MCGA_H
#define EMU_VID_MCGA_H

#ifdef __cplusplus
extern "C" {
#endif

extern void mcga_set_enabled(void *priv, int enabled);

#ifdef EMU_DEVICE_H
extern const device_t mcga_device;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMU_VID_MCGA_H */
