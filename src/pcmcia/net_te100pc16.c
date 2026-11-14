/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             TRENDnet TE100-PC16: a 16-bit PC Card 10/100 Ethernet card
 *             (an AboCom design around the ASIX AX88190), in a socket of the
 *             PC Card controller (pcic_pd6722.c).  From MegaPPBox, where it
 *             goes in socket A of the Merit MAXX I/O board.
 *
 * ---------------------------------------------------------------------------
 *
 * What the card is, from its driver disk (driver_te100-pc16.zip):
 *
 *   - Windows lists it as PCMCIA\Fast_Ethernet-16-bit_PC_Card, and the
 *     pcmcia-cs config the Linux MAXX releases carry binds "Fast Ethernet",
 *     "16-bit PC Card", *, "AX88190" (manfid 0x0149, 0xc1ab) to axnet_cs;
 *   - the DOS drivers (LE100PD.COM, LE100.DOS) read the reset port, +0x1F:
 *     0xFF is the AX88190, 0x09/0x91/0x99 the DL10019/DL10022 of the same
 *     driver's other cards.  The AX88190 path reads the station address by
 *     remote DMA from 0x0400 (three packed words) and talks MII through
 *     +0x14 -- the same as Linux axnet_cs.
 *
 * So the card is an NE2000 as far as the 8390 goes (net_dp8390.c; 16 KB of
 * buffer at 0x4000, data port +0x10, reset +0x1F), plus:
 *
 *   +0x14  MII: bit 0 MDC, bit 3 MDIO out, bit 1 = the PHY drives MDIO,
 *          bit 2 MDIO in.  The PHY is a generic 10/100 one (a Davicom
 *          DM9161's ID, which the DOS driver has no special case for),
 *          answering at every address, link up at 100 Mbit/s full duplex.
 *   +0x15  test register: reads 0 (an AX88790 reads non-zero; axnet_cs)
 *   +0x17  GPIO, kept
 *   +0x1F  a read resets the 8390 and returns 0xFF
 *
 * Attribute memory holds the CIS, and the configuration registers at 0x3C0
 * (axnet_cs insists on that base): COR, which puts the card in I/O mode
 * once a configuration index is written, and CCSR, whose bit 1 shows a
 * pending interrupt.  The CIS offers 32 ports (five address lines) and any
 * IRQ; the card decodes only its low five address lines, so the socket's I/O
 * window can put it anywhere.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/random.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_dp8390.h>
#include <86box/pcmcia.h>
#include <86box/plat_unused.h>

#define TE100_CONFIG     0x3c0 /* attribute memory: COR, then CCSR at +2 */
#define COR_INDEX        0x3f
#define COR_SRESET       0x80
#define CCSR_INTR        0x02

#define PORT_DATA        0x10
#define PORT_MII         0x14
#define PORT_TEST        0x15
#define PORT_GPIO        0x17
#define PORT_RESET       0x1f

#define MII_MDC          0x01
#define MII_DIR_IN       0x02
#define MII_MDIO_IN      0x04
#define MII_MDIO_OUT     0x08

#define MAC_ROM          0x0400 /* where the station address is read from */

enum {
    MDIO_IDLE = 0, /* preamble: ones                             */
    MDIO_CMD,      /* start, opcode, PHY and register addresses  */
    MDIO_READ,     /* turnaround and 16 bits out, from the PHY   */
    MDIO_WRITE     /* turnaround and 16 bits in                  */
};

typedef struct {
    dp8390_t *dp8390;
    pccard_t  pccard;
    int       socket; /* 0 = A, 1 = B: the device instance less one */

    uint8_t   cis[256];
    int       cis_len;
    uint8_t   cor;
    uint8_t   ccsr;
    int       ireq; /* the 8390's interrupt output */

    uint8_t   mii;  /* last value written to +0x14 */
    uint8_t   gpio;
    int       mdio_state;
    int       mdio_bits;
    uint32_t  mdio_shift;
    int       mdio_reg;
    uint16_t  mdio_out;
    uint16_t  phy[32];

    uint8_t   maclocal[6];
} te100_t;

#ifdef ENABLE_TE100_LOG
int te100_do_log = ENABLE_TE100_LOG;

static void
te100_log(const char *fmt, ...)
{
    va_list ap;

    if (te100_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define te100_log(fmt, ...)
#endif

static int
te100_io_enabled(const te100_t *dev)
{
    return (dev->cor & COR_INDEX) && !(dev->cor & COR_SRESET);
}

static void
te100_irq_update(te100_t *dev)
{
    pcmcia_card_irq(dev->socket, dev->ireq && te100_io_enabled(dev));
}

static void
te100_interrupt(void *priv, int set)
{
    te100_t *dev = (te100_t *) priv;

    dev->ireq = !!set;
    te100_irq_update(dev);
}

/* --- CIS ------------------------------------------------------------------ */

/* pccard_cis.c's TE100-PC16 CIS, with this card's station address. */
static void
te100_cis_build(te100_t *dev)
{
    dev->cis_len = pccard_cis_te100pc16(dev->cis, dev->maclocal);
}

static uint8_t
te100_attr_read(uint32_t addr, void *priv)
{
    const te100_t *dev = (te100_t *) priv;

    if (addr & 1)
        return 0xff;
    if (addr == TE100_CONFIG)
        return dev->cor;
    if (addr == (TE100_CONFIG + 2))
        return dev->ccsr | (dev->ireq ? CCSR_INTR : 0x00);
    if ((addr >> 1) < (uint32_t) dev->cis_len)
        return dev->cis[addr >> 1];
    return 0xff;
}

static void te100_card_reset(void *priv);

static void
te100_attr_write(uint32_t addr, uint8_t val, void *priv)
{
    te100_t *dev = (te100_t *) priv;

    if (addr == TE100_CONFIG) {
        if (val & COR_SRESET) {
            te100_card_reset(dev);
            dev->cor = COR_SRESET;
        } else
            dev->cor = val;
        te100_log("TE100: COR = %02X\n", dev->cor);
        te100_irq_update(dev);
    } else if (addr == (TE100_CONFIG + 2))
        dev->ccsr = val & ~CCSR_INTR;
}

/* --- MII ------------------------------------------------------------------ */

static void
te100_phy_reset(te100_t *dev)
{
    memset(dev->phy, 0, sizeof(dev->phy));
    dev->phy[0] = 0x3100; /* 100 Mbit/s, autonegotiation on, full duplex    */
    dev->phy[1] = 0x782d; /* 10/100 half/full, AN complete, link up        */
    dev->phy[2] = 0x0181; /* Davicom DM9161                                 */
    dev->phy[3] = 0xb880;
    dev->phy[4] = 0x01e1; /* advertising 10/100 half/full                   */
    dev->phy[5] = 0x45e1; /* the partner: the same, acknowledged            */
    dev->phy[6] = 0x0001;
}

static uint16_t
te100_phy_read(te100_t *dev, int reg)
{
    return dev->phy[reg & 0x1f];
}

static void
te100_phy_write(te100_t *dev, int reg, uint16_t val)
{
    switch (reg) {
        case 0:
            if (val & 0x8000) {
                te100_phy_reset(dev);
                return;
            }
            /* Restart autonegotiation completes at once. */
            dev->phy[0] = val & ~0x0200;
            break;
        case 4:
            dev->phy[4] = val;
            break;
        default:
            break;
    }
}

/* Management frames, bit by bit on the rising edge of MDC: a preamble of
   ones, 01, the opcode (10 read, 01 write), PHY and register addresses,
   then the turnaround and 16 data bits.  In a read the PHY drives MDIO
   after each rising edge -- the second turnaround bit (0), then D15..D0 --
   so a driver that samples before the edge (Linux) and one that samples
   after it (the DOS drivers) both see the frame they expect. */
static void
te100_mdio_clock(te100_t *dev, int bit)
{
    switch (dev->mdio_state) {
        case MDIO_IDLE:
            if (!bit) {
                dev->mdio_state = MDIO_CMD;
                dev->mdio_shift = 0;
                dev->mdio_bits  = 1;
            }
            break;

        case MDIO_CMD:
            dev->mdio_shift = (dev->mdio_shift << 1) | bit;
            if (++dev->mdio_bits < 14)
                break;
            /* 0 (already in), 1, op[2], phy[5], reg[5]: 13 bits shifted. */
            dev->mdio_reg = dev->mdio_shift & 0x1f;
            dev->mdio_bits = 0;
            switch ((dev->mdio_shift >> 10) & 7) {
                case 6: /* 1 10: read */
                    dev->mdio_state = MDIO_READ;
                    dev->mdio_out   = te100_phy_read(dev, dev->mdio_reg);
                    break;
                case 5: /* 1 01: write */
                    dev->mdio_state = MDIO_WRITE;
                    dev->mdio_shift = 0;
                    break;
                default:
                    dev->mdio_state = MDIO_IDLE;
                    break;
            }
            break;

        case MDIO_READ:
            if (++dev->mdio_bits >= 18)
                dev->mdio_state = MDIO_IDLE;
            break;

        case MDIO_WRITE:
            dev->mdio_shift = (dev->mdio_shift << 1) | bit;
            if (++dev->mdio_bits == 18) {
                te100_phy_write(dev, dev->mdio_reg, dev->mdio_shift & 0xffff);
                dev->mdio_state = MDIO_IDLE;
            }
            break;

        default:
            break;
    }
}

static int
te100_mdio_in(const te100_t *dev)
{
    if (dev->mdio_state != MDIO_READ)
        return 1; /* pulled up */
    if (dev->mdio_bits == 0)
        return 1; /* first turnaround bit: nobody drives it */
    if (dev->mdio_bits == 1)
        return 0;
    return (dev->mdio_out >> (17 - dev->mdio_bits)) & 1;
}

static void
te100_mii_write(te100_t *dev, uint8_t val)
{
    if (!(dev->mii & MII_MDC) && (val & MII_MDC))
        te100_mdio_clock(dev, !!(val & MII_MDIO_OUT));
    dev->mii = val;
}

/* --- 8390 and the NE2000-style ports -------------------------------------- */

static uint32_t
te100_chipmem_read(te100_t *dev, uint32_t addr, unsigned int len)
{
    uint32_t ret = 0;

    for (unsigned int i = 0; i < len; i++, addr++) {
        uint8_t b;

        if ((addr >= MAC_ROM) && (addr < (MAC_ROM + 0x10)))
            b = ((addr - MAC_ROM) < 6) ? dev->maclocal[addr - MAC_ROM] : 0x00;
        else
            b = dp8390_chipmem_read(dev->dp8390, addr, 1);
        ret |= (uint32_t) b << (i << 3);
    }
    return ret;
}

/* The remote DMA step the data port makes, as the NE2000's ASIC. */
static void
te100_rdma_step(te100_t *dev)
{
    dp8390_t *nic = dev->dp8390;

    nic->remote_dma += (nic->DCR.wdsize + 1);
    if (nic->remote_dma == (nic->page_stop << 8))
        nic->remote_dma = nic->page_start << 8;

    if (nic->remote_bytes > nic->DCR.wdsize)
        nic->remote_bytes -= (nic->DCR.wdsize + 1);
    else
        nic->remote_bytes = 0;

    if (nic->remote_bytes == 0) {
        nic->ISR.rdma_done = 1;
        if (nic->IMR.rdma_inte)
            te100_interrupt(dev, 1);
    }
}

static uint32_t
te100_data_read(te100_t *dev, unsigned int len)
{
    const uint32_t ret = te100_chipmem_read(dev, dev->dp8390->remote_dma, len);

    te100_rdma_step(dev);
    return ret;
}

static void
te100_data_write(te100_t *dev, uint32_t val, unsigned int len)
{
    dp8390_chipmem_write(dev->dp8390, dev->dp8390->remote_dma, val, len);
    te100_rdma_step(dev);
}

static uint8_t
te100_io_read(uint16_t port, void *priv)
{
    te100_t  *dev = (te100_t *) priv;
    const int off = port & 0x1f;

    if (!te100_io_enabled(dev))
        return 0xff;

    if (off == 0x00)
        return dp8390_read_cr(dev->dp8390);
    if (off < 0x10) {
        switch (dev->dp8390->CR.pgsel) {
            case 0x00:
                return dp8390_page0_read(dev->dp8390, off, 1);
            case 0x01:
                return dp8390_page1_read(dev->dp8390, off, 1);
            case 0x02:
                return dp8390_page2_read(dev->dp8390, off, 1);
            default:
                return 0x00;
        }
    }

    switch (off) {
        case PORT_DATA ... PORT_DATA + 3:
            return te100_data_read(dev, 1);
        case PORT_MII:
            return (dev->mii & ~MII_MDIO_IN) | (te100_mdio_in(dev) ? MII_MDIO_IN : 0x00);
        case PORT_TEST:
            return 0x00;
        case PORT_GPIO:
            return dev->gpio;
        case PORT_RESET:
            dp8390_soft_reset(dev->dp8390);
            return 0xff;
        default:
            return 0x00;
    }
}

static uint16_t
te100_io_readw(uint16_t port, void *priv)
{
    te100_t *dev = (te100_t *) priv;

    if (!te100_io_enabled(dev))
        return 0xffff;
    if (((port & 0x1f) >= PORT_DATA) && ((port & 0x1f) <= (PORT_DATA + 3)))
        return te100_data_read(dev, 2);
    return te100_io_read(port, priv) | (te100_io_read(port + 1, priv) << 8);
}

static void
te100_io_write(uint16_t port, uint8_t val, void *priv)
{
    te100_t  *dev = (te100_t *) priv;
    const int off = port & 0x1f;

    if (!te100_io_enabled(dev))
        return;

    if (off == 0x00) {
        dp8390_write_cr(dev->dp8390, val);
        return;
    }
    if (off < 0x10) {
        switch (dev->dp8390->CR.pgsel) {
            case 0x00:
                dp8390_page0_write(dev->dp8390, off, val, 1);
                break;
            case 0x01:
                dp8390_page1_write(dev->dp8390, off, val, 1);
                break;
            case 0x02:
                dp8390_page2_write(dev->dp8390, off, val, 1);
                break;
            default:
                break;
        }
        return;
    }

    switch (off) {
        case PORT_DATA ... PORT_DATA + 3:
            te100_data_write(dev, val, 1);
            break;
        case PORT_MII:
            te100_mii_write(dev, val);
            break;
        case PORT_GPIO:
            dev->gpio = val;
            break;
        default: /* +0x1F: the end of the reset pulse */
            break;
    }
}

static void
te100_io_writew(uint16_t port, uint16_t val, void *priv)
{
    te100_t *dev = (te100_t *) priv;

    if (!te100_io_enabled(dev))
        return;
    if (((port & 0x1f) >= PORT_DATA) && ((port & 0x1f) <= (PORT_DATA + 3))) {
        if (dev->dp8390->DCR.wdsize)
            te100_data_write(dev, val, 2);
        return;
    }
    te100_io_write(port, val & 0xff, priv);
    te100_io_write(port + 1, val >> 8, priv);
}

/* --- Device --------------------------------------------------------------- */

/* Power-up, the RESET pin or a soft reset through the COR. */
static void
te100_card_reset(void *priv)
{
    te100_t *dev = (te100_t *) priv;

    dev->cor        = 0x00;
    dev->ccsr       = 0x00;
    dev->mii        = 0x00;
    dev->gpio       = 0x00;
    dev->mdio_state = MDIO_IDLE;
    te100_phy_reset(dev);
    dp8390_reset(dev->dp8390);
    dev->ireq = 0;
    te100_irq_update(dev);
}

static void *
te100_init(UNUSED(const device_t *info))
{
    te100_t *dev = (te100_t *) calloc(1, sizeof(te100_t));
    uint32_t mac;

    dev->socket = device_get_instance() - 1;
    if ((dev->socket < 0) || (dev->socket >= PCMCIA_SOCKETS))
        dev->socket = 0;

    /* AboCom's OUI, and a local address kept in the configuration. */
    dev->maclocal[0] = 0x00;
    dev->maclocal[1] = 0xe0;
    dev->maclocal[2] = 0x98;
    mac              = device_get_config_mac("mac", -1);
    if (mac & 0xff000000) {
        dev->maclocal[3] = random_generate();
        dev->maclocal[4] = random_generate();
        dev->maclocal[5] = random_generate();
        mac              = (dev->maclocal[3] << 16) | (dev->maclocal[4] << 8) | dev->maclocal[5];
        device_set_config_mac("mac", mac);
    } else {
        dev->maclocal[3] = (mac >> 16) & 0xff;
        dev->maclocal[4] = (mac >> 8) & 0xff;
        dev->maclocal[5] = mac & 0xff;
    }

    /* Its own, not in the device list: the card is hot-pluggable, and its
       8390 goes when it does. */
    dev->dp8390            = dp8390_device.init(&dp8390_device);
    dev->dp8390->priv      = dev;
    dev->dp8390->interrupt = te100_interrupt;
    dp8390_set_defaults(dev->dp8390, DP8390_FLAG_EVEN_MAC | DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ);
    dp8390_mem_alloc(dev->dp8390, 0x4000, 0x4000);
    memcpy(dev->dp8390->physaddr, dev->maclocal, sizeof(dev->maclocal));

    te100_cis_build(dev);
    te100_card_reset(dev);

    dev->dp8390->card              = pcmcia_network_attach(dev->socket, dev->dp8390, dev->dp8390->physaddr, dp8390_rx, NULL);
    dev->dp8390->card->byte_period = NET_PERIOD_100M;

    dev->pccard = (pccard_t) {
        .name       = "TRENDnet TE100-PC16",
        .attr_read  = te100_attr_read,
        .attr_write = te100_attr_write,
        .io_read    = te100_io_read,
        .io_readw   = te100_io_readw,
        .io_write   = te100_io_write,
        .io_writew  = te100_io_writew,
        .reset      = te100_card_reset,
        .priv       = dev
    };
    pcmcia_insert(dev->socket, &dev->pccard);

    te100_log("TE100: %02X:%02X:%02X:%02X:%02X:%02X in socket %c\n",
              dev->maclocal[0], dev->maclocal[1], dev->maclocal[2],
              dev->maclocal[3], dev->maclocal[4], dev->maclocal[5], 'A' + dev->socket);
    return dev;
}

static void
te100_close(void *priv)
{
    te100_t *dev = (te100_t *) priv;

    pcmcia_insert(dev->socket, NULL);
    dp8390_device.close(dev->dp8390);   /* its network link with it */
    free(dev);
}

// clang-format off
static const device_config_t te100_config[] = {
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t te100pc16_device = {
    .name          = "TRENDnet TE100-PC16 Fast Ethernet",
    .internal_name = "te100pc16",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = te100_init,
    .close         = te100_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = te100_config
};
