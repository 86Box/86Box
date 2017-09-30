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
 * Version:	@(#)hdc.h	1.0.1	2017/09/29
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_HDC_H
# define EMU_HDC_H


extern char	hdc_name[16];
extern int	hdc_current;


extern device_t	mfm_xt_xebec_device;		/* mfm_xt_xebec */
extern device_t	mfm_xt_dtc5150x_device;		/* mfm_xt_dtc */
extern device_t	mfm_at_wd1003_device;		/* mfm_at_wd1003 */

extern device_t	esdi_at_wd1007vse1_device;	/* esdi_at */
extern device_t	esdi_ps2_device;		/* esdi_mca */

extern device_t	xtide_device;			/* xtide_xt */
extern device_t	xtide_at_device;		/* xtide_at */
extern device_t	xtide_ps2_device;		/* xtide_ps2 */
extern device_t	xtide_at_ps2_device;		/* xtide_at_ps2 */


extern char	*hdc_get_name(int hdc);
extern char	*hdc_get_internal_name(int hdc);
extern int	hdc_get_flags(int hdc);
extern int	hdc_available(int hdc);
extern int	hdc_current_is_mfm(void);
extern void	hdc_init(char *internal_name);


#endif	/*EMU_HDC_H*/
