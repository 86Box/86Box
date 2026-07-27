#ifndef _SDL_MONITOR_H
#define _SDL_MONITOR_H

#ifdef __APPLE__
#    define LIBEDIT_LIBRARY "libedit.dylib"
#else
#    define LIBEDIT_LIBRARY "libedit.so"
#endif

extern void monitor_init(void);
extern void monitor_close(void);
extern void monitor_thread(void *param);
extern void monitor_execute_line(char *line);

#endif