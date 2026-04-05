/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          File character device.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/plat.h>

#define ENABLE_CHAR_FILE_LOG 1
#ifdef ENABLE_CHAR_FILE_LOG
int char_file_do_log = ENABLE_CHAR_FILE_LOG;

static void
char_file_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_file_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_file_log(priv, fmt, ...)
#endif

typedef struct {
    void *log;
    char_port_t *port;
    FILE *file_in;
    FILE *file_out;
    int loop_in : 1;
} char_file_t;

static ssize_t
char_file_read(uint8_t *buf, ssize_t len, void *priv)
{
    char_file_t *dev = (char_file_t *) priv;

    if (!dev->file_in)
        return 0;

    size_t ret = 0;
    for (int looped = 0; looped <= 1; looped++) {
        /* Read as many bytes as we can... */
        size_t read_count;
        while ((len > 0) && (read_count = fread(buf, 1, len, dev->file_in))) {
            ret += read_count;
            buf += read_count;
            len -= read_count;
        }
        if ((ret >= len) || looped)
            break;

        /* ...then loop back if requested. */
        if (dev->loop_in) {
            char_file_log(dev->log, "Looping input file\n");
            fseek(dev->file_in, 0, SEEK_SET);
        } else {
            char_file_log(dev->log, "Reached end of input file, closing\n");
            fclose(dev->file_in);
            dev->file_in = NULL;
            break;
        }
    }
    return ret;
}

static ssize_t
char_file_write(uint8_t *buf, ssize_t len, void *priv)
{
    char_file_t *dev = (char_file_t *) priv;

    if (!dev->file_out)
        return 0;

    size_t ret = 0;
    size_t write_count;
    while ((len > 0) && (write_count = fwrite(buf, 1, len, dev->file_out))) {
        ret += write_count;
        buf += write_count;
        len -= write_count;
    }
    return ret;
}

static uint32_t
char_file_status(void *priv)
{
    char_file_t *dev = (char_file_t *) priv;

    uint32_t ret = 0;
    if (dev->file_in)
        ret |= CHAR_COM_DCD;
    if (dev->file_out)
        ret |= CHAR_COM_CTS;
    ret |= ret ? CHAR_COM_DSR : CHAR_DISCONNECTED;
    return ret;
}

static void
char_file_close(void *priv)
{
    char_file_t *dev = (char_file_t *) priv;

    char_file_log(dev->log, "close()\n");

    if (dev->file_in)
        fclose(dev->file_in);
    if (dev->file_out)
        fclose(dev->file_out);
}

static void *
char_file_init(const device_t *info)
{
    char_file_t *dev = (char_file_t *) calloc(1, sizeof(char_file_t));

    dev->log = log_open("Host File");
    char_file_log(dev->log, "init()\n");

    /* Attach character device. */
    dev->port = char_attach(0, char_file_read, char_file_write, char_file_status, NULL, NULL, dev);

    char *path = ini_get_string(dev->port->config, "", "path", NULL);
    if (path) {
        dev->file_out = plat_fopen(path, ini_get_int(dev->port->config, "", "append", 0) ? "ab" : "wb");
        char_file_log(dev->log, "%s output file [%s]\n", dev->file_out ? "Opened" : "Could not open", path);
    } else {
        char_file_log(dev->log, "No output file specified\n");
    }
    path = ini_get_string(dev->port->config, "", "input_path", NULL);
    if (path) {
        dev->file_in = plat_fopen(path, "rb");
        char_file_log(dev->log, "%s input file [%s]\n", dev->file_in ? "Opened" : "Could not open", path);
    } else {
        char_file_log(dev->log, "No input file specified\n");
    }

    dev->loop_in = !!ini_get_int(dev->port->config, "", "input_loop", 0);

    return dev;
}

// clang-format off
static const device_config_t char_file_config[] = {
    {
        .name           = "path",
        .description    = "Output file path",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "append",
        .description    = "Append to file if it exists",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "input_path",
        .description    = "Input file path",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "input_loop",
        .description    = "Loop input file",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t char_file_device = {
    .name          = "File",
    .internal_name = "file",
    .flags         = DEVICE_COM | DEVICE_LPT,
    .local         = 0,
    .init          = char_file_init,
    .close         = char_file_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = char_file_config
};
