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
 * Version:	@(#)dma.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

void dma_init(void);
void dma16_init(void);
void ps2_dma_init(void);
void dma_reset(void);
int dma_mode(int channel);

#define DMA_NODATA -1
#define DMA_OVER 0x10000
#define DMA_VERIFY 0x20000

void readdma0(void);
int readdma1(void);
uint8_t readdma2(void);
int readdma3(void);

void writedma2(uint8_t temp);

int dma_channel_read(int channel);
int dma_channel_write(int channel, uint16_t val);

void dma_alias_set(void);
void dma_alias_remove(void);
void dma_alias_remove_piix(void);

void DMAPageRead(uint32_t PhysAddress, char *DataRead, uint32_t TotalSize);
void DMAPageWrite(uint32_t PhysAddress, const char *DataWrite, uint32_t TotalSize);
