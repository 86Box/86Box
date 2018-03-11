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
 * Version:	@(#)scsi_device.c	1.0.14	2018/03/07
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
#include "../cdrom/cdrom.h"
#include "../disk/hdd.h"
#include "../disk/zip.h"
#include "scsi.h"
#include "scsi_disk.h"


static uint8_t scsi_null_device_sense[14] = { 0x70,0,SENSE_ILLEGAL_REQUEST,0,0,0,0,0,0,0,0,0,ASC_INV_LUN,0 };


static uint8_t scsi_device_target_command(int lun_type, uint8_t id, uint8_t *cdb)
{
	if (lun_type == SCSI_DISK)
	{
		scsi_hd_command(id, cdb);
		return scsi_hd_err_stat_to_scsi(id);
	}
	else if (lun_type == SCSI_CDROM)
	{
		cdrom_command(id, cdb);
		return cdrom_CDROM_PHASE_to_scsi(id);
	}
	else if (lun_type == SCSI_ZIP)
	{
		zip_command(id, cdb);
		return zip_ZIP_PHASE_to_scsi(id);
	}
	else
	{
		return SCSI_STATUS_CHECK_CONDITION;
	}
}


static void scsi_device_target_phase_callback(int lun_type, uint8_t id)
{
	if (lun_type == SCSI_DISK)
	{
		scsi_hd_callback(id);
	}
	else if (lun_type == SCSI_CDROM)
	{
		cdrom_phase_callback(id);
	}
	else if (lun_type == SCSI_ZIP)
	{
		zip_phase_callback(id);
	}
	else
	{
		return;
	}
}


static int scsi_device_target_err_stat_to_scsi(int lun_type, uint8_t id)
{
	if (lun_type == SCSI_DISK)
	{
		return scsi_hd_err_stat_to_scsi(id);
	}
	else if (lun_type == SCSI_CDROM)
	{
		return cdrom_CDROM_PHASE_to_scsi(id);
	}
	else if (lun_type == SCSI_ZIP)
	{
		return zip_ZIP_PHASE_to_scsi(id);
	}
	else
	{
		return SCSI_STATUS_CHECK_CONDITION;
	}
}


static void scsi_device_target_save_cdb_byte(int lun_type, uint8_t id, uint8_t cdb_byte)
{
	if (lun_type == SCSI_DISK)
	{
		shdc[id].request_length = cdb_byte;
	}
	else if (lun_type == SCSI_CDROM)
	{
		cdrom[id].request_length = cdb_byte;
	}
	else if (lun_type == SCSI_ZIP)
	{
		zip[id].request_length = cdb_byte;
	}
	else
	{
		return;
	}
}


int64_t scsi_device_get_callback(uint8_t scsi_id, uint8_t scsi_lun)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_hard_disks[scsi_id][scsi_lun];
		return shdc[id].callback;
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id][scsi_lun];
		return cdrom[id].callback;
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id][scsi_lun];
		return zip[id].callback;
		break;
	default:
		return -1LL;
		break;
    }
}


uint8_t *scsi_device_sense(uint8_t scsi_id, uint8_t scsi_lun)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_hard_disks[scsi_id][scsi_lun];
		return shdc[id].sense;
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id][scsi_lun];
		return cdrom[id].sense;
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id][scsi_lun];
		return zip[id].sense;
		break;
	default:
		return scsi_null_device_sense;
		break;
    }
}


void scsi_device_request_sense(uint8_t scsi_id, uint8_t scsi_lun, uint8_t *buffer, uint8_t alloc_length)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_hard_disks[scsi_id][scsi_lun];
		scsi_hd_request_sense_for_scsi(id, buffer, alloc_length);
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id][scsi_lun];
		cdrom_request_sense_for_scsi(id, buffer, alloc_length);
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id][scsi_lun];
		zip_request_sense_for_scsi(id, buffer, alloc_length);
		break;
	default:
		memcpy(buffer, scsi_null_device_sense, alloc_length);
		break;
    }
}


void scsi_device_type_data(uint8_t scsi_id, uint8_t scsi_lun, uint8_t *type, uint8_t *rmb)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_hard_disks[scsi_id][scsi_lun];
		*type = 0x00;
		*rmb = (hdd[id].bus == HDD_BUS_SCSI_REMOVABLE) ? 0x80 : 0x00;
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
		*type = *rmb = 0xFF;
		break;
    }
}


int scsi_device_read_capacity(uint8_t scsi_id, uint8_t scsi_lun, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_hard_disks[scsi_id][scsi_lun];
		return scsi_hd_read_capacity(id, cdb, buffer, len);
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id][scsi_lun];
		return cdrom_read_capacity(id, cdb, buffer, len);
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id][scsi_lun];
		return zip_read_capacity(id, cdb, buffer, len);
	default:
		return 0;
    }
}


int scsi_device_present(uint8_t scsi_id, uint8_t scsi_lun)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    switch (lun_type)
    {
	case SCSI_NONE:
		return 0;
	default:
		return 1;
    }
}


int scsi_device_valid(uint8_t scsi_id, uint8_t scsi_lun)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_hard_disks[scsi_id][scsi_lun];
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id][scsi_lun];
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id][scsi_lun];
		break;
	default:
		id = 0;
		break;
    }

    return (id == 0xFF) ? 0 : 1;
}


int scsi_device_cdb_length(uint8_t scsi_id, uint8_t scsi_lun)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id][scsi_lun];
		return cdrom[id].cdb_len;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id][scsi_lun];
		return zip[id].cdb_len;
	default:
		return 12;
    }
}


void scsi_device_command_phase0(uint8_t scsi_id, uint8_t scsi_lun, int cdb_len, uint8_t *cdb)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    uint8_t id = 0;

    switch (lun_type)
    {
	case SCSI_DISK:
		id = scsi_hard_disks[scsi_id][scsi_lun];
		break;
	case SCSI_CDROM:
		id = scsi_cdrom_drives[scsi_id][scsi_lun];
		break;
	case SCSI_ZIP:
		id = scsi_zip_drives[scsi_id][scsi_lun];
		break;
	default:
		id = 0;
		SCSIDevices[scsi_id][scsi_lun].Phase = SCSI_PHASE_STATUS;
		SCSIDevices[scsi_id][scsi_lun].Status = SCSI_STATUS_CHECK_CONDITION;
		return;
    }

    /*
     * Since that field in the target struct is never used when
     * the bus type is SCSI, let's use it for this scope.
     */
    scsi_device_target_save_cdb_byte(lun_type, id, cdb[1]);

    if (cdb_len != 12) {
	/*
	 * Make sure the LUN field of the temporary CDB is always 0,
	 * otherwise Daemon Tools drives will misbehave when a command
	 * is passed through to them.
	 */
	cdb[1] &= 0x1f;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    SCSIDevices[scsi_id][scsi_lun].Phase = SCSI_PHASE_COMMAND;
    SCSIDevices[scsi_id][scsi_lun].Status = scsi_device_target_command(lun_type, id, cdb);

    if (SCSIDevices[scsi_id][scsi_lun].Phase == SCSI_PHASE_STATUS) {
	/* Command completed (either OK or error) - call the phase callback to complete the command. */
	scsi_device_target_phase_callback(lun_type, id);
    }
    /* If the phase is DATA IN or DATA OUT, finish this here. */
}

void scsi_device_command_phase1(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

	uint8_t id = 0;

	switch (lun_type)
	{
		case SCSI_DISK:
			id = scsi_hard_disks[scsi_id][scsi_lun];
			break;
		case SCSI_CDROM:
			id = scsi_cdrom_drives[scsi_id][scsi_lun];
			break;
		case SCSI_ZIP:
			id = scsi_zip_drives[scsi_id][scsi_lun];
			break;
		default:
			id = 0;
			return;
	}

	/* Call the second phase. */
	scsi_device_target_phase_callback(lun_type, id);
	SCSIDevices[scsi_id][scsi_lun].Status = scsi_device_target_err_stat_to_scsi(lun_type, id);
	/* Command second phase complete - call the callback to complete the command. */
	scsi_device_target_phase_callback(lun_type, id);
}

int32_t *scsi_device_get_buf_len(uint8_t scsi_id, uint8_t scsi_lun)
{
	return &SCSIDevices[scsi_id][scsi_lun].BufferLength;
}
