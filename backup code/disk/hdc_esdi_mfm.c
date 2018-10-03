/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Driver for the MFM controller (WD1007-vse1) for PC/AT.
 *
 * Version:	@(#)hdc_mfm_at.c	1.0.13	2018/05/02
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../io.h"
#include "../mem.h"
#include "../pic.h"
#include "../rom.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../timer.h"
#include "../plat.h"
#include "../ui.h"
#include "hdc.h"
#include "hdd.h"


#define HDC_TIME		(TIMER_USEC*10LL)
#define BIOS_FILE		L"roms/hdd/mfm_at/62-000279-061.bin"

#define STAT_ERR		0x01
#define STAT_INDEX		0x02
#define STAT_CORRECTED_DATA	0x04
#define STAT_DRQ		0x08	/* Data request */
#define STAT_DSC                0x10
#define STAT_SEEK_COMPLETE      0x20
#define STAT_READY		0x40
#define STAT_BUSY		0x80

#define ERR_DAM_NOT_FOUND       0x01	/* Data Address Mark not found */
#define ERR_TR000               0x02	/* track 0 not found */
#define ERR_ABRT		0x04	/* command aborted */
#define ERR_ID_NOT_FOUND	0x10	/* ID not found */
#define ERR_DATA_CRC	        0x40	/* data CRC error */
#define ERR_BAD_BLOCK	        0x80	/* bad block detected */

#define CMD_NOP                 0x00
#define CMD_RESTORE		0x10
#define CMD_READ		0x20
#define CMD_WRITE		0x30
#define CMD_VERIFY		0x40
#define CMD_FORMAT		0x50
#define CMD_SEEK   		0x70
#define CMD_DIAGNOSE		0x90
#define CMD_SET_PARAMETERS	0x91
#define CMD_READ_PARAMETERS	0xec


typedef struct {
    int		cfg_spt;
    int		cfg_hpc;
    int		current_cylinder;
    int		real_spt;
    int		real_hpc;
    int		real_tracks;
    int		present;
    int		hdd_num;
} drive_t;

typedef struct {
    uint8_t	status;
    uint8_t	error;
    int		secount,sector,cylinder,head,cylprecomp;
    uint8_t	command;
    uint8_t	fdisk;
    int		pos;

    int		drive_sel;
    int		reset;
    uint16_t	buffer[256];
    int		irqstat;

    int64_t	callback;

    drive_t	drives[2];

    rom_t	bios_rom;
} mfm_t;


#ifdef ENABLE_MFM_AT_LOG
int mfm_at_do_log = ENABLE_MFM_AT_LOG;
#endif


static void
mfm_at_log(const char *fmt, ...)
{
#ifdef ENABLE_MFM_AT_LOG
    va_list ap;

    if (mfm_at_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


static inline void
irq_raise(mfm_t *mfm)
{
    if (!(mfm->fdisk & 2))
	picint(1 << 14);

    mfm->irqstat = 1;
}


static inline void
irq_lower(mfm_t *mfm)
{
    if (mfm->irqstat) {
	if (!(mfm->fdisk & 2))
		picintc(1 << 14);

	mfm->irqstat = 0;
    }
}


/* Return the sector offset for the current register values. */
static int
get_sector(mfm_t *mfm, off64_t *addr)
{
    drive_t *drive = &mfm->drives[mfm->drive_sel];
    int heads = drive->cfg_hpc;
    int sectors = drive->cfg_spt;
    int c, h, s;

    if (mfm->head > heads) {
	mfm_at_log("mfm_get_sector: past end of configured heads\n");
	return(1);
    }

    if (mfm->sector >= sectors+1) {
	mfm_at_log("mfm_get_sector: past end of configured sectors\n");
	return(1);
    }

    if (drive->cfg_spt==drive->real_spt && drive->cfg_hpc==drive->real_hpc) {
	*addr = ((((off64_t) mfm->cylinder * heads) + mfm->head) *
					sectors) + (mfm->sector - 1);
    } else {
	/*
	 * When performing translation, the firmware seems to leave 1
	 * sector per track inaccessible (spare sector)
	 */

	*addr = ((((off64_t) mfm->cylinder * heads) + mfm->head) *
					sectors) + (mfm->sector - 1);

	s = *addr % (drive->real_spt - 1);
	h = (*addr / (drive->real_spt - 1)) % drive->real_hpc;
	c = (*addr / (drive->real_spt - 1)) / drive->real_hpc;

	*addr = ((((off64_t)c * drive->real_hpc) + h) * drive->real_spt) + s;
    }
        
    return(0);
}


/* Move to the next sector using CHS addressing. */
static void
next_sector(mfm_t *mfm)
{
    drive_t *drive = &mfm->drives[mfm->drive_sel];

    mfm->sector++;
    if (mfm->sector == (drive->cfg_spt + 1)) {
	mfm->sector = 1;
	if (++mfm->head == drive->cfg_hpc) {
		mfm->head = 0;
		mfm->cylinder++;
		if (drive->current_cylinder < drive->real_tracks)
			drive->current_cylinder++;
	}
    }
}


static void
mfm_writew(uint16_t port, uint16_t val, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;

    mfm->buffer[mfm->pos >> 1] = val;
    mfm->pos += 2;

    if (mfm->pos >= 512) {
	mfm->pos = 0;
	mfm->status = STAT_BUSY;
	timer_clock();
	/* 390.625 us per sector at 10 Mbit/s = 1280 kB/s. */
	mfm->callback = (3125LL * TIMER_USEC) / 8LL;
	timer_update_outstanding();
    }
}


static void
mfm_write(uint16_t port, uint8_t val, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;

    mfm_at_log("WD1007 write(%04x, %02x)\n", port, val);

    switch (port) {
	case 0x1f0:	/* data */
		mfm_writew(port, val | (val << 8), priv);
		return;

	case 0x1f1:	/* write precompensation */
		mfm->cylprecomp = val;
		return;

	case 0x1f2:	/* sector count */
		mfm->secount = val;
		return;

	case 0x1f3:	/* sector */
		mfm->sector = val;
		return;

	case 0x1f4:	/* cylinder low */
		mfm->cylinder = (mfm->cylinder & 0xFF00) | val;
		return;

	case 0x1f5:	/* cylinder high */
		mfm->cylinder = (mfm->cylinder & 0xFF) | (val << 8);
		return;

	case 0x1f6: /* drive/Head */
		mfm->head = val & 0xF;
		mfm->drive_sel = (val & 0x10) ? 1 : 0;
		if (mfm->drives[mfm->drive_sel].present) {
			mfm->status = STAT_READY|STAT_DSC;
		} else {
			mfm->status = 0;
		}
		return;

	case 0x1f7:	/* command register */
		irq_lower(mfm);
		mfm->command = val;
		mfm->error = 0;

		mfm_at_log("WD1007: command %02x\n", val & 0xf0);

		switch (val & 0xf0) {
			case CMD_RESTORE:
				mfm->command &= ~0x0f; /*mask off step rate*/
				mfm->status = STAT_BUSY;
				timer_clock();
				mfm->callback = 200LL*HDC_TIME;
				timer_update_outstanding();
				break;

			case CMD_SEEK:
				mfm->command &= ~0x0f; /*mask off step rate*/
				mfm->status = STAT_BUSY;
				timer_clock();
				mfm->callback = 200LL*HDC_TIME;
				timer_update_outstanding();
				break;

			default:
				switch (val) {
					case CMD_NOP:
						mfm->status = STAT_BUSY;
						timer_clock();
						mfm->callback = 200LL*HDC_TIME;
						timer_update_outstanding();
						break;

					case CMD_READ:
					case CMD_READ+1:
					case CMD_READ+2:
					case CMD_READ+3:
						mfm->command &= ~0x03;
						if (val & 0x02)
							fatal("Read with ECC\n");

					case 0xa0:
						mfm->status = STAT_BUSY;
						timer_clock();
						mfm->callback = 200LL*HDC_TIME;
						timer_update_outstanding();
						break;

					case CMD_WRITE:
					case CMD_WRITE+1:
					case CMD_WRITE+2:
					case CMD_WRITE+3:
						mfm->command &= ~0x03;
						if (val & 0x02)
							fatal("Write with ECC\n");
						mfm->status = STAT_DRQ | STAT_DSC;
						mfm->pos = 0;
						break;

					case CMD_VERIFY:
					case CMD_VERIFY+1:
						mfm->command &= ~0x01;
						mfm->status = STAT_BUSY;
						timer_clock();
						mfm->callback = 200LL*HDC_TIME;
						timer_update_outstanding();
						break;

					case CMD_FORMAT:
						mfm->status = STAT_DRQ;
						mfm->pos = 0;
						break;

					case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
						mfm->status = STAT_BUSY;
						timer_clock();
						mfm->callback = 30LL*HDC_TIME;
						timer_update_outstanding();
						break;

					case CMD_DIAGNOSE: /* Execute Drive Diagnostics */
						mfm->status = STAT_BUSY;
						timer_clock();
						mfm->callback = 200LL*HDC_TIME;
						timer_update_outstanding();
						break;

					case 0xe0: /*???*/
					case CMD_READ_PARAMETERS:
						mfm->status = STAT_BUSY;
						timer_clock();
						mfm->callback = 200LL*HDC_TIME;
						timer_update_outstanding();
						break;

					default:
						mfm_at_log("WD1007: bad command %02X\n", val);
					case 0xe8: /*???*/
						mfm->status = STAT_BUSY;
						timer_clock();
						mfm->callback = 200LL*HDC_TIME;
						timer_update_outstanding();
						break;
				}
		}
		break;

	case 0x3f6: /* Device control */
		if ((mfm->fdisk & 0x04) && !(val & 0x04)) {
			timer_clock();
			mfm->callback = 500LL*HDC_TIME;
			timer_update_outstanding();
			mfm->reset = 1;
			mfm->status = STAT_BUSY;
		}

		if (val & 0x04) {
			/*Drive held in reset*/
			timer_clock();
			mfm->callback = 0LL;
			timer_update_outstanding();
			mfm->status = STAT_BUSY;
		}
		mfm->fdisk = val;
		/* Lower IRQ on IRQ disable. */
		if ((val & 2) && !(mfm->fdisk & 0x02))
			picintc(1 << 14);
		break;
	}
}


static uint16_t
mfm_readw(uint16_t port, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    uint16_t temp;

    temp = mfm->buffer[mfm->pos >> 1];
    mfm->pos += 2;

    if (mfm->pos >= 512) {
	mfm->pos=0;
	mfm->status = STAT_READY | STAT_DSC;
	if (mfm->command == CMD_READ || mfm->command == 0xa0) {
		mfm->secount = (mfm->secount - 1) & 0xff;
		if (mfm->secount) {
			next_sector(mfm);
			mfm->status = STAT_BUSY;
			timer_clock();
			/* 390.625 us per sector at 10 Mbit/s = 1280 kB/s. */
			mfm->callback = (3125LL * TIMER_USEC) / 8LL;
			timer_update_outstanding();
		} else {
			ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 0);
		}
	}
    }

    return(temp);
}


static uint8_t
mfm_read(uint16_t port, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    uint8_t temp = 0xff;

    switch (port) {
	case 0x1f0:	/* data */
		temp = mfm_readw(port, mfm) & 0xff;
		break;

	case 0x1f1:	/* error */
		temp = mfm->error;
		break;

	case 0x1f2:	/* sector count */
		temp = mfm->secount;
		break;

	case 0x1f3:	/* sector */
		temp = mfm->sector;
		break;

	case 0x1f4:	/* cylinder low */
		temp = (uint8_t)(mfm->cylinder&0xff);
		break;

	case 0x1f5:	/* cylinder high */
		temp = (uint8_t)(mfm->cylinder>>8);
		break;

	case 0x1f6:	/* drive/Head */
		temp = (uint8_t)(0xa0|mfm->head|(mfm->drive_sel?0x10:0));
		break;

	case 0x1f7:	/* status */
		irq_lower(mfm);
		temp = mfm->status;
		break;
    }

    mfm_at_log("WD1007 read(%04x) = %02x\n", port, temp);

    return(temp);
}


static void
mfm_callback(void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    drive_t *drive = &mfm->drives[mfm->drive_sel];
    off64_t addr;

    mfm->callback = 0LL;
    if (mfm->reset) {
	mfm->status = STAT_READY|STAT_DSC;
	mfm->error = 1;
	mfm->secount = 1;
	mfm->sector = 1;
	mfm->head = 0;
	mfm->cylinder = 0;
	mfm->reset = 0;

	ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 0);

	return;
    }

    mfm_at_log("WD1007: command %02x\n", mfm->command);

    switch (mfm->command) {
	case CMD_RESTORE:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
		} else {
			drive->current_cylinder = 0;
			mfm->status = STAT_READY|STAT_DSC;
		}
		irq_raise(mfm);
		break;

	case CMD_SEEK:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
		} else {
			mfm->status = STAT_READY|STAT_DSC;
		}
		irq_raise(mfm);
		break;

	case CMD_READ:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
			irq_raise(mfm);
			break;
		}

		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY|STAT_DSC|STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_read(drive->hdd_num, addr, 1,
			      (uint8_t *)mfm->buffer);

		mfm->pos = 0;
		mfm->status = STAT_DRQ|STAT_READY|STAT_DSC;
		irq_raise(mfm);
		ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);
		break;

	case CMD_WRITE:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
			irq_raise(mfm);
			break;
		}

		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY|STAT_DSC|STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_write(drive->hdd_num, addr, 1,
				(uint8_t *)mfm->buffer);

		irq_raise(mfm);
		mfm->secount = (mfm->secount - 1) & 0xff;
		if (mfm->secount) {
			mfm->status = STAT_DRQ|STAT_READY|STAT_DSC;
			mfm->pos = 0;
			next_sector(mfm);
		} else {
			mfm->status = STAT_READY|STAT_DSC;
		}
		ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);
		break;

	case CMD_VERIFY:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
			irq_raise(mfm);
			break;
		}

		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY|STAT_DSC|STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_read(drive->hdd_num, addr, 1,
			      (uint8_t *)mfm->buffer);

		ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);
		next_sector(mfm);
		mfm->secount = (mfm->secount - 1) & 0xff;
		if (mfm->secount)
			mfm->callback = 6LL*HDC_TIME;
		else {
			mfm->pos = 0;
			mfm->status = STAT_READY|STAT_DSC;
			irq_raise(mfm);
		}
		break;

	case CMD_FORMAT:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
			irq_raise(mfm);
			break;
		}

		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY|STAT_DSC|STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_zero(drive->hdd_num, addr, mfm->secount);

		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);
		break;

	case CMD_DIAGNOSE:
		mfm->error = 1;	 /*no error detected*/
		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		break;

	case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
		if (drive->present == 0) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
			irq_raise(mfm);
			break;
		}

		drive->cfg_spt = mfm->secount;
		drive->cfg_hpc = mfm->head+1;

		mfm_at_log("WD1007: parameters: spt=%i hpc=%i\n", drive->cfg_spt,drive->cfg_hpc);

		if (! mfm->secount)
			fatal("WD1007: secount=0\n");
		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		break;

	case CMD_NOP:
		mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
		mfm->error = ERR_ABRT;
		irq_raise(mfm);
		break;

	case 0xe0:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
			irq_raise(mfm);
			break;
		}

		switch (mfm->cylinder >> 8) {
			case 0x31:
				mfm->cylinder = drive->real_tracks;
				break;

			case 0x33:
				mfm->cylinder = drive->real_hpc;
				break;

			case 0x35:
				mfm->cylinder = 0x200;
				break;

			case 0x36:
				mfm->cylinder = drive->real_spt;
				break;

			default:
				mfm_at_log("WD1007: bad read config %02x\n",
						mfm->cylinder >> 8);
		}
		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		break;

	case 0xa0:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
		} else {
			memset(mfm->buffer, 0x00, 512);
			memset(&mfm->buffer[3], 0xff, 512-6);
			mfm->pos = 0;
			mfm->status = STAT_DRQ|STAT_READY|STAT_DSC;
		}
		irq_raise(mfm);
		break;

	case CMD_READ_PARAMETERS:
		if (! drive->present) {
			mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
			mfm->error = ERR_ABRT;
			irq_raise(mfm);
			break;
		}

		memset(mfm->buffer, 0x00, 512);
		mfm->buffer[0] = 0x44;	/* general configuration */
		mfm->buffer[1] = drive->real_tracks; /* number of non-removable cylinders */
		mfm->buffer[2] = 0;	/* number of removable cylinders */
		mfm->buffer[3] = drive->real_hpc;    /* number of heads */
		mfm->buffer[4] = 600;	/* number of unformatted bytes/track */
		mfm->buffer[5] = mfm->buffer[4] * drive->real_spt; /* number of unformatted bytes/sector */
		mfm->buffer[6] = drive->real_spt; /* number of sectors */
		mfm->buffer[7] = 0;	/*minimum bytes in inter-sector gap*/
		mfm->buffer[8] = 0;	/* minimum bytes in postamble */
		mfm->buffer[9] = 0;	/* number of words of vendor status */
		/* controller info */
		mfm->buffer[20] = 2; 	/* controller type */
		mfm->buffer[21] = 1;	/* sector buffer size, in sectors */
		mfm->buffer[22] = 0;	/* ecc bytes appended */
		mfm->buffer[27] = 'W' | ('D' << 8);
		mfm->buffer[28] = '1' | ('0' << 8);
		mfm->buffer[29] = '0' | ('7' << 8);
		mfm->buffer[30] = 'V' | ('-' << 8);
		mfm->buffer[31] = 'S' | ('E' << 8);
		mfm->buffer[32] = '1';
		mfm->buffer[47] = 0;	/* sectors per interrupt */
		mfm->buffer[48] = 0;	/* can use double word read/write? */
		mfm->pos = 0;
		mfm->status = STAT_DRQ|STAT_READY|STAT_DSC;
		irq_raise(mfm);
		break;

	default:
		mfm_at_log("WD1007: callback on unknown command %02x\n", mfm->command);
		/*FALLTHROUGH*/

	case 0xe8:
		mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
		mfm->error = ERR_ABRT;
		irq_raise(mfm);
		break;
    }

    ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 0);
}


static void
loadhd(mfm_t *mfm, int hdd_num, int d, const wchar_t *fn)
{
    drive_t *drive = &mfm->drives[hdd_num];

    if (! hdd_image_load(d)) {
	mfm_at_log("WD1007: drive %d not present!\n", d);
	drive->present = 0;
	return;
    }

    drive->cfg_spt = drive->real_spt = hdd[d].spt;
    drive->cfg_hpc = drive->real_hpc = hdd[d].hpc;
    drive->real_tracks = hdd[d].tracks;
    drive->hdd_num = d;
    drive->present = 1;
}


static void *
wd1007vse1_init(const device_t *info)
{
    int c, d;

    mfm_t *mfm = malloc(sizeof(mfm_t));
    memset(mfm, 0x00, sizeof(mfm_t));

    c = 0;
    for (d=0; d<HDD_NUM; d++) {
	if ((hdd[d].bus==HDD_BUS_MFM) && (hdd[d].mfm_channel<MFM_NUM)) {
		loadhd(mfm, hdd[d].mfm_channel, d, hdd[d].fn);

		if (++c >= MFM_NUM) break;
	}
    }

    mfm->status = STAT_READY|STAT_DSC;
    mfm->error = 1;

    rom_init(&mfm->bios_rom,
	     BIOS_FILE, 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    io_sethandler(0x01f0, 1,
		  mfm_read, mfm_readw, NULL,
		  mfm_write, mfm_writew, NULL, mfm);
    io_sethandler(0x01f1, 7,
		  mfm_read, NULL, NULL,
		  mfm_write, NULL, NULL, mfm);
    io_sethandler(0x03f6, 1, NULL, NULL, NULL,
		  mfm_write, NULL, NULL, mfm);

    timer_add(mfm_callback, &mfm->callback, &mfm->callback, mfm);

    ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);

    return(mfm);
}


static void
wd1007vse1_close(void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    drive_t *drive;
    int d;

    for (d=0; d<2; d++) {
	drive = &mfm->drives[d];

	hdd_image_close(drive->hdd_num);
    }

    free(mfm);

    ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
}


static int
wd1007vse1_available(void)
{
    return(rom_present(BIOS_FILE));
}


const device_t mfm_at_wd1007vse1_device = {
    "Western Digital WD1007V-SE1 (MFM)",
    DEVICE_ISA | DEVICE_AT,
    0,
    wd1007vse1_init, wd1007vse1_close, NULL,
    wd1007vse1_available,
    NULL, NULL,
    NULL
};
