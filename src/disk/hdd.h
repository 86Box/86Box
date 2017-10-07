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
 * Version:	@(#)hdd.h	1.0.3	2017/10/05
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_HDD_H
# define EMU_HDD_H


#define HDD_NUM		30	/* total of 30 images supported */


/* Hard Disk bus types. */
enum {
    HDD_BUS_DISABLED = 0,
    HDD_BUS_MFM,
    HDD_BUS_XTIDE,
    HDD_BUS_ESDI,
    HDD_BUS_IDE_PIO_ONLY,
    HDD_BUS_IDE_PIO_AND_DMA,
    HDD_BUS_SCSI,
    HDD_BUS_SCSI_REMOVABLE,
    HDD_BUS_USB
};


/* Define the virtual Hard Disk. */
typedef struct {
    int8_t	is_hdi;			/* image type (should rename) */
    int8_t	wp;			/* disk has been mounted READ-ONLY */

    uint8_t	bus;

    uint8_t	mfm_channel;		/* should rename and/or unionize */
    uint8_t	esdi_channel;
    uint8_t	xtide_channel;
    uint8_t	ide_channel;
    uint8_t	scsi_id;
    uint8_t	scsi_lun;

    uint32_t	base;

    uint64_t	spt,
		hpc,			/* physical geometry parameters */
		tracks;

    uint64_t	at_spt,			/* [Translation] parameters */
		at_hpc;

    FILE	*f;			/* current file handle to image */

    wchar_t	fn[260];		/* name of current image file */
    wchar_t	prev_fn[260];		/* name of previous image file */
} hard_disk_t;


extern hard_disk_t      hdd[HDD_NUM];
extern uint64_t		hdd_table[128][3];


extern int	hdd_init(void);
extern int	hdd_string_to_bus(char *str, int cdrom);
extern char	*hdd_bus_to_string(int bus, int cdrom);
extern int	hdd_is_valid(int c);

extern int	hdd_image_load(int id);
extern void	hdd_image_seek(uint8_t id, uint32_t sector);
extern void	hdd_image_read(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern int	hdd_image_read_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern void	hdd_image_write(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern int	hdd_image_write_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern void	hdd_image_zero(uint8_t id, uint32_t sector, uint32_t count);
extern int	hdd_image_zero_ex(uint8_t id, uint32_t sector, uint32_t count);
extern uint32_t	hdd_image_get_last_sector(uint8_t id);
extern uint8_t	hdd_image_get_type(uint8_t id);
extern void	hdd_image_specify(uint8_t id, uint64_t hpc, uint64_t spt);
extern void	hdd_image_unload(uint8_t id, int fn_preserve);
extern void	hdd_image_close(uint8_t id);

extern int	image_is_hdi(const wchar_t *s);
extern int	image_is_hdx(const wchar_t *s, int check_signature);


#endif	/*EMU_HDD_H*/
