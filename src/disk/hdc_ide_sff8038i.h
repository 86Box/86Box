/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the SFF-8038i IDE Bus Master.
 *
 *		Emulation core dispatcher.
 *
 * Version:	@(#)hdc_ide_sff8038i.h	1.0.0	2019/05/12
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */

typedef struct
{
    uint8_t	command, status,
		ptr0, enabled;
    uint32_t	ptr, ptr_cur,
		addr;
    int		count, eot;
} sff8038i_t;


extern const device_t sff8038i_device;

extern void	sff_bus_master_handlers(sff8038i_t *dev, uint16_t old_base, uint16_t new_base, int enabled);

extern int	sff_bus_master_dma_read(int channel, uint8_t *data, int transfer_length, void *priv);
extern int	sff_bus_master_dma_write(int channel, uint8_t *data, int transfer_length, void *priv);

extern void	sff_bus_master_set_irq(int channel, void *priv);

extern void	sff_bus_master_reset(sff8038i_t *dev, uint16_t old_base);
