/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the common disk controller handler.
 *
 * Version:	@(#)hdc.h	1.0.8	2018/04/05
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_HDC_H
# define EMU_HDC_H


#define MFM_NUM		2	/* 2 drives per controller supported */
#define ESDI_NUM	2	/* 2 drives per controller supported */
#define XTA_NUM		2	/* 2 drives per controller supported */
#define IDE_NUM		8
#define SCSI_NUM	16	/* theoretically the controller can have at
				 * least 7 devices, with each device being
				 * able to support 8 units, but hey... */

extern char	*hdc_name;
extern int	hdc_current;


extern const device_t	mfm_xt_xebec_device;		/* mfm_xt_xebec */
extern const device_t	mfm_xt_dtc5150x_device;		/* mfm_xt_dtc */
extern const device_t	mfm_at_wd1003_device;		/* mfm_at_wd1003 */

extern const device_t	esdi_at_wd1007vse1_device;	/* esdi_at */
extern const device_t	esdi_ps2_device;		/* esdi_mca */

extern const device_t	ide_isa_device;			/* isa_ide */
extern const device_t	ide_isa_2ch_device;		/* isa_ide_2ch */
extern const device_t	ide_isa_2ch_opt_device;		/* isa_ide_2ch_opt */
extern const device_t	ide_vlb_device;			/* vlb_ide */
extern const device_t	ide_vlb_2ch_device;		/* vlb_ide_2ch */
extern const device_t	ide_pci_device;			/* pci_ide */
extern const device_t	ide_pci_2ch_device;		/* pci_ide_2ch */

extern const device_t	ide_ter_device;
extern const device_t	ide_qua_device;

extern const device_t	xta_wdxt150_device;		/* xta_wdxt150 */
extern const device_t	xta_hd20_device;		/* EuroPC internal */

extern const device_t	xtide_device;			/* xtide_xt */
extern const device_t	xtide_at_device;		/* xtide_at */
extern const device_t	xtide_acculogic_device;		/* xtide_ps2 */
extern const device_t	xtide_at_ps2_device;		/* xtide_at_ps2 */


extern void	hdc_init(char *name);
extern void	hdc_reset(void);

extern char	*hdc_get_name(int hdc);
extern char	*hdc_get_internal_name(int hdc);
extern int	hdc_get_from_internal_name(char *s);
extern int	hdc_has_config(int hdc);
extern const device_t	*hdc_get_device(int hdc);
extern int	hdc_get_flags(int hdc);
extern int	hdc_available(int hdc);


#endif	/*EMU_HDC_H*/
