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
 * Version:	@(#)hdd_ide.h	1.0.4	2017/09/30
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_IDE_H
# define EMU_IDE_H


typedef struct {
	int type;
	int board;
	uint8_t atastat;
	uint8_t error;
	int secount,sector,cylinder,head,drive,cylprecomp;
	uint8_t command;
	uint8_t fdisk;
	int pos;
	int packlen;
	int spt,hpc;
	int tracks;
	int packetstatus;
	uint8_t asc;
	int reset;
	uint16_t buffer[65536];
	int irqstat;
	int service;
	int lba;
	int channel;
	uint32_t lba_addr;
	int skip512;
	int blocksize, blockcount;
	uint16_t dma_identify_data[3];
	int hdi,base;
	int hdd_num;
	uint8_t specify_success;
	int mdma_mode;
	uint8_t sector_buffer[256*512];
	int do_initial_read;
	int sector_pos;
} IDE;


extern int ideboard;

extern int ide_enable[5];
extern int ide_irq[5];

extern IDE ide_drives[IDE_NUM + XTIDE_NUM];
extern int idecallback[5];


extern void	ide_irq_raise(IDE *ide);
extern void	ide_irq_lower(IDE *ide);

extern void	writeide(int ide_board, uint16_t addr, uint8_t val);
extern void	writeidew(int ide_board, uint16_t val);
extern uint8_t	readide(int ide_board, uint16_t addr);
extern uint16_t	readidew(int ide_board);
extern void	callbackide(int ide_board);

extern void	ide_set_bus_master(int (*read)(int channel, uint8_t *data, int transfer_length), int (*write)(int channel, uint8_t *data, int transfer_length), void (*set_irq)(int channel));

extern void	win_cdrom_eject(uint8_t id);
extern void	win_cdrom_reload(uint8_t id);

extern void	ide_set_base(int controller, uint16_t port);
extern void	ide_set_side(int controller, uint16_t port);

extern void	ide_init_first(void);
extern void	ide_init(void);
extern void	ide_reset(void);

extern void	ide_xtide_init(void);

extern void	ide_pri_enable(void);
extern void	ide_pri_enable_ex(void);
extern void	ide_pri_disable(void);
extern void	ide_sec_enable(void);
extern void	ide_sec_disable(void);
extern void	ide_ter_enable(void);
extern void	ide_ter_disable(void);
extern void	ide_ter_init(void);
extern void	ide_qua_enable(void);
extern void	ide_qua_disable(void);
extern void	ide_qua_init(void);

extern void	secondary_ide_check(void);

extern void	ide_padstr8(uint8_t *buf, int buf_size, const char *src);


#endif	/*EMU_IDE_H*/
