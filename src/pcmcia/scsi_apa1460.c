/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             Adaptec APA-1460 SlimSCSI: a 16-bit PC Card SCSI host adapter,
 *             the AIC-6360 of the AHA-1520A (scsi_aic6360.c) on a PC Card,
 *             with no BIOS and no DMA.
 *
 *             From its Technical Reference (Adaptec 510671-00 rev. A, 1994):
 *
 *               - attribute memory decodes A13-A0 only: 0000h-1FFFh is the
 *                 CIS ROM (an EEPROM, writable after an unlock sequence --
 *                 its writes are ignored here), 2000h-3FFFh the Configuration
 *                 Option Register, every address of it, and the two repeat
 *                 every 16 KB;
 *               - the register: bit 7 SRESET (as a RESET of the card), bit 3
 *                 IOEN (the card answers I/O), bit 0 PRIMARY (340h-35Fh, else
 *                 140h-15Fh); the rest reserved.  It is not a configuration
 *                 index, so the CIS's entries carry indexes 09h and 08h
 *                 (pccard_cis.c), which are the values that set it right;
 *               - the AIC-6360's 32 registers at that range, decoded on ten
 *                 address lines, in its overlapping address mode; its
 *                 interrupt is the card's IREQ;
 *               - pull-ups on D0-D7 make the AIC-6360's PORTA and PORTB read
 *                 FFh -- no jumpers on the card, and ASPI2DOS wants that FFh
 *                 to recognise it; PORTB's bit 6, written, switches the
 *                 active termination (nothing to emulate).
 *
 *             The SCSI bus is its socket's, which pcmcia.c keeps for the
 *             socket from the moment a SCSI card is in it until the next hard
 *             reset, so the disks put on that bus are there whenever the card
 *             is.
 *
 *             Windows 98 installs SCSI.INF's "Adaptec APA-1425/50/60 PCMCIA
 *             SCSI Host Adapter" (SPARROW.MPD) for it; Linux binds aha152x_cs
 *             by the VERS_1 strings.
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
#include <86box/scsi_aic6360.h>
#include <86box/pcmcia.h>
#include <86box/plat_unused.h>

#define COR_SRESET  0x80
#define COR_IOEN    0x08
#define COR_PRIMARY 0x01

typedef struct {
    pccard_t pccard;
    int      socket;
    void    *aic;
    uint8_t  cor;
    int      ireq;   /* the AIC-6360's interrupt output */
    uint8_t  cis[256];
    int      cis_len;
} apa1460_t;

#ifdef ENABLE_APA1460_LOG
int apa1460_do_log = ENABLE_APA1460_LOG;

static void
apa1460_log(const char *fmt, ...)
{
    va_list ap;

    if (apa1460_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define apa1460_log(fmt, ...)
#endif

static int
apa1460_io_enabled(const apa1460_t *dev)
{
    return (dev->cor & COR_IOEN) && !(dev->cor & COR_SRESET);
}

/* A port of the card's range, on the ten lines it decodes. */
static int
apa1460_decodes(const apa1460_t *dev, uint16_t port)
{
    return apa1460_io_enabled(dev) && ((port & 0x3e0) == ((dev->cor & COR_PRIMARY) ? 0x340 : 0x140));
}

/* Without IOEN the card is a memory card, whose IREQ pin is READY. */
static void
apa1460_irq_update(apa1460_t *dev)
{
    pcmcia_card_irq(dev->socket, dev->ireq && apa1460_io_enabled(dev));
}

static void
apa1460_aic_irq(void *priv, int level)
{
    apa1460_t *dev = (apa1460_t *) priv;

    dev->ireq = level;
    apa1460_irq_update(dev);
}

static uint8_t
apa1460_attr_read(uint32_t addr, void *priv)
{
    const apa1460_t *dev = (apa1460_t *) priv;

    addr &= 0x3fff;
    if (addr & 0x2000)
        return dev->cor;
    if (addr & 1)
        return 0xff;
    return ((addr >> 1) < (uint32_t) dev->cis_len) ? dev->cis[addr >> 1] : 0xff;
}

static void apa1460_card_reset(void *priv);

static void
apa1460_attr_write(uint32_t addr, uint8_t val, void *priv)
{
    apa1460_t *dev = (apa1460_t *) priv;

    if (!(addr & 0x2000))
        return;   /* the CIS EEPROM, write-protected */
    if (val & COR_SRESET)
        apa1460_card_reset(dev);
    dev->cor = val;
    apa1460_log("APA-1460: COR = %02X\n", val);
    apa1460_irq_update(dev);
}

static uint8_t
apa1460_io_read(uint16_t port, void *priv)
{
    const apa1460_t *dev = (apa1460_t *) priv;

    return apa1460_decodes(dev, port) ? aic6360_read(port, dev->aic) : 0xff;
}

static uint16_t
apa1460_io_readw(uint16_t port, void *priv)
{
    const apa1460_t *dev = (apa1460_t *) priv;

    return apa1460_decodes(dev, port) ? aic6360_readw(port, dev->aic) : 0xffff;
}

static void
apa1460_io_write(uint16_t port, uint8_t val, void *priv)
{
    const apa1460_t *dev = (apa1460_t *) priv;

    if (apa1460_decodes(dev, port))
        aic6360_write(port, val, dev->aic);
}

static void
apa1460_io_writew(uint16_t port, uint16_t val, void *priv)
{
    const apa1460_t *dev = (apa1460_t *) priv;

    if (apa1460_decodes(dev, port))
        aic6360_writew(port, val, dev->aic);
}

/* Power-up, the RESET pin or SRESET: I/O off, the chip reset. */
static void
apa1460_card_reset(void *priv)
{
    apa1460_t *dev = (apa1460_t *) priv;

    dev->cor = 0x00;
    aic6360_reset(dev->aic);
    apa1460_irq_update(dev);
}

static void *
apa1460_init(UNUSED(const device_t *info))
{
    apa1460_t *dev = (apa1460_t *) calloc(1, sizeof(apa1460_t));
    uint8_t    bus;

    dev->socket = device_get_instance() - 1;
    if ((dev->socket < 0) || (dev->socket >= PCMCIA_SOCKETS))
        dev->socket = 0;

    bus = pcmcia_scsi_bus(dev->socket);
    if (bus == 0xff) {
        pclog("APA-1460: no SCSI bus left for socket %c\n", 'A' + dev->socket);
        free(dev);
        return NULL;
    }

    dev->cis_len = pccard_cis_apa1460(dev->cis);
    dev->aic     = aic6360_chip_init(bus, 0xff, 0xff, apa1460_aic_irq, dev);
    apa1460_card_reset(dev);

    dev->pccard = (pccard_t) {
        .name       = "Adaptec APA-1460",
        .attr_read  = apa1460_attr_read,
        .attr_write = apa1460_attr_write,
        .io_read    = apa1460_io_read,
        .io_readw   = apa1460_io_readw,
        .io_write   = apa1460_io_write,
        .io_writew  = apa1460_io_writew,
        .reset      = apa1460_card_reset,
        .priv       = dev
    };
    pcmcia_insert(dev->socket, &dev->pccard);

    apa1460_log("APA-1460: socket %c, SCSI bus %i\n", 'A' + dev->socket, bus);
    return dev;
}

static void
apa1460_close(void *priv)
{
    apa1460_t *dev = (apa1460_t *) priv;

    pcmcia_insert(dev->socket, NULL);
    aic6360_chip_close(dev->aic);
    free(dev);
}

const device_t apa1460_device = {
    .name          = "Adaptec APA-1460 SlimSCSI",
    .internal_name = "apa1460",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = apa1460_init,
    .close         = apa1460_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL,
    .short_name    = "APA-1460"
};
