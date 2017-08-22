#include "ibm.h"
#include "scsi.h"
#include "scsi_disk.h"
#include "cdrom.h"


static uint8_t scsi_null_device_sense[14] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0 };


static void scsi_device_target_command(int lun_type, uint8_t id, uint8_t *cdb)
{
	if (lun_type == SCSI_DISK)
	{
		SCSIPhase = SCSI_PHASE_COMMAND;
		scsi_hd_command(id, cdb);
		SCSIStatus = scsi_hd_err_stat_to_scsi(id);
	}
	else if (lun_type == SCSI_CDROM)
	{
		SCSIPhase = SCSI_PHASE_COMMAND;
		cdrom_command(id, cdb);
		SCSIStatus = cdrom_CDROM_PHASE_to_scsi(id);
	}
	else
	{
		SCSIStatus = SCSI_STATUS_CHECK_CONDITION;
	}
}


static int scsi_device_target_phase_to_scsi(int lun_type, uint8_t id)
{
	if (lun_type == SCSI_DISK)
	{
		return scsi_hd_phase_to_scsi(id);
	}
	else if (lun_type == SCSI_CDROM)
	{
		return cdrom_atapi_phase_to_scsi(id);
	}
	else
	{
		return 0;
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
	else
	{
		return;
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
	default:
		return scsi_null_device_sense;
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
		*rmb = (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? 0x80 : 0x00;
		break;
	case SCSI_CDROM:
		*type = 0x05;
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
		id = scsi_hard_disks[scsi_id][scsi_lun];
		return cdrom_read_capacity(id, cdb, buffer, len);
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
	default:
		return 12;
    }
}


int scsi_device_block_shift(uint8_t scsi_id, uint8_t scsi_lun)
{
    uint8_t lun_type = SCSIDevices[scsi_id][scsi_lun].LunType;

    switch (lun_type)
    {
	case SCSI_CDROM:
		return 11;	/* 2048 bytes per block */
	default:
		return 9;	/* 512 bytes per block */
    }
}


void scsi_device_command(int cdb_len, uint8_t scsi_id, uint8_t scsi_lun, uint8_t *cdb)
{
    uint8_t phase = 0;
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
	default:
		id = 0;
		break;
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
    scsi_device_target_command(lun_type, id, cdb);
    if (SCSIStatus == SCSI_STATUS_OK) {
	phase = scsi_device_target_phase_to_scsi(lun_type, id);
	if (phase == 2) {
		/* Command completed - call the phase callback to complete the command. */
		scsi_device_target_phase_callback(lun_type, id);
	} else {
		/* Command first phase complete - call the callback to execute the second phase. */
		scsi_device_target_phase_callback(lun_type, id);
		SCSIStatus = scsi_device_target_err_stat_to_scsi(lun_type, id);
		/* Command second phase complete - call the callback to complete the command. */
		scsi_device_target_phase_callback(lun_type, id);
	}
    } else {
	/* Error (Check Condition) - call the phase callback to complete the command. */
	scsi_device_target_phase_callback(lun_type, id);
    }
}
