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
 * Version:	@(#)scsi_device.c	1.0.21	2018/10/13
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
#include "../86box.h"
#include "../device.h"
#include "../disk/hdd.h"
#include "scsi.h"
#include "scsi_device.h"


scsi_device_t	scsi_devices[SCSI_ID_MAX];

uint8_t scsi_null_device_sense[18] = { 0x70,0,SENSE_ILLEGAL_REQUEST,0,0,0,0,0,0,0,0,0,ASC_INV_LUN,0,0,0,0,0 };


static uint8_t
scsi_device_target_command(scsi_device_t *dev, uint8_t *cdb)
{
    if (dev->command && dev->err_stat_to_scsi) {
	dev->command(dev->p, cdb);
	return dev->err_stat_to_scsi(dev->p);
    } else
	return SCSI_STATUS_CHECK_CONDITION;
}


static void scsi_device_target_callback(scsi_device_t *dev)
{
    if (dev->callback)
	dev->callback(dev->p);

    return;
}


static int scsi_device_target_err_stat_to_scsi(scsi_device_t *dev)
{
    if (dev->err_stat_to_scsi)
	return dev->err_stat_to_scsi(dev->p);
    else
	return SCSI_STATUS_CHECK_CONDITION;
}


int64_t scsi_device_get_callback(scsi_device_t *dev)
{
    scsi_device_data_t *sdd = (scsi_device_data_t *) dev->p;

    if (sdd)
	return sdd->callback;
    else
	return -1LL;
}


uint8_t *scsi_device_sense(scsi_device_t *dev)
{
    scsi_device_data_t *sdd = (scsi_device_data_t *) dev->p;

    if (sdd)
	return sdd->sense;
    else
	return scsi_null_device_sense;
}


void scsi_device_request_sense(scsi_device_t *dev, uint8_t *buffer, uint8_t alloc_length)
{
    if (dev->request_sense)
	dev->request_sense(dev->p, buffer, alloc_length);
    else
	memcpy(buffer, scsi_null_device_sense, alloc_length);
}


void scsi_device_reset(scsi_device_t *dev)
{
    if (dev->reset)
	dev->reset(dev->p);
}


int scsi_device_present(scsi_device_t *dev)
{
    if (dev->type == SCSI_NONE)
	return 0;
    else
	return 1;
}


int scsi_device_valid(scsi_device_t *dev)
{
    if (dev->p)
	return 1;
    else
	return 0;
}


int scsi_device_cdb_length(scsi_device_t *dev)
{
    /* Right now, it's 12 for all devices. */
    return 12;
}


void scsi_device_command_phase0(scsi_device_t *dev, uint8_t *cdb)
{
    if (!dev->p) {
	dev->phase = SCSI_PHASE_STATUS;
	dev->status = SCSI_STATUS_CHECK_CONDITION;
	return;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    dev->phase = SCSI_PHASE_COMMAND;
    dev->status = scsi_device_target_command(dev, cdb);

    if (dev->phase == SCSI_PHASE_STATUS) {
	/* Command completed (either OK or error) - call the phase callback to complete the command. */
	scsi_device_target_callback(dev);
    }
    /* If the phase is DATA IN or DATA OUT, finish this here. */
}

void scsi_device_command_phase1(scsi_device_t *dev)
{
    if (!dev->p)
	return;

    /* Call the second phase. */
    scsi_device_target_callback(dev);
    dev->status = scsi_device_target_err_stat_to_scsi(dev);
    /* Command second phase complete - call the callback to complete the command. */
    scsi_device_target_callback(dev);
}

int32_t *scsi_device_get_buf_len(scsi_device_t *dev)
{
    return &dev->buffer_length;
}
