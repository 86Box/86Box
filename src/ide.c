/* Copyright holders: Sarah Walker, Tenshi, SA1988
   see COPYING for more details
*/
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "86box.h"
#include "cdrom.h"
#include "ibm.h"
#include "io.h"
#include "pic.h"
#include "timer.h"
#include "cdrom.h"
#include "scsi.h"
#include "ide.h"

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
#define WIN_WRITE_DMA                   0xCA
#define WIN_STANDBYNOW1			0xE0
#define WIN_IDLENOW1			0xE1
#define WIN_SETIDLE1			0xE3
#define WIN_CHECKPOWERMODE1		0xE5
#define WIN_SLEEP1			0xE6
#define WIN_IDENTIFY			0xEC /* Ask drive to identify itself */
#define WIN_SET_FEATURES		0xEF
#define WIN_READ_NATIVE_MAX		0xF8

enum
{
        IDE_NONE = 0,
        IDE_HDD,
        IDE_CDROM
};

uint64_t hdt[128][3] = {	{  306,  4, 17 }, {  615,  2, 17 }, {  306,  4, 26 }, { 1024,  2, 17 }, {  697,  3, 17 }, {  306,  8, 17 }, {  614,  4, 17 }, {  615,  4, 17 },		/* 000-007 */
			{  670,  4, 17 }, {  697,  4, 17 }, {  987,  3, 17 }, {  820,  4, 17 }, {  670,  5, 17 }, {  697,  5, 17 }, {  733,  5, 17 }, {  615,  6, 17 },		/* 008-015 */
			{  462,  8, 17 }, {  306,  8, 26 }, {  615,  4, 26 }, { 1024,  4, 17 }, {  855,  5, 17 }, {  925,  5, 17 }, {  932,  5, 17 }, { 1024,  2, 40 },		/* 016-023 */
			{  809,  6, 17 }, {  976,  5, 17 }, {  977,  5, 17 }, {  698,  7, 17 }, {  699,  7, 17 }, {  981,  5, 17 }, {  615,  8, 17 }, {  989,  5, 17 },		/* 024-031 */
			{  820,  4, 26 }, { 1024,  5, 17 }, {  733,  7, 17 }, {  754,  7, 17 }, {  733,  5, 26 }, {  940,  6, 17 }, {  615,  6, 26 }, {  462,  8, 26 },		/* 032-039 */
			{  830,  7, 17 }, {  855,  7, 17 }, {  751,  8, 17 }, { 1024,  4, 26 }, {  918,  7, 17 }, {  925,  7, 17 }, {  855,  5, 26 }, {  977,  7, 17 },		/* 040-047 */
			{  987,  7, 17 }, { 1024,  7, 17 }, {  823,  4, 38 }, {  925,  8, 17 }, {  809,  6, 26 }, {  976,  5, 26 }, {  977,  5, 26 }, {  698,  7, 26 },		/* 048-055 */
			{  699,  7, 26 }, {  940,  8, 17 }, {  615,  8, 26 }, { 1024,  5, 26 }, {  733,  7, 26 }, { 1024,  8, 17 }, {  823, 10, 17 }, {  754, 11, 17 },		/* 056-063 */
			{  830, 10, 17 }, {  925,  9, 17 }, { 1224,  7, 17 }, {  940,  6, 26 }, {  855,  7, 26 }, {  751,  8, 26 }, { 1024,  9, 17 }, {  965, 10, 17 },		/* 064-071 */
			{  969,  5, 34 }, {  980, 10, 17 }, {  960,  5, 35 }, {  918, 11, 17 }, { 1024, 10, 17 }, {  977,  7, 26 }, { 1024,  7, 26 }, { 1024, 11, 17 },		/* 072-079 */
			{  940,  8, 26 }, {  776,  8, 33 }, {  755, 16, 17 }, { 1024, 12, 17 }, { 1024,  8, 26 }, {  823, 10, 26 }, {  830, 10, 26 }, {  925,  9, 26 },		/* 080-087 */
			{  960,  9, 26 }, { 1024, 13, 17 }, { 1224, 11, 17 }, {  900, 15, 17 }, {  969,  7, 34 }, {  917, 15, 17 }, {  918, 15, 17 }, { 1524,  4, 39 },		/* 088-095 */
			{ 1024,  9, 26 }, { 1024, 14, 17 }, {  965, 10, 26 }, {  980, 10, 26 }, { 1020, 15, 17 }, { 1023, 15, 17 }, { 1024, 15, 17 }, { 1024, 16, 17 },		/* 096-103 */
			{ 1224, 15, 17 }, {  755, 16, 26 }, {  903,  8, 46 }, {  984, 10, 34 }, {  900, 15, 26 }, {  917, 15, 26 }, { 1023, 15, 26 }, {  684, 16, 38 },		/* 104-111 */
			{ 1930,  4, 62 }, {  967, 16, 31 }, { 1013, 10, 63 }, { 1218, 15, 36 }, {  654, 16, 63 }, {  659, 16, 63 }, {  702, 16, 63 }, { 1002, 13, 63 },		/* 112-119 */
			{  854, 16, 63 }, {  987, 16, 63 }, {  995, 16, 63 }, { 1024, 16, 63 }, { 1036, 16, 63 }, { 1120, 16, 59 }, { 1054, 16, 63 }, {    0,  0,  0 }	};	/* 119-127 */
			
IDE ide_drives[IDE_NUM];

IDE *ext_ide;

char hdd_fn[HDC_NUM][512];

int (*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length);
int (*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length);
void (*ide_bus_master_set_irq)(int channel);

int idecallback[4] = {0, 0, 0, 0};

int cur_ide[4];

int ide_do_log = 0;

void ide_log(const char *format, ...)
{
#ifdef ENABLE_IDE_LOG
   if (ide_do_log)
   {
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
   }
#endif
}

uint8_t getstat(IDE *ide) { return ide->atastat; }

int ide_drive_is_cdrom(IDE *ide)
{
	if (atapi_cdrom_drives[ide->channel] >= CDROM_NUM)
	{
		return 0;
	}
	else
	{
		if (cdrom_drives[atapi_cdrom_drives[ide->channel]].enabled && !cdrom_drives[atapi_cdrom_drives[ide->channel]].bus_type && (cdrom_drives[atapi_cdrom_drives[ide->channel]].bus_mode & 1))
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
}

int image_is_hdi(const char *s)
{
	int i, len;
	char ext[5] = { 0, 0, 0, 0, 0 };
	len = strlen(s);
	if ((len < 4) || (s[0] == '.'))
	{
		return 0;
	}
	memcpy(ext, s + len - 4, 4);
	for (i = 0; i < 4; i++)
	{
		ext[i] = toupper(ext[i]);
	}
	if (strcmp(ext, ".HDI") == 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int image_is_hdx(const char *s, int check_signature)
{
	int i, len;
	FILE *f;
	uint64_t filelen;
	uint64_t signature;
	char ext[5] = { 0, 0, 0, 0, 0 };
	len = strlen(s);
	if ((len < 4) || (s[0] == '.'))
	{
		return 0;
	}
	memcpy(ext, s + len - 4, 4);
	for (i = 0; i < 4; i++)
	{
		ext[i] = toupper(ext[i]);
	}
	if (strcmp(ext, ".HDX") == 0)
	{
		if (check_signature)
		{
			f = fopen(s, "rb");
			if (!f)
			{
				return 0;
			}
			fseeko64(f, 0, SEEK_END);
			filelen = ftello64(f);
			fseeko64(f, 0, SEEK_SET);
			if (filelen < 44)
			{
				return 0;
			}
			fread(&signature, 1, 8, f);
			fclose(f);
			if (signature == 0xD778A82044445459ll)
			{
				return 1;
			}
			else
			{
				return 0;
			}
		}
		else
		{
			return 1;
		}
	}
	else
	{
		return 0;
	}
}

int ide_enable[4] = { 1, 1, 0, 0 };
int ide_irq[4] = { 14, 15, 10, 11 };

void ide_irq_raise(IDE *ide)
{
	if ((ide->board > 3) || ide->irqstat)
	{
		return;
	}

	ide_log("Raising IRQ %i (board %i)\n", ide_irq[ide->board], ide->board);
	
	if (!(ide->fdisk&2))
	{
		picint(1 << ide_irq[ide->board]);

		if (ide->board < 2)
		{
			if (ide_bus_master_set_irq)
			{
				ide_bus_master_set_irq(ide->board);
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
		return;
	}

	ide_log("Lowering IRQ %i (board %i)\n", ide_irq[ide->board], ide->board);

	picintc(1 << ide_irq[ide->board]);
	ide->irqstat=0;
}

void ide_irq_update(IDE *ide)
{
	int pending = 0;
	int mask = 0;
	
	if (ide->board > 3)
	{
		return;
	}

	mask = ide_irq[ide->board];
	mask &= 7;

	pending = (pic2.pend | pic2.ins);
	pending &= (1 << mask);

	if (ide->irqstat && !pending && !(ide->fdisk & 2))
	{
		picint(1 << ide_irq[ide->board]);
	}
	else if (pending)
	{
		picintc(1 << ide_irq[ide->board]);
	}
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
	uint32_t c, h, s;
	char device_identify[8] = { '8', '6', 'B', '_', 'H', 'D', '0', 0 };
	uint64_t full_size = (hdc[cur_ide[ide->board]].tracks * hdc[cur_ide[ide->board]].hpc * hdc[cur_ide[ide->board]].spt);
	
	device_identify[6] = ide->channel + 0x30;
	ide_log("IDE Identify: %s\n", device_identify);

	memset(ide->buffer, 0, 512);
	
	c = hdc[cur_ide[ide->board]].tracks; /* Cylinders */
	h = hdc[cur_ide[ide->board]].hpc;  /* Heads */
	s = hdc[cur_ide[ide->board]].spt;  /* Sectors */

	if (hdc[cur_ide[ide->board]].tracks <= 16383)
	{
		ide->buffer[1] = hdc[cur_ide[ide->board]].tracks; /* Cylinders */
	}
	else
	{
		ide->buffer[1] = 16383; /* Cylinders */
	}
	ide->buffer[3] = hdc[cur_ide[ide->board]].hpc;  /* Heads */
	ide->buffer[6] = hdc[cur_ide[ide->board]].spt;  /* Sectors */
	ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
	ide_padstr((char *) (ide->buffer + 23), emulator_version, 8); /* Firmware */
	ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */
	ide->buffer[20] = 3;   /*Buffer type*/
	ide->buffer[21] = 512; /*Buffer size*/
	ide->buffer[47] = 16;  /*Max sectors on multiple transfer command*/
	ide->buffer[48] = 1;   /*Dword transfers supported*/
	if (PCI && (ide->board < 2))
	{
		ide->buffer[49] = (1 << 8); /* LBA and DMA supported */
	}
	else
	{
		ide->buffer[49] = 0;
	}
	if ((c > 1024) || (h > 16) || (s > 63))
	{
		ide->buffer[49] |= (1 << 9);
	}
	ide->buffer[50] = 0x4000; /* Capabilities */
	ide->buffer[51] = 2 << 8; /*PIO timing mode*/
	ide->buffer[52] = 2 << 8; /*DMA timing mode*/
	ide->buffer[53] = 1;
	ide->buffer[55] = ide->hpc;
	ide->buffer[56] = ide->spt;
	if (((full_size / ide->hpc) / ide->spt) <= 16383)
	{
		ide->buffer[54] = (full_size / ide->hpc) / ide->spt;
	}
	else
	{
		ide->buffer[54] = 16383;
	}
	full_size = ((uint64_t) ide->hpc) * ((uint64_t) ide->spt) * ((uint64_t) ide->buffer[54]);
	ide->buffer[57] = full_size & 0xFFFF; /* Total addressable sectors (LBA) */
	ide->buffer[58] = (full_size >> 16) & 0x0FFF;
	ide->buffer[59] = ide->blocksize ? (ide->blocksize | 0x100) : 0;
	if (ide->buffer[49] & (1 << 9))
	{
		ide->buffer[60] = (hdc[cur_ide[ide->board]].tracks * hdc[cur_ide[ide->board]].hpc * hdc[cur_ide[ide->board]].spt) & 0xFFFF; /* Total addressable sectors (LBA) */
		ide->buffer[61] = ((hdc[cur_ide[ide->board]].tracks * hdc[cur_ide[ide->board]].hpc * hdc[cur_ide[ide->board]].spt) >> 16) & 0x0FFF;
	}
	if (PCI && (ide->board < 2))
	{
		ide->buffer[63] = 7;
	}
	ide->buffer[80] = 0xe; /*ATA-1 to ATA-3 supported*/
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY PACKET DEVICE" command
 */
static void ide_atapi_identify(IDE *ide)
{
	char device_identify[8] = { '8', '6', 'B', '_', 'C', 'D', '0', 0 };
	uint8_t cdrom_id;
	
	memset(ide->buffer, 0, 512);
	cdrom_id = atapi_cdrom_drives[ide->channel];

	device_identify[6] = cdrom_id + 0x30;
	ide_log("ATAPI Identify: %s\n", device_identify);

	ide->buffer[0] = 0x8000 | (5<<8) | 0x80 | (2<<5); /* ATAPI device, CD-ROM drive, removable media, accelerated DRQ */
	ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
	ide_padstr((char *) (ide->buffer + 23), emulator_version, 8); /* Firmware */
	ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */
	ide->buffer[48] = 1;   /*Dword transfers supported*/
	ide->buffer[49] = 0x200; /* LBA supported */
	ide->buffer[73] = 6;
	ide->buffer[74] = 9;
	ide->buffer[80] = 0x10; /*ATA/ATAPI-4 supported*/

	if (PCI && (ide->board < 2) && (cdrom_drives[cdrom_id].bus_mode & 2))
	{
		ide->buffer[49] |= 0x100; /* DMA supported */
		ide->buffer[63] = 7;
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
        	uint32_t heads = ide->hpc;
        	uint32_t sectors = ide->spt;

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
        	if (ide->sector == (ide->spt + 1))
			{
        		ide->sector = 1;
        		ide->head++;
        		if (ide->head == ide->hpc)
				{
        			ide->head = 0;
        			ide->cylinder++;
				}
			}
		}
}

static void loadhd(IDE *ide, int d, const char *fn)
{
	uint32_t sector_size = 512;
	uint32_t zero = 0;
	uint64_t signature = 0xD778A82044445459ll;
	uint64_t full_size = 0;
	int c;
	ide->base = 0;
	ide->hdi = 0;
	if (ide->hdfile == NULL)
	{
		/* Try to open existing hard disk image */
		if (fn[0] == '.')
		{
			ide->type = IDE_NONE;
			return;
		}
		ide->hdfile = fopen64(fn, "rb+");
		if (ide->hdfile == NULL)
		{
			/* Failed to open existing hard disk image */
			if (errno == ENOENT)
			{
				/* Failed because it does not exist,
				   so try to create new file */
				ide->hdfile = fopen64(fn, "wb+");
				if (ide->hdfile == NULL)
				{
					ide->type = IDE_NONE;
					return;
				}
				else
				{
					if (image_is_hdi(fn))
					{
						full_size = hdc[d].spt * hdc[d].hpc * hdc[d].tracks * 512;
						ide->base = 0x1000;
						ide->hdi = 1;
						fwrite(&zero, 1, 4, ide->hdfile);
						fwrite(&zero, 1, 4, ide->hdfile);
						fwrite(&(ide->base), 1, 4, ide->hdfile);
						fwrite(&full_size, 1, 4, ide->hdfile);
						fwrite(&sector_size, 1, 4, ide->hdfile);
						fwrite(&(hdc[d].spt), 1, 4, ide->hdfile);
						fwrite(&(hdc[d].hpc), 1, 4, ide->hdfile);
						fwrite(&(hdc[d].tracks), 1, 4, ide->hdfile);
						for (c = 0; c < 0x3f8; c++)
						{
							fwrite(&zero, 1, 4, ide->hdfile);
						}
					}
					else if (image_is_hdx(fn, 0))
					{
						full_size = hdc[d].spt * hdc[d].hpc * hdc[d].tracks * 512;
						ide->base = 0x28;
						ide->hdi = 2;
						fwrite(&signature, 1, 8, ide->hdfile);
						fwrite(&full_size, 1, 8, ide->hdfile);
						fwrite(&sector_size, 1, 4, ide->hdfile);
						fwrite(&(hdc[d].spt), 1, 4, ide->hdfile);
						fwrite(&(hdc[d].hpc), 1, 4, ide->hdfile);
						fwrite(&(hdc[d].tracks), 1, 4, ide->hdfile);
						fwrite(&zero, 1, 4, ide->hdfile);
						fwrite(&zero, 1, 4, ide->hdfile);
					}
					ide->hdc_num = d;
				}
			}
			else
			{
				/* Failed for another reason */
				ide->type = IDE_NONE;
				return;
			}
		}
		else
		{
			if (image_is_hdi(fn))
			{
				fseeko64(ide->hdfile, 0x8, SEEK_SET);
				fread(&(ide->base), 1, 4, ide->hdfile);
				fseeko64(ide->hdfile, 0x10, SEEK_SET);
				fread(&sector_size, 1, 4, ide->hdfile);
				if (sector_size != 512)
				{
					/* Sector size is not 512 */
					fclose(ide->hdfile);
					ide->type = IDE_NONE;
					return;
				}
				fread(&(hdc[d].spt), 1, 4, ide->hdfile);
				fread(&(hdc[d].hpc), 1, 4, ide->hdfile);
				fread(&(hdc[d].tracks), 1, 4, ide->hdfile);
				ide->hdi = 1;
			}
			else if (image_is_hdx(fn, 1))
			{
				ide->base = 0x28;
				fseeko64(ide->hdfile, 0x10, SEEK_SET);
				fread(&sector_size, 1, 4, ide->hdfile);
				if (sector_size != 512)
				{
					/* Sector size is not 512 */
					fclose(ide->hdfile);
					ide->type = IDE_NONE;
					return;
				}
				fread(&(hdc[d].spt), 1, 4, ide->hdfile);
				fread(&(hdc[d].hpc), 1, 4, ide->hdfile);
				fread(&(hdc[d].tracks), 1, 4, ide->hdfile);
				fread(&(hdc[d].at_spt), 1, 4, ide->hdfile);
				fread(&(hdc[d].at_hpc), 1, 4, ide->hdfile);
				ide->hdi = 2;
			}
		}
	}
	
    ide->spt = hdc[d].spt;
    ide->hpc = hdc[d].hpc;
    ide->tracks = hdc[d].tracks;
    ide->type = IDE_HDD;
	ide->hdc_num = d;
}

void ide_set_signature(IDE *ide)
{
	uint8_t cdrom_id = atapi_cdrom_drives[ide->channel];
	ide->sector=1;
	ide->head=0;
	if (ide_drive_is_cdrom(ide))
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

int ide_cdrom_is_pio_only(IDE *ide)
{
	uint8_t cdrom_id = atapi_cdrom_drives[cur_ide[ide->board]];
	if (!ide_drive_is_cdrom(ide))
	{
		return 0;
	}
	if (cdrom_drives[cdrom_id].bus_mode & 2)
	{
		return 0;
	}
	return 1;
}

#if 0
int ide_set_features(IDE *ide)
{
	uint8_t cdrom_id = cur_ide[ide->board];

	uint8_t features = 0;
	
	uint8_t sf_data = 0;
	uint8_t val = 0;
	
	if (ide_drive_is_cdrom(ide))
	{
		features = cdrom[cdrom_id].features;
		sf_data = cdrom[cdrom_id].phase;
	}
	else
	{
		features = ide->cylprecomp;
		sf_data = ide->secount;
	}
	
	val = sf_data & 7;

	if (ide->type == IDE_NONE)  return 0;
	
	switch(features)
	{
		case 0x02:
		case 0x82:
			return 0;
		case 0xcc:
		case 0x66:
		case 0xaa:
		case 0x55:
		case 0x05:
		case 0x85:
		case 0x69:
		case 0x67:
		case 0x96:
		case 0x9a:
		case 0x42:
		case 0xc2:
			return 1;
		case 0x03:
#if 0
			if (ide->type == IDE_CDROM)
			{
				return 0;
			}
#endif
			switch(sf_data >> 3)
			{
				case 0:
					ide->dma_identify_data[0] = 7;
					break;
				case 1:
					/* We do not (yet) emulate flow control, so return with error if this is attempted. */
					return 0;
				case 4:
					if (ide_cdrom_is_pio_only(ide) || (ide->board >= 2))
					{
						return 0;
					}
					ide->dma_identify_data[0] = 7 | (1 << (val + 8));
					break;
				default:
					return 0;
			}
	}
	return 1;
}
#endif

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
		cyl = sector_num / (hdc[cur_ide[ide->board]].hpc * hdc[cur_ide[ide->board]].spt);
		r = sector_num % (hdc[cur_ide[ide->board]].hpc * hdc[cur_ide[ide->board]].spt);
		ide->cylinder = cyl;
		ide->head = ((r / hdc[cur_ide[ide->board]].spt) & 0x0f);
		ide->sector = (r % hdc[cur_ide[ide->board]].spt) + 1;
	}
}

void ide_ter_disable_cond();
void ide_qua_disable_cond();

void resetide(void)
{
	int c, d;
	
	build_atapi_cdrom_map();

	/* Close hard disk image files (if previously open) */
	for (d = 0; d < IDE_NUM; d++)
	{
		ide_drives[d].channel = d;
		ide_drives[d].type = IDE_NONE;
		if (ide_drives[d].hdfile != NULL)
		{
			fclose(ide_drives[d].hdfile);
			ide_drives[d].hdfile = NULL;
		}
		if (ide_drive_is_cdrom(&ide_drives[d]))
		{
			cdrom[atapi_cdrom_drives[d]].status = READY_STAT | DSC_STAT;
		}
		ide_drives[d].atastat = READY_STAT | DSC_STAT;
		ide_drives[d].service = 0;
		ide_drives[d].board = d >> 1;
	}
		
	idecallback[0]=idecallback[1]=0;
	idecallback[2]=idecallback[3]=0;

	c = 0;
	for (d = 0; d < HDC_NUM; d++)
	{
		if ((hdc[d].bus == 2) && (hdc[d].ide_channel < IDE_NUM))
		{
			pclog("Found IDE hard disk on channel %i\n", hdc[d].ide_channel);
			loadhd(&ide_drives[hdc[d].ide_channel], d, hdd_fn[d]);
			c++;
			if (c >= IDE_NUM)  break;
		}
	}

	for (d = 0; d < IDE_NUM; d++)
	{
		if (ide_drive_is_cdrom(&ide_drives[d]) && (ide_drives[d].type != IDE_HDD))
		{
			ide_drives[d].type = IDE_CDROM;
		}
			
		ide_set_signature(&ide_drives[d]);

#if 0
		if (ide_drives[d].type != IDE_NONE)
		{
			ide_drives[d].dma_identify_data[0] = 7;
		}
#endif

		ide_drives[d].error = 1;
	}

	for (d = 0; d < 4; d++)
	{
		cur_ide[d] = d << 1;
	}

	ide_ter_disable_cond();
	ide_qua_disable_cond();
}

int idetimes = 0;

void ide_write_data(int ide_board, uint32_t val, int length)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];

	uint8_t *idebufferb = (uint8_t *) ide->buffer;
	uint16_t *idebufferw = ide->buffer;
	uint32_t *idebufferl = (uint32_t *) ide->buffer;
	
#if 0
	if (ide_drive_is_cdrom(ide))
	{
		ide_log("CD-ROM write data: %04X\n", val);
	}
#endif
        
	if (ide->command == WIN_PACKETCMD)
	{
		ide->pos = 0;

		if (!ide_drive_is_cdrom(ide))
		{
			return;
		}

		cdrom_write(cur_ide[ide_board], val, length);

		if (cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].callback)
		{
			idecallback[ide_board] = cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].callback;
		}
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
				idecallback[ide_board]=6*IDE_TIME;
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

	ide_log("WriteIDE %04X %02X from %04X(%08X):%08X %i\n", addr, val, CS, cs, cpu_state.pc, ins);
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
			if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].features = val;
			}
			ide->cylprecomp = val;

			if (ide_drive_is_cdrom(ide_other))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].features = val;
			}
			ide_other->cylprecomp = val;
			return;

        case 0x1F2: /* Sector count */
			if (ide_drive_is_cdrom(ide))
			{
				ide_log("Sector count write: %i\n", val);
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].phase = val;
			}
			ide->secount = val;

			if (ide_drive_is_cdrom(ide_other))
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
			if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length &= 0xFF00;
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length |= val;
			}
			ide->cylinder = (ide->cylinder & 0xFF00) | val;
			ide->lba_addr = (ide->lba_addr & 0xFFF00FF) | (val << 8);

			if (ide_drive_is_cdrom(ide_other))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].request_length &= 0xFF00;
				cdrom[atapi_cdrom_drives[cur_ide[ide_board] ^ 1]].request_length |= val;
			}
			ide_other->cylinder = (ide_other->cylinder&0xFF00) | val;
			ide_other->lba_addr = (ide_other->lba_addr&0xFFF00FF) | (val << 8);
			return;

        case 0x1F5: /* Cylinder high */
			if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length &= 0xFF;
				cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].request_length |= (val << 8);
			}
			ide->cylinder = (ide->cylinder & 0xFF) | (val << 8);
			ide->lba_addr = (ide->lba_addr & 0xF00FFFF) | (val << 16);

			if (ide_drive_is_cdrom(ide_other))
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

					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = READY_STAT | DSC_STAT;
						cdrom[atapi_cdrom_drives[ide->channel]].error = 1;
						cdrom[atapi_cdrom_drives[ide->channel]].phase = 1;
						cdrom[atapi_cdrom_drives[ide->channel]].request_length = 0xEB14;
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 0;
						ide->cylinder = 0xEB14;
					}
					if (ide_drive_is_cdrom(ide_other))
					{
						cdrom[atapi_cdrom_drives[ide_other->channel]].status = READY_STAT | DSC_STAT;
						cdrom[atapi_cdrom_drives[ide_other->channel]].error = 1;
						cdrom[atapi_cdrom_drives[ide_other->channel]].phase = 1;
						cdrom[atapi_cdrom_drives[ide_other->channel]].request_length = 0xEB14;
						cdrom[atapi_cdrom_drives[ide_other->channel]].callback = 0;
						ide->cylinder = 0xEB14;
					}

					idecallback[ide_board] = 0;
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

			ide_irq_update(ide);
			return;

        case 0x1F7: /* Command register */
			if (ide->type == IDE_NONE)
			{
				return;
			}

			ide_irq_lower(ide);
			ide->command=val;

			ide->error=0;
			if (ide_drive_is_cdrom(ide))
			{
				cdrom[atapi_cdrom_drives[ide->channel]].error = 0;
			}
			switch (val)
			{
                case WIN_SRST: /* ATAPI Device Reset */
                        if (ide_drive_is_cdrom(ide))
						{
							cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
						}
                        else
						{
							ide->atastat = READY_STAT;
						}
                        timer_process();
						if (ide_drive_is_cdrom(ide))
						{
							cdrom[atapi_cdrom_drives[ide->channel]].callback = 100*IDE_TIME;
						}
                        idecallback[ide_board]=100*IDE_TIME;
                        timer_update_outstanding();
                        return;

                case WIN_RESTORE:
                case WIN_SEEK:
                        if (ide_drive_is_cdrom(ide))
						{
							cdrom[atapi_cdrom_drives[ide->channel]].status = READY_STAT;
						}
						else
						{
							ide->atastat = READY_STAT;
						}
                        timer_process();
						if (ide_drive_is_cdrom(ide))
						{
							cdrom[atapi_cdrom_drives[ide->channel]].callback = 100*IDE_TIME;
						}
                        idecallback[ide_board]=100*IDE_TIME;
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
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
					}
					else
					{
						ide->atastat = BUSY_STAT;
					}
					timer_process();
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 200*IDE_TIME;
					}
					idecallback[ide_board]=200*IDE_TIME;
					timer_update_outstanding();
					return;
                        
                case WIN_WRITE_MULTIPLE:
					if (!ide->blocksize && !ide_drive_is_cdrom(ide))
					{
						fatal("Write_MULTIPLE - blocksize = 0\n");
					}
					ide->blockcount = 0;
                        
                case WIN_WRITE:
                case WIN_WRITE_NORETRY:
					if (ide_drive_is_cdrom(ide))
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
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
					}
					else
					{
						ide->atastat = BUSY_STAT;
					}
					timer_process();
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 200*IDE_TIME;
					}
					idecallback[ide_board]=200*IDE_TIME;
					timer_update_outstanding();
					return;

                case WIN_VERIFY:
				case WIN_VERIFY_ONCE:
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
					}
					else
					{
						ide->atastat = BUSY_STAT;
					}
					timer_process();
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 200*IDE_TIME;
					}
					idecallback[ide_board]=200*IDE_TIME;
					timer_update_outstanding();
					return;

                case WIN_FORMAT:
					if (ide_drive_is_cdrom(ide))
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
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
					}
					else
					{
						ide->atastat = BUSY_STAT;
					}
					timer_process();
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 30*IDE_TIME;
					}
					idecallback[ide_board]=30*IDE_TIME;
					timer_update_outstanding();
					return;

                case WIN_DRIVE_DIAGNOSTICS: /* Execute Drive Diagnostics */
                case WIN_PIDENTIFY: /* Identify Packet Device */
                case WIN_SET_MULTIPLE_MODE: /*Set Multiple Mode*/
				case WIN_NOP:
				case WIN_STANDBYNOW1:
				case WIN_IDLENOW1:
                case WIN_SETIDLE1: /* Idle */
				case WIN_CHECKPOWERMODE1:
				case WIN_SLEEP1:
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
					}
					else
					{
						ide->atastat = BUSY_STAT;
					}
					timer_process();
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 30*IDE_TIME;
					}
					idecallback[ide_board]=30*IDE_TIME;
					timer_update_outstanding();
					return;

		case WIN_IDENTIFY: /* Identify Device */
				case WIN_READ_NATIVE_MAX:
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
					}
					else
					{
						ide->atastat = BUSY_STAT;
					}
					timer_process();
					if (ide_drive_is_cdrom(ide))
					{
						cdrom[atapi_cdrom_drives[ide->channel]].callback = 200*IDE_TIME;
					}
					idecallback[ide_board]=200*IDE_TIME;
					timer_update_outstanding();
					return;

                case WIN_PACKETCMD: /* ATAPI Packet */
					/* Skip the command callback wait, and process immediately. */
					if (ide_drive_is_cdrom(ide))
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
                        idecallback[ide_board]=1;
                        timer_update_outstanding();
                        ide->pos=0;
					}
					return;
                        
                case 0xF0:
				default:
ide_bad_command:
					if (ide_drive_is_cdrom(ide))
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
				if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].callback = 0;
				}
				idecallback[ide_board]=500*IDE_TIME;
				timer_update_outstanding();

				if (ide->type != IDE_NONE)
				{
					ide->reset = 1;
				}
				if (ide_other->type != IDE_NONE)
				{
					ide->reset = 1;
				}
				if (ide_drive_is_cdrom(ide))
				{
					cdrom[atapi_cdrom_drives[ide->channel]].status = BUSY_STAT;
				}
				ide->atastat = ide_other->atastat = BUSY_STAT;
			}
			if (val & 4)
			{
				/*Drive held in reset*/
				timer_process();
				idecallback[ide_board] = 0;
				timer_update_outstanding();
				ide->atastat = ide_other->atastat = BUSY_STAT;
			}
			ide->fdisk = ide_other->fdisk = val;
			ide_irq_update(ide);
			return;
	}
}

uint32_t ide_read_data(int ide_board, int length)
{
	IDE *ide = &ide_drives[cur_ide[ide_board]];
	uint32_t temp;

	uint8_t *idebufferb = (uint8_t *) ide->buffer;
	uint16_t *idebufferw = ide->buffer;
	uint32_t *idebufferl = (uint32_t *) ide->buffer;
	
	if (ide->command == WIN_PACKETCMD)
	{
		ide->pos = 0;
		if (!ide_drive_is_cdrom(ide))
		{
			ide_log("Drive not CD-ROM (position: %i)\n", ide->pos);
			return 0;
		}
		temp = cdrom_read(cur_ide[ide_board], length);
		if (cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].callback)
		{
			idecallback[ide_board] = cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].callback;
		}
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
		if (ide_drive_is_cdrom(ide))
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
					idecallback[ide_board]=6*IDE_TIME;
				}
				timer_update_outstanding();
			}
			else
			{
				update_status_bar_icon(0x21, 0);
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
				if (ide_drive_is_cdrom(ide))
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
			if (ide_drive_is_cdrom(ide))
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
				if (ide_drive_is_cdrom(ide))
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
				if (ide_drive_is_cdrom(ide))
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
				return 0;
			}
			if (ide_drive_is_cdrom(ide))
			{
				temp = (cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
			}
			else
			{
				temp = ide->atastat;
			}
			break;

		case 0x3F6: /* Alternate Status */
			if (ide->type == IDE_NONE)
			{
				return 0;
			}
			if (ide_drive_is_cdrom(ide))
			{
				temp = (cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
			}
			else
			{
				temp = ide->atastat;
			}
			break;

		default:
			return 0xff;
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
	off64_t addr;
	int c;
	int64_t snum;
	int cdrom_id;
	uint64_t full_size;

	ide = &ide_drives[cur_ide[ide_board]];
	ide_other = &ide_drives[cur_ide[ide_board] ^ 1];
	full_size = (hdc[cur_ide[ide->board]].tracks * hdc[cur_ide[ide->board]].hpc * hdc[cur_ide[ide->board]].spt);
	ext_ide = ide;

	if (ide_drive_is_cdrom(ide))
	{
		cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].callback = 0;
	}
	
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

		if (ide_drive_is_cdrom(ide))
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
		if (ide_drive_is_cdrom(ide_other))
		{
			cdrom_id = atapi_cdrom_drives[cur_ide[ide_board] ^ 1];
			cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
			cdrom[cdrom_id].error = 1;
			cdrom[cdrom_id].phase = 1;
			cdrom[cdrom_id].request_length=0xEB14;
			ide_other->cylinder = 0xEB14;
			if (cdrom_drives[cdrom_id].handler->stop)
			{
				cdrom_drives[cdrom_id].handler->stop(cdrom_id);
			}
		}
		if (ide_other->type == IDE_NONE)
		{
			ide_other->cylinder=0xFFFF;
		}
		return;
	}

	cdrom_id = atapi_cdrom_drives[cur_ide[ide_board]];

	switch (ide->command)
	{
		/* Initialize the Task File Registers as follows: Status = 00h, Error = 01h, Sector Count = 01h, Sector Number = 01h,
		   Cylinder Low = 14h, Cylinder High =EBh and Drive/Head = 00h. */
        case WIN_SRST: /*ATAPI Device Reset */
			ide->atastat = READY_STAT | DSC_STAT;
			ide->error=1; /*Device passed*/
			ide->secount = ide->sector = 1;
			ide_set_signature(ide);

			if (ide_drive_is_cdrom(ide))
			{
				cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
				cdrom[cdrom_id].error = 1;
				cdrom[cdrom_id].phase = 1;
				cdrom_reset(cdrom_id);
			}
			ide_irq_raise(ide);
			if (ide_drive_is_cdrom(ide))
			{
				ide->service = 0;
			}
			return;

        case WIN_RESTORE:
        case WIN_SEEK:
			if (ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
		case WIN_NOP:
		case WIN_STANDBYNOW1:
		case WIN_IDLENOW1:
		case WIN_SETIDLE1:
			if (ide_drive_is_cdrom(ide))
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
			if (ide_drive_is_cdrom(ide))
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
			if (ide_drive_is_cdrom(ide))
			{
				ide_set_signature(ide);
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			addr = ide_get_sector(ide) * 512;

			fseeko64(ide->hdfile, ide->base + addr, SEEK_SET);
			fread(ide->buffer, 512, 1, ide->hdfile);
			ide->pos=0;
			ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;

			ide_irq_raise(ide);

			update_status_bar_icon(0x21, 1);
			return;

        case WIN_READ_DMA:
			if (ide_drive_is_cdrom(ide) || (ide->board >= 2))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			addr = ide_get_sector(ide) * 512;
			fseeko64(ide->hdfile, addr, SEEK_SET);
			fread(ide->buffer, 512, 1, ide->hdfile);
			ide->pos=0;
                
			if (ide_bus_master_read)
			{
				if (ide_bus_master_read(ide_board, (uint8_t *)ide->buffer, 512))
				{
					idecallback[ide_board]=6*IDE_TIME;           /*DMA not performed, try again later*/
				}
				else
				{
					/*DMA successful*/
					ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;

					ide->secount = (ide->secount - 1) & 0xff;
					if (ide->secount)
					{
						ide_next_sector(ide);
						ide->atastat = BUSY_STAT;
						idecallback[ide_board]=6*IDE_TIME;
						update_status_bar_icon(0x21, 1);
					}
					else
					{
						ide_irq_raise(ide);
						update_status_bar_icon(0x21, 0);
					}
				}
			}

			return;

        case WIN_READ_MULTIPLE:
			/* According to the official ATA reference:

			   If the Read Multiple command is attempted before the Set Multiple Mode
			   command  has  been  executed  or  when  Read  Multiple  commands  are
			   disabled, the Read Multiple operation is rejected with an Aborted Com-
			   mand error. */
			if (ide_drive_is_cdrom(ide) || !ide->blocksize)
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}

			addr = ide_get_sector(ide) * 512;
			fseeko64(ide->hdfile, ide->base + addr, SEEK_SET);
			fread(ide->buffer, 512, 1, ide->hdfile);
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

			update_status_bar_icon(0x21, 1);
			return;

        case WIN_WRITE:
        case WIN_WRITE_NORETRY:
			if (ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			addr = ide_get_sector(ide) * 512;
			fseeko64(ide->hdfile, ide->base + addr, SEEK_SET);
			fwrite(ide->buffer, 512, 1, ide->hdfile);
			ide_irq_raise(ide);
			ide->secount = (ide->secount - 1) & 0xff;
			if (ide->secount)
			{
				ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;
				ide->pos=0;
				ide_next_sector(ide);
				update_status_bar_icon(0x21, 1);
			}
			else
			{
				ide->atastat = READY_STAT | DSC_STAT;
				update_status_bar_icon(0x21, 0);
			}

			return;
                
        case WIN_WRITE_DMA:
			if (ide_drive_is_cdrom(ide) || (ide_board >= 2))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}

			if (ide_bus_master_write)
			{
				if (ide_bus_master_write(ide_board, (uint8_t *)ide->buffer, 512))
				{
					idecallback[ide_board]=6*IDE_TIME;           /*DMA not performed, try again later*/
				}
				else
				{
					/*DMA successful*/
					addr = ide_get_sector(ide) * 512;
					fseeko64(ide->hdfile, ide->base + addr, SEEK_SET);
					fwrite(ide->buffer, 512, 1, ide->hdfile);

					ide->atastat = DRQ_STAT | READY_STAT | DSC_STAT;

					ide->secount = (ide->secount - 1) & 0xff;
					if (ide->secount)
					{
						ide_next_sector(ide);
						ide->atastat = BUSY_STAT;
						idecallback[ide_board]=6*IDE_TIME;
						update_status_bar_icon(0x21, 1);
					}
					else
					{
						ide_irq_raise(ide);
						update_status_bar_icon(0x21, 0);
					}
				}
			}

			return;

        case WIN_WRITE_MULTIPLE:
			if (ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			addr = ide_get_sector(ide) * 512;
			fseeko64(ide->hdfile, ide->base + addr, SEEK_SET);
			fwrite(ide->buffer, 512, 1, ide->hdfile);
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
				update_status_bar_icon(0x21, 1);
			}
			else
			{
				ide->atastat = READY_STAT | DSC_STAT;
				update_status_bar_icon(0x21, 0);
			}
			return;

		case WIN_VERIFY:
		case WIN_VERIFY_ONCE:
			if (ide_drive_is_cdrom(ide))
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
			update_status_bar_icon(0x21, 1);
			return;

        case WIN_FORMAT:
			if (ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			if (!ide->specify_success)
			{
				goto id_not_found;
			}
			addr = ide_get_sector(ide) * 512;
			fseeko64(ide->hdfile, ide->base + addr, SEEK_SET);
			memset(ide->buffer, 0, 512);
			for (c=0;c<ide->secount;c++)
			{
				fwrite(ide->buffer, 512, 1, ide->hdfile);
			}
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);

			/* update_status_bar_icon(0x21, 1); */
			return;

        case WIN_DRIVE_DIAGNOSTICS:
			ide_set_signature(ide);
			ide->error=1; /*No error detected*/

			if (ide_drive_is_cdrom(ide))
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
			return;

        case WIN_SPECIFY: /* Initialize Drive Parameters */
			if (ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			full_size /= (ide->head+1);
			full_size /= ide->secount;
			ide->specify_success = 1;
			if (ide->hdi == 2)
			{
				hdc[cur_ide[ide->board]].at_hpc = ide->head+1;
				hdc[cur_ide[ide->board]].at_spt = ide->secount;
				fseeko64(ide->hdfile, 0x20, SEEK_SET);
				fwrite(&(hdc[cur_ide[ide->board]].at_spt), 1, 4, ide->hdfile);
				fwrite(&(hdc[cur_ide[ide->board]].at_hpc), 1, 4, ide->hdfile);
			}
			ide->spt=ide->secount;
			ide->hpc=ide->head+1;
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;

        case WIN_PIDENTIFY: /* Identify Packet Device */
			if (ide_drive_is_cdrom(ide))
			{
				ide_atapi_identify(ide);
				ide->pos = 0;
				cdrom[cdrom_id].pos = 0;
				cdrom[cdrom_id].error = 0;
				cdrom[cdrom_id].status = DRQ_STAT | READY_STAT | DSC_STAT;
				ide_irq_raise(ide);
				return;
			}
			goto abort_cmd;

        case WIN_SET_MULTIPLE_MODE:
			if (ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			ide->blocksize = ide->secount;
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;

#if 0
		case WIN_SET_FEATURES:
			if (!(ide_set_features(ide)))
			{
				goto abort_cmd;
			}
			if (ide_drive_is_cdrom(ide))
			{
				cdrom[cdrom_id].status = READY_STAT | DSC_STAT;
			}
			else
			{
				ide->atastat = READY_STAT | DSC_STAT;
			}
			ide_irq_raise(ide);
			return;
#endif
			
		case WIN_READ_NATIVE_MAX:
			if ((ide->type != IDE_HDD) || ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}
			snum = hdc[cur_ide[ide->board]].spt;
			snum *= hdc[cur_ide[ide->board]].hpc;
			snum *= hdc[cur_ide[ide->board]].tracks;
			ide_set_sector(ide, snum - 1);
			ide->atastat = READY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;
				
        case WIN_IDENTIFY: /* Identify Device */
			if (ide->type == IDE_NONE)
			{
				ide_set_signature(ide);
				cdrom[cdrom_id].status = READY_STAT | ERR_STAT | DSC_STAT;
				cdrom[cdrom_id].pos = 0;
				goto abort_cmd;
			}
			if (ide_drive_is_cdrom(ide))
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
			if (!ide_drive_is_cdrom(ide))
			{
				goto abort_cmd;
			}

			cdrom_phase_callback(atapi_cdrom_drives[cur_ide[ide_board]]);
			idecallback[ide_board] = cdrom[atapi_cdrom_drives[cur_ide[ide_board]]].callback;
			ide_log("IDE callback now: %i\n", idecallback[ide_board]);
			return;

		case 0xFF:
			goto abort_cmd;
		}

abort_cmd:
	ide->command = 0;
	if (ide_drive_is_cdrom(ide))
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
	idecallback[0] = 0;
	callbackide(0);
}

void ide_callback_sec()
{
	idecallback[1] = 0;
	callbackide(1);
}

void ide_callback_ter()
{
	idecallback[2] = 0;
	callbackide(2);
}

void ide_callback_qua()
{
	idecallback[3] = 0;
	callbackide(3);
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

/* *** REMOVE FROM CODE SUBMITTED TO MAINLINE - START *** */
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
/* *** REMOVE FROM CODE SUBMITTED TO MAINLINE - END *** */

static uint16_t ide_base_main[2] = { 0x1f0, 0x170 };
static uint16_t ide_side_main[2] = { 0x3f6, 0x376 };

void ide_pri_enable()
{
        io_sethandler(0x01f0, 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
        io_sethandler(0x03f6, 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
		ide_base_main[0] = 0x1f0;
		ide_side_main[0] = 0x3f6;
}

void ide_pri_enable_ex()
{
		if (ide_base_main[0] & 0x300)
		{
			pclog("Enabling primary base (%04X)...\n", ide_base_main[0]);
			io_sethandler(ide_base_main[0], 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
		}
		if (ide_side_main[0] & 0x300)
		{
			pclog("Enabling primary side (%04X)...\n", ide_side_main[0]);
			io_sethandler(ide_side_main[0], 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
		}
}

void ide_pri_disable()
{
        io_removehandler(ide_base_main[0], 0x0008, ide_read_pri, ide_read_pri_w, ide_read_pri_l, ide_write_pri, ide_write_pri_w, ide_write_pri_l, NULL);
        io_removehandler(ide_side_main[0], 0x0001, ide_read_pri, NULL,           NULL,           ide_write_pri, NULL,            NULL           , NULL);
}

void ide_sec_enable()
{
        io_sethandler(0x0170, 0x0008, ide_read_sec, ide_read_sec_w, ide_read_sec_l, ide_write_sec, ide_write_sec_w, ide_write_sec_l, NULL);
        io_sethandler(0x0376, 0x0001, ide_read_sec, NULL,           NULL,           ide_write_sec, NULL,            NULL           , NULL);
		ide_base_main[1] = 0x170;
		ide_side_main[1] = 0x376;
}

void ide_sec_enable_ex()
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

void ide_sec_disable()
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

/* *** REMOVE FROM CODE SUBMITTED TO MAINLINE - START *** */
void ide_ter_enable()
{
        io_sethandler(0x0168, 0x0008, ide_read_ter, ide_read_ter_w, ide_read_ter_l, ide_write_ter, ide_write_ter_w, ide_write_ter_l, NULL);
        io_sethandler(0x036e, 0x0001, ide_read_ter, NULL,           NULL,           ide_write_ter, NULL,            NULL           , NULL);
}

void ide_ter_disable()
{
        io_removehandler(0x0168, 0x0008, ide_read_ter, ide_read_ter_w, ide_read_ter_l, ide_write_ter, ide_write_ter_w, ide_write_ter_l, NULL);
        io_removehandler(0x036e, 0x0001, ide_read_ter, NULL,           NULL,           ide_write_ter, NULL,            NULL           , NULL);
}

void ide_ter_disable_cond()
{
		if ((ide_drives[4].type == IDE_NONE) && (ide_drives[5].type == IDE_NONE))
		{
			ide_ter_disable();
		}
}

void ide_ter_init()
{
		ide_ter_enable();

        timer_add(ide_callback_ter, &idecallback[2], &idecallback[2],  NULL);
}

void ide_qua_enable()
{
        io_sethandler(0x01e8, 0x0008, ide_read_qua, ide_read_qua_w, ide_read_qua_l, ide_write_qua, ide_write_qua_w, ide_write_qua_l, NULL);
        io_sethandler(0x03ee, 0x0001, ide_read_qua, NULL,           NULL,           ide_write_qua, NULL,            NULL           , NULL);
}

void ide_qua_disable_cond()
{
		if ((ide_drives[6].type == IDE_NONE) && (ide_drives[7].type == IDE_NONE))
		{
			ide_qua_disable();
		}
}

void ide_qua_disable()
{
        io_removehandler(0x01e8, 0x0008, ide_read_qua, ide_read_qua_w, ide_read_qua_l, ide_write_qua, ide_write_qua_w, ide_write_qua_l, NULL);
        io_removehandler(0x03ee, 0x0001, ide_read_qua, NULL,           NULL,           ide_write_qua, NULL,            NULL           , NULL);
}

void ide_qua_init()
{
		ide_qua_enable();

        timer_add(ide_callback_qua, &idecallback[3], &idecallback[3],  NULL);
}
/* *** REMOVE FROM CODE SUBMITTED TO MAINLINE - END *** */

void ide_init()
{
        ide_pri_enable();
        ide_sec_enable();
        ide_bus_master_read = ide_bus_master_write = NULL;
        
        timer_add(ide_callback_pri, &idecallback[0], &idecallback[0],  NULL);
        timer_add(ide_callback_sec, &idecallback[1], &idecallback[1],  NULL);
}

void ide_set_bus_master(int (*read)(int channel, uint8_t *data, int transfer_length), int (*write)(int channel, uint8_t *data, int transfer_length), void (*set_irq)(int channel))
{
        ide_bus_master_read = read;
        ide_bus_master_write = write;
        ide_bus_master_set_irq = set_irq;
}
