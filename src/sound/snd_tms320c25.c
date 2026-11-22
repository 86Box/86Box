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
#include <stdint.h>
#include <string.h>
#include "86box/snd_tms320c25.h"

#define DREG_DRR  0
#define DREG_DXR  1
#define DREG_TIM  2
#define DREG_PRD  3
#define DREG_IMR  4
#define DREG_GREG 5

static uint16_t
rd(c25_t *c, uint16_t a)
{
    if (a < 6) {
        switch (a) {
            case DREG_DRR:  return c->drr;
            case DREG_DXR:  return c->dxr;
            case DREG_TIM:  return c->tim;
            case DREG_PRD:  return c->prd;
            case DREG_IMR:  return c->imr | 0xffc0;
            default:        return c->greg | 0xff00;
        }
    }
    if ((a >= 0x60) && (a < 0x80))
        return c->b2[a - 0x60];
    if ((a >= 0x200) && (a < 0x300) && !c->cnf)
        return c->b0[a - 0x200];
    if ((a >= 0x300) && (a < 0x400))
        return c->b1[a - 0x300];
    if (a < 0x400)
        return 0;
    return c->data_read ? c->data_read(c->priv, a) : 0;
}

static void
wr(c25_t *c, uint16_t a, uint16_t v)
{
    if (a < 6) {
        switch (a) {
            case DREG_DRR:  c->drr = v; break;
            case DREG_DXR:  c->dxr = v; break;
            case DREG_TIM:  c->tim = v; break;
            case DREG_PRD:  c->prd = v; break;
            case DREG_IMR:  c->imr = v & 0x3f; break;
            default:        c->greg = v & 0xff; break;
        }
        return;
    }
    if ((a >= 0x60) && (a < 0x80))
        c->b2[a - 0x60] = v;
    else if ((a >= 0x200) && (a < 0x300) && !c->cnf)
        c->b0[a - 0x200] = v;
    else if ((a >= 0x300) && (a < 0x400))
        c->b1[a - 0x300] = v;
    else if ((a >= 0x400) && c->data_write)
        c->data_write(c->priv, a, v);
}

static uint16_t
prd_(c25_t *c, uint16_t a)
{
    if (c->cnf && (a >= 0xff00))
        return c->b0[a & 0xff];
    return c->prog_read ? c->prog_read(c->priv, a) : 0;
}

static void
pwr(c25_t *c, uint16_t a, uint16_t v)
{
    if (c->cnf && (a >= 0xff00))
        c->b0[a & 0xff] = v;
    else if (c->prog_write)
        c->prog_write(c->priv, a, v);
}

static uint16_t
brev(uint16_t v)
{
    uint16_t r = 0;
    for (int i = 0; i < 16; i++)
        if (v & (1 << i))
            r |= (1 << (15 - i));
    return r;
}

static uint16_t
ea(c25_t *c, uint16_t op)
{
    if (!(op & 0x80))
        return (uint16_t) ((c->dp << 7) | (op & 0x7f));
    return c->ar[c->arp];
}

static void
post(c25_t *c, uint16_t op)
{
    if (!(op & 0x80))
        return;
    uint16_t *a = &c->ar[c->arp];
    switch ((op >> 4) & 7) {
        case 1: (*a)--; break;
        case 2: (*a)++; break;
        case 4: *a = brev((uint16_t) (brev(*a) - brev(c->ar[0]))); break;
        case 5: *a -= c->ar[0]; break;
        case 6: *a += c->ar[0]; break;
        case 7: *a = brev((uint16_t) (brev(*a) + brev(c->ar[0]))); break;
        default: break;
    }
    if (op & 8)
        c->arp = op & 7;
}

static inline int32_t
ext(c25_t *c, uint16_t d)
{
    return c->sxm ? (int32_t) (int16_t) d : (int32_t) d;
}

static void
sat(c25_t *c, int64_t r)
{
    if ((r > INT32_MAX) || (r < INT32_MIN)) {
        c->ov = 1;
        if (c->ovm)
            c->acc = (r > 0) ? INT32_MAX : INT32_MIN;
        else
            c->acc = (int32_t) (uint32_t) r;
    } else
        c->acc = (int32_t) r;
}

static void
alu_add(c25_t *c, int32_t v, int cin)
{
    const uint64_t u = (uint64_t) (uint32_t) c->acc + (uint64_t) (uint32_t) v + (uint64_t) cin;
    const int64_t  r = (int64_t) c->acc + (int64_t) v + cin;
    c->c = (u >> 32) & 1;
    sat(c, r);
}

static void
alu_sub(c25_t *c, int32_t v, int borrow)
{
    const int64_t r = (int64_t) c->acc - (int64_t) v - borrow;
    c->c = ((uint64_t) (uint32_t) c->acc >= ((uint64_t) (uint32_t) v + borrow));
    sat(c, r);
}

static int32_t
pshift(c25_t *c)
{
    switch (c->pm) {
        case 0: return c->p;
        case 1: return (int32_t) ((uint32_t) c->p << 1);
        case 2: return (int32_t) ((uint32_t) c->p << 4);
        default: return c->p >> 6;
    }
}

uint16_t
c25_get_st0(c25_t *c)
{
    return (uint16_t) ((c->arp << 13) | (c->ov << 12) | (c->ovm << 11) | 0x0400 | (c->intm << 9) | (c->dp & 0x1ff));
}

uint16_t
c25_get_st1(c25_t *c)
{
    return (uint16_t) ((c->arb << 13) | (c->cnf << 12) | (c->tc << 11) | (c->sxm << 10) | (c->c << 9)
                       | 0x0180 | (c->hm << 6) | (c->fsm << 5) | (c->xf << 4) | (c->fo << 3)
                       | (c->txm << 2) | (c->pm & 3));
}

static void
set_st1(c25_t *c, uint16_t v)
{
    c->arb = (v >> 13) & 7;
    c->cnf = (v >> 12) & 1;
    c->tc  = (v >> 11) & 1;
    c->sxm = (v >> 10) & 1;
    c->c   = (v >> 9) & 1;
    c->hm  = (v >> 6) & 1;
    c->fsm = (v >> 5) & 1;
    c->xf  = (v >> 4) & 1;
    c->fo  = (v >> 3) & 1;
    c->txm = (v >> 2) & 1;
    c->pm  = v & 3;
}

static void
push(c25_t *c, uint16_t v)
{
    for (int i = 7; i > 0; i--)
        c->stack[i] = c->stack[i - 1];
    c->stack[0] = v;
}

static uint16_t
pop(c25_t *c)
{
    const uint16_t v = c->stack[0];
    for (int i = 0; i < 7; i++)
        c->stack[i] = c->stack[i + 1];
    return v;
}

void
c25_reset(c25_t *c)
{
    memset(c->ar, 0, sizeof(c->ar));
    memset(c->stack, 0, sizeof(c->stack));
    c->pc = 0;
    c->acc = 0;
    c->p = 0;
    c->t = 0;
    c->arp = c->arb = 0;
    c->dp = 0;
    c->pm = 0;
    c->ov = c->ovm = 0;
    c->intm = 1;
    c->cnf = 0;
    c->tc = 0;
    c->sxm = 1;
    c->c = 1;
    c->xf = 1;
    c->fo = 0;
    c->txm = 1;
    c->hm = 0;
    c->fsm = 1;
    c->imr = 0;
    c->ifr = 0;
    c->tim = 0xffff;
    c->prd = 0xffff;
    c->greg = 0;
    c->rptc = 0;
    c->rpt_pending = 0;
    c->idle = 0;
    c->eint_delay = 0;
}

void
c25_set_int(c25_t *c, int irq)
{
    c->ifr |= (1 << irq);
}

void
c25_clear_int(c25_t *c, int irq)
{
    c->ifr &= ~(1 << irq);
}

static const uint8_t int_order[6]  = { C25_INT0, C25_INT1, C25_INT2, C25_TINT, C25_RINT, C25_XINT };
static const uint8_t int_vector[6] = { 0x02, 0x04, 0x06, 0x18, 0x1a, 0x1c };

static void
take_int(c25_t *c)
{
    const uint16_t pend = c->ifr & c->imr & 0x3f;

    for (int i = 0; i < 6; i++) {
        if (pend & (1 << int_order[i])) {
            c->ifr &= ~(1 << int_order[i]);
            push(c, c->pc);
            c->intm = 1;
            c->pc   = int_vector[i];
            return;
        }
    }
}

#define D()  rd(c, ea(c, op))

static int
exec(c25_t *c, uint16_t op, uint16_t w2, uint16_t pc0)
{
    const uint16_t hi  = op >> 8;
    const uint8_t  lo  = op & 0xff;
    int            cyc = 1;
    uint16_t       d;
    uint16_t       a;
    int            len = 1;
    int            first = (c->iter == 0);

    c->npc = pc0 + 1;

    switch (hi) {
        case 0x00 ... 0x0f: /* ADD */
            d = D(); alu_add(c, ext(c, d) * (1 << (hi & 15)), 0); post(c, op); break;
        case 0x10 ... 0x1f: /* SUB */
            d = D(); alu_sub(c, ext(c, d) * (1 << (hi & 15)), 0); post(c, op); break;
        case 0x20 ... 0x2f: /* LAC */
            d = D(); c->acc = (int32_t) ((uint32_t) ext(c, d) << (hi & 15)); post(c, op); break;
        case 0x30 ... 0x37: /* LAR */
            d = D(); post(c, op); c->ar[hi & 7] = d; break;
        case 0x38: d = D(); c->p = (int32_t) (int16_t) c->t * (int32_t) (int16_t) d; post(c, op); break; /* MPY */
        case 0x39: /* SQRA */
            d = D(); alu_add(c, pshift(c), 0); c->t = d; c->p = (int32_t) (int16_t) d * (int32_t) (int16_t) d; post(c, op); break;
        case 0x3a: /* MPYA */
            d = D(); alu_add(c, pshift(c), 0); c->p = (int32_t) (int16_t) c->t * (int32_t) (int16_t) d; post(c, op); break;
        case 0x3b: /* MPYS */
            d = D(); alu_sub(c, pshift(c), 0); c->p = (int32_t) (int16_t) c->t * (int32_t) (int16_t) d; post(c, op); break;
        case 0x3c: d = D(); c->t = d; post(c, op); break; /* LT */
        case 0x3d: d = D(); alu_add(c, pshift(c), 0); c->t = d; post(c, op); break; /* LTA */
        case 0x3e: d = D(); c->acc = pshift(c); c->t = d; post(c, op); break;       /* LTP */
        case 0x3f: /* LTD */
            a = ea(c, op); d = rd(c, a); alu_add(c, pshift(c), 0); c->t = d; wr(c, a + 1, d); post(c, op); break;

        case 0x40: d = D(); c->acc = (int32_t) ((uint32_t) d << 16); post(c, op); break;           /* ZALH */
        case 0x41: d = D(); c->acc = (int32_t) d; post(c, op); break;                               /* ZALS */
        case 0x42: d = D(); c->acc = (int32_t) ext(c, d) * (1 << (c->t & 15)); post(c, op); break;  /* LACT */
        case 0x43: d = D(); alu_add(c, (int32_t) d, c->c); post(c, op); break;                      /* ADDC */
        case 0x44: d = D(); alu_sub(c, (int32_t) ((uint32_t) d << 16), 0); post(c, op); break;     /* SUBH */
        case 0x45: d = D(); alu_sub(c, (int32_t) d, 0); post(c, op); break;                         /* SUBS */
        case 0x46: d = D(); alu_sub(c, ext(c, d) * (1 << (c->t & 15)), 0); post(c, op); break;      /* SUBT */
        case 0x47: { /* SUBC */
            d = D();
            const int64_t r = (int64_t) c->acc - ((int64_t) d << 15);
            if (r >= 0)
                c->acc = (int32_t) (((uint32_t) r << 1) + 1);
            else
                c->acc = (int32_t) ((uint32_t) c->acc << 1);
            post(c, op);
            break;
        }
        case 0x48: d = D(); alu_add(c, (int32_t) ((uint32_t) d << 16), 0); post(c, op); break;     /* ADDH */
        case 0x49: d = D(); alu_add(c, (int32_t) d, 0); post(c, op); break;                         /* ADDS */
        case 0x4a: d = D(); alu_add(c, ext(c, d) * (1 << (c->t & 15)), 0); post(c, op); break;      /* ADDT */
        case 0x4b: d = D(); c->rptc = d & 0xff; c->rpt_pending = 1; post(c, op); break;             /* RPT */
        case 0x4c: d = D(); c->acc = (int32_t) ((uint32_t) c->acc ^ d); post(c, op); break;         /* XOR */
        case 0x4d: d = D(); c->acc = (int32_t) ((uint32_t) c->acc | d); post(c, op); break;         /* OR */
        case 0x4e: d = D(); c->acc = (int32_t) ((uint32_t) c->acc & d); post(c, op); break;         /* AND */
        case 0x4f: d = D(); alu_sub(c, (int32_t) d, !c->c); post(c, op); break;                     /* SUBB */

        case 0x50: { /* LST */
            d = D();
            const uint8_t oldarp = c->arp;
            const uint8_t intm   = c->intm;
            c->arp  = (d >> 13) & 7;
            c->ov   = (d >> 12) & 1;
            c->ovm  = (d >> 11) & 1;
            c->dp   = d & 0x1ff;
            c->intm = intm;
            c->arb  = oldarp;
            post(c, op);
            break;
        }
        case 0x51: { /* LST1 */
            d = D();
            set_st1(c, d);
            c->arp = c->arb;
            post(c, op);
            break;
        }
        case 0x52: d = D(); c->dp = d & 0x1ff; post(c, op); break;                                  /* LDP */
        case 0x53: d = D(); c->p = (int32_t) (((uint32_t) d << 16) | ((uint32_t) c->p & 0xffff)); post(c, op); break; /* LPH */
        case 0x54: d = D(); push(c, d); post(c, op); break;                                         /* PSHD */
        case 0x55: post(c, op); break;                                                              /* MAR / NOP */
        case 0x56: a = ea(c, op); d = rd(c, a); wr(c, a + 1, d); post(c, op); break;               /* DMOV */
        case 0x57: d = D(); c->tc = (d >> (15 - (c->t & 15))) & 1; post(c, op); break;             /* BITT */
        case 0x58: /* TBLR */
            d = prd_(c, (uint16_t) (c->acc + c->iter)); wr(c, ea(c, op), d); post(c, op);
            cyc = first ? 3 : 1; break;
        case 0x59: /* TBLW */
            d = D(); pwr(c, (uint16_t) (c->acc + c->iter), d); post(c, op);
            cyc = first ? 3 : 1; break;
        case 0x5a: /* SQRS */
            d = D(); alu_sub(c, pshift(c), 0); c->t = d; c->p = (int32_t) (int16_t) d * (int32_t) (int16_t) d; post(c, op); break;
        case 0x5b: d = D(); alu_sub(c, pshift(c), 0); c->t = d; post(c, op); break;                 /* LTS */
        case 0x5c: /* MACD */
        case 0x5d: { /* MAC */
            len = 2;
            a   = ea(c, op);
            d   = rd(c, a);
            alu_add(c, pshift(c), 0);
            c->t = d;
            if (hi == 0x5c)
                wr(c, a + 1, d);
            c->p = (int32_t) (int16_t) d * (int32_t) (int16_t) prd_(c, (uint16_t) (w2 + c->iter));
            post(c, op);
            cyc = first ? 3 : 1;
            break;
        }
        case 0x5e: len = 2; cyc = 2; if (c->c) c->npc = w2; post(c, op); break;                     /* BC */
        case 0x5f: len = 2; cyc = 2; if (!c->c) c->npc = w2; post(c, op); break;                    /* BNC */

        case 0x60 ... 0x67: wr(c, ea(c, op), (uint16_t) ((uint32_t) c->acc << (hi & 7))); post(c, op); break;               /* SACL */
        case 0x68 ... 0x6f: wr(c, ea(c, op), (uint16_t) (((uint32_t) c->acc << (hi & 7)) >> 16)); post(c, op); break;      /* SACH */
        case 0x70 ... 0x77: a = ea(c, op); wr(c, a, c->ar[hi & 7]); post(c, op); break;                                    /* SAR */
        case 0x78: wr(c, (op & 0x80) ? ea(c, op) : (op & 0x7f), c25_get_st0(c)); post(c, op); break;                       /* SST */
        case 0x79: wr(c, (op & 0x80) ? ea(c, op) : (op & 0x7f), c25_get_st1(c)); post(c, op); break;                       /* SST1 */
        case 0x7a: wr(c, ea(c, op), pop(c)); post(c, op); break;                                                           /* POPD */
        case 0x7b: d = D(); c->acc = (int32_t) (((uint32_t) d << 16) | 0x8000); post(c, op); break;                        /* ZALR */
        case 0x7c: wr(c, ea(c, op), (uint16_t) pshift(c)); post(c, op); break;                                             /* SPL */
        case 0x7d: wr(c, ea(c, op), (uint16_t) ((uint32_t) pshift(c) >> 16)); post(c, op); break;                          /* SPH */
        case 0x7e: c->ar[c->arp] += lo; break;                                                                             /* ADRK */
        case 0x7f: c->ar[c->arp] -= lo; break;                                                                             /* SBRK */

        case 0x80 ... 0x8f: /* IN */
            wr(c, ea(c, op), c->io_read ? c->io_read(c->priv, hi & 15) : 0xffff); post(c, op); cyc = 2; break;
        case 0x90 ... 0x9f: d = D(); c->tc = (d >> (15 - (hi & 15))) & 1; post(c, op); break;                              /* BIT */
        case 0xa0 ... 0xbf: { /* MPYK */
            int32_t k = (int32_t) (op & 0x1fff);
            if (k & 0x1000)
                k -= 0x2000;
            c->p = (int32_t) (int16_t) c->t * k;
            break;
        }
        case 0xc0 ... 0xc7: c->ar[hi & 7] = lo; break;                                                                      /* LARK */
        case 0xc8: case 0xc9: c->dp = op & 0x1ff; break;                                                                    /* LDPK */
        case 0xca: c->acc = lo; break;                                                                                      /* LACK / ZAC */
        case 0xcb: c->rptc = lo; c->rpt_pending = 1; break;                                                                 /* RPTK */
        case 0xcc: alu_add(c, lo, 0); break;                                                                                /* ADDK */
        case 0xcd: alu_sub(c, lo, 0); break;                                                                                /* SUBK */
        case 0xcf: d = D(); c->p = (int32_t) ((uint32_t) c->t * (uint32_t) d); post(c, op); break;                          /* MPYU */

        case 0xce:
            if ((op & 0xff80) == 0xce80) { /* NORM */
                if ((((uint32_t) c->acc >> 31) & 1) == (((uint32_t) c->acc >> 30) & 1)) {
                    c->acc = (int32_t) ((uint32_t) c->acc << 1);
                    c->tc = 0;
                } else
                    c->tc = 1;
                post(c, op);
                break;
            }
            switch (lo) {
                case 0x00: c->intm = 0; c->eint_delay = 1; break;                                                           /* EINT */
                case 0x01: c->intm = 1; break;                                                                              /* DINT */
                case 0x02: c->ovm = 0; break;                                                                               /* ROVM */
                case 0x03: c->ovm = 1; break;                                                                               /* SOVM */
                case 0x04: c->cnf = 0; break;                                                                               /* CNFD */
                case 0x05: c->cnf = 1; break;                                                                               /* CNFP */
                case 0x06: c->sxm = 0; break;                                                                               /* RSXM */
                case 0x07: c->sxm = 1; break;                                                                               /* SSXM */
                case 0x08 ... 0x0b: c->pm = lo & 3; break;                                                                  /* SPM */
                case 0x0c: c->xf = 0; break;                                                                                /* RXF */
                case 0x0d: c->xf = 1; break;                                                                                /* SXF */
                case 0x0e: case 0x0f: c->fo = lo & 1; break;                                                                /* FORT */
                case 0x14: c->acc = pshift(c); break;                                                                       /* PAC */
                case 0x15: alu_add(c, pshift(c), 0); break;                                                                 /* APAC */
                case 0x16: alu_sub(c, pshift(c), 0); break;                                                                 /* SPAC */
                case 0x18: c->c = ((uint32_t) c->acc >> 31) & 1; c->acc = (int32_t) ((uint32_t) c->acc << 1); break;        /* SFL */
                case 0x19: c->c = c->acc & 1; c->acc = c->sxm ? (c->acc >> 1) : (int32_t) ((uint32_t) c->acc >> 1); break;  /* SFR */
                case 0x1b: /* ABS */
                    if (c->acc < 0) {
                        if (c->acc == INT32_MIN) {
                            c->ov = 1;
                            c->acc = c->ovm ? INT32_MAX : INT32_MIN;
                        } else
                            c->acc = -c->acc;
                    }
                    break;
                case 0x1c: push(c, (uint16_t) c->acc); break;                                                               /* PUSH */
                case 0x1d: c->acc = (int32_t) (((uint32_t) c->acc & 0xffff0000) | pop(c)); break;                           /* POP */
                case 0x1e: push(c, (uint16_t) (pc0 + 1)); c->npc = 0x1e; cyc = 2; break;                                    /* TRAP */
                case 0x1f: c->idle = 1; break;                                                                              /* IDLE */
                case 0x20: c->txm = 0; break;                                                                               /* RTXM */
                case 0x21: c->txm = 1; break;                                                                               /* STXM */
                case 0x23: /* NEG */
                    if (c->acc == INT32_MIN) {
                        c->ov = 1;
                        c->acc = c->ovm ? INT32_MAX : INT32_MIN;
                    } else
                        c->acc = -c->acc;
                    break;
                case 0x24: push(c, (uint16_t) (pc0 + 1)); c->npc = (uint16_t) c->acc; cyc = 2; break;                       /* CALA */
                case 0x25: c->npc = (uint16_t) c->acc; cyc = 2; break;                                                      /* BACC */
                case 0x26: c->npc = pop(c); cyc = 2; break;                                                                 /* RET */
                case 0x27: c->acc = ~c->acc; break;                                                                         /* CMPL */
                case 0x30: c->c = 0; break;                                                                                 /* RC */
                case 0x31: c->c = 1; break;                                                                                 /* SC */
                case 0x32: c->tc = 0; break;                                                                                /* RTC */
                case 0x33: c->tc = 1; break;                                                                                /* STC */
                case 0x34: { const uint8_t nc = ((uint32_t) c->acc >> 31) & 1; c->acc = (int32_t) (((uint32_t) c->acc << 1) | c->c); c->c = nc; break; } /* ROL */
                case 0x35: { const uint8_t nc = c->acc & 1; c->acc = (int32_t) (((uint32_t) c->acc >> 1) | ((uint32_t) c->c << 31)); c->c = nc; break; } /* ROR */
                case 0x36: c->fsm = 0; break;                                                                               /* RFSM */
                case 0x37: c->fsm = 1; break;                                                                               /* SFSM */
                case 0x38: c->hm = 0; break;                                                                                /* RHM */
                case 0x39: c->hm = 1; break;                                                                                /* SHM */
                case 0x50: c->tc = (c->ar[c->arp] == c->ar[0]); break;                                                      /* CMPR 0 */
                case 0x51: c->tc = (c->ar[c->arp] < c->ar[0]); break;                                                       /* CMPR 1 */
                case 0x52: c->tc = (c->ar[c->arp] > c->ar[0]); break;                                                       /* CMPR 2 */
                case 0x53: c->tc = (c->ar[c->arp] != c->ar[0]); break;                                                      /* CMPR 3 */
                default: c->unimpl = op; c->unimpl_pc = pc0; break;
            }
            break;

        case 0xd0 ... 0xdf: { /* long immediate */
            len = 2;
            cyc = 2;
            const int      sh = hi & 15;
            const int32_t  k  = ext(c, w2);
            switch (lo) {
                case 0x00: if (sh < 8) c->ar[sh] = w2; break;                                                              /* LRLK */
                case 0x01: c->acc = (int32_t) ((uint32_t) k << sh); break;                                                 /* LALK */
                case 0x02: alu_add(c, k * (1 << sh), 0); break;                                                            /* ADLK */
                case 0x03: alu_sub(c, k * (1 << sh), 0); break;                                                            /* SBLK */
                case 0x04: c->acc = (int32_t) ((uint32_t) c->acc & ((uint32_t) w2 << sh)); break;                          /* ANDK */
                case 0x05: c->acc = (int32_t) ((uint32_t) c->acc | ((uint32_t) w2 << sh)); break;                          /* ORK */
                case 0x06: c->acc = (int32_t) ((uint32_t) c->acc ^ ((uint32_t) w2 << sh)); break;                          /* XORK */
                default: c->unimpl = op; c->unimpl_pc = pc0; break;
            }
            break;
        }
        case 0xe0 ... 0xef: /* OUT */
            d = D(); if (c->io_write) c->io_write(c->priv, hi & 15, d); post(c, op); cyc = 2; break;

        case 0xf0 ... 0xff: { /* branches, calls, block moves */
            len = 2;
            cyc = 2;
            switch (hi) {
                case 0xf0: if (c->ov) c->npc = w2; c->ov = 0; break;                                                       /* BV */
                case 0xf1: if (c->acc > 0) c->npc = w2; break;                                                             /* BGZ */
                case 0xf2: if (c->acc <= 0) c->npc = w2; break;                                                            /* BLEZ */
                case 0xf3: if (c->acc < 0) c->npc = w2; break;                                                             /* BLZ */
                case 0xf4: if (c->acc >= 0) c->npc = w2; break;                                                            /* BGEZ */
                case 0xf5: if (c->acc != 0) c->npc = w2; break;                                                            /* BNZ */
                case 0xf6: if (c->acc == 0) c->npc = w2; break;                                                            /* BZ */
                case 0xf7: if (!c->ov) c->npc = w2; c->ov = 0; break;                                                      /* BNV */
                case 0xf8: if (!c->tc) c->npc = w2; break;                                                                 /* BBZ */
                case 0xf9: if (c->tc) c->npc = w2; break;                                                                  /* BBNZ */
                case 0xfa: if (c->bio && !c->bio(c->priv)) c->npc = w2; break;                                             /* BIOZ */
                case 0xfb: if (c->ar[c->arp] != 0) c->npc = w2; break;                                                     /* BANZ */
                case 0xfc: /* BLKP */
                    d = prd_(c, (uint16_t) (w2 + c->iter));
                    if (op & 0x80) { wr(c, ea(c, op), d); } else wr(c, (uint16_t) (ea(c, op) + c->iter), d);
                    cyc = first ? 3 : 1;
                    break;
                case 0xfd: /* BLKD */
                    d = rd(c, (uint16_t) (w2 + c->iter));
                    if (op & 0x80) { wr(c, ea(c, op), d); } else wr(c, (uint16_t) (ea(c, op) + c->iter), d);
                    cyc = first ? 3 : 1;
                    break;
                case 0xfe: push(c, (uint16_t) (pc0 + 2)); c->npc = w2; break;                                              /* CALL */
                default:   c->npc = w2; break;                                                                             /* B */
            }
            post(c, op);
            break;
        }
        default: c->unimpl = op; c->unimpl_pc = pc0; break;
    }

    if (len == 2 && c->npc == (uint16_t) (pc0 + 1))
        c->npc = pc0 + 2;

    return cyc;
}

static void
tick_timer(c25_t *c, int n)
{
    int32_t t = (int32_t) c->tim - n;

    while (t < 0) {
        t += (int32_t) c->prd + 1;
        c->ifr |= (1 << C25_TINT);
    }
    c->tim = (uint16_t) t;
}

static int
step(c25_t *c)
{
    const uint16_t pc0 = c->pc;
    const uint16_t op  = prd_(c, pc0);
    const uint16_t w2  = prd_(c, (uint16_t) (pc0 + 1));
    int            n   = 1;
    int            cyc = 0;

    if (c->rpt_pending) {
        n = c->rptc + 1;
        c->rpt_pending = 0;
        c->rptc = 0;
    }

    for (int i = 0; i < n; i++) {
        c->iter = i;
        cyc += exec(c, op, w2, pc0);
        if (c->rpt_pending) /* a RPT following a RPT is not repeated */
            break;
        if (c->idle)
            break;
    }
    c->iter = 0;
    c->pc   = c->npc;
    return cyc;
}

int
c25_run(c25_t *c, int cycles)
{
    int used = 0;

    while (used < cycles) {
        if (c->idle) {
            if (c->ifr & c->imr & 0x3f)
                c->idle = 0;
            else {
                tick_timer(c, cycles - used);
                used = cycles;
                break;
            }
        }
        if (!c->intm && !c->eint_delay && (c->ifr & c->imr & 0x3f) && !c->rpt_pending)
            take_int(c);
        c->eint_delay = 0;

        const int n = step(c);
        tick_timer(c, n);
        used += n;
    }
    return used;
}
