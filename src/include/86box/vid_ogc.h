/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Olivetti OGC 8-bit ISA (GO708) and
 *          M21/M24/M28 16-bit bus (GO317/318/380/709) video cards.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          EngiNerd, <webmaster.crrc@yahoo.it>
 *
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2020 EngiNerd.
 */
#ifndef VIDEO_OGC_H
#define VIDEO_OGC_H

typedef struct ogc_t {
    cga_t cga;
    uint16_t ctrl_addr;
    /* unused in OGC, required for M19 video card structure idiom */
    uint8_t  ctrl_3dd;
    uint8_t  ctrl_3de;
    uint32_t base;
    int      lineff;
    int      mono_display;
} ogc_t;

void    ogc_recalctimings(ogc_t *ogc);
void    ogc_out(uint16_t addr, uint8_t val, void *priv);
uint8_t ogc_in(uint16_t addr, void *priv);
void    ogc_write(uint32_t addr, uint8_t val, void *priv);
uint8_t ogc_read(uint32_t addr, void *priv);
void    ogc_poll(void *priv);
void    ogc_close(void *priv);
void    ogc_mdaattr_rebuild(void);

#ifdef EMU_DEVICE_H
extern const device_config_t ogc_config[];
extern const device_t        ogc_device;
#endif

#endif /*VIDEO_OGC_H*/
