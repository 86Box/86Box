/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Shared debug logging helper for modem networking modules.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_DEBUG_H
#define EMU_MODEM_DEBUG_H

#if defined(ENABLE_MODEM_LOG) && defined(ENABLE_MODEM_DEBUG)
extern uint8_t modem_do_log;

static inline void
modem_debug_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}

#    define MODEM_DEBUG_LOG(priv, ...) modem_debug_log((priv), __VA_ARGS__)
#else
#    define MODEM_DEBUG_LOG(...) ((void) 0)
#endif

#endif /* EMU_MODEM_DEBUG_H */
