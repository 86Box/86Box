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
#include <86box/thread.h>

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
    int prev_in_mode_valid : 1;
    int prev_out_mode_valid : 1;
    DWORD prev_in_mode;
    DWORD prev_out_mode;
    ATOMIC_INT buf_in_valid;
    uint8_t buf_in;
    thread_t *thread_in;
    event_t *event_in;
#else
    int fd_in;
    int fd_out;
    int prev_config_valid : 1;
    int prev_flags_valid : 1;
    struct termios prev_config;
    int prev_flags;
#endif
} stdio_t;

#ifdef _WIN32
static void
stdio_stdin_thread(void *priv)
{
    stdio_t *dev = (stdio_t *) priv;

    while (dev->fd_in) {
        DWORD read;
        if (!ReadFile(dev->fd_in, &dev->buf_in, 1, &read, NULL))
            break;
        if (read <= 0)
            continue;
        dev->buf_in_valid = 1;
        thread_wait_event(dev->event_in, -1);
        thread_reset_event(dev->event_in);
    }
}
#endif

static ssize_t
stdio_read(uint8_t *buf, ssize_t len, void *priv)
{
    stdio_t *dev = (stdio_t *) priv;

    ssize_t ret = 0;
#ifdef _WIN32
    if (dev->thread_in) {
        if (dev->buf_in_valid && (len > 0)) {
            *buf = dev->buf_in;
            dev->buf_in_valid = 0;
            thread_set_event(dev->event_in);
            return 1;
        } else {
            return 0;
        }
    }

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
    ret |= ret ? CHAR_COM_DSR : CHAR_DISCONNECTED;
    return ret;
}

static void
stdio_control(uint32_t flags, void *priv)
{
    stdio_t *dev = (stdio_t *) priv;
    
    if (flags & CHAR_COM_BREAK) {
        uint8_t brk = 0;
        stdio_write(&brk, sizeof(brk), dev);
    }
}

static void
stdio_close(void *priv)
{
    stdio_t *dev = (stdio_t *) priv;

    /* Resume logging to stdout if it had been stopped. */
    if (dev->prev_log) {
        fclose(stdlog);
        stdlog = dev->prev_log;
    }

    stdio_log(dev->log, "close()\n");

    /* Restore existing terminal configuration. */
#ifdef _WIN32
    if (dev->thread_in) {
        /* Terminate input thread if it's being used. */
        stdio_log(dev->log, "Waiting for stdin thread to terminate...");
        HANDLE fd_in = dev->fd_in;
        dev->fd_in = NULL;
        CancelIoEx(fd_in, NULL);
        thread_set_event(dev->event_in);
        thread_wait(dev->thread_in);
        stdio_log(dev->log, " done.\n");
    }
    if (dev->event_in)
        thread_destroy_event(dev->event_in);
    if (dev->prev_in_mode_valid && !SetConsoleMode(dev->fd_in, dev->prev_in_mode))
        stdio_log(dev->log, "Input restore SetConsoleMode failed (%08X)\n", GetLastError());
    if (dev->prev_out_mode_valid && !SetConsoleMode(dev->fd_out, dev->prev_out_mode))
        stdio_log(dev->log, "Output restore SetConsoleMode failed (%08X)\n", GetLastError());
#else
    if (dev->prev_config_valid && tcsetattr(STDIN_FILENO, TCSAFLUSH, &dev->prev_config))
        stdio_log(dev->log, "Restore TCSAFLUSH failed (%d)\n", errno);
    if (dev->prev_flags_valid && (fcntl(dev->fd_out, F_SETFL, dev->prev_flags) < 0))
        stdio_log(dev->log, "Restore F_SETFL failed (%d)\n", errno);
#endif

    free(dev);
}

static void *
stdio_init(const device_t *info)
{
    stdio_t *dev = (stdio_t *) calloc(1, sizeof(stdio_t));

    dev->log = log_open("StdIO");
    stdio_log(dev->log, "init()\n");

    /* Attach character device. */
    dev->port = char_attach(0, stdio_read, stdio_write, stdio_status, stdio_control, NULL, dev);

#ifdef _WIN32
    /* Spawn a console if required. (GUI executable) */
    dev->fd_in = GetStdHandle(STD_INPUT_HANDLE);
    if (!dev->fd_in) {
        stdio_log(dev->log, "No Windows console, spawning one\n");
        pc_debug_console();
        dev->fd_in = GetStdHandle(STD_INPUT_HANDLE);
    }
    dev->fd_out = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Discover what kind of stdin we're dealing with. */
    if (dev->fd_in) {
        dev->prev_in_mode_valid = !!GetConsoleMode(dev->fd_in, &dev->prev_in_mode);
        if (dev->prev_in_mode_valid) {
            /* Proper console (CLI executable or spawned earlier), enable raw and ANSI output and use console events. */
            if (!SetConsoleMode(dev->fd_in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS)) /* ENABLE_EXTENDED_FLAGS disables quickedit */
                stdio_log(dev->log, "Input SetConsoleMode failed (%08X)\n", GetLastError());
        } else {
            /* Redirected (or MSYS2), use file I/O. */
            stdio_log(dev->log, "Input GetConsoleMode failed (%08X)\n", GetLastError());
            dev->event_in = thread_create_event();
            dev->thread_in = thread_create(stdio_stdin_thread, dev);
        }
    }

    /* Enable ANSI output if we're on a proper console. */
    if (dev->fd_out) {
        dev->prev_out_mode_valid = !!GetConsoleMode(dev->fd_out, &dev->prev_out_mode);
        if (!dev->prev_out_mode_valid)
            stdio_log(dev->log, "Output GetConsoleMode failed (%08X)\n", GetLastError());
        if (!SetConsoleMode(dev->fd_out, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            stdio_log(dev->log, "Output SetConsoleMode failed (%08X)\n", GetLastError());
    }
#else
    /* Set file descriptors. */
    dev->fd_in = STDIN_FILENO;
    dev->fd_out = STDOUT_FILENO;

    /* Save current stdout flags for restoring on close. */
    dev->prev_flags = fcntl(dev->fd_out, F_GETFL);
    if (dev->prev_flags >= 0) {
        dev->prev_flags_valid = 1;

        /* Enable non-blocking input. */
        if (fcntl(dev->fd_out, F_SETFL, dev->prev_flags | O_NONBLOCK))
            stdio_log(dev->log, "F_SETFL failed (%d)\n", errno);
    } else {
        stdio_log(dev->log, "F_GETFL failed (%d)\n", errno);
    }

    /* Save current terminal configuration for restoring on close. */
    if (!tcgetattr(STDIN_FILENO, &dev->prev_config)) {
        dev->prev_config_valid = 1;

        /* Enable raw input. */
        struct termios ios;
        memcpy(&ios, &dev->prev_config, sizeof(struct termios));
        ios.c_lflag &= ~(ECHO | ICANON | ISIG);
        ios.c_iflag &= ~IXON;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &ios))
            stdio_log(dev->log, "TCSAFLUSH failed (%d)\n", errno);
    } else {
        stdio_log(dev->log, "tcgetattr failed (%d)\n", errno);
    }
#endif

    /* Stop logging to stdout. */
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

const device_t stdio_device = {
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
};
