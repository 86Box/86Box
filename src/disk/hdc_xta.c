/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of a generic IDE-XTA disk controller.
 *
 *		XTA is the acronym for 'XT-Attached', which was basically
 *		the XT-counterpart to what we know now as IDE (which is
 *		also named ATA - AT Attachment.)  The basic ideas was to
 *		put the actual drive controller electronics onto the drive
 *		itself, and have the host machine just talk to that using
 *		a simpe, standardized I/O path- hence the name IDE, for
 *		Integrated Drive Electronics.
 *
 *		In the ATA version of IDE, the programming interface of
 *		the IBM PC/AT (which used the Western Digitial 1002/1003
 *		controllers) was kept, and, so, ATA-IDE assumes a 16bit
 *		data path: it reads and writes 16bit words of data. The
 *		disk drives for this bus commonly have an 'A' suffix to
 *		identify them as 'ATBUS'.
 *
 *		In XTA-IDE, which is slightly older, the programming 
 *		interface of the IBM PC/XT (which used the MFM controller
 *		from Xebec) was kept, and, so, it uses an 8bit data path.
 *		Disk drives for this bus commonly have the 'X' suffix to
 *		mark them as being for this XTBUS variant.
 *
 *		So, XTA and ATA try to do the same thing, but they use
 *		different ways to achive their goal.
 *
 *		Also, XTA is **not** the same as XTIDE.  XTIDE is a modern
 *		variant of ATA-IDE, but retro-fitted for use on 8bit XT
 *		systems: an extra register is used to deal with the extra
 *		data byte per transfer.  XTIDE uses regular IDE drives,
 *		and uses the regular ATA/IDE programming interface, just
 *		with the extra register.
 * 
 * NOTE:	This driver implements both the 'standard' XTA interface,
 *		sold by Western Digital as the WDXT-140 (no BIOS) and the
 *		WDXT-150 (with BIOS), as well as some variants customized
 *		for specific machines.
 *
 * NOTE:	The XTA interface is 0-based for sector numbers !!
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Based on my earlier HD20 driver for the EuroPC.
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc.h>
#include <86box/hdd.h>


#define HDC_TIME	(50*TIMER_USEC)

#define WD_BIOS_FILE	"roms/hdd/xta/idexywd2.bin"


enum {
    STATE_IDLE = 0,
    STATE_RECV,
    STATE_RDATA,
    STATE_RDONE,
    STATE_SEND,
    STATE_SDATA,
    STATE_SDONE,
    STATE_COMPL
};


/* Command values. */
#define CMD_TEST_READY		0x00
#define CMD_RECALIBRATE		0x01
		/* unused 	0x02 */
#define CMD_READ_SENSE		0x03
#define CMD_FORMAT_DRIVE	0x04
#define CMD_READ_VERIFY		0x05
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

/* Status register (reg 1) values. */
#define STAT_REQ		0x01	/* controller needs data transfer */
#define STAT_IO			0x02	/* direction of transfer (TO bus) */
#define STAT_CD			0x04	/* transfer of Command or Data */
#define STAT_BSY		0x08	/* controller is busy */
#define STAT_DRQ		0x10	/* DMA requested */
#define STAT_IRQ		0x20	/* interrupt requested */
#define STAT_DCB		0x80	/* not seen by driver */

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
typedef struct {
    uint8_t	cmd;			/* [7:5] class, [4:0] opcode	*/

    uint8_t	head		:5,	/* [4:0] head number		*/
		drvsel		:1,	/* [5] drive select		*/
		mbz		:2;	/* [7:6] 00			*/

    uint8_t	sector		:6,	/* [5:0] sector number 0-63	*/
		cyl_high	:2;	/* [7:6] cylinder [9:8] bits	*/

    uint8_t	cyl_low;		/* [7:0] cylinder [7:0] bits	*/

    uint8_t	count;			/* [7:0] blk count / interleave	*/

    uint8_t	ctrl;			/* [7:0] control field		*/
} dcb_t;
#pragma pack(pop)

/* The (configured) Drive Parameters. */
#pragma pack(push,1)
typedef struct {
    uint8_t	cyl_high;	/* (MSB) number of cylinders */
    uint8_t	cyl_low;	/* (LSB) number of cylinders */
    uint8_t	heads;		/* number of heads per cylinder */
    uint8_t	rwc_high;	/* (MSB) reduced write current cylinder */
    uint8_t	rwc_low;	/* (LSB) reduced write current cylinder */
    uint8_t	wp_high;	/* (MSB) write precompensation cylinder */
    uint8_t	wp_low;		/* (LSB) write precompensation cylinder */
    uint8_t	maxecc;		/* max ECC data burst length */
} dprm_t;
#pragma pack(pop)

/* Define an attached drive. */
typedef struct {
    int8_t	id,			/* drive ID on bus */
		present,		/* drive is present */
		hdd_num,		/* index to global disk table */
		type;			/* drive type ID */

    uint16_t	cur_cyl;		/* last known position of heads */

    uint8_t	spt,			/* active drive parameters */
		hpc;
    uint16_t	tracks;

    uint8_t	cfg_spt,		/* configured drive parameters */
		cfg_hpc;
    uint16_t	cfg_tracks;
} drive_t;


typedef struct {
    const char	*name;			/* controller name */

    uint16_t	base;			/* controller base I/O address */
    int8_t	irq;			/* controller IRQ channel */
    int8_t	dma;			/* controller DMA channel */
    int8_t	type;			/* controller type ID */

    uint32_t	rom_addr;		/* address where ROM is */
    rom_t	bios_rom;		/* descriptor for the BIOS */

    /* Controller state. */
    int8_t	state;			/* controller state */
    uint8_t	sense;			/* current SENSE ERROR value	*/
    uint8_t	status;			/* current operational status	*/
    uint8_t	intr;
    uint64_t	callback;
	pc_timer_t timer;

    /* Data transfer. */
    int16_t	buf_idx,		/* buffer index and pointer */
		buf_len;
    uint8_t	*buf_ptr;

    /* Current operation parameters. */
    dcb_t	dcb;			/* device control block */
    uint16_t	track;			/* requested track# */
    uint8_t	head,			/* requested head# */
		sector,			/* requested sector# */
		comp;			/* operation completion byte */
    int		count;			/* requested sector count */

    drive_t	drives[XTA_NUM];	/* the attached drive(s) */

    uint8_t	data[512];		/* data buffer */
    uint8_t	sector_buf[512];	/* sector buffer */
} hdc_t;


#ifdef ENABLE_XTA_LOG
int xta_do_log = ENABLE_XTA_LOG;


static void
xta_log(const char *fmt, ...)
{
    va_list ap;

    if (xta_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define xta_log(fmt, ...)
#endif


static void
set_intr(hdc_t *dev)
{
    dev->status = STAT_REQ|STAT_CD|STAT_IO|STAT_BSY;
    dev->state = STATE_COMPL;

    if (dev->intr & IRQ_ENA) {
	dev->status |= STAT_IRQ;
	picint(1 << dev->irq);
    }
}


/* Get the logical (block) address of a CHS triplet. */
static int
get_sector(hdc_t *dev, drive_t *drive, off64_t *addr)
{
    if (drive->cur_cyl != dev->track) {
	xta_log("%s: get_sector: wrong cylinder %d/%d\n",
		dev->name, drive->cur_cyl, dev->track);
	dev->sense = ERR_ILLADDR;
	return(1);
    }

    if (dev->head >= drive->hpc) {
	xta_log("%s: get_sector: past end of heads\n", dev->name);
	dev->sense = ERR_ILLADDR;
	return(1);
    }

    if (dev->sector >= drive->spt) {
	xta_log("%s: get_sector: past end of sectors\n", dev->name);
	dev->sense = ERR_ILLADDR;
	return(1);
    }

    /* Calculate logical address (block number) of desired sector. */
    *addr = ((((off64_t) dev->track*drive->hpc) + \
	      dev->head)*drive->spt) + dev->sector;

    return(0);
}


static void
next_sector(hdc_t *dev, drive_t *drive)
{
    if (++dev->sector >= drive->spt) {
	dev->sector = 0;
	if (++dev->head >= drive->hpc) {
		dev->head = 0;
		dev->track++;
		if (++drive->cur_cyl >= drive->tracks)
			drive->cur_cyl = (drive->tracks - 1);
	}
    }
}

static void
xta_set_callback(hdc_t *dev, uint64_t callback)
{
    if (!dev) {
	return;
    }

    if (callback) {
	dev->callback = callback;
	timer_set_delay_u64(&dev->timer, dev->callback);
	} else {
	dev->callback = 0;
	timer_disable(&dev->timer);
	}
}


/* Perform the seek operation. */
static void
do_seek(hdc_t *dev, drive_t *drive, int cyl)
{
    dev->track = cyl;

    if (dev->track >= drive->tracks)
	drive->cur_cyl = (drive->tracks - 1);
      else
	drive->cur_cyl = dev->track;
}


/* Format a track or an entire drive. */
static void
do_format(hdc_t *dev, drive_t *drive, dcb_t *dcb)
{
    int start_cyl, end_cyl;
    int start_hd, end_hd;
    off64_t addr;
    int h, s;

    /* Get the parameters from the DCB. */
    if (dcb->cmd == CMD_FORMAT_DRIVE) {
	start_cyl = 0;
	start_hd = 0;
	end_cyl = drive->tracks;
	end_hd = drive->hpc;
    } else {
	start_cyl = (dcb->cyl_low | (dcb->cyl_high << 8));
	start_hd = dcb->head;
	end_cyl = start_cyl + 1;
	end_hd = start_hd + 1;
    }

    switch (dev->state) {
	case STATE_IDLE:
		/* Seek to cylinder. */
		do_seek(dev, drive, start_cyl);
		dev->head = dcb->head;
		dev->sector = 0;

		/* Activate the status icon. */
		ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 1);

do_fmt:
		/*
		 * For now, we don't use the interleave factor (in
		 * dcb->count), although we should one day use an
		 * image format that can handle it..
		 *
		 * That said, we have been given a sector_buf of
		 * data to fill the sectors with, so we will use
		 * that at least.
		 */
		for (h = start_hd; h < end_hd; h++) {
			for (s = 0; s < drive->spt; s++) {
				/* Set the sector we need to write. */
				dev->head = h;
				dev->sector = s;

				/* Get address of sector to write. */
				if (get_sector(dev, drive, &addr)) break;

				/* Write the block to the image. */
				hdd_image_write(drive->hdd_num, addr, 1,
						(uint8_t *)dev->sector_buf);
			}
		}

		/* One more track done. */
		if (++start_cyl == end_cyl) break;

		/* This saves us a LOT of code. */
		goto do_fmt;
    }

    /* De-activate the status icon. */
    ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 0);
}


/* Execute the DCB we just received. */
static void
hdc_callback(void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    dcb_t *dcb = &dev->dcb;
    drive_t *drive;
    dprm_t *params;
    off64_t addr;
    int no_data = 0;
    int val;

    /* Cancel timer. */
    xta_set_callback(dev, 0);

    drive = &dev->drives[dcb->drvsel];
    dev->comp = (dcb->drvsel) ? COMP_DRIVE : 0x00;
    dev->status |= STAT_DCB;

    switch (dcb->cmd) {
	case CMD_TEST_READY:
		if (! drive->present) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
		}
		set_intr(dev);
		break;

	case CMD_RECALIBRATE:
		if (! drive->present) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
		} else {
			dev->track = drive->cur_cyl = 0;
		}
		set_intr(dev);
		break;
		
	case CMD_READ_SENSE:
		switch(dev->state) {
			case STATE_IDLE:
				dev->buf_idx = 0;
				dev->buf_len = 4;
				dev->buf_ptr = dev->data;
				dev->buf_ptr[0] = dev->sense;
				dev->buf_ptr[1] = dcb->drvsel ? 0x20 : 0x00;
				dev->buf_ptr[2] = (drive->cur_cyl >> 2) | \
						  (dev->sector & 0x3f);
				dev->buf_ptr[3] = (drive->cur_cyl & 0xff);
				dev->sense = ERR_NOERROR;
				dev->status |= (STAT_IO | STAT_REQ);
				dev->state = STATE_SDATA;
				break;

			case STATE_SDONE:
				set_intr(dev);
		}
		break;		

	case CMD_READ_VERIFY:
		no_data = 1;
		/*FALLTHROUGH*/

	case CMD_READ_SECTORS:
		if (! drive->present) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
			set_intr(dev);
			break;
		}

		switch (dev->state) {
			case STATE_IDLE:
				/* Seek to cylinder. */
				do_seek(dev, drive,
					(dcb->cyl_low|(dcb->cyl_high<<8)));
				dev->head = dcb->head;
				dev->sector = dcb->sector;

				/* Get sector count; count=0 means 256. */
				dev->count = (int)dcb->count;
				if (dev->count == 0)
					dev->count = 256;
				dev->buf_len = 512;

				dev->state = STATE_SEND;
				/*FALLTHROUGH*/

			case STATE_SEND:
				/* Activate the status icon. */
				ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 1);
do_send:
				/* Get address of sector to load. */
				if (get_sector(dev, drive, &addr)) {
					/* De-activate the status icon. */
					ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 0);
					dev->comp |= COMP_ERR;
					set_intr(dev);
					return;
				}

				/* Read the block from the image. */
				hdd_image_read(drive->hdd_num, addr, 1,
					       (uint8_t *)dev->sector_buf);

				/* Ready to transfer the data out. */
				dev->state = STATE_SDATA;
				dev->buf_idx = 0;
				if (no_data) {
					/* Delay a bit, no actual transfer. */
					xta_set_callback(dev, HDC_TIME);
				} else {
					if (dev->intr & DMA_ENA) {
						/* DMA enabled. */
						dev->buf_ptr = dev->sector_buf;
						xta_set_callback(dev, HDC_TIME);
					} else {
						/* Copy from sector to data. */
						memcpy(dev->data,
						       dev->sector_buf,
						       dev->buf_len);
						dev->buf_ptr = dev->data;

						dev->status |= (STAT_IO | STAT_REQ);
					}
				}
				break;
			
			case STATE_SDATA:
				if (! no_data) {
					/* Perform DMA. */
					while (dev->buf_idx < dev->buf_len) {
						val = dma_channel_write(dev->dma,
							*dev->buf_ptr);
						if (val == DMA_NODATA) {
							xta_log("%s: CMD_READ_SECTORS out of data (idx=%d, len=%d)!\n", dev->name, dev->buf_idx, dev->buf_len);

							dev->status |= (STAT_CD | STAT_IO| STAT_REQ);
							xta_set_callback(dev, HDC_TIME);
							return;
						}
						dev->buf_ptr++;
						dev->buf_idx++;
					}
				}
				xta_set_callback(dev, HDC_TIME);
				dev->state = STATE_SDONE;
				break;

			case STATE_SDONE:
				dev->buf_idx = 0;
				if (--dev->count == 0) {
					/* De-activate the status icon. */
					ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 0);

					set_intr(dev);
					return;
				}

				/* Addvance to next sector. */
				next_sector(dev, drive);

				/* This saves us a LOT of code. */
				dev->state = STATE_SEND;
				goto do_send;
		}
		break;

	case CMD_WRITE_SECTORS:
		if (! drive->present) {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
			set_intr(dev);
			break;
		}

		switch (dev->state) {
			case STATE_IDLE:
				/* Seek to cylinder. */
				do_seek(dev, drive,
					(dcb->cyl_low|(dcb->cyl_high<<8)));
				dev->head = dcb->head;
				dev->sector = dcb->sector;

				/* Get sector count; count=0 means 256. */
				dev->count = (int)dev->dcb.count;
				if (dev->count == 0)
					dev->count = 256;
				dev->buf_len = 512;

				dev->state = STATE_RECV;
				/*FALLTHROUGH*/

			case STATE_RECV:
				/* Activate the status icon. */
				ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 1);
do_recv:
				/* Ready to transfer the data in. */
				dev->state = STATE_RDATA;
				dev->buf_idx = 0;
				if (dev->intr & DMA_ENA) {
					/* DMA enabled. */
					dev->buf_ptr = dev->sector_buf;
					xta_set_callback(dev, HDC_TIME);
				} else {
					/* No DMA, do PIO. */
					dev->buf_ptr = dev->data;
					dev->status |= STAT_REQ;
				}
				break;

			case STATE_RDATA:
				if (! no_data) {
					/* Perform DMA. */
					dev->status = STAT_BSY;
					while (dev->buf_idx < dev->buf_len) {
						val = dma_channel_read(dev->dma);
						if (val == DMA_NODATA) {
							xta_log("%s: CMD_WRITE_SECTORS out of data (idx=%d, len=%d)!\n", dev->name, dev->buf_idx, dev->buf_len);

							xta_log("%s: CMD_WRITE_SECTORS out of data!\n", dev->name);
							dev->status |= (STAT_CD | STAT_IO | STAT_REQ);
							xta_set_callback(dev, HDC_TIME);
							return;
						}

						dev->buf_ptr[dev->buf_idx] = (val & 0xff);
						dev->buf_idx++;
					}
					dev->state = STATE_RDONE;
					xta_set_callback(dev, HDC_TIME);
				}
				break;

			case STATE_RDONE:
				/* Copy from data to sector if PIO. */
				if (! (dev->intr & DMA_ENA))
					memcpy(dev->sector_buf, dev->data,
					       dev->buf_len);

				/* Get address of sector to write. */
				if (get_sector(dev, drive, &addr)) {
					/* De-activate the status icon. */
					ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 0);

					dev->comp |= COMP_ERR;
					set_intr(dev);
					return;
				}

				/* Write the block to the image. */
				hdd_image_write(drive->hdd_num, addr, 1,
						(uint8_t *)dev->sector_buf);

				dev->buf_idx = 0;
				if (--dev->count == 0) {
					/* De-activate the status icon. */
					ui_sb_update_icon(SB_HDD|HDD_BUS_XTA, 0);

					set_intr(dev);
					return;
				}

				/* Advance to next sector. */
				next_sector(dev, drive);

				/* This saves us a LOT of code. */
				dev->state = STATE_RECV;
				goto do_recv;
		}
		break;

	case CMD_FORMAT_DRIVE:
	case CMD_FORMAT_TRACK:
		if (drive->present) {
			do_format(dev, drive, dcb);
		} else {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
		}
		set_intr(dev);
		break;

	case CMD_SEEK:
		/* Seek to cylinder. */
		val = (dcb->cyl_low | (dcb->cyl_high << 8));
		if (drive->present) {
			do_seek(dev, drive, val);
			if (val != drive->cur_cyl) {
				dev->comp |= COMP_ERR;
				dev->sense = ERR_SEEK;
			}
		} else {
			dev->comp |= COMP_ERR;
			dev->sense = ERR_NOTRDY;
		}
		set_intr(dev);
		break;

	case CMD_SET_DRIVE_PARAMS:
		switch(dev->state) {
			case STATE_IDLE:
				dev->state = STATE_RDATA;
				dev->buf_idx = 0;
				dev->buf_len = sizeof(dprm_t);
				dev->buf_ptr = (uint8_t *)dev->data;
				dev->status |= STAT_REQ;
				break;

			case STATE_RDONE:
				params = (dprm_t *)dev->data;
				drive->tracks =
				    (params->cyl_high << 8) | params->cyl_low;
				drive->hpc = params->heads;
				drive->spt = 17	/*hardcoded*/;
				dev->status &= ~STAT_REQ;
				set_intr(dev);
				break;
		}
		break;

	case CMD_WRITE_SECTOR_BUFFER:
		switch (dev->state) {
			case STATE_IDLE:
				dev->buf_idx = 0;
				dev->buf_len = 512;
				dev->state = STATE_RDATA;
				if (dev->intr & DMA_ENA) {
					dev->buf_ptr = dev->sector_buf;
					xta_set_callback(dev, HDC_TIME);
				} else {
					dev->buf_ptr = dev->data;
					dev->status |= STAT_REQ;
				}
				break;

			case STATE_RDATA:
				if (dev->intr & DMA_ENA) {
					/* Perform DMA. */
					while (dev->buf_idx < dev->buf_len) {
						val = dma_channel_read(dev->dma);
						if (val == DMA_NODATA) {
							xta_log("%s: CMD_WRITE_BUFFER out of data!\n", dev->name);
							dev->status |= (STAT_CD | STAT_IO | STAT_REQ);
							xta_set_callback(dev, HDC_TIME);
							return;
						}

						dev->buf_ptr[dev->buf_idx] = (val & 0xff);
						dev->buf_idx++;
					}
					dev->state = STATE_RDONE;
					xta_set_callback(dev, HDC_TIME);
				}
				break;

			case STATE_RDONE:
				if (! (dev->intr & DMA_ENA))
					memcpy(dev->sector_buf,
					       dev->data, dev->buf_len);
				set_intr(dev);
				break;
		}
		break;

	case CMD_RAM_DIAGS:
		switch(dev->state) {
			case STATE_IDLE:
				dev->state = STATE_RDONE;
				xta_set_callback(dev, 5 * HDC_TIME);
				break;

			case STATE_RDONE:
				set_intr(dev);
				break;
		}
		break;

	case CMD_DRIVE_DIAGS:
		switch(dev->state) {
			case STATE_IDLE:
				if (drive->present) {
					dev->state = STATE_RDONE;
					xta_set_callback(dev, 5 * HDC_TIME);
				} else {
					dev->comp |= COMP_ERR;
					dev->sense = ERR_NOTRDY;
					set_intr(dev);
				}
				break;

			case STATE_RDONE:
				set_intr(dev);
				break;
		}
		break;

	case CMD_CTRL_DIAGS:
		switch(dev->state) {
			case STATE_IDLE:
				dev->state = STATE_RDONE;
				xta_set_callback(dev, 10 * HDC_TIME);
				break;

			case STATE_RDONE:
				set_intr(dev);
				break;
		}
		break;

	default:
		xta_log("%s: unknown command - %02x\n", dev->name, dcb->cmd);
		dev->comp |= COMP_ERR;
		dev->sense = ERR_ILLCMD;
		set_intr(dev);
    }
}


/* Read one of the controller registers. */
static uint8_t
hdc_read(uint16_t port, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    uint8_t ret = 0xff;
	
    switch (port & 7) {
	case 0:		/* DATA register */
		dev->status &= ~STAT_IRQ;

		if (dev->state == STATE_SDATA) {
			if (dev->buf_idx > dev->buf_len) {
				xta_log("%s: read with empty buffer!\n",
								dev->name);
				dev->comp |= COMP_ERR;
				dev->sense = ERR_ILLCMD;
				break;
			}

			ret = dev->buf_ptr[dev->buf_idx];
			if (++dev->buf_idx == dev->buf_len) {
				/* All data sent. */
				dev->status &= ~STAT_REQ;
				dev->state = STATE_SDONE;
				xta_set_callback(dev, HDC_TIME);
			}
		} else if (dev->state == STATE_COMPL) {
xta_log("DCB=%02X  status=%02X comp=%02X\n", dev->dcb.cmd, dev->status, dev->comp);
			ret = dev->comp;
			dev->status = 0x00;
			dev->state = STATE_IDLE;
		}
		break;

	case 1:		/* STATUS register */
		ret = (dev->status & ~STAT_DCB);
		break;

	case 2:		/* "read option jumpers" */
		ret = 0xff;		/* all switches off */
		break;
    }

    return(ret);	
}


/* Write to one of the controller registers. */
static void
hdc_write(uint16_t port, uint8_t val, void *priv)
{
    hdc_t *dev = (hdc_t *)priv;

    switch (port & 7) {
	case 0:		/* DATA register */
		if (dev->state == STATE_RDATA) {
			if (! (dev->status & STAT_REQ)) {
				xta_log("%s: not ready for command/data!\n", dev->name);
				dev->comp |= COMP_ERR;
				dev->sense = ERR_ILLCMD;
				break;
			}

			if (dev->buf_idx >= dev->buf_len) {
				xta_log("%s: write with full buffer!\n", dev->name);
				dev->comp |= COMP_ERR;
				dev->sense = ERR_ILLCMD;
				break;
			}

			/* Store the data into the buffer. */
			dev->buf_ptr[dev->buf_idx] = val;
			if (++dev->buf_idx == dev->buf_len) {
				/* We got all the data we need. */
				dev->status &= ~STAT_REQ;
				if (dev->status & STAT_DCB)
					dev->state = STATE_RDONE;
				  else
					dev->state = STATE_IDLE;
				dev->status &= ~STAT_CD;
				xta_set_callback(dev, HDC_TIME);
			}
		}
		break;

	case 1:		/* RESET register */
		dev->sense = 0x00;
		dev->state = STATE_IDLE;
		break;

	case 2:		/* "controller-select" */
		/* Reset the DCB buffer. */
		dev->buf_idx = 0;
		dev->buf_len = sizeof(dcb_t);
		dev->buf_ptr = (uint8_t *)&dev->dcb;
		dev->state = STATE_RDATA;
		dev->status = (STAT_BSY | STAT_CD | STAT_REQ);
		break;

	case 3:		/* DMA/IRQ intr register */
//xta_log("%s: WriteMASK(%02X)\n", dev->name, val);
		dev->intr = val;
		break;
    }
}


static int
xta_available(void)
{
    return(rom_present(WD_BIOS_FILE));
}


static void *
xta_init(const device_t *info)
{
    drive_t *drive;
    char *fn = NULL;
    hdc_t *dev;
    int c, i;
    int max = XTA_NUM;

    /* Allocate and initialize device block. */
    dev = malloc(sizeof(hdc_t));
    memset(dev, 0x00, sizeof(hdc_t));
    dev->type = info->local;

    /* Do per-controller-type setup. */
    switch(dev->type) {
	case 0:		/* WDXT-150, with BIOS */
		dev->name = "WDXT-150";
		dev->base = device_get_config_hex16("base");
		dev->irq = device_get_config_int("irq");
		dev->rom_addr = device_get_config_hex20("bios_addr");
		dev->dma = 3;
		fn = WD_BIOS_FILE;
		max = 1;
		break;

	case 1:		/* EuroPC */
		dev->name = "HD20";
		dev->base = 0x0320;
		dev->irq = 5;
		dev->dma = 3;
		break;
    }

    xta_log("%s: initializing (I/O=%04X, IRQ=%d, DMA=%d",
		dev->name, dev->base, dev->irq, dev->dma);
    if (dev->rom_addr != 0x000000)
	xta_log(", BIOS=%06X", dev->rom_addr);
    xta_log(")\n");

    /* Load any disks for this device class. */
    c = 0;
    for (i = 0; i < HDD_NUM; i++) {
	if ((hdd[i].bus == HDD_BUS_XTA) && (hdd[i].xta_channel < max)) {
		drive = &dev->drives[hdd[i].xta_channel];

		if (! hdd_image_load(i)) {
			drive->present = 0;
			continue;
		}
		drive->id = c;
		drive->hdd_num = i;
		drive->present = 1;

		/* These are the "hardware" parameters (from the image.) */
		drive->cfg_spt = (uint8_t)(hdd[i].spt & 0xff);
		drive->cfg_hpc = (uint8_t)(hdd[i].hpc & 0xff);
		drive->cfg_tracks = (uint16_t)hdd[i].tracks;

		/* Use them as "configured" parameters until overwritten. */
		drive->spt = drive->cfg_spt;
		drive->hpc = drive->cfg_hpc;
		drive->tracks = drive->cfg_tracks;

		xta_log("%s: drive%d (cyl=%d,hd=%d,spt=%d), disk %d\n",
			dev->name, hdd[i].xta_channel, drive->tracks,
			drive->hpc, drive->spt, i);

		if (++c > max) break;
	}
    }

    /* Enable the I/O block. */
    io_sethandler(dev->base, 4,
		  hdc_read,NULL,NULL, hdc_write,NULL,NULL, dev);

    /* Load BIOS if it has one. */
    if (dev->rom_addr != 0x000000) {
	rom_init(&dev->bios_rom, fn,
		 dev->rom_addr, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);
   }
		
    /* Create a timer for command delays. */
    timer_add(&dev->timer, hdc_callback, dev, 0);

    return(dev);
}


static void
xta_close(void *priv)
{
    hdc_t *dev = (hdc_t *)priv;
    drive_t *drive;
    int d;

    /* Remove the I/O handler. */
    io_removehandler(dev->base, 4,
		     hdc_read,NULL,NULL, hdc_write,NULL,NULL, dev);

    /* Close all disks and their images. */
    for (d = 0; d < XTA_NUM; d++) {
	drive = &dev->drives[d];

	hdd_image_close(drive->hdd_num);
    }

    /* Release the device. */
    free(dev);
}


static const device_config_t wdxt150_config[] = {
        {
		"base", "Address", CONFIG_HEX16, "", 0x0320, "", { 0 },		/*W2*/
                {
                        {
                                "320H", 0x0320
                        },
                        {
                                "324H", 0x0324
                        },
                        {
                                ""
                        }
                },
        },
        {
		"irq", "IRQ", CONFIG_SELECTION, "", 5, "", { 0 },		/*W3*/
                {
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 4", 4
                        },
                        {
                                ""
                        }
                },
        },
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0xc8000, "", { 0 }, /*W1*/
                {
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "CA00H", 0xca000
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};


const device_t xta_wdxt150_device = {
    "WDXT-150 XTA Fixed Disk Controller",
    DEVICE_ISA,
    0,
    xta_init, xta_close, NULL,
    { xta_available }, NULL, NULL,
    wdxt150_config
};


const device_t xta_hd20_device = {
    "EuroPC HD20 Fixed Disk Controller",
    DEVICE_ISA,
    1,
    xta_init, xta_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
