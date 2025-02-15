/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          New logging system handler header.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Connor Hyde, <mario64crashed@gmail.com, nomorestarfrost@gmail.com>
 *
 *          Copyright 2021-25 Miran Grca.
 *          Copyright 2021-25 Fred N. van Kempen.
 *          Copyright 2025 Connor Hyde.
 */

#ifndef EMU_LOG_H
#define EMU_LOG_H

#    ifdef __cplusplus
extern "C" {
#    endif

#define LOG_SIZE_BUFFER                 1024            /* Log size buffer */
#define LOG_SIZE_BUFFER_CYCLIC_LINES    32              /* Cyclic log size buffer (number of lines that should be cehcked) */
#define LOG_MINIMUM_REPEAT_ORDER        4               /* Minimum repeat size */

/* Function prototypes. */
extern void log_set_suppr_seen(void *priv, int suppr_seen);
extern void log_set_dev_name(void *priv, char *dev_name);
#ifndef RELEASE_BUILD
extern void log_out(void *priv, const char *fmt, va_list);
extern void log_out_cyclic(void* priv, const char *fmt, va_list);
#endif /*RELEASE_BUILD*/
extern void log_fatal(void *priv, const char *fmt, ...);
extern void *log_open(const char *dev_name);
extern void *log_open_cyclic(const char *dev_name);
extern void  log_close(void *priv);

#    ifdef __cplusplus
}
#    endif

#endif /*EMU_LOG_H*/
