/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Generic floppy disk interface that communicates with the
 *		other handlers.
 *
 * Version:	@(#)disc.h	1.0.1	2017/06/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_DISC_H
# define EMU_DISC_H


#define FDD_NUM		4


typedef struct
{
        void (*seek)(int drive, int track);
        void (*readsector)(int drive, int sector, int track, int side, int density, int sector_size);
        void (*writesector)(int drive, int sector, int track, int side, int density, int sector_size);
        void (*comparesector)(int drive, int sector, int track, int side, int density, int sector_size);
        void (*readaddress)(int drive, int side, int density);
        void (*format)(int drive, int side, int density, uint8_t fill);
        int (*hole)(int drive);
        double (*byteperiod)(int drive);
        void (*stop)(int drive);
        void (*poll)(int drive);
} DRIVE;


extern DRIVE	drives[FDD_NUM];
extern int	curdrive;

extern int	disc_time;
extern int	disc_poll_time[FDD_NUM];


extern void	disc_load(int drive, wchar_t *fn);
extern void	disc_new(int drive, char *fn);
extern void	disc_close(int drive);
extern void	disc_init(void);
extern void	disc_reset(void);
extern void	disc_poll(int drive);
extern void	disc_poll_0(void* priv);
extern void	disc_poll_1(void* priv);
extern void	disc_poll_2(void* priv);
extern void	disc_poll_3(void* priv);
extern void	disc_seek(int drive, int track);
extern void	disc_readsector(int drive, int sector, int track,
				int side, int density, int sector_size);
extern void	disc_writesector(int drive, int sector, int track,
				 int side, int density, int sector_size);
extern void	disc_comparesector(int drive, int sector, int track,
				   int side, int density, int sector_size);
extern void	disc_readaddress(int drive, int side, int density);
extern void	disc_format(int drive, int side, int density, uint8_t fill);
extern int	disc_hole(int drive);
extern double	disc_byteperiod(int drive);
extern void	disc_stop(int drive);
extern int	disc_empty(int drive);
extern void	disc_set_rate(int drive, int drvden, int rate);

extern void	fdc_callback(void *priv);
extern int	fdc_data(uint8_t dat);
extern void	fdc_spindown(void);
extern void	fdc_finishread(void);
extern void	fdc_datacrcerror(void);
extern void	fdc_headercrcerror(void);
extern void	fdc_writeprotect(void);
extern int	fdc_getdata(int last);
extern void	fdc_sectorid(uint8_t track, uint8_t side, uint8_t sector,
			     uint8_t size, uint8_t crc1, uint8_t crc2);
extern void	fdc_indexpulse(void);

/*extern int fdc_time;
extern int fdc_ready;
extern int fdc_indexcount;*/

extern int motorspin;
extern int motoron[FDD_NUM];

extern int swwp;
extern int disable_write;

extern int defaultwriteprot;

extern int writeprot[FDD_NUM], fwriteprot[FDD_NUM];
extern int disc_track[FDD_NUM];
extern int disc_changed[FDD_NUM];
extern int drive_empty[FDD_NUM];
extern int drive_type[FDD_NUM];

/*Used in the Read A Track command. Only valid for disc_readsector(). */
#define SECTOR_FIRST -2
#define SECTOR_NEXT  -1

#if 0
/* Bits 0-3 define byte type, bit 5 defines whether it is a per-track (0) or per-sector (1) byte, if bit 7 is set, the byte is the index hole. */
#define BYTE_GAP0		0x00
#define BYTE_GAP1		0x10
#define BYTE_GAP4		0x20
#define BYTE_GAP2		0x40
#define BYTE_GAP3		0x50
#define BYTE_I_SYNC		0x01
#define BYTE_ID_SYNC		0x41
#define BYTE_DATA_SYNC		0x51
#define BYTE_IAM_SYNC		0x02
#define BYTE_IDAM_SYNC		0x42
#define BYTE_DATAAM_SYNC	0x52
#define BYTE_IAM		0x03
#define BYTE_IDAM		0x43
#define BYTE_DATAAM		0x53
#define BYTE_ID			0x44
#define BYTE_DATA		0x54
#define BYTE_ID_CRC		0x45
#define BYTE_DATA_CRC		0x55

#define BYTE_IS_FUZZY		0x80
#define BYTE_INDEX_HOLE		0x80	/* 1 = index hole, 0 = regular byte */
#define BYTE_IS_SECTOR		0x40	/* 1 = per-sector, 0 = per-track */
#define BYTE_IS_POST_TRACK	0x20	/* 1 = after all sectors, 0 = before or during all sectors */
#define BYTE_IS_DATA		0x10	/* 1 = data, 0 = id */
#define BYTE_TYPE		0x0F	/* 5 = crc, 4 = data, 3 = address mark, 2 = address mark sync, 1 = sync, 0 = gap */

#define BYTE_TYPE_GAP		0x00
#define BYTE_TYPE_SYNC		0x01
#define BYTE_TYPE_AM_SYNC	0x02
#define BYTE_TYPE_AM		0x03
#define BYTE_TYPE_DATA		0x04
#define BYTE_TYPE_CRC		0x05
#endif

typedef union {
	uint16_t word;
	uint8_t bytes[2];
} crc_t;

void disc_calccrc(uint8_t byte, crc_t *crc_var);

typedef struct
{
	uint16_t (*disk_flags)(int drive);
	uint16_t (*side_flags)(int drive);
        void (*writeback)(int drive);
	void (*set_sector)(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n);
        uint8_t (*read_data)(int drive, int side, uint16_t pos);
        void (*write_data)(int drive, int side, uint16_t pos, uint8_t data);
	int (*format_conditions)(int drive);
	int32_t (*extra_bit_cells)(int drive, int side);
        uint16_t* (*encoded_data)(int drive, int side);
	void (*read_revolution)(int drive);
        uint32_t (*index_hole_pos)(int drive, int side);
	uint32_t (*get_raw_size)(int drive, int side);
	uint8_t check_crc;
} d86f_handler_t;

d86f_handler_t d86f_handler[FDD_NUM];

void d86f_common_handlers(int drive);

int d86f_is_40_track(int drive);

void d86f_reset_index_hole_pos(int drive, int side);

uint16_t d86f_prepare_pretrack(int drive, int side, int iso);
uint16_t d86f_prepare_sector(int drive, int side, int prev_pos, uint8_t *id_buf, uint8_t *data_buf, int data_len, int gap2, int gap3, int deleted, int bad_crc);

int gap3_sizes[5][8][48];

void null_writeback(int drive);
void null_write_data(int drive, int side, uint16_t pos, uint8_t data);
int null_format_conditions(int drive);
void d86f_unregister(int drive);

uint8_t dmf_r[21];
uint8_t xdf_physical_sectors[2][2];
uint8_t xdf_gap3_sizes[2][2];
uint16_t xdf_trackx_spos[2][8];

typedef struct
{
	uint8_t h;
	uint8_t r;
} xdf_id_t;

typedef union
{
	uint16_t word;
	xdf_id_t id;
} xdf_sector_t;

xdf_sector_t xdf_img_layout[2][2][46];
xdf_sector_t xdf_disk_layout[2][2][38];

uint32_t td0_get_raw_tsize(int side_flags, int slower_rpm);

void d86f_set_track_pos(int drive, uint32_t track_pos);

int32_t null_extra_bit_cells(int drive, int side);
uint16_t* common_encoded_data(int drive, int side);

void common_read_revolution(int drive);
void null_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n);

uint32_t null_index_hole_pos(int drive, int side);

uint32_t common_get_raw_size(int drive, int side);

typedef struct
{
	uint8_t c;
	uint8_t h;
	uint8_t r;
	uint8_t n;
} sector_id_fields_t;

typedef union
{
	uint32_t dword;
	uint8_t byte_array[4];
	sector_id_fields_t id;
} sector_id_t;

void d86f_set_version(int drive, uint16_t version);

void d86f_initialize_last_sector_id(int drive, int c, int h, int r, int n);
void d86f_zero_bit_field(int drive, int side);


#endif	/*EMU_DISC_H*/
