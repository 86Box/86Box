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
 * Version:	@(#)hdc_ide_sff8038i.h	1.0.1	2020/01/14
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 */

typedef struct
{
    uint8_t	command, status,
		ptr0, enabled;
    uint32_t	ptr, ptr_cur,
		addr;
    int		count, eot,
		slot,
		irq_mode, irq_pin;
} sff8038i_t;


extern const device_t sff8038i_device;

extern void	sff_bus_master_handlers(sff8038i_t *dev, uint16_t old_base, uint16_t new_base, int enabled);

extern int	sff_bus_master_dma_read(int channel, uint8_t *data, int transfer_length, void *priv);
extern int	sff_bus_master_dma_write(int channel, uint8_t *data, int transfer_length, void *priv);

extern void	sff_bus_master_set_irq(int channel, void *priv);

extern void	sff_bus_master_reset(sff8038i_t *dev, uint16_t old_base);

extern void	sff_set_slot(sff8038i_t *dev, int slot);

extern void	sff_set_irq_mode(sff8038i_t *dev, int irq_mode);
extern void	sff_set_irq_pin(sff8038i_t *dev, int irq_pin);
