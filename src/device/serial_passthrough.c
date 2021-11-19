/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Serial Passthrough Virtual Device
 *
 *
 *
 * Author:	Andreas J. Reichel, <webmaster@6th-dimension.com>
 *
 *		Copyright 2021          Andreas J. Reichel.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/serial_passthrough.h>
#include <86box/plat_serial_passthrough.h>


static device_t serial_passthrough_devs[SERIAL_MAX];
static int num_passthrough_devs;


static void *
serpt_dev_init(const struct _device_ *dev)
{
        /* FIXME: just allocate some memory to return something.

           This will be the platform specific and generic
           interface for the host adapter to write and recv
           data as well as to hold the SOCKET or dev name and
           things like that */
        return malloc(sizeof(int));
} 


static void
serpt_dev_close(void *priv)
{
        free(priv);
}


static void
serpt_dev_reset(void *priv)
{
        /* FIXME: Nothing to do yet? */
}


void
serial_passthrough_init(void)
{
        memset(serial_passthrough_devs, 0, sizeof(serial_passthrough_devs)); 
        num_passthrough_devs = 0;
}


static void
serpt_rcr(struct serial_s *serial, void *p)
{
        /* TODO: Handle register base+4 writes */
}


static void
serpt_write(struct serial_s *serial, void *p, uint8_t data)
{
        /* Serial port writes out data byte to the host,
           handle it platform specifically */
        plat_serpt_write(p, data);
}


uint8_t
serial_passthrough_create(uint8_t com_port)
{
        device_t *sp_dev = NULL;
        char tmp[32];
        uint8_t i;
        
        memset(tmp, 0, sizeof(tmp));

        /* Get free slot in local instance list */
        if (num_passthrough_devs >= SERIAL_MAX) {
                return 1;
        }

        i = num_passthrough_devs;
        sprintf(tmp, "serpt_dev%u", i);
        
        /* Create a serial passthrough-device */
        serial_passthrough_devs[i].flags = DEVICE_COM;
        serial_passthrough_devs[i].name = tmp;
        serial_passthrough_devs[i].close = serpt_dev_close;
        serial_passthrough_devs[i].init = serpt_dev_init;
        serial_passthrough_devs[i].reset = serpt_dev_reset;

        /* Add the device to the 'device manager' */
        void *priv = device_add(&serial_passthrough_devs[i]); 
       
        if (!priv) {
                return 1;
        } 

        /* Attach the device to the serial port */
        serial_t *s = serial_attach(com_port, serpt_rcr, serpt_write, priv);

        if (!s) {
                /* actually we could remove the device here, but that
                 * should not matter at the moment and there is no
                 * device_remove API call, so let the dev manager
                 * clean up in the very end. */
                return 1;
        }

        return 0;
}

