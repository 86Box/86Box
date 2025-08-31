/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Host serial passthrough character device.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2025 RichardG.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
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

#ifdef ENABLE_HOSTSER_LOG
int hostser_do_log = ENABLE_HOSTSER_LOG;

static void
hostser_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (hostser_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define hostser_log(priv, fmt, ...)
#endif

typedef struct {
    void *log;
    char_port_t *port;

    const char *path;
#ifdef _WIN32
    HANDLE fd;
    DCB    prev_config;
#else
    int             fd;
#    ifdef TCGETS2
    struct termios2 prev_config;
#    else
    struct termios  prev_config;
#    endif
#endif
    uint8_t prev_config_valid;
} hostser_t;

#ifdef _WIN32
typedef DWORD host_baudrate_t;
#else
typedef speed_t host_baudrate_t;
#endif

static const struct {
    uint32_t        baud;
    host_baudrate_t value;
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
hostser_disconnect(hostser_t *dev)
{
#ifdef _WIN32
    if (!dev->fd)
        return;

    /* Restore serial port configuration. */
    FlushFileBuffers(dev->fd);
    if (dev->prev_config_valid)
        SetCommState(dev->fd, &dev->prev_config);

    /* Close serial port. */
    CloseHandle(dev->fd);
    dev->fd = NULL;
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

static uint8_t
hostser_connect(hostser_t *dev)
{
#ifdef _WIN32
    /* Open serial port. */
    COMMTIMEOUTS timeouts = {
        .ReadIntervalTimeout         = MAXDWORD,
        .ReadTotalTimeoutConstant    = 0,
        .ReadTotalTimeoutMultiplier  = 0,
        .WriteTotalTimeoutMultiplier = 0,
        .WriteTotalTimeoutConstant   = 1000
    };
    dev->fd = CreateFileA(dev->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
    if (!dev->fd) {
        hostser_log(dev->log, "Connect failed (%08X)\n", GetLastError());
        return 0;
    }

    /* Set port timeouts. */
    SetCommTimeouts(dev->fd, &timeouts);

    /* Save current serial port configuration for restoring on close. */
    dev->prev_config_valid = !!GetCommState(dev->fd, &dev->prev_config);
#else
    /* Open serial port. */
    dev->fd = open(dev->path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (dev->fd == -1) {
        hostser_log(dev->log, "Connect failed (%d)\n", errno);
        return 0;
    }

    /* Make sure we're talking to a tty. */
    if (!isatty(dev->fd)) {
        hostser_log(dev->log, "Not a tty\n");
        close(dev->fd);
        dev->fd = -1;
        return 0;
    }

    /* Save current serial port configuration for restoring on close. */
#    ifdef TCGETS2
    dev->prev_config_valid = !ioctl(dev->fd, TCGETS2, &dev->prev_config);
#    elif defined(USE_LINUX_TERMIOS)
    dev->prev_config_valid = !ioctl(dev->fd, TCGETS, &dev->prev_config);
#    else
    dev->prev_config_valid = tcgetattr(dev->fd, &dev->prev_config) != -1;
#    endif
#endif

    return 1;
}

static ssize_t
hostser_read(uint8_t *buf, ssize_t len, void *priv)
{
    hostser_t *dev = (hostser_t *) priv;

#ifdef _WIN32
    DWORD ret = 0;
    if (dev->fd)
        ReadFile(dev->fd, buf, len, &ret, NULL);
#else
    ssize_t ret = 0;
    if (dev->fd != -1) {
        ret = read(dev->fd, buf, len);
        if (ret == (ssize_t) -1)
            ret = 0;
    }
#endif

    return ret;
}

static ssize_t
hostser_write(uint8_t *buf, ssize_t len, void *priv)
{
    hostser_t *dev = (hostser_t *) priv;

#ifdef _WIN32
    DWORD ret = 0;
    if (dev->fd)
        WriteFile(dev->fd, buf, len, &ret, NULL);
#else
    ssize_t ret = 0;
    if (dev->fd) {
        do {
            ret = write(dev->fd, buf, len);
        } while ((ret == 0) || ((ret == (ssize_t) -1) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))));
        if (ret == (ssize_t) -1)
            ret = 0;
    }
#endif

    return ret;
}

static host_baudrate_t
hostser_find_baud(uint32_t baud)
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
hostser_port_config(void *priv)
{
    hostser_t *dev = (hostser_t *) priv;

    switch (dev->port->type) {
        case CHAR_PORT_COM: {
#ifdef _WIN32
            if (!dev->fd)
                return;

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
            else if (dev->port->com.data_bits == 5)
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
                port_config.BaudRate = hostser_find_baud(dev->port->com.baud);
                if (!SetCommState(dev->fd, &port_config))
                    hostser_log(dev->log, "SetCommState failed (%08X)\n", GetLastError());
            }
#else
            if (dev->fd == -1)
                return;

            /* Get existing configuration. */
#    ifdef TCGETS2
            struct termios2 port_config = { 0 };
            ioctl(dev->fd, TCGETS2, &port_config);
#    elif defined(USE_LINUX_TERMIOS)
            struct termios port_config = { 0 };
            ioctl(dev->fd, TCGETS, &port_config);
#    else
            struct termios port_config = { 0 };
            tcgetattr(dev->fd, &port_config);
#    endif

            /* Modify configuration. */
            port_config.c_cflag &= ~(CSIZE | PARODD | CSTOPB);
#    ifdef CBAUD
            port_config.c_cflag &= ~CBAUD;
#        ifdef IBSHIFT
            port_config.c_cflag &= ~(CBAUD << IBSHIFT);
#        endif
#    endif

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

#    ifdef CMSPAR
            port_config.c_cflag &= ~CMSPAR;
#    endif
            port_config.c_cflag |= PARENB;
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
                /* Failed or we're not in a BOTHER system, try again with the closest common baud rate. */
#    ifdef CBAUD
                port_config.c_cflag &= ~CBAUD;
#        ifdef IBSHIFT
                port_config.c_cflag &= ~(CBAUD << IBSHIFT);
#        endif
#    endif
                host_baudrate_t baud = hostser_find_baud(dev->port->com.baud);
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
                    hostser_log(dev->log, "TCS* failed (%08X)\n", errno);
            }
#endif
            break;
        }

        default:
            break;
    }
}

static void
hostser_control(uint32_t flags, void *priv)
{
    hostser_t *dev = (hostser_t *) priv;

#ifdef _WIN32
    if (!dev->fd)
        return;

    EscapeCommFunction(dev->fd, (flags & CHAR_COM_DTR) ? SETDTR : CLRDTR);
    EscapeCommFunction(dev->fd, (flags & CHAR_COM_RTS) ? SETRTS : CLRRTS);
    EscapeCommFunction(dev->fd, (flags & CHAR_COM_BREAK) ? SETBREAK : CLRBREAK);
#else
    if (dev->fd == -1)
        return;

    if (flags & CHAR_COM_BREAK)
        tcsendbreak(dev->fd, 0);

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

    ioctl(dev->fd, TIOCMBIS, &set);
    ioctl(dev->fd, TIOCMBIC, &clear);
#endif
}

static uint32_t
hostser_status(void *priv)
{
    hostser_t *dev = (hostser_t *) priv;

    uint32_t ret = 0;

#ifdef _WIN32
    if (!dev->fd) {
        ret |= CHAR_DISCONNECTED;
    } else {
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
    }
#else
    if (dev->fd == -1) {
        ret |= CHAR_DISCONNECTED;
    } else {
        int status;
        if (!ioctl(dev->fd, TIOCMGET, &status)) {
            if (status & TIOCM_CTS)
                ret |= CHAR_COM_CTS;
            if (status & TIOCM_DSR)
                ret |= CHAR_COM_DSR;
            if (status & TIOCM_RI)
                ret |= CHAR_COM_RI;
            if (status & TIOCM_CAR)
                ret |= CHAR_COM_DCD;
        }
    }
#endif

    return ret;
}

static void
hostser_close(void *priv)
{
    hostser_t *dev = (hostser_t *) priv;

    hostser_log(dev->log, "close()\n");

    hostser_disconnect(dev);

    free(dev);
}

static void *
hostser_init(const device_t *info)
{
    hostser_t *dev = (hostser_t *) calloc(1, sizeof(hostser_t));

    dev->log = log_open("Host Serial");
    dev->path = device_get_config_string("path");
    hostser_log(dev->log, "init(%s)\n", path);

    hostser_connect(dev);

    return dev;
}

// clang-format off
static const device_config_t hostser_config[] = {
    {
        .name           = "path",
        .description    = "Host Serial Device",
        .type           = CONFIG_SERPORT,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    }
};
// clang-format on

const char_device_t hostser_device = {
    .device = {
        .name          = "Host Serial Passthrough",
        .internal_name = "hostser",
        .flags         = DEVICE_COM,
        .local         = 0,
        .init          = hostser_init,
        .close         = hostser_close,
        .reset         = NULL,
        .available     = NULL,
        .speed_changed = NULL,
        .force_redraw  = NULL,
        .config        = hostser_config
    },
    .flags       = 0,
    .read        = hostser_read,
    .write       = hostser_write,
    .port_config = hostser_port_config,
    .control     = hostser_control,
    .status      = hostser_status
};
