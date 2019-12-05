/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the 86F floppy image format.
 *
 * Version:	@(#)floppy_86f.h	1.0.6	2019/12/05
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#ifndef EMU_FLOPPY_86F_H
# define EMU_FLOPPY_86F_H


#define D86FVER			0x020C

/* Thesere were borrowed from TeleDisk. */
#define SECTOR_DUPLICATED	0x01
#define SECTOR_CRC_ERROR	0x02
#define SECTOR_DELETED_DATA	0x04
#define SECTOR_DATA_SKIPPED	0x10
#define SECTOR_NO_DATA		0x20
#define SECTOR_NO_ID		0x40

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


extern d86f_handler_t	d86f_handler[FDD_NUM];


extern void	d86f_init(void);
extern void	d86f_load(int drive, wchar_t *fn);
extern void	d86f_close(int drive);
extern void	d86f_seek(int drive, int track);
extern int	d86f_hole(int drive);
extern uint64_t	d86f_byteperiod(int drive);
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
extern void	d86f_initialize_last_sector_id(int drive, int c, int h, int r, int n);
extern void	d86f_initialize_linked_lists(int drive);
extern void	d86f_destroy_linked_lists(int drive, int side);

extern uint16_t	d86f_prepare_sector(int drive, int side, int prev_pos, uint8_t *id_buf, uint8_t *data_buf,
				    int data_len, int gap2, int gap3, int flags);
extern void	d86f_setup(int drive);
extern void	d86f_destroy(int drive);
extern int	d86f_export(int drive, wchar_t *fn);
extern void	d86f_unregister(int drive);
extern void	d86f_common_handlers(int drive);
extern void	d86f_set_version(int drive, uint16_t version);
extern int	d86f_is_40_track(int drive);
extern void	d86f_reset_index_hole_pos(int drive, int side);
extern uint16_t	d86f_prepare_pretrack(int drive, int side, int iso);
extern void	d86f_set_track_pos(int drive, uint32_t track_pos);
extern void	d86f_set_cur_track(int drive, int track);
extern void	d86f_zero_track(int drive);
extern void	d86f_initialize_last_sector_id(int drive, int c, int h, int r, int n);
extern void	d86f_initialize_linked_lists(int drive);
extern void	d86f_destroy_linked_lists(int drive, int side);

extern uint16_t	*common_encoded_data(int drive, int side);
extern void	common_read_revolution(int drive);
extern uint32_t	common_get_raw_size(int drive, int side);

extern void	null_writeback(int drive);
extern void	null_write_data(int drive, int side, uint16_t pos, uint8_t data);
extern int	null_format_conditions(int drive);
extern int32_t	null_extra_bit_cells(int drive, int side);
extern void	null_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n);
extern uint32_t	null_index_hole_pos(int drive, int side);


#endif	/*EMU_FLOPPY_86F_H*/
