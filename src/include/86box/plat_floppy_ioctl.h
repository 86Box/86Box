/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Definitions for platform specific floppy device access.
 *
 * Authors:     Tiago Gasiba <tiga@FreeBSD.org>
 *
 *              Copyright 2026 Tiago Gasiba.
 */
#ifndef PLAT_FLOPPY_IOCTL_H
#define PLAT_FLOPPY_IOCTL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void fdd_set_host_device(int drive, const char *path);
extern const char *fdd_get_host_device(int drive);
extern void floppy_ioctl_set_buffering(int enabled);
extern int floppy_ioctl_get_buffering(void);
extern int floppy_ioctl_open(int drive, int *out_tracks, int *out_sides, int *out_sectors, int *out_rate);
extern void floppy_ioctl_close(int drive);
extern int floppy_ioctl_read_sector(int drive, int track, int side, int sector, uint8_t *buffer);
extern int floppy_ioctl_write_sector(int drive, int track, int side, int sector, const uint8_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* PLAT_FLOPPY_IOCTL_H */
