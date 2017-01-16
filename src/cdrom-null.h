/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#ifndef CDROM_IOCTL_H
#define CDROM_IOCTL_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

extern int cdrom_null_open(uint8_t id, char d);
extern void cdrom_null_reset(uint8_t id);
extern void null_close(uint8_t id);

#endif /* ! CDROM_IOCTL_H */
