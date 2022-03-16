/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the Intel DMA controller.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2020 Fred N. van Kempen.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2008-2020 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef EMU_DMA_H
# define EMU_DMA_H


#define DMA_NODATA	-1
#define DMA_OVER	0x10000
#define DMA_VERIFY	0x20000


typedef struct {
    uint8_t	m, mode, page, stat,
		stat_rq, command,
		ps2_mode, arb_level,
		sg_command, sg_status,
		ptr0, enabled,
		ext_mode, page_l,
		page_h, pad;
    uint16_t	cb, io_addr,
		base, transfer_mode;
    uint32_t	ptr, ptr_cur,
		addr,
		ab, ac;
    int		cc, wp,
		size, count,
		eot;
} dma_t;


extern dma_t	dma[8];
extern uint8_t	dma_e;
extern uint8_t	dma_m;


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

extern int	dma_get_drq(int channel);
extern void	dma_set_drq(int channel, int set);

extern int	dma_channel_read(int channel);
extern int	dma_channel_write(int channel, uint16_t val);

extern void	dma_alias_set(void);
extern void	dma_alias_set_piix(void);
extern void	dma_alias_remove(void);
extern void	dma_alias_remove_piix(void);

extern void	dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize);
extern void	dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize);

void		dma_set_params(uint8_t advanced, uint32_t mask);
void		dma_set_mask(uint32_t mask);

void		dma_set_at(uint8_t at);

void		dma_ext_mode_init(void);
void		dma_high_page_init(void);

void		dma_remove_sg(void);
void		dma_set_sg_base(uint8_t sg_base);


#endif	/*EMU_DMA_H*/
