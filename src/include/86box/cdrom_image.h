/* Copyright holders: RichardG867, Tenshi
   see COPYING for more details
*/

#ifndef CDROM_IMAGE_H
# define CDROM_IMAGE_H

/* this header file lists the functions provided by
   various platform specific cdrom-ioctl files */

#ifdef __cplusplus
extern "C" {
#endif

extern int image_open(uint8_t id, wchar_t *fn);
extern void image_reset(uint8_t id);

extern void image_close(uint8_t id);

void update_status_bar_icon_state(int tag, int state);
extern void cdrom_set_null_handler(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /*CDROM_IMAGE_H*/
