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
 * Version:	@(#)plat.h	1.0.4	2017/10/10
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_PLAT_H
# define EMU_PLAT_H


#ifdef __cplusplus
extern "C" {
#endif

/* Global variables residing in the platform module. */


/* System-related functions. */
extern void	get_executable_name(wchar_t *s, int size);
extern void	set_window_title(wchar_t *s);
extern int	dir_check_exist(wchar_t *path);
extern int	dir_create(wchar_t *path);
extern void	leave_fullscreen(void);


/* Resource management. */
extern wchar_t	*plat_get_string(int id);
extern wchar_t	*plat_get_string_from_string(char *str);


/* Platform-specific device support. */
extern uint8_t	host_cdrom_drive_available[26];
extern uint8_t	host_cdrom_drive_available_num;
extern uint32_t	cdrom_capacity;

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

extern thread_t	*thread_create(void (*thread_rout)(void *param), void *param);
extern void	thread_kill(thread_t *handle);

extern event_t	*thread_create_event(void);
extern void	thread_set_event(event_t *event);
extern void	thread_reset_event(event_t *_event);
extern int	thread_wait_event(event_t *event, int timeout);
extern void	thread_destroy_event(event_t *_event);

extern void	thread_sleep(int t);

extern void	*thread_create_mutex(wchar_t *name);
extern void	thread_close_mutex(void *mutex);
extern uint8_t	thread_wait_mutex(void *mutex);
extern uint8_t	thread_release_mutex(void *mutex);


/* Other stuff. */
extern void	startblit(void);
extern void	endblit(void);
extern void	take_screenshot(void);


extern uint32_t	get_ticks(void);
extern void	delay_ms(uint32_t count);


#ifdef __cplusplus
}
#endif

extern void	startslirp(void);
extern void	endslirp(void);



#endif	/*EMU_PLAT_H*/
