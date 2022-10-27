/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Chips&Technology's 82C100 chipset.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Miran Grca.
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
#include "cpu.h"
#include "x86.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/port_92.h>
#include <86box/rom.h>
#include <86box/chipset.h>

typedef struct
{
    int      enabled;
    uint32_t virt, phys;
} ems_page_t;

typedef struct
{
    uint8_t  index, access;
    uint16_t ems_io_base;
    uint32_t ems_window_base;
    uint8_t  ems_page_regs[4],
        regs[256];
    ems_page_t    ems_pages[4];
    mem_mapping_t ems_mappings[4];
} ct_82c100_t;

#ifdef ENABLE_CT_82C100_LOG
int ct_82c100_do_log = ENABLE_CT_82C100_LOG;

static void
ct_82c100_log(const char *fmt, ...)
{
    va_list ap;

    if (ct_82c100_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ct_82c100_log(fmt, ...)
#endif

static void
ct_82c100_ems_pages_recalc(ct_82c100_t *dev)
{
    int      i;
    uint32_t page_base;

    for (i = 0; i < 4; i++) {
        page_base = dev->ems_window_base + (i << 14);
        if ((i == 1) || (i == 2))
            page_base ^= 0xc000;
        if (dev->ems_page_regs[i] & 0x80) {
            dev->ems_pages[i].virt = page_base;
            dev->ems_pages[i].phys = 0xa0000 + (((uint32_t) (dev->ems_page_regs[i] & 0x7f)) << 14);
            ct_82c100_log("Enabling EMS page %i: %08X-%08X -> %08X-%08X\n", i,
                          dev->ems_pages[i].virt, dev->ems_pages[i].virt + 0x00003fff,
                          dev->ems_pages[i].phys, dev->ems_pages[i].phys + 0x00003fff);
            mem_mapping_set_addr(&(dev->ems_mappings[i]), dev->ems_pages[i].virt, 0x4000);
            mem_mapping_set_exec(&(dev->ems_mappings[i]), &(ram[dev->ems_pages[i].phys]));
            mem_set_mem_state_both(page_base, 0x00004000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        } else {
            ct_82c100_log("Disabling EMS page %i\n", i);
            mem_mapping_disable(&(dev->ems_mappings[i]));
            mem_set_mem_state_both(page_base, 0x00004000, MEM_READ_EXTANY | MEM_WRITE_EXTANY);
        }
    }

    flushmmucache_nopc();
}

static void
ct_82c100_ems_out(uint16_t port, uint8_t val, void *priv)
{
    ct_82c100_t *dev = (ct_82c100_t *) priv;

    ct_82c100_log("[%04X] dev->ems_page_regs[%i] = %02X\n", port, port >> 14, val);
    dev->ems_page_regs[port >> 14] = val;
    ct_82c100_ems_pages_recalc(dev);
}

static uint8_t
ct_82c100_ems_in(uint16_t port, void *priv)
{
    ct_82c100_t *dev = (ct_82c100_t *) priv;
    uint8_t      ret = 0xff;

    ret = dev->ems_page_regs[port >> 14];

    return ret;
}

static void
ct_82c100_ems_update(ct_82c100_t *dev)
{
    int i;

    for (i = 0; i < 4; i++) {
        ct_82c100_log("Disabling EMS I/O handler %i at %04X\n", i, dev->ems_io_base + (i << 14));
        io_handler(0, dev->ems_io_base + (i << 14), 1,
                   ct_82c100_ems_in, NULL, NULL, ct_82c100_ems_out, NULL, NULL, dev);
    }

    dev->ems_io_base = 0x0208 + (dev->regs[0x4c] & 0xf0);

    for (i = 0; i < 4; i++) {
        ct_82c100_log("Enabling EMS I/O handler %i at %04X\n", i, dev->ems_io_base + (i << 14));
        io_handler(1, dev->ems_io_base + (i << 14), 1,
                   ct_82c100_ems_in, NULL, NULL, ct_82c100_ems_out, NULL, NULL, dev);
    }

    dev->ems_window_base = 0xc0000 + (((uint32_t) (dev->regs[0x4c] & 0x0f)) << 14);

    ct_82c100_ems_pages_recalc(dev);
}

static void
ct_82c100_reset(void *priv)
{
    ct_82c100_t *dev = (ct_82c100_t *) priv;

    ct_82c100_log("Reset\n");

    memset(dev->regs, 0x00, sizeof(dev->regs));
    memset(dev->ems_page_regs, 0x00, sizeof(dev->ems_page_regs));

    dev->index = dev->access = 0x00;

    /* INTERNAL CONFIGURATION/CONTROL REGISTERS */
    dev->regs[0x40] = 0x01; /* Defaults to 8086/V30 mode. */
    dev->regs[0x43] = 0x30;
    dev->regs[0x48] = 0x01;

    use_custom_nmi_vector = 0;
    ct_82c100_ems_update(dev);

    /* ADDITIONAL I/O REGISTERS */
}

static void
ct_82c100_out(uint16_t port, uint8_t val, void *priv)
{
    ct_82c100_t *dev = (ct_82c100_t *) priv;

    if (port == 0x0022) {
        dev->index  = val;
        dev->access = 1;
    } else if (port == 0x0023) {
        if (dev->access) {
            switch (dev->index) {
                /* INTERNAL CONFIGURATION/CONTROL REGISTERS */
                case 0x40:
                    dev->regs[0x40] = val & 0xc7;
                    /* TODO: Clock stuff - needs CPU speed change functionality that's
                             going to be implemented in 86box v4.0.
                             Bit 0 is 0 for 8088/V20 and 1 for 8086/V30. */
                    break;
                case 0x41:
                    dev->regs[0x41] = val & 0xed;
                    /* TODO: Where is the Software Reset Function that's enabled by
                             setting bit 6 to 1? */
                    break;
                case 0x42:
                    dev->regs[0x42] = val & 0x01;
                    break;
                case 0x43:
                    dev->regs[0x43] = val;
                    break;
                case 0x44:
                    dev->regs[0x44]   = val;
                    custom_nmi_vector = (custom_nmi_vector & 0xffffff00) | ((uint32_t) val);
                    break;
                case 0x45:
                    dev->regs[0x45]   = val;
                    custom_nmi_vector = (custom_nmi_vector & 0xffff00ff) | (((uint32_t) val) << 8);
                    break;
                case 0x46:
                    dev->regs[0x46]   = val;
                    custom_nmi_vector = (custom_nmi_vector & 0xff00ffff) | (((uint32_t) val) << 16);
                    break;
                case 0x47:
                    dev->regs[0x47]   = val;
                    custom_nmi_vector = (custom_nmi_vector & 0x00ffffff) | (((uint32_t) val) << 24);
                    break;
                case 0x48:
                case 0x49:
                    dev->regs[dev->index] = val;
                    break;
                case 0x4b:
                    dev->regs[0x4b]       = val;
                    use_custom_nmi_vector = !!(val & 0x40);
                    break;
                case 0x4c:
                    ct_82c100_log("CS4C: %02X\n", val);
                    dev->regs[0x4c] = val;
                    ct_82c100_ems_update(dev);
                    break;
            }
            dev->access = 0;
        }
    } else if (port == 0x72)
        dev->regs[0x72] = val & 0x7e;
    else if (port == 0x7e)
        dev->regs[0x7e] = val;
    else if (port == 0x7f) {
        /* Bit 3 is Software Controlled Reset, asserted if set. Will be
           done in the feature/machine_and_kb branch using hardresetx86(). */
        dev->regs[0x7f] = val;
        if ((dev->regs[0x41] & 0x40) && (val & 0x08)) {
            softresetx86();
            cpu_set_edx();
            ct_82c100_reset(dev);
        }
    }
}

static uint8_t
ct_82c100_in(uint16_t port, void *priv)
{
    ct_82c100_t *dev = (ct_82c100_t *) priv;
    uint8_t      ret = 0xff;

    if (port == 0x0022)
        ret = dev->index;
    else if (port == 0x0023) {
        if (dev->access) {
            switch (dev->index) {
                /* INTERNAL CONFIGURATION/CONTROL REGISTERS */
                case 0x40 ... 0x49:
                case 0x4b:
                case 0x4c:
                    ret = dev->regs[dev->index];
                    break;
            }
            dev->access = 0;
        }
    } else if (port == 0x72)
        ret = dev->regs[0x72];
    else if (port == 0x7e)
        ret = dev->regs[0x7e];
    else if (port == 0x7f)
        ret = dev->regs[0x7f];

    return ret;
}

static uint8_t
mem_read_emsb(uint32_t addr, void *priv)
{
    ems_page_t *page = (ems_page_t *) priv;
    uint8_t     ret  = 0xff;
#ifdef ENABLE_CT_82C100_LOG
    uint32_t old_addr = addr;
#endif

    addr = addr - page->virt + page->phys;

    if (addr < ((uint32_t) mem_size << 10))
        ret = ram[addr];

    ct_82c100_log("mem_read_emsb(%08X = %08X): %02X\n", old_addr, addr, ret);

    return ret;
}

static uint16_t
mem_read_emsw(uint32_t addr, void *priv)
{
    ems_page_t *page = (ems_page_t *) priv;
    uint16_t    ret  = 0xffff;
#ifdef ENABLE_CT_82C100_LOG
    uint32_t old_addr = addr;
#endif

    addr = addr - page->virt + page->phys;

    if (addr < ((uint32_t) mem_size << 10))
        ret = *(uint16_t *) &ram[addr];

    ct_82c100_log("mem_read_emsw(%08X = %08X): %04X\n", old_addr, addr, ret);

    return ret;
}

static void
mem_write_emsb(uint32_t addr, uint8_t val, void *priv)
{
    ems_page_t *page = (ems_page_t *) priv;
#ifdef ENABLE_CT_82C100_LOG
    uint32_t old_addr = addr;
#endif

    addr = addr - page->virt + page->phys;

    if (addr < ((uint32_t) mem_size << 10))
        ram[addr] = val;

    ct_82c100_log("mem_write_emsb(%08X = %08X, %02X)\n", old_addr, addr, val);
}

static void
mem_write_emsw(uint32_t addr, uint16_t val, void *priv)
{
    ems_page_t *page = (ems_page_t *) priv;
#ifdef ENABLE_CT_82C100_LOG
    uint32_t old_addr = addr;
#endif

    addr = addr - page->virt + page->phys;

    if (addr < ((uint32_t) mem_size << 10))
        *(uint16_t *) &ram[addr] = val;

    ct_82c100_log("mem_write_emsw(%08X = %08X, %04X)\n", old_addr, addr, val);
}

static void
ct_82c100_close(void *priv)
{
    ct_82c100_t *dev = (ct_82c100_t *) priv;

    free(dev);
}

static void *
ct_82c100_init(const device_t *info)
{
    ct_82c100_t *dev;
    uint32_t     i;

    dev = (ct_82c100_t *) malloc(sizeof(ct_82c100_t));
    memset(dev, 0x00, sizeof(ct_82c100_t));

    ct_82c100_reset(dev);

    io_sethandler(0x0022, 2,
                  ct_82c100_in, NULL, NULL, ct_82c100_out, NULL, NULL, dev);
    io_sethandler(0x0072, 1,
                  ct_82c100_in, NULL, NULL, ct_82c100_out, NULL, NULL, dev);
    io_sethandler(0x007e, 2,
                  ct_82c100_in, NULL, NULL, ct_82c100_out, NULL, NULL, dev);

    for (i = 0; i < 4; i++) {
        mem_mapping_add(&(dev->ems_mappings[i]), (i + 28) << 14, 0x04000,
                        mem_read_emsb, mem_read_emsw, NULL,
                        mem_write_emsb, mem_write_emsw, NULL,
                        ram + 0xa0000 + (i << 14), MEM_MAPPING_INTERNAL, &dev->ems_pages[i]);
        mem_mapping_disable(&(dev->ems_mappings[i]));
    }

    mem_mapping_disable(&ram_mid_mapping);

    device_add(&port_92_device);

    return (dev);
}

const device_t ct_82c100_device = {
    .name          = "C&T 82C100",
    .internal_name = "ct_82c100",
    .flags         = 0,
    .local         = 0,
    .init          = ct_82c100_init,
    .close         = ct_82c100_close,
    .reset         = ct_82c100_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
