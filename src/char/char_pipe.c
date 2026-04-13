/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Windows named pipe or UNIX socket character device.
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

#define ENABLE_CHAR_PIPE_LOG 1
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
    void *log;
    char_port_t *port;
#ifdef _WIN32
    const
#endif
        char *path;
    int mode;
    uint32_t last_connect_attempt;
    int reconnect : 1;
    int server : 1;
#ifdef _WIN32
    int block_connect : 1;
    HANDLE fd;
#else
    int block_connect_in : 1;
    int block_connect_out : 1;
    int direction;
    int path_len;
    int fd_in;
    int fd_out;
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
    const
#endif
        char *path = dev->path;
    if (!path || !path[0]) {
        if (startup)
            char_pipe_log(dev->log, "No path specified\n");
        return 0;
    }

    char msg[1024];
#ifdef _WIN32
    /* Strip any leading slashes. */
    while ((path[0] == '\\') || (path[0] == '/'))
        path++;

    /* Add \\.\pipe\ prefix if not present. */
    char full_path[257]; /* "The entire pipe name string can be up to 256 characters long." */
    int len = snprintf(full_path, sizeof(full_path), !strnicmp(path, ".\\pipe\\", 7) ? "\\\\%s" : "\\\\.\\pipe\\%s", path);

    /* Convert forward slashes to backslashes. */
    if (len >= sizeof(full_path))
        len = sizeof(full_path) - 1;
    for (int i = 0; i < len; i++) {
        if (full_path[i] == '/')
            full_path[i] = '\\';
    }

    /* Connect or create pipe. */
    char fmt[512];
    if ((dev->mode == CHAR_PIPE_MODE_AUTO) || (dev->mode == CHAR_PIPE_MODE_CLIENT)) {
        dev->fd = CreateFileA(full_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (CHAR_FD_VALID(dev->fd)) {
            char_pipe_log(dev->log, "Connected to existing pipe: %s\n", full_path);
            dev->block_connect = !dev->reconnect;

            /* Configure client pipe. */
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            SetNamedPipeHandleState(dev->fd, &mode, NULL, NULL);
        } else {
            DWORD err = GetLastError();
            char_pipe_log(dev->log, "CreateFileA failed (%08X)\n", err);

            snprintf(fmt, sizeof(fmt), "FormatMessageA failed");
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), fmt, sizeof(fmt), NULL);
            snprintf(msg, sizeof(msg), "%s: Could not connect to %s: %s", dev->port->name, full_path, fmt);
            if (dev->mode == CHAR_PIPE_MODE_AUTO)
                goto server;
            else
                goto errmsg;
            return 0;
        }
    } else if (dev->mode == CHAR_PIPE_MODE_SERVER) {
server:
        dev->fd = CreateNamedPipeA(full_path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, PIPE_UNLIMITED_INSTANCES, 65536, 65536, NMPWAIT_USE_DEFAULT_WAIT, NULL);
        if (CHAR_FD_VALID(dev->fd)) {
            char_pipe_log(dev->log, "Created new pipe: %s\n", full_path);
            dev->block_connect = !dev->reconnect;
            dev->server = 1;
        } else {
            /* Both creation and connection failed. */
            DWORD err = GetLastError();
            char_pipe_log(dev->log, "CreateNamedPipeA failed (%08X)\n", err);

            if (dev->mode == CHAR_PIPE_MODE_AUTO) { /* on auto mode, delay connect failed message to here */
                if (startup)
                    ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
                else
                    char_pipe_log(dev->log, "%s\n", msg);
            }
            snprintf(fmt, sizeof(fmt), "FormatMessageA failed");
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), fmt, sizeof(fmt), NULL);
            snprintf(msg, sizeof(msg), "%s: Could not create %s: %s", dev->port->name, full_path, fmt);
            goto errmsg;
        }
    }
#else
    /* Determine pipe direction. */
    if (dev->mode == CHAR_PIPE_MODE_SERVER) {
        dev->direction = 0;
    } else if (dev->mode == CHAR_PIPE_MODE_CLIENT) {
        dev->direction = 1;
    } else if (!CHAR_FD_VALID(dev->fd_out) && !CHAR_FD_VALID(dev->fd_in)) {
        snprintf(&path[dev->path_len - 5], 5, ".in");
        int fd = open(path, O_WRONLY | O_NONBLOCK); /* fails if there's nobody on the other end */
        if ((dev->direction = CHAR_FD_VALID(fd)))
            close(fd);
        char_pipe_log(dev->log, "Automatic direction %d\n", dev->direction);
    }

    /* Connect or create pipes. */
    for (int i = 0; i <= 1; i++) {
        /* Skip pipe if it's already connected. */
        int *target = (i == 0) ? &dev->fd_out : &dev->fd_in;
        if (CHAR_FD_VALID(*target))
            continue;

        /* Determine file suffix. */
        snprintf(&path[dev->path_len - 5], 5, ((i ^ dev->direction) == 0) ? ".out" : ".in");

        /* Create pipe if it doesn't exist. */
        int create_err = 0;
        if (mkfifo(path, 0666)) {
            create_err = errno;
            char_pipe_log(dev->log, "%s mkfifo failed (%d)\n", (i == 0) ? "Output" : "Input", create_err);
        }

        /* Connect to pipe. */
        *target = open(path, ((i == 0) ? O_WRONLY : O_RDONLY) | O_NONBLOCK);
        if (CHAR_FD_VALID(*target)) {
            char_pipe_log(dev->log, "Connected to %s pipe: %s\n", (i == 0) ? "output" : "input", path);
            if (i == 0)
                dev->block_connect_out = !dev->reconnect;
            else
                dev->block_connect_in = !dev->reconnect;
        } else {
            int err = errno;
            char_pipe_log(dev->log, "%s open failed (%d)\n", (i == 0) ? "Output" : "Input", err);

            if ((i == 0) && (errno == ENXIO)) /* don't display error if there's nobody on the other end */
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

errmsg:
    char_update_status(dev->port);
    if (startup)
        ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
    else
        char_pipe_log(dev->log, "%s\n", msg);
    return 0;
}

static size_t
char_pipe_read(uint8_t *buf, size_t len, void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

#ifdef _WIN32
    int connect = !CHAR_FD_VALID(dev->fd) && !dev->block_connect;
    DWORD ret = 0;
retry:
    if (connect)
        char_pipe_connect(dev, 0);
    if (CHAR_FD_VALID(dev->fd) && !ReadFile(dev->fd, buf, len, &ret, NULL)) {
        ret = 0;
        if (!dev->server && (GetLastError() != ERROR_NO_DATA)) {
            char_pipe_log(dev->log, "ReadFile failed (%08X)\n", GetLastError());
            char_pipe_disconnect(dev, 1);
            if (!dev->block_connect && !connect)
#else
    int connect = !CHAR_FD_VALID(dev->fd_in) && !dev->block_connect_in;
    ssize_t ret = 0;
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
    int connect = !CHAR_FD_VALID(dev->fd) && !dev->block_connect;
    DWORD ret = 0;
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
    int connect = !CHAR_FD_VALID(dev->fd_out) && !dev->block_connect_out;
    ssize_t ret = 0;
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
    return CHAR_FD_VALID(dev->fd) ? (CHAR_COM_DSR | CHAR_COM_DCD | CHAR_COM_CTS) : CHAR_DISCONNECTED;
#else
    return (CHAR_FD_VALID(dev->fd_in) ? (CHAR_COM_DSR | CHAR_COM_DCD) : CHAR_RX_DISCONNECTED) |
           (CHAR_FD_VALID(dev->fd_out) ? CHAR_COM_CTS : CHAR_TX_DISCONNECTED);
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
    free(dev->path);
#endif

    free(dev);
}

static void *
char_pipe_init(const device_t *info)
{
    char_pipe_t *dev = (char_pipe_t *) calloc(1, sizeof(char_pipe_t));

    /* Get configuration. */
#ifdef _WIN32
    dev->path = device_get_config_string("path");
#else
    const char *path = device_get_config_string("path");
    dev->path_len = strlen(path) + 5;
    dev->path = (char *) calloc(1, dev->path_len);
    snprintf(dev->path, dev->path_len, "%s", path);
#endif
    dev->mode = device_get_config_int("mode");
    dev->reconnect = !!device_get_config_int("reconnect");

    /* Attach character device. */
    dev->port = char_attach(0, char_pipe_read, char_pipe_write, char_pipe_status, NULL, NULL, dev);
    dev->log = char_log_open(dev->port, "Pipe");
    char_pipe_log(dev->log, "init(%s)\n", dev->path);

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
