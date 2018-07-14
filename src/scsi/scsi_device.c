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
 * Version:	@(#)scsi_device.c	1.0.17	2018/06/02
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
#include "../cdrom/cdrom.h"
#include "../disk/zip.h"
#include "scsi_disk.h"


uint8_t scsi_null_device_sense[18] = { 0x70,0,SENSE_ILLEGAL_REQUEST,0,0,0,0,0,0,0,0,0,ASC_INV_LUN,0,0,0,0,0 };


static uint8_t
scsi_device_target_command(int lun_type, uint8_t id, uint8_t *cdb)
{
    switch(lun_type) {
	case SCSI_DISK:
		scsi_disk_command(scsi_disk[id], cdb);
		return scsi_disk_err_stat_to_scsi(scsi_disk[id]);
	case SCSI_CDROM:
		cdrom_command(cdrom[id], cdb);
		return cdrom_CDROM_PHASE_to_scsi(cdrom[id]);
	case SCSI_ZIP:
		zip_command(zip[id], cdb);
		return zip_ZIP_PHASE_to_scsi(zip[id]);
	default:
		return SCSI_STATUS_CHECK_CONDITION;
    }
}


static void scsi_device_target_phase_callback(int lun_type, uint8_t id)
{
    switch(lun_type) {
	case SCSI_DISK:
		scsi_disk_callback(scsi_disk[id]);
		break;
	case SCSI_CDROM:
		cdrom_phase_callback(cdrom[id]);
		break;
	case SCSI_ZIP:
		zip_phase_callback(zip[id]);
		break;
    }
    return;
}


static int scsi_device_target_err_stat_to_scsi(int lun_type, uint8_t id)
{
    switch(lun_type) {
	case SCSI_DISK:
		return scsi_disk_err_stat_to_scsi(scsi_disk[id]);
	case SCSI_CDROM:
		return cdrom_CDROM_PHASE_to_scsi(cdrom[id]);
	case SCSI_ZIP:
		return zip_ZIP_PHASE_to_scsi(zip[id]);
	default:
		return SCSI_STATUS_CHECK_CONDITION;
    }
}


int64_t scsi_device_get_callback(uint8_t scsi_id)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		return scsi_disk[id]->callback;
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		return cdrom[id]->callback;
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		return zip[id]->callback;
		break;
	default:
		return -1LL;
		break;
    }
}


uint8_t *scsi_device_sense(uint8_t scsi_id)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		return scsi_disk[id]->sense;
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		return cdrom[id]->sense;
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		return zip[id]->sense;
		break;
	default:
		return scsi_null_device_sense;
		break;
    }
}


void scsi_device_request_sense(uint8_t scsi_id, uint8_t *buffer, uint8_t alloc_length)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		scsi_disk_request_sense_for_scsi(scsi_disk[id], buffer, alloc_length);
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		cdrom_request_sense_for_scsi(cdrom[id], buffer, alloc_length);
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		zip_request_sense_for_scsi(zip[id], buffer, alloc_length);
		break;
	default:
		memcpy(buffer, scsi_null_device_sense, alloc_length);
		break;
    }
}


void scsi_device_reset(uint8_t scsi_id)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		scsi_disk_reset(scsi_disk[id]);
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		cdrom_reset(cdrom[id]);
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		zip_reset(zip[id]);
		break;
    }
}


void scsi_device_type_data(uint8_t scsi_id, uint8_t *type, uint8_t *rmb)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    switch (lun_type)
    {
	case SCSI_DISK:
		*type = *rmb = 0x00;
		break;
	case SCSI_CDROM:
		*type = 0x05;
		*rmb = 0x80;
		break;
	case SCSI_ZIP:
		*type = 0x00;
		*rmb = 0x80;
		break;
	default:
		*type = *rmb = 0xff;
		break;
    }
}


int scsi_device_read_capacity(uint8_t scsi_id, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		return scsi_disk_read_capacity(scsi_disk[id], cdb, buffer, len);
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		return cdrom_read_capacity(cdrom[id], cdb, buffer, len);
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		return zip_read_capacity(zip[id], cdb, buffer, len);
	default:
		return 0;
    }
}


int scsi_device_present(uint8_t scsi_id)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    switch (lun_type)
    {
	case SCSI_NONE:
		return 0;
	default:
		return 1;
    }
}


int scsi_device_valid(uint8_t scsi_id)
{
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		break;
	default:
		id = 0;
		break;
    }

    return (id == 0xFF) ? 0 : 1;
}


int scsi_device_cdb_length(uint8_t scsi_id)
{
    /* Right now, it's 12 for all devices. */
    return 12;
}


void scsi_device_command_phase0(uint8_t scsi_id, uint8_t *cdb)
{
    uint8_t id = 0;
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    switch (lun_type) {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		break;
	default:
		id = 0;
		SCSIDevices[scsi_id].Phase = SCSI_PHASE_STATUS;
		SCSIDevices[scsi_id].Status = SCSI_STATUS_CHECK_CONDITION;
		return;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    SCSIDevices[scsi_id].Phase = SCSI_PHASE_COMMAND;
    SCSIDevices[scsi_id].Status = scsi_device_target_command(lun_type, id, cdb);

    if (SCSIDevices[scsi_id].Phase == SCSI_PHASE_STATUS) {
	/* Command completed (either OK or error) - call the phase callback to complete the command. */
	scsi_device_target_phase_callback(lun_type, id);
    }
    /* If the phase is DATA IN or DATA OUT, finish this here. */
}

void scsi_device_command_phase1(uint8_t scsi_id)
{
    uint8_t id = 0;
    uint8_t lun_type = SCSIDevices[scsi_id].LunType;

    switch (lun_type) {
	case SCSI_DISK:
		id = scsi_disks[scsi_id];
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id];
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id];
		break;
	default:
		id = 0;
		return;
    }

    /* Call the second phase. */
    scsi_device_target_phase_callback(lun_type, id);
    SCSIDevices[scsi_id].Status = scsi_device_target_err_stat_to_scsi(lun_type, id);
    /* Command second phase complete - call the callback to complete the command. */
    scsi_device_target_phase_callback(lun_type, id);
}

int32_t *scsi_device_get_buf_len(uint8_t scsi_id)
{
    return &SCSIDevices[scsi_id].BufferLength;
}
