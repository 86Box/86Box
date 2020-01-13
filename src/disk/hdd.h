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
 * Version:	@(#)hdd.h	1.0.8	2018/10/28
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_HDD_H
# define EMU_HDD_H


#define HDD_NUM		30	/* total of 30 images supported */


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
    HDD_BUS_SCSI,
    HDD_BUS_USB
};
#endif


/* Define the virtual Hard Disk. */
typedef struct {
    uint8_t	id;
    uint8_t	mfm_channel;		/* Should rename and/or unionize */
    uint8_t	esdi_channel;
    uint8_t	xta_channel;
    uint8_t	ide_channel;
    uint8_t	scsi_id;
    uint8_t	bus,
		res;			/* Reserved for bus mode */
    uint8_t	wp;			/* Disk has been mounted READ-ONLY */
    uint8_t	pad, pad0;

    void	*priv;

    wchar_t	fn[1024],		/* Name of current image file */
		prev_fn[1024];		/* Name of previous image file */

    uint32_t	res0, pad1,
		base,
		spt,
		hpc,			/* Physical geometry parameters */
		tracks;
} hard_disk_t;


extern hard_disk_t      hdd[HDD_NUM];
extern unsigned int	hdd_table[128][3];


typedef struct vhd_footer_t
{
    uint8_t	cookie[8];
    uint32_t	features;
    uint32_t	version;
    uint64_t	offset;
    uint32_t	timestamp;
    uint8_t	creator[4];
    uint32_t	creator_vers;
    uint8_t	creator_host_os[4];
    uint64_t	orig_size;
    uint64_t	curr_size;
    struct {
	uint16_t	cyl;
	uint8_t		heads;
	uint8_t		spt;
    } geom;
    uint32_t	type;
    uint32_t	checksum;
    uint8_t	uuid[16];
    uint8_t	saved_state;
    uint8_t	reserved[427];
} vhd_footer_t;


extern int	hdd_init(void);
extern int	hdd_string_to_bus(char *str, int cdrom);
extern char	*hdd_bus_to_string(int bus, int cdrom);
extern int	hdd_is_valid(int c);

extern void	hdd_image_init(void);
extern int	hdd_image_load(int id);
extern void	hdd_image_seek(uint8_t id, uint32_t sector);
extern void	hdd_image_read(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern int	hdd_image_read_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern void	hdd_image_write(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern int	hdd_image_write_ex(uint8_t id, uint32_t sector, uint32_t count, uint8_t *buffer);
extern void	hdd_image_zero(uint8_t id, uint32_t sector, uint32_t count);
extern int	hdd_image_zero_ex(uint8_t id, uint32_t sector, uint32_t count);
extern uint32_t	hdd_image_get_last_sector(uint8_t id);
extern uint32_t	hdd_image_get_pos(uint8_t id);
extern uint8_t	hdd_image_get_type(uint8_t id);
extern void	hdd_image_unload(uint8_t id, int fn_preserve);
extern void	hdd_image_close(uint8_t id);
extern void	hdd_image_calc_chs(uint32_t *c, uint32_t *h, uint32_t *s, uint32_t size);

extern void	vhd_footer_from_bytes(vhd_footer_t *vhd, uint8_t *bytes);
extern void	vhd_footer_to_bytes(uint8_t *bytes, vhd_footer_t *vhd);
extern void	new_vhd_footer(vhd_footer_t **vhd);
extern void	generate_vhd_checksum(vhd_footer_t *vhd);

extern int	image_is_hdi(const wchar_t *s);
extern int	image_is_hdx(const wchar_t *s, int check_signature);
extern int	image_is_vhd(const wchar_t *s, int check_signature);


#endif	/*EMU_HDD_H*/
