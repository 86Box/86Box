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
 *			drive type. Four switches are defined, where switches
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
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2008-2019 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/ui.h>
#include <86box/plat.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/hdc.h>
#include <86box/hdd.h>


#define XEBEC_BIOS_FILE		"roms/hdd/st506/ibm_xebec_62x0822_1985.bin"
#define DTC_BIOS_FILE		"roms/hdd/st506/dtc_cxd21a.bin"
#define ST11_BIOS_FILE_OLD	"roms/hdd/st506/st11_bios_vers_1.7.bin"
#define ST11_BIOS_FILE_NEW	"roms/hdd/st506/st11_bios_vers_2.0.bin"
#define WD1002A_WX1_BIOS_FILE	"roms/hdd/st506/wd1002a_wx1-62-000094-032.bin"
#define WD1004A_WX1_BIOS_FILE	"roms/hdd/st506/wd1002a_wx1-62-000094-032.bin"
/* SuperBIOS was for both the WX1 and 27X, users jumpers readout to determine
   if to use 26 sectors per track, 26 -> 17 sectors per track translation, or
   17 sectors per track. */
#define WD1002A_27X_BIOS_FILE	"roms/hdd/st506/wd1002a_27x-62-000094-032.bin"
#define WD1004_27X_BIOS_FILE	"roms/hdd/st506/western_digital_WD1004A-27X.bin"
#define WD1004A_27X_BIOS_FILE	"roms/hdd/st506/western_digital_WD1004A-27X.bin"


#define ST506_TIME		(250 * TIMER_USEC)
#define ST506_TIME_MS		(1000 * TIMER_USEC)

/* MFM and RLL use different sectors/track. */
#define SECTOR_SIZE		512
#define MFM_SECTORS		17
#define RLL_SECTORS		26


/* Status register. */
#define STAT_REQ		0x01		/* controller ready */
#define STAT_IO			0x02		/* input, data to host */
#define STAT_CD			0x04		/* command mode (else data) */
#define STAT_BSY		0x08		/* controller is busy */
#define STAT_DRQ		0x10		/* controller needs DMA */
#define STAT_IRQ		0x20		/* interrupt, we have info */

/* DMA/IRQ enable register. */
#define DMA_ENA			0x01		/* DMA operation enabled */
#define IRQ_ENA			0x02		/* IRQ operation enabled */

/* Error codes in sense report. */
#define ERR_BV			0x80
#define ERR_TYPE_MASK		0x30
#define ERR_TYPE_SHIFT		4
# define ERR_TYPE_DRIVE		0x00
# define ERR_TYPE_CONTROLLER	0x01
# define ERR_TYPE_COMMAND	0x02
# define ERR_TYPE_MISC		0x03

/* No, um, errors.. */
#define ERR_NONE		0x00

/* Group 0: drive errors. */
#define ERR_NO_SEEK		0x02		/* no seek_complete */
#define ERR_WR_FAULT		0x03		/* write fault */
#define ERR_NOT_READY		0x04		/* drive not ready */
#define ERR_NO_TRACK0		0x06		/* track 0 not found */
#define ERR_STILL_SEEKING	0x08		/* drive is still seeking */
#define ERR_NOT_AVAILABLE	0x09		/* drive not available */

/* Group 1: controller errors. */
#define ERR_ID_FAULT		0x10		/* could not read ID field */
#define ERR_UNC_ERR		0x11		/* uncorrectable data */
#define ERR_SECTOR_ADDR		0x12		/* sector address */
#define ERR_DATA_ADDR		0x13		/* data mark not found */
#define ERR_TARGET_SECTOR	0x14		/* target sector not found */
#define ERR_SEEK_ERROR		0x15		/* seek error- cyl not found */
#define ERR_CORR_ERR		0x18		/* correctable data */
#define ERR_BAD_TRACK		0x19		/* track is flagged as bad */
#define ERR_ALT_TRACK_FLAGGED	0x1c		/* alt trk not flagged as alt */
#define ERR_ALT_TRACK_ACCESS	0x1e 		/* illegal access to alt trk */
#define ERR_NO_RECOVERY		0x1f		/* recovery mode not avail */

/* Group 2: command errors. */
#define ERR_BAD_COMMAND		0x20		/* invalid command */
#define ERR_ILLEGAL_ADDR	0x21		/* address beyond disk size */
#define ERR_BAD_PARAMETER	0x22		/* invalid command parameter */

/* Group 3: misc errors. */
#define ERR_BAD_RAM		0x30		/* controller has bad RAM */
#define ERR_BAD_ROM		0x31		/* ROM failed checksum test */
#define ERR_CRC_FAIL		0x32		/* CRC circuit failed test */

/* Controller commands. */
#define CMD_TEST_DRIVE_READY	0x00
#define CMD_RECALIBRATE		0x01
/* reserved			0x02 */
#define CMD_STATUS		0x03
#define CMD_FORMAT_DRIVE	0x04
#define CMD_VERIFY		0x05
#define CMD_FORMAT_TRACK	0x06
#define CMD_FORMAT_BAD_TRACK	0x07
#define CMD_READ		0x08
#define CMD_REASSIGN		0x09
#define CMD_WRITE		0x0a
#define CMD_SEEK		0x0b
#define CMD_SPECIFY		0x0c
#define CMD_READ_ECC_BURST_LEN	0x0d
#define CMD_READ_BUFFER		0x0e
#define CMD_WRITE_BUFFER	0x0f
#define CMD_ALT_TRACK		0x11
#define CMD_INQUIRY_ST11	0x12		/* ST-11 BIOS */
#define CMD_RAM_DIAGNOSTIC	0xe0
/* reserved			0xe1 */
/* reserved			0xe2 */
#define CMD_DRIVE_DIAGNOSTIC	0xe3
#define CMD_CTRLR_DIAGNOSTIC	0xe4
#define CMD_READ_LONG		0xe5
#define CMD_WRITE_LONG		0xe6

#define CMD_FORMAT_ST11		0xf6		/* ST-11 BIOS */
#define CMD_GET_GEOMETRY_ST11	0xf8		/* ST-11 BIOS */
#define CMD_SET_GEOMETRY_ST11	0xfa		/* ST-11 BIOS */
#define CMD_WRITE_GEOMETRY_ST11	0xfc		/* ST-11 BIOS 2.0 */

#define CMD_GET_DRIVE_PARAMS_DTC 0xfb		/* DTC */
#define CMD_SET_STEP_RATE_DTC	0xfc		/* DTC */
#define CMD_SET_GEOMETRY_DTC	0xfe		/* DTC */
#define CMD_GET_GEOMETRY_DTC	0xff		/* DTC */

enum {
    STATE_IDLE,
    STATE_RECEIVE_COMMAND,
    STATE_START_COMMAND,
    STATE_RECEIVE_DATA,
    STATE_RECEIVED_DATA,
    STATE_SEND_DATA,
    STATE_SENT_DATA,
    STATE_COMPLETION_BYTE,
    STATE_DONE
};


typedef struct {
    int8_t	present;
    uint8_t	hdd_num;

    uint8_t	interleave;		/* default interleave */
    char	pad;

    uint16_t	cylinder;		/* current cylinder */

    uint8_t	spt,			/* physical parameters */
		hpc;
    uint16_t	tracks;

    uint8_t	cfg_spt,		/* configured parameters */
		cfg_hpc;
    uint16_t	cfg_cyl;
} drive_t;


typedef struct {
    uint8_t	type;			/* controller type */

    uint8_t	spt;			/* sectors-per-track for controller */

    uint16_t	base;			/* controller configuration */
    int8_t	irq,
		dma;
    uint8_t	switches;
    uint8_t	misc;
    uint8_t	nr_err, err_bv, cur_sec, pad;
    uint32_t	bios_addr,
		bios_size,
		bios_ram;
    rom_t	bios_rom;

    int		state;			/* operational data */
    uint8_t	irq_dma;
    uint8_t	error;
    uint8_t	status;
    int8_t	cyl_off;		/* for ST-11, cylinder0 offset */
    pc_timer_t	timer;

    uint8_t	command[6];		/* current command request */
    int		drive_sel;
    int		sector,
		head,
		cylinder,
		count;
    uint8_t	compl;			/* current request completion code */

    int		buff_pos,		/* pointers to the RAM buffer */
		buff_cnt;

    drive_t	drives[MFM_NUM];	/* the attached drives */
    uint8_t	scratch[64];		/* ST-11 scratchpad RAM */
    uint8_t	buff[SECTOR_SIZE + 4];	/* sector buffer RAM (+ ECC bytes) */
} hdc_t;


/* Supported drives table for the Xebec controller. */
typedef struct {
    uint16_t	tracks;
    uint8_t	hpc;
    uint8_t	spt;
} hd_type_t;

hd_type_t hd_types[4] = {
    { 306, 4, MFM_SECTORS },	/* type 0	*/
    { 612, 4, MFM_SECTORS },	/* type 16	*/
    { 615, 4, MFM_SECTORS },	/* type 2	*/
    { 306, 8, MFM_SECTORS } 	/* type 13	*/
};


#ifdef ENABLE_ST506_XT_LOG
int st506_xt_do_log = ENABLE_ST506_XT_LOG;


static void
st506_xt_log(const char *fmt, ...)
{
    va_list ap;

    if (st506_xt_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define st506_xt_log(fmt, ...)
#endif


static void
st506_complete(hdc_t *dev)
{
    dev->status = STAT_REQ | STAT_CD | STAT_IO | STAT_BSY;
    dev->state = STATE_COMPLETION_BYTE;

    if (dev->irq_dma & DMA_ENA)
	dma_set_drq(dev->dma, 0);

    if (dev->irq_dma & IRQ_ENA) {
	dev->status |= STAT_IRQ;
	picint(1 << dev->irq);
    }
}


static void
st506_error(hdc_t *dev, uint8_t err)
{
    dev->compl |= 0x02;
    dev->error = err;
}


static int
get_sector(hdc_t *dev, drive_t *drive, off64_t *addr)
{
    if (! drive->present) {
	/* No need to log this. */
	dev->error = dev->nr_err;
	return(0);
    }

#if 0
    if (drive->cylinder != dev->cylinder) {
#ifdef ENABLE_ST506_XT_LOG
	st506_xt_log("ST506: get_sector: wrong cylinder\n");
#endif
	dev->error = ERR_ILLEGAL_ADDR;
	return(0);
    }
#endif

    if (dev->head >= drive->cfg_hpc) {
#ifdef ENABLE_ST506_XT_LOG
	st506_xt_log("ST506: get_sector: past end of configured heads\n");
#endif
	dev->error = ERR_ILLEGAL_ADDR;
	return(0);
    }
    if (dev->sector >= drive->cfg_spt) {
#ifdef ENABLE_ST506_XT_LOG
	st506_xt_log("ST506: get_sector: past end of configured sectors\n");
#endif
	dev->error = ERR_ILLEGAL_ADDR;
	return(0);
    }

    *addr = ((((off64_t)dev->cylinder * drive->cfg_hpc) + dev->head) * drive->cfg_spt) + dev->sector;

    return(1);
}


static void
next_sector(hdc_t *dev, drive_t *drive)
{
    if (++dev->sector >= drive->cfg_spt) {
	dev->sector = 0;
	if (++dev->head >= drive->cfg_hpc) {
		dev->head = 0;
		if (++drive->cylinder >= drive->cfg_cyl) {
			/*
			 * This really is an error, we cannot move
			 * past the end of the drive, which should
			 * result in an ERR_ILLEGAL_ADDR.  --FvK
			 */
			drive->cylinder = drive->cfg_cyl - 1;
		} else
			dev->cylinder++;
	}
    }
}


/* Extract the CHS info from a command block. */
static int
get_chs(hdc_t *dev, drive_t *drive)
{
    dev->err_bv = 0x80;

    dev->head = dev->command[1] & 0x1f;
    /* 6 bits are used for the sector number even on the IBM PC controller. */
    dev->sector = dev->command[2] & 0x3f;
    dev->count = dev->command[4];
    if (((dev->type == 11) || (dev->type == 12)) && (dev->command[0] >= 0xf0))
	dev->cylinder = 0;
    else {
	dev->cylinder = dev->command[3] | ((dev->command[2] & 0xc0) << 2);
	dev->cylinder += dev->cyl_off;		/* for ST-11 */
    }

    if (dev->cylinder >= drive->cfg_cyl) {
	/*
	 * This really is an error, we cannot move
	 * past the end of the drive, which should
	 * result in an ERR_ILLEGAL_ADDR.  --FvK
	 */
	drive->cylinder = drive->cfg_cyl - 1;
	return(0);
    }

    drive->cylinder = dev->cylinder;

    return(1);
}


static void
st506_callback(void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    drive_t *drive;
    off64_t addr;
    uint32_t capac;
    int val;

    /* Get the drive info. Note that the API supports up to 8 drives! */
    dev->drive_sel = (dev->command[1] >> 5) & 0x07;
    drive = &dev->drives[dev->drive_sel];

    /* Preset the completion byte to "No error" and the selected drive. */
    dev->compl = (dev->drive_sel << 5) | ERR_NONE;

    if (dev->command[0] != 3)
	dev->err_bv = 0x00;

    switch (dev->command[0]) {
	case CMD_TEST_DRIVE_READY:
		st506_xt_log("ST506: TEST_READY(%i) = %i\n",
			     dev->drive_sel, drive->present);
		if (! drive->present)
			st506_error(dev, dev->nr_err);
		st506_complete(dev);
		break;

	case CMD_RECALIBRATE:
		switch (dev->state) {
			case STATE_START_COMMAND:
				st506_xt_log("ST506: RECALIBRATE(%i) [%i]\n",
					     dev->drive_sel, drive->present);
				if (! drive->present) {
					st506_error(dev, dev->nr_err);
					st506_complete(dev);
					break;
				}

				/* Wait 20msec. */
				timer_advance_u64(&dev->timer, ST506_TIME_MS * 20);

				dev->cylinder = dev->cyl_off;
				drive->cylinder = dev->cylinder;
				dev->state = STATE_DONE;

				break;

			case STATE_DONE:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_STATUS:
		switch (dev->state) {
			case STATE_START_COMMAND:
#ifdef ENABLE_ST506_XT_LOG
				st506_xt_log("ST506: STATUS\n");
#endif
				dev->buff_pos = 0;
				dev->buff_cnt = 4;
				dev->buff[0] = dev->err_bv | dev->error;
				dev->error = 0;

				/* Give address of last operation. */
				dev->buff[1] = (dev->drive_sel ? 0x20 : 0) |
					       dev->head;
				dev->buff[2] = ((dev->cylinder & 0x0300) >> 2) |
					       dev->sector;
				dev->buff[3] = (dev->cylinder & 0xff);

				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_FORMAT_DRIVE:
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				st506_xt_log("ST506: FORMAT_DRIVE(%i) interleave=%i\n",
					     dev->drive_sel, dev->command[4]);
				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);
				timer_advance_u64(&dev->timer, ST506_TIME);
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:	/* wrong, but works */
				if (! get_sector(dev, drive, &addr)) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				/* FIXME: should be drive->capac, not ->spt */
				capac = (drive->tracks - 1) * drive->hpc * drive->spt;
				hdd_image_zero(drive->hdd_num, addr, capac);

				/* Wait 20msec per cylinder. */
				timer_advance_u64(&dev->timer, ST506_TIME_MS * 20);

				dev->state = STATE_SENT_DATA;
				break;

			case STATE_SENT_DATA:
				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
				st506_complete(dev);
				break;
		}
		break;

	case CMD_VERIFY:
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				st506_xt_log("ST506: VERIFY(%i, %i/%i/%i, %i)\n",
					     dev->drive_sel, dev->cylinder,
					     dev->head, dev->sector, dev->count);
				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);
				timer_advance_u64(&dev->timer, ST506_TIME);
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:
				if (dev->count-- == 0) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_complete(dev);
				}

				if (! get_sector(dev, drive, &addr)) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				next_sector(dev, drive);

				timer_advance_u64(&dev->timer, ST506_TIME);
				break;
		}
		break;

	case CMD_FORMAT_ST11:	/* This is really "Format cylinder 0" */
		if ((dev->type < 11) || (dev->type > 12)) {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
			break;
		}
	case CMD_FORMAT_TRACK:
	case CMD_FORMAT_BAD_TRACK:
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				st506_xt_log("ST506: FORMAT_%sTRACK(%i, %i/%i)\n",
					     (dev->command[0] == CMD_FORMAT_BAD_TRACK) ? "BAD_" : "",
					     dev->drive_sel, dev->cylinder, dev->head);
				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);
				timer_advance_u64(&dev->timer, ST506_TIME);
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:	/* wrong, but works */
				if (! get_sector(dev, drive, &addr)) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				hdd_image_zero(drive->hdd_num,
					       addr, drive->cfg_spt);

				/* Wait 20 msec per cylinder. */
				timer_advance_u64(&dev->timer, ST506_TIME_MS * 20);

				dev->state = STATE_SENT_DATA;
				break;

			case STATE_SENT_DATA:
				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
				st506_complete(dev);
				break;
		}
		break;

	case CMD_GET_GEOMETRY_ST11:	/* "Get geometry" is really "Read cylinder 0" */
		if ((dev->type < 11) || (dev->type > 12)) {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
			break;
		}
	case CMD_READ:
#if 0
	case CMD_READ_LONG:
#endif
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				st506_xt_log("ST506: READ%s(%i, %i/%i/%i, %i)\n",
					     (dev->command[0] == CMD_READ_LONG) ? "_LONG" : "",
					     dev->drive_sel, dev->cylinder,
					     dev->head, dev->sector, dev->count);

				if (! get_sector(dev, drive, &addr)) {
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}
				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);

				/* Read data from the image. */
				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *)dev->buff);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				if (dev->command[0] == CMD_READ_LONG)
					dev->buff_cnt += 4;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					timer_advance_u64(&dev->timer, ST506_TIME);
					dma_set_drq(dev->dma, 1);
				}
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_write(dev->dma, dev->buff[dev->buff_pos]);
					if (val == DMA_NODATA) {
#ifdef ENABLE_ST506_XT_LOG
						st506_xt_log("ST506: CMD_READ out of data!\n");
#endif
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
				}
				dma_set_drq(dev->dma, 0);
				timer_advance_u64(&dev->timer, ST506_TIME);
				dev->state = STATE_SENT_DATA;
				break;

			case STATE_SENT_DATA:
				if (--dev->count == 0) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_complete(dev);
					break;
				}

				next_sector(dev, drive);

				if (! get_sector(dev, drive, &addr)) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				/* Read data from the image. */
				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *)dev->buff);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					timer_advance_u64(&dev->timer, ST506_TIME);
					dma_set_drq(dev->dma, 1);
				}
				dev->state = STATE_SEND_DATA;
				break;
		}
		break;

	case CMD_SET_GEOMETRY_ST11:	/* "Set geometry" is really "Write cylinder 0" */
		if (dev->type == 1) {
			/* DTC sends this... */
			st506_complete(dev);
			break;
		} else if ((dev->type < 11) || (dev->type > 12)) {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
			break;
		}
	case CMD_WRITE:
#if 0
	case CMD_WRITE_LONG:
#endif
		switch (dev->state) {
			case STATE_START_COMMAND:
				(void)get_chs(dev, drive);
				st506_xt_log("ST506: WRITE%s(%i, %i/%i/%i, %i)\n",
					     (dev->command[0] == CMD_WRITE_LONG) ? "_LONG" : "",
					     dev->drive_sel, dev->cylinder,
					     dev->head, dev->sector, dev->count);

				if (! get_sector(dev, drive, &addr)) {
					st506_error(dev, ERR_BAD_PARAMETER);
					st506_complete(dev);
					return;
				}

				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				if (dev->command[0] == CMD_WRITE_LONG)
					dev->buff_cnt += 4;
				dev->status = STAT_BSY | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					timer_advance_u64(&dev->timer, ST506_TIME);
					dma_set_drq(dev->dma, 1);
				}
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVE_DATA:
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_read(dev->dma);
					if (val == DMA_NODATA) {
#ifdef ENABLE_ST506_XT_LOG
						st506_xt_log("ST506: CMD_WRITE out of data!\n");
#endif
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
					dev->buff[dev->buff_pos] = val & 0xff;
				}

				dma_set_drq(dev->dma, 0);
				timer_advance_u64(&dev->timer, ST506_TIME);
				dev->state = STATE_RECEIVED_DATA;
				break;

			case STATE_RECEIVED_DATA:
				if (! get_sector(dev, drive, &addr)) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_error(dev, dev->error);
					st506_complete(dev);
					return;
				}

				/* Write data to image. */
				hdd_image_write(drive->hdd_num, addr, 1,
						(uint8_t *)dev->buff);

				if (--dev->count == 0) {
					ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
					st506_complete(dev);
					break;
				}

				next_sector(dev, drive);

				/* Set up the data transfer. */
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				dev->status = STAT_BSY | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					timer_advance_u64(&dev->timer, ST506_TIME);
					dma_set_drq(dev->dma, 1);
				}
				dev->state = STATE_RECEIVE_DATA;
				break;
		}
		break;

	case CMD_SEEK:
		if (drive->present) {
			val = get_chs(dev, drive);
			st506_xt_log("ST506: SEEK(%i, %i) [%i]\n",
				     dev->drive_sel, drive->cylinder, val);
			if (! val)
				st506_error(dev, ERR_SEEK_ERROR);
		} else
			st506_error(dev, dev->nr_err);
		st506_complete(dev);
		break;

	case CMD_SPECIFY:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = 8;
				dev->status = STAT_BSY | STAT_REQ;
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVED_DATA:
				drive->cfg_cyl = dev->buff[1] | (dev->buff[0] << 8);
				drive->cfg_hpc = dev->buff[2];
				/* For a 615/4/26 we get 666/2/31 geometry. */
				st506_xt_log("ST506: drive%i: cyls=%i, heads=%i\n",
					     dev->drive_sel, drive->cfg_cyl, drive->cfg_hpc);
				st506_complete(dev);
				break;
		}
		break;

	case CMD_READ_ECC_BURST_LEN:
		switch (dev->state) {
			case STATE_START_COMMAND:
#ifdef ENABLE_ST506_XT_LOG
				st506_xt_log("ST506: READ_ECC_BURST_LEN\n");
#endif
				dev->buff_pos = 0;
				dev->buff_cnt = 1;
				dev->buff[0] = 0;	/* 0 bits */
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_READ_BUFFER:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				st506_xt_log("ST506: READ_BUFFER (%i)\n",
					     dev->buff_cnt);

				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					timer_advance_u64(&dev->timer, ST506_TIME);
					dma_set_drq(dev->dma, 1);
				}
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SEND_DATA:
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_write(dev->dma, dev->buff[dev->buff_pos]);
					if (val == DMA_NODATA) {
#ifdef ENABLE_ST506_XT_LOG
						st506_xt_log("ST506: CMD_READ_BUFFER out of data!\n");
#endif
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
				}

				dma_set_drq(dev->dma, 0);
				timer_advance_u64(&dev->timer, ST506_TIME);
				dev->state = STATE_SENT_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_WRITE_BUFFER:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = SECTOR_SIZE;
				st506_xt_log("ST506: WRITE_BUFFER (%i)\n",
					     dev->buff_cnt);

				dev->status = STAT_BSY | STAT_REQ;
				if (dev->irq_dma & DMA_ENA) {
					timer_advance_u64(&dev->timer, ST506_TIME);
					dma_set_drq(dev->dma, 1);
				}
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVE_DATA:
				for (; dev->buff_pos < dev->buff_cnt; dev->buff_pos++) {
					val = dma_channel_read(dev->dma);
					if (val == DMA_NODATA) {
#ifdef ENABLE_ST506_XT_LOG
						st506_xt_log("ST506: CMD_WRITE_BUFFER out of data!\n");
#endif
						st506_error(dev, ERR_NO_RECOVERY);
						st506_complete(dev);
						return;
					}
					dev->buff[dev->buff_pos] = val & 0xff;
				}

				dma_set_drq(dev->dma, 0);
				timer_advance_u64(&dev->timer, ST506_TIME);
				dev->state = STATE_RECEIVED_DATA;
				break;

			case STATE_RECEIVED_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_INQUIRY_ST11:
		if (dev->type == 11 || dev->type == 12) switch (dev->state) {
			case STATE_START_COMMAND:
				st506_xt_log("ST506: INQUIRY (type=%i)\n", dev->type);
				dev->buff_pos = 0;
				dev->buff_cnt = 2;
				dev->buff[0] = 0x80;		/* "ST-11" */
				if (dev->spt == 17)
					dev->buff[0] |= 0x40;	/* MFM */
				dev->buff[1] = dev->misc;	/* revision */
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		} else {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
		}
		break;

	case CMD_RAM_DIAGNOSTIC:
#ifdef ENABLE_ST506_XT_LOG
		st506_xt_log("ST506: RAM_DIAG\n");
#endif
		st506_complete(dev);
		break;

	case CMD_CTRLR_DIAGNOSTIC:
#ifdef ENABLE_ST506_XT_LOG
		st506_xt_log("ST506: CTRLR_DIAG\n");
#endif
		st506_complete(dev);
		break;

	case CMD_SET_STEP_RATE_DTC:
		if (dev->type == 1) {
			/* For DTC, we are done. */
			st506_complete(dev);
		} else if (dev->type == 11 || dev->type == 12) {
			/*
			 * For Seagate ST-11, this is WriteGeometry.
			 *
			 * This writes the contents of the buffer to track 0.
			 *
			 * By the time this command is sent, it will have
			 * formatted the first track, so it should be good,
			 * and our sector buffer contains the magic data
			 * (see above) we need to write to it.
			 */
			(void)get_chs(dev, drive);
			st506_xt_log("ST506: WRITE BUFFER (%i, %i/%i/%i, %i)\n",
				     dev->drive_sel, dev->cylinder,
				     dev->head, dev->sector, dev->count);

			if (! get_sector(dev, drive, &addr)) {
				st506_error(dev, ERR_BAD_PARAMETER);
				st506_complete(dev);
				return;
			}

			ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 1);

			/* Write data to image. */
			hdd_image_write(drive->hdd_num, addr, 1,
					(uint8_t *)dev->buff);

			if (--dev->count == 0) {
				ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
				st506_complete(dev);
				break;
			}

			next_sector(dev, drive);
			timer_advance_u64(&dev->timer, ST506_TIME);
			break;
		} else {
			st506_error(dev, ERR_BAD_COMMAND);
			st506_complete(dev);
		}
		break;

	case CMD_GET_DRIVE_PARAMS_DTC:
		switch (dev->state) {
			case STATE_START_COMMAND:
				dev->buff_pos = 0;
				dev->buff_cnt = 4;
				memset(dev->buff, 0x00, dev->buff_cnt);
				dev->buff[0] = drive->tracks & 0xff;
				dev->buff[1] = ((drive->tracks >> 2) & 0xc0) | dev->spt;
				dev->buff[2] = drive->hpc - 1;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	case CMD_SET_GEOMETRY_DTC:
		switch (dev->state) {
			case STATE_START_COMMAND:
				val = dev->command[1] & 0x01;
				st506_xt_log("ST506: DTC_GET_GEOMETRY(%i) %i\n",
					     dev->drive_sel, val);
				dev->buff_pos = 0;
				dev->buff_cnt = 16;
				dev->status = STAT_BSY | STAT_REQ;
				dev->state = STATE_RECEIVE_DATA;
				break;

			case STATE_RECEIVED_DATA:
				/* FIXME: ignore the results. */
				st506_complete(dev);
			break;
		}
		break;

	case CMD_GET_GEOMETRY_DTC:
		switch (dev->state) {
			case STATE_START_COMMAND:
				val = dev->command[1] & 0x01;
				st506_xt_log("ST506: DTC_GET_GEOMETRY(%i) %i\n",
					     dev->drive_sel, val);
				dev->buff_pos = 0;
				dev->buff_cnt = 16;
				memset(dev->buff, 0x00, dev->buff_cnt);
				dev->buff[4] = drive->tracks & 0xff;
				dev->buff[5] = (drive->tracks >> 8) & 0xff;
				dev->buff[10] = drive->hpc;
				dev->status = STAT_BSY | STAT_IO | STAT_REQ;
				dev->state = STATE_SEND_DATA;
				break;

			case STATE_SENT_DATA:
				st506_complete(dev);
				break;
		}
		break;

	default:
		if (dev->command[0] == CMD_WRITE_GEOMETRY_ST11)
			fatal("CMD_WRITE_GEOMETRY_ST11\n");
#ifdef ENABLE_ST506_XT_LOG
		st506_xt_log("ST506: unknown command:\n");
#endif
		st506_xt_log("ST506: %02x %02x %02x %02x %02x %02x\n",
			     dev->command[0], dev->command[1], dev->command[2],
			     dev->command[3], dev->command[4], dev->command[5]);
		st506_error(dev, ERR_BAD_COMMAND);
		st506_complete(dev);
    }
}


/* Read from one of the registers. */
static uint8_t
st506_read(uint16_t port, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 3) {
	case 0:		/* read data */
		dev->status &= ~STAT_IRQ;
		switch (dev->state) {
			case STATE_COMPLETION_BYTE:
				ret = dev->compl;
				dev->status = 0x00;
				dev->state = STATE_IDLE;
				break;

			case STATE_SEND_DATA:
				ret = dev->buff[dev->buff_pos++];
				if (dev->buff_pos == dev->buff_cnt) {
					dev->buff_pos = 0;
					dev->buff_cnt = 0;
					dev->status = STAT_BSY;
					dev->state = STATE_SENT_DATA;
					timer_set_delay_u64(&dev->timer, ST506_TIME);
				}
				break;
		}
		break;

	case 1:		/* read status */
		ret = dev->status;
		if ((dev->irq_dma & DMA_ENA) && dma_get_drq(dev->dma))
			ret |= STAT_DRQ;
		break;

	case 2:		/* read option jumpers */
		ret = dev->switches;
		break;
    }
    st506_xt_log("ST506: read(%04x) = %02x\n", port, ret);

    return(ret);
}


/* Write to one of the registers. */
static void
st506_write(uint16_t port, uint8_t val, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;

    st506_xt_log("ST506: write(%04x, %02x)\n", port, val);
    switch (port & 3) {
	case 0:		/* write data */
		switch (dev->state) {
			case STATE_RECEIVE_COMMAND:	/* command data */
				/* Write directly to the command buffer to avoid overwriting
				   the data buffer. */
				dev->command[dev->buff_pos++] = val;
				if (dev->buff_pos == dev->buff_cnt) {
					/* We have a new command. */
					dev->buff_pos = 0;
					dev->buff_cnt = 0;
					dev->status = STAT_BSY;
					dev->state = STATE_START_COMMAND;
					timer_set_delay_u64(&dev->timer, ST506_TIME);
				}
				break;

			case STATE_RECEIVE_DATA:	/* data */
				dev->buff[dev->buff_pos++] = val;
				if (dev->buff_pos == dev->buff_cnt) {
					dev->buff_pos = 0;
					dev->buff_cnt = 0;
					dev->status = STAT_BSY;
					dev->state = STATE_RECEIVED_DATA;
					timer_set_delay_u64(&dev->timer, ST506_TIME);
				}
				break;
		}
		break;

	case 1:		/* controller reset */
		dev->status = 0x00;
		break;

	case 2:		/* generate controller-select-pulse */
		dev->status = STAT_BSY | STAT_CD | STAT_REQ;
		dev->buff_pos = 0;
		dev->buff_cnt = sizeof(dev->command);
		dev->state = STATE_RECEIVE_COMMAND;
		break;

	case 3:		/* DMA/IRQ enable register */
		dev->irq_dma = val;

		if (!(dev->irq_dma & DMA_ENA))
			dma_set_drq(dev->dma, 0);

		if (!(dev->irq_dma & IRQ_ENA)) {
			dev->status &= ~STAT_IRQ;
			picintc(1 << dev->irq);
		}
		break;
    }
}


/* Write to ROM (or scratchpad RAM.) */
static void
mem_write(uint32_t addr, uint8_t val, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    uint32_t ptr, mask = 0;

    /* Ignore accesses to anything below the configured address,
       needed because of the emulator's 4k mapping granularity. */
    if (addr < dev->bios_addr)
	return;

    addr -= dev->bios_addr;

    switch(dev->type) {
	case 11:	/* ST-11M */
	case 12:	/* ST-11R */
		mask = 0x1fff;	/* ST-11 decodes RAM on each 8K block */
		break;

	default:
		break;
    }

    addr &= dev->bios_rom.mask;

    ptr = (dev->bios_rom.mask & mask) - dev->bios_ram;
    if (mask && ((addr & mask) > ptr) &&
		((addr & mask) <= (ptr + dev->bios_ram)))
	dev->scratch[addr & (dev->bios_ram - 1)] = val;
}


static uint8_t
mem_read(uint32_t addr, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    uint32_t ptr, mask = 0;
    uint8_t ret = 0xff;

    /* Ignore accesses to anything below the configured address,
       needed because of the emulator's 4k mapping granularity. */
    if (addr < dev->bios_addr)
	return 0xff;

    addr -= dev->bios_addr;

    switch(dev->type) {
	case 0:		/* Xebec */
		if (addr >= 0x001000) {
#ifdef ENABLE_ST506_XT_LOG
			st506_xt_log("ST506: Xebec ROM access(0x%06lx)\n", addr);
#endif
			return 0xff;
		}
		break;

	case 1:		/* DTC */
	default:
		if (addr >= 0x002000) {
#ifdef ENABLE_ST506_XT_LOG
			st506_xt_log("ST506: DTC-5150X ROM access(0x%06lx)\n", addr);
#endif
			return 0xff;
		}
		break;

	case 11:	/* ST-11M */
	case 12:	/* ST-11R */
		mask = 0x1fff;	/* ST-11 decodes RAM on each 8K block */
		break;

	/* default:
		break; */
    }

    addr = addr & dev->bios_rom.mask;

    ptr = (dev->bios_rom.mask & mask) - dev->bios_ram;
    if (mask && ((addr & mask) > ptr) &&
		((addr & mask) <= (ptr + dev->bios_ram)))
	ret = dev->scratch[addr & (dev->bios_ram - 1)];
    else
	ret = dev->bios_rom.rom[addr];

    return(ret);
}


/*
 * Set up and load the ROM BIOS for this controller.
 *
 * This is straightforward for most, but some (like the ST-11x)
 * map part of the area as scratchpad RAM, so we cannot use the
 * standard 'rom_init' function here.
 */
static void
loadrom(hdc_t *dev, const char *fn)
{
    uint32_t size;
    FILE *fp;

    if (fn == NULL) {
#ifdef ENABLE_ST506_XT_LOG
	st506_xt_log("ST506: NULL BIOS ROM file pointer!\n");
#endif
	return;
    }

    if ((fp = rom_fopen((char *) fn, "rb")) == NULL) {
	st506_xt_log("ST506: BIOS ROM '%s' not found!\n", fn);
	return;
    }

    /* Initialize the ROM entry. */
    memset(&dev->bios_rom, 0x00, sizeof(rom_t));

    /* Manually load and process the ROM image. */
    (void)fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    (void)fseek(fp, 0L, SEEK_SET);

    /* Load the ROM data. */
    dev->bios_rom.rom = (uint8_t *)malloc(size);
    memset(dev->bios_rom.rom, 0xff, size);
    if (fread(dev->bios_rom.rom, 1, size, fp) != size)
	fatal("ST-506 XT loadrom(): Error reading data\n");
    (void)fclose(fp);

    /* Set up an address mask for this memory. */
    dev->bios_size = size;
    dev->bios_rom.mask = (size - 1);

    /* Map this system into the memory map. */
    mem_mapping_add(&dev->bios_rom.mapping, dev->bios_addr, size,
		    mem_read,NULL,NULL, mem_write,NULL,NULL,
		    dev->bios_rom.rom, MEM_MAPPING_EXTERNAL, dev);
}


static void
loadhd(hdc_t *dev, int c, int d, const char *fn)
{
    drive_t *drive = &dev->drives[c];

    if (! hdd_image_load(d)) {
	drive->present = 0;
	return;
    }

    /* Make sure we can do this. */
    /* Allow 31 sectors per track on RLL controllers, for the
       ST225R, which is 667/2/31. */
    if ((hdd[d].spt != dev->spt) && (hdd[d].spt != 31) && (dev->spt != 26)) {
	/*
	 * Uh-oh, MFM/RLL mismatch.
	 *
	 * Although this would be no issue in the code itself,
	 * most of the BIOSes were hardwired to whatever their
	 * native SPT setting was, so, do not allow this here.
	 */
	st506_xt_log("ST506: drive%i: MFM/RLL mismatch (%i/%i)\n",
		     c, hdd[d].spt, dev->spt);
	hdd_image_close(d);
	drive->present = 0;
	return;
    }

    drive->spt = (uint8_t)hdd[d].spt;
    drive->hpc = (uint8_t)hdd[d].hpc;
    drive->tracks = (uint16_t)hdd[d].tracks;

    drive->hdd_num = d;
    drive->present = 1;
}


/* Set the "drive type" switches for the IBM Xebec controller. */
static void
set_switches(hdc_t *dev)
{
    drive_t *drive;
    int c, d;

    dev->switches = 0x00;

    for (d = 0; d < MFM_NUM; d++) {
	drive = &dev->drives[d];

	if (! drive->present) continue;

	for (c = 0; c < 4; c++) {
		if ((drive->spt == hd_types[c].spt) &&
		    (drive->hpc == hd_types[c].hpc) &&
		    (drive->tracks == hd_types[c].tracks)) {
			dev->switches |= (c << (d ? 0 : 2));
			break;
		}
	}

#ifdef ENABLE_ST506_XT_LOG
	st506_xt_log("ST506: ");
	if (c == 4)
		st506_xt_log("*WARNING* drive%i unsupported", d);
	else
		st506_xt_log("drive%i is type %i", d, c);
	st506_xt_log(" (%i/%i/%i)\n", drive->tracks, drive->hpc, drive->spt);
#endif
    }
}


static void *
st506_init(const device_t *info)
{
    char *fn = NULL;
    hdc_t *dev;
    int i, c;

    dev = (hdc_t *)malloc(sizeof(hdc_t));
    memset(dev, 0x00, sizeof(hdc_t));
    dev->type = info->local & 255;

    /* Set defaults for the controller. */
    dev->spt = MFM_SECTORS;
    dev->base = 0x0320;
    dev->irq = 5;
    dev->dma = 3;
    dev->bios_addr = 0xc8000;
    dev->nr_err = ERR_NOT_READY;

    switch(dev->type) {
	case 0:		/* Xebec (MFM) */
		fn = XEBEC_BIOS_FILE;
		break;

	case 1:		/* DTC5150 (MFM) */
		fn = DTC_BIOS_FILE;
		dev->switches = 0xff;
		break;

	case 12:	/* Seagate ST-11R (RLL) */
		dev->spt = RLL_SECTORS;
		/*FALLTHROUGH*/

	case 11:	/* Seagate ST-11M (MFM) */
		dev->nr_err = ERR_NOT_AVAILABLE;
		dev->switches = 0x01;	/* fixed */
		dev->misc = device_get_config_int("revision");
		switch (dev->misc) {
			case 5:		/* v1.7 */
				fn = ST11_BIOS_FILE_OLD;
				break;

			case 19:	/* v2.0 */
				fn = ST11_BIOS_FILE_NEW;
				break;
		}
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		dev->bios_addr = device_get_config_hex20("bios_addr");
		dev->bios_ram = 64;	/* scratch RAM size */

		/*
		 * Industrial Madness Alert.
		 *
		 * With the ST-11 controller, Seagate decided to act
		 * like they owned the industry, and reserved the
		 * first cylinder of a drive for the controller. So,
		 * when the host accessed cylinder 0, that would be
		 * the actual cylinder 1 on the drive, and so on.
		 */
		dev->cyl_off = 1;
		break;

	case 21:	/* Western Digital WD1002A-WX1 (MFM) */
		dev->nr_err = ERR_NOT_AVAILABLE;
		fn = WD1002A_WX1_BIOS_FILE;
		/* The switches are read in reverse: 0 = closed, 1 = open.
		   Both open means MFM, 17 sectors per track. */
		dev->switches = 0x30;	/* autobios */
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		if (dev->irq == 2)
			dev->switches |= 0x40;
		dev->bios_addr = device_get_config_hex20("bios_addr");
		break;

	case 22:	/* Western Digital WD1002A-27X (RLL) */
		dev->nr_err = ERR_NOT_AVAILABLE;
		fn = WD1002A_27X_BIOS_FILE;
		/* The switches are read in reverse: 0 = closed, 1 = open.
		   Both closed means translate 26 sectors per track to 17,
		   SW6 closed, SW5 open means 26 sectors per track. */
		dev->switches = device_get_config_int("translate") ? 0x00 : 0x10;	/* autobios */
		dev->spt = RLL_SECTORS;
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		if (dev->irq == 2)
			dev->switches |= 0x40;
		dev->bios_addr = device_get_config_hex20("bios_addr");
		break;
    }

    /* Load the ROM BIOS. */
    loadrom(dev, fn);

    /* Set up the I/O region. */
    io_sethandler(dev->base, 4,
		  st506_read,NULL,NULL, st506_write,NULL,NULL, dev);

    /* Add the timer. */
    timer_add(&dev->timer, st506_callback, dev, 0);

    st506_xt_log("ST506: %s (I/O=%03X, IRQ=%i, DMA=%i, BIOS @0x%06lX, size %lu)\n",
		 info->name,dev->base,dev->irq,dev->dma, dev->bios_addr,dev->bios_size);

    /* Load any drives configured for us. */
#ifdef ENABLE_ST506_XT_LOG
    st506_xt_log("ST506: looking for disks...\n");
#endif
    for (c = 0, i = 0; i < HDD_NUM; i++) {
	if ((hdd[i].bus == HDD_BUS_MFM) && (hdd[i].mfm_channel < MFM_NUM)) {
		st506_xt_log("ST506: disk '%s' on channel %i\n",
			     hdd[i].fn, hdd[i].mfm_channel);
		loadhd(dev, hdd[i].mfm_channel, i, hdd[i].fn);

		if (++c > MFM_NUM) break;
	}
    }
    st506_xt_log("ST506: %i disks loaded.\n", c);

    /* For the Xebec, set the switches now. */
    if (dev->type == 0)
	set_switches(dev);

    /* Initial "active" drive parameters. */
    for (c = 0; c < MFM_NUM; c++) {
	dev->drives[c].cfg_cyl = dev->drives[c].tracks;
	dev->drives[c].cfg_hpc = dev->drives[c].hpc;
	dev->drives[c].cfg_spt = dev->drives[c].spt;
    }

    return(dev);
}


static void
st506_close(void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    drive_t *drive;
    int d;

    for (d = 0; d < MFM_NUM; d++) {
	drive = &dev->drives[d];

	hdd_image_close(drive->hdd_num);
    }

    if (dev->bios_rom.rom != NULL) {
	free(dev->bios_rom.rom);
	dev->bios_rom.rom = NULL;
    }

    free(dev);
}


static int
xebec_available(void)
{
    return(rom_present(XEBEC_BIOS_FILE));
}


static int
dtc5150x_available(void)
{
    return(rom_present(DTC_BIOS_FILE));
}

static int
st11_m_available(void)
{
    return(rom_present(ST11_BIOS_FILE_OLD) && rom_present(ST11_BIOS_FILE_NEW));
}

static int
st11_r_available(void)
{
    return(rom_present(ST11_BIOS_FILE_OLD) && rom_present(ST11_BIOS_FILE_NEW));
}

static int
wd1002a_wx1_available(void)
{
    return(rom_present(WD1002A_WX1_BIOS_FILE));
}

static int
wd1002a_27x_available(void)
{
    return(rom_present(WD1002A_27X_BIOS_FILE));
}

static int
wd1004a_wx1_available(void)
{
    return(rom_present(WD1004A_WX1_BIOS_FILE));
}

static int
wd1004_27x_available(void)
{
    return(rom_present(WD1004_27X_BIOS_FILE));
}

static int
wd1004a_27x_available(void)
{
    return(rom_present(WD1004A_27X_BIOS_FILE));
}

// clang-format off
static const device_config_t dtc_config[] = {
    {
        .name = "bios_addr",
        .description = "BIOS address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "CA00H",    .value = 0xca000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "F400H",    .value = 0xf4000 },
            { .description = ""                           }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t st11_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x0320,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "320H", .value = 0x0320 },
            { .description = "324H", .value = 0x0324 },
            { .description = "328H", .value = 0x0328 },
            { .description = "32CH", .value = 0x032c },
            { .description = ""                      }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 5", .value = 5 },
            { .description = ""                  }
        }
    },
    {
        .name = "bios_addr",
        .description = "BIOS address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "D000H",    .value = 0xd0000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "E000H",    .value = 0xe0000 },
            { .description = ""                           }
        }
    },
    {
        .name = "revision",
        .description = "Board Revision",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 19,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Rev. 05 (v1.7)", .value =  5 },
            { .description = "Rev. 19 (v2.0)", .value = 19 },
            { .description = ""                            }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t wd_config[] = {
    {
        .name = "bios_addr",
        .description = "BIOS address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = ""                           }
        }
    },
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x0320,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "320H", .value = 0x0320 },
            { .description = "324H", .value = 0x0324 },
            { .description = ""                      }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 5", .value = 5 },
            { .description = ""                  }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t wd_rll_config[] = {
    {
        .name = "bios_addr",
        .description = "BIOS address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = ""                           }
        }
    },
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x0320,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "320H", .value = 0x0320 },
            { .description = "324H", .value = 0x0324 },
            { .description = ""                      }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 5", .value = 5 },
            { .description = ""                  }
        }
    },
    {
        .name = "translate",
        .description = "Translate 26 -> 17",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Off", .value = 0 },
            { .description = "On",  .value = 1 },
            { .description = ""                }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t wd1004a_config[] = {
    {
        .name = "bios_addr",
        .description = "BIOS address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = ""                           }
        }
    },
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x0320,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "320H", .value = 0x0320 },
            { .description = "324H", .value = 0x0324 },
            { .description = ""                      }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 5", .value = 5 },
            { .description = ""                  }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t wd1004_rll_config[] = {
    {
        .name = "bios_addr",
        .description = "BIOS address",
        .type = CONFIG_HEX20,
        .default_string = "",
        .default_int = 0xc8000,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0x00000 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "CA00H",    .value = 0xca000 },
            { .description = "CC00H",    .value = 0xcc000 },
            { .description = "CE00H",    .value = 0xce000 },
            { .description = ""                           }
        }
    },
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x0320,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "320H", .value = 0x0320 },
            { .description = "324H", .value = 0x0324 },
            { .description = "328H", .value = 0x0328 },
            { .description = "32CH", .value = 0x032c },
            { .description = ""                      }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "IRQ 2", .value = 2 },
            { .description = "IRQ 5", .value = 5 },
            { .description = ""                  }
        }
    },
    {
        .name = "translate",
        .description = "Translate 26 -> 17",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Off", .value = 0 },
            { .description = "On",  .value = 1 },
            { .description = ""                }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

// clang-format on

const device_t st506_xt_xebec_device = {
    .name = "IBM PC Fixed Disk Adapter (MFM)",
    .internal_name = "st506_xt",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 0,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = xebec_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t st506_xt_dtc5150x_device = {
    .name = "DTC 5150X MFM Fixed Disk Adapter",
    .internal_name = "st506_xt_dtc5150x",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 1,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = dtc5150x_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = dtc_config
};

const device_t st506_xt_st11_m_device = {
    .name = "ST-11M MFM Fixed Disk Adapter",
    .internal_name = "st506_xt_st11_m",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 11,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = st11_m_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = st11_config
};

const device_t st506_xt_st11_r_device = {
    .name = "ST-11R RLL Fixed Disk Adapter",
    .internal_name = "st506_xt_st11_r",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 12,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = st11_r_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = st11_config
};

const device_t st506_xt_wd1002a_wx1_device = {
    .name = "WD1002A-WX1 MFM Fixed Disk Adapter",
    .internal_name = "st506_xt_wd1002a_wx1",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 21,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = wd1002a_wx1_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = wd_config
};

const device_t st506_xt_wd1002a_27x_device = {
    .name = "WD1002A-27X RLL Fixed Disk Adapter",
    .internal_name = "st506_xt_wd1002a_27x",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 22,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = wd1002a_27x_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = wd_rll_config
};

const device_t st506_xt_wd1004a_wx1_device = {
    .name = "WD1004A-WX1 MFM Fixed Disk Adapter",
    .internal_name = "st506_xt_wd1004a_wx1",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 21,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { wd1004a_wx1_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = wd1004a_config
};

const device_t st506_xt_wd1004_27x_device = {
    .name = "WD1004-27X RLL Fixed Disk Adapter",
    .internal_name = "st506_xt_wd1004_27x",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 22,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = wd1004_27x_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = wd1004_rll_config
};

const device_t st506_xt_wd1004a_27x_device = {
    .name = "WD1004a-27X RLL Fixed Disk Adapter",
    .internal_name = "st506_xt_wd1004a_27x",
    .flags = DEVICE_ISA,
    .local = (HDD_BUS_MFM << 8) | 22,
    .init = st506_init,
    .close = st506_close,
    .reset = NULL,
    { .available = wd1004a_27x_available },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = wd_rll_config
};
