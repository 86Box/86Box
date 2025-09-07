/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SCSI controller handler header.
 *
 *
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2018 TheCollector1995.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2017-2018 Fred N. van Kempen.
 */
#ifndef EMU_SCSI_H
#define EMU_SCSI_H

/* Configuration. */
#define SCSI_CARD_MAX 4
#define SCSI_BUS_MAX  9 /* currently we support up to 9 controllers:
                           up to 1 on-board + up to 4x pas plus/16 + up to 4 scsi controllers */

#define SCSI_ID_MAX   16 /* 16 on wide buses */
#define SCSI_LUN_MAX  8  /* always 8 */

extern int             scsi_card_current[SCSI_CARD_MAX];

extern void            scsi_reset(void);
extern uint8_t         scsi_get_bus(void);

extern int             scsi_card_available(int card);
#ifdef EMU_DEVICE_H
extern const device_t *scsi_card_getdevice(int card);
#endif
extern int             scsi_card_has_config(int card);
extern const char     *scsi_card_get_internal_name(int card);
extern int             scsi_card_get_from_internal_name(char *s);
extern void            scsi_card_init(void);

extern void            scsi_bus_set_speed(uint8_t bus, double speed);
extern double          scsi_bus_get_speed(uint8_t bus);

#endif /*EMU_SCSI_H*/
