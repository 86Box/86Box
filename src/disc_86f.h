/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the 86F floppy image format (stores the
 *		data in the form of FM/MFM-encoded transitions) which also
 *		forms the core of the emulator's floppy disk emulation.
 *
 * Version:	@(#)disc_86f.h	1.0.1	2017/08/23
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_DISC_86F_H
# define EMU_DISC_86F_H


extern void d86f_init(void);
extern void d86f_load(int drive, wchar_t *fn);
extern void d86f_close(int drive);
extern void d86f_seek(int drive, int track);
extern int d86f_hole(int drive);
extern double d86f_byteperiod(int drive);
extern void d86f_stop(int drive);
extern void d86f_poll(int drive);
extern int d86f_realtrack(int track, int drive);
extern void d86f_reset(int drive, int side);
extern void d86f_readsector(int drive, int sector, int track, int side, int density, int sector_size);
extern void d86f_writesector(int drive, int sector, int track, int side, int density, int sector_size);
extern void d86f_comparesector(int drive, int sector, int track, int side, int rate, int sector_size);
extern void d86f_readaddress(int drive, int side, int density);
extern void d86f_format(int drive, int side, int density, uint8_t fill);

extern void d86f_prepare_track_layout(int drive, int side);
extern void d86f_set_version(int drive, uint16_t version);

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

#if 0
extern int raw_tsize[2];
extern int gap2_size[2];
extern int gap3_size[2];
extern int gap4_size[2];
#endif

#define D86FVER		0x020B

extern void d86f_initialize_last_sector_id(int drive, int c, int h, int r, int n);
extern void d86f_zero_bit_field(int drive, int side);


#endif	/*EMU_DISC_86F_H*/
