/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		SCSI controller handler header.
 *
 *
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 TheCollector1995.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_SCSI_H
#define EMU_SCSI_H

extern int	scsi_card_current[4];

extern int	scsi_card_available(int card);
#ifdef EMU_DEVICE_H
extern const	device_t *scsi_card_getdevice(int card);
#endif
extern int	scsi_card_has_config(int card);
extern char	*scsi_card_get_internal_name(int card);
extern int	scsi_card_get_from_internal_name(char *s);
extern void	scsi_card_init(void);

#endif	/*EMU_SCSI_H*/
