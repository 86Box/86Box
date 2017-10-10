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
 * Version:	@(#)plat.h	1.0.2	2017/10/09
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
extern void	cdrom_close(uint8_t id);
extern void	cdrom_eject(uint8_t id);
extern void	cdrom_reload(uint8_t id);
extern void	removable_disk_unload(uint8_t id);
extern void	removable_disk_eject(uint8_t id);
extern void	removable_disk_reload(uint8_t id);
extern int      ioctl_open(uint8_t id, char d);
extern void     ioctl_reset(uint8_t id);
extern void     ioctl_close(uint8_t id);


/* Other stuff. */
extern void	startblit(void);
extern void	endblit(void);


#ifdef __cplusplus
}
#endif


#endif	/*EMU_PLAT_H*/
