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

#    define VFIO_DEVICE_MAX 32

typedef struct vfio_device_conf_t {
    char location[256]; /* dddd:bb:dd.f location or sysfs path, blank = end of list */
    char rom_fn[1024];  /* custom expansion ROM file, blank = the device's own */
} vfio_device_conf_t;

#    ifdef __cplusplus
extern "C" {
#    endif

extern vfio_device_conf_t vfio_device_conf[VFIO_DEVICE_MAX]; /* (C) passed through devices */

extern int  vfio_parse_location(const char *location, char *out, int out_size);
extern void vfio_init(void);
extern void vfio_irq_intx_resample(int irq);

#    ifdef __cplusplus
}
#    endif

#endif /*EMU_VFIO_H*/
