/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NEC uPD7220 graphic display controller.
 *
 *
 *
 * Authors: TAKEDA toshiya,
 *          yui/Neko Project II
 *
 *          Copyright 2009-2023 TAKEDA, toshiya.
 *          Copyright 2008-2023 yui/Neko Project II.
 */
#ifndef VIDEO_UPD7220_H
#    define VIDEO_UPD7220_H

#define TVRAM_SIZE      0x4000
#define VRAM16_SIZE     0x40000
#define VRAM256_SIZE    0x80000
#define EMS_SIZE        0x10000

#define GDC_BUFFERS     1024
#define GDC_TABLEMAX    0x1000

#define GDC_VTICKS   18
#define GDC_VSTICKS  2

typedef struct upd7220_t {
    void *priv;

    /* vram access */
    void (*vram_write)(uint32_t addr, uint8_t val, void *priv);
    uint8_t (*vram_read)(uint32_t addr, void *priv);

    /* address */
    uint32_t address[480][80];

    /* registers */
    int cmdreg;
    uint8_t statreg;

    /* params */
    uint8_t sync[16];
    uint8_t zoom, zr, zw;
    uint8_t ra[16];
    uint8_t cs[3];
    uint8_t pitch;
    uint32_t lad;
    uint8_t vect[11];
    uint32_t ead, dad;
    uint8_t maskl, maskh;
    uint8_t mod;
    uint8_t start;
    uint8_t dirty;

    /* fifo buffers */
    uint8_t params[16];
    int params_count;
    uint8_t data[GDC_BUFFERS];
    int data_count, data_read, data_write;

    /* draw */
    int rt[GDC_TABLEMAX + 1];
    int dx, dy;
    int dir, diff, sl, dc, d, d2, d1, dm;
    uint16_t pattern;
} upd7220_t;

extern void upd7220_init(upd7220_t *dev, void *priv,
                         uint8_t (*vram_read)(uint32_t addr, void *priv),
                         void (*vram_write)(uint32_t addr, uint8_t val, void *priv));

void    upd7220_param_write(uint16_t addr, uint8_t value, void *priv);
uint8_t upd7220_statreg_read(uint16_t addr, void *priv);
void    upd7220_cmdreg_write(uint16_t addr, uint8_t value, void *priv);
uint8_t upd7220_data_read(uint16_t addr, void *priv);
void    upd7220_reset(upd7220_t *dev);

#    ifdef EMU_DEVICE_H
extern const device_t ati68860_ramdac_device;
extern const device_t ati68875_ramdac_device;
extern const device_t att490_ramdac_device;
extern const device_t att491_ramdac_device;
extern const device_t att492_ramdac_device;
extern const device_t att498_ramdac_device;
extern const device_t av9194_device;
extern const device_t bt484_ramdac_device;
extern const device_t att20c504_ramdac_device;
extern const device_t bt485_ramdac_device;
extern const device_t att20c505_ramdac_device;
extern const device_t bt485a_ramdac_device;
extern const device_t gendac_ramdac_device;
extern const device_t ibm_rgb528_ramdac_device;
extern const device_t ics2494an_305_device;
extern const device_t ati18810_device;
extern const device_t ati18811_0_device;
extern const device_t ati18811_1_device;
extern const device_t ics2595_device;
extern const device_t icd2061_device;
extern const device_t ics9161_device;
extern const device_t sc11483_ramdac_device;
extern const device_t sc11487_ramdac_device;
extern const device_t sc11486_ramdac_device;
extern const device_t sc11484_nors2_ramdac_device;
extern const device_t sc1502x_ramdac_device;
extern const device_t sdac_ramdac_device;
extern const device_t stg_ramdac_device;
extern const device_t tkd8001_ramdac_device;
extern const device_t tseng_ics5301_ramdac_device;
extern const device_t tseng_ics5341_ramdac_device;
extern const device_t tvp3026_ramdac_device;
#    endif

#endif /*VIDEO_UPD7220_H*/
