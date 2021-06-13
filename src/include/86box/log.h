/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Main include file for the application.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2021 Miran Grca.
 *		Copyright 2021 Fred N. van Kempen.
 */
#ifndef EMU_LOG_H
# define EMU_LOG_H

#ifndef RELEASE_BUILD

#ifdef __cplusplus
extern "C" {
#endif

/* Function prototypes. */
extern void	log_set_suppr_seen(void *priv, int suppr_seen);
extern void	log_set_dev_name(void *priv, char *dev_name);
#ifdef HAVE_STDARG_H
extern void	log_out(void *priv, const char *fmt, va_list);
extern void	log_fatal(void *priv, const char *fmt, ...);
#endif
extern void *	log_open(char *dev_name);
extern void	log_close(void *priv);

#ifdef __cplusplus
}
#endif

#else
#define	log_fatal(priv, fmt, ...) fatal(fmt, ...)
#endif	/*RELEASE_BUILD*/

#endif	/*EMU_LOG_H*/
