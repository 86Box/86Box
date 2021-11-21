/*
 * 86Box        A hypervisor and IBM PC system emulator that specializes in
 *              running old operating systems and software designed for IBM
 *              PC systems and compatibles from 1981 through fairly recent
 *              system designs based on the PCI bus.
 *
 *              This file is part of the 86Box distribution.
 *
 *              Definitions for the Serial Passthrough Virtual Device
 *
 *
 *
 * Author:      Andreas J. Reichel, <webmaster@6th-dimension.com>
 *
 *              Copyright 2021          Andreas J. Reichel.
 */

#ifndef SERIAL_PASSTHROUGH_H
#define SERIAL_PASSTHROUGH_H


#include <stdint.h>
#include <stdbool.h>

#include <86box/device.h>
#include <86box/serial.h>


typedef struct serpt_ctx_s {
        serial_t *serial_dev;
        device_t *passthrough_dev;
        serpt_mode_t mode;
        char slave_pt[32];      /* used for pseudo terminal name of slave side */
        int master_fd;          /* file desc for master pseudo termnal or socket or alike */
} serpt_ctx_t;


void serial_passthrough_init(void);
uint8_t serial_passthrough_create(uint8_t com_port, serpt_mode_t mode);
void passthrough_override_data(serial_t *dev, uint8_t *data);
void passthrough_update_status(serial_t *dev);

#endif
