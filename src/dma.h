/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel DMA controllers.
 *
 * Version:	@(#)dma.h	1.0.2	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_DMA_H
# define EMU_DMA_H


#define DMA_NODATA -1
#define DMA_OVER 0x10000
#define DMA_VERIFY 0x20000


extern void	dma_init(void);
extern void	dma16_init(void);
extern void	ps2_dma_init(void);
extern void	dma_reset(void);
extern int	dma_mode(int channel);

extern void	readdma0(void);
extern int	readdma1(void);
extern uint8_t	readdma2(void);
extern int	readdma3(void);

extern void	writedma2(uint8_t temp);

extern int	dma_channel_read(int channel);
extern int	dma_channel_write(int channel, uint16_t val);

extern void	dma_alias_set(void);
extern void	dma_alias_remove(void);
extern void	dma_alias_remove_piix(void);

extern void	DMAPageRead(uint32_t PhysAddress, char *DataRead,
			    uint32_t TotalSize);
extern void	DMAPageWrite(uint32_t PhysAddress, const char *DataWrite,
			     uint32_t TotalSize);


#endif	/*EMU_DMA_H*/
