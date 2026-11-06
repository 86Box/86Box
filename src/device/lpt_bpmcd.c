/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Micro Solutions BackPack CD-ROM, models 160550 and 162550.
 *
 *          The first BackPack CD-ROMs, before the ATAPI ones (163550 on,
 *          the Bantam among them, lpt_bpck.c), carry a Mitsumi FX001 or
 *          FX001D drive. The pod speaks the BackPack wire protocol
 *          (lpt_bpck_link.c) and passes the drive's own interface through
 *          two of its registers: 0x40 is the Mitsumi data and command
 *          register, 0x41 its flags (and, written, its reset). The drive is
 *          86Box's Mitsumi CD-ROM, hosted behind the pod: the first CD-ROM
 *          drive on the Mitsumi bus that no other interface took. The pods
 *          take their drives after the CD-ROM drives were reset and the
 *          CD-ROM interface card took its one (cdrom_interface_reset()).
 *
 *          Modelled from Grant Guenther's Linux driver for these drives,
 *          bpmcd.c (1996, 1997), and against Micro Solutions' BPCDDRV.SYS
 *          1.06 as a black box.
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
#include <86box/lpt.h>
#include <86box/lpt_bpck_link.h>
#include <86box/cdrom_mitsumi.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_LPT_BPMCD_LOG
static void
bpmcd_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#    define BPMCD_LINK_LOG bpmcd_log
#else
#    define bpmcd_log(fmt, ...)
#    define BPMCD_LINK_LOG NULL
#endif

/* The register file, as far as the drivers use it. */
#define BP_REG_STAT    0x00 /* bit 7: the EEPROM's data line */
#define BP_REG_CTRL    0x04 /* transfer protocol */
#define BP_REG_EEPROM  0x06 /* the EEPROM's lines */
#define BP_REG_TEST    0x13 /* counts the bytes read since it was written */
#define BP_REG_MCD     0x40 /* the Mitsumi interface, registers 0 and 1 */

#define BP_STAT_ALWAYS 0x04 /* bpmcd.c takes 0x84 for a one from the EEPROM */
#define BP_STAT_EE_DO  0x80

#define BP_CTRL_PROTO_MASK 0x18
#define BP_CTRL_PROTO_PS2  0x10
#define BP_CTRL_PROTO_EPP  0x18

typedef struct bpmcd_t {
    bpck_link_t link;
    bpck_ee_t   ee;
    void       *mcd;

    uint8_t reg[256];
    uint8_t test_ctr;
} bpmcd_t;

/* The pods, until they have their drives. */
static bpmcd_t *bpmcd_pods[PARALLEL_MAX];

/*
   The identity EEPROM, laid out as the Bantam's: word 2 the pod's type,
   from word 3 the drive's name as IDENTIFY spells it, words 0x37 to 0x3A
   the serial number, which bpmcd.c prints. No dump of one of these early
   pods is known. The type word is the one BPCDDRV.SYS 1.06 takes for a
   Mitsumi pod: 0080h is the hard disk, 0082h the ATAPI CD-ROM.
 */
static const uint16_t bpmcd_ee_default[64] = {
    0x07F8, 0x0000, 0x0081, 0x494D, 0x5354, 0x4D55, 0x2049, 0x5243,
    0x434D, 0x462D, 0x3058, 0x3130, 0x2044, 0x2020, 0x2020, 0x2020,
    0x2020, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3638,
    0x4f42, 0x3058, 0x3030, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static uint8_t
bpmcd_read_reg(void *priv, int reg)
{
    bpmcd_t *dev = (bpmcd_t *) priv;
    uint8_t  ret;

    reg &= 0xff;

    switch (reg) {
        case BP_REG_MCD:
        case BP_REG_MCD + 1:
            /* No log: the data register streams a sector a byte at a time. */
            if (dev->mcd == NULL)
                return 0xff; /* No drive in the pod. */
            return mitsumi_cdrom_host_read(dev->mcd, reg - BP_REG_MCD);

        case BP_REG_STAT:
            ret = BP_STAT_ALWAYS;
            if (dev->ee.dout)
                ret |= BP_STAT_EE_DO;
            break;

        case BP_REG_TEST:
            ret = ++dev->test_ctr;
            break;

        default:
            ret = dev->reg[reg];
            break;
    }

    bpmcd_log("BPMCD:    RR %02X -> %02X\n", reg, ret);

    return ret;
}

static void
bpmcd_write_reg(void *priv, int reg, uint8_t val)
{
    bpmcd_t *dev = (bpmcd_t *) priv;

    reg &= 0xff;

    bpmcd_log("BPMCD:    WR %02X <- %02X\n", reg, val);

    switch (reg) {
        case BP_REG_MCD:
        case BP_REG_MCD + 1:
            if (dev->mcd != NULL)
                mitsumi_cdrom_host_write(dev->mcd, reg - BP_REG_MCD, val);
            return;

        case BP_REG_CTRL:
            /* The protocol, at once: the host switches and then talks. */
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
            break;

        case BP_REG_EEPROM:
            bpck_ee_write(&dev->ee, val, "BPMCD", BPMCD_LINK_LOG);
            break;

        case BP_REG_TEST:
            dev->test_ctr = 0;
            break;

        default:
            break;
    }

    dev->reg[reg] = val;
}

static const bpck_link_ops_t bpmcd_link_ops = {
    .read_reg  = bpmcd_read_reg,
    .write_reg = bpmcd_write_reg
};

static void *
bpmcd_init(UNUSED(const device_t *info))
{
    bpmcd_t *dev = calloc(1, sizeof(bpmcd_t));

    if (dev == NULL)
        return NULL;

    memcpy(dev->ee.data, bpmcd_ee_default, sizeof(dev->ee.data));

    dev->link.unit      = (uint8_t) device_get_config_int("unit");
    dev->link.proto     = BPCK_PROTO_SPP;
    dev->link.max_proto = BPCK_PROTO_EPP8;

    if (!bpck_link_attach(&dev->link, &bpmcd_link_ops, dev, "BPMCD", BPMCD_LINK_LOG)) {
        /* Another device already has this port. */
        free(dev);
        return NULL;
    }

    bpmcd_log("BPMCD: attached as unit %02X\n", dev->link.unit);

    /* The drive comes in lpt_bpmcd_attach_drives(). */
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        if (bpmcd_pods[i] == NULL) {
            bpmcd_pods[i] = dev;
            break;
        }
    }

    return dev;
}

/*
   Give each pod a Mitsumi drive. The CD-ROM drives are reset after the
   parallel port devices are added, which would take the drive back from
   an interface that took it before, so this is called after that reset,
   once the CD-ROM interface card has taken its drive.
 */
void
lpt_bpmcd_attach_drives(void)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        bpmcd_t *dev = bpmcd_pods[i];

        if ((dev == NULL) || (dev->mcd != NULL))
            continue;

        dev->mcd = device_add_inst(&mitsumi_cdrom_hosted_device, i + 1);
        if (dev->mcd == NULL)
            bpmcd_log("BPMCD: no CD-ROM drive left on the Mitsumi bus\n");
    }
}

static void
bpmcd_close(void *priv)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        if (bpmcd_pods[i] == priv)
            bpmcd_pods[i] = NULL;
    }

    free(priv);
}

// clang-format off
static const device_config_t bpmcd_config[] = {
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

const device_t lpt_bpmcd_device = {
    .name          = "Micro Solutions BackPack CD-ROM (Mitsumi, 160550/162550)",
    .internal_name = "bpmcd",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = bpmcd_init,
    .close         = bpmcd_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = bpmcd_config
};
