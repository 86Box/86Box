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
 * Author:      Andreas J. Reichel, <webmaster@6th-dimension.com>
 *
 *              Copyright 2021          Andreas J. Reichel 
 */

#ifndef SERIAL_PASSTHROUGH_H
#define SERIAL_PASSTHROUGH_H

#include <stdint.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>


enum passthrough_mode {
        PASSTHROUGH_MODE_VCON = 1
};


typedef struct serial_passthrough_s {
        enum passthrough_mode mode;
        pc_timer_t host_to_serial_timer;
        pc_timer_t serial_to_host_timer;
        serial_t *serial;
        uint32_t baudrate;
        uint8_t port;
        uint8_t data;
} serial_passthrough_t;

#endif
