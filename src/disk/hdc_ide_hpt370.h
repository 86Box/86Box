/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 */

#ifndef HDC_IDE_HPT370_H
#define HDC_IDE_HPT370_H

#define HPT370_ROM_BAR_32K 1
#define HPT370_ROM_BAR_64K 2

void  hpt370_reset(void *priv);
void  hpt370_close(void *priv);
void *hpt370_init(const device_t *info);

#endif /* HDC_IDE_HPT370_H */
