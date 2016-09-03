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
