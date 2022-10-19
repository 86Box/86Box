/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		CD-ROM image file handling module header, translated to C
 *		from cdrom_dosbox.h.
 *
 * Authors:	RichardG,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2022 RichardG.
 *		Copyright 2016-2022 Miran Grca.
 */
#ifndef CDROM_IMAGE_H
#define CDROM_IMAGE_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

#ifdef __cplusplus
extern "C" {
#endif

extern int  image_open(uint8_t id, wchar_t *fn);
extern void image_reset(uint8_t id);

extern void image_close(uint8_t id);

void        update_status_bar_icon_state(int tag, int state);
extern void cdrom_set_null_handler(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /*CDROM_IMAGE_H*/
