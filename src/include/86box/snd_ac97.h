/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for AC'97 audio emulation.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2021-2025 RichardG.
 */
#ifndef SOUND_AC97_H
#define SOUND_AC97_H

#define AC97_VENDOR_ID(a, b, c, i) ((((a) &0xff) << 24) | (((b) &0xff) << 16) | (((c) &0xff) << 8) | ((i) &0xff))

/* Misc support bits (misc_flags). Most of these are not part of any
   registers, but control enabling/disabling of registers and bits. */
#define AC97_AUDIO      (1 << 0)  /* audio codec */
#define AC97_MODEM      (1 << 1)  /* modem codec */
#define AC97_MASTER_6B  (1 << 2)  /* register 02 bits [13,5] (ML5/MR5) */
#define AC97_AUXOUT     (1 << 3)  /* register 04 */
#define AC97_AUXOUT_6B  (1 << 4)  /* register 04 bits [13,5] (ML5/MR5) */
#define AC97_MONOOUT    (1 << 5)  /* register 06 */
#define AC97_MONOOUT_6B (1 << 6)  /* register 06 bit 5 (MM5) */
#define AC97_PCBEEP     (1 << 9)  /* register 0A */
#define AC97_PCBEEP_GEN (1 << 10) /* register 0A bits [12:5] (F[7:0]) */
#define AC97_PHONE      (1 << 11) /* register 0C */
#define AC97_VIDEO      (1 << 12) /* register 14 */
#define AC97_AUXIN      (1 << 13) /* register 16 */
#define AC97_POP        (1 << 15) /* register 20 bit 15 (POP) - definition shared with General Purpose bits */
#define AC97_MS         (1 << 8)  /* register 20 bit 8 (MS) - definition shared with General Purpose bits */
#define AC97_LPBK       (1 << 7)  /* register 20 bit 7 (LPBK) - definition shared with General Purpose bits */
#define AC97_DSA        (1 << 14) /* register 28 bits [5:4] (DSA[1:0]) */
#define AC97_LFE_6B     (1 << 16) /* register 36 bit 13 (LFE5) */
#define AC97_CENTER_6B  (1 << 17) /* register 36 bit 5 (CNT5) */
#define AC97_SURR_6B    (1 << 18) /* register 38 bits [13,5] (LSR5/RSR5) */
#define AC97_GAIN_3B    (1 << 19) /* registers [1C:1E] (audio) or [46:4A] (modem) bits [8,0] are always 0 (spec violation?) if this is set */

/* Reset bits (reset_flags), register 00. */
#define AC97_MICPCM    (1 << 0)
#define AC97_MODEMLINE (1 << 1)
#define AC97_TONECTL   (1 << 2)
#define AC97_SIMSTEREO (1 << 3)
#define AC97_HPOUT     (1 << 4)
#define AC97_LOUDNESS  (1 << 5)
#define AC97_DAC_18B   (1 << 6)
#define AC97_DAC_20B   (1 << 7)
#define AC97_ADC_18B   (1 << 8)
#define AC97_ADC_20B   (1 << 9)
#define AC97_3D_SHIFT  10

/* Extended Audio ID bits (extid_flags), register 28. */
#define AC97_VRA      (1 << 0)
#define AC97_DRA      (1 << 1)
#define AC97_SPDIF    (1 << 2)
#define AC97_VRM      (1 << 3)
#define AC97_CDAC     (1 << 6)
#define AC97_SDAC     (1 << 7)
#define AC97_LDAC     (1 << 8)
#define AC97_AMAP     (1 << 9)
#define AC97_REV_2_1  (0 << 10)
#define AC97_REV_2_2  (1 << 10)
#define AC97_REV_2_3  (2 << 10)
#define AC97_REV_MASK (3 << 10)

/* Volume bits. */
#define AC97_MUTE   (1 << 15)
#define AC97_MUTE_L (1 << 15)
#define AC97_MUTE_R (1 << 7)

/* General Purpose bits, register 20. */
/* POP already defined */
#define AC97_ST        (1 << 14)
#define AC97_3D        (1 << 13)
#define AC97_LD        (1 << 12)
#define AC97_DRSS_MASK (3 << 10)
#define AC97_MIX       (1 << 9)
/* MS already defined */
/* LPBK already defined */

/* Extended Audio Status/Control bits, register 2A. */
#define AC97_SPSA_SHIFT 4
#define AC97_SPSA_MASK  3
#define AC97_MADC       (1 << 9)
#define AC97_SPCV       (1 << 10)
#define AC97_PRI        (1 << 11)
#define AC97_PRJ        (1 << 12)
#define AC97_PRK        (1 << 13)
#define AC97_PRL        (1 << 14)

/* Extended Modem ID bits, register 3C. */
#define AC97_LIN1 (1 << 0)
#define AC97_LIN2 (1 << 1)
#define AC97_HSET (1 << 2)
#define AC97_CID1 (1 << 3)
#define AC97_CID2 (1 << 4)
#define AC97_CIDR (1 << 7) /* special case: not part of register 3C, but rather 56 bit 13 */

/* Codec IDs. */
#define AC97_CODEC_AD1881   AC97_VENDOR_ID('A', 'D', 'S', 0x40)
#define AC97_CODEC_AK4540   AC97_VENDOR_ID('A', 'D', 'S', 0x40)
#define AC97_CODEC_ALC100   AC97_VENDOR_ID('A', 'L', 'C', 0x20)
#define AC97_CODEC_CS4297   AC97_VENDOR_ID('C', 'R', 'Y', 0x03)
#define AC97_CODEC_CS4297A  AC97_VENDOR_ID('C', 'R', 'Y', 0x11)
#define AC97_CODEC_ICE1232  AC97_VENDOR_ID('I', 'C', 'E', 0x11)
#define AC97_CODEC_STAC9708 AC97_VENDOR_ID(0x83, 0x84, 0x76, 0x08)
#define AC97_CODEC_STAC9721 AC97_VENDOR_ID(0x83, 0x84, 0x76, 0x09)
#define AC97_CODEC_TR28023  AC97_VENDOR_ID('T', 'R', 'A', 0x03)
#define AC97_CODEC_W83971D  AC97_VENDOR_ID('W', 'E', 'C', 0x01)
#define AC97_CODEC_WM9701A  AC97_VENDOR_ID('W', 'M', 'L', 0x00)
#define AC97_CODEC_SI3036   AC97_VENDOR_ID('S', 'I', 'L', 0x22)

typedef struct ac97_vendor_reg_t {
    uint8_t  page; /* for paged registers [60:6F], 0 otherwise */
    uint8_t  index;
    uint16_t value;
    uint16_t write_mask;
} ac97_vendor_reg_t;

typedef struct ac97_codec_t {
    int                      model;
    uint16_t                 regs[64];
    uint8_t                  codec_id;
    uint8_t                  vendor_reg_page_max;
    const ac97_vendor_reg_t *vendor_regs;
    uint16_t                *vendor_reg_pages;
    uint16_t                 gpi;
    uint16_t                 gpo;
} ac97_codec_t;

extern uint16_t        ac97_codec_readw(ac97_codec_t *dev, uint8_t reg);
extern void            ac97_codec_writew(ac97_codec_t *dev, uint8_t reg, uint16_t val);
extern void            ac97_codec_reset(void *priv);
extern void            ac97_codec_getattn(void *priv, uint8_t reg, int *l, int *r);
extern uint32_t        ac97_codec_getrate(void *priv, uint8_t reg);
extern void            ac97_codec_setgpi(void *priv, uint16_t gpi);
extern void            ac97_codec_setgpo(void *priv, uint16_t gpo);
extern const device_t *ac97_codec_get(uint32_t id);

extern void    ac97_via_set_slot(void *priv, int slot, int irq_pin);
extern uint8_t ac97_via_read_status(void *priv);
extern void    ac97_via_write_control(void *priv, uint8_t val);
extern void    ac97_via_remap_audio_sgd(void *priv, uint16_t new_io_base, uint8_t enable);
extern void    ac97_via_remap_modem_sgd(void *priv, uint16_t new_io_base, uint8_t enable);
extern void    ac97_via_remap_audio_codec(void *priv, uint16_t new_io_base, uint8_t enable);
extern void    ac97_via_remap_modem_codec(void *priv, uint16_t new_io_base, uint8_t enable);

extern ac97_codec_t **ac97_codec;
extern int            ac97_codec_count;
extern int            ac97_codec_id;

#ifdef EMU_DEVICE_H
extern const device_t ad1881_device;
extern const device_t ak4540_device;
extern const device_t alc100_device;
extern const device_t cs4297_device;
extern const device_t cs4297a_device;
#    define ct1297_device tr28023_device
extern const device_t ice1232_device;
extern const device_t stac9708_device;
extern const device_t stac9721_device;
extern const device_t tr28023_device;
#    define vt1611a_device ice1232_device
extern const device_t w83971d_device;
extern const device_t wm9701a_device;
#ifdef USE_SOFTMODEM
extern const device_t si3036_device;
#endif

extern const device_t ac97_via_device;
#endif

#endif /*SOUND_AC97_H*/
