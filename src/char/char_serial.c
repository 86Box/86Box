/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Serial passthrough character device.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2025-2026 RichardG.
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
#    include <fcntl.h>
#    ifdef USE_LINUX_TERMIOS
#        include <asm/termbits.h>
#        include <asm/ioctls.h>
#    else
#        include <termios.h>
#    endif
#    include <unistd.h>
#    include <sys/ioctl.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/ini.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_fallthrough.h>
#include <86box/ui.h>

#define ENABLE_CHAR_SERIAL_LOG 1
#ifdef ENABLE_CHAR_SERIAL_LOG
int char_serial_do_log = ENABLE_CHAR_SERIAL_LOG;

static void
char_serial_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_serial_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_serial_log(priv, fmt, ...)
#endif

typedef struct {
    void *log;
    char_port_t *port;

    const char *path;
    int reconnect : 1;
    uint32_t last_connect_attempt;
#ifdef _WIN32
    HANDLE fd;
    DCB    prev_config;
#else
    int fd;
#    ifdef TCGETS2
    struct termios2 prev_config;
#    else
    struct termios prev_config;
#    endif
#endif
    int prev_config_valid : 1;
} char_serial_t;

#ifdef _WIN32
typedef DWORD speed_t;
#endif

static const struct {
    uint32_t baud;
    speed_t  value;
} common_baud[] = {
    // clang-format off
#ifdef _WIN32
    { .baud = 110, .value = CBR_110 },
    { .baud = 300, .value = CBR_300 },
    { .baud = 600, .value = CBR_600 },
    { .baud = 1200, .value = CBR_1200 },
    { .baud = 2400, .value = CBR_2400 },
    { .baud = 4800, .value = CBR_4800 },
    { .baud = 9600, .value = CBR_9600 },
    { .baud = 14400, .value = CBR_14400 },
    { .baud = 19200, .value = CBR_19200 },
    { .baud = 38400, .value = CBR_38400 },
    { .baud = 57600, .value = CBR_57600 },
    { .baud = 115200, .value = CBR_115200 },
    { .baud = 128000, .value = CBR_128000 },
    { .baud = 256000, .value = CBR_256000 },
#else
    { .baud = 50, .value = B50 },
    { .baud = 75, .value = B75 },
    { .baud = 110, .value = B110 },
    { .baud = 134, .value = B134 },
    { .baud = 150, .value = B150 },
    { .baud = 200, .value = B200 },
    { .baud = 300, .value = B300 },
    { .baud = 600, .value = B600 },
    { .baud = 1200, .value = B1200 },
    { .baud = 1800, .value = B1800 },
    { .baud = 2400, .value = B2400 },
    { .baud = 4800, .value = B4800 },
    { .baud = 9600, .value = B9600 },
    { .baud = 19200, .value = B19200 },
    { .baud = 38400, .value = B38400 },
#    ifdef B57600
    { .baud = 57600, .value = B57600 },
#    endif
#    ifdef B115200
    { .baud = 115200, .value = B115200 },
#    endif
#    ifdef B230400
    { .baud = 230400, .value = B230400 },
#    endif
#    ifdef B460800
    { .baud = 460800, .value = B460800 },
#    endif
#    ifdef B500000
    { .baud = 500000, .value = B500000 },
#    endif
#    ifdef B576000
    { .baud = 576000, .value = B576000 },
#    endif
#    ifdef B921600
    { .baud = 921600, .value = B921600 },
#    endif
#    ifdef B1000000
    { .baud = 1000000, .value = B1000000 },
#    endif
#    ifdef B1152000
    { .baud = 1152000, .value = B1152000 },
#    endif
#    ifdef B1500000
    { .baud = 1500000, .value = B1500000 },
#    endif
#    ifdef B2000000
    { .baud = 2000000, .value = B2000000 },
#    endif
#    ifdef B2500000
    { .baud = 2500000, .value = B2500000 },
#    endif
#    ifdef B3000000
    { .baud = 3000000, .value = B3000000 },
#    endif
#    ifdef B3500000
    { .baud = 3500000, .value = B3500000 },
#    endif
#    ifdef B4000000
    { .baud = 4000000, .value = B4000000 },
#    endif
#endif
    { 0 }
    // clang-format on
};

static void
char_serial_disconnect(char_serial_t *dev)
{
    if (!CHAR_FD_VALID(dev->fd))
        return;

#ifdef _WIN32
    /* Restore serial port configuration. */
    FlushFileBuffers(dev->fd);
    if (dev->prev_config_valid)
        SetCommState(dev->fd, &dev->prev_config);

    /* Close serial port. */
    CloseHandle(dev->fd);
    dev->fd = INVALID_HANDLE_VALUE;
#else
    /* Restore serial port configuration. */
    if (dev->prev_config_valid)
#    ifdef TCSETS2
        ioctl(dev->fd, TCSETS2, &dev->prev_config);
#    elif defined(USE_LINUX_TERMIOS)
        ioctl(dev->fd, TCSETS, &dev->prev_config);
#    else
        tcsetattr(dev->fd, TCSANOW, &dev->prev_config);
#    endif

    /* Close serial port. */
    close(dev->fd);
    dev->fd = -1;
#endif
}

static int
char_serial_connect(char_serial_t *dev, int startup)
{
    /* Limit the connection attempt rate. */
    uint32_t now = plat_get_ticks();
    if (LIKELY(!startup) && ((now - dev->last_connect_attempt) < CHAR_RECONNECT_MS))
        return 0;
    dev->last_connect_attempt = now;

    /* Close any existing connection. */
    if (LIKELY(!startup))
        char_serial_disconnect(dev);

    /* Stop if there's nothing to connect to. */
    const char *path = dev->path;
    if (!path || !path[0]) {
        if (startup)
            char_serial_log(dev->log, "No path specified\n");
        return 0;
    }

    char msg[1024];
#ifdef _WIN32
    /* Open serial port. */
    COMMTIMEOUTS timeouts = {
        .ReadIntervalTimeout         = MAXDWORD,
        .ReadTotalTimeoutConstant    = 0,
        .ReadTotalTimeoutMultiplier  = 0,
        .WriteTotalTimeoutMultiplier = 0,
        .WriteTotalTimeoutConstant   = 1000
    };
    dev->fd = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
    if (!CHAR_FD_VALID(dev->fd)) {
        DWORD err = GetLastError();
        char_serial_log(dev->log, "CreateFileA failed (%08X)\n", err);

        char fmt[512];
        snprintf(fmt, sizeof(fmt), "FormatMessageA failed");
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), fmt, sizeof(fmt), NULL);
        snprintf(msg, sizeof(msg), "Could not connect to %s: %s", path, fmt);
        goto errmsg;
    }

    /* Save current serial port configuration for restoring on close. */
    dev->prev_config_valid = !!GetCommState(dev->fd, &dev->prev_config);

    /* Set up serial port. */
    SetupComm(dev->fd, 2048, 2048);
    SetCommTimeouts(dev->fd, &timeouts);
    DWORD err;
    COMSTAT stats;
    ClearCommError(dev->fd, &err, &stats);
#else
    /* Open serial port. */
    int err = 0;
    dev->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (!CHAR_FD_VALID(dev->fd)) {
        err = errno;
        char_serial_log(dev->log, "open failed (%d)\n", err);
    } else if (!isatty(dev->fd)) {
        err = ENOTTY;
        char_serial_log(dev->log, "Path is not a TTY\n");
    }
    if (err > 0) {        
        snprintf(msg, sizeof(msg), "Could not connect to %s: %s", path, strerror(err));
        goto errmsg;
    }

    /* Save current serial port configuration for restoring on close. */
#    ifdef TCGETS2
    dev->prev_config_valid = !ioctl(dev->fd, TCGETS2, &dev->prev_config);
#    elif defined(USE_LINUX_TERMIOS)
    dev->prev_config_valid = !ioctl(dev->fd, TCGETS, &dev->prev_config);
#    else
    dev->prev_config_valid = !tcgetattr(dev->fd, &dev->prev_config);
#    endif
    if (!dev->prev_config_valid)
        char_serial_log(dev->log, "tcgetattr failed (%d)\n", errno);
#endif

    char_serial_log(dev->log, "Connected: %s\n", path);

    return 1;

errmsg:
    char_serial_disconnect(dev);
    if (startup)
        ui_msgbox(MBX_ERROR | MBX_ANSI, msg);
    else
        char_serial_log(dev->log, "%s\n", msg);
    return 0;
}

static size_t
char_serial_read(uint8_t *buf, size_t len, void *priv)
{
    char_serial_t *dev = (char_serial_t *) priv;

    int retried = !dev->reconnect;
#ifdef _WIN32
    DWORD ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd)) {
        DWORD err;
        COMSTAT stats;
        ClearCommError(dev->fd, &err, &stats);
        if ((stats.cbInQue <= 0) || ReadFile(dev->fd, buf, len, &ret, NULL))
            return ret;
        char_serial_log(dev->log, "ReadFile failed (%08X)\n", GetLastError());
#else
    ssize_t ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd)) {
        do {
            ret = read(dev->fd, buf, len);
        } while ((ret == 0) || ((ret < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))));
    }
    if (ret < 0) {
        char_serial_log(dev->log, "read failed (%d)\n", errno);
#endif
        ret = 0;
        if (!retried && char_serial_connect(dev, 0)) {
            retried = 1;
            goto retry;
        }
    }
    return ret;
}

static size_t
char_serial_write(uint8_t *buf, size_t len, void *priv)
{
    char_serial_t *dev = (char_serial_t *) priv;

    int retried = !dev->reconnect;
#ifdef _WIN32
    DWORD ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd) && !WriteFile(dev->fd, buf, len, &ret, NULL)) {
        char_serial_log(dev->log, "WriteFile failed (%08X)\n", GetLastError());
#else
    ssize_t ret = 0;
retry:
    if (CHAR_FD_VALID(dev->fd)) {
        do {
            ret = write(dev->fd, buf, len);
        } while ((ret == 0) || ((ret < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))));
    }
    if (ret < 0) {
        char_serial_log(dev->log, "write failed (%d)\n", errno);
#endif
        ret = 0;
        if (!retried && char_serial_connect(dev, 0)) {
            retried = 1;
            goto retry;
        }
    }
    return ret;
}

static speed_t
char_serial_find_baud(uint32_t baud)
{
    /* Search the host's common baud rate table for the closest match to the specified baud rate. */
    uint32_t lbound = 0;
    uint32_t hbound;
    int i;
    for (i = 0; common_baud[i + 1].baud; i++) {
        hbound = (common_baud[i].baud + common_baud[i + 1].baud) / 2;
        if ((baud >= lbound) && (baud < hbound))
            break;
        lbound = hbound;
    }
    return common_baud[i].value;
}

static void
char_serial_port_config(void *priv)
{
    char_serial_t *dev = (char_serial_t *) priv;

    if ((dev->port->type != CHAR_PORT_COM) || !CHAR_FD_VALID(dev->fd))
        return;

#ifdef _WIN32
    /* Get existing configuration. */
    DCB port_config = { 0 };
    GetCommState(dev->fd, &port_config);

    /* Modify configuration. */
    port_config.BaudRate        = dev->port->com.baud; /* try actual baud rate first */
    port_config.fDtrControl     = DTR_CONTROL_ENABLE;
    port_config.fDsrSensitivity = FALSE;
    port_config.fInX            = FALSE;
    port_config.fOutX           = FALSE;
    port_config.fRtsControl     = RTS_CONTROL_ENABLE;
    port_config.fAbortOnError   = FALSE;
    port_config.ByteSize        = dev->port->com.data_bits;

    if (dev->port->com.stop_bits <= 1)
        port_config.StopBits = ONESTOPBIT;
    else if (dev->port->com.data_bits <= 5)
        port_config.StopBits = ONE5STOPBITS;
    else
        port_config.StopBits = TWOSTOPBITS;

    port_config.fParity = 1;
    switch (dev->port->com.parity) {
        case CHAR_COM_PARITY_EVEN:
            port_config.Parity = EVENPARITY;
            break;

        case CHAR_COM_PARITY_ODD:
            port_config.Parity = ODDPARITY;
            break;

        case CHAR_COM_PARITY_MARK:
            port_config.Parity = MARKPARITY;
            break;

        case CHAR_COM_PARITY_SPACE:
            port_config.Parity = SPACEPARITY;
            break;

        default:
            port_config.fParity = 0;
            port_config.Parity  = NOPARITY;
            break;
    }

    /* Set configuration. */
    if (!SetCommState(dev->fd, &port_config)) {
        /* Failed, try again with the closest common baud rate. */
        port_config.BaudRate = char_serial_find_baud(dev->port->com.baud);
        if (!SetCommState(dev->fd, &port_config))
            char_serial_log(dev->log, "SetCommState failed (%08X)\n", GetLastError());
    }
#else
    /* Get existing configuration. */
#    ifdef TCGETS2
    struct termios2 port_config = { 0 };
    ioctl(dev->fd, TCGETS2, &port_config);
#    else
    struct termios port_config = { 0 };
#        ifdef USE_LINUX_TERMIOS
    ioctl(dev->fd, TCGETS, &port_config);
#        else
    tcgetattr(dev->fd, &port_config);
#        endif
#    endif

    /* Modify configuration. */
    port_config.c_cflag &= ~(CSIZE | PARODD | CSTOPB);
#    ifdef CMSPAR
    port_config.c_cflag &= ~CMSPAR;
#    endif
#    ifdef CBAUD
    port_config.c_cflag &= ~CBAUD;
#        ifdef IBSHIFT
    port_config.c_cflag &= ~(CBAUD << IBSHIFT);
#        endif
#    endif
    port_config.c_cflag |= CREAD | PARENB;

    switch (dev->port->com.data_bits) {
        case 5:
            port_config.c_cflag |= CS5;
            break;

        case 6:
            port_config.c_cflag |= CS6;
            break;

        case 7:
            port_config.c_cflag |= CS7;
            break;

        default:
            port_config.c_cflag |= CS8;
            break;
    }

    switch (dev->port->com.parity) {
        case CHAR_COM_PARITY_SPACE:
#    ifdef CMSPAR
            port_config.c_cflag |= CMSPAR;
#    endif
            fallthrough;

        case CHAR_COM_PARITY_EVEN:
            break;

        case CHAR_COM_PARITY_MARK:
#    ifdef CMSPAR
            port_config.c_cflag |= CMSPAR;
#    endif
            fallthrough;

        case CHAR_COM_PARITY_ODD:
            port_config.c_cflag |= PARODD;
            break;

        default:
            port_config.c_cflag &= ~PARENB;
            break;
    }

    if (dev->port->com.stop_bits > 1)
        port_config.c_cflag |= CSTOPB;

    /* Set specific baud rate through BOTHER if available. */
#    if defined(BOTHER) && defined(TCSETS2)
    port_config.c_cflag |= BOTHER;
    port_config.c_ospeed = dev->port->com.baud;
#        ifdef IBSHIFT
    port_config.c_cflag |= BOTHER << IBSHIFT;
    port_config.c_ispeed = dev->port->com.baud;
#        endif

    /* Set configuration. */
    if (ioctl(dev->fd, TCSETS2, &port_config))
#    endif
    {
        /* Failed or we're not in a BOTHER platform, try again with the closest common baud rate. */
#    ifdef CBAUD
        port_config.c_cflag &= ~CBAUD;
#        ifdef IBSHIFT
        port_config.c_cflag &= ~(CBAUD << IBSHIFT);
#        endif
#    endif
        speed_t baud = char_serial_find_baud(dev->port->com.baud);
        port_config.c_cflag |= baud;
#    ifdef IBSHIFT
        port_config.c_cflag |= baud << IBSHIFT;
#    endif

#    ifdef TCSETS2
        if (ioctl(dev->fd, TCSETS2, &port_config))
#    elif defined(USE_LINUX_TERMIOS)
        if (ioctl(dev->fd, TCSETS, &port_config))
#    else
        if (tcsetattr(dev->fd, TCSANOW, &port_config))
#    endif
            char_serial_log(dev->log, "TCS* failed (%08X)\n", errno);
    }
#endif
}

static uint32_t
char_serial_status(void *priv)
{
    char_serial_t *dev = (char_serial_t *) priv;

    if (!CHAR_FD_VALID(dev->fd))
        return CHAR_DISCONNECTED;

    uint32_t ret = 0;
#ifdef _WIN32
    DWORD status;
    if (GetCommModemStatus(dev->fd, &status)) {
        if (status & MS_CTS_ON)
            ret |= CHAR_COM_CTS;
        if (status & MS_DSR_ON)
            ret |= CHAR_COM_DSR;
        if (status & MS_RING_ON)
            ret |= CHAR_COM_RI;
        if (status & MS_RLSD_ON)
            ret |= CHAR_COM_DCD;
    }
#else
    int status;
    if (!ioctl(dev->fd, TIOCMGET, &status)) {
        if (status & TIOCM_CTS)
            ret |= CHAR_COM_CTS;
        if (status & TIOCM_DSR)
            ret |= CHAR_COM_DSR;
        if (status & TIOCM_RNG)
            ret |= CHAR_COM_RI;
        if (status & TIOCM_CAR)
            ret |= CHAR_COM_DCD;
    }
#endif

    return ret;
}

static void
char_serial_control(uint32_t flags, void *priv)
{
    char_serial_t *dev = (char_serial_t *) priv;

    if (!CHAR_FD_VALID(dev->fd))
        return;

#ifdef _WIN32
    EscapeCommFunction(dev->fd, (flags & CHAR_COM_DTR) ? SETDTR : CLRDTR);
    EscapeCommFunction(dev->fd, (flags & CHAR_COM_RTS) ? SETRTS : CLRRTS);
    EscapeCommFunction(dev->fd, (flags & CHAR_COM_BREAK) ? SETBREAK : CLRBREAK);
#else
    int set = 0;
    int clear = 0;

    if (flags & CHAR_COM_DTR)
        set |= TIOCM_DTR;
    else
        clear |= TIOCM_DTR;
    if (flags & CHAR_COM_RTS)
        set |= TIOCM_RTS;
    else
        clear |= TIOCM_RTS;

    if (set)
        ioctl(dev->fd, TIOCMBIS, &set);
    if (clear)
        ioctl(dev->fd, TIOCMBIC, &clear);
    ioctl(dev->fd, (flags & CHAR_COM_BREAK) ? TIOCSBRK : TIOCCBRK);
#endif
}

static void
char_serial_close(void *priv)
{
    char_serial_t *dev = (char_serial_t *) priv;

    char_serial_log(dev->log, "close()\n");
    log_close(dev->log);

    char_serial_disconnect(dev);

    free(dev);
}

static void *
char_serial_init(const device_t *info)
{
    char_serial_t *dev = (char_serial_t *) calloc(1, sizeof(char_serial_t));

    /* Get serial port. */
    dev->path = device_get_config_string("path");
    char buf[256];
    snprintf(buf, sizeof(buf), "Host Serial %s", path_get_filename((char *) dev->path));
    dev->log = log_open(buf);
    char_serial_log(dev->log, "init(%s)\n", dev->path);
    dev->reconnect = !!device_get_config_int("reconnect");

    /* Attach character device. */
    dev->port = char_attach(0, char_serial_read, char_serial_write, char_serial_status, char_serial_control, char_serial_port_config, dev);

    /* Connect to serial port. */
    char_serial_connect(dev, 1);

    return dev;
}

// clang-format off
static const device_config_t char_serial_config[] = {
    {
        .name           = "path",
        .description    = "Host serial device",
        .type           = CONFIG_SERPORT,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
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

const device_t char_serial_passthrough_com_device = {
    .name          = "Serial Passthrough (COM)",
    .internal_name = "serial_passthrough",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = char_serial_init,
    .close         = char_serial_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = char_serial_config
};
