/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the Direct2D rendering module.
 *
 * Version:	@(#)win_d2d.h	1.0.1	2019/10/12
 *
 * Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2018,2019 David Hrdlička.
 */
#ifndef WIN_D2D_H
# define WIN_D2D_H

#ifdef __cplusplus
extern "C" {
#endif

extern void	d2d_close(void);
extern int	d2d_init(HWND h);
extern int	d2d_init_fs(HWND h);
extern int	d2d_pause(void);
extern void	d2d_enable(int enable);

#ifdef __cplusplus
}
#endif


#endif	/*WIN_D2D_H*/