/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Driver for the ESDI controller (WD1007-vse1) for PC/AT.
 *
 * Version:	@(#)hdc_esdi_at.c	1.0.1	2017/09/29
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../ibm.h"
#include "../device.h"
#include "../io.h"
#include "../mem.h"
#include "../pic.h"
#include "../rom.h"
#include "../timer.h"
#include "hdc.h"
#include "hdd.h"


#define HDC_TIME		(TIMER_USEC*10)
#define BIOS_FILE		L"roms/hdd/esdi_at/62-000279-061.bin"

#define STAT_ERR		0x01
#define STAT_INDEX		0x02
#define STAT_CORRECTED_DATA	0x04
#define STAT_DRQ		0x08 /* Data request */
#define STAT_DSC                0x10
#define STAT_SEEK_COMPLETE      0x20
#define STAT_READY		0x40
#define STAT_BUSY		0x80

#define ERR_DAM_NOT_FOUND       0x01 /*Data Address Mark not found*/
#define ERR_TR000               0x02 /*Track 0 not found*/
#define ERR_ABRT		0x04 /*Command aborted*/
#define ERR_ID_NOT_FOUND	0x10 /*ID not found*/
#define ERR_DATA_CRC	        0x40 /*Data CRC error*/
#define ERR_BAD_BLOCK	        0x80 /*Bad Block detected*/

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


extern char ide_fn[4][512];


typedef struct {
    int cfg_spt;
    int cfg_hpc;
    int current_cylinder;
    int real_spt;
    int real_hpc;
    int real_tracks;
    int present;
    int hdd_num;
} drive_t;

typedef struct {
    uint8_t status;
    uint8_t error;
    int secount,sector,cylinder,head,cylprecomp;
    uint8_t command;
    uint8_t fdisk;
    int pos;
        
    int drive_sel;
    int reset;
    uint16_t buffer[256];
    int irqstat;

    int callback;

    drive_t drives[2];
        
    rom_t bios_rom;
} esdi_t;


static inline void
irq_raise(esdi_t *esdi)
{
    if (! (esdi->fdisk&2))
	picint(1<<14);

    esdi->irqstat=1;
}


static inline void
irq_lower(esdi_t *esdi)
{
    picintc(1<<14);
}


static void
irq_update(esdi_t *esdi)
{
    if (esdi->irqstat && !((pic2.pend|pic2.ins)&0x40) && !(esdi->fdisk & 2))
	picint(1<<14);
}


/* Return the sector offset for the current register values. */
static int
get_sector(esdi_t *esdi, off64_t *addr)
{
    drive_t *drive = &esdi->drives[esdi->drive_sel];
    int heads = drive->cfg_hpc;
    int sectors = drive->cfg_spt;

    if (esdi->head > heads) {
	pclog("esdi_get_sector: past end of configured heads\n");
	return 1;
    }

    if (esdi->sector >= sectors+1) {
	pclog("esdi_get_sector: past end of configured sectors\n");
	return 1;
    }

    if (drive->cfg_spt==drive->real_spt && drive->cfg_hpc==drive->real_hpc) {
	*addr = ((((off64_t) esdi->cylinder * heads) + esdi->head) *
					sectors) + (esdi->sector - 1);
    } else {
	/*
	 * When performing translation, the firmware seems to leave 1
	 * sector per track inaccessible (spare sector)
	 */
	int c, h, s;

	*addr = ((((off64_t) esdi->cylinder * heads) + esdi->head) *
					sectors) + (esdi->sector - 1);

	s = *addr % (drive->real_spt - 1);
	h = (*addr / (drive->real_spt - 1)) % drive->real_hpc;
	c = (*addr / (drive->real_spt - 1)) / drive->real_hpc;

	*addr = ((((off64_t)c * drive->real_hpc) + h) * drive->real_spt) + s;
    }
        
    return(0);
}


/* Move to the next sector using CHS addressing. */
static void
next_sector(esdi_t *esdi)
{
    drive_t *drive = &esdi->drives[esdi->drive_sel];
        
    esdi->sector++;
    if (esdi->sector == (drive->cfg_spt + 1)) {
	esdi->sector = 1;
	if (++esdi->head == drive->cfg_hpc) {
		esdi->head = 0;
		esdi->cylinder++;
		if (drive->current_cylinder < drive->real_tracks)
			drive->current_cylinder++;
	}
    }
}


static void
esdi_writew(uint16_t port, uint16_t val, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;
	
    esdi->buffer[esdi->pos >> 1] = val;
    esdi->pos += 2;

    if (esdi->pos >= 512) {
	esdi->pos = 0;
	esdi->status = STAT_BUSY;
	timer_process();
      	esdi->callback = 6*HDC_TIME;
	timer_update_outstanding();
    }
}


static void
esdi_write(uint16_t port, uint8_t val, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;

    switch (port) {
	case 0x1F0: /* Data */
		esdi_writew(port, val | (val << 8), priv);
		return;

	case 0x1F1: /* Write precompenstation */
		esdi->cylprecomp = val;
		return;

	case 0x1F2: /* Sector count */
		esdi->secount = val;
		return;

	case 0x1F3: /* Sector */
		esdi->sector = val;
		return;

	case 0x1F4: /* Cylinder low */
		esdi->cylinder = (esdi->cylinder & 0xFF00) | val;
		return;

	case 0x1F5: /* Cylinder high */
		esdi->cylinder = (esdi->cylinder & 0xFF) | (val << 8);
		return;

	case 0x1F6: /* Drive/Head */
		esdi->head = val & 0xF;
		esdi->drive_sel = (val & 0x10) ? 1 : 0;
		if (esdi->drives[esdi->drive_sel].present)
			esdi->status = 0;
		  else
			esdi->status = STAT_READY | STAT_DSC;
		return;

	case 0x1F7: /* Command register */
		irq_lower(esdi);
		esdi->command = val;
		esdi->error = 0;
		
		switch (val & 0xf0) {
			case CMD_RESTORE:
				esdi->command &= ~0x0f; /*Mask off step rate*/
				esdi->status = STAT_BUSY;
				timer_process();
				esdi->callback = 200*HDC_TIME;
				timer_update_outstanding();
				break;

			case CMD_SEEK:
				esdi->command &= ~0x0f; /*Mask off step rate*/
				esdi->status = STAT_BUSY;
				timer_process();
				esdi->callback = 200*HDC_TIME;
				timer_update_outstanding();
				break;

			default:
				switch (val) {
					case CMD_NOP:
						esdi->status = STAT_BUSY;
						timer_process();
						esdi->callback = 200*HDC_TIME;
						timer_update_outstanding();
						break;
				
					case CMD_READ:
					case CMD_READ+1:
					case CMD_READ+2:
					case CMD_READ+3:
						esdi->command &= ~3;
						if (val & 2)
							fatal("Read with ECC\n");

					case 0xa0:
						esdi->status = STAT_BUSY;
						timer_process();
						esdi->callback = 200*HDC_TIME;
						timer_update_outstanding();
						break;

					case CMD_WRITE:
					case CMD_WRITE+1:
					case CMD_WRITE+2:
					case CMD_WRITE+3:
						esdi->command &= ~3;
						if (val & 2)
							fatal("Write with ECC\n");
						esdi->status = STAT_DRQ | STAT_DSC;
						esdi->pos=0;
						break;

					case CMD_VERIFY:
					case CMD_VERIFY+1:
						esdi->command &= ~1;
						esdi->status = STAT_BUSY;
						timer_process();
						esdi->callback = 200*HDC_TIME;
						timer_update_outstanding();
						break;

					case CMD_FORMAT:
						esdi->status = STAT_DRQ;
						esdi->pos=0;
						break;

					case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
						esdi->status = STAT_BUSY;
						timer_process();
						esdi->callback = 30*HDC_TIME;
						timer_update_outstanding();
						break;

					case CMD_DIAGNOSE: /* Execute Drive Diagnostics */
						esdi->status = STAT_BUSY;
						timer_process();
						esdi->callback = 200*HDC_TIME;
						timer_update_outstanding();
						break;

					case 0xe0: /*???*/
					case CMD_READ_PARAMETERS:
						esdi->status = STAT_BUSY;
						timer_process();
						esdi->callback = 200*HDC_TIME;
						timer_update_outstanding();
						break;

					default:
						pclog("Bad esdi command %02X\n", val);
					case 0xe8: /*???*/
						esdi->status = STAT_BUSY;
						timer_process();
						esdi->callback = 200*HDC_TIME;
						timer_update_outstanding();
						break;
				}
		}		
		break;

	case 0x3F6: /* Device control */
		if ((esdi->fdisk & 4) && !(val & 4)) {
			timer_process();
			esdi->callback = 500*HDC_TIME;
			timer_update_outstanding();
			esdi->reset = 1;
			esdi->status = STAT_BUSY;
		}

		if (val & 4) {
			/*Drive held in reset*/
			timer_process();
			esdi->callback = 0;
			timer_update_outstanding();
			esdi->status = STAT_BUSY;
		}
		esdi->fdisk = val;
		irq_update(esdi);
	}
}


static uint16_t
esdi_readw(uint16_t port, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;
    uint16_t temp;

    temp = esdi->buffer[esdi->pos >> 1];
    esdi->pos += 2;

    if (esdi->pos >= 512) {
	esdi->pos=0;
	esdi->status = STAT_READY | STAT_DSC;
	if (esdi->command == CMD_READ || esdi->command == 0xa0) {
		esdi->secount = (esdi->secount - 1) & 0xff;
		if (esdi->secount) {
			next_sector(esdi);
			esdi->status = STAT_BUSY;
			timer_process();
			esdi->callback = 6*HDC_TIME;
			timer_update_outstanding();
		}
	}
    }

    return(temp);
}


static uint8_t
esdi_read(uint16_t port, void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;
    uint8_t temp = 0xff;

    switch (port) {
	case 0x1F0: /* Data */
		temp = esdi_readw(port, esdi) & 0xff;
		break;

	case 0x1F1: /* Error */
		temp = esdi->error;
		break;

	case 0x1F2: /* Sector count */
		temp = (uint8_t)esdi->secount;
		break;

	case 0x1F3: /* Sector */
		temp = (uint8_t)esdi->sector;
		break;

	case 0x1F4: /* Cylinder low */
		temp = (uint8_t)(esdi->cylinder&0xFF);
		break;

	case 0x1F5: /* Cylinder high */
		temp = (uint8_t)(esdi->cylinder>>8);
		break;

	case 0x1F6: /* Drive/Head */
		temp = (uint8_t)(esdi->head | (esdi->drive_sel?0x10:0) | 0xa0);
		break;

	case 0x1F7: /* Status */
		irq_lower(esdi);
		temp = esdi->status;
		break;
    }

    return(temp);
}


static void
esdi_callback(void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;
    drive_t *drive = &esdi->drives[esdi->drive_sel];
    off64_t addr;
	
    esdi->callback = 0;
    if (esdi->reset) {
	esdi->status = STAT_READY | STAT_DSC;
	esdi->error = 1;
	esdi->secount = 1;
	esdi->sector = 1;
	esdi->head = 0;
	esdi->cylinder = 0;
	esdi->reset = 0;

	return;
    }

    switch (esdi->command) {
	case CMD_RESTORE:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
		} else {
			drive->current_cylinder = 0;
			esdi->status = STAT_READY | STAT_DSC;
		}
		irq_raise(esdi);
		break;

	case CMD_SEEK:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
		} else {
			esdi->status = STAT_READY | STAT_DSC;
		}
		irq_raise(esdi);
		break;

	case CMD_READ:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
			irq_raise(esdi);
			break;
		}

		if (get_sector(esdi, &addr)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}

		if (hdd_image_read_ex(drive->hdd_num, addr, 1,
				      (uint8_t *)esdi->buffer)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}			

		esdi->pos = 0;
		esdi->status = STAT_DRQ | STAT_READY | STAT_DSC;
		irq_raise(esdi);
		update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 1);
		break;

	case CMD_WRITE:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
			irq_raise(esdi);
			break;
		}

		if (get_sector(esdi, &addr)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}

		if (hdd_image_write_ex(drive->hdd_num, addr, 1,
				       (uint8_t *)esdi->buffer)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}			

		irq_raise(esdi);
		esdi->secount = (esdi->secount - 1) & 0xff;
		if (esdi->secount) {
			esdi->status = STAT_DRQ | STAT_READY | STAT_DSC;
			esdi->pos = 0;
			next_sector(esdi);
		} else {
			esdi->status = STAT_READY | STAT_DSC;
		}
		update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 1);
		break;
		
	case CMD_VERIFY:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
			irq_raise(esdi);
			break;
		}

		if (get_sector(esdi, &addr)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}

		if (hdd_image_read_ex(drive->hdd_num, addr, 1,
				      (uint8_t *)esdi->buffer)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}

		update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 1);
		next_sector(esdi);
		esdi->secount = (esdi->secount - 1) & 0xff;
		if (esdi->secount)
			esdi->callback = 6*HDC_TIME;
		else {
			esdi->pos = 0;
			esdi->status = STAT_READY | STAT_DSC;
			irq_raise(esdi);
		}
		break;

	case CMD_FORMAT:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
			irq_raise(esdi);
			break;
		}

		if (get_sector(esdi, &addr)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}
		if (hdd_image_zero_ex(drive->hdd_num, addr, esdi->secount)) {
			esdi->error = ERR_ID_NOT_FOUND;
			esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
			irq_raise(esdi);
			break;
		}			
		esdi->status = STAT_READY | STAT_DSC;
		irq_raise(esdi);
		update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 1);
		break;

	case CMD_DIAGNOSE:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
		} else {
			esdi->error = 1; /*No error detected*/
			esdi->status = STAT_READY | STAT_DSC;
		}
		irq_raise(esdi);
		break;

	case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
			irq_raise(esdi);
			break;
		}

		drive->cfg_spt = esdi->secount;
		drive->cfg_hpc = esdi->head+1;
		pclog("Parameters: spt=%i hpc=%i\n", drive->cfg_spt,drive->cfg_hpc);
		if (! esdi->secount)
			fatal("secount=0\n");
		esdi->status = STAT_READY | STAT_DSC;
		irq_raise(esdi);
		break;
		
	case CMD_NOP:
		esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
		esdi->error = ERR_ABRT;
		irq_raise(esdi);
		break;
		
	case 0xe0:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
			irq_raise(esdi);
			break;
		}

		switch (esdi->cylinder >> 8) {
			case 0x31:
				esdi->cylinder = drive->real_tracks;
				break;

			case 0x33:
				esdi->cylinder = drive->real_hpc;
				break;

			case 0x35:
				esdi->cylinder = 0x200;
				break;

			case 0x36:
				esdi->cylinder = drive->real_spt;
				break;

			default:
				pclog("EDSI Bad read config %02x\n",
						esdi->cylinder >> 8);
		}
		esdi->status = STAT_READY | STAT_DSC;
		irq_raise(esdi);
		break;

	case 0xa0:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
		} else {
			memset(esdi->buffer, 0, 512);
			memset(&esdi->buffer[3], 0xff, 512-6);
			esdi->pos = 0;
			esdi->status = STAT_DRQ | STAT_READY | STAT_DSC;
		}
		irq_raise(esdi);
		break;
		
	case CMD_READ_PARAMETERS:
		if (! drive->present) {
			esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
			esdi->error = ERR_ABRT;
			irq_raise(esdi);
			break;
		}

		memset(esdi->buffer, 0, 512);
		esdi->buffer[0] = 0x44;		      /* general configuration */
		esdi->buffer[1] = drive->real_tracks; /* number of non-removable cylinders */
		esdi->buffer[2] = 0;		      /* number of removable cylinders */
		esdi->buffer[3] = drive->real_hpc;    /* number of heads */
		esdi->buffer[4] = 600; /* number of unformatted bytes/track */
		esdi->buffer[5] = esdi->buffer[4] * drive->real_spt; /* number of unformatted bytes/sector */
		esdi->buffer[6] = drive->real_spt; /* number of sectors */
		esdi->buffer[7] = 0; /*minimum bytes in inter-sector gap*/
		esdi->buffer[8] = 0; /* minimum bytes in postamble */
		esdi->buffer[9] = 0; /* number of words of vendor status */
		/* controller info */
		esdi->buffer[20] = 2; 	/* controller type */
		esdi->buffer[21] = 1; /* sector buffer size, in sectors */
		esdi->buffer[22] = 0; /* ecc bytes appended */
		esdi->buffer[27] = 'W' | ('D' << 8);
		esdi->buffer[28] = '1' | ('0' << 8);
		esdi->buffer[29] = '0' | ('7' << 8);
		esdi->buffer[30] = 'V' | ('-' << 8);
		esdi->buffer[31] = 'S' | ('E' << 8);
		esdi->buffer[32] = '1';
		esdi->buffer[47] = 0; /* sectors per interrupt */
		esdi->buffer[48] = 0;/* can use double word read/write? */
		esdi->pos = 0;
		esdi->status = STAT_DRQ | STAT_READY | STAT_DSC;
		irq_raise(esdi);
		break;
	
	default:
		pclog("ESDI Callback on unknown command %02x\n", esdi->command);
	case 0xe8:
		esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
		esdi->error = ERR_ABRT;
		irq_raise(esdi);
		break;
    }

	update_status_bar_icon(SB_HDD | HDD_BUS_ESDI, 0);
}


static void
esdi_rom_write(uint32_t addr, uint8_t val, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    addr &= rom->mask;
    if (addr >= 0x1f00 && addr < 0x2000)
	rom->rom[addr] = val;
}


static void
loadhd(esdi_t *esdi, int hdd_num, int d, const wchar_t *fn)
{
    drive_t *drive = &esdi->drives[d];

    if (! hdd_image_load(hdd_num)) {
	drive->present = 0;
	return;
    }

    drive->cfg_spt = drive->real_spt = hdd[hdd_num].spt;
    drive->cfg_hpc = drive->real_hpc = hdd[hdd_num].hpc;
    drive->real_tracks = hdd[hdd_num].tracks;
    drive->hdd_num = hdd_num;
    drive->present = 1;
}


static void *
wd1007vse1_init(void)
{
    int i, c = 0;

    esdi_t *esdi = malloc(sizeof(esdi_t));
    memset(esdi, 0x00, sizeof(esdi_t));

    esdi->drives[0].present = esdi->drives[1].present = 0;

    for (i=0; i<HDD_NUM; i++) {
	if ((hdd[i].bus==HDD_BUS_ESDI) && (hdd[i].esdi_channel<ESDI_NUM)) {
		loadhd(esdi, i, hdd[i].esdi_channel, hdd[i].fn);

		if (++c >= ESDI_NUM) break;
	}
    }

    esdi->status = STAT_READY | STAT_DSC;
    esdi->error = 1;

    rom_init(&esdi->bios_rom,
	     BIOS_FILE, 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    mem_mapping_set_handler(
		&esdi->bios_rom.mapping,
		rom_read, rom_readw, rom_readl,
		esdi_rom_write, NULL, NULL);

    io_sethandler(0x01f0, 1,
		  esdi_read, esdi_readw, NULL,
		  esdi_write, esdi_writew, NULL, esdi);
    io_sethandler(0x01f1, 7,
		  esdi_read, NULL, NULL,
		  esdi_write, NULL, NULL, esdi);
    io_sethandler(0x03f6, 1, NULL, NULL, NULL,
		  esdi_write, NULL, NULL, esdi);

    timer_add(esdi_callback, &esdi->callback, &esdi->callback, esdi);	
	
    return(esdi);
}


static void
wd1007vse1_close(void *priv)
{
    esdi_t *esdi = (esdi_t *)priv;
    drive_t *drive;
    int d;

    esdi->drives[0].present = esdi->drives[1].present = 0;

    for (d=0; d<2; d++) {
	drive = &esdi->drives[d];

	hdd_image_close(drive->hdd_num);
    }

    free(esdi);
}


static int
wd1007vse1_available(void)
{
    return(rom_present(BIOS_FILE));
}


device_t esdi_at_wd1007vse1_device = {
    "Western Digital WD1007V-SE1 (ESDI)",
    DEVICE_AT,
    wd1007vse1_init,
    wd1007vse1_close,
    wd1007vse1_available,
    NULL, NULL, NULL, NULL
};
