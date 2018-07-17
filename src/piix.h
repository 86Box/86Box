/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the Intel PIIX and PIIX3 Xcelerators.
 *
 *		Emulation core dispatcher.
 *
 * Version:	@(#)piix.h	1.0.3	2018/05/11
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */

extern const device_t piix_device;
extern const device_t piix_pb640_device;
extern const device_t piix3_device;

extern int	piix_bus_master_dma_read(int channel, uint8_t *data, int transfer_length, void *priv);
extern int	piix_bus_master_dma_write(int channel, uint8_t *data, int transfer_length, void *priv);

extern void	piix_bus_master_set_irq(int channel, void *priv);
