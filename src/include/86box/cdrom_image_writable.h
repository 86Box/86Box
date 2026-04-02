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

/* Track file struct. */
typedef struct wtrack_file_t {
    int (*read)(void *priv, uint8_t *buffer, uint64_t seek, size_t count);
    uint64_t (*get_length)(void *priv);
    void (*close)(void *priv);

    char  fn[260];
    FILE *fp;
    void *priv;
    void *log;

    int motorola;
} wtrack_file_t;

extern void *wimage_open(cdrom_t *dev, const char *path);

#endif /*CDROM_IMAGE_WRITABLE_H*/
