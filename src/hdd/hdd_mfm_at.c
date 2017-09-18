/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Driver for the IBM PC-AT MFM/RLL Fixed Disk controller.
 *
 *		This controller was a 16bit ISA card, and it used a WD1003
 *		based design. Most cards were WD1003-WA2 or -WAH, where the
 *		-WA2 cards had a floppy controller as well (to save space.)
 *
 * Version:	@(#)hdd_mfm_at.c	1.0.1	2017/09/17
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdlib.h>
#include "../ibm.h"
#include "../device.h"
#include "../io.h"
#include "../pic.h"
#include "../timer.h"
#include "hdd_image.h"
#include "hdd_mfm_at.h"


#define MFM_TIME		(TIMER_USEC*10)

#define STAT_ERR		0x01
#define STAT_INDEX		0x02
#define STAT_CORRECTED_DATA	0x04
#define STAT_DRQ		0x08	/* data request */
#define STAT_DSC		0x10
#define STAT_SEEK_COMPLETE	0x20
#define STAT_READY		0x40
#define STAT_BUSY		0x80

#define ERR_DAM_NOT_FOUND	0x01	/* Data Address Mark not found */
#define ERR_TR000		0x02	/* track 0 not found */
#define ERR_ABRT		0x04	/* command aborted */
#define ERR_ID_NOT_FOUND	0x10	/* ID not found */
#define ERR_DATA_CRC		0x40	/* data CRC error */
#define ERR_BAD_BLOCK		0x80	/* bad block detected */

#define CMD_RESTORE		0x10
#define CMD_READ		0x20
#define CMD_WRITE		0x30
#define CMD_VERIFY		0x40
#define CMD_FORMAT		0x50
#define CMD_SEEK		0x70
#define CMD_DIAGNOSE		0x90
#define CMD_SET_PARAMETERS	0x91


typedef struct {
    int	present;
    int	hdc_num;
    int	spt, hpc;
    int	tracks;
    int	cfg_spt;
    int	cfg_hpc;
    int	current_cylinder;
} mfm_drive_t;


typedef struct {
    uint8_t	status;
    uint8_t	error;
    uint8_t	secount,
		sector,
		head;
    uint16_t	cylinder,
		cylprecomp;
    uint8_t	command;
    uint8_t	fdisk;

    int		pos;
    int		drive_sel;
    int		reset;
    int		irqstat;
    int		callback;

    uint16_t	buffer[256];

    mfm_drive_t drives[MFM_NUM];
} mfm_t;


static __inline void irq_raise(mfm_t *mfm)
{
    if (!(mfm->fdisk&2))
	picint(1 << 14);

    mfm->irqstat=1;
}


static __inline void irq_lower(mfm_t *mfm)
{
    picintc(1 << 14);
}


static void
irq_update(mfm_t *mfm)
{
    if (mfm->irqstat && !((pic2.pend|pic2.ins)&0x40) && !(mfm->fdisk & 2))
		picint(1 << 14);
}


/* Return the sector offset for the current register values. */
static int
get_sector(mfm_t *mfm, off64_t *addr)
{
    mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];

    if (drive->current_cylinder != mfm->cylinder) {
	pclog("WD1003(%d) sector: wrong cylinder\n");
	return(1);
    }

    if (mfm->head > drive->cfg_hpc) {
	pclog("WD1003(%d) get_sector: past end of configured heads\n",
						mfm->drive_sel);
	return(1);
    }

    if (mfm->sector >= drive->cfg_spt+1) {
	pclog("WD1003(%d) get_sector: past end of configured sectors\n",
							mfm->drive_sel);
	return(1);
    }

#if 1
    /* We should check this in the SET_DRIVE_PARAMETERS command!  --FvK */
    if (mfm->head > drive->hpc) {
	pclog("WD1003(%d) get_sector: past end of heads\n", mfm->drive_sel);
	return(1);
    }

    if (mfm->sector >= drive->spt+1) {
	pclog("WD1003(%d) get_sector: past end of sectors\n", mfm->drive_sel);
	return(1);
    }
#endif

    *addr = ((((off64_t) mfm->cylinder * drive->cfg_hpc) + mfm->head) *
			 drive->cfg_spt) + (mfm->sector - 1);

    return(0);
}


/* Move to the next sector using CHS addressing. */
static void
next_sector(mfm_t *mfm)
{
    mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];

    mfm->sector++;
    if (mfm->sector == (drive->cfg_spt+1)) {
	mfm->sector = 1;
	mfm->head++;
	if (mfm->head == drive->cfg_hpc) {
		mfm->head = 0;
		mfm->cylinder++;
		if (drive->current_cylinder < drive->tracks)
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
	timer_process();
	mfm->callback = 6*MFM_TIME;
	timer_update_outstanding();
    }
}


static void
mfm_write(uint16_t port, uint8_t val, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;

#if 0
    pclog("WD1003 write(%04x, %02x)\n", port, val);
#endif
    switch (port) {
	case 0x1f0:	/* data */
		mfm_writew(port, val | (val << 8), priv);
		return;

	case 0x1f1:	/* write precompenstation */
		mfm->cylprecomp = val;
		return;

	case 0x1f2:	/* sector count */
		mfm->secount = val;
		return;

	case 0x1f3:	/* sector */
		mfm->sector = val;
		return;

	case 0x1f4:	/* cylinder low */
		mfm->cylinder = (mfm->cylinder & 0xff00) | val;
		return;

	case 0x1f5:	/* cylinder high */
		mfm->cylinder = (mfm->cylinder & 0xff) | (val << 8);
		return;

	case 0x1f6:	/* drive/head */
		mfm->head = val & 0xF;
		mfm->drive_sel = (val & 0x10) ? 1 : 0;
		if (mfm->drives[mfm->drive_sel].present)
			mfm->status = STAT_READY | STAT_DSC;
		  else
			mfm->status = 0;
		return;

	case 0x1f7:	/* command register */
		if (! mfm->drives[mfm->drive_sel].present) {
			/*
			 * We should not panic on this, as some systems
			 * (like several old UNIX systems like Microport
			 * SystemV/AT) poll both drives...
			 */
			pclog("WD1003(%d) command %02x on non-present drive\n",
							mfm->drive_sel, val);
			mfm->command = 0xff;
			mfm->status = STAT_BUSY;
			timer_process();
			mfm->callback = 200*MFM_TIME;
			timer_update_outstanding();
			return;
		}

		irq_lower(mfm);
		mfm->command = val;
		mfm->error = 0;

		switch (val & 0xf0) {
			case CMD_RESTORE:
				mfm->command &= ~0x0f; /* mask off step rate */
				mfm->status = STAT_BUSY;
				timer_process();
				mfm->callback = 200*MFM_TIME;
				timer_update_outstanding();
				break;

			case CMD_SEEK:
				mfm->command &= ~0x0f; /* mask off step rate */
				mfm->status = STAT_BUSY;
				timer_process();
				mfm->callback = 200*MFM_TIME;
				timer_update_outstanding();
				break;

			default:
				switch (val) {
					case CMD_READ:
					case CMD_READ+1:
					case CMD_READ+2:
					case CMD_READ+3:
						mfm->command &= ~0x03;
						if (val & 2)
						    fatal("WD1003: READ with ECC\n");
						mfm->status = STAT_BUSY;
						timer_process();
						mfm->callback = 200*MFM_TIME;
						timer_update_outstanding();
						break;

					case CMD_WRITE:
					case CMD_WRITE+1:
					case CMD_WRITE+2:
					case CMD_WRITE+3:
						mfm->command &= ~0x03;
						if (val & 2)
						    fatal("WD1003: WRITE with ECC\n");
						mfm->status = STAT_DRQ|STAT_DSC;
						mfm->pos = 0;
						break;

					case CMD_VERIFY:
					case CMD_VERIFY+1:
						mfm->command &= ~0x01;
						mfm->status = STAT_BUSY;
						timer_process();
						mfm->callback = 200 * MFM_TIME;
						timer_update_outstanding();
						break;

					case CMD_FORMAT:
						mfm->status =STAT_DRQ|STAT_BUSY;
						mfm->pos = 0;
						break;

					case CMD_SET_PARAMETERS:
						mfm->status = STAT_BUSY;
						timer_process();
						mfm->callback = 30*MFM_TIME;
						timer_update_outstanding();
						break;

					case CMD_DIAGNOSE:
						mfm->status = STAT_BUSY;
						timer_process();
						mfm->callback = 200*MFM_TIME;
						timer_update_outstanding();
						break;

					default:
						pclog("WD1003: bad command %02X\n", val);
						mfm->status = STAT_BUSY;
						timer_process();
						mfm->callback = 200*MFM_TIME;
						timer_update_outstanding();
						break;
				}
		}
		break;

	case 0x3f6:	/* device control */
		if ((mfm->fdisk & 0x04) && !(val & 0x04)) {
			mfm->reset = 1;
			mfm->status = STAT_BUSY;
			timer_process();
			mfm->callback = 500*MFM_TIME;
			timer_update_outstanding();
		}

		if (val & 0x04) {
			/* Drive held in reset. */
			mfm->status = STAT_BUSY;
			mfm->callback = 0;
			timer_process();
			timer_update_outstanding();
		}

		mfm->fdisk = val;
		irq_update(mfm);
		break;
    }
}


static uint16_t
mfm_readw(uint16_t port, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    uint16_t ret;

    ret = mfm->buffer[mfm->pos >> 1];
    mfm->pos += 2;
    if (mfm->pos >= 512) {
	mfm->pos=0;
	mfm->status = STAT_READY | STAT_DSC;
	if (mfm->command == CMD_READ) {
		mfm->secount = (mfm->secount - 1) & 0xff;
		if (mfm->secount) {
			next_sector(mfm);
			mfm->status = STAT_BUSY;
			timer_process();
			mfm->callback = 6*MFM_TIME;
			timer_update_outstanding();
		} else {
			update_status_bar_icon(SB_HDD | HDD_BUS_MFM, 0);
		}
	}
    }

    return(ret);
}


static uint8_t
mfm_read(uint16_t port, void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x1f0:	/* data */
		ret = mfm_readw(port, mfm) & 0xff;
		break;

	case 0x1f1:	/* error */
		ret = mfm->error;
		break;

	case 0x1f2:	/* sector count */
		ret = mfm->secount;
		break;

	case 0x1f3:	/* sector */
		ret = mfm->sector;
		break;

	case 0x1f4:	/* CYlinder low */
		ret = (uint8_t)(mfm->cylinder&0xff);
		break;

	case 0x1f5:	/* Cylinder high */
		ret = (uint8_t)(mfm->cylinder>>8);
		break;

	case 0x1f6:	/* drive/head */
		ret = (uint8_t)(0xa0 | mfm->head | (mfm->drive_sel?0x10:0));
		break;

	case 0x1f7:	/* Status */
		irq_lower(mfm);
		ret = mfm->status;
		break;

	default:
		break;
    }
#if 0
    pclog("WD1003 read(%04x) = %02x\n", port, ret);
#endif

    return(ret);
}


static void
do_seek(mfm_t *mfm)
{
    mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];

    if (mfm->cylinder < drive->tracks)
	drive->current_cylinder = mfm->cylinder;
      else
	drive->current_cylinder = drive->tracks-1;
}


static void
do_callback(void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    mfm_drive_t *drive = &mfm->drives[mfm->drive_sel];
    off64_t addr;

    mfm->callback = 0;
    if (mfm->reset) {
	mfm->status = STAT_READY | STAT_DSC;
	mfm->error = 1;
	mfm->secount = 1;
	mfm->sector = 1;
	mfm->head = 0;
	mfm->cylinder = 0;
	drive->cfg_spt = 0;	/* we need new parameters after reset! */
	mfm->reset = 0;
	return;
    }

    switch (mfm->command) {
	case CMD_RESTORE:
		drive->current_cylinder = 0;
		mfm->status = STAT_READY | STAT_DSC;
		irq_raise(mfm);
		break;

	case CMD_SEEK:
		do_seek(mfm);
		mfm->status = STAT_READY | STAT_DSC;
		irq_raise(mfm);
		break;

	case CMD_READ:
		do_seek(mfm);
		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_read(drive->hdc_num, addr, 1,(uint8_t *)mfm->buffer);

		mfm->pos = 0;
		mfm->status = STAT_DRQ | STAT_READY | STAT_DSC;
		irq_raise(mfm);
		update_status_bar_icon(SB_HDD | HDD_BUS_MFM, 1);
		break;

	case CMD_WRITE:
		do_seek(mfm);
		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_write(drive->hdc_num, addr, 1,(uint8_t *)mfm->buffer);

		irq_raise(mfm);
		mfm->secount = (mfm->secount - 1) & 0xff;
		if (mfm->secount) {
			mfm->status = STAT_DRQ | STAT_READY | STAT_DSC;
			mfm->pos = 0;
			next_sector(mfm);
			update_status_bar_icon(SB_HDD | HDD_BUS_MFM, 1);
		} else {
			mfm->status = STAT_READY | STAT_DSC;
			update_status_bar_icon(SB_HDD | HDD_BUS_MFM, 0);
		}
		break;

	case CMD_VERIFY:
		do_seek(mfm);
		mfm->pos = 0;
		mfm->status = STAT_READY | STAT_DSC;
		irq_raise(mfm);
		update_status_bar_icon(SB_HDD | HDD_BUS_MFM, 1);
		break;

	case CMD_FORMAT:
		do_seek(mfm);
		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_zero(drive->hdc_num, addr, mfm->secount);

		mfm->status = STAT_READY | STAT_DSC;
		irq_raise(mfm);
		update_status_bar_icon(SB_HDD | HDD_BUS_MFM, 1);
		break;

	case CMD_DIAGNOSE:
		mfm->error = 1; /*No error detected*/
		mfm->status = STAT_READY | STAT_DSC;
		irq_raise(mfm);
		break;

	case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
		/*
		 * NOTE:
		 *
		 * We currently just set these parameters, and never
		 * bother to check if they "fit within" the actual
		 * parameters, as determined by the image loader.
		 *
		 * The difference in parameters is OK, and occurs
		 * when the BIOS or operating system decides to use
		 * a different translation scheme, but either way,
		 * it SHOULD always fit within the actual parameters!
		 *
		 * We SHOULD check that here!!  --FvK
		 */
		if (drive->cfg_spt == 0) {
			drive->cfg_spt = mfm->secount;
			drive->cfg_hpc = mfm->head+1;
			pclog("WD1003(%d) parameters: tracks=%d, spt=%i, hpc=%i\n",
				mfm->drive_sel,drive->tracks,
				drive->cfg_spt,drive->cfg_hpc);
		} else {
			/*
			 * For debugging the weirdness that happens
			 * while trying to install Microport SysV/AT,
			 * which issues several calls with changing
			 * parameters.  --FvK
			 */
			pclog("WD1003(%d) parameters: tracks=%d,spt=%i,hpc=%i (IGNORED)\n",
				mfm->drive_sel,drive->tracks,drive->cfg_spt,drive->cfg_hpc);
		}
		mfm->status = STAT_READY | STAT_DSC;
		irq_raise(mfm);
		break;

	default:
		pclog("WD1003(%d) callback on unknown command %02x\n",
					mfm->drive_sel, mfm->command);
		mfm->status = STAT_READY | STAT_ERR | STAT_DSC;
		mfm->error = ERR_ABRT;
		irq_raise(mfm);
		break;
    }
}


static void
loadhd(mfm_t *mfm, int c, int d, const wchar_t *fn)
{
    mfm_drive_t *drive = &mfm->drives[c];

    if (! hdd_image_load(d)) {
	drive->present = 0;
	return;
    }

    drive->spt = hdc[d].spt;
    drive->hpc = hdc[d].hpc;
    drive->tracks = hdc[d].tracks;
    drive->hdc_num = d;
    drive->present = 1;
}


static void *
mfm_init(void)
{
    mfm_t *mfm;
    int c, d;

    pclog("WD1003: ISA MFM/RLL Fixed Disk Adapter initializing ...\n");
    mfm = malloc(sizeof(mfm_t));
    memset(mfm, 0x00, sizeof(mfm_t));

    c = 0;
    for (d=0; d<HDC_NUM; d++) {
	if ((hdc[d].bus == HDD_BUS_MFM) && (hdc[d].mfm_channel < MFM_NUM)) {
		pclog("WD1003(%d): (%S) on disk %d\n", c, hdc[d].fn, d);
		loadhd(mfm, hdc[d].mfm_channel, d, hdc[d].fn);
		c++;
		if (c >= MFM_NUM)  break;
	}
    }

    mfm->status = STAT_READY | STAT_DSC;	/* drive is ready */
    mfm->error = 1;				/* no errors */

    io_sethandler(0x01f0, 1,
		  mfm_read, mfm_readw, NULL, mfm_write, mfm_writew, NULL, mfm);
    io_sethandler(0x01f1, 7,
		  mfm_read, NULL,      NULL, mfm_write, NULL,       NULL, mfm);
    io_sethandler(0x03f6, 1,
		  NULL,     NULL,      NULL, mfm_write, NULL,       NULL, mfm);

    timer_add(do_callback, &mfm->callback, &mfm->callback, mfm);	

    return(mfm);
}


static void
mfm_close(void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    int d;

    for (d=0; d<2; d++) {
	mfm_drive_t *drive = &mfm->drives[d];

	hdd_image_close(drive->hdc_num);		
    }

    free(mfm);
}


device_t mfm_at_device =
{
    "WD1003 AT MFM/RLL Controller",
    DEVICE_AT,
    mfm_init,
    mfm_close,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
