/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the ISA Bus (de)Bugger expansion card
 *		sold as a DIY kit in the late 1980's in The Netherlands.
 *		This card was a assemble-yourself 8bit ISA addon card for
 *		PC and AT systems that had several tools to aid in low-
 *		level debugging (mostly for faulty BIOSes, bootloaders
 *		and system kernels...)
 *
 *		Definitions for the Distributed DMA emulation.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Miran Grca.
 */
#ifndef DDMA_H
# define DDMA_H


#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t		io_base;
    int			channel, enable;
} ddma_channel_t;

typedef struct
{
    ddma_channel_t	channels[8];
} ddma_t;


/* Global variables. */
extern const device_t	ddma_device;


/* Functions. */
extern void		ddma_update_io_mapping(ddma_t *dev, int ch, uint8_t base_l, uint8_t base_h, int enable);

#ifdef __cplusplus
}
#endif


#endif	/*DDMA_H*/
