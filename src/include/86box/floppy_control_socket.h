#ifndef EMU_FLOPPY_CONTROL_SOCKET_H
#define EMU_FLOPPY_CONTROL_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_FLOPPY_CONTROL_SOCKET
extern void floppy_control_socket_init(void);
extern void floppy_control_socket_close(void);
#else
static inline void floppy_control_socket_init(void) { }
static inline void floppy_control_socket_close(void) { }
#endif

#ifdef __cplusplus
}
#endif

#endif
