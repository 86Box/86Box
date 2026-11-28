/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Texas Instruments TMS320C25 digital signal processor emulation
 *          Used in the IBM M-ACPA/A
 *
 * Authors: NexGenDriven, <nxdcaptain@gmail.com>
 *
 *          Copyright 2026 NexGenDriven (nxdcaptain).
 */
#ifndef EMU_TMS320C25_H
#define EMU_TMS320C25_H

#include <stdint.h>

#define C25_INT0 0
#define C25_INT1 1
#define C25_INT2 2
#define C25_TINT 3
#define C25_RINT 4
#define C25_XINT 5

typedef struct c25_t {
    /* CPU registers */
    uint16_t pc;
    int32_t  acc;
    int32_t  p;
    uint16_t t;
    uint16_t ar[8];
    uint16_t stack[8];
    uint8_t  sp;
    uint8_t  arp;
    uint8_t  arb;
    uint16_t dp;
    uint8_t  pm;
    uint8_t  ov;
    uint8_t  ovm;
    uint8_t  intm;
    uint8_t  cnf;
    uint8_t  tc;
    uint8_t  sxm;
    uint8_t  c;
    uint8_t  xf;
    uint8_t  fo;
    uint8_t  txm;
    uint8_t  hm;
    uint8_t  fsm;

    /* Memory registers */
    uint16_t drr;
    uint16_t dxr;
    uint16_t tim;
    uint16_t prd;
    uint16_t imr;
    uint16_t greg;
    uint16_t ifr;

    /* Repeat counter */
    uint16_t rptc;
    uint8_t  rpt_pending;

    /* Execution state */
    uint8_t  idle;
    uint8_t  eint_delay;
    uint8_t  iter;
    uint16_t unimpl;
    uint16_t unimpl_pc;
    uint16_t npc;
    int      cycles_done;
    int      cycles_left;

    /* RAM */
    uint16_t b0[256];
    uint16_t b1[256];
    uint16_t b2[32];

    /* Bus */
    void     *priv;
    uint16_t (*prog_read)(void *priv, uint16_t addr);
    void     (*prog_write)(void *priv, uint16_t addr, uint16_t val);
    uint16_t (*data_read)(void *priv, uint16_t addr);
    void     (*data_write)(void *priv, uint16_t addr, uint16_t val);
    uint16_t (*io_read)(void *priv, uint8_t port);
    void     (*io_write)(void *priv, uint8_t port, uint16_t val);
    int      (*bio)(void *priv); /* BIO pin level, BIOZ branches on 0 */
} c25_t;

extern void c25_reset(c25_t *c);
extern int  c25_run(c25_t *c, int cycles);
extern void c25_set_int(c25_t *c, int irq);
extern void c25_clear_int(c25_t *c, int irq);
extern uint16_t c25_get_st0(c25_t *c);
extern uint16_t c25_get_st1(c25_t *c);

#endif /*EMU_TMS320C25_H*/
