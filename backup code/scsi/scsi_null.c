/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of SCSI null device, used for invalid LUN's where
 *		LUN 0 is valid.
 *
 * Version:	@(#)scsi_null.c	1.0.0	2018/06/12
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2017,2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../device.h"
#include "../nvr.h"
#include "../disk/hdd.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../plat.h"
#include "../ui.h"
#include "scsi.h"
#include "../cdrom/cdrom.h"
#include "scsi_device.h"


/* Bits of 'status' */
#define ERR_STAT		0x01
#define DRQ_STAT		0x08 /* Data request */
#define DSC_STAT                0x10
#define SERVICE_STAT            0x10
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits of 'error' */
#define ABRT_ERR		0x04 /* Command aborted */
#define MCR_ERR			0x08 /* Media change request */

#define MAX_BLOCKS_AT_ONCE	340


#define scsi_null_sense_key	scsi_null_device_sense[2]
#define scsi_null_asc		scsi_null_device_sense[12]
#define scsi_null_ascq		scsi_null_device_sense[13]


static uint8_t status, phase, packet_status, error, command, packet_len, sense_desc;
static int64_t callback;
static uint8_t null_id, null_lun;
static uint8_t *temp_buffer;



#ifdef ENABLE_SCSI_NULL_LOG
int scsi_null_do_log = ENABLE_SCSI_NULL_LOG;
#endif


static void
scsi_null_log(const char *fmt, ...)
{
#ifdef ENABLE_SCSI_NULL_LOG
    va_list ap;

    if (scsi_null_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int
scsi_null_err_stat_to_scsi(void)
{
    if (status & ERR_STAT)
	return SCSI_STATUS_CHECK_CONDITION;
    else
	return SCSI_STATUS_OK;
}


static void
scsi_null_command_common(void)
{
    status = BUSY_STAT;
    phase = 1;
    if (packet_status == CDROM_PHASE_COMPLETE) {
	scsi_null_callback();
	callback = 0LL;
    } else
	callback = -1LL;	/* Speed depends on SCSI controller */
}


static void
scsi_null_command_complete(void)
{
    packet_status = CDROM_PHASE_COMPLETE;
    scsi_null_command_common();
}


static void
scsi_null_command_read_dma(void)
{
    packet_status = CDROM_PHASE_DATA_IN_DMA;
    scsi_null_command_common();
}


static void
scsi_null_data_command_finish(int len, int block_len, int alloc_len, int direction)
{
    if (alloc_len >= 0) {
	if (alloc_len < len)
		len = alloc_len;
    }
    if (len == 0)
	scsi_null_command_complete();
    else {
	if (direction == 0)
		scsi_null_command_read_dma();
	else
		fatal("SCSI NULL device write command\n");
    }
}


static void
scsi_null_set_phase(uint8_t phase)
{
    SCSIDevices[null_id][null_lun].Phase = phase;
}


static void
scsi_null_cmd_error(void)
{
    scsi_null_set_phase(SCSI_PHASE_STATUS);
    error = ((scsi_null_sense_key & 0xf) << 4) | ABRT_ERR;
    status = READY_STAT | ERR_STAT;
    phase = 3;
    packet_status = 0x80;
    callback = 50 * SCSI_TIME;
    scsi_null_log("SCSI NULL: ERROR: %02X/%02X/%02X\n", scsi_null_sense_key, scsi_null_asc, scsi_null_ascq);
}


static void
scsi_null_invalid_lun(void)
{
    scsi_null_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_null_asc = ASC_INV_LUN;
    scsi_null_ascq = 0;
    scsi_null_set_phase(SCSI_PHASE_STATUS);
    scsi_null_cmd_error();
}


static void
scsi_null_invalid_field(void)
{
    scsi_null_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_null_asc = ASC_INV_FIELD_IN_CMD_PACKET;
    scsi_null_ascq = 0;
    scsi_null_cmd_error();
    status = 0x53;
}


void
scsi_null_request_sense(uint8_t *buffer, uint8_t alloc_length, int desc)
{				
    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
	memset(buffer, 0, alloc_length);
	if (!desc)
		if (alloc_length <= 18)
			memcpy(buffer, scsi_null_device_sense, alloc_length);
		else {
			memset(buffer, 0x00, alloc_length);
			memcpy(buffer, scsi_null_device_sense, 18);
		}
	else {
		buffer[1] = scsi_null_sense_key;
		buffer[2] = scsi_null_asc;
		buffer[3] = scsi_null_ascq;
	}
    } else
	return;

    buffer[0] = 0x70;

    scsi_null_log("SCSI NULL: Reporting sense: %02X %02X %02X\n", buffer[2], buffer[12], buffer[13]);

    /* Clear the sense stuff as per the spec. */
    scsi_null_sense_key = scsi_null_asc = scsi_null_ascq = 0x00;
}


void
scsi_null_request_sense_for_scsi(uint8_t *buffer, uint8_t alloc_length)
{
    scsi_null_request_sense(buffer, alloc_length, 0);
}


void
scsi_null_command(uint8_t *cdb)
{
    int32_t *BufLen;
    uint32_t len;
    int max_len;
    unsigned idx = 0;
    unsigned size_idx, preamble_len;

    BufLen = &SCSIDevices[null_id][null_lun].BufferLength;

    status &= ~ERR_STAT;
    packet_len = 0;

    scsi_null_set_phase(SCSI_PHASE_STATUS);

    command = cdb[0];
    switch (cdb[0]) {
	case GPCMD_REQUEST_SENSE:
		/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
		   should forget about the not ready, and report unit attention straight away. */
		sense_desc = cdb [1];

		if ((*BufLen == -1) || (cdb[4] < *BufLen))
			*BufLen = cdb[4];

		if (*BufLen < cdb[4])
			cdb[4] = *BufLen;

		len = (cdb[1] & 1) ? 8 : 18;

		scsi_null_set_phase(SCSI_PHASE_DATA_IN);
		scsi_null_data_command_finish(len, len, cdb[4], 0);
		break;

#if 0
	case GPCMD_INQUIRY:
		max_len = cdb[3];
		max_len <<= 8;
		max_len |= cdb[4];

		if ((!max_len) || (*BufLen == 0)) {
			scsi_null_set_phase(SCSI_PHASE_STATUS);
			packet_status = CDROM_PHASE_COMPLETE;
			callback = 20 * SCSI_TIME;
			break;
		}			

		if (cdb[1] & 1) {
			scsi_null_invalid_field();
			return;
		} else {
			temp_buffer = malloc(1024);

			preamble_len = 5;
			size_idx = 4;

			memset(temp_buffer, 0, 8);
			temp_buffer[0] = (3 << 5); /*Invalid*/
			temp_buffer[1] = 0; /*Fixed*/
			temp_buffer[2] = 0x02; /*SCSI-2 compliant*/
			temp_buffer[3] = 0x02;
			temp_buffer[4] = 31;
			temp_buffer[6] = 1;	/* 16-bit transfers supported */
			temp_buffer[7] = 0x20;	/* Wide bus supported */

			ide_padstr8(temp_buffer + 8, 8, EMU_NAME); /* Vendor */
			ide_padstr8(temp_buffer + 16, 16, "INVALID"); /* Product */
			ide_padstr8(temp_buffer + 32, 4, EMU_VERSION); /* Revision */
			idx = 36;

			if (max_len == 96) {
				temp_buffer[4] = 91;
				idx = 96;
			}
		}

		temp_buffer[size_idx] = idx - preamble_len;
		len=idx;

		if (len > max_len)
			len = max_len;

		if ((*BufLen == -1) || (len < *BufLen))
			*BufLen = len;

		if (len > *BufLen)
			len = *BufLen;

		scsi_null_set_phase(SCSI_PHASE_DATA_IN);
		scsi_null_data_command_finish(len, len, max_len, 0);
		break;
#endif

	default:
		scsi_null_invalid_lun();
		break;
    }
}


static void
scsi_null_phase_data_in(void)
{
    uint8_t *hdbufferb = SCSIDevices[null_id][null_lun].CmdBuffer;
    int32_t *BufLen = &SCSIDevices[null_id][null_lun].BufferLength;

    if (!*BufLen) {
	scsi_null_log("scsi_null_phase_data_in(): Buffer length is 0\n");
	scsi_null_set_phase(SCSI_PHASE_STATUS);

	return;
    }

    switch (command) {
	case GPCMD_REQUEST_SENSE:
		scsi_null_log("SCSI NULL: %08X, %08X\n", hdbufferb, *BufLen);
		scsi_null_request_sense(hdbufferb, *BufLen, sense_desc & 1);
		break;
#if 0
	case GPCMD_INQUIRY:
		memcpy(hdbufferb, temp_buffer, *BufLen);
		free(temp_buffer);
		temp_buffer = NULL;
		scsi_null_log("%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			    hdbufferb[0], hdbufferb[1], hdbufferb[2], hdbufferb[3], hdbufferb[4], hdbufferb[5], hdbufferb[6], hdbufferb[7],
			    hdbufferb[8], hdbufferb[9], hdbufferb[10], hdbufferb[11], hdbufferb[12], hdbufferb[13], hdbufferb[14], hdbufferb[15]);
		break;
#endif
	default:
		fatal("SCSI NULL: Bad Command for phase 2 (%02X)\n", command);
		break;
    }

    scsi_null_set_phase(SCSI_PHASE_STATUS);
}


void
scsi_null_callback(void)
{
    switch(packet_status) {
	case CDROM_PHASE_IDLE:
		scsi_null_log("SCSI NULL: PHASE_IDLE\n");
		phase = 1;
		status = READY_STAT | DRQ_STAT | (status & ERR_STAT);
		return;
	case CDROM_PHASE_COMPLETE:
		scsi_null_log("SCSI NULL: PHASE_COMPLETE\n");
		status = READY_STAT;
		phase = 3;
		packet_status = 0xFF;
		return;
	case CDROM_PHASE_DATA_IN_DMA:
		scsi_null_log("SCSI NULL: PHASE_DATA_IN_DMA\n");
		scsi_null_phase_data_in();
		packet_status = CDROM_PHASE_COMPLETE;
		status = READY_STAT;
		phase = 3;
		return;
	case CDROM_PHASE_ERROR:
		scsi_null_log("SCSI NULL: PHASE_ERROR\n");
		status = READY_STAT | ERR_STAT;
		phase = 3;
		return;
    }
}


void
scsi_null_set_location(uint8_t id, uint8_t lun)
{
    null_id = id;
    null_lun = lun;
}
