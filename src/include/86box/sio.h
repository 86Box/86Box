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

/* SM(S)C */
extern const device_t fdc37c651_device;
extern const device_t fdc37c651_ide_device;
extern const device_t fdc37c661_device;
extern const device_t fdc37c661_ide_device;
extern const device_t fdc37c661_ide_sec_device;
extern const device_t fdc37c663_device;
extern const device_t fdc37c663_ide_device;
extern const device_t fdc37c665_device;
extern const device_t fdc37c665_ide_device;
extern const device_t fdc37c665_ide_pri_device;
extern const device_t fdc37c665_ide_sec_device;
extern const device_t fdc37c666_device;

extern const device_t fdc37c669_device;
extern const device_t fdc37c669_370_device;

extern const device_t fdc37c67x_device;

#define FDC37C93X_NORMAL     0x0002
#define FDC37C93X_FR         0x0003
#define FDC37C93X_APM        0x0030
#define FDC37C93X_CHIP_ID    0x00ff

#define FDC37C931            0x0100    /* Compaq KBC firmware and configuration registers on GPIO ports. */
#define FDC37C932            0x0200    /* AMI '5' Megakey KBC firmware. */
#define FDC37C933            0x0300    /* IBM KBC firmware. */
#define FDC37C935            0x0500    /* Phoenix Multikey/42 1.38 KBC firmware. */
#define FDC37C937            0x0700    /* Phoenix Multikey/42i 4.16 KBC firmware. */
#define FDC37C93X_KBC        0x0f00

#define FDC37C93X_NO_NVR     0x1000
#define FDC37C93X_370        0x2000

extern const device_t fdc37c93x_device;

extern const device_t fdc37m60x_device;

/* ITE */
extern const device_t it8661f_device;
extern const device_t it8671f_device;

/* Intel */
extern const device_t i82091aa_device;
extern const device_t i82091aa_26e_device;
extern const device_t i82091aa_398_device;
extern const device_t i82091aa_ide_pri_device;
extern const device_t i82091aa_ide_device;

/* National Semiconductors PC87310 / ALi M5105 */
#define PC87310_IDE          0x0001
#define PC87310_ALI          0x0002

extern const device_t pc87310_device;

/* National Semiconductors */
extern const device_t pc87306_device;
extern const device_t pc87311_device;
extern const device_t pc87311_ide_device;
extern const device_t pc87332_device;
extern const device_t pc87332_398_device;
extern const device_t pc87332_398_ide_device;
extern const device_t pc87332_398_ide_sec_device;
extern const device_t pc87332_398_ide_fdcon_device;

/* National Semiconductors PC87307 / PC87309 */
#define PCX7307_PC87307      0x00c0
#define PCX7307_PC97307      0x00cf

#define PC87309_PC87309      0x00e0

#define PCX730X_CHIP_ID      0x00ff

#define PCX730X_AMI          0x0200    /* AMI '5' Megakey KBC firmware. */
#define PCX730X_PHOENIX_42   0x0500    /* Phoenix Multikey/42 1.37 KBC firmware. */
#define PCX730X_PHOENIX_42I  0x0700    /* Phoenix Multikey/42i 4.16 KBC firmware. */
#define PCX730X_KBC          0x0f00

#define PCX730X_15C          0x2000

extern const device_t pc87307_device;

extern const device_t pc87309_device;

/* LG Prime */
extern const device_t prime3b_device;
extern const device_t prime3b_ide_device;
extern const device_t prime3c_device;
extern const device_t prime3c_ide_device;

/* IBM PS/1 */
extern const device_t ps1_m2133_sio;

/* Super I/O Detect */
#ifdef USE_SIO_DETECT
extern const device_t sio_detect_device;
#endif /* USE_SIO_DETECT */

/* UMC */
extern const device_t um82c862f_device;
extern const device_t um82c862f_ide_device;
extern const device_t um82c863f_device;
extern const device_t um82c863f_ide_device;
extern const device_t um8663af_device;
extern const device_t um8663af_ide_device;
extern const device_t um8663af_sec_device;
extern const device_t um8663bf_device;
extern const device_t um8663bf_ide_device;
extern const device_t um8663bf_sec_device;

extern const device_t um8669f_device;
extern const device_t um8669f_ide_device;
extern const device_t um8669f_ide_sec_device;

/* VIA */
extern void vt82c686_sio_write(uint8_t addr, uint8_t val, void *priv);

extern const device_t via_vt82c686_sio_device;

/* VLSI */
extern const device_t vl82c113_device;

/* Winbond */
extern const device_t w83787f_88h_device;
extern const device_t w83787f_device;
extern const device_t w83787f_ide_device;
extern const device_t w83787f_ide_en_device;
extern const device_t w83787f_ide_sec_device;

extern const device_t w83877f_device;
extern const device_t w83877f_president_device;
extern const device_t w83877tf_device;
extern const device_t w83877tf_acorp_device;

#define TYPE_W83977EF    0x52F0
#define TYPE_W83977F     0x9771
#define TYPE_W83977TF    0x9773
#define TYPE_W83977ATF   0x9774

extern const device_t w83977f_device;
extern const device_t w83977f_370_device;
extern const device_t w83977tf_device;
extern const device_t w83977ef_device;
extern const device_t w83977ef_370_device;

#endif /*EMU_SIO_H*/
