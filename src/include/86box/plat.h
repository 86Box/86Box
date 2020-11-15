/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Define the various platform support functions.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#ifndef EMU_PLAT_H
# define EMU_PLAT_H

#ifndef GLOBAL
# define GLOBAL extern
#endif

/* String ID numbers. */
#include <86box/language.h>

/* The Win32 API uses _wcsicmp. */
#ifdef _WIN32
# define wcscasecmp	_wcsicmp
# define strcasecmp	_stricmp
#endif

#if defined(UNIX) && defined(FREEBSD)
/* FreeBSD has largefile by default. */
# define fopen64        fopen
# define fseeko64       fseeko
# define ftello64       ftello
# define off64_t        off_t
#elif defined(_MSC_VER)
//# define fopen64	fopen
# define fseeko64	_fseeki64
# define ftello64	_ftelli64
# define off64_t	off_t
#endif


#ifdef _MSC_VER
# define UNUSED(arg)	arg
#else
  /* A hack (GCC-specific?) to allow us to ignore unused parameters. */
# define UNUSED(arg)	__attribute__((unused))arg
#endif

/* Return the size (in wchar's) of a wchar_t array. */
#define sizeof_w(x)	(sizeof((x)) / sizeof(wchar_t))


#ifdef __cplusplus
extern "C" {
#endif

/* Global variables residing in the platform module. */
extern int	dopause,			/* system is paused */
		doresize,			/* screen resize requested */
		quited,				/* system exit requested */
		mouse_capture;			/* mouse is captured in app */
extern uint64_t	timer_freq;
extern int	infocus;
extern char	emu_version[200];		/* version ID string */
extern int	rctrl_is_lalt;
extern int	update_icons;

extern int	unscaled_size_x,		/* current unscaled size X */
		unscaled_size_y;		/* current unscaled size Y */

/* System-related functions. */
extern wchar_t	*fix_exe_path(wchar_t *str);
extern FILE	*plat_fopen(wchar_t *path, wchar_t *mode);
extern FILE	*plat_fopen64(const wchar_t *path, const wchar_t *mode);
extern void	plat_remove(wchar_t *path);
extern int	plat_getcwd(wchar_t *bufp, int max);
extern int	plat_chdir(wchar_t *path);
extern void	plat_tempfile(wchar_t *bufp, wchar_t *prefix, wchar_t *suffix);
extern void	plat_get_exe_name(wchar_t *s, int size);
extern wchar_t	*plat_get_basename(const wchar_t *path);
extern void	plat_get_dirname(wchar_t *dest, const wchar_t *path);
extern wchar_t	*plat_get_filename(wchar_t *s);
extern wchar_t	*plat_get_extension(wchar_t *s);
extern void	plat_append_filename(wchar_t *dest, wchar_t *s1, wchar_t *s2);
extern void	plat_put_backslash(wchar_t *s);
extern void	plat_path_slash(wchar_t *path);
extern int	plat_path_abs(wchar_t *path);
extern int	plat_dir_check(wchar_t *path);
extern int	plat_dir_create(wchar_t *path);
extern uint64_t	plat_timer_read(void);
extern uint32_t	plat_get_ticks(void);
extern void	plat_delay_ms(uint32_t count);
extern void	plat_pause(int p);
extern void	plat_mouse_capture(int on);
extern int	plat_vidapi(char *name);
extern char	*plat_vidapi_name(int api);
extern int	plat_setvid(int api);
extern void	plat_vidsize(int x, int y);
extern void	plat_setfullscreen(int on);
extern void	plat_resize(int x, int y);
extern void	plat_vidapi_enable(int enabled);


/* Resource management. */
extern void	set_language(int id);
extern wchar_t	*plat_get_string(int id);


/* Emulator start/stop support functions. */
extern void	do_start(void);
extern void	do_stop(void);


/* Power off. */
extern void	plat_power_off(void);


/* Platform-specific device support. */
extern void	floppy_mount(uint8_t id, wchar_t *fn, uint8_t wp);
extern void	floppy_eject(uint8_t id);
extern void	cdrom_mount(uint8_t id, wchar_t *fn);
extern void	plat_cdrom_ui_update(uint8_t id, uint8_t reload);
extern void	zip_eject(uint8_t id);
extern void	zip_mount(uint8_t id, wchar_t *fn, uint8_t wp);
extern void	zip_reload(uint8_t id);
extern void	mo_eject(uint8_t id);
extern void	mo_mount(uint8_t id, wchar_t *fn, uint8_t wp);
extern void	mo_reload(uint8_t id);
extern int      ioctl_open(uint8_t id, char d);
extern void     ioctl_reset(uint8_t id);
extern void     ioctl_close(uint8_t id);


/* Thread support. */
typedef void thread_t;
typedef void event_t;
typedef void mutex_t;

extern thread_t	*thread_create(void (*thread_func)(void *param), void *param);
extern void	thread_kill(thread_t *arg);
extern int	thread_wait(thread_t *arg, int timeout);
extern event_t	*thread_create_event(void);
extern void	thread_set_event(event_t *arg);
extern void	thread_reset_event(event_t *arg);
extern int	thread_wait_event(event_t *arg, int timeout);
extern void	thread_destroy_event(event_t *arg);

extern mutex_t	*thread_create_mutex(void);
extern void	thread_close_mutex(mutex_t *arg);
extern int	thread_wait_mutex(mutex_t *arg);
extern int	thread_release_mutex(mutex_t *mutex);


/* Other stuff. */
extern void	startblit(void);
extern void	endblit(void);
extern void	take_screenshot(void);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_PLAT_H*/
