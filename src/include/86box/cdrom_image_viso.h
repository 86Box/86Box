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
 * Authors: RichardG, <richardg867@gmail.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 RichardG.
 *          Copyright 2016-2025 Miran Grca.
 */
#ifndef CDROM_IMAGE_VISO_H
#define CDROM_IMAGE_VISO_H

/* Virtual ISO functions. */
extern int           viso_read(void *priv, uint8_t *buffer, uint64_t seek, size_t count);
extern uint64_t      viso_get_length(void *priv);
extern void          viso_close(void *priv);
extern track_file_t *viso_init(const uint8_t id, const char *dirname, int *error);

#endif /*CDROM_IMAGE_VISO_H*/
