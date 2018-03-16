/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the EuroPC HD20 internal controller.
 *
 *		The HD20 was an externally-connected drive, very often a
 *		8425XT (20MB, 615/4/17) from Miniscribe. These drives used
 *		an 8-bit version of IDE called X-IDE, also known as XTA.
 *		Some older units had a 8225XT drive (20MB, 771/2/17.)
 *
 *		To access the HD disk formatter, enter the "debug" program
 *		in DOS, and type "g=f000:a000" to start that utility, which
 *		is hidden in the PC's ROM BIOS.
 *
 *		This driver is based on the information found in the IBM-PC
 *		Technical Reference manual, pp 187 and on.
 *
 *		Based on the original "xebec.c" from Sarah Walker.
 *
 * Version:	@(#)m_europc_hdc.c	1.0.2	2018/03/11
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 *		Copyright 2008-2017 Sarah Walker.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../device.h"
#include "../timer.h"
#include "../disk/hdc.h"
#include "../disk/hdd.h"
#include "../plat.h"
#include "../ui.h"
#include "machine.h"


#define HDC_DEBUG	0
#define HDC_NEWPARAMS	1			/* use NEW parameter block */

#define HDD_IOADDR	0x0320
#define HDD_IRQCHAN	5
#define HDD_DMACHAN	3


#define HDC_TIME	(200*TIMER_USEC)


enum {
    STATE_IDLE,
    STATE_CMD,
    STATE_RUN,
    STATE_RXDTA,
    STATE_RDATA,
    STATE_TXDTA,
    STATE_TDATA,
    STATE_COMPL
};


/* Command values. */
#define CMD_TEST_DRV_RDY	0x00
#define CMD_RECALIBRATE		0x01
		/* unused	0x02 */
#define CMD_READ_SENSE		0x03
#define CMD_FORMAT_DRIVE	0x04
#define CMD_READY_VERIFY	0x05
#define CMD_FORMAT_TRACK	0x06
#define CMD_FORMAT_BAD_TRACK	0x07
#define CMD_READ_SECTORS	0x08
		/* unused	0x09 */
#define CMD_WRITE_SECTORS	0x0a
#define CMD_SEEK		0x0b
#define CMD_SET_DRIVE_PARAMS	0x0c
#define CMD_READ_ECC_BURST	0x0d
#define CMD_READ_SECTOR_BUFFER	0x0e
#define CMD_WRITE_SECTOR_BUFFER	0x0f
#define CMD_RAM_DIAGS		0xe0
		/* unused	0xe1 */
		/* unused	0xe2 */
#define CMD_DRIVE_DIAGS		0xe3
#define CMD_CTRL_DIAGS		0xe4
#define CMD_READ_LONG		0xe5
#define CMD_WRITE_LONG		0xe6

/* STATUS register values. */
#define STAT_REQ		0x01
#define STAT_IO			0x02
#define STAT_CD			0x04
#define STAT_BSY		0x08
#define STAT_DRQ		0x10
#define STAT_IRQ		0x20

/* Sense Error codes. */
#define ERR_NOERROR		0x00	/* no error detected */
#define ERR_NOINDEX		0x01	/* drive did not detect IDX pulse */
#define ERR_NOSEEK		0x02	/* drive did not complete SEEK */
#define ERR_WRFAULT		0x03	/* write fault during last cmd */
#define ERR_NOTRDY		0x04	/* drive did not go READY after cmd */
#define ERR_NOTRK000		0x06	/* drive did not see TRK0 signal */
#define ERR_LONGSEEK		0x08	/* long seek in progress */
#define ERR_IDREAD		0x10	/* ECC error during ID field */
#define ERR_DATA		0x11	/* uncorrectable ECC err in data */
#define ERR_NOMARK		0x12	/* no address mark detected */
#define ERR_NOSECT		0x14	/* sector not found */
#define ERR_SEEK		0x15	/* seek error */
#define ERR_ECCDATA		0x18	/* ECC corrected data */
#define ERR_BADTRK		0x19	/* bad track detected */
#define ERR_ILLCMD		0x20	/* invalid command received */
#define ERR_ILLADDR		0x21	/* invalid disk address received */
#define ERR_BADRAM		0x30	/* bad RAM in sector data buffer */
#define ERR_BADROM		0x31	/* bad checksum in ROM test */
#define ERR_BADECC		0x32	/* ECC polynomial generator bad */

/* Completion Byte fields. */
#define COMP_DRIVE		0x20
#define COMP_ERR		0x02

#define IRQ_ENA			0x02
#define DMA_ENA			0x01


/* The device control block (6 bytes) */
#pragma pack(push,1)
struct dcb {
    uint8_t	cmd;		/* [7:5] class, [4:0] opcode	*/
    uint8_t	head:5,		/* [4:0] head number		*/
		drvsel:1,	/* [5] drive select		*/
		unused:2;	/* [7:6] unused MBZ		*/
    uint8_t	sector:6,	/* [5:0] sector number 0-63	*/
		cylh:2;		/* [7:6] cylinder [9:8] bits	*/
    uint8_t	cyl;		/* [7:0] cylinder [7:0] bits	*/
    uint8_t	count;		/* [7:0] blk count / interleave	*/
    uint8_t	ctrl;		/* [7:0] control field		*/
};
#pragma pack(pop)

/*
 * The (configured) Drive Parameters.
 *
 * Although the IBM specification calls for a total of 8 bytes
 * in the Paramater Block, the EuroPC uses a 16-byte block. It
 * looks like it has extended (translated?) information there,
 * as well as the actual data we need.
 *
 * [ 03 ac 04 01 f4 02 67 0b 11 04 67 02 00 00 01 00]
 *
 * is what was sent for a standard 615/4/17 disk with rdwrcyl
 * set to 500, and precomp to 615.
 *
 * For now, we will just look at the rest of the data.
 */
#pragma pack(push,1)
struct dprm {
#if HDC_NEWPARAMS
    uint16_t	tracks;		/* total number of sectors on drive */
    uint8_t	heads;		/* number of heads per cylinder */
    uint16_t	rwcurrent;	/* (MSB) reduced write current cylinder */
    uint16_t	wprecomp;	/* (MSB) write precompensation cylinder */
    uint8_t	maxecc;		/* max ECC data burst length */
#else
    uint16_t	tracks;		/* (MSB) max number of cylinders */
    uint8_t	heads;		/* number of heads per cylinder */
    uint16_t	rwcurrent;	/* (MSB) reduced write current cylinder */
    uint16_t	wprecomp;	/* (MSB) write precompensation cylinder */
    uint8_t	maxecc;		/* max ECC data burst length */
#endif
};

typedef struct {
    uint8_t	spt,
		hpc;
    uint16_t	tracks;

    struct dprm	params;
    uint8_t	cfg_spt,
		cfg_hpc;
    uint16_t	cfg_tracks;

    uint16_t	cur_cyl;

    int8_t	present,
		hdd_num;
} drive_t;
#pragma pack(pop)


typedef struct {
    uint16_t	base;
    int8_t	irq;
    int8_t	dma;
    uint8_t	mask;

    int8_t	state;
    int64_t	callback;

    uint8_t	sense;		/* current SENSE ERROR value	*/
    uint8_t	status;		/* current operational status	*/

    /* Current operation parameters. */
    int16_t	buf_idx,	/* command buffer index and pointer */
		buf_len;
    uint8_t	*buf_ptr;
    uint16_t	track;		/* requested track# */
    uint8_t	head,		/* requested head# */
		sector,		/* requested sector# */
		comp;		/* operation completion byte */
    int		count;		/* requested sector count */

    struct dcb	dcb;		/* device control block */

    drive_t	drives[MFM_NUM];

    uint8_t	data[512];	/* data buffer */
    uint8_t	sector_buf[512];
} hd20_t;


static void
hd20_intr(hd20_t *dev)
{
    dev->status = STAT_REQ|STAT_CD|STAT_IO|STAT_BSY;
    dev->state = STATE_COMPL;
    if (dev->mask & IRQ_ENA) {
	dev->status |= STAT_IRQ;
	picint(1<<dev->irq);
    }
}


static int
get_sector(hd20_t *dev, drive_t *drive, off64_t *addr)
{
    int heads = drive->cfg_hpc;

    if (drive->cur_cyl != dev->track) {
	pclog("HD20: get_sector: wrong cylinder %d/%d\n",
				drive->cur_cyl, dev->track);
	dev->sense = ERR_ILLADDR;
	return(1);
    }

    if (dev->head > heads) {
	pclog("HD20: get_sector: past end of configured heads\n");
	dev->sense = ERR_ILLADDR;
	return(1);
    }

    if (dev->head > drive->hpc) {
	pclog("HD20: get_sector: past end of heads\n");
	dev->sense = ERR_ILLADDR;
	return(1);
    }

    if (dev->sector >= 17) {
	pclog("HD20: get_sector: past end of sectors\n");
	dev->sense = ERR_ILLADDR;
	return(1);
    }

    *addr = ((((off64_t) dev->track*heads) + dev->head)*17) + dev->sector;

    return(0);
}


static void
next_sector(hd20_t *dev, drive_t *drive)
{
    if (++dev->sector >= 17) {
	dev->sector = 0;
	if (++dev->head >= drive->cfg_hpc) {
		dev->head = 0;
		dev->track++;
		drive->cur_cyl++;
		if (drive->cur_cyl >= drive->cfg_tracks)
			drive->cur_cyl = drive->cfg_tracks-1;
	}
    }
}


/* Execute the DCB we just received. */
static void
hd20_callback(void *priv)
{
    hd20_t *dev = (hd20_t *)priv;
    struct dcb *dcb = &dev->dcb;
    drive_t *drive;
    off64_t addr;
    int val;

    dev->callback = 0;

    drive = &dev->drives[dcb->drvsel];
    dev->comp = (dcb->drvsel) ? COMP_DRIVE : 0x00;

    switch (dcb->cmd) {
	case CMD_TEST_DRV_RDY:
#if HDC_DEBUG
	if (dcb->drvsel == 0)
		pclog("HD20: test_rdy(%d) ready=%d\n",
			dcb->drvsel, drive->present);
#endif
		if (! drive->present) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
		}
		hd20_intr(dev);
		break;

	case CMD_RECALIBRATE:
#if HDC_DEBUG
	if (dcb->drvsel == 0)
		pclog("HD20: recalibrate(%d) ready=%d\n",
				dcb->drvsel, drive->present);
#endif
		if (! drive->present) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
		} else {
			dev->track = drive->cur_cyl = 0;
		}
		hd20_intr(dev);
		break;
		
	case CMD_READ_SENSE:
		if (dev->state == STATE_RUN) {
#if HDC_DEBUG
	if (dcb->drvsel == 0)
			pclog("HD20: sense(%d)\n", dcb->drvsel);
#endif
			dev->buf_idx = 0;
			dev->buf_len = 4;
			dev->buf_ptr = dev->data;
			dev->data[0] = dev->sense;
			dev->data[1] = dcb->drvsel ? 0x20 : 0x00;
			dev->data[2] = dev->data[3] = 0x00;
			dev->sense = ERR_NOERROR;
			dev->status = STAT_BSY|STAT_IO|STAT_REQ;
			dev->state = STATE_TXDTA;
		} else if (dev->state == STATE_TDATA) {
			hd20_intr(dev);
		}
		break;		

	case CMD_READY_VERIFY:
		if (dev->state == STATE_RUN) {
			/* Seek to cylinder. */
			dev->track = dcb->cyl | (dcb->cylh<<2);
			if (dev->track >= drive->cfg_tracks)
				drive->cur_cyl = drive->cfg_tracks-1;
			  else
				drive->cur_cyl = dev->track;
			dev->head = dcb->head;
			dev->sector = dcb->sector;
#if HDC_DEBUG
			pclog("HD20: verify_sector(%d) %d,%d,%d\n",
				dcb->drvsel, dev->track, dev->head,dev->sector);
#endif

			/* Get sector count; count=0 means 256. */
			dev->count = (int)dcb->count;
			if (dev->count == 0) dev->count = 256;
			while (dev->count-- > 0) {
				if (get_sector(dev, drive, &addr)) {
					pclog("HD20: get_sector failed\n");
					dev->comp |= COMP_ERR;
					hd20_intr(dev);
					return;
				}

				next_sector(dev, drive);
			}

			hd20_intr(dev);

			ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);
		}
		break;

	case CMD_FORMAT_DRIVE:
#if HDC_DEBUG
		pclog("HD20: format_drive(%d)\n", dcb->drvsel);
#endif
		for (dev->track=0; dev->track<drive->tracks; dev->track++) {
			drive->cur_cyl = dev->track;
			for (dev->head=0; dev->head<drive->hpc; dev->head++) {
				dev->sector = 0;

				if (get_sector(dev, drive, &addr)) {
					pclog("HD20: get_sector failed\n");
					dev->comp |= COMP_ERR;
					hd20_intr(dev);
					return;
				}

				hdd_image_zero(drive->hdd_num,addr,drive->spt);

				ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);
			}
		}
		hd20_intr(dev);
		break;

	case CMD_FORMAT_TRACK:
		/* Seek to cylinder. */
		dev->track = dcb->cyl | (dcb->cylh<<2);
		if (dev->track >= drive->cfg_tracks)
			drive->cur_cyl = drive->cfg_tracks-1;
		  else
			drive->cur_cyl = dev->track;
		dev->head = dcb->head;
		dev->sector = 0;
#if HDC_DEBUG
		pclog("HD20: format_track(%d) %d,%d\n",
			dcb->drvsel, dev->track, dev->head);
#endif

		if (get_sector(dev, drive, &addr)) {
			pclog("HD20: get_sector failed\n");
			dev->comp |= COMP_ERR;
			hd20_intr(dev);
			return;
		}

		hdd_image_zero(drive->hdd_num, addr, drive->spt);

		ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);

		hd20_intr(dev);
		break;

	case CMD_READ_SECTORS:
		switch (dev->state) {
			case STATE_RUN:
				/* Seek to cylinder. */
				dev->track = dcb->cyl | (dcb->cylh<<2);
				if (dev->track >= drive->cfg_tracks)
					drive->cur_cyl = drive->cfg_tracks-1;
				  else
					drive->cur_cyl = dev->track;
				dev->head = dcb->head;
				dev->sector = dcb->sector;

				/* Get sector count; count=0 means 256. */
				dev->count = (int)dcb->count;
				if (dev->count == 0) dev->count = 256;
#if HDC_DEBUG
				pclog("HD20: read_sector(%d) %d,%d,%d cnt=%d\n",
					dcb->drvsel, dev->track, dev->head,
					dev->sector, dev->count);
#endif

				if (get_sector(dev, drive, &addr)) {
					dev->comp |= COMP_ERR;
					hd20_intr(dev);
					return;
				}

				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *)dev->sector_buf);
				ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);

				/* Ready to transfer the data out. */
				dev->buf_idx = 0;
				dev->buf_len = 512;
				dev->state = STATE_TXDTA;

				if (! (dev->mask & DMA_ENA)) {
					memcpy(dev->data, dev->sector_buf, 512);
					dev->buf_ptr = dev->data;
					dev->status = STAT_BSY|STAT_IO|STAT_REQ;
				} else {
					dev->callback = HDC_TIME;
					dev->buf_ptr = dev->sector_buf;
				}
				break;
			
			case STATE_TXDTA:
				dev->status = STAT_BSY;
				while (dev->buf_idx < dev->buf_len) {
					val = dma_channel_write(dev->dma,
							*dev->buf_ptr++);
					if (val == DMA_NODATA) {
						pclog("CMD_READ_SECTORS out of data!\n");
						dev->status = STAT_BSY|STAT_CD|STAT_IO|STAT_REQ;
						dev->callback = HDC_TIME;
						return;
					}
					dev->buf_idx++;
				}
				dev->state = STATE_TDATA;
				dev->callback = HDC_TIME;
				break;

			case STATE_TDATA:
				next_sector(dev, drive);

				dev->buf_idx = 0;
				if (--dev->count == 0) {
					ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 0);
					hd20_intr(dev);
					return;
				}

				if (get_sector(dev, drive, &addr)) {
					dev->comp |= COMP_ERR;
					hd20_intr(dev);
					return;
				}

				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *)dev->sector_buf);
				ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);

				dev->state = STATE_TXDTA;

				if (! (dev->mask & DMA_ENA)) {
					memcpy(dev->data, dev->sector_buf, 512);
					dev->buf_ptr = dev->data;
					dev->status = STAT_BSY|STAT_IO|STAT_REQ;
				} else {
					dev->buf_ptr = dev->sector_buf;
					dev->callback = HDC_TIME;
				}
				break;
		}
		break;
		
	case CMD_WRITE_SECTORS:
		switch (dev->state) {
			case STATE_RUN:
				/* Seek to cylinder. */
				dev->track = dcb->cyl | (dcb->cylh<<2);
				if (dev->track >= drive->cfg_tracks)
					drive->cur_cyl = drive->cfg_tracks-1;
				  else
					drive->cur_cyl = dev->track;
				dev->head = dcb->head;
				dev->sector = dcb->sector;

				/* Get sector count; count=0 means 256. */
				dev->count = (int)dev->dcb.count;
				if (dev->count == 0) dev->count = 256;
#if HDC_DEBUG
				pclog("HD20: write_sector(%d) %d,%d,%d cnt=%d\n",
					dcb->drvsel, dev->track, dev->head,
					dev->sector, dev->count);
#endif
				dev->buf_idx = 0;
				dev->buf_len = 512;
				dev->state = STATE_RXDTA;
				if (! (dev->mask & DMA_ENA)) {
					dev->buf_ptr = dev->data;
					dev->status = STAT_BSY|STAT_REQ;
				} else {
					dev->buf_ptr = dev->sector_buf;
					dev->callback = HDC_TIME;
				}
				break;

			case STATE_RXDTA:
				dev->status = STAT_BSY;
				while (dev->buf_idx < dev->buf_len) {
					val = dma_channel_read(dev->dma);
					if (val == DMA_NODATA) {
						pclog("CMD_WRITE_SECTORS out of data!\n");
						dev->status = STAT_BSY|STAT_CD|STAT_IO|STAT_REQ;
						dev->callback = HDC_TIME;
						return;
					}

					*dev->buf_ptr++ = (val & 0xff);
					dev->buf_idx++;
				}
				dev->state = STATE_RDATA;
				dev->callback = HDC_TIME;
				break;

			case STATE_RDATA:
#if 0
/* If I enable this, we get data corruption.. ???  -FvK */
				if (! (dev->mask & DMA_ENA))
					memcpy(dev->sector_buf, dev->data, 512);
#endif

				if (get_sector(dev, drive, &addr)) {
					dev->comp |= COMP_ERR;
					hd20_intr(dev);

					return;
				}

				hdd_image_write(drive->hdd_num, addr, 1,
						(uint8_t *)dev->sector_buf);
				ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 1);

				next_sector(dev, drive);

				dev->buf_idx = 0;
				if (--dev->count == 0) {
					ui_sb_update_icon(SB_HDD|HDD_BUS_MFM, 0);
					hd20_intr(dev);
					break;
				}

				dev->state = STATE_RXDTA;
				if (! (dev->mask & DMA_ENA)) {
					dev->buf_ptr = dev->data;
					dev->status = STAT_BSY|STAT_REQ;
				} else {
					dev->buf_ptr = dev->sector_buf;
					dev->callback = HDC_TIME;
				}
		}
		break;

	case CMD_SEEK:
		if (! drive->present) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
			hd20_intr(dev);
			break;
		}

		/* Seek to cylinder. */
		val = dcb->cyl | (dcb->cylh<<2);
		if (val >= drive->cfg_tracks)
			drive->cur_cyl = drive->cfg_tracks-1;
		  else
			drive->cur_cyl = val;
#if HDC_DEBUG
		pclog("HD20: seek(%d) %d/%d\n",
			dcb->drvsel, val, drive->cur_cyl);
#endif

		if (val != drive->cur_cyl) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_SEEK;
		}
		hd20_intr(dev);
		break;

	case CMD_SET_DRIVE_PARAMS:
		if (dev->state == STATE_RUN) {
			dev->state = STATE_RXDTA;
			dev->buf_idx = 0;
			dev->buf_len = sizeof(struct dprm);
			dev->buf_ptr = (uint8_t *)&drive->params;
			dev->status = STAT_BSY|STAT_REQ;
		} else {
dev->buf_ptr=(uint8_t *)&drive->params;
pclog("HD20: PARAMS=[");
for(val=0;val<8;val++)pclog(" %02x",*dev->buf_ptr++);
pclog(" ]\n");
#if 0
			drive->cfg_tracks = drive->params.tracks;
			drive->cfg_hpc = drive->params.heads;
			drive->cfg_spt = drive->spt;
#endif
#if HDC_DEBUG
			pclog("HD20: set_params(%d) cyl=%d,hd=%d,spt=%d\n",
				dcb->drvsel, drive->cfg_tracks,
				drive->cfg_hpc, drive->cfg_spt);
#endif
			hd20_intr(dev);
		}
		break;

	case CMD_WRITE_SECTOR_BUFFER:
		switch (dev->state) {
			case STATE_RUN:
#if HDC_DEBUG
				pclog("HD20: write_sector_buffer(%d)\n",
							dcb->drvsel);
#endif
				dev->buf_idx = 0;
				dev->buf_len = 512;
				dev->state = STATE_RXDTA;
				if (! (dev->mask & DMA_ENA)) {
					dev->buf_ptr = dev->data;
					dev->status = STAT_BSY|STAT_REQ;
				} else {
					dev->buf_ptr = dev->sector_buf;
					dev->callback = HDC_TIME;
				}
				break;

			case STATE_RXDTA:
				dev->status = STAT_BSY;
				if (! (dev->mask & DMA_ENA)) break;

				while (dev->buf_idx++ < dev->buf_len) {
					val = dma_channel_read(dev->dma);
					if (val == DMA_NODATA) {
						pclog("CMD_WRITE_SECTORS out of data!\n");
						dev->status = STAT_BSY|STAT_CD|STAT_IO|STAT_REQ;
						dev->callback = HDC_TIME;
						return;
					}

					*dev->buf_ptr++ = (val & 0xff);
				}
				dev->state = STATE_RDATA;
				dev->callback = HDC_TIME;
				break;

			case STATE_RDATA:
				if (! (dev->mask & DMA_ENA))
					memcpy(dev->sector_buf, dev->data, 512);

				hd20_intr(dev);
				break;
		}
		break;

	case CMD_RAM_DIAGS:
#if HDC_DEBUG
		pclog("HD20: ram_diags\n");
#endif
		dev->callback = 5*HDC_TIME;
		hd20_intr(dev);
		break;

	case CMD_DRIVE_DIAGS:
#if HDC_DEBUG
		pclog("HD20: drive_diags(%d)\n", dcb->drvsel);
#endif
		dev->callback = 5*HDC_TIME;
		hd20_intr(dev);
		break;

	case CMD_CTRL_DIAGS:
#if HDC_DEBUG
		pclog("HD20: ctrl_diags\n");
#endif
		dev->callback = 5*HDC_TIME;
		hd20_intr(dev);
		break;

	default:
		pclog("HD20: unknown command - %02x\n", dcb->cmd);
		dev->comp |= COMP_ERR;
		dev->sense = ERR_ILLCMD;
		hd20_intr(dev);
    }
}


/* Read one of the HD controller registers. */
static uint8_t
hd20_read(uint16_t port, void *priv)
{
    hd20_t *dev = (hd20_t *)priv;
    uint8_t ret = 0xff;
	
    switch (port-dev->base) {
	case 0:			/* read data */
		dev->status &= ~STAT_IRQ;

		if (dev->state == STATE_TXDTA) {
			if ((dev->status & 0x0f) !=
				(STAT_IO|STAT_REQ|STAT_BSY))
				fatal("Read data STATE_COMPLETION_BYTE, status=%02x\n", dev->status);
			if (dev->buf_idx > dev->buf_len) {
				pclog("HD20: read with empty buffer!\n");
				dev->comp |= COMP_ERR;
				dev->sense = ERR_ILLCMD;
				break;
			}

			ret = dev->data[dev->buf_idx++];
			if (dev->buf_idx == dev->buf_len) {
				dev->status = STAT_BSY;
				dev->state = STATE_TDATA;
				dev->callback = HDC_TIME;
			}
		} else if (dev->state == STATE_COMPL) {
			if ((dev->status & 0x0f) !=
			    (STAT_CD|STAT_IO|STAT_REQ|STAT_BSY))
				fatal("Read data STATE_COMPL, status=%02x\n", dev->status);
			ret = dev->comp;
			dev->status = 0x00;
			dev->state = STATE_IDLE;
		}
		break;

	case 1:			/* read status */
		ret = dev->status;
		break;

	case 2:			/* read option jumpers */
		ret = 0x00;
		break;
    }

#if HDC_DEBUG > 1
    pclog("HD20: read(%04x) = %02x\n", port, ret);
#endif
    return(ret);	
}


static void
hd20_write(uint16_t port, uint8_t val, void *priv)
{
    hd20_t *dev = (hd20_t *)priv;

#if HDC_DEBUG > 1
    pclog("HD20: write(%04x,%02x)\n", port, val);
#endif
    switch (port-dev->base) {
	case 0:			/* write command/data */
		if (! (dev->status & STAT_REQ)) {
			pclog("HD20: not ready for command/data!\n");
			dev->comp |= COMP_ERR;
			dev->sense = ERR_ILLCMD;
			break;
		}

		if (dev->buf_idx >= dev->buf_len) {
			pclog("HD20: write with full buffer!\n");
			dev->comp |= COMP_ERR;
			dev->sense = ERR_ILLCMD;
			break;
		}

		/* Store the data into the buffer. */
		*dev->buf_ptr++ = val;
		if (++dev->buf_idx == dev->buf_len) {
			/* We got all the data we need. */
			dev->status &= ~STAT_REQ;
			dev->state = (dev->state==STATE_CMD) ? STATE_RUN : STATE_RDATA;
			dev->callback = HDC_TIME;
		}
		break;

	case 1:			/* controller reset */
		dev->sense = 0x00;
		/*FALLTHROUGH*/

	case 2:			/* generate controller-select-pulse */
		dev->status = STAT_BSY|STAT_CD|STAT_REQ;
		dev->buf_idx = 0;
		dev->buf_len = sizeof(struct dcb);
		dev->buf_ptr = (uint8_t *)&dev->dcb;
		dev->state = STATE_CMD;
		break;

	case 3:			/* DMA/IRQ mask register */
		dev->mask = val;
		break;
    }
}


static void *
hd20_init(device_t *info)
{
    drive_t *drive;
    hd20_t *dev;
    int c, i;

    pclog("EuroPC: initializing HD20 controller.\n");

    dev = malloc(sizeof(hd20_t));
    memset(dev, 0x00, sizeof(hd20_t));
    dev->base = HDD_IOADDR;
    dev->irq = HDD_IRQCHAN;
    dev->dma = HDD_DMACHAN;

    for (c=0,i=0; i<HDD_NUM; i++) {
	if ((hdd[i].bus == HDD_BUS_MFM) && (hdd[i].mfm_channel < MFM_NUM)) {
		drive = &dev->drives[hdd[i].mfm_channel];

		if (! hdd_image_load(i)) {
			drive->present = 0;
			continue;
		}

		/* These are the "hardware" parameters (from the image.) */
		drive->spt = hdd[i].spt;
		drive->hpc = hdd[i].hpc;
		drive->tracks = hdd[i].tracks;

		/* Use them as "configured" parameters until overwritten. */
		drive->cfg_spt = drive->spt;
		drive->cfg_hpc = drive->hpc;
		drive->cfg_tracks = drive->tracks;

		drive->hdd_num = i;
		drive->present = 1;

		pclog("HD20: drive%d (cyl=%d,hd=%d,spt=%d), disk %d\n",
		      hdd[i].mfm_channel,drive->tracks,drive->hpc,drive->spt,i);

		if (++c > MFM_NUM) break;
	}
    }

    io_sethandler(dev->base, 4,
		  hd20_read, NULL, NULL, hd20_write, NULL, NULL, dev);

    timer_add(hd20_callback, &dev->callback, &dev->callback, dev);

    return(dev);
}


static void
hd20_close(void *priv)
{
    hd20_t *dev = (hd20_t *)priv;
    drive_t *drive;
    int d;

    for (d=0; d<2; d++) {
	drive = &dev->drives[d];

	hdd_image_close(drive->hdd_num);
    }

    free(dev);
}


static int
hd20_available(void)
{
    return(1);
}


device_t europc_hdc_device = {
    "EuroPC HD20",
    0, 0,
    hd20_init, hd20_close, NULL,
    hd20_available, NULL, NULL, NULL,
    NULL
};
