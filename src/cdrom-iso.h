#ifndef CDROM_ISO_H
#define CDROM_ISO_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

extern char iso_path[1024];

extern int iso_open(char *fn);
extern void iso_reset();

extern void iso_close(void);

#endif /* ! CDROM_ISO_H */
