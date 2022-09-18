/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The generic SCSI device command handler.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/hdd.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>

scsi_device_t scsi_devices[SCSI_BUS_MAX][SCSI_ID_MAX];

uint8_t scsi_null_device_sense[18] = { 0x70, 0, SENSE_ILLEGAL_REQUEST, 0, 0, 0, 0, 0, 0, 0, 0, 0, ASC_INV_LUN, 0, 0, 0, 0, 0 };

static uint8_t
scsi_device_target_command(scsi_device_t *dev, uint8_t *cdb)
{
    if (dev->command) {
        dev->command(dev->sc, cdb);

        if (dev->sc->status & ERR_STAT)
            return SCSI_STATUS_CHECK_CONDITION;
        else
            return SCSI_STATUS_OK;
    } else
        return SCSI_STATUS_CHECK_CONDITION;
}

double
scsi_device_get_callback(scsi_device_t *dev)
{
    if (dev->sc)
        return dev->sc->callback;
    else
        return -1.0;
}

uint8_t *
scsi_device_sense(scsi_device_t *dev)
{
    if (dev->sc)
        return dev->sc->sense;
    else
        return scsi_null_device_sense;
}

void
scsi_device_request_sense(scsi_device_t *dev, uint8_t *buffer, uint8_t alloc_length)
{
    if (dev->request_sense)
        dev->request_sense(dev->sc, buffer, alloc_length);
    else
        memcpy(buffer, scsi_null_device_sense, alloc_length);
}

void
scsi_device_reset(scsi_device_t *dev)
{
    if (dev->reset)
        dev->reset(dev->sc);
}

int
scsi_device_present(scsi_device_t *dev)
{
    if (dev->type == SCSI_NONE)
        return 0;
    else
        return 1;
}

int
scsi_device_valid(scsi_device_t *dev)
{
    if (dev->sc)
        return 1;
    else
        return 0;
}

int
scsi_device_cdb_length(scsi_device_t *dev)
{
    /* Right now, it's 12 for all devices. */
    return 12;
}

void
scsi_device_command_phase0(scsi_device_t *dev, uint8_t *cdb)
{
    if (!dev->sc) {
        dev->phase  = SCSI_PHASE_STATUS;
        dev->status = SCSI_STATUS_CHECK_CONDITION;
        return;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    dev->phase  = SCSI_PHASE_COMMAND;
    dev->status = scsi_device_target_command(dev, cdb);
}

void
scsi_device_command_stop(scsi_device_t *dev)
{
    if (dev->command_stop) {
        dev->command_stop(dev->sc);
        dev->status = SCSI_STATUS_OK;
    }
}

void
scsi_device_command_phase1(scsi_device_t *dev)
{
    if (!dev->sc)
        return;

    /* Call the second phase. */
    if (dev->phase == SCSI_PHASE_DATA_OUT) {
        if (dev->phase_data_out)
            dev->phase_data_out(dev->sc);
    } else
        scsi_device_command_stop(dev);

    if (dev->sc->status & ERR_STAT)
        dev->status = SCSI_STATUS_CHECK_CONDITION;
    else
        dev->status = SCSI_STATUS_OK;
}

/* When LUN is FF, there has been no IDENTIFY message, otherwise
   there has been one. */
void
scsi_device_identify(scsi_device_t *dev, uint8_t lun)
{
    if ((dev == NULL) || (dev->type == SCSI_NONE) || !dev->sc)
        return;

    dev->sc->cur_lun = lun;

    /* TODO: This should return a value, should IDENTIFY fail due to a
             a LUN not supported by the target. */
}

void
scsi_device_close_all(void)
{
    int            i, j;
    scsi_device_t *dev;

    for (i = 0; i < SCSI_BUS_MAX; i++) {
        for (j = 0; j < SCSI_ID_MAX; j++) {
            dev = &(scsi_devices[i][j]);
            if (dev->command_stop && dev->sc)
                dev->command_stop(dev->sc);
        }
    }
}

void
scsi_device_init(void)
{
    int            i, j;
    scsi_device_t *dev;

    for (i = 0; i < SCSI_BUS_MAX; i++) {
        for (j = 0; j < SCSI_ID_MAX; j++) {
            dev = &(scsi_devices[i][j]);

            memset(dev, 0, sizeof(scsi_device_t));
            dev->type = SCSI_NONE;
        }
    }
}
