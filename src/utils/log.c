/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          New logging system handler.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Connor Hyde, <mario64crashed@gmail.com, nomorestarfrost@gmail.com>
 * 
 *          Copyright 2021-25 Miran Grca.
 *          Copyright 2021-25 Fred N. van Kempen.
 *          Copyright 2025 Connor Hyde.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
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

typedef struct log_t {
    char     buff[1024];
    char     dev_name[1024];
    int      seen;
    int      suppr_seen;
    /* Cyclical log buffer. */
    char   **cyclic_buff;
    int32_t  cyclic_last_line;
    int32_t  log_cycles;
} log_t;

/* File to log output to. */
extern FILE *stdlog;
/* Functions only used in this translation unit. */
void log_ensure_stdlog_open(void);

void
log_set_dev_name(void *priv, char *dev_name)
{
    log_t *log = (log_t *) priv;

    memcpy(log->dev_name, dev_name, strlen(dev_name) + 1);
}

static void
log_copy(log_t *log, char *dest, const char *src, size_t dest_size)
{
    memset(dest, 0x00, dest_size * sizeof(char));

    if ((log != NULL) && strcmp(log->dev_name, "")) {
        strcat(dest, log->dev_name);
        strcat(dest, ": ");
    }

    strcat(dest, src);
}

#ifndef RELEASE_BUILD
void 
log_ensure_stdlog_open(void)
{
    if (stdlog == NULL) {
        if (log_path[0] != '\0') {
            stdlog = plat_fopen(log_path, "w");
            if (stdlog == NULL)
                stdlog = stdout;
        } else
            stdlog = stdout;
    }
}

void
log_set_suppr_seen(void *priv, int suppr_seen)
{
    log_t *log = (log_t *) priv;

    log->suppr_seen = suppr_seen;
}

/*
   Log something to the logfile or stdout.

   To avoid excessively-large logfiles because some
   module repeatedly logs, we keep track of what is
   being logged, and catch repeating entries.
 */
void
log_out(void *priv, const char *fmt, va_list ap)
{
    log_t *log = (log_t *) priv;
    char   temp[1024];
    char   fmt2[1024];

    if (log == NULL)
        pclog("WARNING: Logging called with a NULL log pointer\n");
    else if (fmt == NULL)
        pclog("WARNING: Logging called with a NULL format pointer\n");
    else if (fmt[0] != '\0') {
        log_ensure_stdlog_open();

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
}

/*
   Starfrost, 7-8 January 2025:

   For RIVA 128 emulation I needed a way to suppress logging if a repeated
   pattern of the same set of lines were found. 

   Implements a version of the Rabin-Karp algorithm:
   https://en.wikipedia.org/wiki/Rabin%E2%80%93Karp_algorithm .
*/
void 
log_out_cyclic(void* priv, const char* fmt, va_list ap)
{
    /* Get our new logging system instance. */
    log_t* log = (log_t*) priv;

    /* Does the log actually exist? */
    if (log == NULL)
        pclog("WARNING: Cyclical logging called with a NULL log pointer\n");
    else if (log->cyclic_buff == NULL)
        pclog("WARNING: Cyclical logging called with a non-cyclic log\n");
    else if (fmt == NULL)
        pclog("WARNING: Cyclical logging called with a NULL format pointer\n");
    /* Is the string empty? */
    else if (fmt[0] != '\0') {
        /* Ensure stdlog is open. */
        log_ensure_stdlog_open();

        char temp[LOG_SIZE_BUFFER] = {0};

        log->cyclic_last_line %= LOG_SIZE_BUFFER_CYCLIC_LINES;

        vsprintf(temp, fmt, ap);

        log_copy(log, log->cyclic_buff[log->cyclic_last_line], temp,
                 LOG_SIZE_BUFFER);

        uint32_t hashes[LOG_SIZE_BUFFER_CYCLIC_LINES] = {0};

        /* Random numbers. */
        uint32_t base = 257;
        uint32_t mod = 1000000007;

        uint32_t repeat_order = 0;
        bool is_cycle = false;

        /* Compute the set of hashes for the current log buffer. */
        for (int32_t log_line = 0; log_line < LOG_SIZE_BUFFER_CYCLIC_LINES;
             log_line++) {
            if (log->cyclic_buff[log_line][0] == '\0')
                continue;    /* Skip. */

            for (int32_t log_line_char = 0; log_line_char < LOG_SIZE_BUFFER;
                log_line_char++)
                hashes[log_line] = hashes[log_line] * base +
                    log->cyclic_buff[log_line][log_line_char] % mod;
        }

        /*
           Now see if there are real cycles.
           We implement a minimum repeat size.
         */
        for (int32_t check_size = LOG_MINIMUM_REPEAT_ORDER;
             check_size < LOG_SIZE_BUFFER_CYCLIC_LINES / 2; check_size++) {
            /*
               TODO: Log what we need for cycle 1.
               TODO: Command line option that lets us turn off this behaviour.
             */
            for (int32_t log_line_to_check = 0; log_line_to_check < check_size;
                 log_line_to_check++) {
                if (hashes[log_line_to_check] ==
                    hashes[(log_line_to_check + check_size) %
                    LOG_SIZE_BUFFER_CYCLIC_LINES]) {
                    repeat_order = check_size;
                    break;
                }
            }

            is_cycle = (repeat_order != 0);

            /* If there still is a cycle, break. */
            if (is_cycle)
                break;
            
        }

        if (is_cycle) {
            if (log->cyclic_last_line % repeat_order == 0) {
                log->log_cycles++;

                if (log->log_cycles == 1) {
                    /* 
                       'Replay' the last few log entries so they actually
                       show up.

                       TODO: Is this right?
                     */

                    for (uint32_t index = log->cyclic_last_line - 1;
                         index > (log->cyclic_last_line - repeat_order);
                         index--) {
                        /* *Very important* to prevent out of bounds index. */
                        uint32_t real_index = index %
                                              LOG_SIZE_BUFFER_CYCLIC_LINES;
                        log_copy(log, temp, log->cyclic_buff[real_index],
                                 LOG_SIZE_BUFFER);

                        fprintf(stdlog, "%s", log->cyclic_buff[real_index]);
                    }

                    /* Restore the original line. */
                    log_copy(log, temp,
                             log->cyclic_buff[log->cyclic_last_line],
                             LOG_SIZE_BUFFER);

                    /* Allow normal logging. */
                    fprintf(stdlog, "%s", temp);
                }

                if (log->log_cycles > 1 && log->log_cycles < 100)
                    fprintf(stdlog, "***** Cyclical Log Repeat of Order %d "
                            "#%d *****\n", repeat_order, log->log_cycles);
                else if (log->log_cycles == 100)
                    fprintf(stdlog, "Logged the same cycle 100 times... "
                            "Silence until something interesting happens\n");
            }
        } else {
            log->log_cycles = 0;
            fprintf(stdlog, "%s", temp);
        }

        log->cyclic_last_line++;
    }
}
#endif

void
log_fatal(void *priv, const char *fmt, ...)
{
    log_t  *log = (log_t *) priv;
    char    temp[1024];
    char    fmt2[1024];
    va_list ap;

    if (log == NULL)
        return;

    if (log->cyclic_buff != NULL) {
        for (int i = 0; i < LOG_SIZE_BUFFER_CYCLIC_LINES; i++)
            if (log->cyclic_buff[i] != NULL)
                free(log->cyclic_buff[i]);
        free(log->cyclic_buff);
    }

    va_start(ap, fmt);
    log_copy(log, fmt2, fmt, 1024);
    vsprintf(temp, fmt2, ap);
    fatal_ex(fmt2, ap);
    va_end(ap);
    exit(-1);
}

static void *
log_open_common(const char *dev_name, const int cyclic)
{
    log_t *log = calloc(1, sizeof(log_t));

    memcpy(log->dev_name, dev_name, strlen(dev_name) + 1);
    log->suppr_seen = 1;
    log->cyclic_last_line = 0;
    log->log_cycles = 0;

    if (cyclic) {
        log->cyclic_buff = calloc(LOG_SIZE_BUFFER_CYCLIC_LINES,
                                  sizeof(char *));
        for (int i = 0; i < LOG_SIZE_BUFFER_CYCLIC_LINES; i++)
            log->cyclic_buff[i] = calloc(LOG_SIZE_BUFFER, sizeof(char));
    }

    return (void *) log;
}

void *
log_open(const char *dev_name)
{
    return log_open_common(dev_name, 0);
}

/*
   This is so that not all logs get the 32k cyclical buffer
   they may not need.
 */
void *
log_open_cyclic(const char *dev_name)
{
    return log_open_common(dev_name, 1);
}

void
log_close(void *priv)
{
    log_t *log = (log_t *) priv;

    free(log);
}
