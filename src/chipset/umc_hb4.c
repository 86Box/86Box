/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the UMC HB4 "Super Energy Star Green" PCI Chipset.
 *
 * Note:    This chipset has no datasheet, everything were done via
 *          reverse engineering the BIOS of various machines using it.
 *
 * Note 2:  Additional information were also used from all
 *          around the web.
 *
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Tiseno100.
 *          Copyright 2021-2024 Miran Grca.
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
   Bit 0: E0000-EFFFF Read Enable

   Register 55:
   Bit 7: F0000-FFFF Read Enable
   Bit 6: Shadow Write Status (1: Write Protect/0: Write)

   Register 56h & 57h: DRAM Bank 0 Configuration
   Register 58h & 59h: DRAM Bank 1 Configuration

   Register 5A:
   Bit 2: Detrubo

   Register 5C:
   Bits 7-0: SMRAM base A27-A20

   Register 5D:
   Bits 3-0: SMRAM base A31-A28

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
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>
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
    uint8_t idx;
    uint8_t access_data;

    uint8_t  pci_slot;

    uint8_t  pci_conf[256]; /* PCI Registers */

    int      mem_state[9];

    uint32_t smram_base;

    smram_t *smram;         /* SMRAM Handler */
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

    /* Erratum in Vogons' datasheet: Register 55h bit 7 in fact controls E0000-FFFFF. */
    state  = (dev->pci_conf[0x55] & 0x80) ? shadow_read[dev->pci_conf[0x54] & 0x01] :
                                            MEM_READ_EXTANY;
    state |= shadow_write[(dev->pci_conf[0x55] >> 6) & 0x01];

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
    int state;
    int n = 0;

    for (uint8_t i = 0; i < 6; i++) {
        state  = (dev->pci_conf[0x55] & 0x80) ? shadow_read[(dev->pci_conf[0x54] >> (i + 2)) & 0x01] :
                                                MEM_READ_EXTANY;
        state |= shadow_write[(dev->pci_conf[0x55] >> 6) & 0x01];

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

    state  = (dev->pci_conf[0x55] & 0x80) ? shadow_read[(dev->pci_conf[0x54] >> 1) & 0x01] :
                                            MEM_READ_EXTANY;
    state |= shadow_write[(dev->pci_conf[0x55] >> 6) & 0x01];

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
    if (dev->smram_base != 0x00000000)
        umc_smram_recalc(dev->smram_base >> 12, 0);

    dev->smram_base = ((uint32_t) dev->pci_conf[0x5c]) << 20;
    dev->smram_base |= ((uint32_t) (dev->pci_conf[0x5d] & 0x0f)) << 28;
    dev->smram_base |= 0x000a0000;

    /* Bit 0, if set, enables SMRAM access outside SMM. SMRAM appears to be always enabled
       in SMM, and is always set to A0000-BFFFF. */
    smram_enable(dev->smram, dev->smram_base, 0x000a0000, 0x20000, dev->pci_conf[0x60] & 0x01, 1);

    /* Bit 5 seems to set data to go to PCI and code to DRAM. The Samsung SPC7700P-LW uses
       this. */
    if (dev->pci_conf[0x60] & 0x20) {
        if (dev->pci_conf[0x60] & 0x01)
            mem_set_mem_state_smram_ex(0, dev->smram_base, 0x20000, 0x02);
        mem_set_mem_state_smram_ex(1, dev->smram_base, 0x20000, 0x02);
    }

    umc_smram_recalc(dev->smram_base >> 12, 1);
}

static void
hb4_write(UNUSED(int func), int addr, uint8_t val, void *priv)
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

        case 0x51 ... 0x53:
            dev->pci_conf[addr] = val;
            break;

        case 0x54 ... 0x55:
            dev->pci_conf[addr] = val;
            hb4_shadow(dev);
            break;

        case 0x56 ... 0x5a:
        case 0x5e ... 0x5f:
            dev->pci_conf[addr] = val;
            break;

        case 0x5c ... 0x5d:
        case 0x60:
            dev->pci_conf[addr] = val;
            hb4_smram(dev);
            break;

        case 0x61:
            dev->pci_conf[addr] = val;
            break;

        case 0x62:
            dev->pci_conf[addr] = val & 0x03;
            break;

        default:
            break;
    }
}

static uint8_t
hb4_read(int func, int addr, void *priv)
{
    const hb4_t  *dev = (hb4_t *) priv;
    uint8_t       ret = 0xff;

    if (func == 0)
        ret = dev->pci_conf[addr];

    return ret;
}

static void
hb4_reset(void *priv)
{
    hb4_t *dev = (hb4_t *) priv;
    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));

    dev->pci_conf[0x00] = 0x60; /* UMC */
    dev->pci_conf[0x01] = 0x10;
    dev->pci_conf[0x02] = 0x81; /* 8881x */
    dev->pci_conf[0x03] = 0x88;
    dev->pci_conf[0x07] = 0x02;
    dev->pci_conf[0x08] = 0x04;
    dev->pci_conf[0x09] = 0x00;
    dev->pci_conf[0x0a] = 0x00;
    dev->pci_conf[0x0b] = 0x06;
    dev->pci_conf[0x50] = 0x00;
    dev->pci_conf[0x51] = 0x00;
    dev->pci_conf[0x52] = 0x01;
    dev->pci_conf[0x53] = 0x00;
    dev->pci_conf[0x54] = 0x00;
    dev->pci_conf[0x55] = 0x40;
    dev->pci_conf[0x56] = 0xff;
    dev->pci_conf[0x57] = 0x0f;
    dev->pci_conf[0x58] = 0xff;
    dev->pci_conf[0x59] = 0x0f;
    dev->pci_conf[0x5a] = 0x00;
    dev->pci_conf[0x5b] = 0x2c;
    dev->pci_conf[0x5c] = 0x00;
    dev->pci_conf[0x5d] = 0x0f;
    dev->pci_conf[0x5e] = 0x00;
    dev->pci_conf[0x5f] = 0xff;
    dev->pci_conf[0x60] = 0x00;
    dev->pci_conf[0x61] = 0x00;
    dev->pci_conf[0x62] = 0x00;

    hb4_shadow(dev);
    hb4_smram(dev);

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    memset(dev->mem_state, 0x00, sizeof(dev->mem_state));
}

static void
hb4_close(void *priv)
{
    hb4_t *dev = (hb4_t *) priv;

    smram_del(dev->smram);
    free(dev);
}

static void
ims8848_write(uint16_t addr, uint8_t val, void *priv)
{
    hb4_t     *dev = (hb4_t *) priv;

    switch (addr) {
        case 0x22:
            dev->idx = val;
            break;
        case 0x23:
            if (((val & 0x0f) == ((dev->idx >> 4) & 0x0f)) && ((val & 0xf0) == ((dev->idx << 4) & 0xf0)))
                dev->access_data = 1;
            break;
        case 0x24:
            if (dev->access_data)
                dev->access_data = 0;
            break;

            default:
                break;
    }
}

static uint8_t
ims8848_read(uint16_t addr, void *priv)
{
    uint8_t    ret = 0xff;
    hb4_t     *dev = (hb4_t *) priv;

    switch (addr) {
        case 0x22:
            ret = dev->idx;
            break;
        case 0x23:
            ret = (dev->idx >> 4) | (dev->idx << 4);
            break;
        case 0x24:
            if (dev->access_data) {
                ret              = dev->pci_conf[dev->idx];
                dev->access_data = 0;
            }
            break;
        default:
            break;
    }

    return ret;
}

static void *
hb4_init(UNUSED(const device_t *info))
{
    hb4_t *dev = (hb4_t *) calloc(1, sizeof(hb4_t));

    pci_add_card(PCI_ADD_NORTHBRIDGE, hb4_read, hb4_write, dev, &dev->pci_slot); /* Device 10: UMC 8881x */

    /* Port 92 */
    device_add(&port_92_pci_device);

    /* SMRAM */
    dev->smram = smram_add();

    dev->smram_base = 0x000a0000;
    hb4_reset(dev);

    io_sethandler(0x0022, 0x0003, ims8848_read, NULL, NULL, ims8848_write, NULL, NULL, dev);

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
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
