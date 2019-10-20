/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the DirectDraw 9 rendering module.
 *
 * Version:	@(#)win_ddraw.h	1.0.2	2019/10/12
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 */
#ifndef WIN_DDRAW_H
# define WIN_DDRAW_H
# define UNICODE
# define BITMAP WINDOWS_BITMAP
# include <ddraw.h>
# undef BITMAP


#ifdef __cplusplus
extern "C" {
#endif

extern int	ddraw_init(HWND h);
extern int	ddraw_init_fs(HWND h);
extern void	ddraw_close(void);
extern int	ddraw_pause(void);
extern void	ddraw_enable(int enable);

#ifdef __cplusplus
}
#endif


#endif	/*WIN_DDRAW_H*/
