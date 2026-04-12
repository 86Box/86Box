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
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/un.h>
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
    const char *path;
    int mode;
    int reconnect : 1;
    int server : 1;
    uint32_t last_connect_attempt;
#ifdef _WIN32
    HANDLE fd;
#else
    int fd;
#endif
} char_pipe_t;

static void
char_pipe_disconnect(char_pipe_t *dev)
{
    if (!CHAR_FD_VALID(dev->fd))
        return;

#ifdef _WIN32
    FlushFileBuffers(dev->fd);
    DisconnectNamedPipe(dev->fd);
    CloseHandle(dev->fd);
    dev->fd = INVALID_HANDLE_VALUE;
#else
    close(dev->fd);
    dev->fd = -1;
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

    /* Close any existing connection. */
    if (LIKELY(!startup))
        char_pipe_disconnect(dev);

    /* Stop if there's nothing to connect to. */
    const char *path = dev->path;
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
    /* Initialize socket. */
    dev->fd = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (CHAR_FD_VALID(dev->fd)) {
        /* Connect or create socket. */
        struct sockaddr_un addr = { .sun_family = AF_UNIX };
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        if ((dev->mode == CHAR_PIPE_MODE_AUTO) || (dev->mode == CHAR_PIPE_MODE_CLIENT)) {
            if (connect(dev->fd, (struct sockaddr *) &addr, sizeof(addr)) >= 0) {
                char_pipe_log(dev->log, "Connected to existing socket: %s\n", addr.sun_path);
            } else {
                int err = errno;
                char_pipe_log(dev->log, "connect failed (%d)\n", err);

                snprintf(msg, sizeof(msg), "%s: Could not connect to %s: %s", dev->port->name, addr.sun_path, strerror(err));
                if (dev->mode == CHAR_PIPE_MODE_AUTO)
                    goto server;
                else
                    goto errmsg;
            }
        } else if (dev->mode == CHAR_PIPE_MODE_SERVER) {
server: {}
            /* Delete an existing file only if it's a socket. */
            struct stat stats;
            if ((stat(addr.sun_path, &stats) != 0) && S_ISSOCK(stats.st_mode) && (unlink(addr.sun_path) < 0))
                char_pipe_log(dev->log, "unlink failed (%d)\n", errno);

            if (bind(dev->fd, (struct sockaddr *) &addr, sizeof(addr)) >= 0) {
                char_pipe_log(dev->log, "Created new socket: %s\n", addr.sun_path);
                dev->server = 1;
            } else {
                int err = errno;
                char_pipe_log(dev->log, "bind failed (%d)\n", err);

                if (dev->mode == CHAR_PIPE_MODE_AUTO) { /* on auto mode, delay connect failed message to here */
                    if (startup)
                        ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
                    else
                        char_pipe_log(dev->log, "%s\n", msg);
                }
                snprintf(msg, sizeof(msg), "%s: Could not create %s: %s", dev->port->name, addr.sun_path, strerror(err));
                goto errmsg;
            }
        }
    } else {
        int err = errno;
        snprintf(msg, sizeof(msg), "%s: Could not connect to %s: %s", dev->port->name, path, strerror(err));
        goto errmsg;
    }
#endif

    char_update_status(dev->port);
    return 1;

errmsg:
    char_pipe_disconnect(dev);
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

    int retried = !dev->reconnect;
#ifdef _WIN32
    DWORD ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd) && !ReadFile(dev->fd, buf, len, &ret, NULL) && !dev->server && (GetLastError() != ERROR_NO_DATA)) {
        char_pipe_log(dev->log, "ReadFile failed (%08X)\n", GetLastError());
#else
    ssize_t ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd) && ((ret = recv(dev->fd, buf, len, 0)) < 0)) {
        char_pipe_log(dev->log, "recv failed (%d)\n", errno);
#endif
        ret = 0;
        if (!retried && char_pipe_connect(dev, 0)) {
            retried = 1;
            goto retry;
        }
    }
    return ret;
}

static size_t
char_pipe_write(uint8_t *buf, size_t len, void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

    int retried = !dev->reconnect;
#ifdef _WIN32
    DWORD ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd) && !WriteFile(dev->fd, buf, len, &ret, NULL) && !dev->server) {
        char_pipe_log(dev->log, "WriteFile failed (%08X)\n", GetLastError());
#else
    ssize_t ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd) && ((ret = send(dev->fd, buf, len, 0)) < 0)) {
        char_pipe_log(dev->log, "send failed (%d)\n", errno);
#endif
        ret = 0;
        if (!retried && char_pipe_connect(dev, 0)) {
            retried = 1;
            goto retry;
        }
    }
    return ret;
}

static uint32_t
char_pipe_status(void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

    return CHAR_FD_VALID(dev->fd) ? (CHAR_COM_DSR | CHAR_COM_DCD | CHAR_COM_CTS) : CHAR_DISCONNECTED;
}

static void
char_pipe_close(void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

    char_pipe_log(dev->log, "close()\n");
    log_close(dev->log);

    char_pipe_disconnect(dev);

    free(dev);
}

static void *
char_pipe_init(const device_t *info)
{
    char_pipe_t *dev = (char_pipe_t *) calloc(1, sizeof(char_pipe_t));

    /* Get configuration. */
    dev->path = device_get_config_string("path");
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
    dev->fd = -1;
#endif
    char_pipe_connect(dev, 1);

    return dev;
}

// clang-format off
static const device_config_t char_pipe_config[] = {
    {
        .name           = "path",
#ifdef _WIN32
        .description    = "Pipe path",
#else
        .description    = "Socket path",
#endif
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
#ifdef _WIN32
            { .description = "Server",  .value = CHAR_PIPE_MODE_SERVER },
            { .description = "Client",  .value = CHAR_PIPE_MODE_CLIENT },
#else
            { .description = "Create",  .value = CHAR_PIPE_MODE_SERVER },
            { .description = "Connect", .value = CHAR_PIPE_MODE_CLIENT },
#endif
            { NULL                                                }
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
    .name          = "Named Pipe / Socket (COM)",
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
