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
#include <wchar.h>
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
#ifdef _WIN32
    HANDLE fd;
#else
    int fd;
#endif
} char_pipe_t;

static ssize_t
char_pipe_read(uint8_t *buf, ssize_t len, void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

#ifdef _WIN32
    DWORD ret = 0;
    if (CHAR_FD_VALID(dev->fd))
        ReadFile(dev->fd, buf, len, &ret, NULL);
    return ret;
#else
    return CHAR_FD_VALID(dev->fd) ? recv(dev->fd, buf, len, 0) : 0;
#endif
}

static ssize_t
char_pipe_write(uint8_t *buf, ssize_t len, void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

#ifdef _WIN32
    DWORD ret = 0;
    if (CHAR_FD_VALID(dev->fd))
        WriteFile(dev->fd, buf, len, &ret, NULL);
    return ret;
#else
    return CHAR_FD_VALID(dev->fd) ? send(dev->fd, buf, len, 0) : 0;
#endif
}

static uint32_t
char_pipe_status(void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

    return 
#ifdef _WIN32
           dev->fd
#else
           (dev->fd != -1)
#endif
           ? (CHAR_COM_DSR | CHAR_COM_DCD | CHAR_COM_CTS) : CHAR_DISCONNECTED;
}

static void
char_pipe_close(void *priv)
{
    char_pipe_t *dev = (char_pipe_t *) priv;

    char_pipe_log(dev->log, "close()\n");

    if (CHAR_FD_VALID(dev->fd)) {
#ifdef _WIN32
        FlushFileBuffers(dev->fd);
        DisconnectNamedPipe(dev->fd);
        CloseHandle(dev->fd);
#else
        close(dev->fd);
#endif
    }
}

static void *
char_pipe_init(const device_t *info)
{
    char_pipe_t *dev = (char_pipe_t *) calloc(1, sizeof(char_pipe_t));

    dev->log = log_open("Pipe/Socket");
    char_pipe_log(dev->log, "init()\n");

    /* Attach character device. */
    dev->port = char_attach(0, char_pipe_read, char_pipe_write, char_pipe_status, NULL, NULL, dev);

    char *path = ini_get_string(dev->port->config, "", "path", NULL);
    if (path) {
#ifdef _WIN32
        /* Remove any leading slashes. */
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

        /* Try connecting to the pipe first. */
        dev->fd = CreateFileA(full_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (CHAR_FD_VALID(dev->fd)) {
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            SetNamedPipeHandleState(dev->fd, &mode, NULL, NULL);
        } else {
            /* Connection failed, try creating a new pipe. */
            DWORD open_error = GetLastError();
            char_pipe_log(dev->log, "CreateFileA failed (%08X)\n", open_error);

            dev->fd = CreateNamedPipeA(full_path, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, PIPE_UNLIMITED_INSTANCES, 65536, 65536, NMPWAIT_USE_DEFAULT_WAIT, NULL);
            if (!CHAR_FD_VALID(dev->fd)) {
                /* Both creation and connection failed. */
                DWORD create_error = GetLastError();
                char_pipe_log(dev->log, "CreateNamedPipeA failed (%08X)\n", create_error);

                wchar_t msg[3][1024] = { 0 };
                FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, open_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg[0], sizeof(msg[0]) / sizeof(msg[0][0]), NULL);
                FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, create_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg[1], sizeof(msg[1]) / sizeof(msg[1][0]), NULL);
                swprintf(msg[2], sizeof(msg[2]) / sizeof(msg[2][0]), L"Could not create or connect to pipe %s:\nConnect: %ls\nCreate: %ls", full_path, msg[0], msg[1]);
                ui_msgbox(MBX_ERROR, msg[2]);
            }
        }
#else
        /* Create socket. */
        dev->fd = socket(PF_UNIX, SOCK_DGRAM, 0);
        if (CHAR_FD_VALID(dev->fd)) {
            /* Try connecting to the socket first. */
            struct sockaddr_un addr = { .sun_family = AF_UNIX };
            strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
            if (connect(dev->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
                /* Connection failed, try creating a new socket. */
                int open_error = errno;
                char_pipe_log(dev->log, "connect failed (%d)\n", open_error);

                /* Delete an existing file only if it's a socket. */
                struct stat stats;
                if ((stat(path, &stats) != 0) && S_ISSOCK(stats.st_mode))
                    unlink(path);

                if (bind(dev->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
                    /* Both connection and creation failed. */
                    int create_error = errno;
                    char_pipe_log(dev->log, "bind failed (%d)\n", create_error);

                    close(dev->fd);
                    dev->fd = -1;

                    wchar_t msg[1024];
                    swprintf(msg, sizeof(msg) / sizeof(msg[0]), L"Could not create or connect to socket %s:\nConnect: %ls\nCreate: %ls", path, strerror(open_error), strerror(create_error));
                    ui_msgbox(MBX_ERROR, msg);
                }
            }
        } else {
            wchar_t msg[1024];
            swprintf(msg, sizeof(msg) / sizeof(msg[0]), L"Could not create or connect to socket %s: %s", path, strerror(errno));
            ui_msgbox(MBX_ERROR, msg);
        }
#endif
    } else {
        char_pipe_log(dev->log, "No path specified\n");
    }

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
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t char_pipe_device = {
#ifdef _WIN32
    .name          = "Named Pipe",
#else
    .name          = "UNIX Socket",
#endif
    .internal_name = "pipe",
    .flags         = DEVICE_COM | DEVICE_LPT,
    .local         = 0,
    .init          = char_pipe_init,
    .close         = char_pipe_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = char_pipe_config
};
