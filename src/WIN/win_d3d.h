/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
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
