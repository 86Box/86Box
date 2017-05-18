/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifndef CDROM_IOCTL_H
#define CDROM_IOCTL_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

extern uint32_t cdrom_capacity;
   
extern int ioctl_open(uint8_t id, char d);
extern void ioctl_reset(uint8_t id);

extern void ioctl_close(uint8_t id);

#endif /* ! CDROM_IOCTL_H */
