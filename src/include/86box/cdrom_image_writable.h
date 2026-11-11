/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CD-R(W) writable image file handling module.
 *
 * Authors: Nat Portillo, <claunia@claunia.com>
 *
 *          Copyright 2025 Nat Portillo.
 */
#ifndef CDROM_IMAGE_WRITABLE_H
#define CDROM_IMAGE_WRITABLE_H

extern void *wimage_open(cdrom_t *dev, const char *path, const int type);
extern void *wimage_resume(cdrom_t *dev, const char *path, int *type);
extern int   wimage_can_resume(const char *path);
extern int   wimage_get_blank_file_type(const char *path);

#endif /*CDROM_IMAGE_WRITABLE_H*/
