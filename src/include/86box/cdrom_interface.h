/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the common CD-ROM interface controller handler.
 *
 *
 *
 * Authors: TheCollector1995
 *
 *          Copyright 2022 TheCollector1995.
 */
#ifndef EMU_CDROM_INTERFACE_H
#define EMU_CDROM_INTERFACE_H

extern int cdrom_interface_current;

extern void cdrom_interface_reset(void);

const  char           *cdrom_interface_get_internal_name(const int cdinterface);
extern int             cdrom_interface_get_from_internal_name(const char *s);
#ifdef EMU_DEVICE_H
extern const device_t *cdrom_interface_get_device(const int cdinterface);
#endif
extern int             cdrom_interface_has_config(const int cdinterface);
extern int             cdrom_interface_get_flags(const int cdinterface);
extern int             cdrom_interface_available(const int cdinterface);

#endif /*EMU_CDROM_INTERFACE_H*/
