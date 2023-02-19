/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Definition of Serial passthrough device.
 *
 *
 * Authors:     Andreas J. Reichel <webmaster@6th-dimension.com>,
 *              Jasmine Iwanek <jasmine@iwanek.co.uk>
 *
 *              Copyright 2021      Andreas J. Reichel.
 *              Copyright 2021-2022 Jasmine Iwanek.
 */

#ifndef SERIAL_PASSTHROUGH_H
#define SERIAL_PASSTHROUGH_H

#include <stdint.h>
#include <stdbool.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>

enum serial_passthrough_mode {
    SERPT_MODE_VCON,    /*Named Pipe (Server) / Pseudo Terminal/Virtual Console */
    SERPT_MODE_TCPSRV,  /* TCP Server (TODO) */
    SERPT_MODE_TCPCLNT, /* TCP Client (TODO) */
    SERPT_MODE_HOSTSER, /* Host Serial Passthrough */
    SERPT_MODES_MAX,
};

extern const char *serpt_mode_names[SERPT_MODES_MAX];

typedef struct serial_passthrough_s {
    enum serial_passthrough_mode mode;
    pc_timer_t                   host_to_serial_timer;
    pc_timer_t                   serial_to_host_timer;
    serial_t                    *serial;
    double                       baudrate;
    uint8_t                      bits, data_bits;
    uint8_t                      port;
    uint8_t                      data;
    char                         slave_pt[32]; /* used for pseudo term name of slave side */
    intptr_t                     master_fd;    /* file desc for master pseudo terminal or
                                                * socket or alike */
    char  host_serial_path[1024];              /* Path to TTY/host serial port on the host */
    char  named_pipe[1024];                    /* (Windows only) Name of the pipe. */
    void *backend_priv;                        /* Private platform backend data */
} serial_passthrough_t;

extern bool           serial_passthrough_enabled[SERIAL_MAX];
extern const device_t serial_passthrough_device;

extern void serial_passthrough_init(void);

#endif
