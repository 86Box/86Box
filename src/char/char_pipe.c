/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Windows named pipe and UNIX FIFO character device.
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
#else
#    include <errno.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/plat.h>
#include <86box/ui.h>

#ifdef ENABLE_CHAR_PIPE_LOG
int char_pipe_do_log = ENABLE_CHAR_PIPE_LOG;

static void
char_pipe_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_pipe_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_pipe_log(priv, fmt, ...)
#endif

typedef struct {
    void        *log;
    char_port_t *port;
    int          mode;
    uint32_t     last_connect_attempt;
    int          reconnect : 1;
    int          server    : 1;
#ifdef _WIN32
    int    block_connect : 1;
    char   path[257]; /* "The entire pipe name string can be up to 256 characters long." */
    HANDLE fd;
#else
    int   block_connect_in  : 1;
    int   block_connect_out : 1;
    int   direction         : 1;
    char *path_in;
    char *path_out;
    int   fd_in;
    int   fd_out;
#endif
} char_pipe_t;

static void
char_pipe_disconnect(char_pipe_t *dev, int in)
{
#ifdef _WIN32
    if (CHAR_FD_VALID(dev->fd)) {
        FlushFileBuffers(dev->fd);
        DisconnectNamedPipe(dev->fd);
        CloseHandle(dev->fd);
        dev->fd = INVALID_HANDLE_VALUE;
    }
#else
    if ((in != 0) && CHAR_FD_VALID(dev->fd_in)) {
        close(dev->fd_in);
        dev->fd_in = -1;
    }
    if ((in <= 0) && CHAR_FD_VALID(dev->fd_out)) {
        close(dev->fd_out);
        dev->fd_out = -1;
    }
#endif

    char_update_status(dev->port);
}

static int
char_pipe_connect(char_pipe_t *dev, int startup)
{
    /* Limit the connection attempt rate. */
    uint32_t now = plat_get_ticks();
    if (LIKELY(!startup) && ((now - dev->last_connect_attempt) < CHAR_RECONNECT_MS))
        return 0;
    dev->last_connect_attempt = now;

    /* Stop if there's nothing to connect to. */
#ifdef _WIN32
    if (!dev->path[0])
#else
    if (!(dev->path_in && dev->path_in[0]) && !(dev->path_out && dev->path_out[0]))
#endif
    {
        if (startup)
            char_pipe_log(dev->log, "No path specified\n");
        return 0;
    }

    char msg[1024];
#ifdef _WIN32
    /* Connect or create pipe. */
    char fmt[512];
    DWORD create_err = 0;
    if (dev->mode != CHAR_PIPE_MODE_CLIENT) {
        dev->fd = CreateNamedPipeA(dev->path, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, NMPWAIT_USE_DEFAULT_WAIT, NULL);
        if (CHAR_FD_VALID(dev->fd)) {
            char_pipe_log(dev->log, "Created new pipe: %s\n", dev->path);
            dev->block_connect = !dev->reconnect;
            dev->server        = 1;
        } else {
            create_err = GetLastError();
            char_pipe_log(dev->log, "CreateNamedPipeA failed (%08X)\n", create_err);

            snprintf(fmt, sizeof(fmt), "FormatMessageA failed");
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, create_err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), fmt, sizeof(fmt), NULL);
            snprintf(msg, sizeof(msg), "%s: Could not create %s: %s", dev->port->name, dev->path, fmt);
            if (dev->mode == CHAR_PIPE_MODE_SERVER)
                goto errmsg;
            else
                goto client;
        }
    } else {
client:
        dev->fd = CreateFileA(dev->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (CHAR_FD_VALID(dev->fd)) {
            char_pipe_log(dev->log, "Connected to existing pipe: %s\n", dev->path);
            dev->block_connect = !dev->reconnect;
            dev->server        = 0;

            /* Configure client pipe. */
            DWORD mode = PIPE_READMODE_BYTE | PIPE_WAIT;
            SetNamedPipeHandleState(dev->fd, &mode, NULL, NULL);
        } else {
            DWORD err = GetLastError();
            char_pipe_log(dev->log, "CreateFileA failed (%08X)\n", err);

            if (dev->mode != CHAR_PIPE_MODE_CLIENT) {                              /* on auto mode, delay connect failed message to here */
                if ((create_err == ERROR_PIPE_BUSY) && (err == ERROR_PIPE_BUSY)) { /* special case to mitigate race condition when switching modes after the other end hard resets (by deferring a retry) */
                    dev->block_connect = 1;                                        /* avoid infinite loop in char_update_status */
                    char_update_status(dev->port);
                    dev->block_connect = 0;
                    return 0;
                }
                if (startup)
                    ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
                else
                    char_pipe_log(dev->log, "%s\n", msg);
            }
            snprintf(fmt, sizeof(fmt), "FormatMessageA failed");
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), fmt, sizeof(fmt), NULL);
            snprintf(msg, sizeof(msg), "%s: Could not connect to %s: %s", dev->port->name, dev->path, fmt);
            goto errmsg;
        }
    }
#else
    /* Determine pipe direction. */
    int out_test_fd = -1;
    if (dev->mode == CHAR_PIPE_MODE_SERVER) {
        dev->direction = 0;
    } else if (dev->mode == CHAR_PIPE_MODE_CLIENT) {
        dev->direction = 1;
    } else if (!CHAR_FD_VALID(dev->fd_out) && !CHAR_FD_VALID(dev->fd_in)) { /* autodetection can only be performed if .in isn't already open on this end */
        out_test_fd    = open(dev->path_in, O_WRONLY | O_NONBLOCK);         /* fails if nobody is connected; if successful, fd reused further below */
        dev->direction = CHAR_FD_VALID(out_test_fd);
        char_pipe_log(dev->log, "Autodetected direction: %s\n", dev->direction ? "client" : "server");
    }

    /* Connect or create pipes. */
    for (int i = 0; i <= 1; i++) {
        /* Skip pipe if it's already connected. */
        int *target = (i == 0) ? &dev->fd_out : &dev->fd_in;
        if (CHAR_FD_VALID(*target))
            continue;

        const char *path = ((i ^ !!dev->direction) == 0) ? dev->path_out : dev->path_in;
        int create_err   = 0;
        if (CHAR_FD_VALID(out_test_fd)) {
            /* Reuse file descriptor from earlier test. */
            *target     = out_test_fd;
            out_test_fd = -1;
        } else {
            /* Create pipe if it doesn't exist. */
            if (mkfifo(path, 0666)) {
                create_err = errno;
                char_pipe_log(dev->log, "%s mkfifo failed (%d)\n", (i == 0) ? "Output" : "Input", create_err);
            }

            /* Connect to pipe. */
            *target = open(path, ((i == 0) ? O_WRONLY : O_RDONLY) | O_NONBLOCK);
        }
        if (CHAR_FD_VALID(*target)) {
            char_pipe_log(dev->log, "Connected to %s pipe: %s\n", (i == 0) ? "output" : "input", path);
            if (i == 0)
                dev->block_connect_out = !dev->reconnect;
            else
                dev->block_connect_in = !dev->reconnect;
        } else {
            int err = errno;
            char_pipe_log(dev->log, "%s open failed (%d)\n", (i == 0) ? "Output" : "Input", err);

            if ((i == 0) && (errno == ENXIO)) /* don't display error if nobody is connected to the out pipe */
                continue;

            for (int j = 0; j <= 1; j++) {
                snprintf(msg, sizeof(msg), (j == 0) ? "%s: Could not connect to %s: %s" : "%s: Could not create %s: %s", dev->port->name, path, strerror(err));
                if (startup)
                    ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
                else
                    char_pipe_log(dev->log, "%s\n", msg);
                err = create_err;
                if (!err)
                    break;
            }
        }
    }
#endif

    char_update_status(dev->port);
    return 1;

#ifdef _WIN32
errmsg:
    char_update_status(dev->port);
    if (startup)
        ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
    else
        char_pipe_log(dev->log, "%s\n", msg);
    return 0;
#endif
}

static size_t
char_pipe_read(uint8_t *buf, size_t len, void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

#ifdef _WIN32
    int   connect = !CHAR_FD_VALID(dev->fd) && !dev->block_connect;
    DWORD ret     = 0;
retry:
    if (connect)
        char_pipe_connect(dev, 0);
    if (CHAR_FD_VALID(dev->fd)) {
        BOOL result = PeekNamedPipe(dev->fd, NULL, len, NULL, &ret, NULL);
        if (result && (ret > 0))
            result = ReadFile(dev->fd, buf, MIN(len, ret), &ret, NULL);
        if (!result && !dev->server) {
            char_pipe_log(dev->log, "ReadFile failed (%08X)\n", GetLastError());
            char_pipe_disconnect(dev, 1);
            if (!dev->block_connect && !connect)
#else
    int     connect = !CHAR_FD_VALID(dev->fd_in) && !dev->block_connect_in;
    ssize_t ret     = 0;
retry:
    if (connect)
        char_pipe_connect(dev, 0);
    if (CHAR_FD_VALID(dev->fd_in) && ((ret = read(dev->fd_in, buf, len)) < 0)) {
        ret = 0;
        if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
            char_pipe_log(dev->log, "read failed (%d)\n", errno);
            char_pipe_disconnect(dev, 1);
            if (!dev->block_connect_in && !connect)
#endif
            {
                connect = 1;
                goto retry;
            }
        }
    }
    return ret;
}

static size_t
char_pipe_write(uint8_t *buf, size_t len, void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

#ifdef _WIN32
    int   connect = !CHAR_FD_VALID(dev->fd) && !dev->block_connect;
    DWORD ret     = 0;
    if (connect)
        char_pipe_connect(dev, 0);
retry:
    if (CHAR_FD_VALID(dev->fd) && !WriteFile(dev->fd, buf, len, &ret, NULL)) {
        ret = 0;
        if (!dev->server) {
            char_pipe_log(dev->log, "WriteFile failed (%08X)\n", GetLastError());
            char_pipe_disconnect(dev, 0);
            if (!dev->block_connect && !connect)
#else
    int     connect = !CHAR_FD_VALID(dev->fd_out) && !dev->block_connect_out;
    ssize_t ret     = 0;
retry:
    if (connect)
        char_pipe_connect(dev, 0);
    if (CHAR_FD_VALID(dev->fd_out)) {
        do {
            ret = write(dev->fd_out, buf, len);
        } while ((ret == 0) || ((ret < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))));
        if (ret < 0) {
            ret = 0;
            char_pipe_log(dev->log, "write failed (%d)\n", errno);
            char_pipe_disconnect(dev, 0);
            if (!dev->block_connect_out && !connect)
#endif
            {
                connect = 1;
                goto retry;
            }
        }
    }
    return ret;
}

static uint32_t
char_pipe_status(void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

#ifdef _WIN32
    if (!CHAR_FD_VALID(dev->fd) && !dev->block_connect)
        char_pipe_connect(dev, 0);
    return CHAR_FD_VALID(dev->fd) ? (CHAR_COM_DSR | CHAR_COM_DCD | CHAR_COM_CTS) : CHAR_DISCONNECTED;
#else
    if ((!CHAR_FD_VALID(dev->fd_in) && !dev->block_connect_in) || (!CHAR_FD_VALID(dev->fd_out) && !dev->block_connect_out))
        char_pipe_connect(dev, 0);
    return (CHAR_FD_VALID(dev->fd_in) ? (CHAR_COM_DSR | CHAR_COM_DCD) : CHAR_RX_DISCONNECTED) | (CHAR_FD_VALID(dev->fd_out) ? CHAR_COM_CTS : CHAR_TX_DISCONNECTED);
#endif
}

static void
char_pipe_close(void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

    char_pipe_log(dev->log, "close()\n");
    log_close(dev->log);

    char_pipe_disconnect(dev, -1);

#ifndef _WIN32
    free(dev->path_in);
    free(dev->path_out);
#endif

    free(dev);
}

static void *
char_pipe_init(const device_t *info)
{
    char_pipe_t *dev = (char_pipe_t *) calloc(1, sizeof(char_pipe_t));

    /* Get configuration. */
    const char *path = device_get_config_string("path");
#ifdef _WIN32
    while ((path[0] == '\\') || (path[0] == '/')) /* strip leading slashes */
        path++;
    if (path[0]) {
        snprintf(dev->path, sizeof(dev->path), !strnicmp(path, ".\\pipe\\", 7) ? "\\\\%s" : "\\\\.\\pipe\\%s", path); /* add \\.\pipe\ prefix if not present */
        for (int i = 2; dev->path[i]; i++) {                                                                          /* convert forward slashes to backslashes */
            if (dev->path[i] == '/')
                dev->path[i] = '\\';
        }
    }
#else
    size_t len   = strlen(path) + 4;
    dev->path_in = (char *) calloc(1, len);
    snprintf(dev->path_in, len, "%s.in", path);
    dev->path_out = (char *) calloc(1, ++len);
    snprintf(dev->path_out, len, "%s.out", path);
#endif
    dev->mode      = device_get_config_int("mode");
    dev->reconnect = !!device_get_config_int("reconnect");

    /* Attach character device. */
    dev->port = char_attach(0, char_pipe_read, char_pipe_write, char_pipe_status, NULL, NULL, dev);
    dev->log  = char_log_open(dev->port, "Pipe");
    char_pipe_log(dev->log, "init(%s)\n", path);

    /* Connect to pipe. */
#ifdef _WIN32
    dev->fd = INVALID_HANDLE_VALUE;
#else
    dev->fd_in = dev->fd_out = -1;
#endif
    char_pipe_connect(dev, 1);

    return dev;
}

// clang-format off
static const device_config_t char_pipe_config[] = {
    {
        .name           = "path",
        .description    = "Pipe path",
        .type           = CONFIG_STRING,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name         = "mode",
        .description  = "Mode",
        .type         = CONFIG_SELECTION,
        .default_int  = CHAR_PIPE_MODE_AUTO,
        .selection    = {
            { .description = "Auto",    .value = CHAR_PIPE_MODE_AUTO   },
            { .description = "Server",  .value = CHAR_PIPE_MODE_SERVER },
            { .description = "Client",  .value = CHAR_PIPE_MODE_CLIENT },
            { NULL                                                     }
        }
    },
    {
        .name           = "reconnect",
        .description    = "Reconnect automatically",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t char_pipe_com_device = {
    .name          = "Named Pipe (COM)",
    .internal_name = "pipe",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = char_pipe_init,
    .close         = char_pipe_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = char_pipe_config
};
