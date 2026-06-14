#ifndef UNIX_SDL_SHADER_H
#define UNIX_SDL_SHADER_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

int  sdl_shader_init(SDL_Window *win, const char *shader_path);
int  sdl_shader_init_passthrough(SDL_Window *win);
void sdl_shader_blit(SDL_Window *win, const void *pixels,
                     int src_w, int src_h,
                     int dst_x, int dst_y, int dst_w, int dst_h);
void sdl_shader_close(void);
int  sdl_shader_active(void);

SDL_GLContext sdl_shader_get_context(void);

#ifdef __cplusplus
}
#endif

#endif
