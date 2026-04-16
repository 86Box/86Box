/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Standard input/output and pseudoterminal character device.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#ifndef __APPLE__
#    define _XOPEN_SOURCE   600
#    define _DEFAULT_SOURCE 1
#    define _BSD_SOURCE     1
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#    define __BSD_VISIBLE 1
#endif
#ifdef __NetBSD__
#    define _NETBSD_VISIBLE 1
#    define _NETBSD_SOURCE  1
#endif
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
#include <86box/ui.h>

#define CHAR_STDIO_DEFAULT_CMD "screen -dmS \"$VMNAME.$PORT\" \"$PTY\""

#ifdef ENABLE_CHAR_STDIO_LOG
int char_stdio_do_log = ENABLE_CHAR_STDIO_LOG;

static void
char_stdio_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_stdio_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_stdio_log(priv, fmt, ...)
#endif

typedef struct {
    void        *log;
    char_port_t *port;

    FILE *prev_log;
#ifdef _WIN32
    HANDLE     fd_in;
    HANDLE     fd_out;
    int        prev_in_mode_valid  : 1;
    int        prev_out_mode_valid : 1;
    DWORD      prev_in_mode;
    DWORD      prev_out_mode;
    char      *prev_title;
    ATOMIC_INT buf_in_valid;
    uint8_t    buf_in;
    thread_t  *thread_in;
    event_t   *event_in;
#else
    int            fd_in;
    int            fd_out;
    int            prev_config_valid : 1;
    int            prev_flags_valid  : 1;
    struct termios prev_config;
    int            prev_flags;
#endif
} char_stdio_t;

#ifdef _WIN32
static int stdio_claimed = 0;

static void
char_stdio_stdin_thread(void *priv)
{
    char_stdio_t *dev = (char_stdio_t *) priv;

    while (CHAR_FD_VALID(dev->fd_in)) {
        DWORD read;
        if (!ReadFile(dev->fd_in, &dev->buf_in, 1, &read, NULL))
            break;
        if (read <= 0)
            continue;
        dev->buf_in_valid = 1;
        thread_wait_event(dev->event_in, -1);
        thread_reset_event(dev->event_in);
    }

    dev->thread_in = NULL;
    dev->fd_in     = INVALID_HANDLE_VALUE;
}
#endif

static size_t
char_stdio_read(uint8_t *buf, size_t len, void *priv)
{
    char_stdio_t *dev = (char_stdio_t *) priv;

#ifdef _WIN32
    if (dev->thread_in) {
        if (dev->buf_in_valid && (len > 0)) {
            *buf              = dev->buf_in;
            dev->buf_in_valid = 0;
            thread_set_event(dev->event_in);
            return 1;
        } else {
            return 0;
        }
    }

    size_t ret = 0;
    if (CHAR_FD_VALID(dev->fd_in)) {
        while (len-- > 0) {
            DWORD count;
            if (GetNumberOfConsoleInputEvents(dev->fd_in, &count) && (count > 0)) {
                INPUT_RECORD ir;
                if (ReadConsoleInput(dev->fd_in, &ir, 1, &count) && (count > 0) && (ir.EventType == KEY_EVENT) && ir.Event.KeyEvent.bKeyDown && ir.Event.KeyEvent.uChar.AsciiChar) {
                    *buf++ = ir.Event.KeyEvent.uChar.AsciiChar;
                    ret++;
                }
            }
        }
    }
#else
    ssize_t ret = 0;
    if (CHAR_FD_VALID(dev->fd_in)) {
        ret = read(dev->fd_in, buf, len);
        if (ret < 0)
            ret = 0;
    }
#endif

    return ret;
}

static size_t
char_stdio_write(uint8_t *buf, size_t len, void *priv)
{
    char_stdio_t *dev = (char_stdio_t *) priv;

#ifdef _WIN32
    DWORD ret = 0;
    if (CHAR_FD_VALID(dev->fd_out))
        WriteFile(dev->fd_out, buf, len, &ret, NULL);
#else
    ssize_t ret = 0;
    if (CHAR_FD_VALID(dev->fd_out)) {
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
char_stdio_status(void *priv)
{
    char_stdio_t *dev = (char_stdio_t *) priv;

    return (CHAR_FD_VALID(dev->fd_in) ? (CHAR_COM_DSR | CHAR_COM_DCD) : CHAR_RX_DISCONNECTED) | (CHAR_FD_VALID(dev->fd_out) ? CHAR_COM_CTS : CHAR_TX_DISCONNECTED);
}

static void
char_stdio_control(uint32_t flags, void *priv)
{
    char_stdio_t *dev = (char_stdio_t *) priv;

    if (flags & CHAR_COM_BREAK) {
        uint8_t brk = 0;
        char_stdio_write(&brk, sizeof(brk), dev);
    }
}

#ifdef USE_NEW_DYNAREC
extern FILE *stdlog;
#endif

static void
char_stdio_close(void *priv)
{
    char_stdio_t *dev = (char_stdio_t *) priv;

    /* Resume logging to stdout if it had been stopped. */
    if (dev->prev_log) {
        fclose(stdlog);
        stdlog = dev->prev_log;
    }

    char_stdio_log(dev->log, "close()\n");

    /* Restore existing terminal configuration. */
#ifdef _WIN32
    if (dev->thread_in) {
        /* Terminate input thread if it's being used. */
        char_stdio_log(dev->log, "Waiting for stdin thread to terminate...");
        HANDLE fd_in = dev->fd_in;
        dev->fd_in   = NULL;
        CancelIoEx(fd_in, NULL);
        thread_set_event(dev->event_in);
        thread_wait(dev->thread_in);
        char_stdio_log(dev->log, " done.\n");
    }
    if (dev->event_in)
        thread_destroy_event(dev->event_in);
    if (dev->prev_in_mode_valid && CHAR_FD_VALID(dev->fd_in) && !SetConsoleMode(dev->fd_in, dev->prev_in_mode))
        char_stdio_log(dev->log, "Input restore SetConsoleMode failed (%08X)\n", GetLastError());
    if (dev->prev_out_mode_valid && CHAR_FD_VALID(dev->fd_out) && !SetConsoleMode(dev->fd_out, dev->prev_out_mode))
        char_stdio_log(dev->log, "Output restore SetConsoleMode failed (%08X)\n", GetLastError());

    /* Release console. */
    stdio_claimed = 0;
    if (dev->prev_title) { /* reset title */
        SetConsoleTitle(dev->prev_title);
        free(dev->prev_title);
    }
#else
    if (dev->prev_config_valid && CHAR_FD_VALID(dev->fd_in) && tcsetattr(dev->fd_in, TCSAFLUSH, &dev->prev_config))
        char_stdio_log(dev->log, "Restore TCSAFLUSH failed (%d)\n", errno);
    if (dev->prev_flags_valid && CHAR_FD_VALID(dev->fd_out) && (fcntl(dev->fd_out, F_SETFL, dev->prev_flags) < 0))
        char_stdio_log(dev->log, "Restore F_SETFL failed (%d)\n", errno);

    /* Terminate pseudoterminal if we have one. */
    if (CHAR_FD_VALID(dev->fd_out) && (dev->fd_out != STDOUT_FILENO))
        close(dev->fd_out);
#endif

    log_close(dev->log);

    free(dev);
}

static void *
char_stdio_init(const device_t *info)
{
    char_stdio_t *dev = (char_stdio_t *) calloc(1, sizeof(char_stdio_t));

    /* Attach character device. */
    dev->port = char_attach(0, char_stdio_read, char_stdio_write, char_stdio_status, char_stdio_control, NULL, dev);
    dev->log  = char_log_open(dev->port, "StdIO");
    char_stdio_log(dev->log, "init()\n");

#ifdef _WIN32
    /* Check if another instance has already claimed the console. */
    char msg[2048];
    if (stdio_claimed) {
        char_stdio_log(dev->log, "Windows console already claimed\n");

        snprintf(msg, sizeof(msg), "%s: Only one virtual console can be used on Windows", dev->port->name);
        ui_msgbox(MBX_INFO | MBX_ANSI, msg);

        dev->fd_in = dev->fd_out = INVALID_HANDLE_VALUE;
        char_update_status(dev->port);
        return dev;
    }
    stdio_claimed = 1;

    /* Set file descriptors. */
    dev->fd_in = GetStdHandle(STD_INPUT_HANDLE);
    if (!CHAR_FD_VALID(dev->fd_in)) {
        /* Spawn a console if one isn't present. (GUI executable) */
        char_stdio_log(dev->log, "No Windows console, spawning one\n");
        pc_debug_console();
        dev->fd_in = GetStdHandle(STD_INPUT_HANDLE);
    }
    dev->fd_out = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Set console title. */
    if (CHAR_FD_VALID(dev->fd_in) || CHAR_FD_VALID(dev->fd_out)) {
        if ((GetConsoleTitle(msg, sizeof(msg)) > 0) || (GetConsoleOriginalTitle(msg, sizeof(msg)) > 0))
            dev->prev_title = strdup(msg);
        snprintf(msg, sizeof(msg), "%s [%s]", vm_name, dev->port->name);
        SetConsoleTitle(msg);
    }

    /* Discover what kind of stdin we're dealing with. */
    if (CHAR_FD_VALID(dev->fd_in)) {
        dev->prev_in_mode_valid = !!GetConsoleMode(dev->fd_in, &dev->prev_in_mode);
        if (dev->prev_in_mode_valid) {
            /* Proper console (CLI executable or spawned earlier), enable raw and ANSI output and use console events. */
            if (!SetConsoleMode(dev->fd_in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS)) /* ENABLE_EXTENDED_FLAGS disables quickedit */
                char_stdio_log(dev->log, "Input SetConsoleMode failed (%08X)\n", GetLastError());
        } else {
            /* Redirected (or MSYS2), use file I/O. */
            char_stdio_log(dev->log, "Input GetConsoleMode failed (%08X)\n", GetLastError());
            dev->event_in  = thread_create_event();
            dev->thread_in = thread_create(char_stdio_stdin_thread, dev);
        }
    }

    /* Enable ANSI output if we're on a proper console. */
    if (CHAR_FD_VALID(dev->fd_out)) {
        dev->prev_out_mode_valid = !!GetConsoleMode(dev->fd_out, &dev->prev_out_mode);
        if (!dev->prev_out_mode_valid)
            char_stdio_log(dev->log, "Output GetConsoleMode failed (%08X)\n", GetLastError());
        if (!SetConsoleMode(dev->fd_out, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            char_stdio_log(dev->log, "Output SetConsoleMode failed (%08X)\n", GetLastError());
    }
#else
    int mode = device_get_config_int("mode");
    if (mode != CHAR_STDIO_MODE_STDIO) {
        /* Create pseudoterminal. */
        char msg[2048];
        int  err;
        dev->fd_in = dev->fd_out = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        fcntl(dev->fd_out, F_SETFD, FD_CLOEXEC); /* required for any commands we run to properly detach from the pty when it's closed */
        if (dev->fd_out >= 0) {
            if (grantpt(dev->fd_out) >= 0) {
                if (unlockpt(dev->fd_out) >= 0) {
                    char *pty = ptsname(dev->fd_out);
                    if (pty) {
                        char_stdio_log(dev->log, "Bound to %s\n", pty);

#    ifndef __APPLE__ /* pty master is not a terminal on macOS, flags must be set by the other end */
                        /* Enable raw input. */
                        tcgetattr(dev->fd_out, &dev->prev_config);
                        cfmakeraw(&dev->prev_config);
                        if (tcsetattr(dev->fd_out, TCSANOW, &dev->prev_config))
                            char_stdio_log(dev->log, "tcsetattr failed (%d)\n", errno);
#    endif

                        if (mode == CHAR_STDIO_MODE_PTY) {
                            snprintf(msg, sizeof(msg), "%s: Attached to %s", dev->port->name, pty);
                            ui_msgbox(MBX_INFO | MBX_ANSI, msg);
                        } else {
                            /* Build environment variables. */
                            static const char *pipe_cmd = "PIPECMD="
                                                          "exec 2>/dev/null;" /* suppress stderr output */
                                                          "stty raw -echo;"   /* enable raw input on terminal */
                                                          "(stty raw -echo;"  /* enable raw input on pty (for macOS) */
                                                          "cat;"              /* pipe to stdout... */
                                                          "exec kill $$)"     /* (stop script once the read connection is broken) */
                                                          "<\"$PTY\"&"        /* ...from pty in the background */
                                                          "clear;"            /* suppress background task indicator (zsh prints it to stdout) */
                                                          "cat>\"$PTY\";"     /* pipe from stdin to pty */
                                                          "exec kill $!";     /* stop script once the write connection is broken */
                            char env[3][2048];
                            snprintf(env[0], sizeof(env[0]), "PTY=%s", pty);
                            snprintf(env[1], sizeof(env[1]), "VMNAME=%s", vm_name);
                            snprintf(env[2], sizeof(env[2]), "PORT=%s", dev->port->name);

                            /* Determine command to execute. */
                            const char *cmd;
                            if (mode == CHAR_STDIO_MODE_TERM) {
                                cmd = "eval $PIPECMD";
                            } else {
                                cmd = device_get_config_string("command");
                                if (!cmd || !cmd[0]) {
                                    cmd = CHAR_STDIO_DEFAULT_CMD;
                                    device_set_config_string("command", cmd);
                                }
                            }

                            /* Determine whether or not the command should be executed on a terminal. */
                            if ((mode == CHAR_STDIO_MODE_TERM) || device_get_config_int("command_terminal"))
                                snprintf(msg, sizeof(msg), "%s\n%s", vm_name, dev->port->name);
                            else
                                msg[0] = '\0';

                            /* Execute command. */
                            if (!plat_run_command(cmd, (const char *[]) { pipe_cmd, env[0], env[1], env[2], NULL }, msg[0] ? msg : NULL))
                                char_stdio_log(dev->log, "plat_run_command(%s) failed\n", cmd);
                        }
                    } else {
                        err = errno;
                        char_stdio_log(dev->log, "ptsname failed (%d)\n", err);
                        goto errmsg;
                    }
                } else {
                    err = errno;
                    char_stdio_log(dev->log, "unlockpt failed (%d)\n", err);
                    goto errmsg;
                }
            } else {
                err = errno;
                char_stdio_log(dev->log, "grantpt failed (%d)\n", err);
                goto errmsg;
            }
        } else {
            err = errno;
            char_stdio_log(dev->log, "posix_openpt failed (%d)\n", err);
errmsg:
            snprintf(msg, sizeof(msg), "%s: Could not create pseudoterminal: %s", dev->port->name, strerror(err));
            ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
            close(dev->fd_out);
            dev->fd_out = -1;
        }

        /* No terminal configuration or logging redirection required. */
        char_update_status(dev->port);
        return dev;
    }

    /* Set file descriptors. */
    dev->fd_in  = STDIN_FILENO;
    dev->fd_out = STDOUT_FILENO;

    /* Save current stdout flags for restoring on close. */
    dev->prev_flags = fcntl(dev->fd_out, F_GETFL);
    if (dev->prev_flags >= 0) {
        dev->prev_flags_valid = 1;

        /* Enable non-blocking input. */
        if (fcntl(dev->fd_out, F_SETFL, dev->prev_flags | O_NONBLOCK))
            char_stdio_log(dev->log, "F_SETFL failed (%d)\n", errno);
    } else {
        char_stdio_log(dev->log, "F_GETFL failed (%d)\n", errno);
    }

    /* Save current terminal configuration for restoring on close. */
    struct termios port_config = { 0 };
    dev->prev_config_valid     = !tcgetattr(dev->fd_in, &port_config);
    if (dev->prev_config_valid)
        memcpy(&dev->prev_config, &port_config, sizeof(port_config));
    else
        char_stdio_log(dev->log, "tcgetattr failed (%d)\n", errno);

    /* Enable raw input. */
    cfmakeraw(&port_config);
    if (tcsetattr(dev->fd_in, TCSANOW, &port_config))
        char_stdio_log(dev->log, "Raw mode tcsetattr failed (%d)\n", errno);
#endif

    /* Stop logging to stdout. */
    if (stdlog == stdout) {
        char_stdio_log(dev->log, "Disconnecting logging from stdout\n");
        dev->prev_log = stdlog;
#ifdef _WIN32
        stdlog = plat_fopen("NUL", "w");
#else
        stdlog = plat_fopen("/dev/null", "w");
#endif
    }

    char_update_status(dev->port);
    return dev;
}

#ifndef _WIN32
// clang-format off
static const device_config_t char_stdio_config[] = {
    {
        .name         = "mode",
        .description  = "Mode",
        .type         = CONFIG_SELECTION,
        .default_int  = CHAR_STDIO_MODE_TERM,
        .selection    = {
            { .description = "Use standard input/output", .value = CHAR_STDIO_MODE_STDIO },
            { .description = "Create pseudoterminal",     .value = CHAR_STDIO_MODE_PTY   },
            { .description = "Start terminal emulator",   .value = CHAR_STDIO_MODE_TERM  },
            { .description = "Run custom command",        .value = CHAR_STDIO_MODE_CMD   },
            { NULL                                                                       }
        }
    },
    {
        .name           = "command",
        .description    = "Custom command",
        .type           = CONFIG_STRING,
        .default_string = CHAR_STDIO_DEFAULT_CMD,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "command_terminal",
        .description    = "Run custom command in terminal",
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
#endif

const device_t char_stdio_com_device = {
    .name          = "Virtual Console (COM)",
    .internal_name = "stdio",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = char_stdio_init,
    .close         = char_stdio_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
#ifdef _WIN32
    .config = NULL
#else
    .config = char_stdio_config
#endif
};
