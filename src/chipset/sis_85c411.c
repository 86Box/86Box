/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The SiS 85C405/406/411/420/431 EISA chipset for the 386 and
 *          486. Five parts, two of which take software:
 *
 *            85C411  system, cache and DRAM controller; nine registers
 *                    behind an index at 0C18h and data at 0C1Ch
 *            85C406  the system peripheral: arbiter, DMA, the two
 *                    interrupt controllers, NMI, timers, and the EISA
 *                    configuration RAM; its own registers sit behind an
 *                    index at 0CA0h and data at 0CA1h
 *
 *          The 85C405 is the address buffer, the 85C431 the data buffer
 *          and the 85C420 the EISA bus controller; none of them has a
 *          register, and the 85C420's one option, the 8-bit I/O recovery
 *          time, is two strap pins.
 *
 *          No data book for this family survives. The 85C411 map is the
 *          one c't magazine's CTCHIP34 shipped as SIS411.CFG, and an AMI
 *          BIOS of 6/6/92 from a Tyan S1437 uses exactly those nine
 *          registers, so it is taken as complete. The 85C406 map is what
 *          the same BIOS programs: indices 01h to 0Dh, of which only 0Ch
 *          bit 0 is understood -- it chooses between the two ways into
 *          the configuration RAM.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <math.h>
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
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/nmi.h>
#include <86box/nvr.h>
#include <86box/eisa.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
#include <86box/chipset.h>

#ifdef ENABLE_SIS_85C411_LOG
int sis_85c411_do_log = ENABLE_SIS_85C411_LOG;

static void
sis_85c411_log(const char *fmt, ...)
{
    va_list ap;

    if (sis_85c411_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sis_85c411_log(fmt, ...)
#endif

/* 85C411 registers, by index. */
#define R411_DRAM   0x60 /* [7:6] speed, [5] CAS width, [4:0] bank code */
#define R411_CACHE  0x61 /* [7] enable, [6] write back, [5:4] size, ... */
#define R411_SHADOW 0x62 /* [7] read, [6] write protect, [5:0] E8..C0 */
#define R411_MISC   0x63 /* [7] ROM size, [6] 16Mx36, [2:0] BUSCLK */
#define R411_NCA1   0x64
#define R411_NCA1LO 0x65
#define R411_NCA2   0x66
#define R411_NCA2LO 0x67
#define R411_CTRL   0x68 /* [2] F0000 cacheable, [1] shadow cacheable, [0] turbo */
#define R411_FIRST  0x60
#define R411_LAST   0x68

/* 85C406 registers, by index. */
#define R406_FIRST 0x01
#define R406_LAST  0x0d
#define R406_CRAM  0x0c /* bit 0: the configuration RAM answers at 0C00h/0800h */

#define CRAM_SIZE  8192

typedef struct sis_85c411_t {
    uint8_t idx411;
    uint8_t idx406;
    uint8_t regs411[R411_LAST - R411_FIRST + 1];
    uint8_t regs406[R406_LAST + 1];

    /* NMI status and control at 0461h, the EISA extension of port B. */
    uint8_t nmi_ext;

    /* The configuration RAM and its two doors. */
    uint8_t  cram[CRAM_SIZE];
    uint8_t  cram_page; /* 0C00h: which 256-byte page 0800h shows */
    uint16_t cram_addr; /* 0CA2h/0CA3h: a byte address for 0CA4h */
    char     cram_file[64];

    uint8_t  board_id[4];
    uint8_t  force_flush;
    uint32_t mem_state[8];
} sis_85c411_t;

/* ------------------------------------------------------------------ */
/* The 85C411                                                          */
/* ------------------------------------------------------------------ */

/* Register 60h bits 4:0 name one of thirty-two bank arrangements; these
   are their totals in megabytes. The last four are 16 MB banks, or 64 MB
   banks when register 63h bit 6 says the parts are 16M x 36. */
static const uint16_t sis_85c411_bank_total[32] = {
    1, 2, 4, 6, 8, 10, 18, 2, 4, 6, 8, 12, 18, 20, 8, 36,
    4, 8, 12, 16, 20, 24, 36, 40, 8, 16, 24, 32, 16, 32, 48, 64
};

/* The bank code whose total is the memory fitted, or failing that the
   largest that fits. The BIOS sizes memory for itself and rewrites this,
   so it only has to be plausible. */
static void
sis_85c411_dram_default(sis_85c411_t *dev)
{
    uint32_t mb    = mem_size >> 10;
    uint8_t  best  = 0;
    uint32_t total = 0;
    uint8_t  big   = 0;

    for (uint8_t i = 0; i < 32; i++) {
        if ((sis_85c411_bank_total[i] <= mb) && (sis_85c411_bank_total[i] >= total)) {
            best  = i;
            total = sis_85c411_bank_total[i];
        }
    }
    /* 128, 192 and 256 MB only exist with the large parts. */
    if (mb >= 128) {
        for (uint8_t i = 28; i < 32; i++) {
            if ((sis_85c411_bank_total[i] * 4) <= mb) {
                best = i;
                big  = 1;
            }
        }
    }

    dev->regs411[R411_DRAM - R411_FIRST] = best;
    if (big)
        dev->regs411[R411_MISC - R411_FIRST] |= 0x40;
    else
        dev->regs411[R411_MISC - R411_FIRST] &= ~0x40;
}

static void
sis_85c411_recalcmapping(sis_85c411_t *dev)
{
    uint8_t  shadow = dev->regs411[R411_SHADOW - R411_FIRST];
    uint32_t flags;
    uint32_t n = 0;

    shadowbios       = 0;
    shadowbios_write = 0;

    /* Six 32 KB enables for C0000h to EFFFFh, then F0000h, which is
       always shadowed once read is enabled: the BIOS copies itself there
       before it turns bit 7 on. Register 68h says whether any of it may
       be cached, which the emulator does not distinguish. */
    for (uint8_t i = 0; i < 8; i++) {
        uint32_t base    = 0xc0000 + (i << 15);
        uint8_t  enabled = (i < 6) ? !!(shadow & (1 << i)) : 1;

        if (enabled) {
            flags = (shadow & 0x80) ? MEM_READ_INTERNAL : MEM_READ_EXTANY;
            flags |= (shadow & 0x40) ? MEM_WRITE_EXTANY : MEM_WRITE_INTERNAL;
            if (base >= 0xe0000) {
                shadowbios |= !!(shadow & 0x80);
                shadowbios_write |= !(shadow & 0x40);
            }
        } else
            flags = MEM_READ_EXTANY | MEM_WRITE_EXTANY;

        if (dev->force_flush || (dev->mem_state[i] != flags)) {
            n++;
            mem_set_mem_state_both(base, 0x8000, flags);
            if ((base >= 0xf0000) && (dev->mem_state[i] & MEM_READ_INTERNAL) && !(flags & MEM_READ_INTERNAL))
                mem_invalidate_range(base, base + 0x7fff);
            dev->mem_state[i] = flags;
        }
    }

    if (dev->force_flush) {
        flushmmucache();
        dev->force_flush = 0;
    } else if (n > 0)
        flushmmucache_nopc();
}

static void
sis_85c411_busclk(sis_85c411_t *dev)
{
    double bus_clk;

    switch (dev->regs411[R411_MISC - R411_FIRST] & 0x07) {
        default:
        case 0x00:
            bus_clk = 7159091.0;
            break;
        case 0x01:
            bus_clk = cpu_busspeed / 8.0;
            break;
        case 0x02:
            bus_clk = cpu_busspeed / 6.0;
            break;
        case 0x03:
            bus_clk = cpu_busspeed / 5.0;
            break;
        case 0x04:
            bus_clk = cpu_busspeed / 4.0;
            break;
        case 0x05:
            bus_clk = cpu_busspeed / 3.0;
            break;
        case 0x06:
            bus_clk = cpu_busspeed / 2.5;
            break;
        case 0x07:
            bus_clk = cpu_busspeed / 2.0;
            break;
    }
    cpu_set_isa_speed((int) round(bus_clk));
}

static void
sis_85c411_write411(sis_85c411_t *dev, uint8_t idx, uint8_t val)
{
    uint8_t old;

    if ((idx < R411_FIRST) || (idx > R411_LAST))
        return;

    /* Bit 6 of 63h says the banks are 16M x 36 parts, 64 MB each, and the
       BIOS sets it from its defaults and then expects the SIMMs themselves
       to say otherwise: a smaller part aliases across its top and the
       sizing code notices. A flat emulated memory never aliases, so the
       bit would stick and the BIOS would report 64 MB that does not
       exist -- HIMEM then finds "unreliable XMS memory" at the end of
       real RAM. So the parts answer here: the bit only holds when the
       memory fitted needs 64 MB banks. */
    if ((idx == R411_MISC) && ((mem_size >> 10) < 128))
        val &= ~0x40;

    old                            = dev->regs411[idx - R411_FIRST];
    dev->regs411[idx - R411_FIRST] = val;
    sis_85c411_log("SiS411: [%04X:%08X] reg %02X = %02X\n", CS, cpu_state.pc, idx, val);

    switch (idx) {
        case R411_CACHE:
            cpu_cache_ext_enabled = !!(val & 0x80);
            cpu_update_waitstates();
            break;

        case R411_SHADOW:
            if (old != val)
                sis_85c411_recalcmapping(dev);
            break;

        case R411_MISC:
            if ((old ^ val) & 0x07)
                sis_85c411_busclk(dev);
            break;

        default:
            break;
    }
}

static uint8_t
sis_85c411_read411(const sis_85c411_t *dev, uint8_t idx)
{
    if ((idx < R411_FIRST) || (idx > R411_LAST))
        return 0xff;
    return dev->regs411[idx - R411_FIRST];
}

/* ------------------------------------------------------------------ */
/* The 85C406                                                          */
/* ------------------------------------------------------------------ */

static const uint8_t sis_85c411_cram_tag[8] = { 'E', 'I', 'S', 'A', 'C', 'F', 'G', 0x02 };

static void
sis_85c411_cram_load(sis_85c411_t *dev)
{
    FILE   *fp;
    uint8_t tag[sizeof(sis_85c411_cram_tag)];

    fp = nvr_fopen(dev->cram_file, "rb");
    if (fp == NULL)
        return;

    if ((fread(dev->cram, 1, sizeof(dev->cram), fp) != sizeof(dev->cram)) || (fread(tag, 1, sizeof(tag), fp) != sizeof(tag)) || memcmp(tag, sis_85c411_cram_tag, sizeof(tag)))
        memset(dev->cram, 0, sizeof(dev->cram));

    fclose(fp);
}

static void
sis_85c411_cram_save(sis_85c411_t *dev)
{
    FILE *fp;

    fp = nvr_fopen(dev->cram_file, "wb");
    if (fp == NULL)
        return;

    fwrite(dev->cram, 1, sizeof(dev->cram), fp);
    fwrite(sis_85c411_cram_tag, 1, sizeof(sis_85c411_cram_tag), fp);
    fclose(fp);
}

/* Whether the configuration RAM is reached the Compaq way, a page at
   0C00h and a window at 0800h, or the chip's own way, an address at
   0CA2h/0CA3h and the byte at 0CA4h. The BIOS asks before every access. */
static uint8_t
sis_85c411_cram_paged(const sis_85c411_t *dev)
{
    return dev->regs406[R406_CRAM] & 0x01;
}

/* The second timer's counter 0 is the fail-safe timer, and running out is
   an NMI when 0461h bit 2 allows; the BIOS's own test says which bit is
   which: it arms bit 2, loads a count of 48 and expects status bit 7. */
static sis_85c411_t *sis_85c411_inst = NULL;

static void
sis_85c411_fail_safe(int new_out, int old_out, UNUSED(void *priv))
{
    sis_85c411_t *dev = sis_85c411_inst;

    if ((dev == NULL) || !new_out || old_out || !(dev->nmi_ext & 0x04))
        return;

    sis_85c411_log("SiS406: the fail-safe timer ran out\n");
    dev->nmi_ext |= 0x80;
    nmi            = 1;
    nmi_auto_clear = 1;
}

static void
sis_85c411_write(uint16_t port, uint8_t val, void *priv)
{
    sis_85c411_t *dev = (sis_85c411_t *) priv;

    switch (port) {
        case 0x0c18:
            dev->idx411 = val;
            break;
        case 0x0c1c:
            sis_85c411_write411(dev, dev->idx411, val);
            break;

        case 0x0ca0:
            dev->idx406 = val;
            break;
        case 0x0ca1:
            if ((dev->idx406 >= R406_FIRST) && (dev->idx406 <= R406_LAST)) {
                sis_85c411_log("SiS406: [%04X:%08X] reg %02X = %02X\n", CS, cpu_state.pc, dev->idx406, val);
                dev->regs406[dev->idx406] = val;
            }
            break;

        case 0x0ca2:
            dev->cram_addr = (dev->cram_addr & 0xff00) | val;
            break;
        case 0x0ca3:
            dev->cram_addr = (dev->cram_addr & 0x00ff) | (val << 8);
            break;
        case 0x0ca4:
            if (!sis_85c411_cram_paged(dev))
                dev->cram[dev->cram_addr & (CRAM_SIZE - 1)] = val;
            break;

        case 0x0c00:
            dev->cram_page = val & 0x1f;
            break;
        case 0x0800 ... 0x08ff:
            if (sis_85c411_cram_paged(dev))
                dev->cram[(dev->cram_page << 8) | (port & 0xff)] = val;
            break;

        case 0x0461:
            /* Bits 3:0 are the enables and the software NMI; the status
               bits above them are read only. Writing bit 2 or 3 clear
               also clears the pending status it gates, which is how the
               BIOS acknowledges one. */
            dev->nmi_ext = (dev->nmi_ext & 0xf0) | (val & 0x0f);
            if (!(val & 0x02))
                dev->nmi_ext &= ~0x20; /* software NMI */
            if (!(val & 0x04))
                dev->nmi_ext &= ~0x80; /* fail-safe timer */
            if (!(val & 0x08))
                dev->nmi_ext &= ~0x40; /* bus timeout */
            if (!(dev->nmi_ext & 0xe0))
                nmi = 0;
            break;
        case 0x0462:
            /* Software NMI: any write raises it when 0461h bit 1 allows. */
            if (dev->nmi_ext & 0x02) {
                dev->nmi_ext |= 0x20;
                nmi            = 1;
                nmi_auto_clear = 1;
            }
            break;

        default:
            break;
    }
}

static uint8_t
sis_85c411_read(uint16_t port, void *priv)
{
    sis_85c411_t *dev = (sis_85c411_t *) priv;
    uint8_t       ret = 0xff;

    switch (port) {
        case 0x0c18:
            ret = dev->idx411;
            break;
        case 0x0c1c:
            ret = sis_85c411_read411(dev, dev->idx411);
            break;

        case 0x0ca0:
            ret = dev->idx406;
            break;
        case 0x0ca1:
            if ((dev->idx406 >= R406_FIRST) && (dev->idx406 <= R406_LAST))
                ret = dev->regs406[dev->idx406];
            break;

        case 0x0ca2:
            ret = dev->cram_addr & 0xff;
            break;
        case 0x0ca3:
            ret = dev->cram_addr >> 8;
            break;
        case 0x0ca4:
            if (!sis_85c411_cram_paged(dev))
                ret = dev->cram[dev->cram_addr & (CRAM_SIZE - 1)];
            break;

        case 0x0c00:
            ret = dev->cram_page;
            break;
        case 0x0800 ... 0x08ff:
            if (sis_85c411_cram_paged(dev))
                ret = dev->cram[(dev->cram_page << 8) | (port & 0xff)];
            break;

        case 0x0461:
            ret = dev->nmi_ext;
            break;

        case 0x0c80 ... 0x0c83:
            ret = dev->board_id[port & 3];
            break;

        default:
            break;
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/* The device                                                          */
/* ------------------------------------------------------------------ */

void
sis_85c411_set_board_id(const char *mfg, uint16_t product, uint8_t rev)
{
    sis_85c411_t *dev = device_get_priv(&sis_85c411_device);

    if (dev == NULL)
        return;

    eisa_make_id(dev->board_id, mfg, product, rev);
    eisa_set_board_id(dev->board_id);
}

static void
sis_85c411_reset(void *priv)
{
    sis_85c411_t *dev = (sis_85c411_t *) priv;

    memset(dev->regs411, 0x00, sizeof(dev->regs411));
    memset(dev->regs406, 0x00, sizeof(dev->regs406));

    /* What the Tyan BIOS writes before it reads setup: everything off,
       the arbiter and NMI enables at their resting values. */
    dev->regs406[0x01] = 0x53;
    dev->regs406[0x02] = 0x2e;
    dev->regs406[0x04] = 0x01;
    dev->regs406[0x09] = 0x03;
    dev->regs406[0x0a] = 0x01;
    dev->regs406[0x0d] = 0x03;

    dev->regs411[R411_CACHE - R411_FIRST] = 0x01;
    sis_85c411_dram_default(dev);

    dev->idx411    = 0x00;
    dev->idx406    = 0x00;
    dev->nmi_ext   = 0x00;
    dev->cram_page = 0x00;
    dev->cram_addr = 0x0000;

    cpu_cache_ext_enabled = 0;
    cpu_update_waitstates();

    /* The fail-safe counter's gate, which a soft reset leaves wherever the
       timer had it. */
    if (pit_devs[1].data != NULL) {
        pit_devs[1].set_gate(pit_devs[1].data, 0, 1);
        pit_devs[1].set_gate(pit_devs[1].data, 1, 1);
    }

    dev->force_flush = 1;
    sis_85c411_recalcmapping(dev);
    sis_85c411_busclk(dev);
}

static void
sis_85c411_close(void *priv)
{
    sis_85c411_t *dev = (sis_85c411_t *) priv;

    sis_85c411_cram_save(dev);
    sis_85c411_inst = NULL;
    free(dev);
}

static void *
sis_85c411_init(UNUSED(const device_t *info))
{
    sis_85c411_t *dev = (sis_85c411_t *) calloc(1, sizeof(sis_85c411_t));

    sis_85c411_inst = dev;

    /* The 85C406's EISA extensions to the DMA controllers and the two
       interrupt controllers. 86Box already models the parts; what the
       chip adds is that they are switched on. */
    dma_set_params(1, 0xffffffff);
    dma_ext_mode_init();
    dma_high_page_init();
    dma_eisa_init();

    pic_elcr_set_enabled(1);
    pic_elcr_io_handler(1);

    /* The second timer at 0048h: counter 0 is the fail-safe timer, whose
       output is an NMI; counter 2 is CPU speed control. Adding the device
       does not fill in the interface table the way the first timer's init
       does, so that is done here, or nothing could attach to its output. */
    /* The same kind as the first timer, which the machine chose. */
    if (pit_devs[0].set_out_func == pit_fast_intf.set_out_func) {
        pit_devs[1]      = pit_fast_intf;
        pit_devs[1].data = device_add(&i8254_sec_fast_device);
    } else {
        pit_devs[1]      = pit_classic_intf;
        pit_devs[1].data = device_add(&i8254_sec_device);
    }
    /* A fresh timer has every gate low; these two count from power on. */
    pit_devs[1].set_gate(pit_devs[1].data, 0, 1);
    pit_devs[1].set_gate(pit_devs[1].data, 1, 1);
    pit_devs[1].set_out_func(pit_devs[1].data, 0, sis_85c411_fail_safe);

    io_sethandler(0x0c18, 0x0001, sis_85c411_read, NULL, NULL, sis_85c411_write, NULL, NULL, dev);
    io_sethandler(0x0c1c, 0x0001, sis_85c411_read, NULL, NULL, sis_85c411_write, NULL, NULL, dev);
    io_sethandler(0x0ca0, 0x0005, sis_85c411_read, NULL, NULL, sis_85c411_write, NULL, NULL, dev);
    io_sethandler(0x0c00, 0x0001, sis_85c411_read, NULL, NULL, sis_85c411_write, NULL, NULL, dev);
    io_sethandler(0x0800, 0x0100, sis_85c411_read, NULL, NULL, sis_85c411_write, NULL, NULL, dev);
    io_sethandler(0x0461, 0x0002, sis_85c411_read, NULL, NULL, sis_85c411_write, NULL, NULL, dev);
    io_sethandler(0x0c80, 0x0004, sis_85c411_read, NULL, NULL, sis_85c411_write, NULL, NULL, dev);

    /* Until the machine names itself. */
    memset(dev->board_id, 0xff, sizeof(dev->board_id));

    snprintf(dev->cram_file, sizeof(dev->cram_file), "%s_eisa.nvr",
             machine_get_internal_name());
    sis_85c411_cram_load(dev);

    sis_85c411_reset(dev);

    return dev;
}

const device_t sis_85c411_device = {
    .name          = "SiS 85C406/85C411 EISA",
    .internal_name = "sis_85c411",
    .flags         = 0,
    .local         = 0,
    .init          = sis_85c411_init,
    .close         = sis_85c411_close,
    .reset         = sis_85c411_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
