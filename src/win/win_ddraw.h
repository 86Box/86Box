/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
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
extern void	ddraw_close(void);
extern void	ddraw_take_screenshot(wchar_t *fn);

extern int	ddraw_fs_init(HWND h);
extern void	ddraw_fs_close(void);

#ifdef __cplusplus
}
#endif


#endif	/*WIN_DDRAW_H*/
