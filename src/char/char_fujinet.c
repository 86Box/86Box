/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          FujiNet Bus-over-IP (BoIP) character device. Bridges an
 *          emulated COM port straight to a FujiNet-PC BoIP listener over
 *          TCP, byte for byte, so a FUJINET.SYS driver running in the
 *          guest can talk to FujiNet without any host-side PTY plumbing.
 *
 *
 *
 * Authors: FujiNet Project
 *
 *          Copyright 2026 FujiNet Project.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/char.h>
#include <86box/log.h>
#include <86box/plat.h>
#include <86box/plat_netsocket.h>

#define FUJINET_DEFAULT_HOST "127.0.0.1"
#define FUJINET_DEFAULT_PORT 1987

#ifdef ENABLE_CHAR_FUJINET_LOG
int char_fujinet_do_log = ENABLE_CHAR_FUJINET_LOG;

static void
char_fujinet_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (char_fujinet_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define char_fujinet_log(priv, fmt, ...)
#endif

enum {
    FUJINET_STATE_DISCONNECTED = 0,
    FUJINET_STATE_CONNECTING,
    FUJINET_STATE_CONNECTED
};

typedef struct {
    void        *log;
    char_port_t *port;

    SOCKET   sock;
    int      state;
    uint32_t next_attempt;

    /* Snapshotted at init() time - device_get_config_*() relies on a
     * device context that is only valid during device init/close, not
     * from this device's own read()/write() callbacks invoked later by
     * the serial port's receive/transmit timers. */
    char     host[256];
    int      port_num;
} char_fujinet_t;

static void
char_fujinet_disconnect(char_fujinet_t *dev)
{
    if (CHAR_FD_VALID(dev->sock)) {
        plat_netsocket_close(dev->sock);
        dev->sock = (SOCKET) -1;
    }
    dev->state = FUJINET_STATE_DISCONNECTED;
    char_update_status(dev->port);
}

/* Attempt (re)connection, throttled to once per CHAR_RECONNECT_MS. Safe to
 * call on every read()/write() poll - it's a no-op unless a retry is due. */
static void
char_fujinet_poll_connect(char_fujinet_t *dev)
{
    uint32_t now = plat_get_ticks();

    switch (dev->state) {
        case FUJINET_STATE_DISCONNECTED:
            if ((now - dev->next_attempt) >= CHAR_RECONNECT_MS || dev->next_attempt == 0) {
                dev->sock = plat_netsocket_create(NET_SOCKET_TCP);
                if (CHAR_FD_VALID(dev->sock)) {
                    if (plat_netsocket_connect(dev->sock, dev->host, (unsigned short) dev->port_num) == 0) {
                        dev->state = FUJINET_STATE_CONNECTING;
                        char_fujinet_log(dev->log, "Connecting to %s:%d\n", dev->host, dev->port_num);
                    } else {
                        char_fujinet_log(dev->log, "connect() to %s:%d failed\n", dev->host, dev->port_num);
                        plat_netsocket_close(dev->sock);
                        dev->sock = (SOCKET) -1;
                    }
                }
                dev->next_attempt = now;
            }
            break;

        case FUJINET_STATE_CONNECTING: {
            int connected = plat_netsocket_connected(dev->sock);
            if (connected == 1) {
                dev->state = FUJINET_STATE_CONNECTED;
                char_fujinet_log(dev->log, "Connected\n");
                char_update_status(dev->port);
            } else if (connected == -1) {
                char_fujinet_log(dev->log, "Connection attempt failed\n");
                char_fujinet_disconnect(dev);
                dev->next_attempt = now;
            }
            break;
        }

        case FUJINET_STATE_CONNECTED:
        default:
            break;
    }
}

static size_t
char_fujinet_read(uint8_t *buf, size_t len, void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    char_fujinet_poll_connect(dev);

    if (dev->state != FUJINET_STATE_CONNECTED)
        return 0;

    int wouldblock = 0;
    int ret         = plat_netsocket_receive(dev->sock, buf, (unsigned int) len, &wouldblock);
    if (ret < 0) {
        if (!wouldblock) {
            char_fujinet_log(dev->log, "recv() error, disconnecting\n");
            char_fujinet_disconnect(dev);
        }
        return 0;
    }
    if (ret == 0) {
        /* Peer closed the connection. */
        char_fujinet_log(dev->log, "Peer closed connection\n");
        char_fujinet_disconnect(dev);
        return 0;
    }

    return (size_t) ret;
}

static size_t
char_fujinet_write(uint8_t *buf, size_t len, void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    if (dev->state != FUJINET_STATE_CONNECTED)
        return 0;

    int wouldblock = 0;
    int ret         = plat_netsocket_send(dev->sock, buf, (unsigned int) len, &wouldblock);
    if (ret < 0) {
        if (!wouldblock) {
            char_fujinet_log(dev->log, "send() error, disconnecting\n");
            char_fujinet_disconnect(dev);
        }
        return 0;
    }

    return (size_t) ret;
}

static uint32_t
char_fujinet_status(void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    if (dev->state == FUJINET_STATE_CONNECTED)
        return CHAR_COM_CTS | CHAR_COM_DSR | CHAR_COM_DCD;

    return CHAR_DISCONNECTED;
}

static void
char_fujinet_control(uint32_t flags, void *priv)
{
    /* FujiNet's RS-232 wire protocol is fully framed (SLIP) and needs no
     * DTR/RTS handshaking over TCP - see FUJICOM-Protocol.md. Nothing to
     * do here; kept only to satisfy the char_device_t contract. */
    (void) flags;
    (void) priv;
}

static void
char_fujinet_close(void *priv)
{
    char_fujinet_t *dev = (char_fujinet_t *) priv;

    char_fujinet_disconnect(dev);
    log_close(dev->log);
    free(dev);
}

static void *
char_fujinet_init(const device_t *info)
{
    char_fujinet_t *dev = (char_fujinet_t *) calloc(1, sizeof(char_fujinet_t));

    dev->sock  = (SOCKET) -1;
    dev->state = FUJINET_STATE_DISCONNECTED;

    /* device_get_config_*() only works while the device context set up by
     * device_add_inst() is active, i.e. right here in init() - snapshot
     * what we need before it goes away. */
    strncpy(dev->host, device_get_config_string("host"), sizeof(dev->host) - 1);
    dev->port_num = device_get_config_int("port");

    dev->port = char_attach(0, char_fujinet_read, char_fujinet_write, char_fujinet_status,
                             char_fujinet_control, NULL, dev);
    dev->log  = char_log_open(dev->port, "FujiNet");
    char_fujinet_log(dev->log, "init()\n");

    char_update_status(dev->port);

    return dev;
}

// clang-format off
static const device_config_t char_fujinet_config[] = {
    {
        .name           = "host",
        .description    = "Host",
        .type           = CONFIG_STRING,
        .default_string = FUJINET_DEFAULT_HOST,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name        = "port",
        .description = "Port",
        .type        = CONFIG_SPINNER,
        .default_int = FUJINET_DEFAULT_PORT,
        .file_filter = NULL,
        .spinner     = { .min = 1, .max = 32767, .step = 1 },
        .selection   = { { 0 } },
        .bios        = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t char_fujinet_com_device = {
    .name          = "FujiNet (BoIP)",
    .internal_name = "fujinet",
    .flags         = DEVICE_COM | DEVICE_HOTPLUG,
    .local         = 0,
    .init          = char_fujinet_init,
    .close         = char_fujinet_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = char_fujinet_config
};
