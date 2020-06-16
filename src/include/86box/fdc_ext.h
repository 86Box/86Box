/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the NEC uPD-765 and compatible floppy disk
 *		controller.
 *
 *
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2018-2020 Fred N. van Kempen.
 */
#ifndef EMU_FDC_EXT_H
# define EMU_FDC_EXT_H

extern int fdc_type;

/* Controller types. */
#define FDC_NONE		0
#define FDC_INTERNAL		1

extern const device_t fdc_pii151b_device;
extern const device_t fdc_pii158b_device;

extern void fdc_ext_reset(void);

extern char *fdc_ext_get_name(int fdc_ext);
extern char *fdc_ext_get_internal_name(int fdc_ext);
extern int fdc_ext_get_id(char *s);
extern int fdc_ext_get_from_internal_name(char *s);
extern const device_t *fdc_ext_get_device(int fdc_ext);
extern int fdc_ext_has_config(int fdc_ext);
extern int fdc_ext_available(int fdc_ext);

#endif	/*EMU_FDC_H*/
