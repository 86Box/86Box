/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Shared code for all the floppy modules.
 *
 * Version:	@(#)fdd_common.h	1.0.2	2018/03/16
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
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
#ifndef FDD_COMMON_H
# define FDD_COMMON_H


extern const uint8_t	fdd_holes[6];
extern const uint8_t	fdd_rates[6];
extern const double	fdd_bit_rates_300[6];
extern const uint8_t	fdd_max_sectors[8][6];
extern const uint8_t	fdd_dmf_r[21];


extern int 	fdd_get_gap3_size(int rate, int size, int sector);
extern uint8_t	fdd_sector_size_code(int size);
extern int	fdd_sector_code_size(uint8_t code);
extern int	fdd_bps_valid(uint16_t bps);
extern int	fdd_interleave(int sector, int skew, int spt);


#endif	/*FDD_COMMON_H*/
