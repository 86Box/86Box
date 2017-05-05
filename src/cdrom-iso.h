/* Copyright holders: RichardG867, Tenshi
   see COPYING for more details
*/
#ifndef CDROM_ISO_H
#define CDROM_ISO_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

extern int iso_open(uint8_t id, wchar_t *fn);
extern void iso_reset(uint8_t id);

extern void iso_close(uint8_t id);

#endif /* ! CDROM_ISO_H */
