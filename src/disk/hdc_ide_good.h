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
 * Version:	@(#)hdd_ide.h	1.0.9	2018/03/26
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#ifndef EMU_IDE_H
# define EMU_IDE_H


typedef struct {
	uint8_t atastat, error,
		command, fdisk;
	int type, board,
	    irqstat, service,
	    blocksize, blockcount,
	    hdd_num, channel,
	    pos, sector_pos,
	    lba, skip512,
	    reset, specify_success,
	    mdma_mode, do_initial_read,
	    spt, hpc,
	    tracks;
	uint32_t secount, sector,
		 cylinder, head,
		 drive, cylprecomp,
		 t_spt, t_hpc,
		 lba_addr;

	uint16_t *buffer;
	uint8_t *sector_buffer;
} ide_t;


extern int ideboard;
extern int ide_ter_enabled, ide_qua_enabled;

extern ide_t *ide_drives[IDE_NUM];
extern int64_t idecallback[5];


extern void	ide_irq_raise(ide_t *ide);
extern void	ide_irq_lower(ide_t *ide);

extern void *	ide_xtide_init(void);
extern void	ide_xtide_close(void);

extern void	ide_writew(uint16_t addr, uint16_t val, void *priv);
extern void	ide_write_devctl(uint16_t addr, uint8_t val, void *priv);
extern void	ide_writeb(uint16_t addr, uint8_t val, void *priv);
extern uint8_t	ide_readb(uint16_t addr, void *priv);
extern uint8_t	ide_read_alt_status(uint16_t addr, void *priv);
extern uint16_t	ide_readw(uint16_t addr, void *priv);

extern void	ide_set_bus_master(int (*read)(int channel, uint8_t *data, int transfer_length, void *priv),
				   int (*write)(int channel, uint8_t *data, int transfer_length, void *priv),
				   void (*set_irq)(int channel, void *priv),
				   void *priv0, void *priv1);

extern void	win_cdrom_eject(uint8_t id);
extern void	win_cdrom_reload(uint8_t id);

extern void	ide_set_base(int controller, uint16_t port);
extern void	ide_set_side(int controller, uint16_t port);

extern void	ide_pri_enable(void);
extern void	ide_pri_disable(void);
extern void	ide_sec_enable(void);
extern void	ide_sec_disable(void);

extern void	ide_set_callback(uint8_t channel, int64_t callback);
extern void	secondary_ide_check(void);

extern void	ide_padstr8(uint8_t *buf, int buf_size, const char *src);

extern int	(*ide_bus_master_read)(int channel, uint8_t *data, int transfer_length, void *priv);
extern int	(*ide_bus_master_write)(int channel, uint8_t *data, int transfer_length, void *priv);
extern void	(*ide_bus_master_set_irq)(int channel, void *priv);
extern void	*ide_bus_master_priv[2];


#endif	/*EMU_IDE_H*/
