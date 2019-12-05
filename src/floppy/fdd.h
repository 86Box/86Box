/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the floppy drive emulation.
 *
 * Version:	@(#)fdd.h	1.0.7	2019/12/05
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 Fred N. van Kempen.
 */
#ifndef EMU_FDD_H
# define EMU_FDD_H


#define FDD_NUM			4
#define SEEK_RECALIBRATE	-999


#ifdef __cplusplus
extern "C" {
#endif

extern int	fdd_swap;

extern void	fdd_set_motor_enable(int drive, int motor_enable);
extern void	fdd_do_seek(int drive, int track);
extern void	fdd_forced_seek(int drive, int track_diff);
extern void	fdd_seek(int drive, int track_diff);
extern int	fdd_track0(int drive);
extern int	fdd_getrpm(int drive);
extern void	fdd_set_densel(int densel);
extern int	fdd_can_read_medium(int drive);
extern int	fdd_doublestep_40(int drive);
extern int	fdd_is_525(int drive);
extern int	fdd_is_dd(int drive);
extern int	fdd_is_ed(int drive);
extern int	fdd_is_double_sided(int drive);
extern void	fdd_set_head(int drive, int head);
extern int	fdd_get_head(int drive);
extern void	fdd_set_turbo(int drive, int turbo);
extern int	fdd_get_turbo(int drive);
extern void	fdd_set_check_bpb(int drive, int check_bpb);
extern int	fdd_get_check_bpb(int drive);

extern void	fdd_set_type(int drive, int type);
extern int	fdd_get_type(int drive);

extern int	fdd_get_flags(int drive);
extern int	fdd_get_densel(int drive);

extern char	*fdd_getname(int type);

extern char	*fdd_get_internal_name(int type);
extern int	fdd_get_from_internal_name(char *s);

extern int	fdd_current_track(int drive);


typedef struct {
    int		id;

    void	(*seek)(int drive, int track);
    void	(*readsector)(int drive, int sector, int track, int side,
			      int density, int sector_size);
    void	(*writesector)(int drive, int sector, int track, int side,
			       int density, int sector_size);
    void	(*comparesector)(int drive, int sector, int track, int side,
				 int density, int sector_size);
    void	(*readaddress)(int drive, int side, int density);
    void	(*format)(int drive, int side, int density, uint8_t fill);
    int		(*hole)(int drive);
    uint64_t	(*byteperiod)(int drive);
    void	(*stop)(int drive);
    void	(*poll)(int drive);
} DRIVE;


extern DRIVE		drives[FDD_NUM];
extern wchar_t		floppyfns[FDD_NUM][512];
extern pc_timer_t	fdd_poll_time[FDD_NUM];
extern int		ui_writeprot[FDD_NUM];

extern int	curdrive;

extern int	fdd_time;
extern int64_t	floppytime;


extern void	fdd_load(int drive, wchar_t *fn);
extern void	fdd_new(int drive, char *fn);
extern void	fdd_close(int drive);
extern void	fdd_init(void);
extern void	fdd_reset(void);
extern void	fdd_seek(int drive, int track);
extern void	fdd_readsector(int drive, int sector, int track,
				int side, int density, int sector_size);
extern void	fdd_writesector(int drive, int sector, int track,
				 int side, int density, int sector_size);
extern void	fdd_comparesector(int drive, int sector, int track,
				   int side, int density, int sector_size);
extern void	fdd_readaddress(int drive, int side, int density);
extern void	fdd_format(int drive, int side, int density, uint8_t fill);
extern int	fdd_hole(int drive);
extern void	fdd_stop(int drive);

extern int	motorspin;
extern uint64_t	motoron[FDD_NUM];

extern int	swwp;
extern int	disable_write;

extern int	defaultwriteprot;

extern int	writeprot[FDD_NUM], fwriteprot[FDD_NUM];
extern int	fdd_changed[FDD_NUM];
extern int	drive_empty[FDD_NUM];

/*Used in the Read A Track command. Only valid for fdd_readsector(). */
#define SECTOR_FIRST -2
#define SECTOR_NEXT  -1

typedef union {
	uint16_t word;
	uint8_t bytes[2];
} crc_t;

void fdd_calccrc(uint8_t byte, crc_t *crc_var);

typedef struct {
    uint16_t	(*disk_flags)(int drive);
    uint16_t	(*side_flags)(int drive);
    void	(*writeback)(int drive);
    void	(*set_sector)(int drive, int side, uint8_t c, uint8_t h,
			      uint8_t r, uint8_t n);
    uint8_t	(*read_data)(int drive, int side, uint16_t pos);
    void	(*write_data)(int drive, int side, uint16_t pos,
			      uint8_t data);
    int		(*format_conditions)(int drive);
    int32_t	(*extra_bit_cells)(int drive, int side);
    uint16_t*	(*encoded_data)(int drive, int side);
    void	(*read_revolution)(int drive);
    uint32_t	(*index_hole_pos)(int drive, int side);
    uint32_t	(*get_raw_size)(int drive, int side);

    uint8_t check_crc;
} d86f_handler_t;

extern const int	gap3_sizes[5][8][48];

extern const uint8_t	dmf_r[21];
extern const uint8_t	xdf_physical_sectors[2][2];
extern const uint8_t	xdf_gap3_sizes[2][2];
extern const uint16_t	xdf_trackx_spos[2][8];

typedef struct {
    uint8_t	h;
    uint8_t	r;
} xdf_id_t;

typedef union {
    uint16_t	word;
    xdf_id_t	id;
} xdf_sector_t;

extern const xdf_sector_t xdf_img_layout[2][2][46];
extern const xdf_sector_t xdf_disk_layout[2][2][38];


typedef struct {
    uint8_t	c;
    uint8_t	h;
    uint8_t	r;
    uint8_t	n;
} sector_id_fields_t;

typedef union {
    uint32_t	dword;
    uint8_t	byte_array[4];
    sector_id_fields_t id;
} sector_id_t;


void d86f_set_fdc(void *fdc);
void fdi_set_fdc(void *fdc);
void fdd_set_fdc(void *fdc);
void imd_set_fdc(void *fdc);
void img_set_fdc(void *fdc);
void mfm_set_fdc(void *fdc);


#ifdef __cplusplus
}
#endif


#endif	/*EMU_FDD_H*/
