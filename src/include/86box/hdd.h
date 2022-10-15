/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the hard disk image handler.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_HDD_H
#define EMU_HDD_H

#define HDD_NUM 88 /* total of 88 images supported */

/* Hard Disk bus types. */
#if 0
/* Bit 4 = DMA supported (0 = no, 1 yes) - used for IDE and ATAPI only;
   Bit 5 = Removable (0 = no, 1 yes). */

enum {
    BUS_DISABLED		= 0x00,

    BUS_MFM			= 0x01,	/* These four are for hard disk only. */
    BUS_XIDE			= 0x02,
    BUS_XTA			= 0x03,
    BUS_ESDI			= 0x04,

    BUS_PANASONIC		= 0x21,	/ These four are for CD-ROM only. */
    BUS_PHILIPS			= 0x22,
    BUS_SONY			= 0x23,
    BUS_MITSUMI			= 0x24,

    BUS_IDE_PIO_ONLY		= 0x05,
    BUS_IDE_PIO_AND_DMA		= 0x15,
    BUS_IDE_R_PIO_ONLY		= 0x25,
    BUS_IDE_R_PIO_AND_DMA	= 0x35,

    BUS_ATAPI_PIO_ONLY		= 0x06,
    BUS_ATAPI_PIO_AND_DMA	= 0x16,
    BUS_ATAPI_R_PIO_ONLY	= 0x26,
    BUS_ATAPI_R_PIO_AND_DMA	= 0x36,

    BUS_SASI			= 0x07,
    BUS_SASI_R			= 0x27,

    BUS_SCSI			= 0x08,
    BUS_SCSI_R			= 0x28,

    BUS_USB			= 0x09,
    BUS_USB_R			= 0x29
};
#else
enum {
    HDD_BUS_DISABLED = 0,
    HDD_BUS_MFM,
    HDD_BUS_XTA,
    HDD_BUS_ESDI,
    HDD_BUS_IDE,
    HDD_BUS_ATAPI,
    HDD_BUS_SCSI,
    HDD_BUS_USB
};
#endif

enum {
    HDD_OP_SEEK = 0,
    HDD_OP_READ,
    HDD_OP_WRITE
};

#define HDD_MAX_ZONES     16
#define HDD_MAX_CACHE_SEG 16

typedef struct {
    const char *name;
    const char *internal_name;
    uint32_t    zones;
    uint32_t    avg_spt;
    uint32_t    heads;
    uint32_t    rpm;
    uint32_t    rcache_num_seg;
    uint32_t    rcache_seg_size;
    uint32_t    max_multiple;
    double      full_stroke_ms;
    double      track_seek_ms;
} hdd_preset_t;

typedef struct {
    uint32_t id;
    uint32_t lba_addr;
    uint32_t ra_addr;
    uint32_t host_addr;
    uint8_t  lru;
    uint8_t  valid;
} hdd_cache_seg_t;

typedef struct {
    // Read cache
    hdd_cache_seg_t segments[HDD_MAX_CACHE_SEG];
    uint32_t        num_segments;
    uint32_t        segment_size;
    uint32_t        ra_segment;
    uint8_t         ra_ongoing;
    uint64_t        ra_start_time;

    // Write cache
    uint32_t write_addr;
    uint32_t write_pending;
    uint32_t write_size;
    uint64_t write_start_time;
} hdd_cache_t;

typedef struct {
    uint32_t cylinders;
    uint32_t sectors_per_track;
    double   sector_time_usec;
    uint32_t start_sector;
    uint32_t end_sector;
    uint32_t start_track;
} hdd_zone_t;

/* Define the virtual Hard Disk. */
typedef struct {
    uint8_t id;
    union {
        uint8_t channel; /* Needed for Settings to reduce the number of if's */

        uint8_t mfm_channel; /* Should rename and/or unionize */
        uint8_t esdi_channel;
        uint8_t xta_channel;
        uint8_t ide_channel;
        uint8_t scsi_id;
    };
    uint8_t bus,
        res;    /* Reserved for bus mode */
    uint8_t wp; /* Disk has been mounted READ-ONLY */
    uint8_t pad, pad0;

    void *priv;

    char fn[1024],     /* Name of current image file */
        prev_fn[1024]; /* Name of previous image file */

    uint32_t res0, pad1,
        base,
        spt,
        hpc, /* Physical geometry parameters */
        tracks;

    hdd_zone_t  zones[HDD_MAX_ZONES];
    uint32_t    num_zones;
    hdd_cache_t cache;
    uint32_t    phy_cyl;
    uint32_t    phy_heads;
    uint32_t    rpm;
    uint8_t     max_multiple_block;

    uint32_t cur_cylinder;
    uint32_t cur_track;
    uint32_t cur_addr;

    uint32_t speed_preset;

    double avg_rotation_lat_usec;
    double full_stroke_usec;
    double head_switch_usec;
    double cyl_switch_usec;
} hard_disk_t;

extern hard_disk_t  hdd[HDD_NUM];
extern unsigned int hdd_table[128][3];

extern int   hdd_init(void);
extern int   hdd_string_to_bus(char *str, int cdrom);
extern char *hdd_bus_to_string(int bus, int cdrom);
extern int   hdd_is_valid(int c);

extern void     hdd_image_init(void);
extern int      hdd_image_load(int id);
extern void     hdd_image_seek(uint8_t id, uint32_t sector);
extern void     hdd_image_read(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern int      hdd_image_read_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern void     hdd_image_write(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern int      hdd_image_write_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern void     hdd_image_zero(uint8_t id, uint32_t sector, uint32_t count);
extern int      hdd_image_zero_ex(uint8_t id, uint32_t sector, uint32_t count);
extern uint32_t hdd_image_get_last_sector(uint8_t id);
extern uint32_t hdd_image_get_pos(uint8_t id);
extern uint8_t  hdd_image_get_type(uint8_t id);
extern void     hdd_image_unload(uint8_t id, int fn_preserve);
extern void     hdd_image_close(uint8_t id);
extern void     hdd_image_calc_chs(uint32_t *c, uint32_t *h, uint32_t *s, uint32_t size);

extern int image_is_hdi(const char *s);
extern int image_is_hdx(const char *s, int check_signature);
extern int image_is_vhd(const char *s, int check_signature);

extern double      hdd_timing_write(hard_disk_t *hdd, uint32_t addr, uint32_t len);
extern double      hdd_timing_read(hard_disk_t *hdd, uint32_t addr, uint32_t len);
extern double      hdd_seek_get_time(hard_disk_t *hdd, uint32_t dst_addr, uint8_t operation, uint8_t continuous, double max_seek_time);
int                hdd_preset_get_num();
const char        *hdd_preset_getname(int preset);
extern const char *hdd_preset_get_internal_name(int preset);
extern int         hdd_preset_get_from_internal_name(char *s);
extern void        hdd_preset_apply(int hdd_id);

#endif /*EMU_HDD_H*/
