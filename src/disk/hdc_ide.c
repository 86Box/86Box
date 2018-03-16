/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the IDE emulation for hard disks and ATAPI
 *		CD-ROM devices.
 *
 * Version:	@(#)hdc_ide.c	1.0.37	2018/03/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../io.h"
#include "../pic.h"
#include "../pci.h"
#include "../timer.h"
#include "../device.h"
#include "../cdrom/cdrom.h"
#include "../scsi/scsi.h"
#include "../plat.h"
#include "../ui.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "hdd.h"
#include "zip.h"


/* Bits of 'atastat' */
#define ERR_STAT		0x01
#define DRQ_STAT		0x08 /* Data request */
#define DSC_STAT                0x10
#define SERVICE_STAT            0x10
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits of 'error' */
#define ABRT_ERR		0x04 /* Command aborted */
#define MCR_ERR			0x08 /* Media change request */

/* ATA Commands */
#define WIN_NOP				0x00
#define WIN_SRST			0x08 /* ATAPI Device Reset */
#define WIN_RECAL			0x10
#define WIN_RESTORE			WIN_RECAL
#define WIN_READ			0x20 /* 28-Bit Read */
#define WIN_READ_NORETRY                0x21 /* 28-Bit Read - no retry*/
#define WIN_WRITE			0x30 /* 28-Bit Write */
#define WIN_WRITE_NORETRY		0x31 /* 28-Bit Write */
#define WIN_VERIFY			0x40 /* 28-Bit Verify */
#define WIN_VERIFY_ONCE			0x41 /* Added by OBattler - deprected older ATA command, according to the specification I found, it is identical to 0x40 */
#define WIN_FORMAT			0x50
#define WIN_SEEK			0x70
#define WIN_DRIVE_DIAGNOSTICS           0x90 /* Execute Drive Diagnostics */
#define WIN_SPECIFY			0x91 /* Initialize Drive Parameters */
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* Identify ATAPI device */
#define WIN_READ_MULTIPLE               0xC4
#define WIN_WRITE_MULTIPLE              0xC5
#define WIN_SET_MULTIPLE_MODE           0xC6
#define WIN_READ_DMA                    0xC8
#define WIN_READ_DMA_ALT                0xC9
#define WIN_WRITE_DMA                   0xCA
#define WIN_WRITE_DMA_ALT               0xCB
#define WIN_STANDBYNOW1			0xE0
#define WIN_IDLENOW1			0xE1
#define WIN_SETIDLE1			0xE3
#define WIN_CHECKPOWERMODE1		0xE5
#define WIN_SLEEP1			0xE6
#define WIN_IDENTIFY			0xEC /* Ask drive to identify itself */
#define WIN_SET_FEATURES		0xEF
#define WIN_READ_NATIVE_MAX		0xF8

#define FEATURE_SET_TRANSFER_MODE      0x03
#define FEATURE_ENABLE_IRQ_OVERLAPPED  0x5d
#define FEATURE_ENABLE_IRQ_SERVICE     0x5e
#define FEATURE_DISABLE_REVERT         0x66
#define FEATURE_ENABLE_REVERT          0xcc
#define FEATURE_DISABLE_IRQ_OVERLAPPED 0xdd
#define FEATURE_DISABLE_IRQ_SERVICE    0xde

enum
{
        IDE_NONE = 0,
        IDE_HDD,
        IDE_CDROM,
	IDE_ZIP
};


IDE ide_drives[IDE_NUM + XTIDE_NUM];
IDE *ext_ide;
int (*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length);
int (*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length);
void (*ide_bus_master_set_irq)(int channel);
int64_t idecallback[5] = {0LL, 0LL, 0LL, 0LL, 0LL};
int cur_ide[5];
int ide_init_ch[2] = {0, 0};


#ifdef ENABLE_IDE_LOG
int ide_do_log = ENABLE_IDE_LOG;
#endif


static void ide_log(const char *fmt, ...)
{
#ifdef ENABLE_IDE_LOG
   va_list ap;

   if (ide_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
#endif
}


uint8_t getstat(IDE *ide) { return ide->atastat; }


int ide_drive_is_cdrom(IDE *ide)
{
	if (ide->channel >= 8)
	{
		return 0;
	}

	if (atapi_cdrom_drives[ide->channel] >= CDROM_NUM)
	{
		return 0;
	}
	else
	{
		if ((cdrom_drives[atapi_cdrom_drives[ide->channel]].bus_type == CDROM_BUS_ATAPI_PIO_ONLY) || (cdrom_drives[atapi_cdrom_drives[ide->channel]].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA))
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
}

int ide_drive_is_zip(IDE *ide)
{
	if (ide->channel >= 8)
	{
		return 0;
	}

	if (atapi_zip_drives[ide->channel] >= ZIP_NUM)
	{
		return 0;
	}
	else
	{
		if ((zip_drives[atapi_zip_drives[ide->channel]].bus_type == ZIP_BUS_ATAPI_PIO_ONLY) || (zip_drives[atapi_zip_drives[ide->channel]].bus_type == ZIP_BUS_ATAPI_PIO_AND_DMA))
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
}

int ide_enable[5] = { 1, 1, 0, 0, 1 };
int ide_irq[5] = { 14, 15, 10, 11, 0 };

void ide_irq_raise(IDE *ide)
{
	/* ide_log("Attempting to raise IRQ %i (board %i)\n", ide_irq[ide->board], ide->board); */

	if ((ide->board > 3) || ide->irqstat)
	{
		ide->irqstat=1;
		ide->service=1;

		return;
	}

	ide_log("Raising IRQ %i (board %i)\n", ide_irq[ide->board], ide->board);
	
	if (!(ide->fdisk&2))
	{
		if (pci_use_mirq(0) && (ide->board == 1))
		{
			pci_set_mirq(0);
		}
		else
		{
			picint(1 << ide_irq[ide->board]);
		}

		if (ide->board < 2)
		{
			if (ide_bus_master_set_irq)
			{
				ide_bus_master_set_irq(ide->board | 0x40);
			}
		}
	}

	ide->irqstat=1;
	ide->service=1;
}

void ide_irq_lower(IDE *ide)
{
	if ((ide->board > 3) || !(ide->irqstat))
	{
		ide->irqstat=0;
		return;
	}

	ide_log("Lowering IRQ %i (board %i)\n", ide_irq[ide->board], ide->board);

	if (pci_use_mirq(0) && (ide->board == 1))
	{
		pci_clear_mirq(0);
	}
	else
	{
		picintc(1 << ide_irq[ide->board]);
	}

	if (ide_bus_master_set_irq)
	{
		ide_bus_master_set_irq(ide->board);
	}
	ide->irqstat=0;
}

/**
 * Copy a string into a buffer, padding with spaces, and placing characters as
 * if they were packed into 16-bit values, stored little-endian.
 *
 * @param str Destination buffer
 * @param src Source string
 * @param len Length of destination buffer to fill in. Strings shorter than
 *            this length will be padded with spaces.
 */
static void
ide_padstr(char *str, const char *src, int len)
{
	int i, v;

	for (i = 0; i < len; i++)
	{
		if (*src != '\0')
		{
			v = *src++;
		}
		else
		{
			v = ' ';
		}
		str[i ^ 1] = v;
	}
}

/**
 * Copy a string into a buffer, padding with spaces. Does not add string
 * terminator.
 *
 * @param buf      Destination buffer
 * @param buf_size Size of destination buffer to fill in. Strings shorter than
 *                 this length will be padded with spaces.
 * @param src      Source string
 */
void ide_padstr8(uint8_t *buf, int buf_size, const char *src)
{
	int i;

	for (i = 0; i < buf_size; i++)
	{
		if (*src != '\0')
		{
			buf[i] = *src++;
		}
		else
		{
			buf[i] = ' ';
		}
	}
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY DEVICE" command
 */
static void ide_identify(IDE *ide)
{
	uint32_t d;
	char device_identify[9] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', 0 };

	uint64_t d_hpc, d_spt, d_tracks;
	uint64_t full_size = (hdd[ide->hdd_num].tracks * hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);

	device_identify[6] = (ide->hdd_num / 10) + 0x30;
	device_identify[7] = (ide->hdd_num % 10) + 0x30;
	ide_log("IDE Identify: %s\n", device_identify);

	memset(ide->buffer, 0, 512);
	d_spt = ide->spt;
	if (ide->hpc <= 16) {
		/* HPC <= 16, report as needed. */
		d_tracks = ide->tracks;
		d_hpc = ide->hpc;
	} else {
		/* HPC > 16, convert to 16 HPC. */
		d_hpc = 16;
		d_tracks = (ide->tracks * ide->hpc) / 16;
	}

	/* Specify default CHS translation */
	if (full_size <= 16514064) {
		ide->buffer[1] = d_tracks;	/* Tracks in default CHS translation. */
		ide->buffer[3] = d_hpc;		/* Heads in default CHS translation. */
		ide->buffer[6] = d_spt;		/* Heads in default CHS translation. */
	} else {
		ide->buffer[1] = 16383;		/* Tracks in default CHS translation. */
		ide->buffer[3] = 16;		/* Heads in default CHS translation. */
		ide->buffer[6] = 63;		/* Heads in default CHS translation. */
	}
	ide_log("Default CHS translation: %i, %i, %i\n", ide->buffer[1], ide->buffer[3], ide->buffer[6]);

	ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
	ide_padstr((char *) (ide->buffer + 23), EMU_VERSION, 8); /* Firmware */
	ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */
	ide->buffer[20] = 3;   /*Buffer type*/
	ide->buffer[21] = 512; /*Buffer size*/
	ide->buffer[48] = 1;   /*Dword transfers supported*/
	if (PCI && (ide->board < 2) && (hdd[ide->hdd_num].bus == HDD_BUS_IDE_PIO_AND_DMA)) {
		ide->buffer[47] = 32 | 0x8000;  /*Max sectors on multiple transfer command*/
		ide->buffer[49] = (1 << 8); /* LBA and DMA supported */
	} else {
		ide->buffer[47] = 16 | 0x8000;  /*Max sectors on multiple transfer command*/
		ide->buffer[49] = 0;
	}
	if ((ide->tracks >= 1024) || (ide->hpc > 16) || (ide->spt > 63))
	{
		ide->buffer[49] |= (1 << 9);
		ide_log("LBA supported\n");
	}
	ide->buffer[50] = 0x4000; /* Capabilities */
	ide->buffer[51] = 2 << 8; /*PIO timing mode*/

	if (ide->buffer[49] & (1 << 9))
	{
		ide->buffer[60] = full_size & 0xFFFF; /* Total addressable sectors (LBA) */
		ide->buffer[61] = (full_size >> 16) & 0x0FFF;
		ide_log("Full size: %" PRIu64 "\n", full_size);

		ide->buffer[53] |= 1;

		if (ide->specify_success) {
			ide->buffer[54] = (full_size / ide->t_hpc) / ide->t_spt;
			ide->buffer[55] = ide->t_hpc;
			ide->buffer[56] = ide->t_spt;
		} else {
			if (full_size <= 16514064) {
				ide->buffer[54] = d_tracks;
				ide->buffer[55] = d_hpc;
				ide->buffer[56] = d_spt;
			} else {
				ide->buffer[54] = 16383;
				ide->buffer[55] = 16;
				ide->buffer[56] = 63;
			}
		}

		full_size = ((uint64_t) ide->buffer[54]) * ((uint64_t) ide->buffer[55]) * ((uint64_t) ide->buffer[56]);

		ide->buffer[57] = full_size & 0xFFFF; /* Total addressable sectors (LBA) */
		ide->buffer[58] = (full_size >> 16) & 0x0FFF;

		ide_log("Current CHS translation: %i, %i, %i\n", ide->buffer[54], ide->buffer[55], ide->buffer[56]);
	}

	ide->buffer[59] = ide->blocksize ? (ide->blocksize | 0x100) : 0;

	if (ide->buffer[49] & (1 << 8))
	{
		ide->buffer[51] = 120;
		ide->buffer[52] = 120; /*DMA timing mode*/
		ide->buffer[53] |= 6;

		ide->buffer[62] = 7;
		ide->buffer[63] = 7;
		ide->buffer[64] = 3;	/*PIO Modes 3 & 4*/
		ide->buffer[88] = 7;
        	if (ide->mdma_mode != -1)
	        {
		    d = (ide->mdma_mode & 0xff);
		    d <<= 8;
		    if ((ide->mdma_mode & 0x300) == 0x200)
        	    	ide->buffer[88] |= d;
		    else if ((ide->mdma_mode & 0x300) == 0x100)
        	    	ide->buffer[63] |= d;
		    else if ((ide->mdma_mode & 0x300) == 0x400) {
			if ((ide->mdma_mode & 0xff) >= 3)
				ide->buffer[64] |= d;
		    } else
        	    	ide->buffer[62] |= d;
		    ide_log(" IDENTIFY DMA Mode: %04X, %04X\n", ide->buffer[62], ide->buffer[63]);
	        }
		ide->buffer[65] = 120;
		ide->buffer[66] = 120;
		ide->buffer[80] = 0x1e; /*ATA-1 to ATA-4 supported*/
		ide->buffer[81] = 0x18; /*ATA-4 revision 18 supported*/
	} else {
		ide->buffer[80] = 0x0e; /*ATA-1 to ATA-3 supported*/
	}
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY PACKET DEVICE" command
 */
static void ide_atapi_identify(IDE *ide)
{
	char device_identify[9] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', 0 };

	uint8_t cdrom_id;
	int32_t d;

	memset(ide->buffer, 0, 512);
	cdrom_id = atapi_cdrom_drives[ide->channel];

	device_identify[7] = cdrom_id + 0x30;
	ide_log("ATAPI Identify: %s\n", device_identify);

	ide->buffer[0] = 0x8000 | (5<<8) | 0x80 | (2<<5); /* ATAPI device, CD-ROM drive, removable media, accelerated DRQ */
	ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
	ide_padstr((char *) (ide->buffer + 23), EMU_VERSION, 8); /* Firmware */
	ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */
	ide->buffer[48] = 1;   /*Dword transfers supported*/
	ide->buffer[49] = 0x200; /* LBA supported */
	ide->buffer[51] = 2 << 8; /*PIO timing mode*/
	ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */

	if (PCI && (ide->board < 2) && (cdrom_drives[cdrom_id].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA))
	{
		ide->buffer[49] |= 0x100; /* DMA supported */
		ide->buffer[51] = 120;
		ide->buffer[52] = 120; /*DMA timing mode*/
		ide->buffer[53] = 7;
		ide->buffer[62] = 7;
		ide->buffer[63] = 7;
		ide->buffer[64] = 3;	/*PIO Modes 3 & 4*/
		ide->buffer[88] = 7;
        	if (ide->mdma_mode != -1)
	        {
		    d = (ide->mdma_mode & 0xff);
		    d <<= 8;
		    if ((ide->mdma_mode & 0x300) == 0x200)
        	    	ide->buffer[88] |= d;
		    else if ((ide->mdma_mode & 0x300) == 0x100)
        	    	ide->buffer[63] |= d;
		    else if ((ide->mdma_mode & 0x300) == 0x400) {
			if ((ide->mdma_mode & 0xff) >= 3)
				ide->buffer[64] |= d;
		    } else
        	    	ide->buffer[62] |= d;
		    ide_log("PIDENTIFY DMA Mode: %04X, %04X\n", ide->buffer[62], ide->buffer[63]);
	        }
		ide->buffer[65] = 120;
		ide->buffer[66] = 120;
		ide->buffer[67] = 120;
		ide->buffer[71] = 30;
		ide->buffer[72] = 30;
		ide->buffer[80] = 0x1e; /*ATA-1 to ATA-4 supported*/
		ide->buffer[81] = 0x18; /*ATA-4 revision 18 supported*/
	}
}

static void ide_atapi_zip_identify(IDE *ide)
{
	uint8_t zip_id;
	int32_t d;

	zip_id = atapi_zip_drives[ide->channel];

	/* Using (2<<5) below makes the ASUS P/I-P54TP4XE misdentify the ZIP drive
	   as a LS-120. */
	ide->buffer[0] = 0x8000 | (0<<8) | 0x80 | (1<<5); /* ATAPI device, direct-access device, removable media, accelerated DRQ */
	ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
	if (zip_drives[zip_id].is_250) {
		ide_padstr((char *) (ide->buffer + 23), "42.S", 8); /* Firmware */
		ide_padstr((char *) (ide->buffer + 27), "IOMEGA  ZIP 250       ATAPI", 40); /* Model */
	} else {
		ide_padstr((char *) (ide->buffer + 23), "E.08", 8); /* Firmware */
		ide_padstr((char *) (ide->buffer + 27), "IOMEGA ZIP 100 ATAPI", 40); /* Model */
	}
	ide->buffer[49] = 0x200; /* LBA supported */

	/* Note by Kotori: Look at this if this is supported by ZIP at all. */
	ide->buffer[51] = 2 << 8; /*PIO timing mode*/

	ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */

	if (PCI && (ide->board < 2) && (zip_drives[zip_id].bus_type == ZIP_BUS_ATAPI_PIO_AND_DMA))
	{
		ide->buffer[49] |= 0x100; /* DMA supported */
		if (zip_drives[zip_id].is_250) {
			ide->buffer[52] = 0 << 8; /*DMA timing mode*/
			ide->buffer[53] = 6;
			ide->buffer[63] = 3;
			ide->buffer[88] = 7;
			ide->buffer[64] = 0x0001; /*PIO Mode 3*/
			ide->buffer[65] = 0x96;
			ide->buffer[66] = 0x96;
			ide->buffer[67] = 0xb4;
			ide->buffer[68] = 0xb4;
			ide->buffer[80] = 0x30; /*Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-5*/
			ide->buffer[81] = 0x15; /*Maximum ATA revision supported : ATA/ATAPI-5 T13 1321D revision 1*/
		} else {
			ide->buffer[51] = 120;
			ide->buffer[52] = 120;
			ide->buffer[53] = 2;	/*Words 64-70 are valid*/
			ide->buffer[63] = 0x0003; /*Multi-word DMA 0 & 1*/
			ide->buffer[64] = 0x0001; /*PIO Mode 3*/
			ide->buffer[65] = 120;
			ide->buffer[66] = 120;
			ide->buffer[67] = 120;
		}

       		if (ide->mdma_mode != -1)
	        {
		    d = (ide->mdma_mode & 0xff);
		    d <<= 8;
		    if ((ide->mdma_mode & 0x300) == 0x200)
        	    	ide->buffer[88] |= d;
		    else if ((ide->mdma_mode & 0x300) == 0x400) {
			if ((ide->mdma_mode & 0xff) >= 3)
				ide->buffer[64] |= d;
		    } else
        	    	ide->buffer[63] |= d;
		    ide_log("PIDENTIFY DMA Mode: %04X, %04X\n", ide->buffer[62], ide->buffer[63]);
	        }
	}
}

/*
 * Return the sector offset for the current register values
 */
static off64_t ide_get_sector(IDE *ide)
{
        if (ide->lba)
        {
                return (off64_t)ide->lba_addr + ide->skip512;
        }
        else
        {
        	uint32_t heads = ide->t_hpc;
        	uint32_t sectors = ide->t_spt;

        	return ((((off64_t) ide->cylinder * heads) + ide->head) *
        	          sectors) + (ide->sector - 1) + ide->skip512;
        }
}

/**
 * Move to the next sector using CHS addressing
 */
static void ide_next_sector(IDE *ide)
{
        if (ide->lba)
        {
                ide->lba_addr++;
        }
        else
        {
        	ide->sector++;
        	if (ide->sector == (ide->t_spt + 1))
			{
        		ide->sector = 1;
        		ide->head++;
        		if (ide->head == ide->t_hpc)
				{
        			ide->head = 0;
        			ide->cylinder++;
				}
			}
		}
}

static void loadhd(IDE *ide, int d, const wchar_t *fn)
{
	if (! hdd_image_load(d)) {
		ide->type = IDE_NONE;
		return;
	}

	ide->spt = hdd[d].spt;
	ide->hpc = hdd[d].hpc;
	ide->tracks = hdd[d].tracks;
	ide->type = IDE_HDD;
	ide->hdd_num = d;
	ide->hdi = hdd_image_get_type(d);
}

void ide_set_signature(IDE *ide)
{
	uint8_t cdrom_id = atapi_cdrom_drives[ide->channel];
	uint8_t zip_id = atapi_zip_drives[ide->channel];
	ide->sector=1;
	ide->head=0;
	if (ide_drive_is_zip(ide))
	{
		zip_set_signature(zip_id);
		ide->secount = zip[zip_id].phase;
		ide->cylinder = zip[zip_id].request_length;
	}
	else if (ide_drive_is_cdrom(ide))
	{
		cdrom_set_signature(cdrom_id);
		ide->secount = cdrom[cdrom_id].phase;
		ide->cylinder = cdrom[cdrom_id].request_length;
	}
	else
	{
		ide->secount=1;
		ide->cylinder=((ide->type == IDE_HDD) ? 0 : 0xFFFF);
		if (ide->type == IDE_HDD)
		{
			ide->drive = 0;
		}
	}
}

static int ide_set_features(IDE *ide)
{
	uint8_t features, features_data;
	uint8_t mode, submode;

	int bus, dma;
	int max_pio = 2, max_mdma = 2;

	features = ide->cylprecomp;
	features_data = ide->secount;

	if (ide_drive_is_zip(ide)) {
		bus = zip_drives[atapi_zip_drives[ide->channel]].bus_type;
		dma = (bus == ZIP_BUS_ATAPI_PIO_AND_DMA);
		if (!PCI || !dma || (ide->board >= 2))
			max_pio = 0;
		else
			max_pio = 3;
		max_mdma = 1;
	} else if (ide_drive_is_cdrom(ide)) {
		bus = cdrom_drives[atapi_cdrom_drives[ide->channel]].bus_type;
		dma = (bus == CDROM_BUS_ATAPI_PIO_AND_DMA);
		if (!PCI || !dma || (ide->board >= 2))
			max_pio = 0;
		else
			max_pio = 4;
	} else {
		bus = hdd[ide->hdd_num].bus;
		dma = (bus == HDD_BUS_IDE_PIO_AND_DMA);
		if (!PCI || !dma || (ide->board >= 2))
			max_pio = 0;
		else
			max_pio = 2;
	}

	ide_log("Features code %02X\n", features);

	ide_log("IDE %02X: Set features: %02X, %02X\n", ide->channel, features, features_data);

	switch(features)
	{
		case FEATURE_SET_TRANSFER_MODE:	/* Set transfer mode. */
			ide_log("Transfer mode %02X\n", features_data >> 3);

			mode = (features_data >> 3);
			submode = features_data & 7;

			switch(mode)
			{
				case 0x00:	/* PIO default */
					if (submode != 0)
					{
						return 0;
					}
					ide->mdma_mode = -1;
					ide_log("IDE %02X: Setting DPIO mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
					break;

				case 0x01:	/* PIO mode */
					if (submode > max_pio)
					{
						return 0;
					}
					ide->mdma_mode = (1 << submode) | 0x400;
					ide_log("IDE %02X: Setting  PIO mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
					break;

				case 0x02:	/* Singleword DMA mode */
					if (!PCI || !dma || ide_drive_is_zip(ide) || (ide->board >= 2) || (submode > 2))
					{
						return 0;
					}
					ide->mdma_mode = (1 << submode);
					ide_log("IDE %02X: Setting SDMA mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
					break;

				case 0x04:	/* Multiword DMA mode */
					if (!PCI || !dma || (ide->board >= 2) || (submode > max_mdma))
					{
						return 0;
					}
					ide->mdma_mode = (1 << submode) | 0x100;
					ide_log("IDE %02X: Setting MDMA mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
					break;

				case 0x08:	/* Ultra DMA mode */
					if (!PCI || !dma || (ide->board >= 2) || (submode > 2))
					{
						return 0;
					}
					ide->mdma_mode = (1 << submode) | 0x200;
					ide_log("IDE %02X: Setting UDMA mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
					break;

				default:
					return 0;
			}

		case FEATURE_ENABLE_IRQ_OVERLAPPED:
		case FEATURE_ENABLE_IRQ_SERVICE:
		case FEATURE_DISABLE_IRQ_OVERLAPPED:
		case FEATURE_DISABLE_IRQ_SERVICE:
			if (!PCI || !dma || (ide->board >= 2))
				return 0;
			else
				return 1;

		case FEATURE_DISABLE_REVERT:	/* Disable reverting to power on defaults. */
		case FEATURE_ENABLE_REVERT:	/* Enable reverting to power on defaults. */
			return 1;

		default:
			return 0;
	}

	return 1;
}

void ide_set_sector(IDE *ide, int64_t sector_num)
{
	unsigned int cyl, r;
	if (ide->lba)
	{
		ide->head = (sector_num >> 24);
		ide->cylinder = (sector_num >> 8);
		ide->sector = (sector_num);
	}
	else
	{
		cyl = sector_num / (hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);
		r = sector_num % (hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);
		ide->cylinder = cyl;
		ide->head = ((r / hdd[ide->hdd_num].spt) & 0x0f);
		ide->sector = (r % hdd[ide->hdd_num].spt) + 1;
	}
}

void ide_ter_disable_cond();
void ide_qua_disable_cond();


void ide_destroy_buffers(void)
{
	int d;

	for (d = 0; d < (IDE_NUM+XTIDE_NUM); d++)
	{
		if (ide_drives[d].buffer) {
			free(ide_drives[d].buffer);
			ide_drives[d].buffer = NULL;
		}

		if (ide_drives[d].sector_buffer) {
			free(ide_drives[d].sector_buffer);
			ide_drives[d].sector_buffer = NULL;
		}
	}
}

void ide_reset(void)
{
	int c, d;

	build_atapi_cdrom_map();
	build_atapi_zip_map();

	/* Close hard disk image files (if previously open) */
	for (d = 0; d < (IDE_NUM+XTIDE_NUM); d++)
	{
		ide_drives[d].channel = d;
		ide_drives[d].type = IDE_NONE;
		if (ide_drives[d].hdd_num != -1)
			hdd_image_close(ide_drives[d].hdd_num);
		if ((d < 8) && ide_drive_is_zip(&ide_drives[d]))
		{
			zip[atapi_zip_drives[d]].status = READY_STAT | DSC_STAT;
		}
		else if ((d < 8) && ide_drive_is_cdrom(&ide_drives[d]))
		{
			cdrom[atapi_cdrom_drives[d]].status = READY_STAT | DSC_STAT;
		}
		ide_drives[d].atastat = READY_STAT | DSC_STAT;
		ide_drives[d].service = 0;
		ide_drives[d].board = d >> 1;

		if (ide_drives[d].buffer) {
			free(ide_drives[d].buffer);
			ide_drives[d].buffer = NULL;
		}

		if (ide_drives[d].sector_buffer) {
			free(ide_drives[d].sector_buffer);
			ide_drives[d].sector_buffer = NULL;
		}
	}

	idecallback[0]=idecallback[1]=0LL;
	idecallback[2]=idecallback[3]=0LL;
	idecallback[4]=0LL;

	ide_log("IDE: loading disks...\n");
	c = 0;
	for (d = 0; d < HDD_NUM; d++)
	{
		if (((hdd[d].bus == HDD_BUS_IDE_PIO_ONLY) || (hdd[d].bus == HDD_BUS_IDE_PIO_AND_DMA)) && (hdd[d].ide_channel < IDE_NUM))
		{
			ide_log("Found IDE hard disk on channel %i\n", hdd[d].ide_channel);
			loadhd(&ide_drives[hdd[d].ide_channel], d, hdd[d].fn);
			ide_drives[hdd[d].ide_channel].sector_buffer = (uint8_t *) malloc(256*512);
			if (++c >= (IDE_NUM+XTIDE_NUM)) break;
		}
		if ((hdd[d].bus==HDD_BUS_XTIDE) && (hdd[d].xtide_channel < XTIDE_NUM))
		{
			ide_log("Found XT IDE hard disk on channel %i\n", hdd[d].xtide_channel);
			loadhd(&ide_drives[hdd[d].xtide_channel | 8], d, hdd[d].fn);
			ide_drives[hdd[d].xtide_channel | 8].sector_buffer = (uint8_t *) malloc(256*512);
			if (++c >= (IDE_NUM+XTIDE_NUM)) break;
		}
	}
	ide_log("IDE: done, loaded %d disks.\n", c);

	for (d = 0; d < IDE_NUM; d++)
	{
		if (ide_drive_is_zip(&ide_drives[d]) && (ide_drives[d].type == IDE_NONE))
			ide_drives[d].type = IDE_ZIP;
		else if (ide_drive_is_cdrom(&ide_drives[d]) && (ide_drives[d].type == IDE_NONE))
			ide_drives[d].type = IDE_CDROM;

		if (ide_drives[d].type != IDE_NONE)
			ide_drives[d].buffer = (uint16_t *) malloc(65536 * sizeof(uint16_t));

		ide_set_signature(&ide_drives[d]);

		ide_drives[d].mdma_mode = -1;
		ide_drives[d].error = 1;
	}

	for (d = 0; d < XTIDE_NUM; d++)
	{
		ide_set_signature(&ide_drives[d | 8]);

		ide_drives[d | 8].mdma_mode = -1;
		ide_drives[d | 8].error = 1;
	}

	for (d = 0; d < 5; d++)
	{
		cur_ide[d] = d << 1;
	}

	ide_ter_disable_cond();
	ide_qua_disable_cond();
}


void ide_reset_hard(void)
{
	int d;

	for (d = 0; d < (IDE_NUM+XTIDE_NUM); d++)
	{
		ide_drives[d].t_spt = ide_drives[d].spt;
		ide_drives[d].t_hpc = ide_drives[d].hpc;
		ide_drives[d].specify_success = 0;
	}

	ide_reset();
}


int idetimes = 0;

void ide_set_callback(uint8_t channel, int64_t callback)
{
	IDE *ide = &ide_drives[channel];
	if (callback)
		idecallback[ide->board] += callback;
	else
		idecallback[ide->board] = 0LL;
}

void ide_write_data(int ide_board, uint32_t val, int length)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];

	uint8_t *idebufferb = (uint8_t *) ide->buffer;
	uint16_t *idebufferw = ide->buffer;
	uint32_t *idebufferl = (uint32_t *) ide->buffer;
	
	if (ide->command == WIN_PACKETCMD)
	{
		ide->pos = 0;

		if (!ide_drive_is_zip(ide) && !ide_drive_is_cdrom(ide))
		{
			return;
		}

		if (ide_drive_is_zip(ide))
			zip_write(cur_ide[ide_board], val, length);
		else
			cdrom_write(cur_ide[ide_board], val, length);
		return;
	}
	else
	{
		switch(length)
		{
			case 1:
				idebufferb[ide->pos] = val & 0xff;
				ide->pos++;
				break;
			case 2:
				idebufferw[ide->pos >> 1] = val & 0xffff;
				ide->pos += 2;
				break;
			case 4:
				idebufferl[ide->pos >> 2] = val;
				ide->pos += 4;
				break;
			default:
				return;
		}

		if (ide->pos>=512)
		{
			ide->pos=0;
			ide->atastat = BUSY_STAT;
			timer_process();
			if (ide->command == WIN_WRITE_MULTIPLE)
			{
				callbackide(ide_board);
			}
			else
			{
				idecallback[ide_board]=6LL*IDE_TIME;
			}
			timer_update_outstanding();
		}
	}
}

void writeidew(int ide_board, uint16_t val)
{
	ide_write_data(ide_board, val, 2);
}

void writeidel(int ide_board, uint32_t val)
{
	writeidew(ide_board, val);
	writeidew(ide_board, val >> 16);
}

void writeide(int ide_board, uint16_t addr, uint8_t val)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];
	IDE *ide_other = &ide_drives[cur_ide[ide_board] ^ 1];

	ide_log("WriteIDE %04X %02X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc);
	addr|=0x90;
	addr&=0xFFF7;

	if (ide->type == IDE_NONE && (addr == 0x1f0 || addr == 0x1f7)) return;
        
	switch (addr)
	{
		case 0x1F0: /* Data */
			writeidew(ide_board, val | (val << 8));
			return;

		/* Note to self: for ATAPI, bit 0 of this is DMA if set, PIO if clear. */
		case 0x1F1: /* Features */
			if (ide_drive_is_zip(ide))
			{
				ide_log("ATAPI transfer mode: %s\n", (val & 1) ? "DMA" : "PIO");
				zip[atapi_zip_drives[cur_ide[ide_board]]].features = val;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				ide_log("ATAPI transfer mode: %s\n", (val & 1) ? "DMA" : "PIO");
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].features = val;
			}
			ide->cylprecomp = val;

			if (ide_drive_is_zip(ide_other))
			{
				zip[atapi_zip_drives[cur_ide[ide_board] ^ 1]].features = val;
			}
			else if (ide_drive_is_cdrom(ide_other))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].features = val;
			}
			ide_other->cylprecomp = val;
			return;

		case 0x1F2: /* Sector count */
			if (ide_drive_is_zip(ide))
			{
				ide_log("Sector count write: %i\n", val);
				zip[atapi_zip_drives[cur_ide[ide_board]]].phase = val;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				ide_log("Sector count write: %i\n", val);
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].phase = val;
			}
			ide->secount = val;

			if (ide_drive_is_zip(ide_other))
			{
				ide_log("Other sector count write: %i\n", val);
				zip[atapi_zip_drives[cur_ide[ide_board] ^ 1]].phase = val;
			}
			else if (ide_drive_is_cdrom(ide_other))
			{
				ide_log("Other sector count write: %i\n", val);
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].phase = val;
			}
			ide_other->secount = val;
			return;

		case 0x1F3: /* Sector */
			ide->sector = val;
			ide->lba_addr = (ide->lba_addr & 0xFFFFF00) | val;
			ide_other->sector = val;
			ide_other->lba_addr = (ide_other->lba_addr & 0xFFFFF00) | val;
			return;

		case 0x1F4: /* Cylinder low */
			if (ide_drive_is_zip(ide))
			{
				zip[atapi_zip_drives[cur_ide[ide_board]]].request_length &= 0xFF00;
				zip[atapi_zip_drives[cur_ide[ide_board]]].request_length |= val;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length &= 0xFF00;
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length |= val;
			}
			ide->cylinder = (ide->cylinder & 0xFF00) | val;
			ide->lba_addr = (ide->lba_addr & 0xFFF00FF) | (val << 8);

			if (ide_drive_is_zip(ide_other))
			{
				zip[atapi_zip_drives[cur_ide[ide_board] ^ 1]].request_length &= 0xFF00;
				zip[atapi_zip_drives[cur_ide[ide_board] ^ 1]].request_length |= val;
			}
			else if (ide_drive_is_cdrom(ide_other))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].request_length &= 0xFF00;
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].request_length |= val;
			}
			ide_other->cylinder = (ide_other->cylinder&0xFF00) | val;
			ide_other->lba_addr = (ide_other->lba_addr&0xFFF00FF) | (val << 8);
			return;

		case 0x1F5: /* Cylinder high */
			if (ide_drive_is_zip(ide))
			{
				zip[atapi_zip_drives[cur_ide[ide_board]]].request_length &= 0xFF;
				zip[atapi_zip_drives[cur_ide[ide_board]]].request_length |= (val << 8);
			}
			else if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length &= 0xFF;
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length |= (val << 8);
			}
			ide->cylinder = (ide->cylinder & 0xFF) | (val << 8);
			ide->lba_addr = (ide->lba_addr & 0xF00FFFF) | (val << 16);

			if (ide_drive_is_zip(ide_other))
			{
				zip[atapi_zip_drives[cur_ide[ide_board] ^ 1]].request_length &= 0xFF;
				zip[atapi_zip_drives[cur_ide[ide_board] ^ 1]].request_length |= (val << 8);
			}
			else if (ide_drive_is_cdrom(ide_other))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].request_length &= 0xFF;
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].request_length |= (val << 8);
			}
			ide_other->cylinder = (ide_other->cylinder & 0xFF) | (val << 8);
			ide_other->lba_addr = (ide_other->lba_addr & 0xF00FFFF) | (val << 16);
			return;

		case 0x1F6: /* Drive/Head */
			if (cur_ide[ide_board] != ((val>>4)&1)+(ide_board<<1))
			{
				cur_ide[ide_board]=((val>>4)&1)+(ide_board<<1);

				if (ide->reset || ide_other->reset)
				{
					ide->atastat = ide_other->atastat = READY_STAT | DSC_STAT;
					ide->error = ide_other->error = 1;
					ide->secount = ide_other->secount = 1;
					ide->sector = ide_other->sector = 1;
					ide->head = ide_other->head = 0;
					ide->cylinder = ide_other->cylinder = 0;
					ide->reset = ide_other->reset = 0;

					if (ide_drive_is_zip(ide))
					{
						zip[atapi_zip_drives[ide->channel]].status = READY_STAT | DSC_STAT;
						zip[atapi_zip_drives[ide->channel]].error = 1;
						zip[atapi_zip_drives[ide->channel]].phase = 1;
						zip[atapi_zip_drives[ide->channel]].request_length = 0xEB14;
						zip[atapi_zip_drives[ide->channel]].callback = 0LL;
						ide->cylinder = 0xEB14;
					}
					else if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = READY_STAT | DSC_STAT;
						cdrom[atapi_cdrom_drives[ide->channel]].error = 1;
						cdrom[atapi_cdrom_drives[ide->channel]].phase = 1;
						cdrom[atapi_cdrom_drives[ide->channel]].request_length = 0xEB14;
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 0LL;
						ide->cylinder = 0xEB14;
					}

					if (ide_drive_is_zip(ide_other))
					{
						zip[atapi_zip_drives[ide_other->channel]].status = READY_STAT | DSC_STAT;
						zip[atapi_zip_drives[ide_other->channel]].error = 1;
						zip[atapi_zip_drives[ide_other->channel]].phase = 1;
						zip[atapi_zip_drives[ide_other->channel]].request_length = 0xEB14;
						zip[atapi_zip_drives[ide_other->channel]].callback = 0LL;
						ide->cylinder = 0xEB14;
					}
					else if (ide_drive_is_cdrom(ide_other))
					{
						cdrom[atapi_cdrom_drives[ide_other->channel]].status = READY_STAT | DSC_STAT;
						cdrom[atapi_cdrom_drives[ide_other->channel]].error = 1;
						cdrom[atapi_cdrom_drives[ide_other->channel]].phase = 1;
						cdrom[atapi_cdrom_drives[ide_other->channel]].request_length = 0xEB14;
						cdrom[atapi_cdrom_drives[ide_other->channel]].callback = 0LL;
						ide->cylinder = 0xEB14;
					}

					idecallback[ide_board] = 0LL;
					timer_update_outstanding();
					return;
				}

				ide = &ide_drives[cur_ide[ide_board]];
			}
                                
			ide->head = val & 0xF;
			ide->lba = val & 0x40;
			ide_other->head = val & 0xF;
			ide_other->lba = val & 0x40;
                
			ide->lba_addr = (ide->lba_addr & 0x0FFFFFF) | ((val & 0xF) << 24);
			ide_other->lba_addr = (ide_other->lba_addr & 0x0FFFFFF)|((val & 0xF) << 24);

			return;

		case 0x1F7: /* Command register */
			if (ide->type == IDE_NONE)
			{
				return;
			}

		ide_irq_lower(ide);
		ide->command=val;

		ide->error=0;
		if (ide_drive_is_zip(ide))
		{
			zip[atapi_zip_drives[ide->channel]].error = 0;
		}
		else if (ide_drive_is_cdrom(ide))
		{
			cdrom[atapi_cdrom_drives[ide->channel]].error = 0;
		}
		if (((val >= WIN_RESTORE) && (val <= 0x1F)) || ((val >= WIN_SEEK) && (val <= 0x7F)))
		{
			if (ide_drive_is_zip(ide))
			{
				zip[atapi_zip_drives[ide->channel]].status = READY_STAT;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[ide->channel]].status = READY_STAT;
			}
			else
			{
				ide->atastat = BUSY_STAT;
			}
			timer_process();
			if (ide_drive_is_zip(ide))
			{
				zip[atapi_zip_drives[ide->channel]].callback = 100LL*IDE_TIME;
			}
			if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[ide->channel]].callback = 100LL*IDE_TIME;
			}
			idecallback[ide_board]=40000LL * TIMER_USEC /*100LL*IDE_TIME*/;
			timer_update_outstanding();
			return;
		}
		switch (val)
		{
			case WIN_SRST: /* ATAPI Device Reset */
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				else
				{
					ide->atastat = READY_STAT;
				}
				timer_process();
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].callback = 100LL*IDE_TIME;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 100LL*IDE_TIME;
				}
                	        idecallback[ide_board]=100LL*IDE_TIME;
	                        timer_update_outstanding();
        	                return;

			case WIN_READ_MULTIPLE:
				/* Fatal removed in accordance with the official ATAPI reference:
				   If the Read Multiple command is attempted before the Set Multiple Mode
				   command  has  been  executed  or  when  Read  Multiple  commands  are
				   disabled, the Read Multiple operation is rejected with an Aborted Com-
				   mand error. */
				ide->blockcount = 0;

			case WIN_READ:
			case WIN_READ_NORETRY:
			case WIN_READ_DMA:
			case WIN_READ_DMA_ALT:
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				else
				{
					ide->atastat = BUSY_STAT;
				}
				timer_process();
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				idecallback[ide_board]=200LL*IDE_TIME;
				timer_update_outstanding();
				ide->do_initial_read = 1;
				return;

			case WIN_WRITE_MULTIPLE:
				if (!ide->blocksize && !ide_drive_is_zip(ide) && !ide_drive_is_cdrom(ide))
				{
					fatal("Write_MULTIPLE - blocksize = 0\n");
				}
				ide->blockcount = 0;

			case WIN_WRITE:
			case WIN_WRITE_NORETRY:
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = DRQ_STAT | DSC_STAT | READY_STAT;
					zip[atapi_zip_drives[ide->channel]].pos = 0;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = DRQ_STAT | DSC_STAT | READY_STAT;
					cdrom[atapi_cdrom_drives[ide->channel]].pos = 0;
				}
				else
				{
					ide->atastat = DRQ_STAT | DSC_STAT | READY_STAT;
					ide->pos=0;
				}
				return;

			case WIN_WRITE_DMA:
			case WIN_WRITE_DMA_ALT:
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				else
				{
					ide->atastat = BUSY_STAT;
				}
				timer_process();
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				idecallback[ide_board]=200LL*IDE_TIME;
				timer_update_outstanding();
				return;

			case WIN_VERIFY:
			case WIN_VERIFY_ONCE:
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				else
				{
					ide->atastat = BUSY_STAT;
				}
				timer_process();
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				idecallback[ide_board]=200LL*IDE_TIME;
				timer_update_outstanding();
				return;

			case WIN_FORMAT:
				if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
				{
					goto ide_bad_command;
				}
				else
				{
					ide->atastat = DRQ_STAT;
					ide->pos=0;
				}
				return;

			case WIN_SPECIFY: /* Initialize Drive Parameters */
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				else
				{
					ide->atastat = BUSY_STAT;
				}
				timer_process();
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].callback = 30LL*IDE_TIME;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 30LL*IDE_TIME;
				}
				idecallback[ide_board]=30LL*IDE_TIME;
				timer_update_outstanding();
				return;

			case WIN_DRIVE_DIAGNOSTICS: /* Execute Drive Diagnostics */
				if (ide_drive_is_zip(ide))
						zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				else if (ide_drive_is_cdrom(ide))
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				else
					ide->atastat = BUSY_STAT;

				if (ide_drive_is_zip(ide_other))
					zip[atapi_zip_drives[ide_other->channel]].status = BUSY_STAT;
				else if (ide_drive_is_cdrom(ide_other))
					cdrom[atapi_cdrom_drives[ide_other->channel]].status = BUSY_STAT;
				else
					ide_other->atastat = BUSY_STAT;

				timer_process();
				if (ide_drive_is_zip(ide))
					zip[atapi_zip_drives[ide->channel]].callback = 200LL * IDE_TIME;
				else if (ide_drive_is_cdrom(ide))
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 200LL * IDE_TIME;
				idecallback[ide_board] = 200LL * IDE_TIME;
				timer_update_outstanding();
				return;

			case WIN_PIDENTIFY: /* Identify Packet Device */
			case WIN_SET_MULTIPLE_MODE: /* Set Multiple Mode */
			case WIN_NOP:
			case WIN_STANDBYNOW1:
			case WIN_IDLENOW1:
			case WIN_SETIDLE1: /* Idle */
			case WIN_CHECKPOWERMODE1:
			case WIN_SLEEP1:
				if (ide_drive_is_zip(ide))
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				else if (ide_drive_is_cdrom(ide))
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				else
					ide->atastat = BUSY_STAT;
				timer_process();
				callbackide(ide_board);
				timer_update_outstanding();
				return;

			case WIN_IDENTIFY: /* Identify Device */
			case WIN_SET_FEATURES: /* Set Features */
			case WIN_READ_NATIVE_MAX:
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				else
				{
					ide->atastat = BUSY_STAT;
				}
				timer_process();
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 200LL*IDE_TIME;
				}
				idecallback[ide_board]=200LL*IDE_TIME;
				timer_update_outstanding();
				return;

			case WIN_PACKETCMD: /* ATAPI Packet */
				/* Skip the command callback wait, and process immediately. */
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].packet_status = ZIP_PHASE_IDLE;
					zip[atapi_zip_drives[ide->channel]].pos=0;
					zip[atapi_zip_drives[ide->channel]].phase = 1;
					zip[atapi_zip_drives[ide->channel]].status = READY_STAT | DRQ_STAT | (zip[cur_ide[ide_board]].status & ERR_STAT);
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].packet_status = CDROM_PHASE_IDLE;
					cdrom[atapi_cdrom_drives[ide->channel]].pos=0;
					cdrom[atapi_cdrom_drives[ide->channel]].phase = 1;
					cdrom[atapi_cdrom_drives[ide->channel]].status = READY_STAT | DRQ_STAT | (cdrom[cur_ide[ide_board]].status & ERR_STAT);
				}
				else
				{
					ide->atastat = BUSY_STAT;
					timer_process();
					idecallback[ide_board]=200LL*IDE_TIME;
					timer_update_outstanding();
					ide->pos=0;
				}
				return;

			case 0xF0:
			default:
ide_bad_command:
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = READY_STAT | ERR_STAT | DSC_STAT;
					zip[atapi_zip_drives[ide->channel]].error = ABRT_ERR;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = READY_STAT | ERR_STAT | DSC_STAT;
					cdrom[atapi_cdrom_drives[ide->channel]].error = ABRT_ERR;
				}
				else
				{
					ide->atastat = READY_STAT | ERR_STAT | DSC_STAT;
					ide->error = ABRT_ERR;
				}
				ide_irq_raise(ide);
				return;
			}
			return;

        case 0x3F6: /* Device control */
			if ((ide->fdisk & 4) && !(val&4) && (ide->type != IDE_NONE || ide_other->type != IDE_NONE))
			{
				timer_process();
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].callback = 0LL;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 0LL;
				}
				idecallback[ide_board]=500LL*IDE_TIME;
				timer_update_outstanding();

				if (ide->type != IDE_NONE)
				{
					ide->reset = 1;
				}
				if (ide_other->type != IDE_NONE)
				{
					ide->reset = 1;
				}
				if (ide_drive_is_zip(ide))
				{
					zip[atapi_zip_drives[ide->channel]].status = BUSY_STAT;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				ide->atastat = ide_other->atastat = BUSY_STAT;
			}
			if (val & 4)
			{
				/*Drive held in reset*/
				timer_process();
				idecallback[ide_board] = 0LL;
				timer_update_outstanding();
				ide->atastat = ide_other->atastat = BUSY_STAT;
			}
			ide->fdisk = ide_other->fdisk = val;
			return;
	}
}

uint32_t ide_read_data(int ide_board, int length)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];
	uint32_t temp;

	if (!ide->buffer) {
		switch (length)
		{
			case 1:
				return 0xff;
			case 2:
				return 0xffff;
			case 4:
				return 0xffffffff;
			default:
				return 0;
		}
	}

	uint8_t *idebufferb = (uint8_t *) ide->buffer;
	uint16_t *idebufferw = ide->buffer;
	uint32_t *idebufferl = (uint32_t *) ide->buffer;
	
	if (ide->command == WIN_PACKETCMD)
	{
		ide->pos = 0;
		if (!ide_drive_is_zip(ide) && !ide_drive_is_cdrom(ide))
		{
			ide_log("Drive not ZIP or CD-ROM (position: %i)\n", ide->pos);
			return 0;
		}
		if (ide_drive_is_zip(ide))
			temp = zip_read(cur_ide[ide_board], length);
		else
			temp = cdrom_read(cur_ide[ide_board], length);
	}
	else
	{
		switch (length)
		{
			case 1:
				temp = idebufferb[ide->pos];
				ide->pos++;
				break;
			case 2:
				temp = idebufferw[ide->pos >> 1];
				ide->pos += 2;
				break;
			case 4:
				temp = idebufferl[ide->pos >> 2];
				ide->pos += 4;
				break;
			default:
				return 0;
		}
	}
	if (ide->pos>=512 && ide->command != WIN_PACKETCMD)
	{
		ide->pos=0;
		ide->atastat = READY_STAT | DSC_STAT;
		if (ide_drive_is_zip(ide))
		{
			zip[atapi_zip_drives[cur_ide[ide_board]]].status = READY_STAT | DSC_STAT;
			zip[atapi_zip_drives[cur_ide[ide_board]]].packet_status = ZIP_PHASE_IDLE;
		}
		else if (ide_drive_is_cdrom(ide))
		{
			cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].status = READY_STAT | DSC_STAT;
			cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].packet_status = CDROM_PHASE_IDLE;
		}
		if (ide->command == WIN_READ || ide->command == WIN_READ_NORETRY || ide->command == WIN_READ_MULTIPLE)
		{
			ide->secount = (ide->secount - 1) & 0xff;
			if (ide->secount)
			{
				ide_next_sector(ide);
				ide->atastat = BUSY_STAT;
				timer_process();
				if (ide->command == WIN_READ_MULTIPLE)
				{
					callbackide(ide_board);
				}
				else
				{
					idecallback[ide_board]=6LL*IDE_TIME;
				}
				timer_update_outstanding();
			}
			else
			{
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
			}
		}
	}

	return temp;
}

uint8_t readide(int ide_board, uint16_t addr)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];
	uint8_t temp;
	uint16_t tempw;
	
	addr |= 0x90;
	addr &= 0xFFF7;

	switch (addr)
	{
		case 0x1F0: /* Data */
			tempw = readidew(ide_board);
			temp = tempw & 0xff;
			break;

		/* For ATAPI: Bits 7-4 = sense key, bit 3 = MCR (media change requested),
		              Bit 2 = ABRT (aborted command), Bit 1 = EOM (end of media),
		              and Bit 0 = ILI (illegal length indication). */
		case 0x1F1: /* Error */
			if (ide->type == IDE_NONE)
			{
				temp = 0;
			}
			else
			{
				if (ide_drive_is_zip(ide))
				{
					temp = zip[atapi_zip_drives[cur_ide[ide_board]]].error;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					temp = cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].error;
				}
				else
				{
					temp = ide->error;
				}
			}
			break;

		/* For ATAPI:
			Bit 0: Command or Data:
				Data if clear, Command if set;
			Bit 1: I/OB
				Direction:
					To device if set;
					From device if clear.
			IO		DRQ		CoD
			0		1		1		Ready to accept command packet
			1		1		1		Message - ready to send message to host
			1		1		0		Data to host
			0		1		0		Data from host
			1		0		1		Status. */
		case 0x1F2: /* Sector count */
			if (ide_drive_is_zip(ide))
			{
				temp = zip[atapi_zip_drives[cur_ide[ide_board]]].phase;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				temp = cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].phase;
			}
			else
			{
				temp = ide->secount;
			}
			break;

		case 0x1F3: /* Sector */
			temp = (uint8_t)ide->sector;
			break;

		case 0x1F4: /* Cylinder low */
			if (ide->type == IDE_NONE)
			{
				temp = 0xFF;
			}
			else
			{
				if (ide_drive_is_zip(ide))
				{
					temp = zip[atapi_zip_drives[cur_ide[ide_board]]].request_length & 0xff;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					temp = cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length & 0xff;
				}
				else
				{
					temp = ide->cylinder & 0xff;
				}
			}
			break;

		case 0x1F5: /* Cylinder high */
			if (ide->type == IDE_NONE)
			{
				temp = 0xFF;
			}
			else
			{
				if (ide_drive_is_zip(ide))
				{
					temp = zip[atapi_zip_drives[cur_ide[ide_board]]].request_length >> 8;
				}
				else if (ide_drive_is_cdrom(ide))
				{
					temp = cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length >> 8;
				}
				else
				{
					temp = ide->cylinder >> 8;
				}
			}
			break;

		case 0x1F6: /* Drive/Head */
			temp = (uint8_t)(ide->head | ((cur_ide[ide_board] & 1) ? 0x10 : 0) | (ide->lba ? 0x40 : 0) | 0xa0);
			break;

		/* For ATAPI: Bit 5 is DMA ready, but without overlapped or interlaved DMA, it is
					  DF (drive fault). */
		case 0x1F7: /* Status */
			ide_irq_lower(ide);
			if (ide->type == IDE_NONE)
			{
				temp = 0;
			}
			else
			{
				if (ide_drive_is_zip(ide))
				{
					temp = (zip[atapi_zip_drives[cur_ide[ide_board]]].status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
				}
				else if (ide_drive_is_cdrom(ide))
				{
					temp = (cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
				}
				else
				{
					temp = ide->atastat;
				}
			}
			break;

		case 0x3F6: /* Alternate Status */
			if (ide->type == IDE_NONE)
			{
				temp = 0;
			}
			else
			{
				if (ide_drive_is_zip(ide))
				{
					temp = (zip[atapi_zip_drives[cur_ide[ide_board]]].status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
				}
				else if (ide_drive_is_cdrom(ide))
				{
					temp = (cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
				}
				else
				{
					temp = ide->atastat;
				}
			}
			break;

		default:
			temp = 0xff;
			break;
	}
	/* if (ide_board) */  ide_log("Read IDEb %04X %02X   %02X %02X %i %04X:%04X %i\n", addr, temp, ide->atastat,(ide->atastat & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0),cur_ide[ide_board],CS,cpu_state.pc,ide_board);
	return temp;
}

uint8_t cdb[16];

int old_len = 0;

int total_read = 0;

int block_total = 0;
int all_blocks_total = 0;

uint16_t readidew(int ide_board)
{
	return ide_read_data(ide_board, 2);
}

uint32_t readidel(int ide_board)
{
	uint16_t temp;
	temp = readidew(ide_board);
	return temp | (readidew(ide_board) << 16);
}

int times30=0;
void callbackide(int ide_board)
{
	IDE *ide, *ide_other;
	int64_t snum;
	int cdrom_id;
	int cdrom_id_other;
	int zip_id;
	int zip_id_other;
	uint64_t full_size = 0;

	ide = &ide_drives[cur_ide[ide_board]];
	ide_other = &ide_drives[cur_ide[ide_board] ^ 1];
	if (ide->type == IDE_HDD)
	{
		full_size = (hdd[ide->hdd_num].tracks * hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);
	}
	ext_ide = ide;

	if (ide->command==0x30) times30++;
	/*if (ide_board) */ide_log("CALLBACK %02X %i %i  %i\n",ide->command,times30,ide->reset,cur_ide[ide_board]);

	if (ide->reset)
	{
		ide->atastat = ide_other->atastat = READY_STAT | DSC_STAT;
		ide->error = ide_other->error = 1;
		ide->secount = ide_other->secount = 1;
		ide->sector = ide_other->sector = 1;
		ide->head = ide_other->head = 0;
		ide->cylinder = ide_other->cylinder = 0;
		ide->reset = ide_other->reset = 0;

		if (ide_drive_is_zip(ide))
		{
			zip_id = atapi_zip_drives[cur_ide[ide_board]];
			zip[zip_id].status = READY_STAT | DSC_STAT;
			zip[zip_id].error = 1;
			zip[zip_id].phase = 1;
			zip[zip_id].request_length=0xEB14;
			ide->cylinder = 0xEB14;
		}
		else if (ide_drive_is_cdrom(ide))
		{
			cdrom_id = atapi_cdrom_drives[cur_ide[ide_board]];
			cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
			cdrom[cdrom_id].error = 1;
			cdrom[cdrom_id].phase = 1;
			cdrom[cdrom_id].request_length=0xEB14;
			ide->cylinder = 0xEB14;
			if (cdrom_drives[cdrom_id].handler->stop)
			{
				cdrom_drives[cdrom_id].handler->stop(cdrom_id);
			}
		}
		if (ide->type == IDE_NONE)
		{
			ide->cylinder=0xFFFF;
		}
		if (ide_drive_is_zip(ide_other))
		{
			zip_id_other = atapi_zip_drives[cur_ide[ide_board] ^ 1];
			zip[zip_id_other].status = READY_STAT | DSC_STAT;
			zip[zip_id_other].error = 1;
			zip[zip_id_other].phase = 1;
			zip[zip_id_other].request_length=0xEB14;
			ide->cylinder = 0xEB14;
		}
		else if (ide_drive_is_cdrom(ide_other))
		{
			cdrom_id_other = atapi_cdrom_drives[cur_ide[ide_board] ^ 1];
			cdrom[cdrom_id_other].status = READY_STAT | DSC_STAT;
			cdrom[cdrom_id_other].error = 1;
			cdrom[cdrom_id_other].phase = 1;
			cdrom[cdrom_id_other].request_length=0xEB14;
			ide_other->cylinder = 0xEB14;
			if (cdrom_drives[cdrom_id_other].handler->stop)
			{
				cdrom_drives[cdrom_id_other].handler->stop(cdrom_id_other);
			}
		}
		if (ide_other->type == IDE_NONE)
		{
			ide_other->cylinder=0xFFFF;
		}
		return;
	}

	cdrom_id = atapi_cdrom_drives[cur_ide[ide_board]];
	cdrom_id_other = atapi_cdrom_drives[cur_ide[ide_board] ^ 1];

	zip_id = atapi_zip_drives[cur_ide[ide_board]];
	zip_id_other = atapi_zip_drives[cur_ide[ide_board] ^ 1];

	if (((ide->command >= WIN_RESTORE) && (ide->command <= 0x1F)) || ((ide->command >= WIN_SEEK) && (ide->command <= 0x7F)))
	{
		if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
		{
			goto abort_cmd;
		}
		if ((ide->command >= WIN_SEEK) && (ide->command <= 0x7F))
		{
			full_size /= ide->t_hpc;
			full_size /= ide->t_spt;

			if ((ide->cylinder >= full_size) || (ide->head >= ide->t_hpc) || !ide->sector || (ide->sector > ide->t_spt))
				goto id_not_found;
		}
		ide->atastat = READY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;
	}
	switch (ide->command)
	{
		/* Initialize the Task File Registers as follows: Status = 00h, Error = 01h, Sector Count = 01h, Sector Number = 01h,
		   Cylinder Low = 14h, Cylinder High =EBh and Drive/Head = 00h. */
	        case WIN_SRST: /*ATAPI Device Reset */
			ide->atastat = READY_STAT | DSC_STAT;
			ide->error=1; /*Device passed*/
			ide->secount = ide->sector = 1;
			ide_set_signature(ide);

			if (ide_drive_is_zip(ide))
			{
				zip[zip_id].status = READY_STAT | DSC_STAT;
				zip[zip_id].error = 1;
				zip[zip_id].phase = 1;
				zip_reset(zip_id);
			}
			else if (ide_drive_is_cdrom(ide))
			{
				cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
				cdrom[cdrom_id].error = 1;
				cdrom[cdrom_id].phase = 1;
				cdrom_reset(cdrom_id);
			}
			ide_irq_raise(ide);
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				ide->service = 0;
			}
			return;

		case WIN_NOP:
		case WIN_STANDBYNOW1:
		case WIN_IDLENOW1:
		case WIN_SETIDLE1:
			if (ide_drive_is_zip(ide))
			{
				zip[zip_id].status = READY_STAT | DSC_STAT;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
			}
			else
			{
				ide->atastat = READY_STAT | DSC_STAT;
			}
			ide_irq_raise(ide);
			return;

		case WIN_CHECKPOWERMODE1:
		case WIN_SLEEP1:
			if (ide_drive_is_zip(ide))
			{
				zip[zip_id].phase = 0xFF;
				zip[zip_id].status = READY_STAT | DSC_STAT;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				cdrom[cdrom_id].phase = 0xFF;
				cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
			}
			ide->secount = 0xFF;
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;

		case WIN_READ:
		case WIN_READ_NORETRY:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				ide_set_signature(ide);
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}

			if (ide->do_initial_read)
			{
				ide->do_initial_read = 0;
				ide->sector_pos = 0;
				if (ide->secount)
				{
					hdd_image_read(ide->hdd_num, ide_get_sector(ide), ide->secount, ide->sector_buffer);
				}
				else
				{
					hdd_image_read(ide->hdd_num, ide_get_sector(ide), 256, ide->sector_buffer);
				}
			}

			memcpy(ide->buffer, &ide->sector_buffer[ide->sector_pos*512], 512);

			ide->sector_pos++;
			ide->pos=0;

			ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;

			ide_irq_raise(ide);

			ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
			return;

		case WIN_READ_DMA:
		case WIN_READ_DMA_ALT:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide) || (ide->board >= 2))
			{
				ide_log("IDE %i: DMA read aborted (bad device or board)\n", ide->channel);
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				ide_log("IDE %i: DMA read aborted (SPECIFY failed)\n", ide->channel);
				goto id_not_found;
			}

			ide->sector_pos = 0;
			if (ide->secount)
			{
				ide->sector_pos = ide->secount;
			}
			else
			{
				ide->sector_pos = 256;
			}
			hdd_image_read(ide->hdd_num, ide_get_sector(ide), ide->sector_pos, ide->sector_buffer);

			ide->pos=0;
                
			if (ide_bus_master_read)
			{
				if (ide_bus_master_read(ide_board, ide->sector_buffer, ide->sector_pos * 512))
				{
					ide_log("IDE %i: DMA read aborted (failed)\n", ide->channel);
					goto abort_cmd;
				}
				else
				{
					/*DMA successful*/
					ide_log("IDE %i: DMA read successful\n", ide->channel);

					ide->atastat = READY_STAT | DSC_STAT;

					ide_irq_raise(ide);
					ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
				}
			} else {
				ide_log("IDE %i: DMA read aborted (no bus master)\n", ide->channel);
				goto abort_cmd;
			}

			return;

		case WIN_READ_MULTIPLE:
			/* According to the official ATA reference:

			   If the Read Multiple command is attempted before the Set Multiple Mode
			   command  has  been  executed  or  when  Read  Multiple  commands  are
			   disabled, the Read Multiple operation is rejected with an Aborted Com-
			   mand error. */
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide) || !ide->blocksize)
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}

			if (ide->do_initial_read)
			{
				ide->do_initial_read = 0;
				ide->sector_pos = 0;
				if (ide->secount)
				{
					hdd_image_read(ide->hdd_num, ide_get_sector(ide), ide->secount, ide->sector_buffer);
				}
				else
				{
					hdd_image_read(ide->hdd_num, ide_get_sector(ide), 256, ide->sector_buffer);
				}
			}

			memcpy(ide->buffer, &ide->sector_buffer[ide->sector_pos*512], 512);

			ide->sector_pos++;
			ide->pos=0;

			ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
			if (!ide->blockcount)
			{
				ide_irq_raise(ide);
			}                        
			ide->blockcount++;
			if (ide->blockcount >= ide->blocksize)
			{
				ide->blockcount = 0;
			}

			ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
			return;

		case WIN_WRITE:
		case WIN_WRITE_NORETRY:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			hdd_image_write(ide->hdd_num, ide_get_sector(ide), 1, (uint8_t *) ide->buffer);
			ide_irq_raise(ide);
			ide->secount = (ide->secount - 1) & 0xff;
			if (ide->secount)
			{
				ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
				ide->pos=0;
				ide_next_sector(ide);
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
			}
			else
			{
				ide->atastat = READY_STAT | DSC_STAT;
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
			}

			return;
                
		case WIN_WRITE_DMA:
		case WIN_WRITE_DMA_ALT:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide) || (ide_board >= 2))
			{
				ide_log("IDE %i: DMA write aborted (bad device type or board)\n", ide->channel);
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				ide_log("IDE %i: DMA write aborted (SPECIFY failed)\n", ide->channel);
				goto id_not_found;
			}

			if (ide_bus_master_read)
			{
				if (ide->secount)
					ide->sector_pos = ide->secount;
				else
					ide->sector_pos = 256;

				if (ide_bus_master_write(ide_board, ide->sector_buffer, ide->sector_pos * 512))
				{
					ide_log("IDE %i: DMA write aborted (failed)\n", ide->channel);
					goto abort_cmd;
				}
				else
				{
					/*DMA successful*/
					ide_log("IDE %i: DMA write successful\n", ide->channel);

					hdd_image_write(ide->hdd_num, ide_get_sector(ide), ide->sector_pos, ide->sector_buffer);

					ide->atastat = READY_STAT | DSC_STAT;

					ide_irq_raise(ide);
					ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
				}
			} else {
				ide_log("IDE %i: DMA write aborted (no bus master)\n", ide->channel);
				goto abort_cmd;
			}

			return;

		case WIN_WRITE_MULTIPLE:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			hdd_image_write(ide->hdd_num, ide_get_sector(ide), 1, (uint8_t *) ide->buffer);
			ide->blockcount++;
			if (ide->blockcount >= ide->blocksize || ide->secount == 1)
			{
				ide->blockcount = 0;
				ide_irq_raise(ide);
			}
			ide->secount = (ide->secount - 1) & 0xff;
			if (ide->secount)
			{
				ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
				ide->pos=0;
				ide_next_sector(ide);
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
			}
			else
			{
				ide->atastat = READY_STAT | DSC_STAT;
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
			}
			return;

		case WIN_VERIFY:
		case WIN_VERIFY_ONCE:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			ide->pos=0;
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
			return;

		case WIN_FORMAT:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			hdd_image_zero(ide->hdd_num, ide_get_sector(ide), ide->secount);

			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);

			/* ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1); */
			return;

		case WIN_DRIVE_DIAGNOSTICS:
			ide_set_signature(ide);
			ide->error=1; /*No error detected*/

			if (ide_drive_is_zip(ide))
			{
				zip[zip_id].status = 0;
				zip[zip_id].error = 1;
				ide_irq_raise(ide);
			}
			else if (ide_drive_is_cdrom(ide))
			{
				cdrom[cdrom_id].status = 0;
				cdrom[cdrom_id].error = 1;
				ide_irq_raise(ide);
			}
			else
			{
				ide->atastat = READY_STAT | DSC_STAT;
				ide->error = 1;
				ide_irq_raise(ide);
			}

			ide_set_signature(ide_other);
			ide_other->error=1; /*No error detected*/

			if (ide_drive_is_zip(ide_other))
			{
				zip[zip_id_other].status = 0;
				zip[zip_id_other].error = 1;
			}
			else if (ide_drive_is_cdrom(ide_other))
			{
				cdrom[cdrom_id_other].status = 0;
				cdrom[cdrom_id_other].error = 1;
			}
			else
			{
				ide_other->atastat = READY_STAT | DSC_STAT;
				ide_other->error = 1;
			}

			cur_ide[ide_board] &= ~1;
			return;

		case WIN_SPECIFY: /* Initialize Drive Parameters */
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			full_size /= (ide->head+1);
			full_size /= ide->secount;
			ide->specify_success = 1;
			hdd_image_specify(ide->hdd_num, ide->head + 1, ide->secount);
			ide->t_spt=ide->secount;
			ide->t_hpc=ide->head;
			ide->t_hpc++;
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;

		case WIN_PIDENTIFY: /* Identify Packet Device */
			if (ide_drive_is_zip(ide))
			{
				ide_atapi_zip_identify(ide);
				ide->pos = 0;
				zip[zip_id].phase = 2;
				zip[zip_id].pos = 0;
				zip[zip_id].error = 0;
				zip[zip_id].status = DRQ_STAT | READY_STAT | DSC_STAT;
				ide_irq_raise(ide);
				return;
			}
			else if (ide_drive_is_cdrom(ide))
			{
				ide_atapi_identify(ide);
				ide->pos = 0;
				cdrom[cdrom_id].phase = 2;
				cdrom[cdrom_id].pos = 0;
				cdrom[cdrom_id].error = 0;
				cdrom[cdrom_id].status = DRQ_STAT | READY_STAT | DSC_STAT;
				ide_irq_raise(ide);
				return;
			}
			goto abort_cmd;

		case WIN_SET_MULTIPLE_MODE:
			if (ide_drive_is_zip(ide) || ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			ide->blocksize = ide->secount;
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;

		case WIN_SET_FEATURES:
			if (ide->type == IDE_NONE)
			{
				goto abort_cmd;
			}

			if (!ide_set_features(ide))
			{
				goto abort_cmd;
			}
			else
			{
				if (ide_drive_is_zip(ide)) {
					zip[zip_id].status = READY_STAT | DSC_STAT;
					zip[zip_id].pos = 0;
				}
				else if (ide_drive_is_cdrom(ide)) {
					cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
					cdrom[cdrom_id].pos = 0;
				}
				ide->atastat = READY_STAT | DSC_STAT;
				ide_irq_raise(ide);
			}
			return;

		case WIN_READ_NATIVE_MAX:
			if (ide->type != IDE_HDD)
			{
				goto abort_cmd;
			}
			snum = hdd[ide->hdd_num].spt;
			snum *= hdd[ide->hdd_num].hpc;
			snum *= hdd[ide->hdd_num].tracks;
			ide_set_sector(ide, snum - 1);
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;

		case WIN_IDENTIFY: /* Identify Device */
			if (ide->type != IDE_HDD)
			{
				ide_set_signature(ide);
				goto abort_cmd;
			}
			else
			{
				ide_identify(ide);
				ide->pos=0;
				ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
				ide_irq_raise(ide);
			}
			return;

		case WIN_PACKETCMD: /* ATAPI Packet */
			if (!ide_drive_is_zip(ide) && !ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}

			if (ide_drive_is_zip(ide))
				zip_phase_callback(atapi_zip_drives[cur_ide[ide_board]]);
			else
				cdrom_phase_callback(atapi_cdrom_drives[cur_ide[ide_board]]);
			ide_log("IDE callback now: %i\n", idecallback[ide_board]);
			return;

		case 0xFF:
			goto abort_cmd;
	}

abort_cmd:
	ide->command = 0;
	if (ide_drive_is_zip(ide))
	{
		zip[zip_id].status = READY_STAT | ERR_STAT | DSC_STAT;
		zip[zip_id].error = ABRT_ERR;
		zip[zip_id].pos = 0;
	}
	else if (ide_drive_is_cdrom(ide))
	{
		cdrom[cdrom_id].status = READY_STAT | ERR_STAT | DSC_STAT;
		cdrom[cdrom_id].error = ABRT_ERR;
		cdrom[cdrom_id].pos = 0;
	}
	else
	{
		ide->atastat = READY_STAT | ERR_STAT | DSC_STAT;
		ide->error = ABRT_ERR;
		ide->pos = 0;
	}
	ide_irq_raise(ide);
	return;

id_not_found:
	ide->atastat = READY_STAT | ERR_STAT | DSC_STAT;
	ide->error = ABRT_ERR | 0x10;
	ide->pos = 0;
	ide_irq_raise(ide);
}

void ide_callback_pri()
{
	idecallback[0] = 0LL;
	callbackide(0);
}

void ide_callback_sec()
{
	idecallback[1] = 0LL;
	callbackide(1);
}

void ide_callback_ter()
{
	idecallback[2] = 0LL;
	callbackide(2);
}

void ide_callback_qua()
{
	idecallback[3] = 0LL;
	callbackide(3);
}

void ide_callback_xtide()
{
	idecallback[4] = 0LL;
	callbackide(4);
}

void ide_write_pri(uint16_t addr, uint8_t val, void *priv)
{
	writeide(0, addr, val);
}
void ide_write_pri_w(uint16_t addr, uint16_t val, void *priv)
{
	writeidew(0, val);
}
void ide_write_pri_l(uint16_t addr, uint32_t val, void *priv)
{
	writeidel(0, val);
}
uint8_t ide_read_pri(uint16_t addr, void *priv)
{
	return readide(0, addr);
}
uint16_t ide_read_pri_w(uint16_t addr, void *priv)
{
	return readidew(0);
}
uint32_t ide_read_pri_l(uint16_t addr, void *priv)
{
	return readidel(0);
}

void ide_write_sec(uint16_t addr, uint8_t val, void *priv)
{
	writeide(1, addr, val);
}
void ide_write_sec_w(uint16_t addr, uint16_t val, void *priv)
{
	writeidew(1, val);
}
void ide_write_sec_l(uint16_t addr, uint32_t val, void *priv)
{
	writeidel(1, val);
}
uint8_t ide_read_sec(uint16_t addr, void *priv)
{
	return readide(1, addr);
}
uint16_t ide_read_sec_w(uint16_t addr, void *priv)
{
	return readidew(1);
}
uint32_t ide_read_sec_l(uint16_t addr, void *priv)
{
	return readidel(1);
}

void ide_write_ter(uint16_t addr, uint8_t val, void *priv)
{
	writeide(2, addr, val);
}
void ide_write_ter_w(uint16_t addr, uint16_t val, void *priv)
{
	writeidew(2, val);
}
void ide_write_ter_l(uint16_t addr, uint32_t val, void *priv)
{
	writeidel(2, val);
}
uint8_t ide_read_ter(uint16_t addr, void *priv)
{
	return readide(2, addr);
}
uint16_t ide_read_ter_w(uint16_t addr, void *priv)
{
	return readidew(2);
}
uint32_t ide_read_ter_l(uint16_t addr, void *priv)
{
	return readidel(2);
}

void ide_write_qua(uint16_t addr, uint8_t val, void *priv)
{
	writeide(3, addr, val);
}
void ide_write_qua_w(uint16_t addr, uint16_t val, void *priv)
{
	writeidew(3, val);
}
void ide_write_qua_l(uint16_t addr, uint32_t val, void *priv)
{
	writeidel(3, val);
}
uint8_t ide_read_qua(uint16_t addr, void *priv)
{
	return readide(3, addr);
}
uint16_t ide_read_qua_w(uint16_t addr, void *priv)
{
	return readidew(3);
}
uint32_t ide_read_qua_l(uint16_t addr, void *priv)
{
	return readidel(3);
}

static uint16_t ide_base_main[2] = { 0x1f0, 0x170 };
static uint16_t ide_side_main[2] = { 0x3f6, 0x376 };


void ide_pri_enable(void)
{
	io_sethandler(0x01f0, 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
	io_sethandler(0x03f6, 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
	ide_base_main[0] = 0x1f0;
	ide_side_main[0] = 0x3f6;
}

void ide_pri_enable_ex(void)
{
	if (ide_base_main[0] & 0x300)
	{
		ide_log("Enabling primary base (%04X)...\n", ide_base_main[0]);
		io_sethandler(ide_base_main[0], 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
	}
	if (ide_side_main[0] & 0x300)
	{
		ide_log("Enabling primary side (%04X)...\n", ide_side_main[0]);
		io_sethandler(ide_side_main[0], 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
	}
}

void ide_pri_disable(void)
{
	io_removehandler(ide_base_main[0], 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
	io_removehandler(ide_side_main[0], 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
}

void ide_sec_enable(void)
{
	io_sethandler(0x0170, 0x0008, ide_read_sec, ide_read_sec_w, ide_read_sec_l, ide_write_sec, ide_write_sec_w, ide_write_sec_l, NULL);
	io_sethandler(0x0376, 0x0001, ide_read_sec, NULL,           NULL,           ide_write_sec, NULL,            NULL           , NULL);
	ide_base_main[1] = 0x170;
	ide_side_main[1] = 0x376;
}

void ide_sec_enable_ex(void)
{
	if (ide_base_main[1] & 0x300)
	{
		io_sethandler(ide_base_main[1], 0x0008, ide_read_sec, ide_read_sec_w, ide_read_sec_l, ide_write_sec, ide_write_sec_w, ide_write_sec_l, NULL);
	}
	if (ide_side_main[1] & 0x300)
	{
		io_sethandler(ide_side_main[1], 0x0001, ide_read_sec, NULL,           NULL,           ide_write_sec, NULL,            NULL           , NULL);
	}
}

void ide_sec_disable(void)
{
	io_removehandler(ide_base_main[1], 0x0008, ide_read_sec, ide_read_sec_w, ide_read_sec_l, ide_write_sec, ide_write_sec_w, ide_write_sec_l, NULL);
	io_removehandler(ide_side_main[1], 0x0001, ide_read_sec, NULL,           NULL,           ide_write_sec, NULL,            NULL           , NULL);
}


void ide_set_base(int controller, uint16_t port)
{
	ide_base_main[controller] = port;
}

void ide_set_side(int controller, uint16_t port)
{
	ide_side_main[controller] = port;
}

void ide_ter_enable(void)
{
	io_sethandler(0x0168, 0x0008, ide_read_ter, ide_read_ter_w, ide_read_ter_l, ide_write_ter, ide_write_ter_w, ide_write_ter_l, NULL);
	io_sethandler(0x036e, 0x0001, ide_read_ter, NULL,           NULL,           ide_write_ter, NULL,            NULL           , NULL);
}

void ide_ter_disable(void)
{
	io_removehandler(0x0168, 0x0008, ide_read_ter, ide_read_ter_w, ide_read_ter_l, ide_write_ter, ide_write_ter_w, ide_write_ter_l, NULL);
	io_removehandler(0x036e, 0x0001, ide_read_ter, NULL,           NULL,           ide_write_ter, NULL,            NULL           , NULL);
}

void ide_ter_disable_cond(void)
{
	if ((ide_drives[4].type == IDE_NONE) && (ide_drives[5].type == IDE_NONE))
	{
		ide_ter_disable();
	}
}

void ide_ter_init(void)
{
	ide_ter_enable();

	timer_add(ide_callback_ter, &idecallback[2], &idecallback[2],  NULL);
}

void ide_qua_enable(void)
{
	io_sethandler(0x01e8, 0x0008, ide_read_qua, ide_read_qua_w, ide_read_qua_l, ide_write_qua, ide_write_qua_w, ide_write_qua_l, NULL);
	io_sethandler(0x03ee, 0x0001, ide_read_qua, NULL,           NULL,           ide_write_qua, NULL,            NULL           , NULL);
}

void ide_qua_disable_cond(void)
{
	if ((ide_drives[6].type == IDE_NONE) && (ide_drives[7].type == IDE_NONE))
	{
		ide_qua_disable();
	}
}

void ide_qua_disable(void)
{
	io_removehandler(0x01e8, 0x0008, ide_read_qua, ide_read_qua_w, ide_read_qua_l, ide_write_qua, ide_write_qua_w, ide_write_qua_l, NULL);
	io_removehandler(0x03ee, 0x0001, ide_read_qua, NULL,           NULL,           ide_write_qua, NULL,            NULL           , NULL);
}

void ide_qua_init(void)
{
	ide_qua_enable();

	timer_add(ide_callback_qua, &idecallback[3], &idecallback[3],  NULL);
}


/*FIXME: this will go away after Kotori's rewrite. --FvK */
void ide_init_first(void)
{
	int d;

	memset(ide_drives, 0x00, sizeof(ide_drives));
	for (d = 0; d < (IDE_NUM+XTIDE_NUM); d++)
	{
		ide_drives[d].channel = d;
		ide_drives[d].type = IDE_NONE;
		ide_drives[d].hdd_num = -1;
		ide_drives[d].atastat = READY_STAT | DSC_STAT;
		ide_drives[d].service = 0;
		ide_drives[d].board = d >> 1;
	}
}


void ide_xtide_init(void)
{
	ide_bus_master_read = ide_bus_master_write = NULL;

	timer_add(ide_callback_xtide, &idecallback[4], &idecallback[4],  NULL);
}

void ide_set_bus_master(int (*read)(int channel, uint8_t *data, int transfer_length), int (*write)(int channel, uint8_t *data, int transfer_length), void (*set_irq)(int channel))
{
	ide_bus_master_read = read;
	ide_bus_master_write = write;
	ide_bus_master_set_irq = set_irq;
}

void secondary_ide_check(void)
{
	int i = 0;
	int secondary_cdroms = 0;
	int secondary_zips = 0;

	for (i=0; i<ZIP_NUM; i++) {
		if ((zip_drives[i].ide_channel >= 2) && (zip_drives[i].ide_channel <= 3) && ((zip_drives[i].bus_type == ZIP_BUS_ATAPI_PIO_ONLY) || (zip_drives[i].bus_type == ZIP_BUS_ATAPI_PIO_AND_DMA)))
			secondary_zips++;
	}
	for (i=0; i<CDROM_NUM; i++) {
		if ((cdrom_drives[i].ide_channel >= 2) && (cdrom_drives[i].ide_channel <= 3) && ((cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_ONLY) || (cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA)))
			secondary_cdroms++;
	}
	if (!secondary_zips && !secondary_cdroms) {
		ide_sec_disable();
		ide_init_ch[1] = 0;
	}
}


/*
 * Initialization of standalone IDE controller instance.
 *
 * Eventually, we should clean up the whole mess by only
 * using device_t units, with configuration parameters to
 * indicate primary/secondary and all that, rather than
 * keeping a zillion of duplicate functions around.
 */
static void *
ide_sainit(device_t *info)
{
    switch(info->local) {
	case 0:		/* ISA, single-channel */
	case 2:		/* ISA, dual-channel */
	case 3:		/* ISA, dual-channel, optional 2nd channel */
	case 4:		/* VLB, single-channel */
	case 6:		/* VLB, dual-channel */
	case 8:		/* PCI, single-channel */
	case 10:	/* PCI, dual-channel */
		if (!ide_init_ch[0]) {
			ide_pri_enable();
			timer_add(ide_callback_pri, &idecallback[0], &idecallback[0],  NULL);
			ide_init_ch[0] = 1;
		}

		if ((info->local & 2) && !ide_init_ch[1]) {
			ide_sec_enable();
			timer_add(ide_callback_sec, &idecallback[1], &idecallback[1],  NULL);
			ide_init_ch[1] = 1;

			if (info->local & 1)
				secondary_ide_check();
		}

		if (!(info->local & 8))
			ide_bus_master_read = ide_bus_master_write = NULL;
		break;
    }

    return(info);
}


/* Close a standalone IDE unit. */
static void
ide_saclose(void *priv)
{
    ide_init_ch[0] = ide_init_ch[1] = 0;
}


device_t ide_isa_device = {
    "ISA PC/AT IDE Controller",
    DEVICE_ISA | DEVICE_AT,
    0,
    ide_sainit, ide_saclose, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};

device_t ide_isa_2ch_device = {
    "ISA PC/AT IDE Controller (Dual-Channel)",
    DEVICE_ISA | DEVICE_AT,
    2,
    ide_sainit, ide_saclose, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};

device_t ide_isa_2ch_opt_device = {
    "ISA PC/AT IDE Controller (Single/Dual)",
    DEVICE_ISA | DEVICE_AT,
    3,
    ide_sainit, ide_saclose, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};

device_t ide_vlb_device = {
    "VLB IDE Controller",
    DEVICE_VLB | DEVICE_AT,
    4,
    ide_sainit, ide_saclose, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};

device_t ide_vlb_2ch_device = {
    "VLB IDE Controller (Dual-Channel)",
    DEVICE_VLB | DEVICE_AT,
    6,
    ide_sainit, ide_saclose, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};

device_t ide_pci_device = {
    "PCI IDE Controller",
    DEVICE_PCI | DEVICE_AT,
    8,
    ide_sainit, ide_saclose, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};

device_t ide_pci_2ch_device = {
    "PCI IDE Controller (Dual-Channel)",
    DEVICE_PCI | DEVICE_AT,
    10,
    ide_sainit, ide_saclose, NULL,
    NULL, NULL, NULL, NULL,
    NULL
};
