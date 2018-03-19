/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the 86F floppy image format.
 *
 * Version:	@(#)floppy_86f.h	1.0.4	2018/03/17
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Fred N. van Kempen.
 *		Copyright 2016-2018 Miran Grca.
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
#ifndef EMU_FLOPPY_86F_H
# define EMU_FLOPPY_86F_H


#define D86FVER		0x020B


extern void	d86f_init(void);
extern void	d86f_load(int drive, wchar_t *fn);
extern void	d86f_close(int drive);
extern void	d86f_seek(int drive, int track);
extern int	d86f_hole(int drive);
extern double	d86f_byteperiod(int drive);
extern void	d86f_stop(int drive);
extern void	d86f_poll(int drive);
extern int	d86f_realtrack(int track, int drive);
extern void	d86f_reset(int drive, int side);
extern void	d86f_readsector(int drive, int sector, int track, int side, int density, int sector_size);
extern void	d86f_writesector(int drive, int sector, int track, int side, int density, int sector_size);
extern void	d86f_comparesector(int drive, int sector, int track, int side, int rate, int sector_size);
extern void	d86f_readaddress(int drive, int side, int density);
extern void	d86f_format(int drive, int side, int density, uint8_t fill);

extern void	d86f_prepare_track_layout(int drive, int side);
extern void	d86f_set_version(int drive, uint16_t version);
extern uint16_t	d86f_side_flags(int drive);
extern uint16_t	d86f_track_flags(int drive);
extern void	d86f_initialize_last_sector_id(int drive, int c, int h,
					       int r, int n);
extern void	d86f_initialize_linked_lists(int drive);
extern void	d86f_destroy_linked_lists(int drive, int side);

#define length_gap0	80
#define length_gap1	50
#define length_sync	12
#define length_am	4
#define length_crc	2

#define IBM
#define MFM
#ifdef IBM
#define pre_gap1	length_gap0 + length_sync + length_am
#else
#define pre_gap1	0
#endif
  
#define pre_track	pre_gap1 + length_gap1
#define pre_gap		length_sync + length_am + 4 + length_crc
#define pre_data	length_sync + length_am
#define post_gap	length_crc


#endif	/*EMU_FLOPPY_86F_H*/
