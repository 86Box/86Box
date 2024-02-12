/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the EGC graphics processor used by
 *          the NEC PC-98x1 series of computers.
 *
 *
 *
 * Authors: TAKEDA toshiya,
 *          yui/Neko Project II
 *
 *          Copyright 2009-2024 TAKEDA, toshiya.
 *          Copyright 2008-2024 yui/Neko Project II.
 */
#ifndef VIDEO_PC98X1_EGC_H
#    define VIDEO_PC98X1_EGC_H

typedef union {
    uint8_t b[2];
    uint16_t w;
} egcword_t;

typedef union {
    uint8_t b[4][2];
    uint16_t w[4];
    uint32_t d[2];
    uint64_t q;
} egcquad_t;

typedef struct egc_t {
    void *priv;

    uint16_t access;
    uint16_t fgbg;
    uint16_t ope;
    uint16_t fg;
    egcword_t mask;
    uint16_t bg;
    uint16_t sft;
    uint16_t leng;
    egcquad_t lastvram;
    egcquad_t patreg;
    egcquad_t fgc;
    egcquad_t bgc;
    int func;
    uint32_t remain;
    uint32_t stack;
    uint8_t *inptr;
    int inptr_vmstate;
    uint8_t *outptr;
    int outptr_vmstate;
    egcword_t mask2;
    egcword_t srcmask;
    uint8_t srcbit;
    uint8_t dstbit;
    uint8_t sft8bitl;
    uint8_t sft8bitr;
    uint8_t buf[528];	/* 4096/8 + 4*4 */

    /* vram */
    uint8_t *vram_ptr;
    uint8_t *vram_b;
    uint8_t *vram_r;
    uint8_t *vram_g;
    uint8_t *vram_e;
    egcquad_t vram_src;
    egcquad_t vram_data;
} egc_t;

extern void     egc_mem_writeb(egc_t *dev, uint32_t addr1, uint8_t value);
extern void     egc_mem_writew(egc_t *dev, uint32_t addr1, uint16_t value);
extern uint8_t  egc_mem_readb(egc_t *dev, uint32_t addr1);
extern uint16_t egc_mem_readw(egc_t *dev, uint32_t addr1);
extern void     egc_set_vram(egc_t *dev, uint8_t *vram_ptr);
extern void     egc_reset(egc_t *dev);
extern void     egc_init(egc_t *dev, void *priv);

#endif /*VIDEO_PC98X1_EGC_H*/
