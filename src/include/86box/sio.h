/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Super I/O chips.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2020 Fred N. van Kempen.
 */
#ifndef EMU_SIO_H
#define EMU_SIO_H

/* ACC Micro */
extern const device_t acc3221_device;

/* Acer / ALi */
extern const device_t ali5123_device;

/* Chips & Technologies */
extern const device_t f82c606_device;

extern const device_t f82c710_device;
extern const device_t f82c710_pc5086_device;

extern const device_t p82c604_device;

/* Commodore */
extern const device_t cbm_io_device;

/* Dataworld 90C50 (COMBAT) */
#define DW90C50_IDE          0x00001

extern const device_t dw90c50_device;

extern const device_t pc87310_device;
/* SM(S)C */
#define FDC37C651            0x00051
#define FDC37C661            0x00061
#define FDC37C663            0x00063
#define FDC37C665            0x00065
#define FDC37C666            0x00066

#define FDC37C6XX_IDE_PRI    0x00100
#define FDC37C6XX_IDE_SEC    0x00200

#define FDC37C6XX_370        0x00400

extern const device_t fdc37c6xx_device;

extern const device_t fdc37c669_device;

#define FDC37C93X_NORMAL     0x00002
#define FDC37C93X_FR         0x00003
#define FDC37C93X_APM        0x00030
#define FDC37C93X_CHIP_ID    0x000ff

#define FDC37XXX1            0x00100    /* Compaq KBC firmware and configuration registers on GPIO ports. */
#define FDC37XXX2            0x00200    /* AMI '5' Megakey KBC firmware. */
#define FDC37XXX3            0x00300    /* IBM KBC firmware. */
#define FDC37XXX5            0x00500    /* Phoenix Multikey/42 1.38 KBC firmware. */
#define FDC37XXX7            0x00700    /* Phoenix Multikey/42i 4.16 KBC firmware. */
#define FDC37XXXX_KBC        0x00f00

#define FDC37C93X_NO_NVR     0x01000
#define FDC37XXXX_370        0x02000

extern const device_t fdc37c93x_device;

extern const device_t fdc37m60x_device;

extern const device_t fdc37c67x_device;

/* ITE */
extern const device_t it8661f_device;
extern const device_t it8671f_device;

/* Intel */
#define I82091AA_022         0x00000    /* Default. */
#define I82091AA_024         0x00008
#define I82091AA_26E         0x00100
#define I82091AA_398         0x00108

#define I82091AA_IDE_PRI     0x00200
#define I82091AA_IDE_SEC     0x00400

extern const device_t i82091aa_device;

/* National Semiconductors PC87310 / ALi M5105 */
#define PCX73XX_IDE          0x00001

#define PCX73XX_IDE_PRI      PCX73XX_IDE
#define PCX73XX_IDE_SEC      0x00002

#define PCX73XX_FDC_ON       0x10000

#define PC87310_ALI          0x00004
#define PC87332              PC87310_ALI

extern const device_t pc87310_device;

/* National Semiconductors */
#define PCX7307_PC87307      0x000c0
#define PCX7307_PC97307      0x000cf

#define PC87309_PC87309      0x000e0

#define PCX730X_CHIP_ID      0x000ff

#define PCX730X_AMI          0x00200    /* AMI '5' Megakey KBC firmware. */
#define PCX730X_PHOENIX_42   0x00500    /* Phoenix Multikey/42 1.37 KBC firmware. */
#define PCX730X_PHOENIX_42I  0x00700    /* Phoenix Multikey/42i 4.16 KBC firmware. */
#define PCX730X_KBC          0x00f00

#define PCX730X_398          0x00000
#define PCX730X_26E          0x01000
#define PCX730X_15C          0x02000
#define PCX730X_02E          0x03000
#define PCX730X_BADDR        0x03000
#define PCX730X_BADDR_SHIFT       12

extern const device_t pc87306_device;

extern const device_t pc873xx_device;

/* National Semiconductors PC87307 / PC87309 */
extern const device_t pc87307_device;

extern const device_t pc87309_device;

/* LG Prime */
#define GM82C803A            0x00000
#define GM82C803B            0x00001

#define GM82C803_IDE_PRI     0x00100
#define GM82C803_IDE_SEC     0x00200

extern const device_t gm82c803ab_device;

extern const device_t gm82c803c_device;

/* IBM PS/1 */
extern const device_t ps1_m2133_sio;

/* Super I/O Detect */
#ifdef USE_SIO_DETECT
extern const device_t sio_detect_device;
#endif /* USE_SIO_DETECT */

/* UMC */
#define UM82C862F            0x00000
#define UM82C863F            0x0c100
#define UM8663AF             0x0c300
#define UM8663BF             0x0c400

#define UM866X_IDE_PRI       0x00001
#define UM866X_IDE_SEC       0x00002

extern const device_t um866x_device;

extern const device_t um8669f_device;

/* VIA */
extern void vt82c686_sio_write(uint8_t addr, uint8_t val, void *priv);

extern const device_t via_vt82c686_sio_device;

/* VLSI */
extern const device_t vl82c113_device;

/* Winbond */
#define W83777F              0x00007
#define W83787F              0x00008
#define W83787IF             0x00009

#define W837X7_KEY_88        0x00000
#define W837X7_KEY_89        0x00020

#define W837X7_IDE_START     0x00040

#define W83XX7_IDE_PRI       0x10000
#define W83XX7_IDE_SEC       0x20000

extern const device_t w837x7_device;

#define W83877F              0x00a00
#define W83877TF             0x00c00
#define W83877_3F0           0x00005
#define W83877_250           0x00004

extern const device_t w83877_device;

#define W83977F             0x977100
#define W83977TF            0x977300
#define W83977EF            0x52f000
#define W83977_TYPE         0xffff00
#define W83977_TYPE_SHIFT          8

#define W83977_3F0           0x00000
#define W83977_370           0x00001

#define W83977_NO_NVR        0x00002

#define W83977_AMI           0x00010    /* AMI 'H' KBC firmware. */
#define W83977_PHOENIX       0x00020    /* Unknown Phoenix Multikey KBC firmware. */

#define W83977_KBC           0x000f0

extern const device_t w83977_device;

#endif /*EMU_SIO_H*/
