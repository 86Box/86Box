/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             The PC Card controller: a Cirrus Logic CL-PD6722 (Intel
 *             82365SL compatible) on ISA at 0x3E0, two sockets.  From
 *             MegaPPBox, where it is the PC Card slots of the Merit MAXX I/O
 *             board; here it is also a card of its own (Settings > Other
 *             peripherals > PCMCIA), with the cards in pcmcia.c's list in
 *             its sockets.
 *
 * ---------------------------------------------------------------------------
 *
 * On the MAXX: its I/O cards carry a "PCMCIA connector" (90003007, board
 * identification) -- the "PC CARD SLOTS" of the MAXX service manuals -- used
 * for the TournaMAXX / MegaNET modem, for flash cards (books export, ad
 * screens) and for updates (/etc/init.d/update.pcmcia).  The software finds
 * the controller the way PC laptops of the day did:
 *
 *   - DOS MAXX 1st loads CardSoft with SSCIRRUS.EXE, the Cirrus socket
 *     services;
 *   - the Linux releases load pcmcia_core + i82365 + ds and start cardmgr
 *     (/etc/sysconfig/pcmcia: PCIC=i82365).  Without a controller the i82365
 *     probe fails, cardmgr has nothing to manage, and the boot reports the
 *     errors; /sbin/modemdetect.sh then takes the path meant for the later
 *     Force cabinets ("Detecting Force modem") instead of the MAXX one.
 *
 * What a driver *identifies* the chip by is emulated:
 *
 *   0x00  revision, 0x83 (82365SL step B, which the PD67xx reports; Linux
 *         wants bits 6:4 clear before it believes an interrupt is ours)
 *   0x1F  chip information: bits 7:6 read 11 then 00 alternately (the Cirrus
 *         signature; a write restarts it), bit 5 set = two sockets
 *   0x2E  extended index, read back (Linux tells a PD67xx from a VIA VT83C469
 *         by it); 0x2F extended data behind it
 *
 * and, for each socket, what drives a card:
 *
 *   0x01  status: card detect, battery good, power, ready
 *   0x02  power: VCC on (bit 4) and outputs enabled (bit 7) power the card
 *   0x03  bits 3:0 the IRQ the card's IREQ is steered to, bit 5 I/O card,
 *         bit 6 clear = card held in RESET
 *   0x04  card status change: bit 3 card detect changed (a card went in or
 *         came out), bit 2 ready changed, bits 1:0 battery; reading it clears
 *         it
 *   0x05  status change interrupt: bits 7:4 the IRQ, bits 3:0 which changes
 *         raise it (the same bits as 0x04)
 *   0x06  window enables: memory windows 0-4 (bits 0-4), I/O 0-1 (bits 6-7)
 *   0x08  I/O windows: start and stop, 16 bits each (0x08-0x0F)
 *   0x10  memory windows, eight registers apart: start, stop (4 KB units,
 *         ISA's 16 MB) and the offset to the card address, whose bit 14
 *         selects attribute memory -- clear, the card's common memory -- and
 *         bit 15 write-protects the window
 *   0x16  bit 5, software interrupt: a card detect change on the socket's
 *         status change IRQ, whatever 0x05's enables say, until 0x04 is read
 *         (Linux's i82365 IRQ scan)
 *
 * Everything else a driver writes is kept and read back.  Each socket has
 * its own status changes and its own status change IRQ; the two sockets and
 * their cards may share an IRQ, so every IRQ line is worked out from all of
 * them together (pcic_irq_refresh()).
 *
 * Index 0x00-0x3F is socket A, 0x40-0x7F socket B; there is no socket C or D,
 * and those indexes read 0xFF, which is how the drivers tell.
 *
 * Cards are hot-pluggable (the PC Card status bar icon, pcmcia.c): a request
 * from the UI thread is carried out by the controller's poll on the
 * emulation thread; a card going in or out latches a card detect change, and
 * a card gone stops answering in its windows.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pcmcia.h>
#include <86box/plat_unused.h>

#define PCIC_BASE     0x3e0
#define PCIC_SOCKETS  2
#define PCIC_MEMWIN   5
#define PCIC_IOWIN    2

#define REG_IDENT     0x00
#define REG_STATUS    0x01
#define REG_POWER     0x02
#define REG_INTCTL    0x03
#define REG_CSC       0x04
#define REG_CSCINT    0x05
#define REG_WINEN     0x06
#define REG_IO_BASE   0x08 /* four registers per I/O window            */
#define REG_MEM_BASE  0x10 /* eight registers per memory window        */
#define REG_MISC1     0x16 /* 82365 "general control"                  */
#define REG_CHIP_INFO 0x1f
#define REG_EXT_INDEX 0x2e
#define REG_EXT_DATA  0x2f

#define STATUS_BVD    0x03 /* BVD1/BVD2: battery good (STSCHG/SPKR idle) */
#define STATUS_CD     0x0c /* CD1/CD2: card fully inserted               */
#define STATUS_READY  0x20
#define STATUS_POWER  0x40

#define POWER_VCC     0x10
#define POWER_OUT     0x80

#define INTCTL_IRQ    0x0f
#define INTCTL_IOCARD 0x20
#define INTCTL_NRESET 0x40

#define CSC_DETECT    0x08
#define CSC_READY     0x04
#define CSC_EVENTS    0x0f

#define MISC1_SW_IRQ  0x20

#define PD6722_IDENT  0x83
#define PD6722_INFO   0x20 /* dual socket, revision 0 */

#define POLL_US       10000.0   /* hot-plug requests, every 10 ms */

struct pcic_t;

typedef struct {
    struct pcic_t *pcic;
    int            socket;
    int            win;
} pcic_win_t;

typedef struct {
    const pccard_t *card;    /* what the socket holds now                  */
    int             live;    /* powered, outputs on and out of RESET       */
    int             ireq;    /* the card's interrupt request line          */
    int             sw_irq;  /* a software interrupt is pending            */
    uint16_t        io_base[PCIC_IOWIN];
    uint32_t        io_len[PCIC_IOWIN]; /* 0 = no handler installed  */
    mem_mapping_t   mem[PCIC_MEMWIN];
    pcic_win_t      mem_win[PCIC_MEMWIN];
} pcic_socket_t;

typedef struct pcic_t {
    uint8_t       index;
    uint8_t       reg[PCIC_SOCKETS][0x40];
    uint8_t       ext[PCIC_SOCKETS][0x40];
    uint8_t       info_toggle;
    uint16_t      irq_lines;  /* the ISA IRQs the controller is raising */
    int           running;    /* past power-on: cards coming and going are
                                 changes the guest is told about */
    pcic_socket_t sock[PCIC_SOCKETS];
    pc_timer_t    poll_timer;
} pcic_t;

/* One controller per machine; the cards in its sockets, whether the
   controller exists yet or not. */
static pcic_t         *pcic_inst;
static const pccard_t *pcic_fitted[PCIC_SOCKETS];

#ifdef ENABLE_PCIC_LOG
int pcic_do_log = ENABLE_PCIC_LOG;

static void
pcic_log(const char *fmt, ...)
{
    va_list ap;

    if (pcic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pcic_log(fmt, ...)
#endif

/* --- Interrupts ----------------------------------------------------------- */

static int
pcic_irq_valid(int irq)
{
    return (irq > 2) && (irq < 16);
}

/* Every IRQ line from everything that may drive one: each socket's status
   changes on its status change IRQ, each card's IREQ on the IRQ it is
   steered to.  Only the lines that change are told to the PIC. */
static void
pcic_irq_refresh(pcic_t *dev)
{
    uint16_t want = 0;

    for (int s = 0; s < PCIC_SOCKETS; s++) {
        const pcic_socket_t *sock = &dev->sock[s];
        const uint8_t        ctl  = dev->reg[s][REG_INTCTL];
        const int            cirq = dev->reg[s][REG_CSCINT] >> 4;

        if (pcic_irq_valid(cirq) && (sock->sw_irq || (dev->reg[s][REG_CSC] & dev->reg[s][REG_CSCINT] & CSC_EVENTS)))
            want |= 1 << cirq;
        if (sock->card && sock->live && sock->ireq && (ctl & INTCTL_IOCARD) && pcic_irq_valid(ctl & INTCTL_IRQ))
            want |= 1 << (ctl & INTCTL_IRQ);
    }

    for (int irq = 0; irq < 16; irq++) {
        const uint16_t bit = 1 << irq;
        if ((want ^ dev->irq_lines) & bit) {
            if (want & bit)
                picint(bit);
            else
                picintc(bit);
        }
    }
    dev->irq_lines = want;
}

/* A status change on a socket: latched in 0x04, an interrupt if 0x05
   enables it. */
static void
pcic_status_change(pcic_t *dev, int s, uint8_t bits)
{
    dev->reg[s][REG_CSC] |= bits;
    pcic_irq_refresh(dev);
}

/* The card is live when powered, its outputs enabled and RESET released;
   going live or dropping out of it resets the card.  Live, its READY comes
   up: a ready change. */
static void
pcic_card_power_update(pcic_t *dev, int s)
{
    pcic_socket_t *sock = &dev->sock[s];
    const int      live = sock->card && ((dev->reg[s][REG_POWER] & (POWER_VCC | POWER_OUT)) == (POWER_VCC | POWER_OUT)) &&
                         (dev->reg[s][REG_INTCTL] & INTCTL_NRESET);

    if (live != sock->live) {
        pcic_log("PCIC: socket %c card %s\n", 'A' + s, live ? "live" : "off / in reset");
        sock->live = live;
        if (sock->card && sock->card->reset)
            sock->card->reset(sock->card->priv);
        if (live && !(dev->reg[s][REG_INTCTL] & INTCTL_IOCARD))
            dev->reg[s][REG_CSC] |= CSC_READY;
    }
    pcic_irq_refresh(dev);
}

/* --- I/O windows ---------------------------------------------------------- */

static pcic_socket_t *
pcic_io_socket(void *priv)
{
    pcic_socket_t *sock = (pcic_socket_t *) priv;
    const int      s    = (sock == &pcic_inst->sock[1]);

    if (!sock->card || !sock->live || !(pcic_inst->reg[s][REG_INTCTL] & INTCTL_IOCARD) || !sock->card->io_read)
        return NULL;
    return sock;
}

static uint8_t
pcic_io_readb(uint16_t port, void *priv)
{
    const pcic_socket_t *sock = pcic_io_socket(priv);

    return sock ? sock->card->io_read(port, sock->card->priv) : 0xff;
}

static uint16_t
pcic_io_readw(uint16_t port, void *priv)
{
    const pcic_socket_t *sock = pcic_io_socket(priv);

    if (!sock)
        return 0xffff;
    if (sock->card->io_readw)
        return sock->card->io_readw(port, sock->card->priv);
    return sock->card->io_read(port, sock->card->priv) | (sock->card->io_read(port + 1, sock->card->priv) << 8);
}

static void
pcic_io_writeb(uint16_t port, uint8_t val, void *priv)
{
    const pcic_socket_t *sock = pcic_io_socket(priv);

    if (sock && sock->card->io_write)
        sock->card->io_write(port, val, sock->card->priv);
}

static void
pcic_io_writew(uint16_t port, uint16_t val, void *priv)
{
    const pcic_socket_t *sock = pcic_io_socket(priv);

    if (!sock || !sock->card->io_write)
        return;
    if (sock->card->io_writew)
        sock->card->io_writew(port, val, sock->card->priv);
    else {
        sock->card->io_write(port, val & 0xff, sock->card->priv);
        sock->card->io_write(port + 1, val >> 8, sock->card->priv);
    }
}

static void
pcic_io_remap(pcic_t *dev, int s, int w)
{
    pcic_socket_t *sock  = &dev->sock[s];
    const uint8_t *r     = &dev->reg[s][REG_IO_BASE + (w << 2)];
    const uint16_t start = r[0] | (r[1] << 8);
    const uint16_t stop  = r[2] | (r[3] << 8);
    const int      on    = sock->card && (dev->reg[s][REG_WINEN] & (0x40 << w)) && (stop >= start);

    if (sock->io_len[w]) {
        io_removehandler(sock->io_base[w], sock->io_len[w],
                         pcic_io_readb, pcic_io_readw, NULL, pcic_io_writeb, pcic_io_writew, NULL, sock);
        sock->io_len[w] = 0;
    }
    if (on) {
        sock->io_base[w] = start;
        sock->io_len[w]  = stop - start + 1;
        io_sethandler(sock->io_base[w], sock->io_len[w],
                      pcic_io_readb, pcic_io_readw, NULL, pcic_io_writeb, pcic_io_writew, NULL, sock);
        pcic_log("PCIC: socket %c I/O window %d = %04X-%04X\n", 'A' + s, w, start, stop);
    }
}

/* --- Memory windows ------------------------------------------------------- */

/* The card address behind a host address: 1 for attribute memory, 0 for
   common memory, -1 when the card is not answering. */
static int
pcic_mem_card_addr(const pcic_win_t *win, uint32_t addr, uint32_t *caddr)
{
    const pcic_t  *dev = win->pcic;
    const uint8_t *r   = &dev->reg[win->socket][REG_MEM_BASE + (win->win << 3)];
    const uint32_t off = (r[4] | ((r[5] & 0x3f) << 8)) << 12;

    if (!dev->sock[win->socket].card || !dev->sock[win->socket].live)
        return -1;
    *caddr = (addr + off) & 0x3ffffff;
    return !!(r[5] & 0x40);
}

static uint8_t
pcic_mem_readb(uint32_t addr, void *priv)
{
    const pcic_win_t *win = (pcic_win_t *) priv;
    const pccard_t   *card;
    uint32_t          caddr;

    switch (pcic_mem_card_addr(win, addr, &caddr)) {
        case 1:
            card = win->pcic->sock[win->socket].card;
            return card->attr_read ? card->attr_read(caddr, card->priv) : 0xff;
        case 0:
            card = win->pcic->sock[win->socket].card;
            return card->common_read ? card->common_read(caddr, card->priv) : 0xff;
        default:
            return 0xff;
    }
}

static uint16_t
pcic_mem_readw(uint32_t addr, void *priv)
{
    return pcic_mem_readb(addr, priv) | (pcic_mem_readb(addr + 1, priv) << 8);
}

static void
pcic_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
    const pcic_win_t *win = (pcic_win_t *) priv;
    const uint8_t    *r   = &win->pcic->reg[win->socket][REG_MEM_BASE + (win->win << 3)];
    const pccard_t   *card;
    uint32_t          caddr;

    if (r[5] & 0x80) /* write-protected window */
        return;
    switch (pcic_mem_card_addr(win, addr, &caddr)) {
        case 1:
            card = win->pcic->sock[win->socket].card;
            if (card->attr_write)
                card->attr_write(caddr, val, card->priv);
            break;
        case 0:
            card = win->pcic->sock[win->socket].card;
            if (card->common_write)
                card->common_write(caddr, val, card->priv);
            break;
        default:
            break;
    }
}

static void
pcic_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    pcic_mem_writeb(addr, val & 0xff, priv);
    pcic_mem_writeb(addr + 1, val >> 8, priv);
}

static void
pcic_mem_remap(pcic_t *dev, int s, int w)
{
    pcic_socket_t *sock  = &dev->sock[s];
    const uint8_t *r     = &dev->reg[s][REG_MEM_BASE + (w << 3)];
    const uint32_t start = (r[0] | ((r[1] & 0x0f) << 8)) << 12;
    const uint32_t stop  = ((r[2] | ((r[3] & 0x0f) << 8)) << 12) | 0xfff;

    if (sock->card && (dev->reg[s][REG_WINEN] & (1 << w)) && (stop > start)) {
        mem_mapping_set_addr(&sock->mem[w], start, stop - start + 1);
        pcic_log("PCIC: socket %c memory window %d = %06X-%06X, %s memory\n", 'A' + s, w, start, stop,
                 (r[5] & 0x40) ? "attribute" : "common");
    } else
        mem_mapping_disable(&sock->mem[w]);
}

static void
pcic_remap_all(pcic_t *dev, int s)
{
    for (int w = 0; w < PCIC_IOWIN; w++)
        pcic_io_remap(dev, s, w);
    for (int w = 0; w < PCIC_MEMWIN; w++)
        pcic_mem_remap(dev, s, w);
}

/* --- Registers ------------------------------------------------------------ */

static uint8_t
pcic_reg_read(pcic_t *dev, int s, int r)
{
    const pcic_socket_t *sock = &dev->sock[s];
    uint8_t              ret;

    switch (r) {
        case REG_IDENT:
            return PD6722_IDENT;

        case REG_STATUS:
            /* No card: both card-detect bits clear, no power, not ready. */
            if (!sock->card)
                return 0x00;
            ret = STATUS_CD | STATUS_BVD;
            if (dev->reg[s][REG_POWER] & POWER_VCC)
                ret |= STATUS_POWER;
            if (sock->live)
                ret |= STATUS_READY;
            return ret;

        case REG_CSC:
            ret                       = dev->reg[s][r];
            dev->reg[s][r]            = 0x00;
            dev->sock[s].sw_irq       = 0;
            pcic_irq_refresh(dev);
            return ret;

        case REG_CHIP_INFO:
            ret = (dev->info_toggle ? 0x00 : 0xc0) | PD6722_INFO;
            dev->info_toggle ^= 1;
            return ret;

        case REG_EXT_DATA:
            return dev->ext[s][dev->reg[s][REG_EXT_INDEX] & 0x3f];

        default:
            return dev->reg[s][r];
    }
}

static void
pcic_reg_write(pcic_t *dev, int s, int r, uint8_t val)
{
    switch (r) {
        case REG_IDENT:
        case REG_STATUS:
        case REG_CSC:
            break;

        case REG_CHIP_INFO:
            dev->info_toggle = 0;
            break;

        case REG_EXT_DATA:
            dev->ext[s][dev->reg[s][REG_EXT_INDEX] & 0x3f] = val;
            break;

        case REG_MISC1:
            /* The software interrupt fires once per write that sets it; the
               bit does not stay set, so the next probe can fire it again. */
            if (val & MISC1_SW_IRQ) {
                val &= ~MISC1_SW_IRQ;
                dev->reg[s][REG_CSC] |= CSC_DETECT;
                dev->sock[s].sw_irq = 1;
                pcic_log("PCIC: socket %c software interrupt on IRQ %d\n", 'A' + s, dev->reg[s][REG_CSCINT] >> 4);
            }
            dev->reg[s][r] = val;
            pcic_irq_refresh(dev);
            break;

        case REG_CSCINT:
            dev->reg[s][r] = val;
            pcic_irq_refresh(dev);
            break;

        case REG_POWER:
        case REG_INTCTL:
            dev->reg[s][r] = val;
            pcic_card_power_update(dev, s);
            break;

        case REG_WINEN:
            dev->reg[s][r] = val;
            pcic_remap_all(dev, s);
            break;

        default:
            dev->reg[s][r] = val;
            if ((r >= REG_IO_BASE) && (r < REG_MEM_BASE))
                pcic_io_remap(dev, s, (r - REG_IO_BASE) >> 2);
            else if ((r >= REG_MEM_BASE) && (r < (REG_MEM_BASE + (PCIC_MEMWIN << 3))) && (((r - REG_MEM_BASE) & 7) < 6))
                pcic_mem_remap(dev, s, (r - REG_MEM_BASE) >> 3);
            break;
    }
}

static uint8_t
pcic_read(uint16_t port, void *priv)
{
    pcic_t *dev = (pcic_t *) priv;

    if (!(port & 1))
        return dev->index;
    if ((dev->index >> 6) >= PCIC_SOCKETS)
        return 0xff;
    return pcic_reg_read(dev, dev->index >> 6, dev->index & 0x3f);
}

static void
pcic_write(uint16_t port, uint8_t val, void *priv)
{
    pcic_t *dev = (pcic_t *) priv;

    if (!(port & 1)) {
        dev->index = val;
        return;
    }
    if ((dev->index >> 6) < PCIC_SOCKETS)
        pcic_reg_write(dev, dev->index >> 6, dev->index & 0x3f, val);
}

/* --- Cards ---------------------------------------------------------------- */

/* Put what socket s should hold into it.  A different card from before --
   one in, one out, or one for another -- is a card detect change. */
static void
pcic_socket_set(pcic_t *dev, int s)
{
    pcic_socket_t  *sock = &dev->sock[s];
    const pccard_t *card = pcic_fitted[s];

    if (sock->card == card)
        return;
    pcic_log("PCIC: socket %c %s\n", 'A' + s, card ? "card inserted" : "card removed");
    if (sock->live && sock->card && sock->card->reset)
        sock->card->reset(sock->card->priv);   /* power gone from the old one */
    sock->card = card;
    sock->live = 0;
    sock->ireq = 0;
    pcic_remap_all(dev, s);
    pcic_card_power_update(dev, s);
    if (dev->running)
        pcic_status_change(dev, s, CSC_DETECT);
}

void
pcmcia_insert(int socket, const pccard_t *card)
{
    if ((socket < 0) || (socket >= PCIC_SOCKETS))
        return;
    pcic_fitted[socket] = card;
    if (pcic_inst)
        pcic_socket_set(pcic_inst, socket);
}

void
pcmcia_card_irq(int socket, int level)
{
    pcic_t *dev = pcic_inst;

    if (!dev || (socket < 0) || (socket >= PCIC_SOCKETS))
        return;
    dev->sock[socket].ireq = !!level;
    pcic_irq_refresh(dev);
}

const char *
pcmcia_socket_card_name(int socket)
{
    if ((socket < 0) || (socket >= PCIC_SOCKETS) || !pcic_fitted[socket])
        return NULL;
    return pcic_fitted[socket]->name ? pcic_fitted[socket]->name : "PC Card";
}

int
pcmcia_controller_present(void)
{
    return pcic_inst != NULL;
}

/* --- Device --------------------------------------------------------------- */

/* The UI's hot-plug requests, on the emulation thread. */
static void
pcic_poll(void *priv)
{
    pcic_t *dev = (pcic_t *) priv;

    timer_on_auto(&dev->poll_timer, POLL_US);
    dev->running = 1;
    pcmcia_slots_poll();
}

static void
pcic_reset(void *priv)
{
    pcic_t *dev = (pcic_t *) priv;

    memset(dev->reg, 0, sizeof(dev->reg));
    memset(dev->ext, 0, sizeof(dev->ext));
    dev->index       = 0;
    dev->info_toggle = 0;

    /* Power off, RESET asserted, every window closed, nothing pending. */
    for (int s = 0; s < PCIC_SOCKETS; s++) {
        dev->sock[s].ireq   = 0;
        dev->sock[s].sw_irq = 0;
        pcic_remap_all(dev, s);
        pcic_card_power_update(dev, s);
    }
    pcic_irq_refresh(dev);
}

static void *
pcic_init(UNUSED(const device_t *info))
{
    pcic_t *dev = (pcic_t *) calloc(1, sizeof(pcic_t));

    for (int s = 0; s < PCIC_SOCKETS; s++) {
        /* A card fitted at power-on is simply there: no change to report. */
        dev->sock[s].card = pcic_fitted[s];
        for (int w = 0; w < PCIC_MEMWIN; w++) {
            dev->sock[s].mem_win[w] = (pcic_win_t) { .pcic = dev, .socket = s, .win = w };
            mem_mapping_add(&dev->sock[s].mem[w], 0, 0,
                            pcic_mem_readb, pcic_mem_readw, NULL,
                            pcic_mem_writeb, pcic_mem_writew, NULL,
                            NULL, MEM_MAPPING_EXTERNAL, &dev->sock[s].mem_win[w]);
            mem_mapping_disable(&dev->sock[s].mem[w]);
        }
        if (dev->sock[s].card)
            pcic_log("PCIC: socket %c holds a card\n", 'A' + s);
    }

    io_sethandler(PCIC_BASE, 2, pcic_read, NULL, NULL, pcic_write, NULL, NULL, dev);
    timer_add(&dev->poll_timer, pcic_poll, dev, 1);
    timer_on_auto(&dev->poll_timer, POLL_US);
    pcic_inst = dev;
    return dev;
}

static void
pcic_close(void *priv)
{
    pcic_t *dev = (pcic_t *) priv;

    timer_stop(&dev->poll_timer);
    for (int s = 0; s < PCIC_SOCKETS; s++) {
        /* Close the windows before the card goes away. */
        dev->sock[s].card   = NULL;
        dev->sock[s].sw_irq = 0;
        dev->reg[s][REG_CSC] = 0;
        pcic_remap_all(dev, s);
    }
    pcic_irq_refresh(dev);   /* every line down */
    pcic_inst = NULL;
    free(dev);
}

static const device_t pcic_pd6722_device = {
    .name          = "Cirrus Logic CL-PD6722 PC Card controller",
    .internal_name = "pcic_pd6722",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = pcic_init,
    .close         = pcic_close,
    .reset         = pcic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/* One controller per machine, whoever asks for it first: the PCMCIA settings
   or an I/O board with PC Card slots. */
void
pcmcia_controller_add(void)
{
    if (pcic_inst == NULL)
        device_add(&pcic_pd6722_device);
}
