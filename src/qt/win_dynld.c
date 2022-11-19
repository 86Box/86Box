/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 * 		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Try to load a support DLL.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat_dynld.h>

#ifdef ENABLE_DYNLD_LOG
int dynld_do_log = ENABLE_DYNLD_LOG;

static void
dynld_log(const char *fmt, ...)
{
    va_list ap;

    if (dynld_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define dynld_log(fmt, ...)
#endif

void *
dynld_module(const char *name, dllimp_t *table)
{
    HMODULE   h;
    dllimp_t *imp;
    void     *func;

    /* See if we can load the desired module. */
    if ((h = LoadLibrary(name)) == NULL) {
        dynld_log("DynLd(\"%s\"): library not found! (%08X)\n", name, GetLastError());
        return (NULL);
    }

    /* Now load the desired function pointers. */
    for (imp = table; imp->name != NULL; imp++) {
        func = GetProcAddress(h, imp->name);
        if (func == NULL) {
            dynld_log("DynLd(\"%s\"): function '%s' not found! (%08X)\n",
                      name, imp->name, GetLastError());
            FreeLibrary(h);
            return (NULL);
        }

        /* To overcome typing issues.. */
        *(char **) imp->func = (char *) func;
    }

    /* All good. */
    dynld_log("loaded %s\n", name);
    return ((void *) h);
}

void
dynld_close(void *handle)
{
    if (handle != NULL)
        FreeLibrary((HMODULE) handle);
}
