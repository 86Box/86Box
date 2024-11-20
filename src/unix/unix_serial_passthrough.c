/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Definitions for platform specific serial to host passthrough
 *
 *
 * Authors:     Andreas J. Reichel <webmaster@6th-dimension.com>,
 *              Jasmine Iwanek <jasmine@iwanek.co.uk>
 *
 *              Copyright 2021      Andreas J. Reichel.
 *              Copyright 2021-2022 Jasmine Iwanek.
 */

#ifndef __APPLE__
#    define _XOPEN_SOURCE   500
#    define _DEFAULT_SOURCE 1
#    define _BSD_SOURCE     1
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#    define __BSD_VISIBLE 1
#endif
#ifdef __NetBSD__
#    define _NETBSD_VISIBLE 1
#endif
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/select.h>
#include <stdint.h>
#include <sys/select.h>

#include <86box/86box.h>
#include <86box/log.h>
#include <86box/plat.h>
#include <86box/device.h>
#include <86box/serial_passthrough.h>
#include <86box/plat_serial_passthrough.h>
#include <termios.h>
#include <errno.h>

#define LOG_PREFIX "serial_passthrough: "

int
plat_serpt_read(void *priv, uint8_t *data)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;
    int                   res;
    struct timeval        tv;
    fd_set                rdfds;

    switch (dev->mode) {
        case SERPT_MODE_VCON:
        case SERPT_MODE_HOSTSER:
            FD_ZERO(&rdfds);
            FD_SET(dev->master_fd, &rdfds);
            tv.tv_sec  = 0;
            tv.tv_usec = 0;

            res = select(dev->master_fd + 1, &rdfds, NULL, NULL, &tv);
            if (res <= 0 || !FD_ISSET(dev->master_fd, &rdfds)) {
                return 0;
            }

            if (read(dev->master_fd, data, 1) > 0) {
                return 1;
            }
            break;
        default:
            break;
    }
    return 0;
}

void
plat_serpt_close(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    if (dev->mode == SERPT_MODE_HOSTSER) {
        tcsetattr(dev->master_fd, TCSANOW, (struct termios *) dev->backend_priv);
        free(dev->backend_priv);
    }
    close(dev->master_fd);
}

static void
plat_serpt_write_vcon(serial_passthrough_t *dev, uint8_t data)
{
#if 0
    fd_set wrfds;
    int    res;
#endif
    size_t res;

    /* We cannot use select here, this would block the hypervisor! */
#if 0
    FD_ZERO(&wrfds);
    FD_SET(ctx->master_fd, &wrfds);

    res = select(ctx->master_fd + 1, NULL, &wrfds, NULL, NULL);

    if (res <= 0) {
        return;
    }
#endif

    /* just write it out */
    if (dev->mode == SERPT_MODE_HOSTSER) {
        do {
            res = write(dev->master_fd, &data, 1);
        } while (res == 0 || (res == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)));
    } else
        res = write(dev->master_fd, &data, 1);
}

void
plat_serpt_set_params(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    if (dev->mode == SERPT_MODE_HOSTSER) {
        struct termios term_attr;
        tcgetattr(dev->master_fd, &term_attr);
#define BAUDRATE_RANGE(baud_rate, min, max, val) \
    if (baud_rate >= min && baud_rate < max) {   \
        cfsetispeed(&term_attr, val);            \
        cfsetospeed(&term_attr, val);            \
    }

        BAUDRATE_RANGE(dev->baudrate, 50, 75, B50);
        BAUDRATE_RANGE(dev->baudrate, 75, 110, B75);
        BAUDRATE_RANGE(dev->baudrate, 110, 134, B110);
        BAUDRATE_RANGE(dev->baudrate, 134, 150, B134);
        BAUDRATE_RANGE(dev->baudrate, 150, 200, B150);
        BAUDRATE_RANGE(dev->baudrate, 200, 300, B200);
        BAUDRATE_RANGE(dev->baudrate, 300, 600, B300);
        BAUDRATE_RANGE(dev->baudrate, 600, 1200, B600);
        BAUDRATE_RANGE(dev->baudrate, 1200, 1800, B1200);
        BAUDRATE_RANGE(dev->baudrate, 1800, 2400, B1800);
        BAUDRATE_RANGE(dev->baudrate, 2400, 4800, B2400);
        BAUDRATE_RANGE(dev->baudrate, 4800, 9600, B4800);
        BAUDRATE_RANGE(dev->baudrate, 9600, 19200, B9600);
        BAUDRATE_RANGE(dev->baudrate, 19200, 38400, B19200);
        BAUDRATE_RANGE(dev->baudrate, 38400, 57600, B38400);
        BAUDRATE_RANGE(dev->baudrate, 57600, 115200, B57600);
        BAUDRATE_RANGE(dev->baudrate, 115200, 0xFFFFFFFF, B115200);

        term_attr.c_cflag &= ~CSIZE;
        switch (dev->data_bits) {
            case 8:
            default:
                term_attr.c_cflag |= CS8;
                break;
            case 7:
                term_attr.c_cflag |= CS7;
                break;
            case 6:
                term_attr.c_cflag |= CS6;
                break;
            case 5:
                term_attr.c_cflag |= CS5;
                break;
        }
        term_attr.c_cflag &= ~CSTOPB;
        if (dev->serial->lcr & 0x04)
            term_attr.c_cflag |= CSTOPB;
#if !defined(__linux__)
        term_attr.c_cflag &= ~(PARENB | PARODD);
#else
        term_attr.c_cflag &= ~(PARENB | PARODD | CMSPAR);
#endif
        if (dev->serial->lcr & 0x08) {
            term_attr.c_cflag |= PARENB;
            if (!(dev->serial->lcr & 0x10))
                term_attr.c_cflag |= PARODD;
#if defined(__linux__)
            if ((dev->serial->lcr & 0x20))
                term_attr.c_cflag |= CMSPAR;
#endif
        }
        tcsetattr(dev->master_fd, TCSANOW, &term_attr);
#undef BAUDRATE_RANGE
    }
}

void
plat_serpt_write(void *priv, uint8_t data)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    switch (dev->mode) {
        case SERPT_MODE_VCON:
        case SERPT_MODE_HOSTSER:
            plat_serpt_write_vcon(dev, data);
            break;
        default:
            break;
    }
}

static int
open_pseudo_terminal(serial_passthrough_t *dev)
{
    int            master_fd = open("/dev/ptmx", O_RDWR | O_NONBLOCK);
    char          *ptname;
    struct termios term_attr_raw;

    if (!master_fd) {
        return 0;
    }

    /* get name of slave device */
    if (!(ptname = ptsname(master_fd))) {
        pclog(LOG_PREFIX "could not get name of slave pseudo terminal");
        close(master_fd);
        return 0;
    }
    memset(dev->slave_pt, 0, sizeof(dev->slave_pt));
    strncpy(dev->slave_pt, ptname, sizeof(dev->slave_pt) - 1);

    fprintf(stderr, LOG_PREFIX "Slave side is %s\n", dev->slave_pt);

    if (grantpt(master_fd)) {
        pclog(LOG_PREFIX "error in grantpt()\n");
        close(master_fd);
        return 0;
    }

    if (unlockpt(master_fd)) {
        pclog(LOG_PREFIX "error in unlockpt()\n");
        close(master_fd);
        return 0;
    }

    tcgetattr(master_fd, &term_attr_raw);
    cfmakeraw(&term_attr_raw);
    tcsetattr(master_fd, TCSANOW, &term_attr_raw);

    dev->master_fd = master_fd;

    return master_fd;
}

static int
open_host_serial_port(serial_passthrough_t *dev)
{
    struct termios *term_attr     = NULL;
    struct termios  term_attr_raw = {};
    int             fd            = open(dev->host_serial_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) {
        return 0;
    }

    if (!isatty(fd)) {
        return 0;
    }

    term_attr = calloc(1, sizeof(struct termios));
    if (!term_attr) {
        close(fd);
        return 0;
    }

    if (tcgetattr(fd, term_attr) == -1) {
        free(term_attr);
        close(fd);
        return 0;
    }
    term_attr_raw = *term_attr;
    /* "Raw" mode. */
    cfmakeraw(&term_attr_raw);
    term_attr_raw.c_cflag &= CSIZE;
    switch (dev->data_bits) {
        case 8:
        default:
            term_attr_raw.c_cflag |= CS8;
            break;
        case 7:
            term_attr_raw.c_cflag |= CS7;
            break;
        case 6:
            term_attr_raw.c_cflag |= CS6;
            break;
        case 5:
            term_attr_raw.c_cflag |= CS5;
            break;
    }
    tcsetattr(fd, TCSANOW, &term_attr_raw);
    dev->backend_priv = term_attr;
    dev->master_fd    = fd;
    pclog(LOG_PREFIX "Opened host TTY/serial port %s\n", dev->host_serial_path);
    return 1;
}

int
plat_serpt_open_device(void *priv)
{
    serial_passthrough_t *dev = (serial_passthrough_t *) priv;

    switch (dev->mode) {
        case SERPT_MODE_VCON:
            if (!open_pseudo_terminal(dev)) {
                return 1;
            }
            break;
        case SERPT_MODE_HOSTSER:
            if (!open_host_serial_port(dev)) {
                return 1;
            }
            break;
        default:
            break;
    }
    return 0;
}
