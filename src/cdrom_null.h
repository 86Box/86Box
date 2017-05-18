/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#ifndef CDROM_NULL_H
#define CDROM_NULL_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

int cdrom_null_open(uint8_t id, char d);
void cdrom_null_reset(uint8_t id);
void null_close(uint8_t id);

#endif /* ! CDROM_NULL_H */
