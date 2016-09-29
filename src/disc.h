/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
typedef struct
{
        void (*seek)(int drive, int track);
        void (*readsector)(int drive, int sector, int track, int side, int density, int sector_size);
        void (*writesector)(int drive, int sector, int track, int side, int density, int sector_size);
        void (*readaddress)(int drive, int track, int side, int density);
        void (*format)(int drive, int track, int side, int density, uint8_t fill);
        int (*hole)(int drive);
        double (*byteperiod)(int drive);
        void (*stop)(int drive);
        void (*poll)();
	int (*realtrack)(int drive, int track);
} DRIVE;

extern DRIVE drives[2];

extern int curdrive;

void disc_load(int drive, char *fn);
void disc_new(int drive, char *fn);
void disc_close(int drive);
void disc_init();
void disc_reset();
void disc_poll();
void disc_seek(int drive, int track);
void disc_readsector(int drive, int sector, int track, int side, int density, int sector_size);
void disc_writesector(int drive, int sector, int track, int side, int density, int sector_size);
void disc_readaddress(int drive, int track, int side, int density);
void disc_format(int drive, int track, int side, int density, uint8_t fill);
int disc_realtrack(int drive, int track);
int disc_hole(int drive);
double disc_byteperiod(int drive);
void disc_stop(int drive);
int disc_empty(int drive);
void disc_set_rate(int drive, int drvden, int rate);
void disc_set_drivesel(int drive);
extern int disc_time;
extern int64_t disc_poll_time;
extern int disc_drivesel;

void fdc_callback();
int  fdc_data(uint8_t dat);
void fdc_spindown();
void fdc_finishread();
void fdc_notfound();
void fdc_datacrcerror();
void fdc_headercrcerror();
void fdc_writeprotect();
int  fdc_getdata(int last);
void fdc_sectorid(uint8_t track, uint8_t side, uint8_t sector, uint8_t size, uint8_t crc1, uint8_t crc2);
void fdc_indexpulse();
/*extern int fdc_time;
extern int fdc_ready;
extern int fdc_indexcount;*/

extern int motorspin;
extern int64_t motoron;

extern int swwp;
extern int disable_write;

extern int defaultwriteprot;
//extern char discfns[4][260];

extern int writeprot[2], fwriteprot[2];
extern int disc_track[2];
extern int disc_changed[2];
extern int drive_empty[2];
extern int drive_type[2];

/*Used in the Read A Track command. Only valid for disc_readsector(). */
#define SECTOR_FIRST -2
#define SECTOR_NEXT  -1

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

typedef union {
	uint16_t word;
	uint8_t bytes[2];
} crc_t;

void disc_calccrc(int drive, uint8_t byte, crc_t *crc_var);

typedef struct
{
	uint16_t (*disk_flags)(int drive);
	uint16_t (*side_flags)(int drive);
        void (*writeback)(int drive);
	void (*set_sector)(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n);
        uint8_t (*read_data)(int drive, int side, uint16_t pos);
        void (*write_data)(int drive, int side, uint16_t pos, uint8_t data);
	int (*format_conditions)(int drive);
	uint8_t check_crc;
} d86f_handler_t;

d86f_handler_t d86f_handler[2];

void d86f_common_handlers(int drive);

int d86f_is_40_track(int drive);

uint8_t* d86f_track_data(int drive, int side);
uint8_t* d86f_track_layout(int drive, int side);

uint16_t d86f_prepare_pretrack(int drive, int side, int iso, int write_data);
uint16_t d86f_prepare_sector(int drive, int side, int pos, uint8_t *id_buf, uint8_t *data_buf, int data_len, int write_data, int gap2, int gap3, int limit, int deleted, int bad_crc);

int gap3_sizes[5][8][256];

void null_writeback(int drive);
void null_poll_write_data(int drive, int side, uint16_t pos, uint8_t data);
int null_format_conditions(int drive);
void d86f_unregister(int drive);

void d86f_reset_index_hole_pos(int drive, int side);
