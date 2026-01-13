/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Standard input/output character device.
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
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <fcntl.h>
#elif defined(USE_LINUX_TERMIOS)
#    include <asm/termios.h>
#else
#    include <termios.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/plat.h>

#define ENABLE_STDIO_LOG 1
#ifdef ENABLE_STDIO_LOG
int stdio_do_log = ENABLE_STDIO_LOG;

static void
stdio_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (stdio_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define stdio_log(priv, fmt, ...)
#endif

typedef struct {
    void *log;
    char_port_t *port;

    FILE *prev_log;
#ifdef _WIN32
    HANDLE fd_in;
    HANDLE fd_out;
    DWORD saved_in_mode;
    DWORD saved_out_mode;
#else
    int fd_in;
    int fd_out;
#endif
} stdio_t;

static ssize_t
stdio_read(uint8_t *buf, ssize_t len, void *priv)
{
    stdio_t *dev = (stdio_t *) priv;

#ifdef _WIN32
    DWORD ret = 0;
    if (dev->fd_in) {
        while (len-- > 0) {
            DWORD events;
            if (GetNumberOfConsoleInputEvents(dev->fd_in, &events) && (events > 0)) {
                INPUT_RECORD ir;
                DWORD count;
                if (ReadConsoleInput(dev->fd_in, &ir, 1, &count) && (count > 0) &&
                    (ir.EventType == KEY_EVENT) && ir.Event.KeyEvent.bKeyDown) {
                    *buf++ = ir.Event.KeyEvent.uChar.AsciiChar;
                    ret++;
                }
            }
        }
    }
#else
    ssize_t ret = 0;
    if (dev->fd_in != -1) {
        ret = read(dev->fd_in, buf, len);
        if (ret == (ssize_t) -1)
            ret = 0;
    }
#endif

    return ret;
}

static ssize_t
stdio_write(uint8_t *buf, ssize_t len, void *priv)
{
    stdio_t *dev = (stdio_t *) priv;

#ifdef _WIN32
    DWORD ret = 0;
    if (dev->fd_out)
        WriteFile(dev->fd_out, buf, len, &ret, NULL);
#else
    ssize_t ret = 0;
    if (dev->fd_out) {
        do {
            ret = write(dev->fd_out, buf, len);
        } while ((ret == 0) || ((ret == (ssize_t) -1) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))));
        if (ret == (ssize_t) -1)
            ret = 0;
    }
#endif

    return ret;
}

static void
stdio_port_config(void *priv)
{
    stdio_t *dev = (stdio_t *) priv;
    (void) dev;
}

static void
stdio_control(uint32_t flags, void *priv)
{
    stdio_t *dev = (stdio_t *) priv;
    (void) dev;
}

static uint32_t
stdio_status(void *priv)
{
    return CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_DCD;
}

static void
stdio_close(void *priv)
{
    stdio_t *dev = (stdio_t *) priv;

    /* Reconnect logging if it had been disconnected. */
    if (dev->prev_log) {
        fclose(stdlog);
        stdlog = dev->prev_log;
    }

    stdio_log(dev->log, "close()\n");

#ifdef _WIN32
    if (!SetConsoleMode(dev->fd_in, dev->saved_in_mode))
        stdio_log(dev->log, "Input restore SetConsoleMode failed (%08X)\n", GetLastError());

    if (!SetConsoleMode(dev->fd_out, dev->saved_out_mode))
        stdio_log(dev->log, "Output restore SetConsoleMode failed (%08X)\n", GetLastError());
#endif

    free(dev);
}

static void *
stdio_init(const device_t *info)
{
    stdio_t *dev = (stdio_t *) calloc(1, sizeof(stdio_t));

    dev->log = log_open("StdIO");
    dev->port = (char_port_t *) info->local;
    stdio_log(dev->log, "init()\n");

    /* Set file descriptors. */
#ifdef _WIN32
    dev->fd_in = GetStdHandle(STD_INPUT_HANDLE);
    if (!dev->fd_in || !GetConsoleMode(dev->fd_in, &dev->saved_in_mode)) {
        pc_debug_console();
        dev->fd_in = GetStdHandle(STD_INPUT_HANDLE);
    }
    dev->fd_out = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Enable ANSI input. */
    if (!GetConsoleMode(dev->fd_in, &dev->saved_in_mode))
        stdio_log(dev->log, "Input GetConsoleMode failed (%08X)\n", GetLastError());
    if (!SetConsoleMode(dev->fd_in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS)) /* ENABLE_EXTENDED_FLAGS disables quickedit */
        stdio_log(dev->log, "Input SetConsoleMode failed (%08X)\n", GetLastError());

    /* Enable ANSI output. */
    if (!GetConsoleMode(dev->fd_out, &dev->saved_out_mode))
        stdio_log(dev->log, "Output GetConsoleMode failed (%08X)\n", GetLastError());
    if (!SetConsoleMode(dev->fd_out, dev->saved_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        stdio_log(dev->log, "Output SetConsoleMode failed (%08X)\n", GetLastError());
#else
    dev->fd_in = 2;
    dev->fd_out = 0;
#endif

    /* Disconnect logging from stdout. */
    if (stdlog == stdout) {
        stdio_log(dev->log, "Disconnecting logging from stdout\n");
        dev->prev_log = stdlog;
#ifdef _WIN32
        stdlog = plat_fopen("NUL", "w");
#else
        stdlog = plat_fopen("/dev/null", "w");
#endif
    }

    return dev;
}

const char_device_t stdio_device = {
    .device = {
        .name          = "Standard Input/Output",
        .internal_name = "stdio",
        .flags         = DEVICE_COM,
        .local         = 0,
        .init          = stdio_init,
        .close         = stdio_close,
        .reset         = NULL,
        .available     = NULL,
        .speed_changed = NULL,
        .force_redraw  = NULL,
        .config        = NULL
    },
    .flags       = 0,
    .read        = stdio_read,
    .write       = stdio_write,
    .port_config = stdio_port_config,
    .control     = stdio_control,
    .status      = stdio_status
};
