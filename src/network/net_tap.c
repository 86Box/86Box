/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Linux TAP network interface for 86box.
 *
 *          This file was created by looking at the VDE network backend
 *          as a reference, credit to jguillaumes.
 *
 * Authors: Doug Johnson <dougvj@gmail.com>
 *
 *          Copyright 2023 Doug Johnson
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef _WIN32
#    error TAP networking is only supported on Linux
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/if_arp.h>
#include <linux/sockios.h>

#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_event.h>

typedef struct net_tap_t {
    int        fd; // tap device file descriptor
    netcard_t *card;
    thread_t  *poll_tid;
    net_evt_t  tx_event;
    net_evt_t  stop_event;
    netpkt_t   pkt_rx;
    netpkt_t   pkts_tx[NET_QUEUE_LEN];
} net_tap_t;

#ifdef ENABLE_TAP_LOG
int tap_do_log = ENABLE_TAP_LOG;


static void tap_logv(const char *fmt, va_list ap)
{
    if (tap_do_log) {
        pclog_ex(fmt, ap);
    }
}

static void tap_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (tap_do_log) {
        va_start(ap, fmt);
        tap_logv(fmt, ap);
        va_end(ap);
    }
    va_end(ap);
}

#else
#    define tap_log(...) \
        do {             \
        } while (0)
#    define tap_logv(...) \
        do {              \
        } while (0)
#endif

static void net_tap_thread(void *priv) {
    enum {
        NET_EVENT_STOP = 0,
        NET_EVENT_TX,
        NET_EVENT_RX,
        NET_EVENT_TAP,
        NET_EVENT_MAX,
    };
    net_tap_t *tap = priv;
    tap_log("TAP: poll thread started.\n");
    struct pollfd pfd[NET_EVENT_MAX];
    pfd[NET_EVENT_STOP].fd = net_event_get_fd(&tap->stop_event);
    pfd[NET_EVENT_STOP].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_TX].fd = net_event_get_fd(&tap->tx_event);
    pfd[NET_EVENT_TX].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_RX].fd = tap->fd;
    pfd[NET_EVENT_RX].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_TAP].fd = tap->fd;
    pfd[NET_EVENT_TAP].events = POLLERR | POLLHUP | POLLPRI;
    fcntl(tap->fd, F_SETFL, O_NONBLOCK);
    while(1) {
        ssize_t ret = poll(pfd, NET_EVENT_MAX, -1);
        if (ret < 0) {
            tap_log("TAP: poll error: %s\n", strerror(errno));
            net_event_set(&tap->stop_event);
            break;
        }
        if (pfd[NET_EVENT_TAP].revents) {
            tap_log("TAP: tap close/error event received.\n");
            net_event_set(&tap->stop_event);
        }
        if (pfd[NET_EVENT_TX].revents & POLLIN) {
            net_event_clear(&tap->tx_event);
            int packets = network_tx_popv(tap->card, tap->pkts_tx,
                                          NET_QUEUE_LEN);
            for(int i = 0; i < packets; i++) {
                netpkt_t *pkt = &tap->pkts_tx[i];
                ssize_t ret = write(tap->fd, pkt->data, pkt->len);
                if (ret < 0) {
                    tap_log("TAP: write error: %s\n", strerror(errno));
                }
            }
        }
        if (pfd[NET_EVENT_RX].revents & POLLIN) {
            ssize_t len = read(tap->fd, tap->pkt_rx.data, NET_MAX_FRAME);
            if (len < 0) {
                tap_log("TAP: read error: %s\n", strerror(errno));
                continue;
            }
            tap->pkt_rx.len = len;
            network_rx_put_pkt(tap->card, &tap->pkt_rx);
        }
        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
            net_event_clear(&tap->stop_event);
            break;
        }
    }
}

void net_tap_close(void *priv)
{
    if (!priv) {
        return;
    }
    net_tap_t *tap = priv;
    tap_log("TAP: closing.\n");
    net_event_set(&tap->stop_event);
    tap_log("TAP: waiting for poll thread to exit.\n");
    thread_wait(tap->poll_tid);
    tap_log("TAP: poll thread exited.\n");
    for(int i = 0; i < NET_QUEUE_LEN; i++) {
        free(tap->pkts_tx[i].data);
    }
    free(tap->pkt_rx.data);
    if (tap->fd >= 0) {
        close(tap->fd);
    }
    free(tap);
}

void net_tap_error(char *errbuf, const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    vsnprintf(errbuf, NET_DRV_ERRBUF_SIZE, format, ap);
    tap_log("TAP: %s", errbuf);
    va_end(ap);
}

// Error handling macro for the many ioctl calls we use in net_tap_alloc
#define ioctl_or_fail(fd, request, argp) \
    do {                                 \
        if ((err = ioctl(fd, request, argp)) < 0) { \
            tap_log("TAP: ioctl " #request " error: %s\n", strerror(errno)); \
            goto fail; \
        } \
    } while (0)

// Returns -ERRNO so we can get an idea what's wrong
int net_tap_alloc(const uint8_t *mac_addr, const char* bridge_dev)
{
    int fd;
    struct ifreq ifr = {0};
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
        tap_log("TAP: open error: %s\n", strerror(errno));
        return -errno;
    }
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    int err;
    if ((err = ioctl(fd, TUNSETIFF, &ifr)) < 0) {
        tap_log("TAP: ioctl TUNSETIFF error: %s\n", strerror(errno));
        close(fd);
        return -errno;
    }
    // Create a socket for ioctl operations
    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        tap_log("TAP: socket error: %s\n", strerror(errno));
        close(fd);
        return -errno;
    }
    // Bring the interface up
    tap_log("TAP: Bringing interface '%s' up.\n", ifr.ifr_name);
    ifr.ifr_flags = IFF_UP;
    ioctl_or_fail(sock, SIOCSIFFLAGS, &ifr);
    // Add interface to bridge, if specified
    if (bridge_dev && bridge_dev[0] != '\0') {
        // First see if the bridge exists
        struct ifreq ifr_bridge;
        //NOTE strncpy does not null terminate if the string is too long, I use
        //     snprintf or strlcpy instead
        //strncpy(ifr_bridge.ifr_name, bridge_dev, IFNAMSIZ);
        snprintf(ifr_bridge.ifr_name, IFNAMSIZ, "%s", bridge_dev);
        if ((err = ioctl(sock, SIOCGIFINDEX, &ifr_bridge)) < 0) {
            if (errno != ENODEV) {
                tap_log("TAP: ioctl SIOCGIFINDEX error: %s\n", strerror(errno));
                goto fail;
            } else {
                // Create the bridge
                ioctl_or_fail(sock, SIOCBRADDBR, &ifr_bridge);
                // Set the bridge up
                ifr_bridge.ifr_flags = IFF_UP;
                ioctl_or_fail(sock, SIOCSIFFLAGS, &ifr_bridge);
            }
        }
        // Get TAP index
        ioctl_or_fail(sock, SIOCGIFINDEX, &ifr);
        // Add the tap device to the bridge
        ifr_bridge.ifr_ifindex = ifr.ifr_ifindex;
        ioctl_or_fail(sock, SIOCBRADDIF, &ifr_bridge);
    }
    // close the socket we used for ioctl operations
    close(sock);
    tap_log("Allocated tap device %s\n", ifr.ifr_name);
    return fd;
    // cleanup point used by ioctl_or_fail macro
fail:
    close(sock);
    close(fd);
    return -errno;
}

void net_tap_in_available(void *priv)
{
    net_tap_t *tap = priv;
    net_event_set(&tap->tx_event);
}

void *
net_tap_init(
        const netcard_t *card,
        const uint8_t *mac_addr,
        void *priv,
        char *netdrv_errbuf)
{
    const char *bridge_dev = (void *) priv;
    int tap_fd = net_tap_alloc(mac_addr, bridge_dev);
    if (tap_fd < 0) {
        if (tap_fd == -EPERM) {
            net_tap_error(
                    netdrv_errbuf,
                    "No permissions to allocate tap device. "
                    "Try adding NET_CAP_ADMIN,NET_CAP_RAW to 86box ("
                    "sudo setcap 'CAP_NET_RAW,CAP_NET_ADMIN=eip')");
        } else {
            net_tap_error(
                    netdrv_errbuf,
                    "Unable to allocate TAP device: %s",
                    strerror(-tap_fd));
        }
        return NULL;
    }
    if (bridge_dev && bridge_dev[0] != '\0') {
    }
    net_tap_t *tap = calloc(1, sizeof(net_tap_t));
    if (!tap) {
        goto alloc_fail;
    }
    tap->pkt_rx.data = calloc(1, NET_MAX_FRAME);
    if (!tap->pkt_rx.data) {
        goto alloc_fail;
    }
    for(int i = 0; i < NET_QUEUE_LEN; i++) {
        tap->pkts_tx[i].data = calloc(1, NET_MAX_FRAME);
        if (!tap->pkts_tx[i].data) {
            goto alloc_fail;
        }
    }
    tap->fd   = tap_fd;
    tap->card = (netcard_t *) card;
    net_event_init(&tap->tx_event);
    net_event_init(&tap->stop_event);
    tap->poll_tid = thread_create(net_tap_thread, tap);
    return tap;
alloc_fail:
    net_tap_error(netdrv_errbuf, "Failed to allocate memory");
    close(tap_fd);
    free(tap);
    return NULL;
}

const netdrv_t net_tap_drv = {
    &net_tap_in_available,
    &net_tap_init,
    &net_tap_close,
    NULL
};
