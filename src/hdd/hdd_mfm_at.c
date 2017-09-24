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
 * Version:	@(#)hdd_mfm_at.c	1.0.2	2017/09/23
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
#define MFM_DEBUG		0

#define STAT_ERR		0x01
#define STAT_INDEX		0x02
#define STAT_ECC		0x04
#define STAT_DRQ		0x08	/* data request */
#define STAT_DSC		0x10
#define STAT_WRFLT		0x20
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
    int8_t	present,		/* drive is present */
		hdc_num,		/* drive number in system */
		steprate,		/* current servo step rate */
		spt,			/* physical #sectors per track */
		hpc,			/* physical #heads per cylinder */
		pad;
    int16_t	tracks;			/* physical #tracks per cylinder */

    int8_t	cfg_spt,		/* configured #sectors per track */
		cfg_hpc;		/* configured #heads per track */

    int16_t	curcyl;			/* current track number */
} mfm_drive_t;


typedef struct {
    uint8_t	precomp,		/* 1: precomp/error register */
		error,
		secount,		/* 2: sector count register */
		sector,			/* 3: sector number */
		head,			/* 6: head number + drive select */
		command,		/* 7: command/status */
		status,
		fdisk;			/* 8: control register */
    uint16_t	cylinder;		/* 4/5: cylinder LOW and HIGH */

    int8_t	reset,			/* controller in reset */
		irqstat,		/* current IRQ status */
		drvsel,			/* current selected drive */
		pad;

    int		pos;			/* offset within data buffer */
    int		callback;		/* callback delay timer */

    uint16_t	buffer[256];		/* data buffer (16b wide) */

    mfm_drive_t drives[MFM_NUM];	/* attached drives */
} mfm_t;


static __inline void irq_raise(mfm_t *mfm)
{
    /* If not already pending.. */
    if (! mfm->irqstat) {
	/* If enabled in the control register.. */
	if (! (mfm->fdisk&0x02)) {
		/* .. raise IRQ14. */
		picint(1<<14);
	}

	/* Remember this. */
	mfm->irqstat = 1;
    }
}


static __inline void irq_lower(mfm_t *mfm)
{
    /* If raised.. */
    if (mfm->irqstat) {
	/* If enabled in the control register.. */
	if (! (mfm->fdisk&0x02)) {
		/* .. drop IRQ14. */
		picintc(1<<14);
	}

	/* Remember this. */
	mfm->irqstat = 0;
    }
}


/*
 * Return the sector offset for the current register values.
 *
 * According to the WD1002/WD1003 technical reference manual,
 * this is not done entirely correct. It specifies that the
 * parameters set with the SET_DRIVE_PARAMETERS command are
 * to be used only for multi-sector operations, and that any
 * such operation can only be executed AFTER these parameters
 * have been set. This would imply that for regular single
 * transfers, the controller uses (or, can use) the actual
 * geometry information...
 */
static int
get_sector(mfm_t *mfm, off64_t *addr)
{
    mfm_drive_t *drive = &mfm->drives[mfm->drvsel];

    if (drive->curcyl != mfm->cylinder) {
	pclog("WD1003(%d) sector: wrong cylinder\n");
	return(1);
    }

    if (mfm->head > drive->cfg_hpc) {
	pclog("WD1003(%d) get_sector: past end of configured heads\n",
							mfm->drvsel);
	return(1);
    }

    if (mfm->sector >= drive->cfg_spt+1) {
	pclog("WD1003(%d) get_sector: past end of configured sectors\n",
							mfm->drvsel);
	return(1);
    }

#if 1
    /* We should check this in the SET_DRIVE_PARAMETERS command!  --FvK */
    if (mfm->head > drive->hpc) {
	pclog("WD1003(%d) get_sector: past end of heads\n", mfm->drvsel);
	return(1);
    }

    if (mfm->sector >= drive->spt+1) {
	pclog("WD1003(%d) get_sector: past end of sectors\n", mfm->drvsel);
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
    mfm_drive_t *drive = &mfm->drives[mfm->drvsel];

    if (++mfm->sector == (drive->cfg_spt+1)) {
	mfm->sector = 1;
	if (++mfm->head == drive->cfg_hpc) {
		mfm->head = 0;
		mfm->cylinder++;
		if (drive->curcyl < drive->tracks)
			drive->curcyl++;
	}
    }
}


static void
mfm_cmd(mfm_t *mfm, uint8_t val)
{
    mfm_drive_t *drive = &mfm->drives[mfm->drvsel];

    if (! drive->present) {
	/* This happens if sofware polls all drives. */
	pclog("WD1003(%d) command %02x on non-present drive\n",
					mfm->drvsel, val);
	mfm->command = 0xff;
	mfm->status = STAT_BUSY;
	timer_process();
	mfm->callback = 200*MFM_TIME;
	timer_update_outstanding();

	return;
    }

    irq_lower(mfm);
    mfm->error = 0;

    switch (val & 0xf0) {
	case CMD_RESTORE:
		drive->steprate = (val & 0x0f);
#if MFM_DEBUG
		pclog("WD1003(%d) restore, step=%d\n",
			mfm->drvsel, drive->steprate);
#endif
		drive->curcyl = 0;
		mfm->status = STAT_READY|STAT_DSC;
		mfm->command = 0x00;
		irq_raise(mfm);
		break;

	case CMD_SEEK:
		drive->steprate = (val & 0x0f);
		mfm->command = (val & 0xf0);
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
#if MFM_DEBUG
				pclog("WD1003(%d) read, opt=%d\n",
					mfm->drvsel, val&0x03);
#endif
				mfm->command = (val & 0xf0);
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
#if MFM_DEBUG
				pclog("WD1003(%d) write, opt=%d\n",
					mfm->drvsel, val & 0x03);
#endif
				mfm->command = (val & 0xf0);
				if (val & 2)
					fatal("WD1003: WRITE with ECC\n");
				mfm->status = STAT_DRQ|STAT_DSC;
				mfm->pos = 0;
				break;

			case CMD_VERIFY:
			case CMD_VERIFY+1:
				mfm->command = (val & 0xfe);
				mfm->status = STAT_BUSY;
				timer_process();
				mfm->callback = 200*MFM_TIME;
				timer_update_outstanding();
				break;

			case CMD_FORMAT:
				mfm->command = val;
				mfm->status = STAT_DRQ|STAT_BUSY;
				mfm->pos = 0;
				break;

			case CMD_DIAGNOSE:
				mfm->command = val;
				mfm->status = STAT_BUSY;
				timer_process();
				mfm->callback = 200*MFM_TIME;
				timer_update_outstanding();
				break;

			case CMD_SET_PARAMETERS:
				/*
				 * NOTE:
				 *
				 * We currently just set these parameters, and
				 * never bother to check if they "fit within"
				 * the actual parameters, as determined by the
				 * image loader.
				 *
				 * The difference in parameters is OK, and
				 * occurs when the BIOS or operating system
				 * decides to use a different translation
				 * scheme, but either way, it SHOULD always
				 * fit within the actual parameters!
				 *
				 * We SHOULD check that here!! --FvK
				 */
				if (drive->cfg_spt == 0) {
					/* Only accept after RESET or DIAG. */
					drive->cfg_spt = mfm->secount;
					drive->cfg_hpc = mfm->head+1;
					pclog("WD1003(%d) parameters: tracks=%d, spt=%i, hpc=%i\n",
						mfm->drvsel, drive->tracks,
						drive->cfg_spt, drive->cfg_hpc);
				} else {
					pclog("WD1003(%d) parameters: tracks=%d,spt=%i,hpc=%i (IGNORED)\n",
						mfm->drvsel, drive->tracks,
						drive->cfg_spt, drive->cfg_hpc);
				}
				mfm->command = 0x00;
				mfm->status = STAT_READY|STAT_DSC;
				mfm->error = 1;
				irq_raise(mfm);
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

#if MFM_DEBUG > 1
    pclog("WD1003 write(%04x, %02x)\n", port, val);
#endif
    switch (port) {
	case 0x01f0:	/* data */
		mfm_writew(port, val | (val << 8), priv);
		return;

	case 0x01f1:	/* write precompenstation */
		mfm->precomp = val;
		return;

	case 0x01f2:	/* sector count */
		mfm->secount = val;
		return;

	case 0x01f3:	/* sector */
		mfm->sector = val;
		return;

	case 0x01f4:	/* cylinder low */
		mfm->cylinder = (mfm->cylinder & 0xff00) | val;
		return;

	case 0x01f5:	/* cylinder high */
		mfm->cylinder = (mfm->cylinder & 0xff) | (val << 8);
		return;

	case 0x01f6:	/* drive/head */
		mfm->head = val & 0xF;
		mfm->drvsel = (val & 0x10) ? 1 : 0;
		if (mfm->drives[mfm->drvsel].present)
			mfm->status = STAT_READY|STAT_DSC;
		  else
			mfm->status = 0;
		return;

	case 0x01f7:	/* command register */
		mfm_cmd(mfm, val);
		break;

	case 0x03f6:	/* device control */
		val &= 0x0f;
		if ((mfm->fdisk & 0x04) && !(val & 0x04)) {
			mfm->status = STAT_BUSY;
			mfm->reset = 1;
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
	mfm->pos = 0;
	mfm->status = STAT_READY|STAT_DSC;
	if (mfm->command == CMD_READ) {
		mfm->secount = (mfm->secount - 1) & 0xff;
		if (mfm->secount) {
			next_sector(mfm);
			mfm->status = STAT_BUSY;
			timer_process();
			mfm->callback = 6*MFM_TIME;
			timer_update_outstanding();
		} else {
			update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 0);
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
	case 0x01f0:	/* data */
		ret = mfm_readw(port, mfm) & 0xff;
		break;

	case 0x01f1:	/* error */
		ret = mfm->error;
		break;

	case 0x01f2:	/* sector count */
		ret = mfm->secount;
		break;

	case 0x01f3:	/* sector */
		ret = mfm->sector;
		break;

	case 0x01f4:	/* CYlinder low */
		ret = (uint8_t)(mfm->cylinder&0xff);
		break;

	case 0x01f5:	/* Cylinder high */
		ret = (uint8_t)(mfm->cylinder>>8);
		break;

	case 0x01f6:	/* drive/head */
		ret = (uint8_t)(0xa0 | mfm->head | (mfm->drvsel?0x10:0));
		break;

	case 0x01f7:	/* Status */
		irq_lower(mfm);
		ret = mfm->status;
		break;

	default:
		break;
    }
#if MFM_DEBUG > 1
    pclog("WD1003 read(%04x) = %02x\n", port, ret);
#endif

    return(ret);
}


static void
do_seek(mfm_t *mfm)
{
    mfm_drive_t *drive = &mfm->drives[mfm->drvsel];

#if MFM_DEBUG
    pclog("WD1003(%d) seek(%d) max=%d\n",
	mfm->drvsel,mfm->cylinder,drive->tracks);
#endif
    if (mfm->cylinder < drive->tracks)
	drive->curcyl = mfm->cylinder;
      else
	drive->curcyl = drive->tracks-1;
}


static void
do_callback(void *priv)
{
    mfm_t *mfm = (mfm_t *)priv;
    mfm_drive_t *drive = &mfm->drives[mfm->drvsel];
    off64_t addr;

    mfm->callback = 0;
    if (mfm->reset) {
#if MFM_DEBUG
	pclog("WD1003(%d) reset\n", mfm->drvsel);
#endif
	mfm->status = STAT_READY|STAT_DSC;
	mfm->error = 1;
	mfm->secount = 1;
	mfm->sector = 1;
	mfm->head = 0;
	mfm->cylinder = 0;

	drive->steprate = 0x0f;		/* default steprate */
	drive->cfg_spt = 0;		/* need new parameters */

	mfm->reset = 0;

	update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 0);

	return;
    }

    switch (mfm->command) {
	case CMD_SEEK:
#if MFM_DEBUG
		pclog("WD1003(%d) seek, step=%d\n",
			mfm->drvsel, drive->steprate);
#endif
		do_seek(mfm);
		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		break;

	case CMD_READ:
#if MFM_DEBUG
		pclog("WD1003(%d) read(%d,%d,%d)\n",
			mfm->drvsel, mfm->cylinder, mfm->head, mfm->sector);
#endif
		do_seek(mfm);
		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY|STAT_DSC|STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_read(drive->hdc_num, addr, 1, (uint8_t *)mfm->buffer);

		mfm->pos = 0;
		mfm->status = STAT_DRQ|STAT_READY|STAT_DSC;
		irq_raise(mfm);
		update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 1);
		break;

	case CMD_WRITE:
#if MFM_DEBUG
		pclog("WD1003(%d) write(%d,%d,%d)\n",
			mfm->drvsel, mfm->cylinder, mfm->head, mfm->sector);
#endif
		do_seek(mfm);
		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY|STAT_DSC|STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_write(drive->hdc_num, addr, 1,(uint8_t *)mfm->buffer);

		mfm->status = STAT_READY|STAT_DSC;
		mfm->secount = (mfm->secount - 1) & 0xff;
		if (mfm->secount) {
			/* More sectors to do.. */
			mfm->status |= STAT_DRQ;
			mfm->pos = 0;
			next_sector(mfm);
			update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 1);
		} else {
			update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 0);
		}
		irq_raise(mfm);
		break;

	case CMD_VERIFY:
#if MFM_DEBUG
		pclog("WD1003(%d) verify(%d,%d,%d)\n",
			mfm->drvsel, mfm->cylinder, mfm->head, mfm->sector);
#endif
		do_seek(mfm);
		mfm->pos = 0;
		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 1);
		break;

	case CMD_FORMAT:
#if MFM_DEBUG
		pclog("WD1003(%d) format(%d,%d)\n",
			mfm->drvsel, mfm->cylinder, mfm->head);
#endif
		do_seek(mfm);
		if (get_sector(mfm, &addr)) {
			mfm->error = ERR_ID_NOT_FOUND;
			mfm->status = STAT_READY|STAT_DSC|STAT_ERR;
			irq_raise(mfm);
			break;
		}

		hdd_image_zero(drive->hdc_num, addr, mfm->secount);

		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 1);
		break;

	case CMD_DIAGNOSE:
#if MFM_DEBUG
		pclog("WD1003(%d) diag\n", mfm->drvsel);
#endif
		drive->steprate = 0x0f;
		mfm->error = 1;
		mfm->status = STAT_READY|STAT_DSC;
		irq_raise(mfm);
		break;

	default:
		pclog("WD1003(%d) callback on unknown command %02x\n",
					mfm->drvsel, mfm->command);
		mfm->status = STAT_READY|STAT_ERR|STAT_DSC;
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
		loadhd(mfm, hdc[d].mfm_channel, d, hdc[d].fn);

		pclog("WD1003(%d): (%S) geometry %d/%d/%d\n", c, hdc[d].fn,
			(int)hdc[d].tracks, (int)hdc[d].hpc, (int)hdc[d].spt);

		if (++c >= MFM_NUM) break;
	}
    }

    mfm->status = STAT_READY|STAT_DSC;		/* drive is ready */
    mfm->error = 1;				/* no errors */

    io_sethandler(0x01f0, 1,
		  mfm_read, mfm_readw, NULL, mfm_write, mfm_writew, NULL, mfm);
    io_sethandler(0x01f1, 7,
		  mfm_read, NULL,      NULL, mfm_write, NULL,       NULL, mfm);
    io_sethandler(0x03f6, 1,
		  NULL,     NULL,      NULL, mfm_write, NULL,       NULL, mfm);

    timer_add(do_callback, &mfm->callback, &mfm->callback, mfm);	

    update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 0);

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

    update_status_bar_icon(SB_HDD|HDD_BUS_MFM, 0);
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
