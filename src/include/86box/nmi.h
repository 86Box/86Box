/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#ifndef EMU_NMI_H
#define EMU_NMI_H

extern int nmi_mask;
extern int nmi;
extern int nmi_auto_clear;

/* Told when the NMI enable (port 70h bit 7 on an AT) changes, for a board
   whose NMI logic latches its sources and re-evaluates its output then. */
extern void (*nmi_mask_changed)(void);
/* The board's NMI line as a level, where the board has one (the ESC's
   output); NULL where NMI is only ever an edge. */
extern int (*nmi_line_level)(void);

extern void nmi_init(void);

extern void nmi_write(uint16_t port, uint8_t val, void *priv);

#endif /*EMU_NMI_H*/
