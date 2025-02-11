/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CD-ROM image file handling module header.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          RichardG, <richardg867@gmail.com>
 *          Cacodemon345
 *
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2016-2025 RichardG.
 *          Copyright 2024-2025 Cacodemon345.
 */
#ifndef CDROM_IMAGE_H
#define CDROM_IMAGE_H

/* Track file struct. */
typedef struct track_file_t {
    int (*read)(void *priv, uint8_t *buffer, uint64_t seek, size_t count);
    uint64_t (*get_length)(void *priv);
    void (*close)(void *priv);

    char  fn[260];
    FILE *fp;
    void *priv;
    void *log;

    int motorola;
} track_file_t;

extern void *        image_open(cdrom_t *dev, const char *path);

#endif /*CDROM_IMAGE_H*/
