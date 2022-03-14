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
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
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
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/scsi_device.h>
#include <86box/isapnp.h>
#include <86box/cdrom.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdd.h>
#include <86box/zip.h>
#include <86box/version.h>


/* Bits of 'atastat' */
#define ERR_STAT			0x01 /* Error */
#define IDX_STAT			0x02 /* Index */
#define CORR_STAT			0x04 /* Corrected data */
#define DRQ_STAT			0x08 /* Data request */
#define DSC_STAT			0x10 /* Drive seek complete */
#define SERVICE_STAT			0x10 /* ATAPI service */
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
#define WIN_READ_NORETRY		0x21 /* 28-Bit Read - no retry */
#define WIN_WRITE			0x30 /* 28-Bit Write */
#define WIN_WRITE_NORETRY		0x31 /* 28-Bit Write - no retry */
#define WIN_VERIFY			0x40 /* 28-Bit Verify */
#define WIN_VERIFY_ONCE			0x41 /* Added by OBattler - deprected older ATA command, according to the specification I found, it is identical to 0x40 */
#define WIN_FORMAT			0x50
#define WIN_SEEK			0x70
#define WIN_DRIVE_DIAGNOSTICS		0x90 /* Execute Drive Diagnostics */
#define WIN_SPECIFY			0x91 /* Initialize Drive Parameters */
#define WIN_PACKETCMD			0xA0 /* Send a packet command. */
#define WIN_PIDENTIFY			0xA1 /* Identify ATAPI device */
#define WIN_READ_MULTIPLE		0xC4
#define WIN_WRITE_MULTIPLE		0xC5
#define WIN_SET_MULTIPLE_MODE		0xC6
#define WIN_READ_DMA			0xC8
#define WIN_READ_DMA_ALT		0xC9
#define WIN_WRITE_DMA			0xCA
#define WIN_WRITE_DMA_ALT		0xCB
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

#define IDE_TIME 10.0


typedef struct {
    int		bit32, cur_dev,
		irq, inited,
		diag, force_ata3;
    uint16_t	base_main, side_main;
    pc_timer_t	timer;
    ide_t	*ide[2];
} ide_board_t;

typedef struct {
    int		(*dma)(int channel, uint8_t *data, int transfer_length, int out, void *priv);
    void	(*set_irq)(int channel, void *priv);
    void	*priv;
} ide_bm_t;

static ide_board_t	*ide_boards[4] = { NULL, NULL, NULL, NULL };
static ide_bm_t		*ide_bm[4] = { NULL, NULL, NULL, NULL };

static uint8_t ide_ter_pnp_rom[] = {
    0x09, 0xf8, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, /* BOX0001, serial 0, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x10, /* PnP version 1.0, vendor version 1.0 */
    0x82, 0x0e, 0x00, 'I', 'D', 'E', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r', /* ANSI identifier */

    0x15, 0x09, 0xf8, 0x00, 0x01, 0x00, /* logical device BOX0001 */
	0x1c, 0x41, 0xd0, 0x06, 0x00, /* compatible device PNP0600 */
	0x31, 0x00, /* start dependent functions, preferred */
		0x22, 0x00, 0x04, /* IRQ 10 */
		0x47, 0x01, 0x68, 0x01, 0x68, 0x01, 0x01, 0x08, /* I/O 0x168, decodes 16-bit, 1-byte alignment, 8 addresses */
		0x47, 0x01, 0x6e, 0x03, 0x6e, 0x03, 0x01, 0x01, /* I/O 0x36E, decodes 16-bit, 1-byte alignment, 1 address */
	0x30, /* start dependent functions, acceptable */
		0x22, 0xb8, 0x1e, /* IRQ 3/4/5/7/9/10/11/12 */
		0x47, 0x01, 0x68, 0x01, 0x68, 0x01, 0x01, 0x08, /* I/O 0x168, decodes 16-bit, 1-byte alignment, 8 addresses */
		0x47, 0x01, 0x6e, 0x03, 0x6e, 0x03, 0x01, 0x01, /* I/O 0x36E, decodes 16-bit, 1-byte alignment, 1 address */
	0x30, /* start dependent functions, acceptable */
		0x22, 0xb8, 0x1e, /* IRQ 3/4/5/7/9/10/11/12 */
		0x47, 0x01, 0x00, 0x01, 0xf8, 0xff, 0x08, 0x08, /* I/O 0x100-0xFFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
		0x47, 0x01, 0x00, 0x01, 0xff, 0xff, 0x01, 0x01, /* I/O 0x100-0xFFFF, decodes 16-bit, 1-byte alignment, 1 address */
	0x38, /* end dependent functions */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};
static uint8_t ide_qua_pnp_rom[] = {
    0x09, 0xf8, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, /* BOX0001, serial 1, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x10, /* PnP version 1.0, vendor version 1.0 */
    0x82, 0x0e, 0x00, 'I', 'D', 'E', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r', /* ANSI identifier */

    0x15, 0x09, 0xf8, 0x00, 0x01, 0x00, /* logical device BOX0001 */
	0x1c, 0x41, 0xd0, 0x06, 0x00, /* compatible device PNP0600 */
	0x31, 0x00, /* start dependent functions, preferred */
		0x22, 0x00, 0x08, /* IRQ 11 */
		0x47, 0x01, 0xe8, 0x01, 0xe8, 0x01, 0x01, 0x08, /* I/O 0x1E8, decodes 16-bit, 1-byte alignment, 8 addresses */
		0x47, 0x01, 0xee, 0x03, 0xee, 0x03, 0x01, 0x01, /* I/O 0x3EE, decodes 16-bit, 1-byte alignment, 1 address */
	0x30, /* start dependent functions, acceptable */
		0x22, 0xb8, 0x1e, /* IRQ 3/4/5/7/9/10/11/12 */
		0x47, 0x01, 0xe8, 0x01, 0xe8, 0x01, 0x01, 0x08, /* I/O 0x1E8, decodes 16-bit, 1-byte alignment, 8 addresses */
		0x47, 0x01, 0xee, 0x03, 0xee, 0x03, 0x01, 0x01, /* I/O 0x3EE, decodes 16-bit, 1-byte alignment, 1 address */
	0x30, /* start dependent functions, acceptable */
		0x22, 0xb8, 0x1e, /* IRQ 3/4/5/7/9/10/11/12 */
		0x47, 0x01, 0x00, 0x01, 0xf8, 0xff, 0x08, 0x08, /* I/O 0x100-0xFFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
		0x47, 0x01, 0x00, 0x01, 0xff, 0xff, 0x01, 0x01, /* I/O 0x100-0xFFFF, decodes 16-bit, 1-byte alignment, 1 address */
	0x38, /* end dependent functions */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};

ide_t	*ide_drives[IDE_NUM];
int	ide_ter_enabled = 0, ide_qua_enabled = 0;

static void	ide_atapi_callback(ide_t *ide);
static void	ide_callback(void *priv);


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


ide_t *
ide_get_drive(int ch)
{
    if (ch >= 8)
	return NULL;

    return ide_drives[ch];
}


double
ide_get_period(ide_t *ide, int size)
{
    double period = (10.0 / 3.0);

    /* We assume that 1 MB = 1000000 B in this case, so we have as
       many B/us as there are MB/s because 1 s = 1000000 us. */
    switch(ide->mdma_mode & 0x300) {
	case 0x000:	/* PIO */
		switch(ide->mdma_mode & 0xff) {
			case 0x01:
				period = (10.0 / 3.0);
				break;
			case 0x02:
				period = (20.0 / 3.83);
				break;
			case 0x04:
				period = (25.0 / 3.0);
				break;
			case 0x08:
				period = (100.0 / 9.0);
				break;
			case 0x10:
				period = (50.0 / 3.0);
				break;
		}
		break;
	case 0x100:	/* Single Word DMA */
		switch(ide->mdma_mode & 0xff) {
			case 0x01:
				period = (25.0 / 12.0);
				break;
			case 0x02:
				period = (25.0 / 6.0);
				break;
			case 0x04:
				period = (25.0 / 3.0);
				break;
		}
		break;
	case 0x200:	/* Multiword DMA */
		switch(ide->mdma_mode & 0xff) {
			case 0x01:
				period = (25.0 / 6.0);
				break;
			case 0x02:
				period = (40.0 / 3.0);
				break;
			case 0x04:
				period = (50.0 / 3.0);
				break;
		}
		break;
	case 0x300:	/* Ultra DMA */
		switch(ide->mdma_mode & 0xff) {
			case 0x01:
				period = (50.0 / 3.0);
				break;
			case 0x02:
				period = 25.0;
				break;
			case 0x04:
				period = (100.0 / 3.0);
				break;
			case 0x08:
				period = (400.0 / 9.0);
				break;
			case 0x10:
				period = (200.0 / 3.0);
				break;
			case 0x20:
				period = 100.0;
				break;
		}
		break;
    }

    period = (1.0 / period);		/* get us for 1 byte */
    return period * ((double) size);	/* multiply by bytes to get period for the entire transfer */
}


double
ide_atapi_get_period(uint8_t channel)
{
    ide_t *ide = ide_drives[channel];

    ide_log("ide_atapi_get_period(%i)\n", channel);

    if (!ide) {
	ide_log("Get period failed\n");
	return -1.0;
    }

    return ide_get_period(ide, 1);
}


void
ide_irq_raise(ide_t *ide)
{
    if (!ide_boards[ide->board])
	return;

    /* ide_log("Raising IRQ %i (board %i)\n", ide_boards[ide->board]->irq, ide->board); */

    ide_log("IDE %i: IRQ raise\n", ide->board);

    if (!(ide->fdisk & 2) && ide->selected) {
	if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board] && ide_bm[ide->board]->set_irq)
		ide_bm[ide->board]->set_irq(ide->board | 0x40, ide_bm[ide->board]->priv);
	else if (ide_boards[ide->board]->irq != -1)
		picint(1 << ide_boards[ide->board]->irq);
    }

    ide->irqstat = 1;
    ide->service = 1;
}


void
ide_irq_lower(ide_t *ide)
{
    if (!ide_boards[ide->board])
	return;

    /* ide_log("Lowering IRQ %i (board %i)\n", ide_boards[ide->board]->irq, ide->board); */

    // ide_log("IDE %i: IRQ lower\n", ide->board);

    if (ide->irqstat && ide->selected) {
	if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board] && ide_bm[ide->board]->set_irq)
		ide_bm[ide->board]->set_irq(ide->board, ide_bm[ide->board]->priv);
	else if (ide_boards[ide->board]->irq != -1)
		picintc(1 << ide_boards[ide->board]->irq);
    }

    ide->irqstat = 0;
}


static void
ide_irq_update(ide_t *ide)
{
    if (!ide_boards[ide->board])
	return;

    /* ide_log("Raising IRQ %i (board %i)\n", ide_boards[ide->board]->irq, ide->board); */

    if (!(ide->fdisk & 2) && ide->irqstat) {
	ide_log("IDE %i: IRQ update raise\n", ide->board);
	if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board] && ide_bm[ide->board]->set_irq) {
		ide_bm[ide->board]->set_irq(ide->board, ide_bm[ide->board]->priv);
		ide_bm[ide->board]->set_irq(ide->board | 0x40, ide_bm[ide->board]->priv);
	} else if (ide_boards[ide->board]->irq != -1) {
		picintc(1 << ide_boards[ide->board]->irq);
		picint(1 << ide_boards[ide->board]->irq);
	}
    } else if ((ide->fdisk & 2) || !ide->irqstat) {
	ide_log("IDE %i: IRQ update lower\n", ide->board);
	if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board] && ide_bm[ide->board]->set_irq)
		ide_bm[ide->board]->set_irq(ide->board, ide_bm[ide->board]->priv);
	else if (ide_boards[ide->board]->irq != -1)
		picintc(1 << ide_boards[ide->board]->irq);
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
    if (ide->type == IDE_ATAPI)
	return ide->get_max(!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL), type);

    switch(type) {
	case TYPE_PIO:	/* PIO */
		if (!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL))
			return 4;

		return 0;	/* Maximum PIO 0 for legacy PIO-only drive. */
	case TYPE_SDMA:	/* SDMA */
		if (!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL))
			return 2;

		return -1;
	case TYPE_MDMA:	/* MDMA */
		if (!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL))
			return 2;

		return -1;
	case TYPE_UDMA:	/* UDMA */
		if (!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL))
			return 5;

		return -1;
	default:
		fatal("Unknown transfer type: %i\n", type);
		return -1;
    }
}


static int
ide_get_timings(ide_t *ide, int type)
{
    if (ide->type == IDE_ATAPI)
	return ide->get_timings(!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL), type);

    switch(type) {
	case TIMINGS_DMA:
		if (!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL))
			return 120;

		return 0;
	case TIMINGS_PIO:
		if (!ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL))
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
    uint64_t full_size = (((uint64_t) hdd[ide->hdd_num].tracks) * hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);

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
    ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8); /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */
	ide->buffer[0] = (1 << 6); /*Fixed drive*/
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

    if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board]) {
	ide->buffer[47] = 32 | 0x8000;  /*Max sectors on multiple transfer command*/
	ide->buffer[80] = 0x7e; /*ATA-1 to ATA-6 supported*/
	ide->buffer[81] = 0x19; /*ATA-6 revision 3a supported*/
    } else {
	ide->buffer[47] = 16 | 0x8000;  /*Max sectors on multiple transfer command*/
	ide->buffer[80] = 0x0e; /*ATA-1 to ATA-3 supported*/
    }
}


static void
ide_identify(ide_t *ide)
{
    int d, i, max_pio, max_sdma, max_mdma, max_udma;
    ide_t *ide_other = ide_drives[ide->channel ^ 1];

    ide_log("IDE IDENTIFY or IDENTIFY PACKET DEVICE on board %i (channel %i)\n", ide->board, ide->channel);

    memset(ide->buffer, 0, 512);

    if (ide->type == IDE_ATAPI)
	ide->identify(ide, !ide_boards[ide->board]->force_ata3 && (ide_bm[ide->board] != NULL));
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
    ide_log("IDE %i: max_pio = %i, max_sdma = %i, max_mdma = %i, max_udma = %i\n",
	    ide->channel, max_pio, max_sdma, max_mdma, max_udma);

    if (ide_boards[ide->board]->bit32)
	ide->buffer[48] |= 1;   /*Dword transfers supported*/
    ide->buffer[51] = ide_get_timings(ide, TIMINGS_PIO);
    ide->buffer[53] &= 0xfff9;
    ide->buffer[52] = ide->buffer[62] = ide->buffer[63] = ide->buffer[64] = 0x0000;
    ide->buffer[65] = ide->buffer[66] = ide_get_timings(ide, TIMINGS_DMA);
    ide->buffer[67] = ide->buffer[68] = 0x0000;
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
	if (max_udma >= 4)
		ide->buffer[93] = 0x6000; /* Drive reports 80-conductor cable */

	if (ide->channel & 1)
		ide->buffer[93] |= 0x0b00;
	else {
		ide->buffer[93] |= 0x000b;
		/* PDIAG- is assered by device 1, so the bit should be 1 if there's a device 1,
		   so it should be |= 0x001b if device 1 is present. */
		if (ide_other != NULL)
			ide->buffer[93] |= 0x0010;
	}
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
loadhd(ide_t *ide, int d, const char *fn)
{
    if (! hdd_image_load(d)) {
	ide->type = IDE_NONE;
	return;
    }

    ide->spt = ide->cfg_spt = hdd[d].spt;
    ide->hpc = ide->cfg_hpc = hdd[d].hpc;
    ide->tracks = hdd[d].tracks;
    ide->type = IDE_HDD;
    ide->hdd_num = d;
}


void
ide_set_signature(ide_t *ide)
{
    ide->sector=1;
    ide->head=0;

    if (ide->type == IDE_ATAPI) {
	ide->sc->phase = 1;
	ide->sc->request_length = 0xEB14;
	ide->secount = ide->sc->phase;
	ide->cylinder = ide->sc->request_length;
    } else {
	ide->secount = 1;
	ide->cylinder = ((ide->type == IDE_HDD) ? 0 : 0xFFFF);
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

		switch (mode) {
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
		break;

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
    if (ide_drives[d] == NULL)
	ide_drives[d] = (ide_t *) malloc(sizeof(ide_t));
    memset(ide_drives[d], 0, sizeof(ide_t));
    dev = ide_drives[d];
    dev->channel = d;
    dev->type = IDE_NONE;
    dev->hdd_num = -1;
    dev->atastat = DRDY_STAT | DSC_STAT;
    dev->service = 0;
    dev->board = d >> 1;
    dev->selected = !(d & 1);
    ide_boards[dev->board]->ide[d & 1] = dev;
    timer_add(&dev->timer, ide_callback, dev, 0);
}


void
ide_allocate_buffer(ide_t *dev)
{
    if (dev->buffer == NULL)
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
    ide->mdma_mode = (1 << ide->get_max(ide_boards[ide->board]->force_ata3 || !ide_bm[ide->board], TYPE_PIO));
    ide->error = 1;
    ide->cfg_spt = ide->cfg_hpc = 0;
}


void
ide_set_callback(ide_t *ide, double callback)
{

    if (!ide) {
	ide_log("ide_set_callback(NULL): Set callback failed\n");
	return;
    }

    ide_log("ide_set_callback(%i)\n", ide->channel);

    if (callback == 0.0)
	timer_stop(&ide->timer);
    else
	timer_on_auto(&ide->timer, callback);
}


void
ide_set_board_callback(uint8_t board, double callback)
{
    ide_board_t *dev = ide_boards[board];

    ide_log("ide_set_board_callback(%i)\n", board);

    if (!dev) {
	ide_log("Set board callback failed\n");
	return;
    }

    if (callback == 0.0)
	timer_stop(&dev->timer);
    else
	timer_on_auto(&dev->timer, callback);
}


static void
ide_atapi_command_bus(ide_t *ide)
{
    ide->sc->status = BUSY_STAT;
    ide->sc->phase = 1;
    ide->sc->pos = 0;
    ide->sc->callback = 1.0 * IDE_TIME;
    ide_set_callback(ide, ide->sc->callback);
}


static void
ide_atapi_callback(ide_t *ide)
{
    int out, ret = 0;

    switch(ide->sc->packet_status) {
	case PHASE_IDLE:
#ifdef ENABLE_IDE_LOG
		ide_log("PHASE_IDLE\n");
#endif
		ide->sc->pos = 0;
		ide->sc->phase = 1;
		ide->sc->status = READY_STAT | DRQ_STAT | (ide->sc->status & ERR_STAT);
		return;
	case PHASE_COMMAND:
#ifdef ENABLE_IDE_LOG
		ide_log("PHASE_COMMAND\n");
#endif
		ide->sc->status = BUSY_STAT | (ide->sc->status & ERR_STAT);
		if (ide->packet_command) {
			ide->packet_command(ide->sc, ide->sc->atapi_cdb);
			if ((ide->sc->packet_status == PHASE_COMPLETE) && (ide->sc->callback == 0.0))
				ide_atapi_callback(ide);
		}
		return;
	case PHASE_COMPLETE:
#ifdef ENABLE_IDE_LOG
		ide_log("PHASE_COMPLETE\n");
#endif
		ide->sc->status = READY_STAT;
		ide->sc->phase = 3;
		ide->sc->packet_status = PHASE_NONE;
		ide_irq_raise(ide);
		return;
	case PHASE_DATA_IN:
	case PHASE_DATA_OUT:
#ifdef ENABLE_IDE_LOG
		ide_log("PHASE_DATA_IN or PHASE_DATA_OUT\n");
#endif
		ide->sc->status = READY_STAT | DRQ_STAT | (ide->sc->status & ERR_STAT);
		ide->sc->phase = !(ide->sc->packet_status & 0x01) << 1;
		ide_irq_raise(ide);
		return;
	case PHASE_DATA_IN_DMA:
	case PHASE_DATA_OUT_DMA:
#ifdef ENABLE_IDE_LOG
		ide_log("PHASE_DATA_IN_DMA or PHASE_DATA_OUT_DMA\n");
#endif
		out = (ide->sc->packet_status & 0x01);

		if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board] && ide_bm[ide->board]->dma) {
			ret = ide_bm[ide->board]->dma(ide->board,
						      ide->sc->temp_buffer, ide->sc->packet_len,
						      out, ide_bm[ide->board]->priv);
		} else {
			/* DMA command without a bus master. */
			if (ide->bus_master_error)
				ide->bus_master_error(ide->sc);
			return;
		}

		if (ret == 0) {
		if (ide->bus_master_error)
				ide->bus_master_error(ide->sc);
		} else if (ret == 1) {
			if (out && ide->phase_data_out)
				ret = ide->phase_data_out(ide->sc);
			else if (!out && ide->command_stop)
				ide->command_stop(ide->sc);

			if ((ide->sc->packet_status == PHASE_COMPLETE) && (ide->sc->callback == 0.0))
				ide_atapi_callback(ide);
		} else if (ret == 2)
			ide_atapi_command_bus(ide);

		return;
	case PHASE_ERROR:
#ifdef ENABLE_IDE_LOG
		ide_log("PHASE_ERROR\n");
#endif
		ide->sc->status = READY_STAT | ERR_STAT;
		ide->sc->phase = 3;
		ide->sc->packet_status = PHASE_NONE;
		ide_irq_raise(ide);
		return;
	default:
		ide_log("PHASE_UNKNOWN %02X\n", ide->sc->packet_status);
		return;
    }
}


/* This is the general ATAPI PIO request function. */
static void
ide_atapi_pio_request(ide_t *ide, uint8_t out)
{
    scsi_common_t *dev = ide->sc;

    ide_irq_lower(ide_drives[ide->board]);

    dev->status = BSY_STAT;

    if (dev->pos >= dev->packet_len) {
	ide_log("%i bytes %s, command done\n", dev->pos, out ? "written" : "read");

	dev->pos = dev->request_pos = 0;
	if (out && ide->phase_data_out)
		ide->phase_data_out(dev);
	else if (!out && ide->command_stop)
		ide->command_stop(dev);

	if ((ide->sc->packet_status == PHASE_COMPLETE) && (ide->sc->callback == 0.0))
		ide_atapi_callback(ide);
    } else {
	ide_log("%i bytes %s, %i bytes are still left\n", dev->pos,
		out ? "written" : "read", dev->packet_len - dev->pos);

	/* If less than (packet length) bytes are remaining, update packet length
	   accordingly. */
	if ((dev->packet_len - dev->pos) < (dev->max_transfer_len)) {
		dev->max_transfer_len = dev->packet_len - dev->pos;
		/* Also update the request length so the host knows how many bytes to transfer. */
		dev->request_length = dev->max_transfer_len;
        }
	ide_log("CD-ROM %i: Packet length %i, request length %i\n", dev->id, dev->packet_len,
		dev->max_transfer_len);

	dev->packet_status = PHASE_DATA_IN | out;

	dev->status = BSY_STAT;
	dev->phase = 1;
	ide_atapi_callback(ide);
	ide_set_callback(ide, 0.0);

	dev->request_pos = 0;
    }
}


static uint32_t
ide_atapi_packet_read(ide_t *ide, int length)
{
    scsi_common_t *dev = ide->sc;

    uint16_t *bufferw;
    uint32_t *bufferl;

    uint32_t temp = 0;

    if (!dev || !dev->temp_buffer || (dev->packet_status != PHASE_DATA_IN))
	return 0;

    if (dev->packet_status == PHASE_DATA_IN)
	ide_log("PHASE_DATA_IN read: %i, %i, %i, %i\n", dev->request_pos, dev->max_transfer_len, dev->pos, dev->packet_len);

    bufferw = (uint16_t *) dev->temp_buffer;
    bufferl = (uint32_t *) dev->temp_buffer;

    /* Make sure we return a 0 and don't attempt to read from the buffer if we're transferring bytes beyond it,
       which can happen when issuing media access commands with an allocated length below minimum request length
       (which is 1 sector = 2048 bytes). */
    switch(length) {
	case 1:
		temp = (dev->pos < dev->packet_len) ? dev->temp_buffer[dev->pos] : 0;
		dev->pos++;
		dev->request_pos++;
		break;
	case 2:
		temp = (dev->pos < dev->packet_len) ? bufferw[dev->pos >> 1] : 0;
		dev->pos += 2;
		dev->request_pos += 2;
		break;
	case 4:
		temp = (dev->pos < dev->packet_len) ? bufferl[dev->pos >> 2] : 0;
		dev->pos += 4;
		dev->request_pos += 4;
		break;
	default:
		return 0;
    }

    if (dev->packet_status == PHASE_DATA_IN) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		ide_atapi_pio_request(ide, 0);
	}
	return temp;
    } else
	return 0;
}


static void
ide_atapi_packet_write(ide_t *ide, uint32_t val, int length)
{
    scsi_common_t *dev = ide->sc;

    uint8_t *bufferb;
    uint16_t *bufferw;
    uint32_t *bufferl;

    if (!dev)
	return;

    if (dev->packet_status == PHASE_IDLE)
	bufferb = dev->atapi_cdb;
    else {
	if (dev->temp_buffer)
		bufferb = dev->temp_buffer;
	else
		return;
    }

    bufferw = (uint16_t *) bufferb;
    bufferl = (uint32_t *) bufferb;

    switch(length) {
	case 1:
		bufferb[dev->pos] = val & 0xff;
		dev->pos++;
		dev->request_pos++;
		break;
	case 2:
		bufferw[dev->pos >> 1] = val & 0xffff;
		dev->pos += 2;
		dev->request_pos += 2;
		break;
	case 4:
		bufferl[dev->pos >> 2] = val;
		dev->pos += 4;
		dev->request_pos += 4;
		break;
	default:
		return;
    }

    if (dev->packet_status == PHASE_DATA_OUT) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		ide_atapi_pio_request(ide, 1);
	}
	return;
    } else if (dev->packet_status == PHASE_IDLE) {
	if (dev->pos >= 12) {
		dev->pos = 0;
		dev->status = BSY_STAT;
		dev->packet_status = PHASE_COMMAND;
		ide_atapi_callback(ide);
	}
	return;
    }
}


void
ide_write_data(ide_t *ide, uint32_t val, int length)
{
    uint8_t *idebufferb = (uint8_t *) ide->buffer;
    uint16_t *idebufferw = ide->buffer;
    uint32_t *idebufferl = (uint32_t *) ide->buffer;

    if (ide->command == WIN_PACKETCMD) {
	ide->pos = 0;

	if (ide->type == IDE_ATAPI)
		ide_atapi_packet_write(ide, val, length);
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

	if (ide->pos >= 512) {
		ide->pos=0;
		ide->atastat = BSY_STAT;
		if (ide->command == WIN_WRITE_MULTIPLE)
			ide_callback(ide);
		else
			ide_set_callback(ide, ide_get_period(ide, 512));
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

    ide_log("ide_writew %04X %04X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc);

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
	return;

    switch (addr) {
	case 0x0: /* Data */
		ide_write_data(ide, val, 2);
		break;
	case 0x7:
		ide_writeb(addr, val & 0xff, priv);
		break;
	default:
		ide_writeb(addr, val & 0xff, priv);
		ide_writeb(addr + 1, (val >> 8) & 0xff, priv);
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

    ide_log("ide_writel %04X %08X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc);

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
	return;

    switch (addr) {
	case 0x0: /* Data */
		ide_write_data(ide, val & 0xffff, 2);
		if (dev->bit32)
			ide_write_data(ide, val >> 16, 2);
		else
			ide_writew(addr + 2, (val >> 16) & 0xffff, priv);
		break;
	case 0x6: case 0x7:
		ide_writew(addr, val & 0xffff, priv);
		break;
	default:
		ide_writew(addr, val & 0xffff, priv);
		ide_writew(addr + 2, (val >> 16) & 0xffff, priv);
		break;
    }
}


static void
dev_reset(ide_t *ide)
{
    ide_set_signature(ide);

    if ((ide->type == IDE_ATAPI) && ide->stop)
	ide->stop(ide->sc);
}


void
ide_write_devctl(uint16_t addr, uint8_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide, *ide_other;
    int ch;
    uint8_t old;

    ch = dev->cur_dev;
    ide = ide_drives[ch];
    ide_other = ide_drives[ch ^ 1];

    ide_log("ide_write_devctl %04X %02X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc);

    if ((ide->type == IDE_NONE) && (ide_other->type == IDE_NONE))
		return;

    dev->diag = 0;

    if ((val & 4) && !(ide->fdisk & 4)) {
	/* Reset toggled from 0 to 1, initiate reset procedure. */
	if (ide->type == IDE_ATAPI)
		ide->sc->callback = 0.0;
	ide_set_callback(ide, 0.0);
	ide_set_callback(ide_other, 0.0);

	/* We must set set the status to busy in reset mode or
	   some 286 and 386 machines error out. */
	if (!(ch & 1)) {
		if (ide->type != IDE_NONE) {
			ide->atastat = BSY_STAT;
			ide->error = 1;
			if (ide->type == IDE_ATAPI) {
				ide->sc->status = BSY_STAT;
				ide->sc->error = 1;
			}
		}

		if (ide_other->type != IDE_NONE) {
			ide_other->atastat = BSY_STAT;
			ide_other->error = 1;
			if (ide_other->type == IDE_ATAPI) {
				ide_other->sc->status = BSY_STAT;
				ide_other->sc->error = 1;
			}
		}
	}
    } else if (!(val & 4) && (ide->fdisk & 4)) {
	/* Reset toggled from 1 to 0. */
	if (!(ch & 1)) {
		/* Currently active device is 0, use the device 0 reset protocol. */
		/* Device 0. */
		dev_reset(ide);
		ide->atastat = BSY_STAT;
		ide->error = 1;
		if (ide->type == IDE_ATAPI) {
			ide->sc->status = BSY_STAT;
			ide->sc->error = 1;
		}

		/* Device 1. */
		dev_reset(ide_other);
		ide_other->atastat = BSY_STAT;
		ide_other->error = 1;
		if (ide_other->type == IDE_ATAPI) {
			ide_other->sc->status = BSY_STAT;
			ide_other->sc->error = 1;
		}

		/* Fire the timer. */
		dev->diag = 0;
		ide->reset = 1;
		ide_set_callback(ide, 0.0);
		ide_set_callback(ide_other, 0.0);
		ide_set_board_callback(ide->board, 1000.4);	/* 1 ms + 400 ns, per the specification */
	} else {
		/* Currently active device is 1, simply reset the status and the active device. */
		dev_reset(ide);
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide->error = 1;
		if (ide->type == IDE_ATAPI) {
			ide->sc->status = DRDY_STAT | DSC_STAT;
			ide->sc->error = 1;
		}
		dev->cur_dev &= ~1;
		ch = dev->cur_dev;

		ide = ide_drives[ch];
		ide->selected = 1;

		ide_other = ide_drives[ch ^ 1];
		ide_other->selected = 0;
	}
    }

    old = ide->fdisk;
    ide->fdisk = ide_other->fdisk = val;
    if (!(val & 0x02) && (old & 0x02) && ide->irqstat)
	ide_irq_update(ide);
}


void
ide_writeb(uint16_t addr, uint8_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide, *ide_other;
    int ch;

    ch = dev->cur_dev;
    ide = ide_drives[ch];
    ide_other = ide_drives[ch ^ 1];

    ide_log("ide_write %04X %02X from %04X(%08X):%08X\n", addr, val, CS, cs, cpu_state.pc);

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
	return;

    switch (addr) {
	case 0x0: /* Data */
		ide_write_data(ide, val | (val << 8), 2);
		return;

	/* Note to self: for ATAPI, bit 0 of this is DMA if set, PIO if clear. */
	case 0x1: /* Features */
		if (ide->type == IDE_ATAPI) {
			ide_log("ATAPI transfer mode: %s\n", (val & 1) ? "DMA" : "PIO");
			ide->sc->features = val;
		}
		ide->cylprecomp = val;

		if (ide_other->type == IDE_ATAPI)
			ide_other->sc->features = val;
		ide_other->cylprecomp = val;
		return;

	case 0x2: /* Sector count */
		if (ide->type == IDE_ATAPI) {
			ide_log("Sector count write: %i\n", val);
			ide->sc->phase = val;
		}
		ide->secount = val;

		if (ide_other->type == IDE_ATAPI) {
			ide_log("Other sector count write: %i\n", val);
			ide_other->sc->phase = val;
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
		if (ide->type == IDE_ATAPI) {
			ide->sc->request_length &= 0xFF00;
			ide->sc->request_length |= val;
		}
		ide->cylinder = (ide->cylinder & 0xFF00) | val;
		ide->lba_addr = (ide->lba_addr & 0xFFF00FF) | (val << 8);

		if (ide_other->type == IDE_ATAPI) {
			ide_other->sc->request_length &= 0xFF00;
			ide_other->sc->request_length |= val;
		}
		ide_other->cylinder = (ide_other->cylinder & 0xFF00) | val;
		ide_other->lba_addr = (ide_other->lba_addr & 0xFFF00FF) | (val << 8);
		return;

	case 0x5: /* Cylinder high */
		if (ide->type == IDE_ATAPI) {
			ide->sc->request_length &= 0xFF;
			ide->sc->request_length |= (val << 8);
		}
		ide->cylinder = (ide->cylinder & 0xFF) | (val << 8);
		ide->lba_addr = (ide->lba_addr & 0xF00FFFF) | (val << 16);

		if (ide_other->type == IDE_ATAPI) {
			ide_other->sc->request_length &= 0xFF;
			ide_other->sc->request_length |= (val << 8);
		}
		ide_other->cylinder = (ide_other->cylinder & 0xFF) | (val << 8);
		ide_other->lba_addr = (ide_other->lba_addr & 0xF00FFFF) | (val << 16);
		return;

	case 0x6: /* Drive/Head */
		if (ch != ((val >> 4) & 1) + (ide->board << 1)) {
			ide_boards[ide->board]->cur_dev = ((val >> 4) & 1) + (ide->board << 1);
			ch = ide_boards[ide->board]->cur_dev;

			ide = ide_drives[ch];
			ide->selected = 1;

			ide_other = ide_drives[ch ^ 1];
			ide_other->selected = 0;

			if (ide->reset || ide_other->reset) {
				ide->atastat = ide_other->atastat = DRDY_STAT | DSC_STAT;
				ide->error = ide_other->error = 1;
				ide->secount = ide_other->secount = 1;
				ide->sector = ide_other->sector = 1;
				ide->head = ide_other->head = 0;
				ide->cylinder = ide_other->cylinder = 0;
				ide->reset = ide_other->reset = 0;

				if (ide->type == IDE_ATAPI) {
					ide->sc->status = DRDY_STAT | DSC_STAT;
					ide->sc->error = 1;
					ide->sc->phase = 1;
					ide->sc->request_length = 0xEB14;
					ide->sc->callback = 0.0;
					ide->cylinder = 0xEB14;
				}

				if (ide_other->type == IDE_ATAPI) {
					ide_other->sc->status = DRDY_STAT | DSC_STAT;
					ide_other->sc->error = 1;
					ide_other->sc->phase = 1;
					ide_other->sc->request_length = 0xEB14;
					ide_other->sc->callback = 0.0;
					ide_other->cylinder = 0xEB14;
				}

				ide_set_callback(ide, 0.0);
				ide_set_callback(ide_other, 0.0);
				ide_set_board_callback(ide->board, 0.0);
				return;
			}
		}

		ide->head = val & 0xF;
		ide->lba = val & 0x40;
		ide_other->head = val & 0xF;
		ide_other->lba = val & 0x40;

		ide->lba_addr = (ide->lba_addr & 0x0FFFFFF) | ((val & 0xF) << 24);
		ide_other->lba_addr = (ide_other->lba_addr & 0x0FFFFFF)|((val & 0xF) << 24);

		ide_irq_update(ide);
		return;

	case 0x7: /* Command register */
		if (ide->type == IDE_NONE)
			return;

		ide_irq_lower(ide);
		ide->command = val;

		ide->error = 0;
		if (ide->type == IDE_ATAPI)
			ide->sc->error = 0;

		if (((val >= WIN_RECAL) && (val <= 0x1F)) || ((val >= WIN_SEEK) && (val <= 0x7F))) {
			if (ide->type == IDE_ATAPI)
				ide->sc->status = DRDY_STAT;
			else
				ide->atastat = READY_STAT | BSY_STAT;

			if (ide->type == IDE_ATAPI)
				ide->sc->callback = 100.0 * IDE_TIME;
			ide_set_callback(ide, 100.0 * IDE_TIME);
			return;
		}

		switch (val) {
			case WIN_SRST: /* ATAPI Device Reset */
				if (ide->type == IDE_ATAPI) {
					ide->sc->status = BSY_STAT;
					ide->sc->callback = 100.0 * IDE_TIME;
				} else
					ide->atastat = DRDY_STAT;

				ide_set_callback(ide, 100.0 * IDE_TIME);
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
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
				/*FALLTHROUGH*/

			case WIN_READ:
			case WIN_READ_NORETRY:
			case WIN_READ_DMA:
			case WIN_READ_DMA_ALT:
				if (ide->type == IDE_ATAPI) {
					ide->sc->status = BSY_STAT;
					ide->sc->callback = 200.0 * IDE_TIME;
				} else
					ide->atastat = BSY_STAT;

				if (ide->type == IDE_HDD) {
					if ((val == WIN_READ_DMA) || (val == WIN_READ_DMA_ALT)) {
						if (ide->secount)
							ide_set_callback(ide, ide_get_period(ide, (int) ide->secount << 9));
						else
							ide_set_callback(ide, ide_get_period(ide, 131072));
					} else if (val == WIN_READ_MULTIPLE)
						ide_set_callback(ide, 200.0 * IDE_TIME);
					else
						ide_set_callback(ide, ide_get_period(ide, 512));
				} else
					ide_set_callback(ide, 200.0 * IDE_TIME);
				ide->do_initial_read = 1;
				return;

			case WIN_WRITE_MULTIPLE:
				if (!ide->blocksize && (ide->type != IDE_ATAPI))
					fatal("Write_MULTIPLE - blocksize = 0\n");
				ide->blockcount = 0;
				/* Turn on the activity indicator *here* so that it gets turned on
				   less times. */
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
				/*FALLTHROUGH*/

			case WIN_WRITE:
			case WIN_WRITE_NORETRY:
				if (ide->type == IDE_ATAPI) {
					ide->sc->status = DRQ_STAT | DSC_STAT | DRDY_STAT;
					ide->sc->pos = 0;
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
				if (ide->type == IDE_ATAPI) {
					ide->sc->status = BSY_STAT;
					ide->sc->callback = 200.0 * IDE_TIME;
				 } else
					ide->atastat = BSY_STAT;

				if ((ide->type == IDE_HDD) &&
				    ((val == WIN_WRITE_DMA) || (val == WIN_WRITE_DMA_ALT))) {
					if (ide->secount)
						ide_set_callback(ide, ide_get_period(ide, (int) ide->secount << 9));
					else
						ide_set_callback(ide, ide_get_period(ide, 131072));
				} else if ((ide->type == IDE_HDD) &&
					   ((val == WIN_VERIFY) || (val == WIN_VERIFY_ONCE)))
					ide_set_callback(ide, ide_get_period(ide, 512));
				else if (val == WIN_IDENTIFY)
					ide_callback(ide);
				else
					ide_set_callback(ide, 200.0 * IDE_TIME);
				return;

			case WIN_FORMAT:
				if (ide->type == IDE_ATAPI)
					goto ide_bad_command;
				else {
					ide->atastat = DRQ_STAT;
					ide->pos=0;
				}
				return;

			case WIN_SPECIFY: /* Initialize Drive Parameters */
				if (ide->type == IDE_ATAPI) {
					ide->sc->status = BSY_STAT;
					ide->sc->callback = 30.0 * IDE_TIME;
				} else
					ide->atastat = BSY_STAT;

				ide_set_callback(ide, 30.0 * IDE_TIME);
				return;

			case WIN_DRIVE_DIAGNOSTICS: /* Execute Drive Diagnostics */
				dev->cur_dev &= ~1;
				ide = ide_drives[ch & ~1];
				ide->selected = 1;
				ide_other = ide_drives[ch | 1];
				ide_other->selected = 0;

				/* Device 0. */
				dev_reset(ide);
				ide->atastat = BSY_STAT;
				ide->error = 1;
				if (ide->type == IDE_ATAPI) {
					ide->sc->status = BSY_STAT;
					ide->sc->error = 1;
				}

				/* Device 1. */
				dev_reset(ide_other);
				ide_other->atastat = BSY_STAT;
				ide_other->error = 1;
				if (ide_other->type == IDE_ATAPI) {
					ide_other->sc->status = BSY_STAT;
					ide_other->sc->error = 1;
				}

				/* Fire the timer. */
				dev->diag = 1;
				ide->reset = 1;
				ide_set_callback(ide, 0.0);
				ide_set_callback(ide_other, 0.0);
				ide_set_board_callback(ide->board, 200.0 * IDE_TIME);
				return;

			case WIN_PIDENTIFY: /* Identify Packet Device */
			case WIN_SET_MULTIPLE_MODE: /* Set Multiple Mode */
			case WIN_NOP:
			case WIN_STANDBYNOW1:
			case WIN_IDLENOW1:
			case WIN_SETIDLE1: /* Idle */
			case WIN_CHECKPOWERMODE1:
			case WIN_SLEEP1:
				if (ide->type == IDE_ATAPI)
					ide->sc->status = BSY_STAT;
				else
					ide->atastat = BSY_STAT;
				ide_callback(ide);
				return;

			case WIN_PACKETCMD: /* ATAPI Packet */
				/* Skip the command callback wait, and process immediately. */
				if (ide->type == IDE_ATAPI) {
					ide->sc->packet_status = PHASE_IDLE;
					ide->sc->pos = 0;
					ide->sc->phase = 1;
					ide->sc->status = DRDY_STAT | DRQ_STAT;
					if (ide->interrupt_drq)
						ide_irq_raise(ide);	/* Interrupt DRQ, requires IRQ on any DRQ. */
				} else {
					ide->atastat = BSY_STAT;
					ide_set_callback(ide, 200.0 * IDE_TIME);
					ide->pos=0;
				}
				return;

			case 0xF0:
			default:
ide_bad_command:
				if (ide->type == IDE_ATAPI) {
					ide->sc->status = DRDY_STAT | ERR_STAT | DSC_STAT;
					ide->sc->error = ABRT_ERR;
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
	if (ide->type == IDE_ATAPI)
		temp = ide_atapi_packet_read(ide, length);
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
	if (ide->type == IDE_ATAPI) {
		ide->sc->status = DRDY_STAT | DSC_STAT;
		ide->sc->packet_status = PHASE_IDLE;
	}
	if ((ide->command == WIN_READ) || (ide->command == WIN_READ_NORETRY) || (ide->command == WIN_READ_MULTIPLE)) {
		ide->secount = (ide->secount - 1) & 0xff;
		if (ide->secount) {
			ide_next_sector(ide);
			ide->atastat = BSY_STAT | READY_STAT | DSC_STAT;
			if (ide->command == WIN_READ_MULTIPLE)
				ide_callback(ide);
			else
				ide_set_callback(ide, ide_get_period(ide, 512));
		} else if (ide->command != WIN_READ_MULTIPLE)
			ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
	}
    }

    return temp;
}


static uint8_t
ide_status(ide_t *ide, ide_t *ide_other, int ch)
{
    if ((ide->type == IDE_NONE) && ((ide_other->type == IDE_NONE) || !(ch & 1)))
#ifdef STATUS_BIT_7_PULLDOWN
	return 0x7F;	/* Bit 7 pulled down, all other bits pulled up, per the spec. */
#else
	return 0xFF;
#endif
    else if ((ide->type == IDE_NONE) && (ch & 1))
	return 0x00;	/* On real hardware, a slave with a present master always returns a status of 0x00. */
    else if (ide->type == IDE_ATAPI)
	return (ide->sc->status & ~DSC_STAT) | (ide->service ? SERVICE_STAT : 0);
    else
	return ide->atastat;
}


uint8_t
ide_readb(uint16_t addr, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;

    int ch;
    ide_t *ide;

    ch = dev->cur_dev;
    ide = ide_drives[ch];

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
		else if (ide->type == IDE_ATAPI)
			temp = ide->sc->error;
		else
			temp = ide->error;
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
		if (ide->type == IDE_ATAPI)
			temp = ide->sc->phase;
		else if (ide->type != IDE_NONE)
			temp = ide->secount;
		break;

	case 0x3: /* Sector */
		if (ide->type != IDE_NONE)
			temp = (uint8_t) ide->sector;
		break;

	case 0x4: /* Cylinder low */
		if (ide->type == IDE_NONE)
			temp = 0xFF;
		else if (ide->type == IDE_ATAPI)
			temp = ide->sc->request_length & 0xff;
		else
			temp = ide->cylinder & 0xff;
		break;

	case 0x5: /* Cylinder high */
		if (ide->type == IDE_NONE)
			temp = 0xFF;
		else if (ide->type == IDE_ATAPI)
			temp = ide->sc->request_length >> 8;
		else
			temp = ide->cylinder >> 8;
		break;

	case 0x6: /* Drive/Head */
		temp = (uint8_t)(ide->head | ((ch & 1) ? 0x10 : 0) | (ide->lba ? 0x40 : 0) | 0xa0);
		break;

	/* For ATAPI: Bit 5 is DMA ready, but without overlapped or interlaved DMA, it is
		      DF (drive fault). */
	case 0x7: /* Status */
		ide_irq_lower(ide);
		temp = ide_status(ide, ide_drives[ch ^ 1], ch);
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
    temp = ide_status(ide, ide_drives[ch ^ 1], ch);

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
	case 0x7:
		temp = ide_readb(addr, priv) | 0xff00;
		break;
	default:
		temp = ide_readb(addr, priv) | (ide_readb(addr + 1, priv) << 8);
		break;
    }

    ide_log("ide_readw(%04X, %08X) = %04X\n", addr, priv, temp);
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
		if (dev->bit32)
			temp = temp2 | (ide_read_data(ide, 2) << 16);
		else
			temp = temp2 | (ide_readw(addr + 2, priv) << 16);
		break;
	case 0x6: case 0x7:
		temp = ide_readw(addr, priv) | 0xffff0000;
		break;
	default:
		temp = ide_readw(addr, priv) | (ide_readw(addr + 2, priv) << 16);
		break;
    }

    ide_log("ide_readl(%04X, %08X) = %04X\n", addr, priv, temp);
    return temp;
}


static void
ide_board_callback(void *priv)
{
     ide_board_t *dev = (ide_board_t *) priv;

#ifdef ENABLE_IDE_LOG
     ide_log("CALLBACK RESET\n");
#endif

     dev->ide[0]->atastat = DRDY_STAT | DSC_STAT;
     if (dev->ide[0]->type == IDE_ATAPI)
	dev->ide[0]->sc->status = DRDY_STAT | DSC_STAT;

    dev->ide[1]->atastat = DRDY_STAT | DSC_STAT;
    if (dev->ide[1]->type == IDE_ATAPI)
	dev->ide[1]->sc->status = DRDY_STAT | DSC_STAT;

    dev->cur_dev &= ~1;

    if (dev->diag) {
	dev->diag = 0;
	ide_irq_raise(dev->ide[0]);
    }
}


static void
atapi_error_no_ready(ide_t *ide)
{
    ide->command = 0;
    if (ide->type == IDE_ATAPI) {
	ide->sc->status = ERR_STAT | DSC_STAT;
	ide->sc->error = ABRT_ERR;
	ide->sc->pos = 0;
    } else {
	ide->atastat = ERR_STAT | DSC_STAT;
	ide->error = ABRT_ERR;
	ide->pos = 0;
    }
    ide_irq_raise(ide);
}


static void
ide_callback(void *priv)
{
    int snum, ret = 0;

    ide_t *ide = (ide_t *) priv;

    ide_log("CALLBACK    %02X %i  %i\n", ide->command, ide->reset, ide->channel);

    if (((ide->command >= WIN_RECAL) && (ide->command <= 0x1F)) ||
	((ide->command >= WIN_SEEK) && (ide->command <= 0x7F))) {
	if (ide->type != IDE_HDD) {
		atapi_error_no_ready(ide);
		return;
	}
	if ((ide->command >= WIN_SEEK) && (ide->command <= 0x7F) && !ide->lba) {
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

		if (ide->type == IDE_ATAPI) {
			ide->sc->status = DRDY_STAT | DSC_STAT;
			ide->sc->error = 1;
			if (ide->device_reset)
				ide->device_reset(ide->sc);
		}
		ide_irq_raise(ide);
		if (ide->type == IDE_ATAPI)
			ide->service = 0;
		return;

	case WIN_NOP:
	case WIN_STANDBYNOW1:
	case WIN_IDLENOW1:
	case WIN_SETIDLE1:
		if (ide->type == IDE_ATAPI)
			ide->sc->status = DRDY_STAT | DSC_STAT;
		else
			ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;

	case WIN_CHECKPOWERMODE1:
	case WIN_SLEEP1:
		if (ide->type == IDE_ATAPI) {
			ide->sc->phase = 0xFF;
			ide->sc->status = DRDY_STAT | DSC_STAT;
		}
		ide->secount = 0xFF;
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;

	case WIN_READ:
	case WIN_READ_NORETRY:
		if (ide->type == IDE_ATAPI) {
			ide_set_signature(ide);
			goto abort_cmd;
		}
		if (!ide->lba && (ide->cfg_spt == 0))
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
		if ((ide->type == IDE_ATAPI) || ide_boards[ide->board]->force_ata3 || !ide_bm[ide->board]) {
			ide_log("IDE %i: DMA read aborted (bad device or board)\n", ide->channel);
			goto abort_cmd;
		}
		if (!ide->lba && (ide->cfg_spt == 0)) {
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

		if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board] && ide_bm[ide->board]->dma) {
			/* We should not abort - we should simply wait for the host to start DMA. */
			ret = ide_bm[ide->board]->dma(ide->board,
						      ide->sector_buffer, ide->sector_pos * 512,
						      0, ide_bm[ide->board]->priv);
			if (ret == 2) {
				/* Bus master DMA disabled, simply wait for the host to enable DMA. */
				ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
				ide_set_callback(ide, 6.0 * IDE_TIME);
				return;
			} else if (ret == 1) {
				/*DMA successful*/
				ide_log("IDE %i: DMA read successful\n", ide->channel);

				ide->atastat = DRDY_STAT | DSC_STAT;

				ide_irq_raise(ide);
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
			} else {
				/* Bus master DMAS error, abort the command. */
				ide_log("IDE %i: DMA read aborted (failed)\n", ide->channel);
				goto abort_cmd;
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
		if ((ide->type == IDE_ATAPI) || !ide->blocksize)
			goto abort_cmd;
		if (!ide->lba && (ide->cfg_spt == 0))
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
		if (ide->type == IDE_ATAPI)
			goto abort_cmd;
		if (!ide->lba && (ide->cfg_spt == 0))
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
		if ((ide->type == IDE_ATAPI) || ide_boards[ide->board]->force_ata3 || !ide_bm[ide->board]) {
			ide_log("IDE %i: DMA write aborted (bad device type or board)\n", ide->channel);
			goto abort_cmd;
		}
		if (!ide->lba && (ide->cfg_spt == 0)) {
			ide_log("IDE %i: DMA write aborted (SPECIFY failed)\n", ide->channel);
			goto id_not_found;
		}

		if (!ide_boards[ide->board]->force_ata3 && ide_bm[ide->board] && ide_bm[ide->board]->dma) {
			if (ide->secount)
				ide->sector_pos = ide->secount;
			else
				ide->sector_pos = 256;

			ret = ide_bm[ide->board]->dma(ide->board,
						      ide->sector_buffer, ide->sector_pos * 512,
						      1, ide_bm[ide->board]->priv);

			if (ret == 2) {
				/* Bus master DMA disabled, simply wait for the host to enable DMA. */
				ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
				ide_set_callback(ide, 6.0 * IDE_TIME);
				return;
			} else if (ret == 1) {
				/*DMA successful*/
				ide_log("IDE %i: DMA write successful\n", ide->channel);

				hdd_image_write(ide->hdd_num, ide_get_sector(ide), ide->sector_pos, ide->sector_buffer);

				ide->atastat = DRDY_STAT | DSC_STAT;

				ide_irq_raise(ide);
				ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 0);
			} else {
				/* Bus master DMA error, abort the command. */
				ide_log("IDE %i: DMA read aborted (failed)\n", ide->channel);
				goto abort_cmd;
			}
		} else {
			ide_log("IDE %i: DMA write aborted (no bus master)\n", ide->channel);
			goto abort_cmd;
		}

		return;

	case WIN_WRITE_MULTIPLE:
		if (ide->type == IDE_ATAPI)
			goto abort_cmd;
		if (!ide->lba && (ide->cfg_spt == 0))
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
		if (ide->type == IDE_ATAPI)
			goto abort_cmd;
		if (!ide->lba && (ide->cfg_spt == 0))
			goto id_not_found;
		ide->pos=0;
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
		return;

	case WIN_FORMAT:
		if (ide->type == IDE_ATAPI)
			goto abort_cmd;
		if (!ide->lba && (ide->cfg_spt == 0))
			goto id_not_found;
		hdd_image_zero(ide->hdd_num, ide_get_sector(ide), ide->secount);

		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);

		ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus, 1);
		return;

	case WIN_SPECIFY: /* Initialize Drive Parameters */
		if (ide->type == IDE_ATAPI)
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
		if (ide->type == IDE_ATAPI) {
			ide_identify(ide);
			ide->pos = 0;
			ide->sc->phase = 2;
			ide->sc->pos = 0;
			ide->sc->error = 0;
			ide->sc->status = DRQ_STAT | DRDY_STAT | DSC_STAT;
			ide_irq_raise(ide);
			return;
		}
		goto abort_cmd;

	case WIN_SET_MULTIPLE_MODE:
		if (ide->type == IDE_ATAPI)
			goto abort_cmd;
		ide->blocksize = ide->secount;
		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
		return;

	case WIN_SET_FEATURES:
		if ((ide->type == IDE_NONE) || !ide_set_features(ide))
			goto abort_cmd;

		if (ide->type == IDE_ATAPI) {
			ide->sc->status = DRDY_STAT | DSC_STAT;
			ide->sc->pos = 0;
		}

		ide->atastat = DRDY_STAT | DSC_STAT;
		ide_irq_raise(ide);
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
			ide->pos = 0;
			ide->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
			ide_irq_raise(ide);
		}
		return;

	case WIN_PACKETCMD: /* ATAPI Packet */
		if (ide->type != IDE_ATAPI)
			goto abort_cmd;

		ide_atapi_callback(ide);
		return;

	case 0xFF:
		goto abort_cmd;
    }

abort_cmd:
    ide->command = 0;
    if (ide->type == IDE_ATAPI) {
	ide->sc->status = DRDY_STAT | ERR_STAT | DSC_STAT;
	ide->sc->error = ABRT_ERR;
	ide->sc->pos = 0;
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


uint8_t
ide_read_ali_75(void)
{
    ide_t *ide0, *ide1;
    int ch0, ch1;
    uint8_t ret = 0x00;

    ch0 = ide_boards[0]->cur_dev;
    ch1 = ide_boards[1]->cur_dev;
    ide0 = ide_drives[ch0];
    ide1 = ide_drives[ch1];

    if (ch1)
	ret |= 0x08;
    if (ch0)
	ret |= 0x04;
    if (ide1->irqstat)
	ret |= 0x02;
    if (ide0->irqstat)
	ret |= 0x01;

    return ret;
}


uint8_t
ide_read_ali_76(void)
{
    ide_t *ide0, *ide1;
    int ch0, ch1;
    uint8_t ret = 0x00;

    ch0 = ide_boards[0]->cur_dev;
    ch1 = ide_boards[1]->cur_dev;
    ide0 = ide_drives[ch0];
    ide1 = ide_drives[ch1];

    if (ide1->atastat & BSY_STAT)
	ret |= 0x40;
    if (ide1->atastat & DRQ_STAT)
	ret |= 0x20;
    if (ide1->atastat & ERR_STAT)
	ret |= 0x10;
    if (ide0->atastat & BSY_STAT)
	ret |= 0x04;
    if (ide0->atastat & DRQ_STAT)
	ret |= 0x02;
    if (ide0->atastat & ERR_STAT)
	ret |= 0x01;

    return ret;
}


static void
ide_set_handlers(uint8_t board)
{
    if (ide_boards[board] == NULL)
	return;

    if (ide_boards[board]->base_main) {
	io_sethandler(ide_boards[board]->base_main, 8,
		      ide_readb,           ide_readw,  ide_readl,
		      ide_writeb,          ide_writew, ide_writel,
		      ide_boards[board]);
    }

    if (ide_boards[board]->side_main) {
	io_sethandler(ide_boards[board]->side_main, 1,
		      ide_read_alt_status, NULL,       NULL,
		      ide_write_devctl,    NULL,       NULL,
		      ide_boards[board]);
    }
}


static void
ide_remove_handlers(uint8_t board)
{
    if (ide_boards[board] == NULL)
	return;

    if (ide_boards[board]->base_main) {
	io_removehandler(ide_boards[board]->base_main, 8,
			 ide_readb,           ide_readw,  ide_readl,
			 ide_writeb,          ide_writew, ide_writel,
			 ide_boards[board]);
    }

    if (ide_boards[board]->side_main) {
	io_removehandler(ide_boards[board]->side_main, 1,
			 ide_read_alt_status, NULL,       NULL,
			 ide_write_devctl,    NULL,       NULL,
			 ide_boards[board]);
    }
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
ide_set_base(int board, uint16_t port)
{
    ide_log("ide_set_base(%i, %04X)\n", board, port);

    if (ide_boards[board] == NULL)
	return;

    ide_boards[board]->base_main = port;
}


void
ide_set_side(int board, uint16_t port)
{
    ide_log("ide_set_side(%i, %04X)\n", board, port);

    if (ide_boards[board] == NULL)
	return;

    ide_boards[board]->side_main = port;
}


static void
ide_clear_bus_master(int board)
{
    if (ide_bm[board]) {
	free(ide_bm[board]);
	ide_bm[board] = NULL;
    }
}


/* This so drives can be forced to ATA-3 (no DMA) for machines that hide the on-board PCI IDE controller
   (eg. Packard Bell PB640 and ASUS P/I-P54TP4XE), breaking DMA drivers unless this is done. */
extern void
ide_board_set_force_ata3(int board, int force_ata3)
{
    ide_log("ide_board_set_force_ata3(%i, %i)\n", board, force_ata3);

    if ((ide_boards[board] == NULL)|| !ide_boards[board]->inited)
	return;

    ide_boards[board]->force_ata3 = force_ata3;
}


static void
ide_board_close(int board)
{
    ide_t *dev;
    int c, d;

    ide_log("ide_board_close(%i)\n", board);

    if ((ide_boards[board] == NULL)|| !ide_boards[board]->inited)
	return;

    ide_log("IDE: Closing board %i...\n", board);

    timer_stop(&ide_boards[board]->timer);

    ide_clear_bus_master(board);

    /* Close hard disk image files (if previously open) */
    for (d = 0; d < 2; d++) {
	c = (board << 1) + d;

	ide_boards[board]->ide[d] = NULL;

	dev = ide_drives[c];

	if (dev == NULL)
		continue;

	if ((dev->type == IDE_HDD) && (dev->hdd_num != -1))
		hdd_image_close(dev->hdd_num);

	if (dev->type == IDE_ATAPI)
		dev->sc->status = DRDY_STAT | DSC_STAT;

	if (dev->buffer) {
		free(dev->buffer);
		dev->buffer = NULL;
	}

	if (dev->sector_buffer) {
		free(dev->sector_buffer);
		dev->buffer = NULL;
	}

	if (dev) {
		free(dev);
		ide_drives[c] = NULL;
	}
    }

    free(ide_boards[board]);
    ide_boards[board] = NULL;
}


static void
ide_board_setup(int board)
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
		if (ide_drives[ch]->sector_buffer == NULL)
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
	if (dev->type != IDE_HDD)
		dev->cfg_spt = dev->cfg_hpc = 0;
    }
}


static void
ide_board_init(int board, int irq, int base_main, int side_main, int type)
{
    ide_log("ide_board_init(%i, %i, %04X, %04X, %i)\n", board, irq, base_main, side_main, type);

    if ((ide_boards[board] != NULL) && ide_boards[board]->inited)
	return;

    ide_log("IDE: Initializing board %i...\n", board);

    ide_boards[board] = (ide_board_t *) malloc(sizeof(ide_board_t));
    memset(ide_boards[board], 0, sizeof(ide_board_t));
    ide_boards[board]->irq = irq;
    ide_boards[board]->cur_dev = board << 1;
    if (type & 6)
	ide_boards[board]->bit32 = 1;
    ide_boards[board]->base_main = base_main;
    ide_boards[board]->side_main = side_main;
    ide_set_handlers(board);

    timer_add(&ide_boards[board]->timer, ide_board_callback, ide_boards[board], 0);

    ide_board_setup(board);

    ide_boards[board]->inited = 1;
}


void
ide_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    if (ld)
	return;

    intptr_t board = (intptr_t) priv;

    if (ide_boards[board]->base_main || ide_boards[board]->side_main) {
	ide_remove_handlers(board);
	ide_boards[board]->base_main = ide_boards[board]->side_main = 0;
    }

    ide_boards[board]->irq = -1;

    if (config->activate) {
	ide_boards[board]->base_main = config->io[0].base;
	ide_boards[board]->side_main = config->io[1].base;

	if ((ide_boards[board]->base_main != ISAPNP_IO_DISABLED) && (ide_boards[board]->side_main != ISAPNP_IO_DISABLED))
		ide_set_handlers(board);

	if (config->irq[0].irq != ISAPNP_IRQ_DISABLED)
		ide_boards[board]->irq = config->irq[0].irq;
    }
}


static void *
ide_ter_init(const device_t *info)
{
    /* Don't claim this channel again if it was already claimed. */
    if (ide_boards[2])
	return(NULL);

    int irq;
    if (info->local)
	irq = -2;
    else
	irq = device_get_config_int("irq");

    if (irq < 0) {
	ide_board_init(2, -1, 0, 0, 0);
	if (irq == -1)
		isapnp_add_card(ide_ter_pnp_rom, sizeof(ide_ter_pnp_rom), ide_pnp_config_changed, NULL, NULL, NULL, (void *) 2);
    } else {
	ide_board_init(2, irq, 0x168, 0x36e, 0);
    }

    return(ide_boards[2]);
}


/* Close a standalone IDE unit. */
static void
ide_ter_close(void *priv)
{
    ide_board_close(2);
}


static void *
ide_qua_init(const device_t *info)
{
    /* Don't claim this channel again if it was already claimed. */
    if (ide_boards[3])
	return(NULL);

    int irq;
    if (info->local)
	irq = -2;
    else
	irq = device_get_config_int("irq");

    if (irq < 0) {
	ide_board_init(3, -1, 0, 0, 0);
	if (irq == -1)
		isapnp_add_card(ide_qua_pnp_rom, sizeof(ide_qua_pnp_rom), ide_pnp_config_changed, NULL, NULL, NULL, (void *) 3);
    } else {
	ide_board_init(3, irq, 0x1e8, 0x3ee, 0);
    }

    return(ide_boards[3]);
}


/* Close a standalone IDE unit. */
static void
ide_qua_close(void *priv)
{
    ide_board_close(3);
}


void *
ide_xtide_init(void)
{
    ide_board_init(0, -1, 0, 0, 0);

    return ide_boards[0];
}


void
ide_xtide_close(void)
{
    ide_board_close(0);
}


void
ide_set_bus_master(int board,
		   int (*dma)(int channel, uint8_t *data, int transfer_length, int out, void *priv),
		   void (*set_irq)(int channel, void *priv), void *priv)
{
    if (ide_bm[board] == NULL)
	ide_bm[board] = (ide_bm_t *) malloc(sizeof(ide_bm_t));

    ide_bm[board]->dma = dma;
    ide_bm[board]->set_irq = set_irq;
    ide_bm[board]->priv = priv;
}


static void *
ide_init(const device_t *info)
{
    ide_log("Initializing IDE...\n");

    switch(info->local) {
	case 0:		/* ISA, single-channel */
	case 1:		/* ISA, dual-channel */
	case 2:		/* VLB, single-channel */
	case 3:		/* VLB, dual-channel */
	case 4:		/* PCI, single-channel */
	case 5:		/* PCI, dual-channel */
		ide_board_init(0, 14, 0x1f0, 0x3f6, info->local);

		if (info->local & 1)
			ide_board_init(1, 15, 0x170, 0x376, info->local);
		break;
    }

    return(ide_drives);
}


static void
ide_drive_reset(int d)
{
    ide_log("Resetting IDE drive %i...\n", d);

    ide_drives[d]->channel = d;
    ide_drives[d]->atastat = DRDY_STAT | DSC_STAT;
    ide_drives[d]->service = 0;
    ide_drives[d]->board = d >> 1;
    ide_drives[d]->selected = !(d & 1);
    timer_stop(&ide_drives[d]->timer);

    if (ide_boards[d >> 1]) {
	ide_boards[d >> 1]->cur_dev = d & ~1;
	timer_stop(&ide_boards[d >> 1]->timer);
    }

    ide_set_signature(ide_drives[d]);

    if (ide_drives[d]->sector_buffer)
	memset(ide_drives[d]->sector_buffer, 0, 256*512);

    if (ide_drives[d]->buffer)
	memset(ide_drives[d]->buffer, 0, 65536 * sizeof(uint16_t));
}


static void
ide_board_reset(int board)
{
    int d, min, max;

    ide_log("Resetting IDE board %i...\n", board);

    timer_stop(&ide_boards[board]->timer);

    min = (board << 1);
    max = min + 2;

    for (d = min; d < max; d++)
	ide_drive_reset(d);
}


/* Reset a standalone IDE unit. */
static void
ide_reset(void *p)
{
    ide_log("Resetting IDE...\n");

    if (ide_boards[0] != NULL)
	ide_board_reset(0);

    if (ide_boards[1] != NULL)
	ide_board_reset(1);
}


/* Close a standalone IDE unit. */
static void
ide_close(void *priv)
{
    ide_log("Closing IDE...\n");

    if (ide_boards[0] != NULL) {
	ide_board_close(0);
	ide_boards[0] = NULL;
    }

    if (ide_boards[1] != NULL) {
	ide_board_close(1);
	ide_boards[1] = NULL;
    }
}


const device_t ide_isa_device = {
    .name = "ISA PC/AT IDE Controller",
    .internal_name = "ide_isa",
    .flags = DEVICE_ISA | DEVICE_AT,
    .local = 0,
    .init = ide_init,
    .close = ide_close,
    .reset = ide_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_isa_2ch_device = {
    .name = "ISA PC/AT IDE Controller (Dual-Channel)",
    .internal_name = "ide_isa_2ch",
    .flags = DEVICE_ISA | DEVICE_AT,
    .local = 1,
    .init = ide_init,
    .close = ide_close,
    .reset = ide_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_vlb_device = {
    .name = "VLB IDE Controller",
    .internal_name = "ide_vlb",
    .flags = DEVICE_VLB | DEVICE_AT,
    .local = 2,
    .init = ide_init,
    .close = ide_close,
    .reset = ide_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_vlb_2ch_device = {
    .name = "VLB IDE Controller (Dual-Channel)",
    .internal_name = "ide_vlb_2ch",
    .flags = DEVICE_VLB | DEVICE_AT,
    .local = 3,
    .init = ide_init,
    .close = ide_close,
    .reset = ide_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_pci_device = {
    .name = "PCI IDE Controller",
    .internal_name = "ide_pci",
    .flags = DEVICE_PCI | DEVICE_AT,
    .local = 4,
    .init = ide_init,
    .close = ide_close,
    .reset = ide_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_pci_2ch_device = {
    .name = "PCI IDE Controller (Dual-Channel)",
    .internal_name = "ide_pci_2ch",
    .flags = DEVICE_PCI | DEVICE_AT,
    .local = 5,
    .init = ide_init,
    .close = ide_close,
    .reset = ide_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

// clang-format off
static const device_config_t ide_ter_config[] = {
    {
        "irq", "IRQ", CONFIG_SELECTION, "", 10, "", { 0 },
        {
            { "Plug and Play", -1 },
            { "IRQ 2",          2 },
            { "IRQ 3",          3 },
            { "IRQ 4",          4 },
            { "IRQ 5",          5 },
            { "IRQ 7",          7 },
            { "IRQ 9",          9 },
            { "IRQ 10",        10 },
            { "IRQ 11",        11 },
            { "IRQ 12",        12 },
            { ""                  }
        }
    },
    { "", "", -1 }
};

static const device_config_t ide_qua_config[] = {
    {
        "irq", "IRQ", CONFIG_SELECTION, "", 11, "", { 0 },
        {
            { "Plug and Play", -1 },
            { "IRQ 2",          2 },
            { "IRQ 3",          3 },
            { "IRQ 4",          4 },
            { "IRQ 5",          5 },
            { "IRQ 7",          7 },
            { "IRQ 9",          9 },
            { "IRQ 10",        10 },
            { "IRQ 11",        11 },
            { "IRQ 12",        12 },
            { ""                  }
        }
    },
    { "", "", -1 }
};
// clang-format on

const device_t ide_ter_device = {
    .name = "Tertiary IDE Controller",
    .internal_name = "ide_ter",
    .flags = DEVICE_AT,
    .local = 0,
    .init = ide_ter_init,
    .close = ide_ter_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ide_ter_config
};

const device_t ide_ter_pnp_device = {
    .name = "Tertiary IDE Controller (Plug and Play only)",
    .internal_name = "ide_ter_pnp",
    .flags = DEVICE_AT,
    .local = 1,
    .init = ide_ter_init,
    .close = ide_ter_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ide_qua_device = {
    .name = "Quaternary IDE Controller",
    .internal_name = "ide_qua",
    .flags = DEVICE_AT,
    .local = 0,
    .init = ide_qua_init,
    .close = ide_qua_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ide_qua_config
};

const device_t ide_qua_pnp_device = {
    .name = "Quaternary IDE Controller (Plug and Play only)",
    .internal_name = "ide_qua_pnp",
    .flags = DEVICE_AT,
    .local = 1,
    .init = ide_qua_init,
    .close = ide_qua_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = ide_qua_config
};
