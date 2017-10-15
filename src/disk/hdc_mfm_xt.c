/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Driver for the IBM PC-XT Fixed Disk controller.
 *
 *		The original controller shipped by IBM was made by Xebec, and
 *		several variations had been made:
 *
 *		#1	Original, single drive (ST412), 10MB, 2 heads.
 *		#2	Update, single drive (ST412) but with option for a
 *			switch block that can be used to 'set' the actual
 *			drive type. Four switches are define, where switches
 *			1 and 2 define drive0, and switches 3 and 4 drive1.
 *
 *			  0  ON  ON	306  2  0
 *			  1  ON  OFF	375  8  0
 *			  2  OFF ON	306  6  256
 *			  3  OFF OFF	306  4  0
 *
 *			The latter option is the default, in use on boards
 *			without the switch block option.
 *
 *		#3	Another updated board, mostly to accomodate the new
 *			20MB disk now being shipped. The controller can have
 *			up to 2 drives, the type of which is set using the
 *			switch block:
 *
 *			     SW1 SW2	CYLS HD SPT WPC
 *			  0  ON  ON	306  4  17  0
 *			  1  ON  OFF	612  4  17  0	(type 16)
 *			  2  OFF ON    	615  4  17  300	(Seagate ST-225, 2)
 *			  3  OFF OFF	306  8  17  128 (IBM WD25, 13)
 *
 *		Examples of #3 are IBM/Xebec, WD10004A-WX1 and ST11R.
 *
 *		Since all controllers (including the ones made by DTC) use
 *		(mostly) the same API, we keep them all in this module.
 *
 * Version:	@(#)hdd_mfm_xt.c	1.0.9	2017/10/11
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2017 Fred N. van Kempen.
 */
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../device.h"
#include "../dma.h"
#include "../io.h"
#include "../mem.h"
#include "../pic.h"
#include "../rom.h"
#include "../timer.h"
#include "../ui.h"
#include "hdc.h"
#include "hdd.h"


#define MFM_TIME	(2000LL*TIMER_USEC)
#define XEBEC_BIOS_FILE	L"roms/hdd/mfm_xebec/ibm_xebec_62x0822_1985.bin"
#define DTC_BIOS_FILE	L"roms/hdd/mfm_xebec/dtc_cxd21a.bin"


enum {
    STATE_IDLE,
    STATE_RECEIVE_COMMAND,
    STATE_START_COMMAND,
    STATE_RECEIVE_DATA,
    STATE_RECEIVED_DATA,
    STATE_SEND_DATA,
    STATE_SENT_DATA,
    STATE_COMPLETION_BYTE,
    STATE_DUNNO
};


typedef struct {
    int spt, hpc;
    int tracks;
    int cfg_spt;
    int cfg_hpc;
    int cfg_cyl;
    int current_cylinder;
    int present;
    int hdd_num;
} drive_t;

typedef struct {
    rom_t bios_rom;
    int64_t callback;
    int state;
    uint8_t status;
    uint8_t command[6];
    int command_pos;
    uint8_t data[512];
    int data_pos, data_len;
    uint8_t sector_buf[512];
    uint8_t irq_dma_mask;
    uint8_t completion_byte;
    uint8_t error;
    int drive_sel;
    drive_t drives[2];
    int sector, head, cylinder;
    int sector_count;
    uint8_t switches;
} mfm_t;

#define STAT_IRQ 0x20
#define STAT_DRQ 0x10
#define STAT_BSY 0x08
#define STAT_CD  0x04
#define STAT_IO  0x02
#define STAT_REQ 0x01

#define IRQ_ENA 0x02
#define DMA_ENA 0x01

#define CMD_TEST_DRIVE_READY      0x00
#define CMD_RECALIBRATE           0x01
#define CMD_READ_STATUS           0x03
#define CMD_VERIFY_SECTORS        0x05
#define CMD_FORMAT_TRACK          0x06
#define CMD_READ_SECTORS          0x08
#define CMD_WRITE_SECTORS         0x0a
#define CMD_SEEK                  0x0b
#define CMD_INIT_DRIVE_PARAMS     0x0c
#define CMD_WRITE_SECTOR_BUFFER   0x0f
#define CMD_BUFFER_DIAGNOSTIC     0xe0
#define CMD_CONTROLLER_DIAGNOSTIC 0xe4
#define CMD_DTC_GET_DRIVE_PARAMS  0xfb
#define CMD_DTC_SET_STEP_RATE     0xfc
#define CMD_DTC_SET_GEOMETRY      0xfe
#define CMD_DTC_GET_GEOMETRY      0xff

#define ERR_NOT_READY              0x04
#define ERR_SEEK_ERROR             0x15
#define ERR_ILLEGAL_SECTOR_ADDRESS 0x21


static uint8_t
mfm_read(uint16_t port, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    uint8_t temp = 0xff;

    switch (port) {
	case 0x320: /*Read data*/
		mfm->status &= ~STAT_IRQ;
		switch (mfm->state) {
			case STATE_COMPLETION_BYTE:
				if ((mfm->status & 0xf) != (STAT_CD | STAT_IO | STAT_REQ | STAT_BSY))
					fatal("Read data STATE_COMPLETION_BYTE, status=%02x\n", mfm->status);

				temp = mfm->completion_byte;
				mfm->status = 0;
				mfm->state = STATE_IDLE;
				break;
			
			case STATE_SEND_DATA:
				if ((mfm->status & 0xf) != (STAT_IO | STAT_REQ | STAT_BSY))
					fatal("Read data STATE_COMPLETION_BYTE, status=%02x\n", mfm->status);
				if (mfm->data_pos >= mfm->data_len)
					fatal("Data write with full data!\n");
				temp = mfm->data[mfm->data_pos++];
				if (mfm->data_pos == mfm->data_len) {
					mfm->status = STAT_BSY;
					mfm->state = STATE_SENT_DATA;
					mfm->callback = MFM_TIME;
				}
				break;

			default:
				fatal("Read data register - %i, %02x\n", mfm->state, mfm->status);
		}
		break;

	case 0x321: /*Read status*/
		temp = mfm->status;
		break;

	case 0x322: /*Read option jumpers*/
		temp = mfm->switches;
		break;
    }

    return(temp);
}


static void
mfm_write(uint16_t port, uint8_t val, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;

    switch (port) {
	case 0x320: /*Write data*/
		switch (mfm->state) {
			case STATE_RECEIVE_COMMAND:
				if ((mfm->status & 0xf) != (STAT_BSY | STAT_CD | STAT_REQ))
					fatal("Bad write data state - STATE_START_COMMAND, status=%02x\n", mfm->status);
				if (mfm->command_pos >= 6)
					fatal("Command write with full command!\n");
				/*Command data*/
				mfm->command[mfm->command_pos++] = val;
				if (mfm->command_pos == 6) {
					mfm->status = STAT_BSY;
					mfm->state = STATE_START_COMMAND;
					mfm->callback = MFM_TIME;
				}
				break;

				case STATE_RECEIVE_DATA:
				if ((mfm->status & 0xf) != (STAT_BSY | STAT_REQ))
					fatal("Bad write data state - STATE_RECEIVE_DATA, status=%02x\n", mfm->status);
				if (mfm->data_pos >= mfm->data_len)
					fatal("Data write with full data!\n");
				/*Command data*/
				mfm->data[mfm->data_pos++] = val;
				if (mfm->data_pos == mfm->data_len) {
					mfm->status = STAT_BSY;
					mfm->state = STATE_RECEIVED_DATA;
					mfm->callback = MFM_TIME;
				}
				break;

			default:
				fatal("Write data unknown state - %i %02x\n", mfm->state, mfm->status);
		}
		break;

	case 0x321: /*Controller reset*/
		mfm->status = 0;
		break;

	case 0x322: /*Generate controller-select-pulse*/
		mfm->status = STAT_BSY | STAT_CD | STAT_REQ;
		mfm->command_pos = 0;
		mfm->state = STATE_RECEIVE_COMMAND;
		break;

	case 0x323: /*DMA/IRQ mask register*/
		mfm->irq_dma_mask = val;
		break;
    }
}


static void mfm_complete(mfm_t *mfm)
{
    mfm->status = STAT_REQ | STAT_CD | STAT_IO | STAT_BSY;
    mfm->state = STATE_COMPLETION_BYTE;

    if (mfm->irq_dma_mask & IRQ_ENA) {
	mfm->status |= STAT_IRQ;
	picint(1 << 5);
    }
}


static void
mfm_error(mfm_t *mfm, uint8_t error)
{
    mfm->completion_byte |= 0x02;
    mfm->error = error;

    pclog("mfm_error - %02x\n", mfm->error);
}


static int
get_sector(mfm_t *mfm, off64_t *addr)
{
    drive_t *drive = &mfm->drives[mfm->drive_sel];
    int heads = drive->cfg_hpc;

    if (drive->current_cylinder != mfm->cylinder) {
	pclog("mfm_get_sector: wrong cylinder\n");
	mfm->error = ERR_ILLEGAL_SECTOR_ADDRESS;
	return(1);
    }
    if (mfm->head > heads) {
	pclog("mfm_get_sector: past end of configured heads\n");
	mfm->error = ERR_ILLEGAL_SECTOR_ADDRESS;
	return(1);
    }
    if (mfm->head > drive->hpc) {
	pclog("mfm_get_sector: past end of heads\n");
	mfm->error = ERR_ILLEGAL_SECTOR_ADDRESS;
	return(1);
    }
    if (mfm->sector >= 17) {
	pclog("mfm_get_sector: past end of sectors\n");
	mfm->error = ERR_ILLEGAL_SECTOR_ADDRESS;
	return(1);
    }

    *addr = ((((off64_t) mfm->cylinder * heads) + mfm->head) *
			  17) + mfm->sector;
	
    return(0);
}


static void
next_sector(mfm_t *mfm)
{
    drive_t *drive = &mfm->drives[mfm->drive_sel];
	
    mfm->sector++;
    if (mfm->sector >= 17) {
	mfm->sector = 0;
	mfm->head++;
	if (mfm->head >= drive->cfg_hpc) {
		mfm->head = 0;
		mfm->cylinder++;
		drive->current_cylinder++;
		if (drive->current_cylinder >= drive->cfg_cyl)
			drive->current_cylinder = drive->cfg_cyl-1;
	}
    }
}


static void
mfm_callback(void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    drive_t *drive;
    off64_t addr;

    mfm->callback = 0LL;

    mfm->drive_sel = (mfm->command[1] & 0x20) ? 1 : 0;
    mfm->completion_byte = mfm->drive_sel & 0x20;
    drive = &mfm->drives[mfm->drive_sel];

    switch (mfm->command[0]) {
	case CMD_TEST_DRIVE_READY:
		if (!drive->present)
			mfm_error(mfm, ERR_NOT_READY);
		mfm_complete(mfm);
		break;

	case CMD_RECALIBRATE:
		if (!drive->present)
			mfm_error(mfm, ERR_NOT_READY);
		else {
			mfm->cylinder = 0;
			drive->current_cylinder = 0;
		}
		mfm_complete(mfm);
		break;

	case CMD_READ_STATUS:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->state = STATE_SEND_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 4;
				mfm->status = STAT_BSY | STAT_IO | STAT_REQ;
				mfm->data[0] = mfm->error;
				mfm->data[1] = mfm->drive_sel ? 0x20 : 0;
				mfm->data[2] = mfm->data[3] = 0;
				mfm->error = 0;
				break;

			case STATE_SENT_DATA:
				mfm_complete(mfm);
				break;
		}
		break;

	case CMD_VERIFY_SECTORS:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->cylinder = mfm->command[3] | ((mfm->command[2] & 0xc0) << 2);
				drive->current_cylinder = (mfm->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : mfm->cylinder;
				mfm->head = mfm->command[1] & 0x1f;
				mfm->sector = mfm->command[2] & 0x1f;
				mfm->sector_count = mfm->command[4];
				do {
					if (get_sector(mfm, &addr)) {
						pclog("get_sector failed\n");
						mfm_error(mfm, mfm->error);
						mfm_complete(mfm);
						return;
					}

					next_sector(mfm);

					mfm->sector_count = (mfm->sector_count-1) & 0xff;
				} while (mfm->sector_count);

				mfm_complete(mfm);

				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);
				break;

			default:
				fatal("CMD_VERIFY_SECTORS: bad state %i\n", mfm->state);
		}
		break;

	case CMD_FORMAT_TRACK:
		mfm->cylinder = mfm->command[3] | ((mfm->command[2] & 0xc0) << 2);
		drive->current_cylinder = (mfm->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : mfm->cylinder;
		mfm->head = mfm->command[1] & 0x1f;

		if (get_sector(mfm, &addr)) {
			pclog("get_sector failed\n");
			mfm_error(mfm, mfm->error);
			mfm_complete(mfm);
			return;
		}

		hdd_image_zero(drive->hdd_num, addr, 17);
				
		mfm_complete(mfm);
		break;			       

	case CMD_READ_SECTORS:		
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->cylinder = mfm->command[3] | ((mfm->command[2] & 0xc0) << 2);
				drive->current_cylinder = (mfm->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : mfm->cylinder;
				mfm->head = mfm->command[1] & 0x1f;
				mfm->sector = mfm->command[2] & 0x1f;
				mfm->sector_count = mfm->command[4];
				mfm->state = STATE_SEND_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 512;

				if (get_sector(mfm, &addr)) {
					mfm_error(mfm, mfm->error);
					mfm_complete(mfm);
					return;
				}

				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *) mfm->sector_buf);
				ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);

				if (mfm->irq_dma_mask & DMA_ENA)
					mfm->callback = MFM_TIME;
				else {
					mfm->status = STAT_BSY | STAT_IO | STAT_REQ;
					memcpy(mfm->data, mfm->sector_buf, 512);
				}
				break;

			case STATE_SEND_DATA:
				mfm->status = STAT_BSY;
				if (mfm->irq_dma_mask & DMA_ENA) {
					for (; mfm->data_pos < 512; mfm->data_pos++) {
						int val = dma_channel_write(3, mfm->sector_buf[mfm->data_pos]);

						if (val == DMA_NODATA) {
							pclog("CMD_READ_SECTORS out of data!\n");
							mfm->status = STAT_BSY | STAT_CD | STAT_IO | STAT_REQ;
							mfm->callback = MFM_TIME;
							return;
						}
					}
					mfm->state = STATE_SENT_DATA;
					mfm->callback = MFM_TIME;
				} else
					fatal("Read sectors no DMA! - shouldn't get here\n");
				break;

			case STATE_SENT_DATA:
				next_sector(mfm);

				mfm->data_pos = 0;

				mfm->sector_count = (mfm->sector_count-1) & 0xff;

				if (mfm->sector_count) {
					if (get_sector(mfm, &addr)) {
						mfm_error(mfm, mfm->error);
						mfm_complete(mfm);
						return;
					}

					hdd_image_read(drive->hdd_num, addr, 1,
						(uint8_t *) mfm->sector_buf);
					ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);

					mfm->state = STATE_SEND_DATA;

					if (mfm->irq_dma_mask & DMA_ENA)
						mfm->callback = MFM_TIME;
					else {
						mfm->status = STAT_BSY | STAT_IO | STAT_REQ;
						memcpy(mfm->data, mfm->sector_buf, 512);
					}
				} else {
					mfm_complete(mfm);
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
				}
				break;

			default:
				fatal("CMD_READ_SECTORS: bad state %i\n", mfm->state);
		}
		break;

	case CMD_WRITE_SECTORS:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->cylinder = mfm->command[3] | ((mfm->command[2] & 0xc0) << 2);
				drive->current_cylinder = (mfm->cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : mfm->cylinder;
				mfm->head = mfm->command[1] & 0x1f;
				mfm->sector = mfm->command[2] & 0x1f;
				mfm->sector_count = mfm->command[4];
				mfm->state = STATE_RECEIVE_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 512;
				if (mfm->irq_dma_mask & DMA_ENA)
					mfm->callback = MFM_TIME;
				else
					mfm->status = STAT_BSY | STAT_REQ;
				break;

			case STATE_RECEIVE_DATA:
				mfm->status = STAT_BSY;
				if (mfm->irq_dma_mask & DMA_ENA) {
					for (; mfm->data_pos < 512; mfm->data_pos++) {
						int val = dma_channel_read(3);

						if (val == DMA_NODATA) {
							pclog("CMD_WRITE_SECTORS out of data!\n");
							mfm->status = STAT_BSY | STAT_CD | STAT_IO | STAT_REQ;
							mfm->callback = MFM_TIME;
							return;
						}

						mfm->sector_buf[mfm->data_pos] = val & 0xff;
					}

					mfm->state = STATE_RECEIVED_DATA;
					mfm->callback = MFM_TIME;
				} else
					fatal("Write sectors no DMA! - should never get here\n");
				break;

			case STATE_RECEIVED_DATA:
				if (! (mfm->irq_dma_mask & DMA_ENA))
					memcpy(mfm->sector_buf, mfm->data, 512);

				if (get_sector(mfm, &addr))
				{
					mfm_error(mfm, mfm->error);
					mfm_complete(mfm);
					return;
				}

				hdd_image_write(drive->hdd_num, addr, 1,
						(uint8_t *) mfm->sector_buf);
				ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);

				next_sector(mfm);
				mfm->data_pos = 0;
				mfm->sector_count = (mfm->sector_count-1) & 0xff;

				if (mfm->sector_count) {
					mfm->state = STATE_RECEIVE_DATA;
					if (mfm->irq_dma_mask & DMA_ENA)
						mfm->callback = MFM_TIME;
					else
						mfm->status = STAT_BSY | STAT_REQ;
				} else
					mfm_complete(mfm);
				break;

			default:
				fatal("CMD_WRITE_SECTORS: bad state %i\n", mfm->state);
		}
		break;

	case CMD_SEEK:
		if (! drive->present)
			mfm_error(mfm, ERR_NOT_READY);
		else {
			int cylinder = mfm->command[3] | ((mfm->command[2] & 0xc0) << 2);

			drive->current_cylinder = (cylinder >= drive->cfg_cyl) ? drive->cfg_cyl-1 : cylinder;

			if (cylinder != drive->current_cylinder)
				mfm_error(mfm, ERR_SEEK_ERROR);
		}
		mfm_complete(mfm);
		break;

	case CMD_INIT_DRIVE_PARAMS:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->state = STATE_RECEIVE_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 8;
				mfm->status = STAT_BSY | STAT_REQ;
				break;

			case STATE_RECEIVED_DATA:
				drive->cfg_cyl = mfm->data[1] | (mfm->data[0] << 8);
				drive->cfg_hpc = mfm->data[2];
				pclog("Drive %i: cylinders=%i, heads=%i\n", mfm->drive_sel, drive->cfg_cyl, drive->cfg_hpc);
				mfm_complete(mfm);
				break;

			default:
				fatal("CMD_INIT_DRIVE_PARAMS bad state %i\n", mfm->state);
		}
		break;

	case CMD_WRITE_SECTOR_BUFFER:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->state = STATE_RECEIVE_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 512;
				if (mfm->irq_dma_mask & DMA_ENA)
					mfm->callback = MFM_TIME;
				else
					mfm->status = STAT_BSY | STAT_REQ;
				break;

			case STATE_RECEIVE_DATA:
				if (mfm->irq_dma_mask & DMA_ENA) {
					mfm->status = STAT_BSY;

					for (; mfm->data_pos < 512; mfm->data_pos++) {
						int val = dma_channel_read(3);

						if (val == DMA_NODATA) {
							pclog("CMD_WRITE_SECTOR_BUFFER out of data!\n");
							mfm->status = STAT_BSY | STAT_CD | STAT_IO | STAT_REQ;
							mfm->callback = MFM_TIME;
							return;
						}
					
						mfm->data[mfm->data_pos] = val & 0xff;
					}

					mfm->state = STATE_RECEIVED_DATA;
					mfm->callback = MFM_TIME;
				} else
					fatal("CMD_WRITE_SECTOR_BUFFER - should never get here!\n");
				break;

			case STATE_RECEIVED_DATA:
				memcpy(mfm->sector_buf, mfm->data, 512);
				mfm_complete(mfm);
				break;

			default:
				fatal("CMD_WRITE_SECTOR_BUFFER bad state %i\n", mfm->state);
		}
		break;

	case CMD_BUFFER_DIAGNOSTIC:
	case CMD_CONTROLLER_DIAGNOSTIC:
		mfm_complete(mfm);
		break;

	case 0xfa:
		mfm_complete(mfm);
		break;

	case CMD_DTC_SET_STEP_RATE:
		mfm_complete(mfm);
		break;

	case CMD_DTC_GET_DRIVE_PARAMS:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->state = STATE_SEND_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 4;
				mfm->status = STAT_BSY | STAT_IO | STAT_REQ;
				memset(mfm->data, 0, 4);
				mfm->data[0] = drive->tracks & 0xff;
				mfm->data[1] = 17 | ((drive->tracks >> 2) & 0xc0);
				mfm->data[2] = drive->hpc-1;
				pclog("Get drive params %02x %02x %02x %i\n", mfm->data[0], mfm->data[1], mfm->data[2], drive->tracks);
				break;

			case STATE_SENT_DATA:
				mfm_complete(mfm);
				break;

			default:
				fatal("CMD_INIT_DRIVE_PARAMS bad state %i\n", mfm->state);
		}
		break;

	case CMD_DTC_GET_GEOMETRY:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->state = STATE_SEND_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 16;
				mfm->status = STAT_BSY | STAT_IO | STAT_REQ;
				memset(mfm->data, 0, 16);
				mfm->data[0x4] = drive->tracks & 0xff;
				mfm->data[0x5] = (drive->tracks >> 8) & 0xff;
				mfm->data[0xa] = drive->hpc;
				break;

			case STATE_SENT_DATA:
				mfm_complete(mfm);
				break;
		}
		break;

	case CMD_DTC_SET_GEOMETRY:
		switch (mfm->state) {
			case STATE_START_COMMAND:
				mfm->state = STATE_RECEIVE_DATA;
				mfm->data_pos = 0;
				mfm->data_len = 16;
				mfm->status = STAT_BSY | STAT_REQ;
				break;

			case STATE_RECEIVED_DATA:
				/*Bit of a cheat here - we always report the actual geometry of the drive in use*/
				mfm_complete(mfm);
			break;
		}
		break;

	default:
		fatal("Unknown Xebec command - %02x %02x %02x %02x %02x %02x\n",
			mfm->command[0], mfm->command[1],
			mfm->command[2], mfm->command[3],
			mfm->command[4], mfm->command[5]);
    }
}


static void
loadhd(mfm_t *mfm, int c, int d, const wchar_t *fn)
{
    drive_t *drive = &mfm->drives[d];

    if (! hdd_image_load(d)) {
	drive->present = 0;
	return;
    }
	
    drive->spt = hdd[c].spt;
    drive->hpc = hdd[c].hpc;
    drive->tracks = hdd[c].tracks;
    drive->hdd_num = c;
    drive->present = 1;
}


static struct {
    int tracks, hpc;
} hd_types[4] = {
    { 306, 4 },	/* Type 0 */
    { 612, 4 }, /* Type 16 */
    { 615, 4 }, /* Type 2 */
    { 306, 8 }  /* Type 13 */
};


static void
mfm_set_switches(mfm_t *mfm)
{
    int c, d;
	
    mfm->switches = 0;
	
    for (d=0; d<2; d++) {
	drive_t *drive = &mfm->drives[d];

	if (! drive->present) continue;

	for (c=0; c<4; c++) {
		if (drive->spt == 17 &&
		    drive->hpc == hd_types[c].hpc &&
		    drive->tracks == hd_types[c].tracks) {
			mfm->switches |= (c << (d ? 0 : 2));
			break;
		}
	}

	if (c == 4)
	pclog("WARNING: Drive %c: has format not supported by Fixed Disk Adapter", d ? 'D' : 'C');
    }
}


static void *
xebec_init(device_t *info)
{
    int i, c = 0;

    mfm_t *xebec = malloc(sizeof(mfm_t));
    memset(xebec, 0x00, sizeof(mfm_t));

    for (i=0; i<HDD_NUM; i++) {
	if ((hdd[i].bus == HDD_BUS_MFM) && (hdd[i].mfm_channel < MFM_NUM)) {
		loadhd(xebec, i, hdd[i].mfm_channel, hdd[i].fn);

		if (++c > MFM_NUM) break;
	}
    }

    mfm_set_switches(xebec);

    rom_init(&xebec->bios_rom, XEBEC_BIOS_FILE,
	     0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    io_sethandler(0x0320, 4,
		  mfm_read, NULL, NULL, mfm_write, NULL, NULL, xebec);

    timer_add(mfm_callback, &xebec->callback, &xebec->callback, xebec);
	
    return(xebec);
}


static void
mfm_close(void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    int d;

    for (d=0; d<2; d++) {
	drive_t *drive = &mfm->drives[d];

	hdd_image_close(drive->hdd_num);
    }
	
    free(mfm);
}


static int
xebec_available(void)
{
	return(rom_present(XEBEC_BIOS_FILE));
}


device_t mfm_xt_xebec_device = {
    "IBM PC Fixed Disk Adapter",
    DEVICE_ISA,
    0,
    xebec_init, mfm_close, NULL,
    xebec_available, NULL, NULL, NULL,
    NULL
};


static void *
dtc5150x_init(device_t *info)
{
    int i, c = 0;

    mfm_t *dtc = malloc(sizeof(mfm_t));
    memset(dtc, 0x00, sizeof(mfm_t));

    for (i=0; i<HDD_NUM; i++) {
	if ((hdd[i].bus == HDD_BUS_MFM) && (hdd[i].mfm_channel < MFM_NUM)) {
		loadhd(dtc, i, hdd[i].mfm_channel, hdd[i].fn);

		if (++c > MFM_NUM) break;
	}
    }

    dtc->switches = 0xff;

    dtc->drives[0].cfg_cyl = dtc->drives[0].tracks;
    dtc->drives[0].cfg_hpc = dtc->drives[0].hpc;
    dtc->drives[1].cfg_cyl = dtc->drives[1].tracks;
    dtc->drives[1].cfg_hpc = dtc->drives[1].hpc;

    rom_init(&dtc->bios_rom, DTC_BIOS_FILE,
	     0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
		
    io_sethandler(0x0320, 4,
		  mfm_read, NULL, NULL, mfm_write, NULL, NULL, dtc);

    timer_add(mfm_callback, &dtc->callback, &dtc->callback, dtc);

    return(dtc);
}


static int
dtc5150x_available(void)
{
    return(rom_present(DTC_BIOS_FILE));
}


device_t mfm_xt_dtc5150x_device = {
    "DTC 5150X",
    DEVICE_ISA,
    0,
    dtc5150x_init, mfm_close, NULL,
    dtc5150x_available, NULL, NULL, NULL,
    NULL
};
