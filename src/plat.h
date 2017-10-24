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
 * Version:	@(#)plat.h	1.0.13	2017/10/22
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_PLAT_H
# define EMU_PLAT_H

#ifndef GLOBAL
# define GLOBAL extern
#endif


/* A hack (GCC-specific) to allow us to ignore unused parameters. */
#define UNUSED(arg)	__attribute__((unused))arg


#ifdef __cplusplus
extern "C" {
#endif

/* Global variables residing in the platform module. */
GLOBAL int	dopause,			/* system is paused */
		doresize,			/* screen resize requested */
		quited,				/* system exit requested */
		leave_fullscreen_flag;		/* windowed-mode requested */
GLOBAL uint64_t	timer_freq;
GLOBAL int	infocus;


/* System-related functions. */
extern FILE	*plat_fopen(wchar_t *path, wchar_t *mode);
extern void	plat_remove(wchar_t *path);
extern int	plat_getcwd(wchar_t *bufp, int max);
extern int	plat_chdir(wchar_t *path);
extern void	plat_get_exe_name(wchar_t *s, int size);
extern wchar_t	*plat_get_filename(wchar_t *s);
extern wchar_t	*plat_get_extension(wchar_t *s);
extern void	plat_append_filename(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size);
extern void	plat_put_backslash(wchar_t *s);
extern int	plat_dir_check(wchar_t *path);
extern int	plat_dir_create(wchar_t *path);
extern uint64_t	plat_timer_read(void);
extern uint32_t	plat_get_ticks(void);
extern void	plat_delay_ms(uint32_t count);
extern void	plat_pause(int p);
extern int	plat_vidapi(char *name);
extern int	plat_setvid(int api);
extern void	plat_setfullscreen(int on);
extern void	plat_resize(int max_x, int max_y);


/* Return the size (in wchar's) of a wchar_t array. */
#define sizeof_w(x)	(sizeof((x)) / sizeof(wchar_t))

/* The Win32 API uses _wcsicmp. */
#ifdef WIN32
# define wcscasecmp	_wcsicmp
#endif


/* Resource management. */
extern wchar_t	*plat_get_string(int id);
extern wchar_t	*plat_get_string_from_string(char *str);


/* Platform-specific device support. */
extern uint8_t	host_cdrom_drive_available[26];
extern uint8_t	host_cdrom_drive_available_num;

extern void	cdrom_init_host_drives(void);
extern void	cdrom_eject(uint8_t id);
extern void	cdrom_reload(uint8_t id);
extern void	removable_disk_unload(uint8_t id);
extern void	removable_disk_eject(uint8_t id);
extern void	removable_disk_reload(uint8_t id);
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

extern mutex_t	*thread_create_mutex(wchar_t *name);
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
