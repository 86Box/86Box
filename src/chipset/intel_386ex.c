/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 386EX on-chip peripherals.
 *
 *          All of the on-chip peripheral registers are in the expanded
 *          I/O space (0xF000-0xFFFF), which is hidden after reset. The
 *          PC/AT-compatible ones (the interrupt controllers and the timer)
 *          also appear at their usual addresses in the DOS I/O space unless
 *          REMAPCFG maps them out of it. The expanded I/O space is enabled
 *          by a write sequence to ports 22h and 23h, which sets REMAPCFG's
 *          ESE bit.
 *
 *          The interrupt controllers, timer and DMA controller are the
 *          machine's usual AT ones, which the chip only maps. The DMA
 *          controller, Port 92 and the other on-chip units (chip-select,
 *          watchdog, refresh, SSIO, power management, parallel ports, pin
 *          configuration) do not appear in the expanded I/O space. Only
 *          A9:0 are decoded in the DOS I/O space, which is not emulated
 *          either: the peripherals only appear at their usual addresses.
 *
 *          CLKPRS clocks the timer, whose OUT1 and OUT2 are hardwired to
 *          the slave's IR2 and IR3 and still drive the refresh request bit
 *          and the speaker as on an AT. The other configuration registers
 *          are stored but have no effect.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
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
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/i386ex.h>
#include <86box/plat_unused.h>

/* Register addresses. */
#define REMAPCFG     0x0022
#define REMAPCFGH    0x0023
#define ICW1M_PC     0x0020
#define ICW1S_PC     0x00a0
#define TMR0_PC      0x0040
#define ICW1M        0xf020
#define ICW1S        0xf0a0
#define TMR0         0xf040
#define CLKPRS       0xf804
#define DMACFG       0xf830
#define INTCFG       0xf832
#define TMRCFG       0xf834
#define SIOCFG       0xf836

#define REMAP_ESE    0x8000
#define REMAP_S1R    0x0040
#define REMAP_S0R    0x0020
#define REMAP_ISR    0x0010
#define REMAP_IMR    0x0008
#define REMAP_DR     0x0004
#define REMAP_TR     0x0001
#define REMAP_MASK   0x007d

#define EXPANDED_MAX 16

typedef struct i386ex_handler_t {
    uint16_t base;
    int      size;
    uint8_t (*inb)(uint16_t addr, void *priv);
    uint16_t (*inw)(uint16_t addr, void *priv);
    uint32_t (*inl)(uint16_t addr, void *priv);
    void (*outb)(uint16_t addr, uint8_t val, void *priv);
    void (*outw)(uint16_t addr, uint16_t val, void *priv);
    void (*outl)(uint16_t addr, uint32_t val, void *priv);
    void *priv;
} i386ex_handler_t;

struct i386ex_t {
    uint16_t remapcfg;
    uint8_t  unlock; /* Writes of the enable sequence seen so far. */

    uint16_t clkprs;
    uint8_t  dmacfg;
    uint8_t  intcfg;
    uint8_t  tmrcfg;
    uint8_t  siocfg;

    i386ex_handler_t handlers[EXPANDED_MAX];
    int              handlers_num;
};

#ifdef ENABLE_I386EX_LOG
int i386ex_do_log = ENABLE_I386EX_LOG;

static void
i386ex_log(const char *fmt, ...)
{
    va_list ap;

    if (i386ex_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define i386ex_log(fmt, ...)
#endif

/* PSCLK, which clocks the timer, is the internal clock divided by CLKPRS + 2. */
static void
i386ex_timer_clock(const i386ex_t *dev)
{
    if ((pit_devs[0].data != NULL) && (pit_devs[0].set_pit_const != NULL))
        pit_devs[0].set_pit_const(pit_devs[0].data, ((uint64_t) (dev->clkprs & 0x01ff) + 2) << 32);
}

/* TMROUT1 and TMROUT2 are hardwired to the slave's IR2 and IR3. */
static void
i386ex_tmrout1(int new_out, int old_out, void *priv)
{
    pit_refresh_timer_at(new_out, old_out, priv);

    if (new_out && !old_out)
        picint(1 << 10);
    if (!new_out)
        picintc(1 << 10);
}

static void
i386ex_tmrout2(int new_out, int old_out, void *priv)
{
    pit_speaker_timer(new_out, old_out, priv);

    if (new_out && !old_out)
        picint(1 << 11);
    if (!new_out)
        picintc(1 << 11);
}

/* Maps or unmaps the PC/AT-compatible peripherals selected by mask, at their PC/AT or expanded addresses. */
static void
i386ex_map_peripherals(i386ex_t *dev, int set, int expanded, uint16_t mask)
{
    if (mask & REMAP_IMR)
        io_handler(set, expanded ? ICW1M : ICW1M_PC, 2, pic_read, NULL, NULL, pic_write, NULL, NULL, &pic);
    if (mask & REMAP_ISR)
        io_handler(set, expanded ? ICW1S : ICW1S_PC, 2, pic_read, NULL, NULL, pic_write, NULL, NULL, &pic2);
    if ((mask & REMAP_TR) && (pit_devs[0].data != NULL))
        io_handler(set, expanded ? TMR0 : TMR0_PC, 4,
                   pit_devs[0].read, NULL, NULL, pit_devs[0].write, NULL, NULL, pit_devs[0].data);
}

static uint8_t i386ex_config_read(uint16_t port, void *priv);
static void    i386ex_config_write(uint16_t port, uint8_t val, void *priv);

static void
i386ex_expanded(i386ex_t *dev, int set)
{
    i386ex_log("386EX: expanded I/O space %s\n", set ? "enabled" : "disabled");

    i386ex_map_peripherals(dev, set, 1, REMAP_IMR | REMAP_ISR | REMAP_TR | REMAP_S0R | REMAP_S1R);

    io_handler(set, CLKPRS, 2, i386ex_config_read, NULL, NULL, i386ex_config_write, NULL, NULL, dev);
    io_handler(set, DMACFG, 8, i386ex_config_read, NULL, NULL, i386ex_config_write, NULL, NULL, dev);

    for (int i = 0; i < dev->handlers_num; i++) {
        const i386ex_handler_t *h = &dev->handlers[i];

        io_handler(set, h->base, h->size, h->inb, h->inw, h->inl, h->outb, h->outw, h->outl, h->priv);
    }
}

static void
i386ex_remapcfg_set(i386ex_t *dev, uint16_t val)
{
    uint16_t old = dev->remapcfg;
    uint16_t changed;

    dev->remapcfg = val & (REMAP_ESE | REMAP_MASK);
    changed       = old ^ dev->remapcfg;

    i386ex_log("386EX: REMAPCFG = %04X\n", dev->remapcfg);

    if (changed & REMAP_DR)
        i386ex_log("386EX: DMA remapping is not emulated\n");

    /* A peripheral is in the DOS I/O space while its remap bit is clear. */
    if (changed & REMAP_MASK & ~REMAP_DR) {
        i386ex_map_peripherals(dev, 0, 0, changed & dev->remapcfg);
        i386ex_map_peripherals(dev, 1, 0, changed & old);
    }

    if (changed & REMAP_ESE)
        i386ex_expanded(dev, !!(dev->remapcfg & REMAP_ESE));
}

/*
   REMAPCFG is read-only until the expanded I/O space is enabled, which takes
   a byte write of 00h to 23h, a byte write of 80h to 22h and a word write of
   0080h to 22h, in that order. Writing 00h to 23h disables it again, so the
   sequence works whether or not it already is enabled.
 */
static void
i386ex_remapcfg_write(uint16_t port, uint8_t val, void *priv)
{
    i386ex_t *dev    = (i386ex_t *) priv;
    uint8_t   unlock = 0;

    if ((port == REMAPCFGH) && (val == 0x00))
        unlock = 1;
    else if ((port == REMAPCFG) && (val == 0x80) && (dev->unlock == 1))
        unlock = 2;

    if (dev->remapcfg & REMAP_ESE) {
        if (port & 1)
            i386ex_remapcfg_set(dev, (dev->remapcfg & 0x00ff) | (val << 8));
        else
            i386ex_remapcfg_set(dev, (dev->remapcfg & 0xff00) | val);
    }

    dev->unlock = unlock;
}

static void
i386ex_remapcfg_writew(uint16_t port, uint16_t val, void *priv)
{
    i386ex_t *dev = (i386ex_t *) priv;

    if (dev->remapcfg & REMAP_ESE)
        i386ex_remapcfg_set(dev, val);
    else if ((port == REMAPCFG) && (val == 0x0080) && (dev->unlock == 2))
        i386ex_remapcfg_set(dev, dev->remapcfg | REMAP_ESE);

    dev->unlock = 0;
}

static uint8_t
i386ex_remapcfg_read(uint16_t port, void *priv)
{
    const i386ex_t *dev = (i386ex_t *) priv;

    return dev->remapcfg >> ((port & 1) << 3);
}

static uint16_t
i386ex_remapcfg_readw(UNUSED(uint16_t port), void *priv)
{
    const i386ex_t *dev = (i386ex_t *) priv;

    return dev->remapcfg;
}

static uint8_t
i386ex_config_read(uint16_t port, void *priv)
{
    const i386ex_t *dev = (i386ex_t *) priv;

    switch (port) {
        case CLKPRS:
            return dev->clkprs & 0xff;
        case CLKPRS + 1:
            return dev->clkprs >> 8;
        case DMACFG:
            return dev->dmacfg;
        case INTCFG:
            return dev->intcfg;
        case TMRCFG:
            return dev->tmrcfg;
        case SIOCFG:
            return dev->siocfg;
        default:
            return 0xff;
    }
}

static void
i386ex_config_write(uint16_t port, uint8_t val, void *priv)
{
    i386ex_t *dev = (i386ex_t *) priv;

    i386ex_log("386EX: [%04X] = %02X\n", port, val);

    switch (port) {
        case CLKPRS:
            dev->clkprs = (dev->clkprs & 0xff00) | val;
            i386ex_timer_clock(dev);
            break;
        case CLKPRS + 1:
            dev->clkprs = (dev->clkprs & 0x00ff) | ((val & 0x01) << 8);
            i386ex_timer_clock(dev);
            break;
        case DMACFG:
            dev->dmacfg = val;
            break;
        case INTCFG:
            dev->intcfg = val;
            break;
        case TMRCFG:
            dev->tmrcfg = val;
            break;
        case SIOCFG:
            dev->siocfg = val;
            break;

        default:
            break;
    }
}

void
i386ex_expanded_handler(i386ex_t *dev, uint16_t base, int size,
                        uint8_t (*inb)(uint16_t addr, void *priv),
                        uint16_t (*inw)(uint16_t addr, void *priv),
                        uint32_t (*inl)(uint16_t addr, void *priv),
                        void (*outb)(uint16_t addr, uint8_t val, void *priv),
                        void (*outw)(uint16_t addr, uint16_t val, void *priv),
                        void (*outl)(uint16_t addr, uint32_t val, void *priv),
                        void *priv)
{
    i386ex_handler_t *h;

    if (dev->handlers_num >= EXPANDED_MAX)
        fatal("386EX: too many expanded I/O space handlers\n");

    h       = &dev->handlers[dev->handlers_num++];
    h->base = base;
    h->size = size;
    h->inb  = inb;
    h->inw  = inw;
    h->inl  = inl;
    h->outb = outb;
    h->outw = outw;
    h->outl = outl;
    h->priv = priv;

    if (dev->remapcfg & REMAP_ESE)
        io_sethandler(base, size, inb, inw, inl, outb, outw, outl, priv);
}

static void
i386ex_speed_changed(void *priv)
{
    i386ex_t *dev = (i386ex_t *) priv;

    i386ex_timer_clock(dev);
}

static void *
i386ex_init(UNUSED(const device_t *info))
{
    i386ex_t *dev = (i386ex_t *) calloc(1, sizeof(i386ex_t));

    i386ex_timer_clock(dev);

    /* OUT1 and OUT2 still request refresh and drive the speaker, as on an AT. */
    if (pit_devs[0].set_out_func != NULL) {
        pit_devs[0].set_out_func(pit_devs[0].data, 1, i386ex_tmrout1);
        pit_devs[0].set_out_func(pit_devs[0].data, 2, i386ex_tmrout2);
    }

    io_sethandler(REMAPCFG, 2,
                  i386ex_remapcfg_read, i386ex_remapcfg_readw, NULL,
                  i386ex_remapcfg_write, i386ex_remapcfg_writew, NULL, dev);

    return dev;
}

static void
i386ex_close(void *priv)
{
    free(priv);
}

const device_t i386ex_device = {
    .name          = "Intel 386EX",
    .internal_name = "i386ex",
    .flags         = 0,
    .local         = 0,
    .init          = i386ex_init,
    .close         = i386ex_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = i386ex_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
