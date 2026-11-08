/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Micro Solutions BackPack parallel port floppy drive.
 *
 *          The pod is the bridge the Iomega Ditto rides on: the BackPack
 *          wire protocol (lpt_bpck_link.c), a 128 KB buffer the host reaches
 *          over the wire, and a 765 floppy controller of its own that moves
 *          sectors between the drive and that buffer. The controller here is
 *          86Box's own, hosted behind the pod, with the floppy drives of its
 *          bus behind it - floppy drive 5 when the machine has one controller
 *          of its own.
 *
 *          Modelled against Micro Solutions' BACKPACK.SYS 2.13: what crosses
 *          the port, and how the driver reads its drive table out of the
 *          pod's EEPROM.
 *
 * Authors: Mario Fetka
 *
 *          Copyright 2026 Mario Fetka.
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
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/lpt.h>
#include <86box/lpt_bpck_link.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_LPT_BPFDD_LOG
static void
bpfdd_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#    define BPFDD_LINK_LOG bpfdd_log
#else
#    define bpfdd_log(fmt, ...)
#    define BPFDD_LINK_LOG NULL
#endif

/* The register file, as far as the floppy pod uses it. */
#define BP_REG_STAT    0x00 /* bit 7 the EEPROM's DO, bit 0 a pending interrupt */
#define BP_REG_CTRL    0x04 /* transfer protocol, DMA direction */
#define BP_REG_EEPROM  0x06 /* the 93C46's lines; 0x80 arms the interrupt test */
#define BP_REG_INTRQ   0x0b /* bit 7: the controller's interrupt line */
#define BP_REG_IRQ     0x0f
#define BP_REG_TEST    0x13 /* protocol self test counter */
#define BP_REG_MEMADDR 0x28 /* buffer address, three bytes little endian */
#define BP_REG_DMAADDR 0x2c /* where the controller's transfer starts */
#define BP_REG_DMASIZE 0x30 /* its length, less one */
#define BP_REG_FDC     0x40 /* the 765's registers, eight of them */
#define BP_REG_MEMORY  0x80 /* buffer data, at the buffer address */
#define BP_REG_MEMORY2 0xa0 /* the same, as the Ditto's driver reaches it */

#define BP_STAT_IRQ    0x01
#define BP_STAT_ALWAYS 0x10
#define BP_STAT_EE_DO  0x80

#define BP_CTRL_PROTO_MASK 0x18
#define BP_CTRL_PROTO_PS2  0x10
#define BP_CTRL_PROTO_EPP  0x18

#define BP_INTRQ       0x80

#define BP_IRQ_ARM     0x80 /* written to register 0x06 */
#define BP_IRQ_PENDING 0x01
#define BP_IRQ_SOFTEN  0x10 /* let register 0x00 report pending interrupts */
#define BP_IRQ_HARDEN  0x18 /* additionally raise the port's interrupt */
#define BP_IRQ_MASK    0x18

/* The pod's type in word 2 of its EEPROM: no IDE drive behind it. */
#define BPFDD_EE_TYPE  0x0000
/* Drive 0: a 3.5" 1.44 MB drive, type 7, with the flags its driver gives it. */
#define BPFDD_EE_DRIVE 0x4307

#define BPFDD_BUFFER_SIZE 0x20000
#define BPFDD_BUFFER_MASK (BPFDD_BUFFER_SIZE - 1)

typedef struct bpfdd_t {
    bpck_link_t link;
    bpck_ee_t   ee;
    fdc_t      *fdc;

    uint8_t  reg[256];
    int      xfer_idx; /* bytes moved since the register was addressed */
    uint8_t  test_ctr;
    uint8_t  irq_ctrl;
    int      irq_arm;
    int      irq_pending; /* the test interrupt */
    int      fdc_irq;     /* the controller's interrupt line */

    uint8_t  buffer[BPFDD_BUFFER_SIZE];
    uint32_t mem_addr;
    uint32_t dma_addr;
    uint32_t dma_count; /* held as written - one less */
    uint32_t dma_left;  /* of the transfer under way */
    uint32_t addr_latch; /* the address being read back */
} bpfdd_t;

/*
   The identity EEPROM. Word 2 is the pod's type, 0 for one with no IDE
   drive behind it; words 3 to 6 describe floppy drives 0 to 3, the type
   in the low byte and its flags in the high byte, 0xFFFF for none. The
   driver always sets up a drive 1, of its DRIVETYPE= type when the word
   is empty. Words 0x37 to 0x3A hold the serial number.
 */
static const uint16_t bpfdd_ee_default[64] = {
    0x07F8, 0x0000, BPFDD_EE_TYPE, BPFDD_EE_DRIVE, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3204, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3638,
    0x4f42, 0x3058, 0x3030, 0x0C08, 0x07CD, 0x0C08, 0x07CD, 0x0000,
};

static void
bpfdd_set_addr_byte(uint32_t *addr, int idx, uint8_t val)
{
    if ((idx < 0) || (idx > 2))
        return;

    *addr &= ~((uint32_t) 0xff << (idx * 8));
    *addr |= (uint32_t) val << (idx * 8);
}

/*
   The interrupt the pod presents: the controller's, or the one the host
   provokes to find out which line the port is on. The controller's
   reaches the port only once the host has hardened it; the provoked one
   always does, as that is the point of it.
 */
static int
bpfdd_irq_pending(const bpfdd_t *dev)
{
    return dev->irq_pending || dev->fdc_irq;
}

static void
bpfdd_update_irq(bpfdd_t *dev, int test_poke)
{
    if (dev->link.lpt == NULL)
        return;

    lpt_irq(dev->link.lpt, bpfdd_irq_pending(dev) &&
                               (test_poke || ((dev->irq_ctrl & BP_IRQ_MASK) == BP_IRQ_HARDEN)));
}

/* --------------------------------------------------------------------- */
/* The controller's side: DMA into the buffer, and its interrupt         */
/* --------------------------------------------------------------------- */

static int
bpfdd_dma_read(void *priv)
{
    bpfdd_t *dev = (bpfdd_t *) priv;
    int      ret = dev->buffer[dev->dma_addr & BPFDD_BUFFER_MASK];

    dev->dma_addr = (dev->dma_addr + 1) & BPFDD_BUFFER_MASK;
    if (dev->dma_left > 0)
        dev->dma_left--;
    if (dev->dma_left == 0)
        ret |= DMA_OVER;

    return ret;
}

static int
bpfdd_dma_write(void *priv, uint8_t val)
{
    bpfdd_t *dev = (bpfdd_t *) priv;

    dev->buffer[dev->dma_addr & BPFDD_BUFFER_MASK] = val;
    dev->dma_addr = (dev->dma_addr + 1) & BPFDD_BUFFER_MASK;
    if (dev->dma_left > 0)
        dev->dma_left--;

    return (dev->dma_left == 0) ? DMA_OVER : 0;
}

static void
bpfdd_fdc_irq(void *priv, int set)
{
    bpfdd_t *dev = (bpfdd_t *) priv;

    if (dev->fdc_irq != !!set)
        bpfdd_log("BPFDD: controller interrupt %s\n", set ? "raised" : "cleared");

    dev->fdc_irq = !!set;
    bpfdd_update_irq(dev, 0);
}

static const fdc_host_t bpfdd_fdc_host = {
    .dma_read  = bpfdd_dma_read,
    .dma_write = bpfdd_dma_write,
    .drq       = NULL,
    .irq       = bpfdd_fdc_irq
};

/* --------------------------------------------------------------------- */
/* The register file                                                     */
/* --------------------------------------------------------------------- */

static uint8_t
bpfdd_read_reg(void *priv, int reg)
{
    bpfdd_t  *dev = (bpfdd_t *) priv;
    const int idx = dev->xfer_idx++;
    uint8_t   ret;

    reg &= 0xff;

    if ((reg >= BP_REG_FDC) && (reg < (BP_REG_FDC + 8))) {
        ret = fdc_read((uint16_t) (reg - BP_REG_FDC), dev->fdc);
        bpfdd_log("BPFDD:    RR %02X -> %02X (FDC)\n", reg, ret);
        return ret;
    }

    switch (reg) {
        case BP_REG_STAT:
            ret = BP_STAT_ALWAYS;
            if (bpfdd_irq_pending(dev) && (dev->irq_ctrl & BP_IRQ_SOFTEN))
                ret |= BP_STAT_IRQ;
            if (dev->ee.dout)
                ret |= BP_STAT_EE_DO;
            break;

        case BP_REG_INTRQ:
            /* The driver polls this for the end of every controller command. */
            ret = dev->fdc_irq ? BP_INTRQ : 0x00;
            break;

        case BP_REG_IRQ:
            ret = (uint8_t) (dev->irq_ctrl & BP_IRQ_MASK);
            if (bpfdd_irq_pending(dev))
                ret |= BP_IRQ_PENDING;
            break;

        case BP_REG_TEST:
            ret = ++dev->test_ctr;
            break;

        case BP_REG_MEMADDR:
        case BP_REG_DMAADDR:
            /*
               Both read back where they have got to: the driver follows the
               controller's address while the sector comes in. The first byte
               takes a snapshot, so that the other two cannot tear against a
               transfer that carries into them.
             */
            if (idx == 0)
                dev->addr_latch = (reg == BP_REG_MEMADDR) ? dev->mem_addr : dev->dma_addr;
            ret = (idx > 2) ? 0x00 : (uint8_t) (dev->addr_latch >> (idx * 8));
            break;

        case BP_REG_MEMORY:
        case BP_REG_MEMORY2:
            ret           = dev->buffer[dev->mem_addr & BPFDD_BUFFER_MASK];
            dev->mem_addr = (dev->mem_addr + 1) & BPFDD_BUFFER_MASK;
            return ret;

        default:
            ret = dev->reg[reg];
            break;
    }

    bpfdd_log("BPFDD:    RR %02X[%i] -> %02X\n", reg, idx, ret);

    return ret;
}

static void
bpfdd_write_reg(void *priv, int reg, uint8_t val)
{
    bpfdd_t  *dev = (bpfdd_t *) priv;
    const int idx = dev->xfer_idx++;

    reg &= 0xff;

    if ((reg >= BP_REG_FDC) && (reg < (BP_REG_FDC + 8))) {
        bpfdd_log("BPFDD:    WR %02X <- %02X (FDC)\n", reg, val);
        fdc_write((uint16_t) (reg - BP_REG_FDC), val, dev->fdc);
        return;
    }

    if ((reg == BP_REG_MEMORY) || (reg == BP_REG_MEMORY2)) {
        dev->buffer[dev->mem_addr & BPFDD_BUFFER_MASK] = val;
        dev->mem_addr = (dev->mem_addr + 1) & BPFDD_BUFFER_MASK;
        return;
    }

    bpfdd_log("BPFDD:    WR %02X[%i] <- %02X\n", reg, idx, val);

    dev->reg[reg] = val;

    switch (reg) {
        case BP_REG_CTRL:
            /*
               The protocol, at once: the host switches and then talks.
               Writing here also raises the test interrupt once armed.
             */
            switch (val & BP_CTRL_PROTO_MASK) {
                case BP_CTRL_PROTO_EPP:
                    dev->link.proto = BPCK_PROTO_EPP8;
                    break;
                case BP_CTRL_PROTO_PS2:
                    dev->link.proto = BPCK_PROTO_PS2;
                    break;
                default:
                    dev->link.proto = BPCK_PROTO_SPP;
                    break;
            }

            if (dev->irq_arm) {
                dev->irq_pending = 1;
                bpfdd_update_irq(dev, 1);
            }
            break;

        case BP_REG_EEPROM:
            bpck_ee_write(&dev->ee, val, "BPFDD", BPFDD_LINK_LOG);
            dev->irq_arm = (val == BP_IRQ_ARM);
            if (!dev->irq_arm && (val == 0x00)) {
                dev->irq_pending = 0;
                bpfdd_update_irq(dev, 0);
            }
            break;

        case BP_REG_IRQ:
            dev->irq_ctrl = (uint8_t) (val & BP_IRQ_MASK);
            bpfdd_update_irq(dev, 0);
            break;

        case BP_REG_TEST:
            dev->test_ctr = 0;
            break;

        case BP_REG_MEMADDR:
            bpfdd_set_addr_byte(&dev->mem_addr, idx, val);
            break;

        case BP_REG_DMAADDR:
            bpfdd_set_addr_byte(&dev->dma_addr, idx, val);
            break;

        case BP_REG_DMASIZE:
            bpfdd_set_addr_byte(&dev->dma_count, idx, val);
            dev->dma_left = (dev->dma_count & BPFDD_BUFFER_MASK) + 1;
            break;

        default:
            break;
    }
}

static void
bpfdd_link_address(void *priv)
{
    bpfdd_t *dev = (bpfdd_t *) priv;

    dev->xfer_idx = 0;
}

static const bpck_link_ops_t bpfdd_link_ops = {
    .read_reg  = bpfdd_read_reg,
    .write_reg = bpfdd_write_reg,
    .address   = bpfdd_link_address
};

/* --------------------------------------------------------------------- */
/* 86Box device plumbing                                                 */
/* --------------------------------------------------------------------- */

static void *
bpfdd_init(UNUSED(const device_t *info))
{
    bpfdd_t *dev = calloc(1, sizeof(bpfdd_t));

    if (dev == NULL)
        return NULL;

    memcpy(dev->ee.data, bpfdd_ee_default, sizeof(dev->ee.data));

    dev->link.unit      = (uint8_t) device_get_config_int("unit");
    dev->link.proto     = BPCK_PROTO_SPP;
    dev->link.max_proto = BPCK_PROTO_EPP32;

    if (!bpck_link_attach(&dev->link, &bpfdd_link_ops, dev, "BPFDD", BPFDD_LINK_LOG)) {
        /* Another device already has this port. */
        free(dev);
        return NULL;
    }

    dev->fdc = device_add(&fdc_hosted_device);
    fdc_set_host(dev->fdc, &bpfdd_fdc_host, dev);

    bpfdd_log("BPFDD: attached as unit %02X, floppy drives %i to %i\n",
              dev->link.unit, dev->fdc->bus + 1, dev->fdc->bus + 4);

    return dev;
}

static void
bpfdd_close(void *priv)
{
    free(priv);
}

// clang-format off
static const device_config_t bpfdd_config[] = {
    {
        .name           = "unit",
        .description    = "Chain address",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Unit 0", .value = 0 },
            { .description = "Unit 1", .value = 1 },
            { .description = "Unit 2", .value = 2 },
            { .description = "Unit 3", .value = 3 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t lpt_bpfdd_device = {
    .name          = "Micro Solutions BackPack floppy drive",
    .internal_name = "bpfdd",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = bpfdd_init,
    .close         = bpfdd_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = bpfdd_config
};
