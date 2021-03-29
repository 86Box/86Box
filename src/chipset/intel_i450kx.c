/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 450KX Mars Chipset.
 *
 * Authors:	Tiseno100,
 *
 *		Copyright 2021 Tiseno100.
 */

/*
Note: i450KX PB manages PCI memory access with MC manages DRAM memory access.
Due to 86Box limitations we can't manage them seperately thus it is dev branch till then.

i450GX is way more popular of an option but needs more stuff.
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

#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/smram.h>
#include <86box/spd.h>

#include <86box/chipset.h>

#ifdef ENABLE_450KX_LOG
int i450kx_do_log = ENABLE_450KX_LOG;
static void
i450kx_log(const char *fmt, ...)
{
    va_list ap;

    if (i450kx_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define i450kx_log(fmt, ...)
#endif

/* Shadow RAM Flags */
#define LSB_DECISION (((shadow_value & 1) ? MEM_READ_EXTANY : MEM_READ_INTERNAL) | ((shadow_value & 2) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL))
#define MSB_DECISION (((shadow_value & 0x10) ? MEM_READ_EXTANY : MEM_READ_INTERNAL) | ((shadow_value & 0x20) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL))
#define LSB_DECISION_MC (((shadow_value & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((shadow_value & 2) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY))
#define MSB_DECISION_MC (((shadow_value & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((shadow_value & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY))

/* SMRAM */
#define SMRAM_ADDR (((dev->pb_pci_conf[0xb9] << 8) | dev->pb_pci_conf[0xb8]) << 17)
#define SMRAM_SIZE (1 << (((dev->pb_pci_conf[0xbb] >> 4) + 1) * 16))
#define SMRAM_ADDR_MC (((dev->mc_pci_conf[0xb9] << 8) | dev->mc_pci_conf[0xb8]) << 16)
#define SMRAM_SIZE_MC (1 << (((dev->mc_pci_conf[0xbb] >> 4) + 1) * 16))

/* Miscellaneous */
#define ENABLE_SEGMENT (MEM_READ_EXTANY | MEM_WRITE_EXTANY)
#define DISABLE_SEGMENT (MEM_READ_DISABLED | MEM_WRITE_DISABLED)

typedef struct i450kx_t
{
    smram_t *smram;

    uint8_t pb_pci_conf[256], mc_pci_conf[256];
} i450kx_t;

void i450kx_shadow(int is_mc, int cur_reg, uint8_t shadow_value, i450kx_t *dev)
{
    if (cur_reg == 0x59)
    {
        mem_set_mem_state_both(0x80000, 0x20000, (is_mc) ? LSB_DECISION_MC : LSB_DECISION);
        mem_set_mem_state_both(0xf0000, 0x10000, (is_mc) ? MSB_DECISION_MC : MSB_DECISION);
    }
    else
    {
        mem_set_mem_state_both(0xc0000 + (((cur_reg & 7) - 2) * 0x8000), 0x4000, (is_mc) ? LSB_DECISION_MC : LSB_DECISION);
        mem_set_mem_state_both(0xc4000 + (((cur_reg & 7) - 2) * 0x8000), 0x4000, (is_mc) ? MSB_DECISION_MC : MSB_DECISION);
    }
    flushmmucache_nopc();
}

void i450kx_smm(uint32_t smram_addr, uint32_t smram_size, i450kx_t *dev)
{
    smram_disable_all();

    if ((smram_addr != 0) && !!(dev->mc_pci_conf[0x57] & 8))
    {
        smram_enable(dev->smram, smram_addr, smram_addr, smram_size, !!(dev->pb_pci_conf[0x57] & 8), 1);
        mem_set_mem_state_smram_ex(1, smram_addr, smram_size, 0x03);
    }

    flushmmucache();
}

static void
pb_write(int func, int addr, uint8_t val, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;

    switch (addr)
    {
    case 0x04:
        dev->pb_pci_conf[addr] &= val & 0xd7;
        break;

    case 0x06:
        dev->pb_pci_conf[addr] = val & 0x80;
        break;

    case 0x07:
    case 0x0d:
        dev->pb_pci_conf[addr] = val;
        break;

    case 0x0f:
        dev->pb_pci_conf[addr] = val & 0xcf;
        break;

    case 0x40:
    case 0x41:
        dev->pb_pci_conf[addr] = val;
        break;

    case 0x43:
        dev->pb_pci_conf[addr] = val & 0x80;
        break;

    case 0x48:
        dev->pb_pci_conf[addr] = val & 6;
        break;

    case 0x4a:
    case 0x4b:
        dev->pb_pci_conf[addr] = val;
        break;

    case 0x4c:
        dev->pb_pci_conf[addr] = val & 0xd8;
        break;

    case 0x53:
        dev->pb_pci_conf[addr] = val & 2;
        break;

    case 0x54:
        dev->pb_pci_conf[addr] = val & 0x7b;
        break;

    case 0x55:
        dev->pb_pci_conf[addr] = val & 2;
        break;

    case 0x57:
        dev->pb_pci_conf[addr] = val & 8;
        i450kx_smm(SMRAM_ADDR, SMRAM_SIZE, dev);
        break;

    case 0x58:
        dev->pb_pci_conf[addr] = val & 2;
        mem_set_mem_state_both(0xa0000, 0x20000, (val & 2) ? ENABLE_SEGMENT : DISABLE_SEGMENT);
        break;

    case 0x59:
    case 0x5a:
    case 0x5b:
    case 0x5c:
    case 0x5d:
    case 0x5e:
    case 0x5f:
        dev->pb_pci_conf[addr] = val & 0x33;
        i450kx_shadow(0, addr, val, dev);
        break;

    case 0x70:
        dev->pb_pci_conf[addr] = val & 0xfc;
        break;

    case 0x71:
        dev->pb_pci_conf[addr] = val & 0x71;
        break;

    case 0x78:
        dev->pb_pci_conf[addr] = val & 0xf0;
        break;

    case 0x79:
        dev->pb_pci_conf[addr] = val & 0xfc;
        break;

    case 0x7c:
        dev->pb_pci_conf[addr] = val & 0x5f;
        break;

    case 0x7d:
        dev->pb_pci_conf[addr] = val & 0x1a;
        break;

    case 0x7e:
        dev->pb_pci_conf[addr] = val & 0xf0;
        break;

    case 0x7f:
    case 0x88:
    case 0x89:
    case 0x8a:
        dev->pb_pci_conf[addr] = val;
        break;

    case 0x8b:
        dev->pb_pci_conf[addr] = val & 0x80;
        break;

    case 0x9c:
        dev->pb_pci_conf[addr] = val & 1;
        break;

    case 0xa4:
        dev->pb_pci_conf[addr] = val & 0xf9;
        break;

    case 0xa5:
    case 0xa6:
        dev->pb_pci_conf[addr] = val;
        break;

    case 0xa7:
        dev->pb_pci_conf[addr] = val & 0x0f;
        break;

    case 0xb0:
        dev->pb_pci_conf[addr] = val & 0xe0;
        break;

    case 0xb1:
        dev->pb_pci_conf[addr] = val & 0x1f;
        break;

    case 0xb4:
        dev->pb_pci_conf[addr] = val & 0xe8;
        break;

    case 0xb5:
        dev->pb_pci_conf[addr] = val & 0x1f;
        break;

    case 0xb8:
    case 0xb9:
    case 0xbb:
        if (addr == 0xbb)
            dev->pb_pci_conf[addr] = val & 0xf0;
        else
            dev->pb_pci_conf[addr] = val;

        i450kx_smm(SMRAM_ADDR, SMRAM_SIZE, dev);
        break;

    case 0xc4:
        dev->pb_pci_conf[addr] = val & 5;
        break;

    case 0xc5:
        dev->pb_pci_conf[addr] = val & 0x0a;
        break;

    case 0xc6:
        dev->pb_pci_conf[addr] = val & 0x1d;
        break;

    case 0xc8:
        dev->pb_pci_conf[addr] = val & 0x1f;
        break;

    case 0xca:
    case 0xcb:
        dev->pb_pci_conf[addr] = val;
        break;
    }
    i450kx_log("i450KX-PB: dev->regs[%02x] = %02x POST: %02x\n", addr, dev->pb_pci_conf[addr], inb(0x80));
}

static uint8_t
pb_read(int func, int addr, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;
    return dev->pb_pci_conf[addr];
}

static void
mc_write(int func, int addr, uint8_t val, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;

    switch (addr)
    {
    case 0x4c:
        dev->mc_pci_conf[addr] = val & 0xdf;
        break;

    case 0x4d:
        dev->mc_pci_conf[addr] = val & 0xdf;
        break;

    case 0x57:
        dev->mc_pci_conf[addr] = val & 8;
        i450kx_smm(SMRAM_ADDR, SMRAM_SIZE, dev);
        break;

    case 0x58:
        dev->mc_pci_conf[addr] = val & 2;
        mem_set_mem_state_both(0xa0000, 0x20000, (val & 2) ? ENABLE_SEGMENT : DISABLE_SEGMENT);
        break;

    case 0x59:
    case 0x5a:
    case 0x5b:
    case 0x5c:
    case 0x5d:
    case 0x5e:
    case 0x5f:
        dev->mc_pci_conf[addr] = val & 0x33;
        i450kx_shadow(1, addr, val, dev);
        break;

    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
        dev->mc_pci_conf[addr] = ((addr & 0x0f) % 2) ? val : (val & 7);
        spd_write_drbs(dev->mc_pci_conf, 0x60, 0x6f, 1);
        break;

    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
        dev->mc_pci_conf[addr] = val;
        break;

    case 0x78:
        dev->mc_pci_conf[addr] = val & 0xf0;
        break;

    case 0x79:
        dev->mc_pci_conf[addr] = val & 0xfe;
        break;

    case 0x7a:
        dev->mc_pci_conf[addr] = val;
        break;

    case 0x7b:
        dev->mc_pci_conf[addr] = val & 0x0f;
        break;

    case 0x7c:
        dev->mc_pci_conf[addr] = val & 0x1f;
        break;

    case 0x7d:
        dev->mc_pci_conf[addr] = val & 0x0c;
        break;

    case 0x7e:
        dev->mc_pci_conf[addr] = val & 0xf0;
        break;

    case 0x7f:
        dev->mc_pci_conf[addr] = val;
        break;

    case 0x88:
    case 0x89:
        dev->mc_pci_conf[addr] = val;
        break;

    case 0x8b:
        dev->mc_pci_conf[addr] = val & 0x80;
        break;

    case 0x8c:
    case 0x8d:
        dev->mc_pci_conf[addr] = val;
        break;

    case 0xa4:
        dev->mc_pci_conf[addr] = val & 1;
        break;

    case 0xa5:
        dev->pb_pci_conf[addr] = val & 0xf0;
        break;

    case 0xa6:
        dev->mc_pci_conf[addr] = val;
        break;

    case 0xa7:
        dev->mc_pci_conf[addr] = val & 0x0f;
        break;

    case 0xa8:
        dev->mc_pci_conf[addr] = val & 0xfe;
        break;

    case 0xa9:
    case 0xaa:
    case 0xab:
    case 0xac:
    case 0xad:
    case 0xae:
        dev->mc_pci_conf[addr] = val;
        break;

    case 0xaf:
        dev->mc_pci_conf[addr] = val & 0x7f;
        break;

    case 0xb8:
    case 0xb9:
    case 0xbb:
        if (addr == 0xbb)
            dev->mc_pci_conf[addr] = val & 0xf0;
        else
            dev->mc_pci_conf[addr] = val;

        i450kx_smm(SMRAM_ADDR_MC, SMRAM_SIZE_MC, dev);
        break;

    case 0xbc:
        dev->mc_pci_conf[addr] = val & 1;
        break;

    case 0xc0:
        dev->mc_pci_conf[addr] = val & 7;
        break;

    case 0xc2:
        dev->mc_pci_conf[addr] = val & 3;
        break;

    case 0xc4:
        dev->mc_pci_conf[addr] = val & 0x3f;
        break;

    case 0xc6:
        dev->mc_pci_conf[addr] = val & 0x19;
        break;
    }
    i450kx_log("i450KX-MC: dev->regs[%02x] = %02x POST: %02x\n", addr, dev->mc_pci_conf[addr], inb(0x80));
}

static uint8_t
mc_read(int func, int addr, void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;
    return dev->mc_pci_conf[addr];
}

static void
i450kx_reset(void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;

    /* CONFLICTS WARNING! We do not program anything on reset due to that */

    /* Defaults PB */
    dev->pb_pci_conf[0x00] = 0x86;
    dev->pb_pci_conf[0x01] = 0x80;
    dev->pb_pci_conf[0x02] = 0xc4;
    dev->pb_pci_conf[0x03] = 0x84;
    dev->pb_pci_conf[0x05] = 4;
    dev->pb_pci_conf[0x06] = 0x40;
    dev->pb_pci_conf[0x07] = 2;
    dev->pb_pci_conf[0x08] = 1;
    dev->pb_pci_conf[0x0b] = 6;
    dev->pb_pci_conf[0x0c] = 8;
    dev->pb_pci_conf[0x0d] = 0x20;
    dev->pb_pci_conf[0x49] = 0x14;
    dev->pb_pci_conf[0x4c] = 0x39;
    dev->pb_pci_conf[0x58] = 2;
    dev->pb_pci_conf[0x59] = 0x30;
    dev->pb_pci_conf[0x5a] = 0x33;
    dev->pb_pci_conf[0x5b] = 0x33;
    dev->pb_pci_conf[0x5c] = 0x33;
    dev->pb_pci_conf[0x5d] = 0x33;
    dev->pb_pci_conf[0x5e] = 0x33;
    dev->pb_pci_conf[0x5f] = 0x33;
    dev->pb_pci_conf[0xa4] = 1;
    dev->pb_pci_conf[0xa5] = 0xc0;
    dev->pb_pci_conf[0xa6] = 0xfe;
    dev->pb_pci_conf[0xc8] = 3;
    dev->pb_pci_conf[0xb8] = 5;

    /* Defaults MC */
    dev->mc_pci_conf[0x00] = 0x86;
    dev->mc_pci_conf[0x01] = 0x80;
    dev->mc_pci_conf[0x02] = 0xc5;
    dev->mc_pci_conf[0x03] = 0x84;
    dev->mc_pci_conf[0x06] = 0x80;
    dev->mc_pci_conf[0x08] = 1;
    dev->mc_pci_conf[0x0b] = 5;
    dev->mc_pci_conf[0x49] = 0x14;
    dev->mc_pci_conf[0x4c] = 0x0b;
    dev->mc_pci_conf[0x78] = 0x10;
    dev->mc_pci_conf[0xa4] = 1;
    dev->mc_pci_conf[0xa5] = 0xc0;
    dev->mc_pci_conf[0xa6] = 0xfe;
    dev->mc_pci_conf[0xac] = 0x16;
    dev->mc_pci_conf[0xad] = 0x35;
    dev->mc_pci_conf[0xae] = 0xdf;
    dev->mc_pci_conf[0xaf] = 0x30;
    dev->mc_pci_conf[0xb8] = 0x0a;
    dev->mc_pci_conf[0xbc] = 1;
}

static void
i450kx_close(void *priv)
{
    i450kx_t *dev = (i450kx_t *)priv;

    smram_del(dev->smram);
    free(dev);
}

static void *
i450kx_init(const device_t *info)
{
    i450kx_t *dev = (i450kx_t *)malloc(sizeof(i450kx_t));
    memset(dev, 0, sizeof(i450kx_t));
    pci_add_card(PCI_ADD_NORTHBRIDGE, pb_read, pb_write, dev); /* Device 19: Intel 450KX PCI Bridge PB */
    pci_add_card(PCI_ADD_SOUTHBRIDGE, mc_read, mc_write, dev); /* Device 14: Intel 450KX Memory Controller MC */

    dev->smram = smram_add();

    cpu_cache_int_enabled = 1;
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    i450kx_reset(dev);

    return dev;
}

const device_t i450kx_device = {
    "Intel 450KX (Mars)",
    DEVICE_PCI,
    0,
    i450kx_init,
    i450kx_close,
    i450kx_reset,
    {NULL},
    NULL,
    NULL,
    NULL};
