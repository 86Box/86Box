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
 * Version:	@(#)piix.h	1.0.1	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */

extern void piix_init(int card);

extern void piix3_init(int card);

extern uint8_t piix_bus_master_read(uint16_t port, void *priv);
extern void piix_bus_master_write(uint16_t port, uint8_t val, void *priv);

extern int piix_bus_master_get_count(int channel);

extern int piix_bus_master_dma_read_ex(int channel, uint8_t *data);
