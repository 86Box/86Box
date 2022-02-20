/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Iomega ZIP drive with SCSI(-like)
 *		commands, for both ATAPI and SCSI usage.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018,2019 Miran Grca.
 */

#ifndef EMU_ZIP_H
#define EMU_ZIP_H

#define ZIP_NUM			  4

#define BUF_SIZE 32768

#define ZIP_TIME	10.0

#define ZIP_SECTORS (96*2048)

#define ZIP_250_SECTORS (489532)


enum {
    ZIP_BUS_DISABLED = 0,
    ZIP_BUS_ATAPI = 5,
    ZIP_BUS_SCSI,
    ZIP_BUS_USB
};


typedef struct {
    uint8_t id;

    union {
	uint8_t res, res0,		/* Reserved for other ID's. */
		res1,
		ide_channel, scsi_device_id;
    };

    uint8_t bus_type,		/* 0 = ATAPI, 1 = SCSI */
	    bus_mode,		/* Bit 0 = PIO suported;
				   Bit 1 = DMA supportd. */
	    read_only,		/* Struct variable reserved for
				   media status. */
	    pad, pad0;

    FILE *f;
    void *priv;

    char image_path[1024],
	    prev_image_path[1024];

    uint32_t is_250, medium_size,
	     base;
} zip_drive_t;

typedef struct {
    mode_sense_pages_t ms_pages_saved;

    zip_drive_t *drv;

    uint8_t *buffer,
	    atapi_cdb[16],
	    current_cdb[16],
	    sense[256];

    uint8_t status, phase,
	    error, id,
	    features, cur_lun,
	    pad0, pad1;

    uint16_t request_length, max_transfer_len;

    int requested_blocks, packet_status,
	total_length, do_page_save,
	unit_attention, request_pos,
	old_len, pad3;

    uint32_t sector_pos, sector_len,
	     packet_len, pos;

    double callback;
} zip_t;


extern zip_t		*zip[ZIP_NUM];
extern zip_drive_t	zip_drives[ZIP_NUM];
extern uint8_t		atapi_zip_drives[8];
extern uint8_t		scsi_zip_drives[16];

#define zip_sense_error dev->sense[0]
#define zip_sense_key dev->sense[2]
#define zip_asc dev->sense[12]
#define zip_ascq dev->sense[13]


#ifdef __cplusplus
extern "C" {
#endif

extern void     zip_disk_close(zip_t *dev);
extern void     zip_disk_reload(zip_t *dev);
extern void	zip_insert(zip_t *dev);

extern void	zip_global_init(void);
extern void	zip_hard_reset(void);

extern void	zip_reset(scsi_common_t *sc);
extern int	zip_load(zip_t *dev, char *fn);
extern void	zip_close();

#ifdef __cplusplus
}
#endif

#endif	/*EMU_ZIP_H*/
