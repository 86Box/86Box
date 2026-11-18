/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             PC Card 95 multi-function cards: several functions -- a LAN
 *             and a modem, say -- behind one socket, each with its own CIS
 *             chain (found through the card's CISTPL_LONGLINK_MFC), its own
 *             configuration registers and its own I/O range, all sharing the
 *             card's one interrupt request line.
 *
 *             A card fills in a pccard_mfc_t (the registers are described in
 *             pcmcia.h) and puts its card into the socket; this file is the
 *             pccard_t the socket sees: it reads the CIS and the registers in
 *             attribute memory, routes I/O to the function whose range the
 *             port is in, and works out IREQ from the functions' interrupts.
 *
 *             What Card Services does with the registers (Linux pcmcia-cs
 *             modules/cs.c, RequestConfiguration): COR = the configuration
 *             index's bits 5:3 | function enable | IREQ enable | level IREQ,
 *             with address decode when the CONFIG tuple says the I/O base
 *             registers are there, which it then writes with the base of the
 *             I/O window, and the I/O limit with its length minus one.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <86box/pcmcia.h>

#define REG_COR    0
#define REG_CCSR   1
#define REG_IOBASE 5   /* 0x0A: I/O base 0, then 1-3 */
#define REG_IOLIM  9   /* 0x12 */
#define NREGS      10

#define COR_SRESET 0x80
#define COR_LEVEL  0x40
#define COR_IREQ   0x04
#define COR_DECODE 0x02
#define COR_ENABLE 0x01

#define CCSR_INTR  0x02

/* The card's IREQ: any function enabled to interrupt that has a request. */
static void
mfc_update_irq(pccard_mfc_t *m)
{
    int ireq = 0;

    for (int f = 0; f < m->nfunc; f++) {
        const uint8_t cor = m->reg[f][REG_COR];
        if (m->intr[f] && (cor & COR_ENABLE) && (cor & COR_IREQ))
            ireq = 1;
    }
    if (ireq != m->ireq) {
        m->ireq = ireq;
        pcmcia_card_irq(m->socket, ireq);
    }
}

static void
mfc_func_reset(pccard_mfc_t *m, int f)
{
    const int was = m->reg[f][REG_COR] & COR_ENABLE;

    memset(m->reg[f], 0, NREGS);
    m->intr[f] = 0;
    if (m->func[f]->reset)
        m->func[f]->reset(m->func[f]->priv);
    if (was && m->func[f]->enable)
        m->func[f]->enable(0, m->func[f]->priv);
}

/* Which function's configuration register is at attribute address addr: the
   function, and the register (0-9) through *r; -1 if none. */
static int
mfc_reg_at(const pccard_mfc_t *m, uint32_t addr, int *r)
{
    for (int f = 0; f < m->nfunc; f++) {
        if ((addr >= m->cfg_base[f]) && (addr < m->cfg_base[f] + 2 * NREGS) && !((addr - m->cfg_base[f]) & 1)) {
            *r = (int) (addr - m->cfg_base[f]) >> 1;
            return f;
        }
    }
    return -1;
}

static uint8_t
mfc_attr_read(uint32_t addr, void *priv)
{
    const pccard_mfc_t *m = (pccard_mfc_t *) priv;
    int                 r;
    const int           f = mfc_reg_at(m, addr, &r);

    if (f >= 0) {
        if (r == REG_CCSR)
            return (uint8_t) ((m->reg[f][r] & ~CCSR_INTR) | (m->intr[f] ? CCSR_INTR : 0));
        return m->reg[f][r];
    }
    if (!(addr & 1) && ((addr >> 1) < (uint32_t) m->cis_len))
        return m->cis[addr >> 1];
    return 0xff;
}

static void
mfc_attr_write(uint32_t addr, uint8_t val, void *priv)
{
    pccard_mfc_t *m = (pccard_mfc_t *) priv;
    int           r;
    const int     f = mfc_reg_at(m, addr, &r);

    if (f < 0)
        return;
    if (r == REG_COR) {
        if (val & COR_SRESET) {
            mfc_func_reset(m, f);
            m->reg[f][REG_COR] = COR_SRESET;   /* held in reset until cleared */
        } else {
            const int was = m->reg[f][REG_COR] & COR_ENABLE;
            m->reg[f][REG_COR] = val;
            if (((val & COR_ENABLE) != was) && m->func[f]->enable)
                m->func[f]->enable(val & COR_ENABLE, m->func[f]->priv);
        }
        mfc_update_irq(m);
        return;
    }
    if (r == REG_CCSR)
        val &= ~CCSR_INTR;   /* the request is the function's to drop */
    m->reg[f][r] = val;
}

/* The function whose I/O range holds port, and the offset into it. */
static int
mfc_io_func(const pccard_mfc_t *m, uint16_t port, uint16_t *off)
{
    for (int f = 0; f < m->nfunc; f++) {
        const uint8_t *reg = m->reg[f];
        const uint16_t len = m->func[f]->io_len;

        if (!(reg[REG_COR] & COR_ENABLE) || !len || !m->func[f]->io_read)
            continue;
        if (reg[REG_COR] & COR_DECODE) {
            /* Without I/O base 1 the function compares A7-A0 only. */
            const uint16_t mask = (m->cfg_rmask[f] & 0x40) ? 0xffff : 0x00ff;
            const uint16_t base = (uint16_t) (reg[REG_IOBASE] | (reg[REG_IOBASE + 1] << 8)) & mask;
            const uint16_t p    = port & mask;
            if ((p < base) || (p >= base + len))
                continue;
            *off = p - base;
        } else
            *off = port & (len - 1);
        return f;
    }
    return -1;
}

static uint8_t
mfc_io_read(uint16_t port, void *priv)
{
    const pccard_mfc_t *m = (pccard_mfc_t *) priv;
    uint16_t            off;
    const int           f = mfc_io_func(m, port, &off);

    return (f >= 0) ? m->func[f]->io_read(off, m->func[f]->priv) : 0xff;
}

static uint16_t
mfc_io_readw(uint16_t port, void *priv)
{
    const pccard_mfc_t *m = (pccard_mfc_t *) priv;
    uint16_t            off;
    const int           f = mfc_io_func(m, port, &off);

    if (f < 0)
        return 0xffff;
    if (m->func[f]->io_readw)
        return m->func[f]->io_readw(off, m->func[f]->priv);
    return (uint16_t) (mfc_io_read(port, priv) | (mfc_io_read(port + 1, priv) << 8));
}

static void
mfc_io_write(uint16_t port, uint8_t val, void *priv)
{
    const pccard_mfc_t *m = (pccard_mfc_t *) priv;
    uint16_t            off;
    const int           f = mfc_io_func(m, port, &off);

    if ((f >= 0) && m->func[f]->io_write)
        m->func[f]->io_write(off, val, m->func[f]->priv);
}

static void
mfc_io_writew(uint16_t port, uint16_t val, void *priv)
{
    const pccard_mfc_t *m = (pccard_mfc_t *) priv;
    uint16_t            off;
    const int           f = mfc_io_func(m, port, &off);

    if (f < 0)
        return;
    if (m->func[f]->io_writew)
        m->func[f]->io_writew(off, val, m->func[f]->priv);
    else {
        mfc_io_write(port, (uint8_t) val, priv);
        mfc_io_write(port + 1, (uint8_t) (val >> 8), priv);
    }
}

/* RESET or power off: every function back to unconfigured. */
static void
mfc_reset(void *priv)
{
    pccard_mfc_t *m = (pccard_mfc_t *) priv;

    for (int f = 0; f < m->nfunc; f++)
        mfc_func_reset(m, f);
    mfc_update_irq(m);
}

void
pccard_mfc_init(pccard_mfc_t *m, int socket, const char *name, uint8_t *cis, int cis_len)
{
    memset(m, 0, sizeof(pccard_mfc_t));
    m->socket           = socket;
    m->cis              = cis;
    m->cis_len          = cis_len;
    m->card.name        = name;
    m->card.attr_read   = mfc_attr_read;
    m->card.attr_write  = mfc_attr_write;
    m->card.io_read     = mfc_io_read;
    m->card.io_readw    = mfc_io_readw;
    m->card.io_write    = mfc_io_write;
    m->card.io_writew   = mfc_io_writew;
    m->card.reset       = mfc_reset;
    m->card.priv        = m;
}

int
pccard_mfc_add(pccard_mfc_t *m, const pccard_func_t *f, uint32_t cfg_base, uint16_t rmask)
{
    if (m->nfunc >= PCCARD_MFC_MAX)
        return -1;
    m->func[m->nfunc]      = f;
    m->cfg_base[m->nfunc]  = cfg_base;
    m->cfg_rmask[m->nfunc] = rmask;
    return m->nfunc++;
}

/* Function func's interrupt request: 1 = requesting.  In its CCSR always;
   on the card's IREQ while its COR enables that. */
void
pccard_mfc_irq(pccard_mfc_t *m, int func, int level)
{
    if ((func < 0) || (func >= m->nfunc))
        return;
    m->intr[func] = !!level;
    mfc_update_irq(m);
}
