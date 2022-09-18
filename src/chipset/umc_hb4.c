/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the UMC HB4 "Super Energy Star Green" PCI Chipset.
 *
 *		Note: This chipset has no datasheet, everything were done via
 *		reverse engineering the BIOS of various machines using it.
 *
 *		Note 2: Additional information were also used from all
 *		around the web.
 *
 * Authors:	Tiseno100,
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Tiseno100.
 *		Copyright 2021 Miran Grca.
 */

/*
   UMC HB4 Configuration Registers

   Sources & Notes:
   Cache registers were found at Vogons: https://www.vogons.org/viewtopic.php?f=46&t=68829&start=20
   Basic Reverse engineering effort was done personally by me

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
   Bit 7: DC000-DFFFF Read Enable
   Bit 6: D8000-DBFFF Read Enable
   Bit 5: D4000-D7FFF Read Enable
   Bit 4: D0000-D3FFF Read Enable
   Bit 3: CC000-CFFFF Read Enable
   Bit 2: C8000-CBFFF Read Enable
   Bit 1: C0000-C7FFF Read Enable
   Bit 0: Enable C0000-DFFFF Shadow Segment Bits

   Register 55:
   Bit 7: E0000-FFFF Read Enable
   Bit 6: Shadow Write Status (1: Write Protect/0: Write)

   Register 56h & 57h: DRAM Bank 0 Configuration
   Register 58h & 59h: DRAM Bank 1 Configuration

   Register 60:
   Bit 5: If set and SMRAM is enabled, data cycles go to PCI and code cycles go to DRAM
   Bit 0: SMRAM Local Access Enable - if set, SMRAM is also enabled outside SMM
          SMRAM appears to always be enabled in SMM, and always set to A0000-BFFFF.
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
#include "x86.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/port_92.h>
#include <86box/smram.h>

#ifdef USE_DYNAREC
#    include "codegen_public.h"
#else
#    ifdef USE_NEW_DYNAREC
#        define PAGE_MASK_SHIFT 6
#    else
#        define PAGE_MASK_INDEX_MASK  3
#        define PAGE_MASK_INDEX_SHIFT 10
#        define PAGE_MASK_SHIFT       4
#    endif
#    define PAGE_MASK_MASK 63
#endif
#include <86box/chipset.h>

#ifdef ENABLE_HB4_LOG
int hb4_do_log = ENABLE_HB4_LOG;

static void
hb4_log(const char *fmt, ...)
{
    va_list ap;

    if (hb4_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define hb4_log(fmt, ...)
#endif

typedef struct hb4_t {
    uint8_t shadow,
        shadow_read, shadow_write,
        pci_conf[256]; /* PCI Registers */
    int      mem_state[9];
    smram_t *smram[3]; /* SMRAM Handlers */
} hb4_t;

static int shadow_bios[4]  = { (MEM_READ_EXTANY | MEM_WRITE_INTERNAL), (MEM_READ_EXTANY | MEM_WRITE_EXTANY),
                               (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL), (MEM_READ_INTERNAL | MEM_WRITE_EXTANY) };
static int shadow_read[2]  = { MEM_READ_EXTANY, MEM_READ_INTERNAL };
static int shadow_write[2] = { MEM_WRITE_INTERNAL, MEM_WRITE_EXTANY };

int
hb4_shadow_bios_high(hb4_t *dev)
{
    int state;

    state = shadow_bios[dev->pci_conf[0x55] >> 6];

    if (state != dev->mem_state[8]) {
        mem_set_mem_state_both(0xf0000, 0x10000, state);
        if ((dev->mem_state[8] & MEM_READ_INTERNAL) && !(state & MEM_READ_INTERNAL))
            mem_invalidate_range(0xf0000, 0xfffff);
        dev->mem_state[8] = state;
        return 1;
    }

    return 0;
}

int
hb4_shadow_bios_low(hb4_t *dev)
{
    int state;

    state = shadow_bios[(dev->pci_conf[0x55] >> 6) & (dev->shadow | 0x01)];

    if (state != dev->mem_state[7]) {
        mem_set_mem_state_both(0xe0000, 0x10000, state);
        dev->mem_state[7] = state;
        return 1;
    }

    return 0;
}

int
hb4_shadow_main(hb4_t *dev)
{
    int i, state;
    int n = 0;

    for (i = 0; i < 6; i++) {
        state = shadow_read[dev->shadow && ((dev->pci_conf[0x54] >> (i + 2)) & 0x01)] | shadow_write[(dev->pci_conf[0x55] >> 6) & 0x01];

        if (state != dev->mem_state[i + 1]) {
            n++;
            mem_set_mem_state_both(0xc8000 + (i << 14), 0x4000, state);
            dev->mem_state[i + 1] = state;
        }
    }

    return n;
}

int
hb4_shadow_video(hb4_t *dev)
{
    int state;

    state = shadow_read[dev->shadow && ((dev->pci_conf[0x54] >> 1) & 0x01)] | shadow_write[(dev->pci_conf[0x55] >> 6) & 0x01];

    if (state != dev->mem_state[0]) {
        mem_set_mem_state_both(0xc0000, 0x8000, state);
        dev->mem_state[0] = state;
        return 1;
    }

    return 0;
}

void
hb4_shadow(hb4_t *dev)
{
    int n = 0;
    hb4_log("SHADOW: %02X%02X\n", dev->pci_conf[0x55], dev->pci_conf[0x54]);

    n = hb4_shadow_bios_high(dev);
    n += hb4_shadow_bios_low(dev);
    n += hb4_shadow_main(dev);
    n += hb4_shadow_video(dev);

    if (n > 0)
        flushmmucache_nopc();
}

static void
hb4_smram(hb4_t *dev)
{
    smram_disable_all();

    /* Bit 0, if set, enables SMRAM access outside SMM. SMRAM appears to be always enabled
       in SMM, and is always set to A0000-BFFFF. */
    smram_enable(dev->smram[0], 0x000a0000, 0x000a0000, 0x20000, dev->pci_conf[0x60] & 0x01, 1);
    /* There's a mirror of the SMRAM at 0E0A0000, mapped to A0000. */
    smram_enable(dev->smram[1], 0x0e0a0000, 0x000a0000, 0x20000, dev->pci_conf[0x60] & 0x01, 1);
    /* There's another mirror of the SMRAM at 4E0A0000, mapped to A0000. */
    smram_enable(dev->smram[2], 0x4e0a0000, 0x000a0000, 0x20000, dev->pci_conf[0x60] & 0x01, 1);

    /* Bit 5 seems to set data to go to PCI and code to DRAM. The Samsung SPC7700P-LW uses
       this. */
    if (dev->pci_conf[0x60] & 0x20) {
        if (dev->pci_conf[0x60] & 0x01)
            mem_set_mem_state_smram_ex(0, 0x000a0000, 0x20000, 0x02);
        mem_set_mem_state_smram_ex(1, 0x000a0000, 0x20000, 0x02);
    }
}

static void
hb4_write(int func, int addr, uint8_t val, void *priv)
{
    hb4_t *dev = (hb4_t *) priv;

    hb4_log("UM8881: dev->regs[%02x] = %02x POST: %02x \n", addr, val, inb(0x80));

    switch (addr) {
        case 0x04:
        case 0x05:
            dev->pci_conf[addr] = val;
            break;

        case 0x07:
            dev->pci_conf[addr] &= ~(val & 0xf9);
            break;

        case 0x0c:
        case 0x0d:
            dev->pci_conf[addr] = val;
            break;

        case 0x50:
            dev->pci_conf[addr]   = ((val & 0xf8) | 4); /* Hardcode Cache Size to 512KB */
            cpu_cache_ext_enabled = !!(val & 0x80);     /* Fixes freezing issues on the HOT-433A*/
            cpu_update_waitstates();
            break;

        case 0x51:
        case 0x52:
            dev->pci_conf[addr] = val;
            break;

        case 0x53:
            dev->pci_conf[addr] = val;
            hb4_log("HB53: %02X\n", val);
            break;

        case 0x55:
            dev->shadow_read    = (val & 0x80);
            dev->shadow_write   = (val & 0x40);
            dev->pci_conf[addr] = val;
            hb4_shadow(dev);
            break;
        case 0x54:
            dev->shadow         = (val & 0x01) << 1;
            dev->pci_conf[addr] = val;
            hb4_shadow(dev);
            break;

        case 0x56 ... 0x5f:
            dev->pci_conf[addr] = val;
            break;

        case 0x60:
            dev->pci_conf[addr] = val;
            hb4_smram(dev);
            break;

        case 0x61:
            dev->pci_conf[addr] = val;
            break;
    }
}

static uint8_t
hb4_read(int func, int addr, void *priv)
{
    hb4_t  *dev = (hb4_t *) priv;
    uint8_t ret = 0xff;

    if (func == 0)
        ret = dev->pci_conf[addr];

    return ret;
}

static void
hb4_reset(void *priv)
{
    hb4_t *dev = (hb4_t *) priv;
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    dev->pci_conf[0] = 0x60; /* UMC */
    dev->pci_conf[1] = 0x10;

    dev->pci_conf[2] = 0x81; /* 8881x */
    dev->pci_conf[3] = 0x88;

    dev->pci_conf[7] = 2;

    dev->pci_conf[8] = 4;

    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;

    dev->pci_conf[0x51] = 1;
    dev->pci_conf[0x52] = 1;
    dev->pci_conf[0x5a] = 4;
    dev->pci_conf[0x5c] = 0xc0;
    dev->pci_conf[0x5d] = 0x20;
    dev->pci_conf[0x5f] = 0xff;

    hb4_write(0, 0x54, 0x00, dev);
    hb4_write(0, 0x55, 0x00, dev);
    hb4_write(0, 0x60, 0x80, dev);

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    memset(dev->mem_state, 0x00, sizeof(dev->mem_state));
}

static void
hb4_close(void *priv)
{
    hb4_t *dev = (hb4_t *) priv;

    free(dev);
}

static void *
hb4_init(const device_t *info)
{
    hb4_t *dev = (hb4_t *) malloc(sizeof(hb4_t));
    memset(dev, 0, sizeof(hb4_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, hb4_read, hb4_write, dev); /* Device 10: UMC 8881x */

    /* Port 92 */
    device_add(&port_92_pci_device);

    /* SMRAM */
    dev->smram[0] = smram_add();
    dev->smram[1] = smram_add();
    dev->smram[2] = smram_add();

    hb4_reset(dev);

    return dev;
}

const device_t umc_hb4_device = {
    .name          = "UMC HB4(8881F)",
    .internal_name = "umc_hb4",
    .flags         = DEVICE_PCI,
    .local         = 0x886a,
    .init          = hb4_init,
    .close         = hb4_close,
    .reset         = hb4_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
