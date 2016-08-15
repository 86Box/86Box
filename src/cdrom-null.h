/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#ifndef CDROM_IOCTL_H
#define CDROM_IOCTL_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

extern int cdrom_null_open(char d);
extern void cdrom_null_reset();
extern void null_close();

#endif /* ! CDROM_IOCTL_H */
