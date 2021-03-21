/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the UMC HB4(8881F/8886xx) "Super Energy Star Green" PCI Chipset.
 *
 *		Note: This chipset has no datasheet, everything were done via
 *		reverse engineering the BIOS of various machines using it.
 * 
 *		Note 2: Additional information were also used from all
 *		around the web.
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
 */

/*
UMC HB4 Configuration Registers

Sources & Notes:
Cache registers were found at Vogons: https://www.vogons.org/viewtopic.php?f=46&t=68829&start=20
Basic Reverse engineering effort was done personally by me

TODO:
- APM, SMM, SMRAM registers(Did some early work. Still quite incomplete)
- More Appropriate Bitmasking(If it's even possible)

Warning: Register documentation may be inaccurate!

UMC 8881x:

Register 50:
Bit 7: Enable L2 Cache
Bit 6: Cache Policy (0: Write Thru / 1: Write Back)

Bit 5-4 Cache Speed
    0 0 Read 3-2-2-2 Write 3T
    0 1 Read 3-1-1-1 Write 3T
    1 0 Read 2-2-2-2 Write 2T
    1 1 Read 2-1-1-1 Write 2T

Bit 3 Cache Banks (0: 1 Bank / 1: 2 Banks)

Bit 2-1-0 Cache Size
    0 0 0 0KB
    0 0 1 64KB
    x-x-x Multiplications of 2(64*2 for 0 1 0) till 2MB

Register 51:
Bit 7-6 DRAM Read Speed
    5-4 DRAM Write Speed
    0 0 1 Waits
    0 1 1 Waits
    1 0 1 Wait
    1 1 0 Waits

Bit 3 Resource Lock Enable
Bit 2 Graphics Adapter (0: VL Bus / 1: PCI Bus)
Bit 1 L1 WB Policy (0: WT / 1: WB)
Bit 0 L2 Cache Tag Lenght (0: 7 Bits / 1: 8 Bits)

Register 52:
Bit 7: Host-to-PCI Post Write (0: 1 Wait State / 1: 0 Wait States)

Register 54:
Bit 7: DC000-DFFFF
Bit 6: D8000-DBFFF
Bit 5: D4000-D7FFF
Bit 4: D0000-D3FFF
Bit 3: CC000-CFFFF
Bit 2: C8000-CBFFF
Bit 1: C0000-C7FFF
Bit 0: Reserved

Register 55:
Bit 7: Enable Shadow Reads For System & Selected Segments
Bit 6: Write Protect Enable

UMC 8886xx:
(F: Has No Internal IDE / AF or BF: Has Internal IDE)

Function 0 Register 43:
Bits 7-4 PCI IRQ for INTB
Bits 3-0 PCI IRQ for INTA

Function 0 Register 44:
Bits 7-4 PCI IRQ for INTD
Bits 3-0 PCI IRQ for INTC

Function 0 Register 46:
Bit 7: Replace SMI request for non-SMM CPU's (1: IRQ15/0: IRQ10)

Function 0 Register 51:
Bit 2: VGA Power Down (0: Standard/1: VESA DPMS)

Function 1 Register 4:
Bit 0: Enable Internal IDE
*/

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/apm.h>
#include <86box/hdd.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/smram.h>

#include <86box/chipset.h>

#ifdef ENABLE_HB4_LOG
int hb4_do_log = ENABLE_HB4_LOG;
static void
hb4_log(const char *fmt, ...)
{
    va_list ap;

    if (hb4_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define hb4_log(fmt, ...)
#endif

/* Shadow RAM Flags */
#define CAN_READ ((dev->pci_conf[0x55] & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY)
#define CAN_WRITE ((dev->pci_conf[0x55] & 0x40) ? MEM_WRITE_DISABLED : MEM_WRITE_INTERNAL)
#define DISABLE (MEM_READ_EXTANY | MEM_WRITE_EXTANY)

/* PCI IRQ Flags */
#define INTA (PCI_INTA + (2 * !(addr & 1)))
#define INTB (PCI_INTB + (2 * !(addr & 1)))
#define IRQRECALCA (((val & 0xf0) != 0) ? ((val & 0xf0) >> 4) : PCI_IRQ_DISABLED)
#define IRQRECALCB (((val & 0x0f) != 0) ? (val & 0x0f) : PCI_IRQ_DISABLED)

/* Disable Internal IDE Flag needed for the BF Southbridge variant */
#define HAS_IDE dev->has_ide

/* Southbridge Revision */
#define SB_ID dev->sb_id

typedef struct hb4_t
{
    apm_t *apm;
    smram_t *smram;

    uint8_t pci_conf[256], pci_conf_sb[2][256]; /* PCI Registers */
    uint16_t sb_id;                             /* Southbridge Revision */
    int has_ide;                                /* Check if Southbridge Revision is AF or F */
} hb4_t;

void hb4_shadow(int cur_addr, hb4_t *dev)
{
    mem_set_mem_state_both(0xc0000, 0x8000, (dev->pci_conf[0x54] & 2) ? (CAN_READ | CAN_WRITE) : DISABLE);
    for (int i = 2; i < 8; i++)
        mem_set_mem_state_both(0xc8000 + ((i - 2) << 14), 0x4000, (dev->pci_conf[0x54] & (1 << i)) ? (CAN_READ | CAN_WRITE) : DISABLE);

    mem_set_mem_state_both(0xe0000, 0x20000, CAN_READ | CAN_WRITE);
}

void ide_handler(int status)
{
    ide_pri_disable();
    ide_sec_disable();

    if (status)
    {
        ide_pri_enable();
        ide_sec_enable();
    }
}

static void
um8881_write(int func, int addr, uint8_t val, void *priv)
{
    hb4_t *dev = (hb4_t *)priv;
    hb4_log("UM8881: dev->regs[%02x] = %02x\n", addr, val);

    if (addr > 3) /* We don't know the RW status of registers but Phoenix writes on some RO registers too*/

        switch (addr)
        {
        case 0x50:
            dev->pci_conf[addr] = ((val & 0xf8) | 4); /* Hardcode Cache Size to 512KB */
            cpu_cache_ext_enabled = !!(val & 0x80);   /* Fixes freezing issues on the HOT-433A*/
            cpu_update_waitstates();
            break;

        case 0x54:
        case 0x55:
            dev->pci_conf[addr] = val & (!(addr & 1) ? 0xfe : 0xff);
            hb4_shadow(addr, dev);
            break;

        case 0x60:
            dev->pci_conf[addr] = val & 0x3f;
            break;

        case 0x61:
            dev->pci_conf[addr] = val & 0x0f;
            break;

        default:
            dev->pci_conf[addr] = val;
            break;
        }
}

static uint8_t
um8881_read(int func, int addr, void *priv)
{
    hb4_t *dev = (hb4_t *)priv;
    return dev->pci_conf[addr];
}

static void
um8886_write(int func, int addr, uint8_t val, void *priv)
{

    hb4_t *dev = (hb4_t *)priv;
    hb4_log("UM8886: dev->regs[%02x] = %02x (%02x)\n", addr, val, func);

    if (addr > 3) /* We don't know the RW status of registers but Phoenix writes on some RO registers too*/

        switch (func)
        {
        case 0: /* Southbridge */
            switch (addr)
            {
            case 0x43:
            case 0x44:
                dev->pci_conf_sb[func][addr] = val;
                pci_set_irq_routing(INTA, IRQRECALCA);
                pci_set_irq_routing(INTB, IRQRECALCB);
                break;

            case 0x46:
                dev->pci_conf_sb[func][addr] = val & 0xaf;
                break;

            case 0x47:
                dev->pci_conf_sb[func][addr] = val & 0x4f;
                break;

            case 0x57:
                dev->pci_conf_sb[func][addr] = val & 0x38;
                break;

            case 0x71:
                dev->pci_conf_sb[func][addr] = val & 1;
                break;

            case 0x90:
                dev->pci_conf_sb[func][addr] = val & 2;
                break;

            case 0x92:
                dev->pci_conf_sb[func][addr] = val & 0x1f;
                break;

            case 0xa0:
                dev->pci_conf_sb[func][addr] = val & 0xfc;
                break;

            case 0xa4:
                dev->pci_conf_sb[func][addr] = val & 0x88;
                break;

            default:
                dev->pci_conf_sb[func][addr] = val;
                break;
            }
            break;
        case 1: /* IDE Controller */
            if ((addr == 4) && HAS_IDE)
            {
                dev->pci_conf_sb[func][addr] = val;
                ide_handler(val & 1);
            }
            break;
        }
}

static uint8_t
um8886_read(int func, int addr, void *priv)
{
    hb4_t *dev = (hb4_t *)priv;
    return dev->pci_conf_sb[func][addr];
}

static void
hb4_reset(void *priv)
{
    hb4_t *dev = (hb4_t *)priv;

    /* Defaults */
    dev->pci_conf[0] = 0x60; /* UMC */
    dev->pci_conf[1] = 0x10;

    dev->pci_conf[2] = 0x81; /* 8881x */
    dev->pci_conf[3] = 0x88;

    dev->pci_conf[8] = 1;

    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    dev->pci_conf_sb[0][0] = 0x60; /* UMC */
    dev->pci_conf_sb[0][1] = 0x10;

    dev->pci_conf_sb[0][2] = (SB_ID & 0xff); /* 8886xx */
    dev->pci_conf_sb[0][3] = ((SB_ID >> 8) & 0xff);

    dev->pci_conf_sb[0][8] = 1;

    dev->pci_conf_sb[0][0x09] = 0x00;
    dev->pci_conf_sb[0][0x0a] = 0x01;
    dev->pci_conf_sb[0][0x0b] = 0x06;

    for (int i = 1; i < 5; i++) /* Disable all IRQ interrupts */
        pci_set_irq_routing(i, PCI_IRQ_DISABLED);

    if (HAS_IDE)
    {
        dev->pci_conf_sb[1][4] = 1; /* Start with Internal IDE Enabled */

        ide_handler(1);
    }
}

static void
hb4_close(void *priv)
{
    hb4_t *dev = (hb4_t *)priv;

    //smram_del(dev->smram);
    free(dev);
}

static void *
hb4_init(const device_t *info)
{
    hb4_t *dev = (hb4_t *)malloc(sizeof(hb4_t));
    memset(dev, 0, sizeof(hb4_t));

    dev->has_ide = (info->local & 0x886a);
    pci_add_card(PCI_ADD_NORTHBRIDGE, um8881_read, um8881_write, dev); /* Device 10: UMC 8881x */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, um8886_read, um8886_write, dev); /* Device 12: UMC 8886xx */

    /* APM */
    dev->apm = device_add(&apm_pci_device);

    /* SMRAM(Needs excessive documentation before we begin SMM implementation) */
    //dev->smram = smram_add();

    /* Port 92 */
    device_add(&port_92_pci_device);

    /* Add IDE if UM8886AF variant */
    if (HAS_IDE)
        device_add(&ide_pci_2ch_device);

    /* Get the Southbridge Revision */
    SB_ID = info->local;

    hb4_reset(dev);

    return dev;
}

const device_t umc_hb4_device = {
    "UMC HB4(8881F/8886AF)",
    DEVICE_PCI,
    0x886a,
    hb4_init,
    hb4_close,
    hb4_reset,
    {NULL},
    NULL,
    NULL,
    NULL};

const device_t umc_hb4_early_device = {
    "UMC HB4(8881F/8886F)",
    DEVICE_PCI,
    0x8886,
    hb4_init,
    hb4_close,
    hb4_reset,
    {NULL},
    NULL,
    NULL,
    NULL};
