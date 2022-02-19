/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the VNC renderer.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */

#ifndef EMU_VNC_H
# define EMU_VNC_H


#ifdef __cplusplus
extern "C" {
#endif

extern int	vnc_init(void *);
extern void	vnc_close(void);
extern void	vnc_resize(int x, int y);
extern int	vnc_pause(void);

extern void	vnc_kbinput(int, int);

extern void	vnc_take_screenshot(wchar_t *fn);

#ifdef __cplusplus
}
#endif

#endif	/*EMU_VNC_H*/
