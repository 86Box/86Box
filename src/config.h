/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Configuration file handler header.
 *
 * Version:	@(#)config.h	1.0.3	2017/10/13
 *
 * Authors:	Sarah Walker,
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Overdoze,
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_CONFIG_H
# define EMU_CONFIG_H


extern wchar_t	config_file_default[256];


#ifdef __cplusplus
extern "C" {
#endif

extern void	config_load(wchar_t *fn);
extern void	config_save(void);
extern void	config_write(wchar_t *fn);
extern void	config_dump(void);

extern void	config_delete_var(char *head, char *name);
extern int	config_get_int(char *head, char *name, int def);
extern int	config_get_hex16(char *head, char *name, int def);
extern int	config_get_hex20(char *head, char *name, int def);
extern int	config_get_mac(char *head, char *name, int def);
extern char	*config_get_string(char *head, char *name, char *def);
extern wchar_t	*config_get_wstring(char *head, char *name, wchar_t *def);
extern void	config_set_int(char *head, char *name, int val);
extern void	config_set_hex16(char *head, char *name, int val);
extern void	config_set_hex20(char *head, char *name, int val);
extern void	config_set_mac(char *head, char *name, int val);
extern void	config_set_string(char *head, char *name, char *val);
extern void	config_set_wstring(char *head, char *name, wchar_t *val);

extern wchar_t	*get_filename_w(wchar_t *s);
extern void	append_filename_w(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size);
extern void	put_backslash_w(wchar_t *s);
extern wchar_t	*get_extension_w(wchar_t *s);


#ifdef __cplusplus
}
#endif


#endif	/*EMU_CONFIG_H*/
