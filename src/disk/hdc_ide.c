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
 * Version:	@(#)hdc_ide.c	1.0.55	2018/10/25
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
#include "../mem.h"
#include "../pic.h"
#include "../pci.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "../scsi/scsi_device.h"
#include "../cdrom/cdrom.h"
#include "../plat.h"
#include "../ui.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "hdd.h"
#include "zip.h"


/* Bits of 'atastat' */
#define ERR_STAT			0x01 /* Error */
#define IDX_STAT			0x02 /* Index */
#define CORR_STAT			0x04 /* Corrected data */
#define DRQ_STAT			0x08 /* Data request */
#define DSC_STAT                	0x10 /* Drive seek complete */
#define SERVICE_STAT            	0x10 /* ATAPI service */
#define DWF_STAT			0x20 /* Drive write fault */
#define DRDY_STAT			0x40 /* Ready */
#define BSY_STAT			0x80 /* Busy */

/* Bits of 'error' */
#define AMNF_ERR			0x01 /* Address mark not found */
#define TK0NF_ERR			0x02 /* Track 0 not found */
#define ABRT_ERR			0x04 /* Command aborted */
#define MCR_ERR				0x08 /* Media change request */
#define IDNF_ERR			0x10 /* Sector ID not found */
#define MC_ERR				0x20 /* Media change */
#define UNC_ERR				0x40 /* Uncorrectable data error */
#define BBK_ERR				0x80 /* Bad block mark detected */

/* ATA Commands */
#define WIN_NOP				0x00
#define WIN_SRST			0x08 /* ATAPI Device Reset */
#define WIN_RECAL			0x10
#define WIN_READ			0x20 /* 28-Bit Read */
#define WIN_READ_NORETRY                0x21 /* 28-Bit Read - no retry */
#define WIN_WRITE			0x30 /* 28-Bit Write */
#define WIN_WRITE_NORETRY		0x31 /* 28-Bit Write - no retry */
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

#define FEATURE_SET_TRANSFER_MODE	0x03
#define FEATURE_ENABLE_IRQ_OVERLAPPED	0x5d
#define FEATURE_ENABLE_IRQ_SERVICE	0x5e
#define FEATURE_DISABLE_REVERT		0x66
#define FEATURE_ENABLE_REVERT		0xcc
#define FEATURE_DISABLE_IRQ_OVERLAPPED	0xdd
#define FEATURE_DISABLE_IRQ_SERVICE	0xde

#define IDE_PCI (PCI && pio_override && (ide->board < 2))


typedef struct {
    int bit32, cur_dev,
	irq;
    int64_t callback;
} ide_board_t;

static ide_board_t	*ide_boards[4];
static int		pio_override = 0;

ide_t	*ide_drives[IDE_NUM];
int	(*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length, void *priv);
int	(*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length, void *priv);
void	(*ide_bus_master_set_irq)(int channel, void *priv);
void	*ide_bus_master_priv[2];
int	ide_inited = 0;
int	ide_ter_enabled = 0, ide_qua_enabled = 0;

static uint16_t	ide_base_main[4] = { 0x1f0, 0x170, 0x168, 0x1e8 };
static uint16_t	ide_side_main[4] = { 0x3f6, 0x376, 0x36e, 0x3ee };

static void	ide_callback(void *priv);


#define IDE_TIME (20LL * TIMER_USEC) / 3LL


#ifdef ENABLE_IDE_LOG
int ide_do_log = ENABLE_IDE_LOG;


static void
ide_log(const char *fmt, ...)
{
    va_list ap;

    if (ide_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define ide_log(fmt, ...)
#endif


uint8_t
getstat(ide_t *ide) {
    return ide->atastat;
}


int64_t
ide_get_period(ide_t *ide, int size)
{
    double period = 10.0 / 3.0;

    switch(ide->mdma_mode & 0x300) {
	case 0x000:	/* PIO */
		switch(ide->mdma_mode & 0xff) {
			case 0:
				period = 10.0 / 3.0;
				break;
			case 1:
				period = (period * 600.0) / 383.0;
				break;
			case 2:
				period = 25.0 / 3.0;
				break;
			case 3:
				period = 100.0 / 9.0;
				break;
			case 4:
				period = 50.0 / 3.0;
				break;
		}
		break;
	case 0x100:	/* Single Word DMA */
		switch(ide->mdma_mode & 0xff) {
			case 0:
				period = 25.0 / 12.0;
				break;
			case 1:
				period = 25.0 / 6.0;
				break;
			case 2:
				period = 25.0 / 3.0;
				break;
		}
		break;
	case 0x200:	/* Multiword DMA */
		switch(ide->mdma_mode & 0xff) {
			case 0:
				period = 25.0 / 6.0;
				break;
			case 1:
				period = 40.0 / 3.0;
				break;
			case 2:
				period = 50.0 / 3.0;
				break;
		}
		break;
	case 0x300:	/* Ultra DMA */
		switch(ide->mdma_mode & 0xff) {
			case 0:
				period = 50.0 / 3.0;
				break;
			case 1:
				period = 25.0;
				break;
			case 2:
				period = 100.0 / 3.0;
				break;
			case 3:
				period = 400.0 / 9.0;
				break;
			case 4:
				period = 200.0 / 3.0;
				break;
			case 5:
				period = 100.0;
				break;
		}
		break;
    }

    period *= 1048576.0;	/* period * MB */
    period = 1000000.0 / period;
    period *= (double) TIMER_USEC;
    period *= (double) size;
    return (int64_t) period;
}


#if 0
int64_t
ide_get_seek_time(ide_t *ide, uint32_t new_pos)
{
    double dusec, time;
    uint32_t pos = hdd_image_get_pos(ide->hdd_num);
    uint32_t t, nt;
    t = pos / ide->spt;
    nt = new_pos / ide->spt;

    dusec = (double) TIMER_USEC;
    time = (1000000.0 / 2800.0) * dusec;	/* Revolution (1/2800 s). */

    if ((t % ide->hpc) != (pos % ide->hpc))	/* Head change. */
	time += (dusec / 250.0);		/* 4ns */

    t /= ide->hpc;
    nt /= ide->hpc;

    if (t != nt) {
	t = ABS(t - nt);
	time += ((40000.0 * dusec) / ((double) ide->tracks)) * ((double) t);
    }
    return (int64_t) time;
}
#endif


int
ide_drive_is_atapi(ide_t *ide)
{
    int ch = ide->channel;

    if (ch >= 8)
	return 0;

    if (ide->type == IDE_ATAPI)
	return 1;
    else
	return 0;
}


void
ide_irq_raise(ide_t *ide)
{
    if (!ide_boards[ide->board])
	return;

    /* ide_log("Raising IRQ %i (board %i)\n", ide_boards[ide->board]->irq, ide->board); */

    if (!(ide->fdisk & 2) && (ide_boards[ide->board]->irq != -1)) {
	if ((ide->board < 2) && ide_bus_master_set_irq)
		ide_bus_master_set_irq(ide->board | 0x40, ide_bus_master_priv[ide->board]);
	else
		picint(1 << ide_boards[ide->board]->irq);
    }

    ide->irqstat=1;
    ide->service=1;
}


void
ide_irq_lower(ide_t *ide)
{
    if (!ide_boards[ide->board])
	return;

    /* ide_log("Lowering IRQ %i (board %i)\n", ide_boards[ide->board]->irq, ide->board); */

    if ((ide_boards[ide->board]->irq != -1) && ide->irqstat) {
	if ((ide->board < 2) && ide_bus_master_set_irq)
		ide_bus_master_set_irq(ide->board, ide_bus_master_priv[ide->board]);
	else
		picintc(1 << ide_boards[ide->board]->irq);
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
void
ide_padstr(char *str, const char *src, int len)
{
    int i, v;

    for (i = 0; i < len; i++) {
	if (*src != '\0')
		v = *src++;
	else
		v = ' ';
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

    for (i = 0; i < buf_size; i++) {
	if (*src != '\0')
		buf[i] = *src++;
	else
		buf[i] = ' ';
    }
}


static int
ide_get_max(ide_t *ide, int type)
{
    if (ide_drive_is_atapi(ide))
	return ide->get_max(!IDE_PCI, type);

    switch(type) {
	case TYPE_PIO:	/* PIO */
		if (IDE_PCI)
			return 1;

		return 0;	/* Maximum PIO 0 for legacy PIO-only drive. */
	case TYPE_SDMA:	/* SDMA */
		if (IDE_PCI)
			return 2;

		return -1;
	case TYPE_MDMA:	/* MDMA */
		if (IDE_PCI)
			return 2;

		return -1;
	case TYPE_UDMA:	/* UDMA */
		if (IDE_PCI)
			return 2;

		return -1;
	default:
		fatal("Unknown transfer type: %i\n", type);
		return -1;
    }
}


static int
ide_get_timings(ide_t *ide, int type)
{
    if (ide_drive_is_atapi(ide))
	return ide->get_timings(!IDE_PCI, type);

    switch(type) {
	case TIMINGS_DMA:
		if (IDE_PCI)
			return 120;

		return 0;
	case TIMINGS_PIO:
		if (IDE_PCI)
			return 120;

		return 0;
	case TIMINGS_PIO_FC:
		return 0;
	default:
		fatal("Unknown transfer type: %i\n", type);
		return 0;
    }
}


/**
 * Fill in ide->buffer with the output of the "IDENTIFY DEVICE" command
 */
static void ide_hd_identify(ide_t *ide)
{
    char device_identify[9] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', 0 };

    uint32_t d_hpc, d_spt, d_tracks;
    uint64_t full_size = (hdd[ide->hdd_num].tracks * hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);

    device_identify[6] = (ide->hdd_num / 10) + 0x30;
    device_identify[7] = (ide->hdd_num % 10) + 0x30;
    ide_log("IDE Identify: %s\n", device_identify);

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
    ide->buffer[50] = 0x4000; /* Capabilities */
    ide->buffer[59] = ide->blocksize ? (ide->blocksize | 0x100) : 0;

    if ((ide->tracks >= 1024) || (ide->hpc > 16) || (ide->spt > 63)) {
	ide->buffer[49] = (1 << 9);
	ide_log("LBA supported\n");

	ide->buffer[60] = full_size & 0xFFFF; /* Total addressable sectors (LBA) */
	ide->buffer[61] = (full_size >> 16) & 0x0FFF;
	ide_log("Full size: %" PRIu64 "\n", full_size);

        /*
		Bit 0 = The fields reported in words 54-58 are valid;
		Bit 1 = The fields reported in words 64-70 are valid;
		Bit 2 = The fields reported in word 88 are valid.	*/
	ide->buffer[53] = 1;

	if (ide->cfg_spt != 0) {
		ide->buffer[54] = (full_size / ide->cfg_hpc) / ide->cfg_spt;
		ide->buffer[55] = ide->cfg_hpc;
		ide->buffer[56] = ide->cfg_spt;
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

    if (IDE_PCI) {
	ide->buffer[47] = 32 | 0x8000;  /*Max sectors on multiple transfer command*/
	ide->buffer[80] = 0x1e; /*ATA-1 to ATA-4 supported*/
	ide->buffer[81] = 0x18; /*ATA-4 revision 18 supported*/
    } else {
	ide->buffer[47] = 16 | 0x8000;  /*Max sectors on multiple transfer command*/
	ide->buffer[80] = 0x0e; /*ATA-1 to ATA-3 supported*/
    }
}


static void
ide_identify(ide_t *ide)
{
    int d, i, max_pio, max_sdma, max_mdma, max_udma;

    ide_log("IDE IDENTIFY or IDENTIFY PACKET DEVICE on board %i (channel %i)\n", ide->board, ide->channel);

    memset(ide->buffer, 0, 512);

    if (ide_drive_is_atapi(ide))
	ide->identify(ide, IDE_PCI);
    else if (ide->type != IDE_NONE)
	ide_hd_identify(ide);
    else {
	fatal("IDE IDENTIFY or IDENTIFY PACKET DEVICE on non-attached IDE device\n");
	return;
    }

    max_pio = ide_get_max(ide, TYPE_PIO);
    max_sdma = ide_get_max(ide, TYPE_SDMA);
    max_mdma = ide_get_max(ide, TYPE_MDMA);
    max_udma = ide_get_max(ide, TYPE_UDMA);

    if (ide_boards[ide->board]->bit32)
	ide->buffer[48] |= 1;   /*Dword transfers supported*/
    ide->buffer[51] = ide_get_timings(ide, TIMINGS_PIO);
    ide->buffer[53] &= 0xfff9;
    ide->buffer[52] = ide->buffer[62] = ide->buffer[63] = ide->buffer[64] = 0x0000;
    ide->buffer[65] = ide->buffer[66] = ide->buffer[67] = ide->buffer[68] = 0x0000;
    ide->buffer[88] = 0x0000;

    if (max_pio >= 3) {
	ide->buffer[53] |= 0x0002;
	ide->buffer[67] = ide_get_timings(ide, TIMINGS_PIO);
	ide->buffer[68] = ide_get_timings(ide, TIMINGS_PIO_FC);
	for (i = 3; i <= max_pio; i++)
		ide->buffer[64] |= (1 << (i - 3));
    }
    if (max_sdma != -1) {
	for (i = 0; i <= max_sdma; i++)
		ide->buffer[62] |= (1 << i);
    }
    if (max_mdma != -1) {
	for (i = 0; i <= max_mdma; i++)
		ide->buffer[63] |= (1 << i);
    }
    if (max_udma != -1) {
	ide->buffer[53] |= 0x0004;
	for (i = 0; i <= max_udma; i++)
		ide->buffer[88] |= (1 << i);
    }

    if ((max_sdma != -1) || (max_mdma != -1) || (max_udma != -1)) {
	ide->buffer[49] |= 0x100; /* DMA supported */
	ide->buffer[52] = ide_get_timings(ide, TIMINGS_DMA);
    }

    if ((max_mdma != -1) || (max_udma != -1)) {
	ide->buffer[65] = ide_get_timings(ide, TIMINGS_DMA);
	ide->buffer[66] = ide_get_timings(ide, TIMINGS_DMA);
    }

    if (ide->mdma_mode != -1) {
	d = (ide->mdma_mode & 0xff);
	d <<= 8;
	if ((ide->mdma_mode & 0x300) == 0x000) {
		if ((ide->mdma_mode & 0xff) >= 3)
			ide->buffer[64] |= d;
	} else if ((ide->mdma_mode & 0x300) == 0x100)
		ide->buffer[62] |= d;
	else if ((ide->mdma_mode & 0x300) == 0x200)
		ide->buffer[63] |= d;
	else if ((ide->mdma_mode & 0x300) == 0x300)
		ide->buffer[88] |= d;
	ide_log("PIDENTIFY DMA Mode: %04X, %04X\n", ide->buffer[62], ide->buffer[63]);
    }
}


/*
 * Return the sector offset for the current register values
 */
static off64_t
ide_get_sector(ide_t *ide)
{
    uint32_t heads, sectors;

    if (ide->lba)
	return (off64_t)ide->lba_addr + ide->skip512;
    else {
	heads = ide->cfg_hpc;
	sectors = ide->cfg_spt;

	return ((((off64_t) ide->cylinder * heads) + ide->head) *
		sectors) + (ide->sector - 1) + ide->skip512;
    }
}


/**
 * Move to the next sector using CHS addressing
 */
static void
ide_next_sector(ide_t *ide)
{
    if (ide->lba)
	ide->lba_addr++;
    else {
	ide->sector++;
	if (ide->sector == (ide->cfg_spt + 1)) {
		ide->sector = 1;
		ide->head++;
		if (ide->head == ide->cfg_hpc) {
			ide->head = 0;
			ide->cylinder++;
		}
	}
    }
}


static void
loadhd(ide_t *ide, int d, const wchar_t *fn)
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
}


void
ide_set_signature(ide_t *ide)
{
    scsi_device_data_t *atapi = (scsi_device_data_t *) ide->p;

    ide->sector=1;
    ide->head=0;

    if (ide_drive_is_atapi(ide)) {
	ide->set_signature(ide->p);
	ide->secount = atapi->phase;
	ide->cylinder = atapi->request_length;
    } else {
	ide->secount=1;
	ide->cylinder=((ide->type == IDE_HDD) ? 0 : 0xFFFF);
	if (ide->type == IDE_HDD)
		ide->drive = 0;
    }
}


static int
ide_set_features(ide_t *ide)
{
    uint8_t features, features_data;
    int mode, submode, max;

    features = ide->cylprecomp;
    features_data = ide->secount;

    ide_log("Features code %02X\n", features);

    ide_log("IDE %02X: Set features: %02X, %02X\n", ide->channel, features, features_data);

    switch(features) {
	case FEATURE_SET_TRANSFER_MODE:	/* Set transfer mode. */
		ide_log("Transfer mode %02X\n", features_data >> 3);

		mode = (features_data >> 3);
		submode = features_data & 7;

		switch(mode) {
			case 0x00:	/* PIO default */
				if (submode != 0)
					return 0;
				max = ide_get_max(ide, TYPE_PIO);
				ide->mdma_mode = (1 << max);
				ide_log("IDE %02X: Setting DPIO mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
				break;

			case 0x01:	/* PIO mode */
				max = ide_get_max(ide, TYPE_PIO);
				if (submode > max)
					return 0;
				ide->mdma_mode = (1 << submode);
				ide_log("IDE %02X: Setting  PIO mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
				break;

			case 0x02:	/* Singleword DMA mode */
				max = ide_get_max(ide, TYPE_SDMA);
				if (submode > max)
					return 0;
				ide->mdma_mode = (1 << submode) | 0x100;
				ide_log("IDE %02X: Setting SDMA mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
				break;

			case 0x04:	/* Multiword DMA mode */
				max = ide_get_max(ide, TYPE_MDMA);
				if (submode > max)
					return 0;
				ide->mdma_mode = (1 << submode) | 0x200;
				ide_log("IDE %02X: Setting MDMA mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
				break;

			case 0x08:	/* Ultra DMA mode */
				max = ide_get_max(ide, TYPE_UDMA);
				if (submode > max)
					return 0;
				ide->mdma_mode = (1 << submode) | 0x300;
				ide_log("IDE %02X: Setting UDMA mode: %02X, %08X\n", ide->channel, submode, ide->mdma_mode);
				break;

			default:
				return 0;
		}

	case FEATURE_ENABLE_IRQ_OVERLAPPED:
	case FEATURE_ENABLE_IRQ_SERVICE:
	case FEATURE_DISABLE_IRQ_OVERLAPPED:
	case FEATURE_DISABLE_IRQ_SERVICE:
		max = ide_get_max(ide, TYPE_MDMA);
		if (max == -1)
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


void
ide_set_sector(ide_t *ide, int64_t sector_num)
{
    unsigned int cyl, r;
    if (ide->lba) {
	ide->head = (sector_num >> 24);
	ide->cylinder = (sector_num >> 8);
	ide->sector = (sector_num);
    } else {
	cyl = sector_num / (hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);
	r = sector_num % (hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);
	ide->cylinder = cyl;
	ide->head = ((r / hdd[ide->hdd_num].spt) & 0x0f);
	ide->sector = (r % hdd[ide->hdd_num].spt) + 1;
    }
}


static void
ide_zero(int d)
{
    ide_t *dev;
    ide_drives[d] = (ide_t *) malloc(sizeof(ide_t));
    memset(ide_drives[d], 0, sizeof(ide_t));
    dev = ide_drives[d];
    dev->channel = d;
    dev->type = IDE_NONE;
    dev->hdd_num = -1;
    dev->atastat = DRDY_STAT | DSC_STAT;
    dev->service = 0;
    dev->board = d >> 1;
}


static void
ide_board_close(int board)
{
    ide_t *dev;
    int c, d;
    scsi_device_data_t *atapi;

    /* Close hard disk image files (if previously open) */
    for (d = 0; d < 2; d++) {
	c = (board << 1) + d;
	dev = ide_drives[c];

	if ((dev->type == IDE_HDD) && (dev->hdd_num != -1))
		hdd_image_close(dev->hdd_num);

	if (board < 4) {
		if (ide_drive_is_atapi(dev)) {
			atapi = (scsi_device_data_t *) dev->p;
			atapi->status = DRDY_STAT | DSC_STAT;
		}
	}

	if (dev->buffer)
		free(dev->buffer);

	if (dev->sector_buffer)
		free(dev->sector_buffer);

	if (dev)
		free(dev);
    }
}


void
ide_allocate_buffer(ide_t *dev)
{
    if (dev->buffer)
	return;

    dev->buffer = (uint16_t *) malloc(65536 * sizeof(uint16_t));
    memset(dev->buffer, 0, 65536 * sizeof(uint16_t));
}


void
ide_atapi_attach(ide_t *ide)
{
    if (ide->type != IDE_NONE)
	return;

    ide->type = IDE_ATAPI;
    ide_allocate_buffer(ide);
    ide_set_signature(ide);
    ide->mdma_mode = (1 << ide->get_max(!IDE_PCI, TYPE_PIO));
    ide->error = 1;
    ide->cfg_spt = ide->cfg_hpc = 0;
}


static void
ide_board_init(int board)
{
    ide_t *dev;
    int c, d;
    int ch, is_ide, valid_ch;
    int min_ch, max_ch;

    min_ch = (board << 1);
    max_ch = min_ch + 1;

    ide_log("IDE: board %i: loading disks...\n", board);
    for (d = 0; d < 2; d++) {
	c = (board << 1) + d;
	ide_zero(c);
    }

    c = 0;
    for (d = 0; d < HDD_NUM; d++) {
	is_ide = (hdd[d].bus == HDD_BUS_IDE);
	ch = hdd[d].ide_channel;

	if (board == 4) {
		valid_ch = ((ch >= 0) && (ch <= 1));
		ch |= 8;
	} else
		valid_ch = ((ch >= min_ch) && (ch <= max_ch));

	if (is_ide && valid_ch) {
		ide_log("Found IDE hard disk on channel %i\n", ch);
		loadhd(ide_drives[ch], d, hdd[d].fn);
		ide_drives[ch]->sector_buffer = (uint8_t *) malloc(256*512);
		memset(ide_drives[ch]->sector_buffer, 0, 256*512);
		if (++c >= 2) break;
	}
    }
    ide_log("IDE: board %i: done, loaded %d disks.\n", board, c);

    for (d = 0; d < 2; d++) {
	c = (board << 1) + d;
	dev = ide_drives[c];

	if (dev->type == IDE_NONE)
		continue;

	ide_allocate_buffer(dev);

	ide_set_signature(dev);

	dev->mdma_mode = (1 << ide_get_max(dev, TYPE_PIO));
	dev->error = 1;
	dev->cfg_spt = dev->cfg_hpc = 0;
    }
}


void
ide_set_callback(uint8_t board, int64_t callback)
{
    ide_board_t *dev = ide_boards[board];

    ide_log("ide_set_callback(%i)\n", board);

    if (!dev) {
	ide_log("Set callback failed\n");
	return;
    }

    if (callback)
	dev->callback = callback;
    else
	dev->callback = 0LL;
}


void
ide_write_data(ide_t *ide, uint32_t val, int length)
{
    uint8_t *idebufferb = (uint8_t *) ide->buffer;
    uint16_t *idebufferw = ide->buffer;
    uint32_t *idebufferl = (uint32_t *) ide->buffer;

    if (ide->command == WIN_PACKETCMD) {
	ide->pos = 0;

	if (!ide_drive_is_atapi(ide))
		return;

	if (ide_drive_is_atapi(ide))
		ide->packet_write(ide->p, val, length);
	return;
    } else {
	switch(length) {
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

	if (ide->pos>=512) {
		ide->pos=0;
		ide->atastat = BSY_STAT;
		timer_process();
		if (ide->command == WIN_WRITE_MULTIPLE)
			ide_callback(ide_boards[ide->board]);
		else
			ide_set_callback(ide->board, ide_get_period(ide, 512));
		timer_update_outstanding();
	}
    }
}


void
ide_writew(uint16_t addr, uint16_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];

    /* ide_log("ide_writew %04X %04X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc); */

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
	return;

    switch (addr) {
	case 0x0: /* Data */
		ide_write_data(ide, val, 2);
		break;
    }
}


static void
ide_writel(uint16_t addr, uint32_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];

    /* ide_log("ide_writel %04X %08X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc); */

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
	return;

    switch (addr) {
	case 0x0: /* Data */
		ide_write_data(ide, val & 0xffff, 2);
		ide_write_data(ide, val >> 16, 2);
		break;
    }
}


void
ide_write_devctl(uint16_t addr, uint8_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;
    scsi_device_data_t *atapi;

    ide_t *ide, *ide_other;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];
    ide_other = ide_drives[ch ^ 1];

    ide_log("ide_write_devctl %04X %02X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc);

    if ((ide->fdisk & 4) && !(val&4) && (ide->type != IDE_NONE || ide_other->type != IDE_NONE)) {
	atapi = (scsi_device_data_t *) ide->p;

	timer_process();
	if (ide_drive_is_atapi(ide))
		atapi->callback = 0LL;
	ide_set_callback(ide->board, 500LL * IDE_TIME);
	timer_update_outstanding();

	if (ide->type != IDE_NONE)
		ide->reset = 1;
	if (ide_other->type != IDE_NONE)
		ide->reset = 1;
	if (ide_drive_is_atapi(ide))
		atapi->status = BSY_STAT;
	ide->atastat = ide_other->atastat = BSY_STAT;
    }

    if (val & 4) {
	/*Drive held in reset*/
	timer_process();
	ide_set_callback(ide->board, 0LL);
	timer_update_outstanding();
	ide->atastat = ide_other->atastat = BSY_STAT;
    }
    ide->fdisk = ide_other->fdisk = val;
    return;
}


void
ide_writeb(uint16_t addr, uint8_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;
    scsi_device_data_t *atapi, *atapi_other;

    ide_t *ide, *ide_other;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];
    ide_other = ide_drives[ch ^ 1];

    ide_log("ide_write %04X %02X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc);

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
	return;

    atapi = (scsi_device_data_t *) ide->p;
    atapi_other = (scsi_device_data_t *) ide_other->p;

    switch (addr) {
	case 0x0: /* Data */
		ide_write_data(ide, val | (val << 8), 2);
		return;

	/* Note to self: for ATAPI, bit 0 of this is DMA if set, PIO if clear. */
	case 0x1: /* Features */
		if (ide_drive_is_atapi(ide)) {
			ide_log("ATAPI transfer mode: %s\n", (val & 1) ? "DMA" : "PIO");
			atapi->features = val;
		}
		ide->cylprecomp = val;

		if (ide_drive_is_atapi(ide_other))
			atapi_other->features = val;
		ide_other->cylprecomp = val;
		return;

	case 0x2: /* Sector count */
		if (ide_drive_is_atapi(ide)) {
			ide_log("Sector count write: %i\n", val);
			atapi->phase = val;
		}
		ide->secount = val;

		if (ide_drive_is_atapi(ide_other)) {
			ide_log("Other sector count write: %i\n", val);
			atapi_other->phase = val;
		}
		ide_other->secount = val;
		return;

	case 0x3: /* Sector */
		ide->sector = val;
		ide->lba_addr = (ide->lba_addr & 0xFFFFF00) | val;
		ide_other->sector = val;
		ide_other->lba_addr = (ide_other->lba_addr & 0xFFFFF00) | val;
		return;

	case 0x4: /* Cylinder low */
		if (ide_drive_is_atapi(ide)) {
			atapi->request_length &= 0xFF00;
			atapi->request_length |= val;
		}
		ide->cylinder = (ide->cylinder & 0xFF00) | val;
		ide->lba_addr = (ide->lba_addr & 0xFFF00FF) | (val << 8);

		if (ide_drive_is_atapi(ide_other)) {
			atapi_other->request_length &= 0xFF00;
			atapi_other->request_length |= val;
		}
		ide_other->cylinder = (ide_other->cylinder & 0xFF00) | val;
		ide_other->lba_addr = (ide_other->lba_addr & 0xFFF00FF) | (val << 8);
		return;

	case 0x5: /* Cylinder high */
		if (ide_drive_is_atapi(ide)) {
			atapi->request_length &= 0xFF;
			atapi->request_length |= (val << 8);
		}
		ide->cylinder = (ide->cylinder & 0xFF) | (val << 8);
		ide->lba_addr = (ide->lba_addr & 0xF00FFFF) | (val << 16);

		if (ide_drive_is_atapi(ide_other)) {
			atapi_other->request_length &= 0xFF;
			atapi_other->request_length |= (val << 8);
		}
		ide_other->cylinder = (ide_other->cylinder & 0xFF) | (val << 8);
		ide_other->lba_addr = (ide_other->lba_addr & 0xF00FFFF) | (val << 16);
		return;

	case 0x6: /* Drive/Head */
		if (ch != ((val >> 4) & 1) + (ide->board << 1)) {
			ide_boards[ide->board]->cur_dev = ((val >> 4) & 1) + (ide->board << 1);
			ch = ide_boards[ide->board]->cur_dev;

			if (ide->reset || ide_other->reset) {
				ide->atastat = ide_other->atastat = DRDY_STAT | DSC_STAT;
				ide->error = ide_other->error = 1;
				ide->secount = ide_other->secount = 1;
				ide->sector = ide_other->sector = 1;
				ide->head = ide_other->head = 0;
				ide->cylinder = ide_other->cylinder = 0;
				ide->reset = ide_other->reset = 0;

				if (ide_drive_is_atapi(ide)) {
					atapi->status = DRDY_STAT | DSC_STAT;
					atapi->error = 1;
					atapi->phase = 1;
					atapi->request_length = 0xEB14;
					atapi->callback = 0LL;
					ide->cylinder = 0xEB14;
				}

				if (ide_drive_is_atapi(ide_other)) {
					atapi_other->status = DRDY_STAT | DSC_STAT;
					atapi_other->error = 1;
					atapi_other->phase = 1;
					atapi_other->request_length = 0xEB14;
					atapi_other->callback = 0LL;
					ide->cylinder = 0xEB14;
				}

				ide_set_callback(ide->board, 0LL);
				timer_update_outstanding();
				return;
			}

			ide = ide_drives[ch];
		}
                                
		ide->head = val & 0xF;
		ide->lba = val & 0x40;
		ide_other->head = val & 0xF;
		ide_other->lba = val & 0x40;

		ide->lba_addr = (ide->lba_addr & 0x0FFFFFF) | ((val & 0xF) << 24);
		ide_other->lba_addr = (ide_other->lba_addr & 0x0FFFFFF)|((val & 0xF) << 24);
		return;

	case 0x7: /* Command register */
		if (ide->type == IDE_NONE)
			return;

		ide_irq_lower(ide);
		ide->command=val;

		ide->error=0;
		if (ide_drive_is_atapi(ide))
			atapi->error = 0;

		if (((val >= WIN_RECAL) && (val <= 0x1F)) || ((val >= WIN_SEEK) && (val <= 0x7F))) {
			if (ide_drive_is_atapi(ide))
				atapi->status = DRDY_STAT;
			else
				ide->atastat = BSY_STAT;
			timer_process();

			if (ide_drive_is_atapi(ide))
				atapi->callback = 100LL * IDE_TIME;
			ide_set_callback(ide->board, 100LL * IDE_TIME);
			timer_update_outstanding();
			return;
		}

		switch (val) {
			case WIN_SRST: /* ATAPI Device Reset */
				if (ide_drive_is_atapi(ide))
					atapi->status = BSY_STAT;
				else
					ide->atastat = DRDY_STAT;
				timer_process();

				if (ide_drive_is_atapi(ide))
					atapi->callback = 100LL * IDE_TIME;
				ide_set_callback(ide->board, 100LL * IDE_TIME);
				timer_update_outstanding();
				return;

			case WIN_READ_MULTIPLE:
				/* Fatal removed in accordance with the official ATAPI reference:
				   If the Read Multiple command is attempted before the Set Multiple Mode
				   command  has  been  executed  or  when  Read  Multiple  commands  are
				   disabled, the Read Multiple operation is rejected with an Aborted Com-
				   mand error. */
				ide->blockcount = 0;
				/* Turn on the activity indicator *here* so that it gets turned on
				   less times. */
				/* ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1); */

			case WIN_READ:
			case WIN_READ_NORETRY:
			case WIN_READ_DMA:
			case WIN_READ_DMA_ALT:
				if (ide_drive_is_atapi(ide))
					atapi->status = BSY_STAT;
				else
					ide->atastat = BSY_STAT;
				timer_process();

				if (ide_drive_is_atapi(ide))
					atapi->callback = 200LL * IDE_TIME;

				if (ide->type == IDE_HDD) {
					if ((val == WIN_READ_DMA) || (val == WIN_READ_DMA_ALT)) {
						if (ide->secount)
							ide_set_callback(ide->board, ide_get_period(ide, (int) ide->secount << 9));
						else
							ide_set_callback(ide->board, ide_get_period(ide, 131072));
					} else
						ide_set_callback(ide->board, ide_get_period(ide, 512));
				} else
					ide_set_callback(ide->board, 200LL * IDE_TIME);
				timer_update_outstanding();
				ide->do_initial_read = 1;
				return;

			case WIN_WRITE_MULTIPLE:
				if (!ide->blocksize && !ide_drive_is_atapi(ide))
					fatal("Write_MULTIPLE - blocksize = 0\n");
				ide->blockcount = 0;
				/* Turn on the activity indicator *here* so that it gets turned on
				   less times. */
				/* ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1); */

			case WIN_WRITE:
			case WIN_WRITE_NORETRY:
				if (ide_drive_is_atapi(ide)) {
					atapi->status = DRQ_STAT | DSC_STAT | DRDY_STAT;
					atapi->pos = 0;
				} else {
					ide->atastat = DRQ_STAT | DSC_STAT | DRDY_STAT;
					ide->pos=0;
				}
				return;

			case WIN_WRITE_DMA:
			case WIN_WRITE_DMA_ALT:
			case WIN_VERIFY:
			case WIN_VERIFY_ONCE:
			case WIN_IDENTIFY: /* Identify Device */
			case WIN_SET_FEATURES: /* Set Features */
			case WIN_READ_NATIVE_MAX:
				if (ide_drive_is_atapi(ide))
					atapi->status = BSY_STAT;
				else
					ide->atastat = BSY_STAT;
				timer_process();

				if (ide_drive_is_atapi(ide))
					atapi->callback = 200LL * IDE_TIME;
				if ((ide->type == IDE_HDD) &&
				    ((val == WIN_WRITE_DMA) || (val == WIN_WRITE_DMA_ALT))) {
					if (ide->secount)
						ide_set_callback(ide->board, ide_get_period(ide, (int) ide->secount << 9));
					else
						ide_set_callback(ide->board, ide_get_period(ide, 131072));
				} else if ((ide->type == IDE_HDD) &&
					   ((val == WIN_VERIFY) || (val == WIN_VERIFY_ONCE)))
					ide_set_callback(ide->board, ide_get_period(ide, 512));
				else
					ide_set_callback(ide->board, 200LL * IDE_TIME);
				timer_update_outstanding();
				return;

			case WIN_FORMAT:
				if (ide_drive_is_atapi(ide))
					goto ide_bad_command;
				else {
					ide->atastat = DRQ_STAT;
					ide->pos=0;
				}
				return;

			case WIN_SPECIFY: /* Initialize Drive Parameters */
				if (ide_drive_is_atapi(ide))
					atapi->status = BSY_STAT;
				else
					ide->atastat = BSY_STAT;
				timer_process();

				if (ide_drive_is_atapi(ide))
					atapi->callback = 30LL * IDE_TIME;
				ide_set_callback(ide->board, 30LL * IDE_TIME);
				timer_update_outstanding();
				return;

			case WIN_DRIVE_DIAGNOSTICS: /* Execute Drive Diagnostics */
				if (ide_drive_is_atapi(ide))
					atapi->status = BSY_STAT;
				else
					ide->atastat = BSY_STAT;

				if (ide_drive_is_atapi(ide_other))
					atapi_other->status = BSY_STAT;
				else
					ide_other->atastat = BSY_STAT;

				timer_process();
				if (ide_drive_is_atapi(ide))
					atapi->callback = 200LL * IDE_TIME;
				ide_set_callback(ide->board, 200LL * IDE_TIME);
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
				if (ide_drive_is_atapi(ide))
					atapi->status = BSY_STAT;
				else
					ide->atastat = BSY_STAT;
				timer_process();
				ide_callback(dev);
				timer_update_outstanding();
				return;

			case WIN_PACKETCMD: /* ATAPI Packet */
				/* Skip the command callback wait, and process immediately. */
				if (ide_drive_is_atapi(ide)) {
					atapi->packet_status = PHASE_IDLE;
					atapi->pos = 0;
					atapi->phase = 1;
					atapi->status = DRDY_STAT | DRQ_STAT;
					if (ide->interrupt_drq)
						ide_irq_raise(ide);	/* Interrupt DRQ, requires IRQ on any DRQ. */
				} else {
					ide->atastat = BSY_STAT;
					timer_process();
					ide_set_callback(ide->board, 200LL * IDE_TIME);
					timer_update_outstanding();
					ide->pos=0;
				}
				return;

			case 0xF0:
			default:
ide_bad_command:
				if (ide_drive_is_atapi(ide)) {
					atapi->status = DRDY_STAT | ERR_STAT | DSC_STAT;
					atapi->error = ABRT_ERR;
				} else {
					ide->atastat = DRDY_STAT | ERR_STAT | DSC_STAT;
					ide->error = ABRT_ERR;
				}
				ide_irq_raise(ide);
				return;
			}
			return;
    }
}


static uint32_t
ide_read_data(ide_t *ide, int length)
{
    scsi_device_data_t *atapi = (scsi_device_data_t *) ide->p;
    uint32_t temp = 0;

    if (!ide->buffer) {
	switch (length) {
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

    if (ide->command == WIN_PACKETCMD) {
	ide->pos = 0;
	if (ide_drive_is_atapi(ide))
		temp = ide->packet_read(ide->p, length);
	else {
		ide_log("Drive not ATAPI (position: %i)\n", ide->pos);
		return 0;
	}
    } else {
	switch (length) {
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
    if ((ide->pos >= 512) && (ide->command != WIN_PACKETCMD)) {
	ide->pos = 0;
	ide->atastat = DRDY_STAT | DSC_STAT;
	if (ide_drive_is_atapi(ide)) {
		atapi->status = DRDY_STAT | DSC_STAT;
		atapi->packet_status = PHASE_IDLE;
	}
	if ((ide->command == WIN_READ) || (ide->command == WIN_READ_NORETRY) || (ide->command == WIN_READ_MULTIPLE)) {
		ide->secount = (ide->secount - 1) & 0xff;
		if (ide->secount) {
			ide_next_sector(ide);
			ide->atastat = BSY_STAT;
			timer_process();
			if (ide->command == WIN_READ_MULTIPLE)
				ide_callback(ide_boards[ide->board]);
			else
				ide_set_callback(ide->board, ide_get_period(ide, 512));
			timer_update_outstanding();
		} else {
			if (ide->command != WIN_READ_MULTIPLE)
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
		}
	}
    }

    return temp;
}


static uint8_t
ide_status(ide_t *ide, int ch)
{
    scsi_device_data_t *atapi = (scsi_device_data_t *) ide->p;

    if (ide->type == IDE_NONE)
	return 0;
    else {
	if (ide_drive_is_atapi(ide))
		return (atapi->status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
	else
		return ide->atastat;
    }
}


uint8_t
ide_readb(uint16_t addr, void *priv)
{
    scsi_device_data_t *atapi;
    ide_board_t *dev = (ide_board_t *) priv;

    int ch;
    ide_t *ide;

    ch = dev->cur_dev;
    ide = ide_drives[ch];
    atapi = (scsi_device_data_t *) ide->p;

    uint8_t temp = 0xff;
    uint16_t tempw;

    addr |= 0x90;
    addr &= 0xFFF7;

    switch (addr & 0x7) {
	case 0x0: /* Data */
		tempw = ide_read_data(ide, 2);
		temp = tempw & 0xff;
		break;

	/* For ATAPI: Bits 7-4 = sense key, bit 3 = MCR (media change requested),
	              Bit 2 = ABRT (aborted command), Bit 1 = EOM (end of media),
	              and Bit 0 = ILI (illegal length indication). */
	case 0x1: /* Error */
		if (ide->type == IDE_NONE)
			temp = 0;
		else {
			if (ide_drive_is_atapi(ide))
				temp = atapi->error;
			else
				temp = ide->error;
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
	case 0x2: /* Sector count */
		if (ide_drive_is_atapi(ide))
			temp = atapi->phase;
		else
			temp = ide->secount;
		break;

	case 0x3: /* Sector */
		temp = (uint8_t)ide->sector;
		break;

	case 0x4: /* Cylinder low */
		if (ide->type == IDE_NONE)
			temp = 0xFF;
		else {
			if (ide_drive_is_atapi(ide))
				temp = atapi->request_length & 0xff;
			else
				temp = ide->cylinder & 0xff;
		}
		break;

	case 0x5: /* Cylinder high */
		if (ide->type == IDE_NONE)
			temp = 0xFF;
		else {
			if (ide_drive_is_atapi(ide))
				temp = atapi->request_length >> 8;
			else
				temp = ide->cylinder >> 8;
		}
		break;

	case 0x6: /* Drive/Head */
		temp = (uint8_t)(ide->head | ((ch & 1) ? 0x10 : 0) | (ide->lba ? 0x40 : 0) | 0xa0);
		break;

	/* For ATAPI: Bit 5 is DMA ready, but without overlapped or interlaved DMA, it is
		      DF (drive fault). */
	case 0x7: /* Status */
		ide_irq_lower(ide);
		temp = ide_status(ide, ch);
		break;
    }

    ide_log("ide_readb(%04X, %08X) = %02X\n", addr, priv, temp);
    return temp;
}


uint8_t
ide_read_alt_status(uint16_t addr, void *priv)
{
    uint8_t temp = 0xff;

    ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];

    /* Per the Seagate ATA-3 specification:
       Reading the alternate status does *NOT* clear the IRQ. */
    temp = ide_status(ide, ch);

    ide_log("ide_read_alt_status(%04X, %08X) = %02X\n", addr, priv, temp);
    return temp;
}


uint16_t
ide_readw(uint16_t addr, void *priv)
{
    uint16_t temp = 0xffff;

    ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];

    switch (addr & 0x7) {
	case 0x0: /* Data */
		temp = ide_read_data(ide, 2);
		break;
    }

    /* ide_log("ide_readw(%04X, %08X) = %04X\n", addr, priv, temp); */
    return temp;
}


static uint32_t
ide_readl(uint16_t addr, void *priv)
{
    uint16_t temp2;
    uint32_t temp = 0xffffffff;

    ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];

    switch (addr & 0x7) {
	case 0x0: /* Data */
		temp2 = ide_read_data(ide, 2);
		temp = temp2 | (ide_read_data(ide, 2) << 16);
		break;
    }

    /* ide_log("ide_readl(%04X, %08X) = %04X\n", addr, priv, temp); */
    return temp;
}


static void
ide_callback(void *priv)
{
    ide_t *ide, *ide_other;
    int snum, ret, ch;

    ide_board_t *dev = (ide_board_t *) priv;
    scsi_device_data_t *atapi, *atapi_other;
    ch = dev->cur_dev;

    ide = ide_drives[ch];
    atapi = (scsi_device_data_t *) ide->p;
    ide_other = ide_drives[ch ^ 1];
    atapi_other = (scsi_device_data_t *) ide_other->p;

    ide_set_callback(ide->board, 0LL);

    if (ide->reset) {
	ide_log("CALLBACK RESET %i  %i\n", ide->reset,ch);

	ide->atastat = ide_other->atastat = DRDY_STAT | DSC_STAT;
	ide->error = ide_other->error = 1;
	ide->secount = ide_other->secount = 1;
	ide->sector = ide_other->sector = 1;
	ide->head = ide_other->head = 0;
	ide->cylinder = ide_other->cylinder = 0;

	// ide->cfg_spt = ide->cfg_hpc = 0;		/* need new parameters (drive 0) */
	// ide_other->cfg_spt = ide_other->cfg_hpc = 0;	/* need new parameters (drive 1) */

	ide->reset = ide_other->reset = 0;

	ide_set_signature(ide);
	if (ide_drive_is_atapi(ide)) {
		atapi->status = DRDY_STAT | DSC_STAT;
		atapi->error = 1;
		if (ide->stop)
			ide->stop(ide->p);
	}

	ide_set_signature(ide_other);
	if (ide_drive_is_atapi(ide_other)) {
		atapi_other->status = DRDY_STAT | DSC_STAT;
		atapi_other->error = 1;
		if (ide_other->stop)
			ide_other->stop(ide_other->p);
	}

	return;
    }

    ide_log("CALLBACK    %02X %i  %i\n", ide->command, ide->reset,ch);

    if (((ide->command >= WIN_RECAL) && (ide->command <= 0x1F)) ||
	((ide->command >= WIN_SEEK) && (ide->command <= 0x7F))) {
	if (ide->type != IDE_HDD)
		goto abort_cmd;
	if ((ide->command >= WIN_SEEK) && (ide->command <= 0x7F)) {
		if ((ide->cylinder >= ide->tracks) || (ide->head >= ide->hpc) ||
		    !ide->sector || (ide->sector > ide->spt))
			goto id_not_found;
	}
	ide->atastat = DRDY_STAT | DSC_STAT;
	ide_irq_raise(ide);
	return;
    }

    switch (ide->command) {
	/* Initialize the Task File Registers as follows: Status = 00h, Error = 01h, Sector Count = 01h, Sector Number = 01h,
	   Cylinder Low = 14h, Cylinder High =EBh and Drive/Head = 00h. */
        case WIN_SRST: /*ATAPI Device Reset */

		ide->atastat = DRDY_STAT | DSC_STAT;
		ide->error = 1; /*Device passed*/
		ide->secount = 1;
		ide->sector = 1;		

		ide_set_signature(ide);

		if (ide_drive_is_atapi(ide)) {
			atapi->status = DRDY_STAT | DSC_STAT;
			atapi->error = 1;
			if (ide->device_reset)
				ide->device_reset(ide->p);
		}
		ide_irq_raise(ide);
		if (ide_drive_is_atapi(ide))
			ide->service = 0;
		return;

	case WIN_NOP:
	case WIN_STANDBYNOW1:
	case WIN_IDLENOW1:
	case WIN_SETIDLE1:
		if (ide_drive_is_atapi(ide))
			atapi->status = DRDY_STAT | DSC_STAT;
		else
			ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;

	case WIN_CHECKPOWERMODE1:
	case WIN_SLEEP1:
		if (ide_drive_is_atapi(ide)) {
			atapi->phase = 0xFF;
			atapi->status = DRDY_STAT | DSC_STAT;
		}
		ide->secount = 0xFF;
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;

	case WIN_READ:
	case WIN_READ_NORETRY:
		if (ide_drive_is_atapi(ide)) {
			ide_set_signature(ide);
			goto abort_cmd;
		}
		if (ide->cfg_spt == 0)
			goto id_not_found;

		if (ide->do_initial_read) {
			ide->do_initial_read = 0;
			ide->sector_pos = 0;
			if (ide->secount)
				hdd_image_read(ide->hdd_num, ide_get_sector(ide), ide->secount, ide->sector_buffer);
			else
				hdd_image_read(ide->hdd_num, ide_get_sector(ide), 256, ide->sector_buffer);
		}

		memcpy(ide->buffer, &ide->sector_buffer[ide->sector_pos*512], 512);

		ide->sector_pos++;
		ide->pos = 0;

		ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;

		ide_irq_raise(ide);

		ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
		return;

	case WIN_READ_DMA:
	case WIN_READ_DMA_ALT:
		if (ide_drive_is_atapi(ide) || !IDE_PCI) {
			ide_log("IDE %i: DMA read aborted (bad device or board)\n", ide->channel);
			goto abort_cmd;
		}
		if (ide->cfg_spt == 0) {
			ide_log("IDE %i: DMA read aborted (SPECIFY failed)\n", ide->channel);
			goto id_not_found;
		}

		ide->sector_pos = 0;
		if (ide->secount)
			ide->sector_pos = ide->secount;
		else
			ide->sector_pos = 256;
		hdd_image_read(ide->hdd_num, ide_get_sector(ide), ide->sector_pos, ide->sector_buffer);

		ide->pos=0;

		if (ide_bus_master_read) {
			/* We should not abort - we should simply wait for the host to start DMA. */
			ret = ide_bus_master_read(ide->board,
						  ide->sector_buffer, ide->sector_pos * 512,
						  ide_bus_master_priv[ide->board]);
			if (ret == 2) {
				/* Bus master DMA disabled, simply wait for the host to enable DMA. */
				ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
				ide_set_callback(ide->board, 6LL * IDE_TIME);
				return;
			} else if (ret == 1) {
				/* Bus master DMAS error, abort the command. */
				ide_log("IDE %i: DMA read aborted (failed)\n", ide->channel);
				goto abort_cmd;
			} else {
				/*DMA successful*/
				ide_log("IDE %i: DMA read successful\n", ide->channel);

				ide->atastat = DRDY_STAT | DSC_STAT;

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
		if (ide_drive_is_atapi(ide) || !ide->blocksize)
			goto abort_cmd;
		if (ide->cfg_spt == 0)
			goto id_not_found;

		if (ide->do_initial_read) {
			ide->do_initial_read = 0;
			ide->sector_pos = 0;
			if (ide->secount)
				hdd_image_read(ide->hdd_num, ide_get_sector(ide), ide->secount, ide->sector_buffer);
			else
				hdd_image_read(ide->hdd_num, ide_get_sector(ide), 256, ide->sector_buffer);
		}

		memcpy(ide->buffer, &ide->sector_buffer[ide->sector_pos*512], 512);

		ide->sector_pos++;
		ide->pos=0;

		ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
		if (!ide->blockcount)
			ide_irq_raise(ide);
		ide->blockcount++;
		if (ide->blockcount >= ide->blocksize)
			ide->blockcount = 0;
		return;

	case WIN_WRITE:
	case WIN_WRITE_NORETRY:
		if (ide_drive_is_atapi(ide))
			goto abort_cmd;
		if (ide->cfg_spt == 0)
			goto id_not_found;
		hdd_image_write(ide->hdd_num, ide_get_sector(ide), 1, (uint8_t *) ide->buffer);
		ide_irq_raise(ide);
		ide->secount = (ide->secount - 1) & 0xff;
		if (ide->secount) {
			ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
			ide->pos=0;
			ide_next_sector(ide);
			ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
		} else {
			ide->atastat = DRDY_STAT | DSC_STAT;
			ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
		}
		return;

	case WIN_WRITE_DMA:
	case WIN_WRITE_DMA_ALT:
		if (ide_drive_is_atapi(ide) || !IDE_PCI) {
			ide_log("IDE %i: DMA write aborted (bad device type or board)\n", ide->channel);
			goto abort_cmd;
		}
		if (ide->cfg_spt == 0) {
			ide_log("IDE %i: DMA write aborted (SPECIFY failed)\n", ide->channel);
			goto id_not_found;
		}

		if (ide_bus_master_read) {
			if (ide->secount)
				ide->sector_pos = ide->secount;
			else
				ide->sector_pos = 256;

			ret = ide_bus_master_write(ide->board,
						   ide->sector_buffer, ide->sector_pos * 512,
						   ide_bus_master_priv[ide->board]);

			if (ret == 2) {
				/* Bus master DMA disabled, simply wait for the host to enable DMA. */
				ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
				ide_set_callback(ide->board, 6LL * IDE_TIME);
				return;
			} else if (ret == 1) {
				/* Bus master DMA error, abort the command. */
				ide_log("IDE %i: DMA read aborted (failed)\n", ide->channel);
				goto abort_cmd;
			} else {
				/*DMA successful*/
				ide_log("IDE %i: DMA write successful\n", ide->channel);

				hdd_image_write(ide->hdd_num, ide_get_sector(ide), ide->sector_pos, ide->sector_buffer);

				ide->atastat = DRDY_STAT | DSC_STAT;

				ide_irq_raise(ide);
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
			}
		} else {
			ide_log("IDE %i: DMA write aborted (no bus master)\n", ide->channel);
			goto abort_cmd;
		}

		return;

	case WIN_WRITE_MULTIPLE:
		if (ide_drive_is_atapi(ide))
			goto abort_cmd;
		if (ide->cfg_spt == 0)
			goto id_not_found;
		hdd_image_write(ide->hdd_num, ide_get_sector(ide), 1, (uint8_t *) ide->buffer);
		ide->blockcount++;
		if (ide->blockcount >= ide->blocksize || ide->secount == 1) {
			ide->blockcount = 0;
			ide_irq_raise(ide);
		}
		ide->secount = (ide->secount - 1) & 0xff;
		if (ide->secount) {
			ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
			ide->pos=0;
			ide_next_sector(ide);
		} else {
			ide->atastat = DRDY_STAT | DSC_STAT;
			ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
		}
		return;

	case WIN_VERIFY:
	case WIN_VERIFY_ONCE:
		if (ide_drive_is_atapi(ide))
			goto abort_cmd;
		if (ide->cfg_spt == 0)
			goto id_not_found;
		ide->pos=0;
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
		return;

	case WIN_FORMAT:
		if (ide_drive_is_atapi(ide))
			goto abort_cmd;
		if (ide->cfg_spt == 0)
			goto id_not_found;
		hdd_image_zero(ide->hdd_num, ide_get_sector(ide), ide->secount);

		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);

		/* ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1); */
		return;

	case WIN_DRIVE_DIAGNOSTICS:
		ide_set_signature(ide);
		ide->error=1; /*No error detected*/

		if (ide_drive_is_atapi(ide)) {
			atapi->status = 0;
			atapi->error = 1;
			ide_irq_raise(ide);
		} else {
			ide->atastat = DRDY_STAT | DSC_STAT;
			ide->error = 1;
			ide_irq_raise(ide);
		}

		ide_set_signature(ide_other);
		ide_other->error=1; /*No error detected*/

		if (ide_drive_is_atapi(ide_other)) {
			atapi_other->status = 0;
			atapi_other->error = 1;
		} else {
			ide_other->atastat = DRDY_STAT | DSC_STAT;
			ide_other->error = 1;
		}

		ide_boards[ide->board]->cur_dev &= ~1;
		ch = ide_boards[ide->board]->cur_dev;
		return;

	case WIN_SPECIFY: /* Initialize Drive Parameters */
		if (ide_drive_is_atapi(ide))
			goto abort_cmd;
		if (ide->cfg_spt == 0) {
			/* Only accept after RESET or DIAG. */
			ide->cfg_spt = ide->secount;
			ide->cfg_hpc = ide->head + 1;
		}
		ide->command = 0x00;	
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide->error = 1;
		ide_irq_raise(ide);
		return;

	case WIN_PIDENTIFY: /* Identify Packet Device */
		if (ide_drive_is_atapi(ide)) {
			ide_identify(ide);
			ide->pos = 0;
			atapi->phase = 2;
			atapi->pos = 0;
			atapi->error = 0;
			atapi->status = DRQ_STAT | DRDY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;
		}
		goto abort_cmd;

	case WIN_SET_MULTIPLE_MODE:
		if (ide_drive_is_atapi(ide))
			goto abort_cmd;
		ide->blocksize = ide->secount;
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;

	case WIN_SET_FEATURES:
		if (ide->type == IDE_NONE)
			goto abort_cmd;

		if (!ide_set_features(ide))
				goto abort_cmd;
		else {
			if (ide_drive_is_atapi(ide)) {
				atapi->status = DRDY_STAT | DSC_STAT;
				atapi->pos = 0;
			}
			ide->atastat = DRDY_STAT | DSC_STAT;
			ide_irq_raise(ide);
		}
		return;

	case WIN_READ_NATIVE_MAX:
		if (ide->type != IDE_HDD)
			goto abort_cmd;
		snum = hdd[ide->hdd_num].spt;
		snum *= hdd[ide->hdd_num].hpc;
		snum *= hdd[ide->hdd_num].tracks;
		ide_set_sector(ide, snum - 1);
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;

	case WIN_IDENTIFY: /* Identify Device */
		if (ide->type != IDE_HDD) {
			ide_set_signature(ide);
			goto abort_cmd;
		} else {
			ide_identify(ide);
			ide->pos=0;
			ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
			ide_irq_raise(ide);
		}
		return;

	case WIN_PACKETCMD: /* ATAPI Packet */
		if (!ide_drive_is_atapi(ide) || !ide->packet_callback)
			goto abort_cmd;

		ide->packet_callback(ide->p);
		return;

	case 0xFF:
		goto abort_cmd;
    }

abort_cmd:
    ide->command = 0;
    if (ide_drive_is_atapi(ide)) {
	atapi->status = DRDY_STAT | ERR_STAT | DSC_STAT;
	atapi->error = ABRT_ERR;
	atapi->pos = 0;
    } else {
	ide->atastat = DRDY_STAT | ERR_STAT | DSC_STAT;
	ide->error = ABRT_ERR;
	ide->pos = 0;
    }
    ide_irq_raise(ide);
    return;

id_not_found:
    ide->atastat = DRDY_STAT | ERR_STAT | DSC_STAT;
    ide->error = IDNF_ERR;
    ide->pos = 0;
    ide_irq_raise(ide);
}


static void
ide_set_handlers(uint8_t board)
{
    if (ide_base_main[board] & 0x300) {
	if (ide_boards[board]->bit32) {
		io_sethandler(ide_base_main[board], 1,
			      ide_readb,           ide_readw,  ide_readl,
			      ide_writeb,          ide_writew, ide_writel,
			      ide_boards[board]);
	} else {
		io_sethandler(ide_base_main[board], 1,
			      ide_readb,           ide_readw,  NULL,
			      ide_writeb,          ide_writew, NULL,
			      ide_boards[board]);
	}
	io_sethandler(ide_base_main[board] + 1, 7,
		      ide_readb,           NULL,       NULL,
		      ide_writeb,          NULL,       NULL,
		      ide_boards[board]);
    }
    if (ide_side_main[board] & 0x300) {
	io_sethandler(ide_side_main[board], 1,
		      ide_read_alt_status, NULL,       NULL,
		      ide_write_devctl,    NULL,       NULL,
		      ide_boards[board]);
    }
}


static void
ide_remove_handlers(uint8_t board)
{
    if (ide_boards[board]->bit32) {
	io_removehandler(ide_base_main[board], 1,
			 ide_readb,           ide_readw,  ide_readl,
			 ide_writeb,          ide_writew, ide_writel,
			 ide_boards[board]);
    } else {
	io_removehandler(ide_base_main[board], 1,
			 ide_readb,           ide_readw,  NULL,
			 ide_writeb,          ide_writew, NULL,
			 ide_boards[board]);
    }
    io_removehandler(ide_base_main[board] + 1, 7,
		     ide_readb,           NULL,       NULL,
		     ide_writeb,          NULL,       NULL,
		     ide_boards[board]);
    io_removehandler(ide_side_main[board], 1,
		     ide_read_alt_status, NULL,       NULL,
		     ide_write_devctl,    NULL,       NULL,
		     ide_boards[board]);
}


void
ide_pri_enable(void)
{
    ide_set_handlers(0);
}


void
ide_pri_disable(void)
{
    ide_remove_handlers(0);
}


void
ide_sec_enable(void)
{
    ide_set_handlers(1);
}


void
ide_sec_disable(void)
{
    ide_remove_handlers(1);
}


void
ide_set_base(int controller, uint16_t port)
{
    ide_base_main[controller] = port;
}


void
ide_set_side(int controller, uint16_t port)
{
    ide_side_main[controller] = port;
}


static void *
ide_ter_init(const device_t *info)
{
    ide_boards[2] = (ide_board_t *) malloc(sizeof(ide_board_t));
    memset(ide_boards[2], 0, sizeof(ide_board_t));

    ide_boards[2]->irq = device_get_config_int("irq");
    ide_boards[2]->cur_dev = 4;

    ide_set_handlers(2);

    timer_add(ide_callback, &ide_boards[2]->callback, &ide_boards[2]->callback, ide_boards[2]);

    ide_board_init(2);

    return(ide_drives);
}


/* Close a standalone IDE unit. */
static void
ide_ter_close(void *priv)
{
    if (ide_boards[2]) {
	free(ide_boards[2]);
	ide_boards[2] = NULL;

	ide_board_close(2);
    }
}


static void *
ide_qua_init(const device_t *info)
{
    ide_boards[3] = (ide_board_t *) malloc(sizeof(ide_board_t));
    memset(ide_boards[3], 0, sizeof(ide_board_t));

    ide_boards[3]->irq = device_get_config_int("irq");
    ide_boards[3]->cur_dev = 6;

    ide_set_handlers(3);

    timer_add(ide_callback, &ide_boards[3]->callback, &ide_boards[3]->callback, ide_boards[3]);

    ide_board_init(3);

    return(ide_drives);
}


/* Close a standalone IDE unit. */
static void
ide_qua_close(void *priv)
{
    if (ide_boards[3]) {
	free(ide_boards[3]);
	ide_boards[3] = NULL;

	ide_board_close(3);
    }
}


static void
ide_clear_bus_master(void)
{
    ide_bus_master_read = ide_bus_master_write = NULL;
    ide_bus_master_set_irq = NULL;
    ide_bus_master_priv[0] = ide_bus_master_priv[1] = NULL;
}


void *
ide_xtide_init(void)
{
    ide_clear_bus_master();

    if (!ide_boards[0]) {
	ide_boards[0] = (ide_board_t *) malloc(sizeof(ide_board_t));
	memset(ide_boards[0], 0, sizeof(ide_board_t));
	ide_boards[0]->cur_dev = 0;

	timer_add(ide_callback, &ide_boards[0]->callback, &ide_boards[0]->callback,
		  ide_boards[0]);

	ide_board_init(0);
    }
    ide_boards[0]->irq = -1;

    return ide_boards[0];
}


void
ide_xtide_close(void)
{
    if (ide_boards[0]) {
	free(ide_boards[0]);
	ide_boards[0] = NULL;

	ide_board_close(0);
    }
}


void
ide_set_bus_master(int (*read)(int channel, uint8_t *data, int transfer_length, void *priv),
		   int (*write)(int channel, uint8_t *data, int transfer_length, void *priv),
		   void (*set_irq)(int channel, void *priv),
		   void *priv0, void *priv1)
{
    ide_bus_master_read = read;
    ide_bus_master_write = write;
    ide_bus_master_set_irq = set_irq;
    ide_bus_master_priv[0] = priv0;
    ide_bus_master_priv[1] = priv1;
}


void
secondary_ide_check(void)
{
    int i = 0;
    int secondary_cdroms = 0;
    int secondary_zips = 0;

    for (i=0; i<ZIP_NUM; i++) {
	if ((zip_drives[i].ide_channel >= 2) && (zip_drives[i].ide_channel <= 3) &&
	    (zip_drives[i].bus_type == ZIP_BUS_ATAPI))
		secondary_zips++;
    }
    for (i=0; i<CDROM_NUM; i++) {
	if ((cdrom[i].ide_channel >= 2) && (cdrom[i].ide_channel <= 3) &&
	    (cdrom[i].bus_type == CDROM_BUS_ATAPI))
		secondary_cdroms++;
    }
    if (!secondary_zips && !secondary_cdroms)
	ide_remove_handlers(1);
}


static void *
ide_init(const device_t *info)
{
    ide_log("Initializing IDE...\n");

    switch(info->local) {
	case 0:		/* ISA, single-channel */
	case 2:		/* ISA, dual-channel */
	case 3:		/* ISA, dual-channel, optional 2nd channel */
	case 4:		/* VLB, single-channel */
	case 6:		/* VLB, dual-channel */
	case 8:		/* PCI, single-channel */
	case 10:	/* PCI, dual-channel */
		if (!ide_inited) {
			pio_override = 0;

			if (!(info->local & 8))
				ide_clear_bus_master();
		}

		if (!(ide_inited & 1)) {
			ide_boards[0] = (ide_board_t *) malloc(sizeof(ide_board_t));
			memset(ide_boards[0], 0, sizeof(ide_board_t));
			ide_boards[0]->irq = 14;
			ide_boards[0]->cur_dev = 0;
			if (info->local & 8)
				ide_boards[0]->bit32 = 1;
			ide_base_main[0] = 0x1f0;
			ide_side_main[0] = 0x3f6;
			ide_set_handlers(0);
			timer_add(ide_callback, &ide_boards[0]->callback, &ide_boards[0]->callback,
				  ide_boards[0]);
			ide_log("Callback 0 pointer: %08X\n", &ide_boards[0]->callback);

			ide_board_init(0);

			ide_inited |= 1;
		}

		if ((info->local & 3) && !(ide_inited & 2)) {
			ide_boards[1] = (ide_board_t *) malloc(sizeof(ide_board_t));
			memset(ide_boards[1], 0, sizeof(ide_board_t));
			ide_boards[1]->irq = 15;
			ide_boards[1]->cur_dev = 2;
			if (info->local & 8)
				ide_boards[1]->bit32 = 1;
			ide_base_main[1] = 0x170;
			ide_side_main[1] = 0x376;
			ide_set_handlers(1);
			timer_add(ide_callback, &ide_boards[1]->callback, &ide_boards[1]->callback,
				  ide_boards[1]);
			ide_log("Callback 1 pointer: %08X\n", &ide_boards[1]->callback);

			ide_board_init(1);

			if (info->local & 1)
				secondary_ide_check();

			ide_inited |= 2;
		}
		break;
    }

    return(ide_drives);
}


void
ide_enable_pio_override(void)
{
    pio_override = 1;
}


static void
ide_drive_reset(int d)
{
    ide_drives[d]->channel = d;
    ide_drives[d]->atastat = DRDY_STAT | DSC_STAT;
    ide_drives[d]->service = 0;
    ide_drives[d]->board = d >> 1;

    if (ide_boards[d >> 1]) {
	ide_boards[d >> 1]->cur_dev = d & ~1;
	ide_boards[d >> 1]->callback = 0LL;
    }

    ide_set_signature(ide_drives[d]);

    if (ide_drives[d]->sector_buffer)
	memset(ide_drives[d]->sector_buffer, 0, 256*512);

    if (ide_drives[d]->buffer)
	memset(ide_drives[d]->buffer, 0, 65536 * sizeof(uint16_t));
}


/* Reset a standalone IDE unit. */
static void
ide_reset(void *p)
{
    int d;

    ide_log("Resetting IDE...\n");

    if (ide_inited & 1) {
	for (d = 0; d < 2; d++)
		ide_drive_reset(d);
    }

    if (ide_inited & 2) {
	for (d = 2; d < 4; d++)
		ide_drive_reset(d);
    }
}


/* Close a standalone IDE unit. */
static void
ide_close(void *priv)
{
    ide_log("Closing IDE...\n");

    if ((ide_inited & 1) && (ide_boards[0])) {
	free(ide_boards[0]);
	ide_boards[0] = NULL;

	ide_board_close(0);
    }

    if ((ide_inited & 2) && (ide_boards[1])) {
	free(ide_boards[1]);
	ide_boards[1] = NULL;

	ide_board_close(1);
    }

    ide_inited = 0;
}


const device_t ide_isa_device = {
    "ISA PC/AT IDE Controller",
    DEVICE_ISA | DEVICE_AT,
    0,
    ide_init, ide_close, ide_reset,
    NULL, NULL, NULL, NULL
};

const device_t ide_isa_2ch_device = {
    "ISA PC/AT IDE Controller (Dual-Channel)",
    DEVICE_ISA | DEVICE_AT,
    2,
    ide_init, ide_close, ide_reset,
    NULL, NULL, NULL, NULL
};

const device_t ide_isa_2ch_opt_device = {
    "ISA PC/AT IDE Controller (Single/Dual)",
    DEVICE_ISA | DEVICE_AT,
    3,
    ide_init, ide_close, ide_reset,
    NULL, NULL, NULL, NULL
};

const device_t ide_vlb_device = {
    "VLB IDE Controller",
    DEVICE_VLB | DEVICE_AT,
    4,
    ide_init, ide_close, ide_reset,
    NULL, NULL, NULL, NULL
};

const device_t ide_vlb_2ch_device = {
    "VLB IDE Controller (Dual-Channel)",
    DEVICE_VLB | DEVICE_AT,
    6,
    ide_init, ide_close, ide_reset,
    NULL, NULL, NULL, NULL
};

const device_t ide_pci_device = {
    "PCI IDE Controller",
    DEVICE_PCI | DEVICE_AT,
    8,
    ide_init, ide_close, ide_reset,
    NULL, NULL, NULL, NULL
};

const device_t ide_pci_2ch_device = {
    "PCI IDE Controller (Dual-Channel)",
    DEVICE_PCI | DEVICE_AT,
    10,
    ide_init, ide_close, ide_reset,
    NULL, NULL, NULL, NULL
};

static const device_config_t ide_ter_config[] =
{
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 10,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 4", 4
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                "IRQ 11", 11
                        },
                        {
                                "IRQ 12", 12
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

static const device_config_t ide_qua_config[] =
{
        {
                "irq", "IRQ", CONFIG_SELECTION, "", 11,
                {
                        {
                                "IRQ 2", 2
                        },
                        {
                                "IRQ 3", 3
                        },
                        {
                                "IRQ 4", 4
                        },
                        {
                                "IRQ 5", 5
                        },
                        {
                                "IRQ 7", 7
                        },
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                "IRQ 11", 11
                        },
                        {
                                "IRQ 12", 12
                        },
                        {
                                ""
                        }
                }
        },
        {
                "", "", -1
        }
};

const device_t ide_ter_device = {
    "Tertiary IDE Controller",
    DEVICE_AT,
    0,
    ide_ter_init, ide_ter_close, NULL,
    NULL, NULL, NULL,
    ide_ter_config
};

const device_t ide_qua_device = {
    "Quaternary IDE Controller",
    DEVICE_AT,
    0,
    ide_qua_init, ide_qua_close, NULL,
    NULL, NULL, NULL,
    ide_qua_config
};
