/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          DDC monitor emulation definitions.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2020 RichardG.
 */
#ifndef EMU_VID_DDC_H
#define EMU_VID_DDC_H

extern void  *ddc_init(void *i2c);
extern void   ddc_close(void *eeprom);
extern size_t ddc_create_default_edid(uint8_t **size_out);
extern size_t ddc_load_edid(char *path, uint8_t *buf, size_t size);

#endif /*EMU_VID_DDC_H*/
