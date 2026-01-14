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
#include <unistd.h>
#include <fcntl.h>
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <errno.h>
#    include <fcntl.h>
#    include <termios.h>
#    include <unistd.h>
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
    uint16_t buf_in;
    DWORD prev_in_mode;
    DWORD prev_out_mode;
#else
    int fd_in;
    int fd_out;
    struct termios prev_config;
    int prev_flags;
#    define PREV_CONFIG_VALID 1
#    define PREV_FLAGS_VALID  2
    uint8_t prev_config_valid;
#endif
} stdio_t;

#ifdef _WIN32
static void
stdio_stdin_thread(void *priv)
{
    stdio_t *dev = (stdio_t *) priv;

    while (1) {
        DWORD read;
        if (!ReadFile(dev->fd_in, &dev->buf_in, 1, &read, NULL))
            break;
        if (read <= 0)
            continue;
        //thread_wait_event();
    }
}
#endif

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
        if (ret < 0)
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
    if (dev->fd_out != -1) {
        do {
            ret = write(dev->fd_out, buf, len);
        } while ((ret == 0) || ((ret < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))));
        if (ret < 0)
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
    stdio_t *dev = (stdio_t *) priv;

    uint32_t ret = 0;
#ifdef _WIN32
    if (dev->fd_in)
#else
    if (dev->fd_in != -1)
#endif
        ret |= CHAR_COM_DCD;
#ifdef _WIN32
    if (dev->fd_out)
#else
    if (dev->fd_out != -1)
#endif
        ret |= CHAR_COM_CTS;
    if (ret)
        ret |= CHAR_COM_DSR;
    return ret;
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

    /* Restore existing terminal configuration. */
#ifdef _WIN32
    if (!SetConsoleMode(dev->fd_in, dev->prev_in_mode))
        stdio_log(dev->log, "Input restore SetConsoleMode failed (%08X)\n", GetLastError());
    if (!SetConsoleMode(dev->fd_out, dev->prev_out_mode))
        stdio_log(dev->log, "Output restore SetConsoleMode failed (%08X)\n", GetLastError());
#else
    if (dev->prev_config_valid & PREV_CONFIG_VALID) {
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &dev->prev_config))
            stdio_log(dev->log, "Restore tcsetattr failed (%d)\n", errno);
    }
    if (dev->prev_config_valid & PREV_FLAGS_VALID)
        fcntl(dev->fd_out, F_SETFL, dev->prev_flags);
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
    if (!dev->fd_in || !GetConsoleMode(dev->fd_in, &dev->prev_in_mode)) {
        pc_debug_console();
        dev->fd_in = GetStdHandle(STD_INPUT_HANDLE);
    }
    dev->fd_out = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Enable raw and ANSI input. */
    if (dev->fd_in) {
        if (!GetConsoleMode(dev->fd_in, &dev->prev_in_mode))
            stdio_log(dev->log, "Input GetConsoleMode failed (%08X)\n", GetLastError());
        if (!SetConsoleMode(dev->fd_in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS)) /* ENABLE_EXTENDED_FLAGS disables quickedit */
            stdio_log(dev->log, "Input SetConsoleMode failed (%08X)\n", GetLastError());
    }

    /* Enable ANSI output. */
    if (dev->fd_out) {
        if (!GetConsoleMode(dev->fd_out, &dev->prev_out_mode))
            stdio_log(dev->log, "Output GetConsoleMode failed (%08X)\n", GetLastError());
        if (!SetConsoleMode(dev->fd_out, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            stdio_log(dev->log, "Output SetConsoleMode failed (%08X)\n", GetLastError());
    }
#else
    dev->fd_in = STDIN_FILENO;
    dev->fd_out = STDOUT_FILENO;

    /* Save current stdout flags for restoring on close. */
    dev->prev_flags = fcntl(dev->fd_out, F_GETFL);
    if (dev->prev_flags >= 0) {
        dev->prev_config_valid |= PREV_FLAGS_VALID;

        /* Enable non-blocking input. */
        if (fcntl(dev->fd_out, F_SETFL, dev->prev_flags | O_NONBLOCK))
            stdio_log(dev->log, "fcntl F_SETFL failed (%d)\n", errno);
    } else {
        stdio_log(dev->log, "fcntl F_GETFL failed (%d)\n", errno);
    }

    /* Save current terminal configuration for restoring on close. */
    if (!tcgetattr(STDIN_FILENO, &dev->prev_config)) {
        dev->prev_config_valid |= PREV_CONFIG_VALID;

        /* Enable raw input. */
        struct termios ios;
        memcpy(&ios, &dev->prev_config, sizeof(struct termios));
        ios.c_lflag &= ~(ECHO | ICANON | ISIG);
        ios.c_iflag &= ~IXON;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &ios))
            stdio_log(dev->log, "tcsetattr failed (%d)\n", errno);
    } else {
        stdio_log(dev->log, "tcgetattr failed (%d)\n", errno);
    }
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
