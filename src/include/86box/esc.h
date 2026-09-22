/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel's PCI-EISA bridge chip set: the 82375EB/SB PCI-EISA
 *          Bridge (PCEB) and the 82374EB/SB EISA System Component (ESC).
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#ifndef EMU_ESC_H
#define EMU_ESC_H

/* The ESC's own configuration space is reached through an index and data
   pair in I/O, and stays shut until told the magic byte. */
#define ESC_CONF_INDEX 0x0022
#define ESC_CONF_DATA  0x0023
#define ESC_UNLOCK_KEY 0x0f

/* Thirty-two pages of configuration RAM, one per slot and then some. */
#define ESC_CRAM_BASE   0x50  /* the firmware reaches the block through this */
#define ESC_CRAM_RECS   0xfc  /* records grow upward from here */
#define ESC_CRAM_RECLEN 12    /* the smallest the services accept */
#define ESC_CRAM_ESCD   0x140 /* and the extended data through this */
#define ESC_CRAM_PAGES  32

extern const device_t esc_device;  /* 82374SB */
extern const device_t pceb_device; /* 82375SB */

/* The board identifier the ESC answers with at 0C80-0C83. A machine sets
   this before the BIOS looks. */
/* SERR# pulsing active, gated by Mode Select bit 3. */
extern void esc_serr(void);

extern void esc_set_embedded_id(const char *mfg, uint16_t product,
                                uint8_t rev);
extern void esc_set_board_id(const char *mfg, uint16_t product, uint8_t rev);

#endif /*EMU_ESC_H*/
