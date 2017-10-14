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
 * Version:	@(#)win_vnc.h	1.0.1	2017/10/13
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef WIN_VNC_H
# define WIN_VNC_H


extern int	vnc_init(HWND h);
extern void	vnc_close(void);
extern void	vnc_resize(int x, int y);
extern int	vnc_pause(void);

extern void	vnc_take_screenshot(wchar_t *fn);


#endif	/*WIN_VNC_H*/
