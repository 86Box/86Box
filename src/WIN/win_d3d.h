/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Direct3D 9 rendererer and screenshots taking.
 *
 * Version:	@(#)win_d3d.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#ifndef WIN_D3D_H
# define WIN_D3D_H
# define UNICODE
# define BITMAP WINDOWS_BITMAP
# include <d3d9.h>
# include <d3dx9tex.h>
# undef BITMAP


#ifdef __cplusplus
extern "C" {
#endif

extern int	d3d_init(HWND h);
extern void	d3d_close(void);
extern void	d3d_reset(void);
extern void	d3d_resize(int x, int y);

extern int	d3d_fs_init(HWND h);
extern void	d3d_fs_close(void);
extern void	d3d_fs_reset(void);
extern void	d3d_fs_resize(int x, int y);

#ifdef __cplusplus
}
#endif


#endif	/*WIN_D3D_H*/
