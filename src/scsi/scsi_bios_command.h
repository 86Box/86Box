/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The shared AHA and Buslogic SCSI BIOS command handler's
 *		headler.
 *
 * Version:	@(#)scsi_bios_command.h	1.0.0	2017/08/26
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#pragma pack(push,1)
typedef struct
{
	uint8_t	command;
	uint8_t	lun:3,
		reserved:2,
		id:3;
	union {
	    struct {
		uint16_t cyl;
		uint8_t	head;
		uint8_t	sec;
	    } chs;
	    struct {
		uint8_t lba0;	/* MSB */
		uint8_t lba1;
		uint8_t lba2;
		uint8_t lba3;	/* LSB */
	    } lba;
	}	u;
	uint8_t	secount;
	addr24	dma_address;
} BIOSCMD;
#pragma pack(pop)
#define lba32_blk(p)	((uint32_t)(p->u.lba.lba0<<24) | (p->u.lba.lba1<<16) | \
				   (p->u.lba.lba2<<8) | p->u.lba.lba3)

extern uint8_t scsi_bios_command(uint8_t last_id, BIOSCMD *BiosCmd, int8_t islba);
