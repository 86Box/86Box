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

#define GDC_BUFFERS     1024
#define GDC_TABLEMAX    0x1000

enum {
    GDC_CMD_RESET    = 0x00,
    GDC_CMD_SYNC     = 0x0e,
    GDC_CMD_SLAVE    = 0x6e,
    GDC_CMD_MASTER   = 0x6f,
    GDC_CMD_START    = 0x6b,
    GDC_CMD_BCTRL    = 0x0c,
    GDC_CMD_ZOOM     = 0x46,
    GDC_CMD_SCROLL   = 0x70,
    GDC_CMD_CSRFORM  = 0x4b,
    GDC_CMD_PITCH    = 0x47,
    GDC_CMD_LPEN     = 0xc0,
    GDC_CMD_VECTW    = 0x4c,
    GDC_CMD_VECTE    = 0x6c,
    GDC_CMD_TEXTW    = 0x78,
    GDC_CMD_TEXTE    = 0x68,
    GDC_CMD_CSRW     = 0x49,
    GDC_CMD_CSRR     = 0xe0,
    GDC_CMD_MASK     = 0x4a,
    GDC_CMD_WRITE    = 0x20,
    GDC_CMD_READ     = 0xa0,
    GDC_CMD_DMAR     = 0xa4,
    GDC_CMD_DMAW     = 0x24,
    /* unknown command (3 params) */
    GDC_CMD_UNK_5A   = 0x5a,
};

enum {
    GDC_STAT_DRDY    = 0x01,
    GDC_STAT_FULL    = 0x02,
    GDC_STAT_EMPTY   = 0x04,
    GDC_STAT_DRAW    = 0x08,
    GDC_STAT_DMA     = 0x10,
    GDC_STAT_VSYNC   = 0x20,
    GDC_STAT_HBLANK  = 0x40,
    GDC_STAT_LPEN    = 0x80,
};

enum {
    GDC_DIRTY_VRAM   = 0x01,
    GDC_DIRTY_START  = 0x02,
    GDC_DIRTY_SCROLL = 0x04,
    GDC_DIRTY_CURSOR = 0x08,
    GDC_DIRTY_GFX    = GDC_DIRTY_VRAM | GDC_DIRTY_SCROLL,
    GDC_DIRTY_CHR    = GDC_DIRTY_GFX | GDC_DIRTY_CURSOR,
};


#define GDC_VTICKS   18
#define GDC_VSTICKS  2

#define GDC_MULBIT   15
#define GDC_TABLEBIT 12

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

extern void    upd7220_init(upd7220_t *dev, void *priv,
                            uint8_t (*vram_read)(uint32_t addr, void *priv),
                            void (*vram_write)(uint32_t addr, uint8_t val, void *priv));

extern void    upd7220_recalctimings(upd7220_t *dev);
extern void    upd7220_write(uint16_t addr, uint8_t value, void *priv);
extern uint8_t upd7220_read(uint16_t addr, void *priv);
extern void    upd7220_reset(upd7220_t *dev);

#endif /*VIDEO_UPD7220_H*/
