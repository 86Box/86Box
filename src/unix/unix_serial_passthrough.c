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

#include <86box/86box.h>
#include <86box/log.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/device.h>
#include <86box/serial_passthrough.h>
#include <86box/plat_serial_passthrough.h>


#define LOG_PREFIX "serial_passthrough: "


void
plat_serpt_update_status(void *p, uint8_t *lsr)
{
        fd_set rdfds, wrfds;
        struct timeval tv;
        serpt_ctx_t *ctx = (serpt_ctx_t *)p;

        memset(&tv, 0, sizeof(struct timeval));

        switch (ctx->mode) {
        case SERPT_VIRTUAL_CON:
                FD_ZERO(&rdfds);
                FD_ZERO(&wrfds);
                FD_SET(ctx->master_fd, &rdfds);
                FD_SET(ctx->master_fd, &wrfds);

                /* Update DATA_READY and THR_EMPTY in line status register
                 * according to read- and writeability of the file descriptor
                 * to the pseudo terminal */
                if (select(ctx->master_fd + 1,
                           &rdfds, &wrfds, NULL, &tv) > 0) {

                        if (FD_ISSET(ctx->master_fd, &rdfds)) {
                                *lsr |= LSR_DATA_READY;
                        } else {
                                *lsr &= ~LSR_DATA_READY;
                        }
                        if (FD_ISSET(ctx->master_fd, &wrfds)) {
                                *lsr |= LSR_THR_EMPTY;
                        } else {
                                *lsr &= ~LSR_THR_EMPTY;
                        }
                } else {
                        *lsr &= ~(LSR_THR_EMPTY | LSR_DATA_READY);
                }
                break;
        default:

                break; 
        }
}


void
plat_serpt_override_data(void *p, uint8_t *data)
{
        uint8_t byte;
        serpt_ctx_t *ctx = (serpt_ctx_t *)p;

        switch (ctx->mode) {
        case SERPT_VIRTUAL_CON:
                if (read(ctx->master_fd, &byte, 1) == 1) {
                        *data = byte;
                }
                return;
        default:
                break;
        }
}


void
plat_serpt_close(void *p)
{
        serpt_ctx_t *ctx = (serpt_ctx_t *)p;

        close(ctx->master_fd);
}

        
static void
plat_serpt_write_vcon(serpt_ctx_t *ctx, uint8_t data)
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
        write(ctx->master_fd, &data, 1);
}


void
plat_serpt_write(void *p, uint8_t data)
{
        serpt_ctx_t *ctx = (serpt_ctx_t *)p;
        
        switch (ctx->mode) {
        case SERPT_VIRTUAL_CON:
                plat_serpt_write_vcon(ctx, data);
                return;
        default:
                break;
        }
}


static int
open_pseudo_terminal(serpt_ctx_t *ctx)
{
        int master_fd = open("/dev/ptmx", O_NONBLOCK | O_RDWR);
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
        memset(ctx->slave_pt, 0, sizeof(ctx->slave_pt));
        strncpy(ctx->slave_pt, ptname, sizeof(ctx->slave_pt)-1);

        pclog(LOG_PREFIX "Slave side is %s\n", ctx->slave_pt);

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

        ctx->master_fd = master_fd;

        return master_fd;
}


int
plat_serpt_open_device(void *p)
{
        serpt_ctx_t *ctx = (serpt_ctx_t *)p;

        switch (ctx->mode) {
        case SERPT_VIRTUAL_CON:
                if (!open_pseudo_terminal(ctx)) {
                        return 1;
                }
                break;
        default:
                break;

        }
        return 0;
}
