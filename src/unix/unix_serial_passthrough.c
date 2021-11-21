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
 *
 * Authors:     Andreas J. Reichel <webmaster@6th-dimension.com>
 *
 *              Copyright 2021          Andreas J. Reichel
 */

#define _XOPEN_SOURCE 500
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
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/device.h>
#include <86box/serial_passthrough.h>
#include <86box/plat_serial_passthrough.h>


#define LOG_PREFIX "serial_passthrough: "


int
plat_serpt_read(void *p, uint8_t *data)
{
        serial_passthrough_t *dev = (serial_passthrough_t *)p;
        int res;
        struct timeval tv;
        fd_set rdfds;

        switch (dev->mode) {
        case SERPT_MODE_VCON:
                FD_ZERO(&rdfds);
                FD_SET(dev->master_fd, &rdfds);
                tv.tv_sec = 0;
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
plat_serpt_close(void *p)
{
        serial_passthrough_t *dev = (serial_passthrough_t *)p;

        close(dev->master_fd);
}


static void
plat_serpt_write_vcon(serial_passthrough_t *dev, uint8_t data)
{
        /* fd_set wrfds;
         * int res;
         */

        /* We cannot use select here, this would block the hypervisor! */
        /* FD_ZERO(&wrfds);
           FD_SET(ctx->master_fd, &wrfds);
        
           res = select(ctx->master_fd + 1, NULL, &wrfds, NULL, NULL);
        
           if (res <= 0) {
                return;
           }
        */

        /* just write it out */
        write(dev->master_fd, &data, 1);
}


void
plat_serpt_write(void *p, uint8_t data)
{
        serial_passthrough_t *dev = (serial_passthrough_t *)p;
        
        switch (dev->mode) {
        case SERPT_MODE_VCON:
                plat_serpt_write_vcon(dev, data);
                break;
        default:
                break;
        }
}


static int
open_pseudo_terminal(serial_passthrough_t *dev)
{
        int master_fd = open("/dev/ptmx", O_RDWR);
        char *ptname;

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
        strncpy(dev->slave_pt, ptname, sizeof(dev->slave_pt)-1);

        pclog(LOG_PREFIX "Slave side is %s\n", dev->slave_pt);

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

        dev->master_fd = master_fd;

        return master_fd;
}


int
plat_serpt_open_device(void *p)
{
        serial_passthrough_t *dev = (serial_passthrough_t *)p;

        switch (dev->mode) {
        case SERPT_MODE_VCON:
                if (!open_pseudo_terminal(dev)) {
                        return 1;
                }
                break;
        default:
                break;

        }
        return 0;
}
