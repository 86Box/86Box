/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The handler of the new logging system.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2021 Miran Grca.
 *		Copyright 2021 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/mem.h>
#include "cpu.h"
#include <86box/plat.h>
#include <86box/version.h>
#include <86box/log.h>

#ifndef RELEASE_BUILD
typedef struct
{
    char buff[1024], *dev_name;
    int  seen, suppr_seen;
} log_t;

extern FILE *stdlog; /* file to log output to */

void
log_set_suppr_seen(void *priv, int suppr_seen)
{
    log_t *log = (log_t *) priv;

    log->suppr_seen = suppr_seen;
}

void
log_set_dev_name(void *priv, char *dev_name)
{
    log_t *log = (log_t *) priv;

    log->dev_name = dev_name;
}

static void
log_copy(log_t *log, char *dest, const char *src, size_t dest_size)
{
    memset(dest, 0x00, dest_size * sizeof(char));
    if (log && log->dev_name && strcmp(log->dev_name, "")) {
        strcat(dest, log->dev_name);
        strcat(dest, ": ");
    }
    strcat(dest, src);
}

/*
 * Log something to the logfile or stdout.
 *
 * To avoid excessively-large logfiles because some
 * module repeatedly logs, we keep track of what is
 * being logged, and catch repeating entries.
 */
void
log_out(void *priv, const char *fmt, va_list ap)
{
    log_t *log = (log_t *) priv;
    char   temp[1024], fmt2[1024];

    if (log == NULL)
        return;

    if (strcmp(fmt, "") == 0)
        return;

    if (stdlog == NULL) {
        if (log_path[0] != '\0') {
            stdlog = plat_fopen(log_path, "w");
            if (stdlog == NULL)
                stdlog = stdout;
        } else
            stdlog = stdout;
    }

    vsprintf(temp, fmt, ap);
    if (log->suppr_seen && !strcmp(log->buff, temp))
        log->seen++;
    else {
        if (log->suppr_seen && log->seen) {
            log_copy(log, fmt2, "*** %d repeats ***\n", 1024);
            fprintf(stdlog, fmt2, log->seen);
        }
        log->seen = 0;
        strcpy(log->buff, temp);
        log_copy(log, fmt2, temp, 1024);
        fprintf(stdlog, fmt2, ap);
    }

    fflush(stdlog);
}

void
log_fatal(void *priv, const char *fmt, ...)
{
    log_t  *log = (log_t *) priv;
    char    temp[1024], fmt2[1024];
    va_list ap;

    if (log == NULL)
        return;

    va_start(ap, fmt);
    log_copy(log, fmt2, fmt, 1024);
    vsprintf(temp, fmt2, ap);
    fatal_ex(fmt2, ap);
    va_end(ap);
    exit(-1);
}

void *
log_open(char *dev_name)
{
    log_t *log = malloc(sizeof(log_t));

    memset(log, 0, sizeof(log_t));

    log->dev_name   = dev_name;
    log->suppr_seen = 1;

    return (void *) log;
}

void
log_close(void *priv)
{
    log_t *log = (log_t *) priv;

    free(log);
}
#endif
