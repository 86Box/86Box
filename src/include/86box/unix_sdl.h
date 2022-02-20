#ifndef _UNIX_SDL_H
# define _UNIX_SDL_H

extern void	sdl_close(void);
extern int	sdl_inits();
extern int	sdl_inith();
extern int	sdl_initho();
extern int	sdl_pause(void);
extern void	sdl_resize(int x, int y);
extern void	sdl_enable(int enable);
extern void	sdl_set_fs(int fs);
extern void	sdl_reload(void);

#endif /*_UNIX_SDL_H*/
