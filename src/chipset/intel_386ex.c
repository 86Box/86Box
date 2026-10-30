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
 *          PC/AT-compatible ones (the interrupt controllers, the timer and
 *          the two UARTs) also appear at their usual addresses in the DOS
 *          I/O space unless REMAPCFG maps them out of it. The expanded I/O
 *          space is enabled by a write sequence to ports 22h and 23h,
 *          which sets REMAPCFG's ESE bit.
 *
 *          The interrupt controllers, timer and DMA controller are the
 *          machine's usual AT ones, which the chip only maps. The DMA
 *          controller, Port 92 and the other on-chip units (SSIO, power
 *          management, pin configuration) do not appear in the expanded
 *          I/O space, except for PWRCON, which is stored. The chip-select,
 *          refresh control and watchdog timer units have their own files.
 *
 *          The three I/O ports are the machine's GPIO pins, P1 in bits
 *          7:0, P2 in bits 15:8 and P3 in bits 23:16: the chip hands what
 *          it does to the pins to machine_handle_gpio() on every change,
 *          and reads them back from it. A pin the chip does not drive is
 *          handed over at the level of its weak pull-up or pull-down, which
 *          holds it from reset until its port's PnCFG is first written,
 *          or high once they are off, for the board to pull it either way.
 *          Pins in peripheral mode are left undriven, as the peripherals
 *          do not drive them here. Only
 *          A9:0 are decoded in the DOS I/O space, which is not emulated
 *          either: the peripherals only appear at their usual addresses.
 *
 *          CLKPRS clocks the timer, whose OUT1 and OUT2 are hardwired to
 *          the slave's IR2 and IR3 and still drive the refresh request bit
 *          and the speaker as on an AT. The other configuration registers
 *          are stored but have no effect, but for INTCFG's IR3 and IR4 bits
 *          and SIOCFG, which are the serial ports'.
 *
 *          SIOCFG picks the UART baud-rate clocks, from SERCLK or the COMCLK
 *          pin, which the board drives. SIO0 interrupts on the master's IR4
 *          and SIO1 on its IR3 while INTCFG's IR4 or IR3 bit is clear. While
 *          the bit is set, the UART's MCR OUT2 bit picks between it and the
 *          INT9 or INT8 pin (Tables 5-1 and 5-2), which are not emulated, so
 *          the UART is only connected while OUT2 is set. The UARTs gate their
 *          interrupts with OUT2 as on a PC, which the real chip does not do
 *          while the INTCFG bit is clear.
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
#include <86box/serial.h>
#include <86box/machine.h>
#include <86box/i386ex.h>
#include <86box/plat_unused.h>

/* Register addresses. */
#define REMAPCFG      0x0022
#define REMAPCFGH     0x0023
#define ICW1M_PC      0x0020
#define ICW1S_PC      0x00a0
#define TMR0_PC       0x0040
#define RBR0_PC       0x03f8
#define RBR1_PC       0x02f8
#define MCR0_PC       0x03fc
#define MCR1_PC       0x02fc
#define ICW1M         0xf020
#define ICW1S         0xf0a0
#define TMR0          0xf040
#define RBR0          0xf4f8
#define RBR1          0xf8f8
#define MCR0          0xf4fc
#define MCR1          0xf8fc
#define PWRCON        0xf800
#define CLKPRS        0xf804
#define P1CFG         0xf820
#define DMACFG        0xf830
#define INTCFG        0xf832
#define TMRCFG        0xf834
#define SIOCFG        0xf836
#define P1PIN         0xf860

#define REMAP_ESE     0x8000
#define REMAP_S1R     0x0040
#define REMAP_S0R     0x0020
#define REMAP_ISR     0x0010
#define REMAP_IMR     0x0008
#define REMAP_DR      0x0004
#define REMAP_TR      0x0001
#define REMAP_MASK    0x007d

#define INTCFG_IR3    0x40
#define INTCFG_IR4    0x20

#define SIOCFG_S1BSRC 0x02
#define SIOCFG_S0BSRC 0x01

#define MCR_OUT2      0x08

#define PORTS         3

#define EXPANDED_MAX  16

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
    double   comclk; /* What the board drives the COMCLK pin at, 0 if nothing. */

    uint8_t pwrcon;

    uint8_t pcfg[PORTS];
    uint8_t pdir[PORTS];
    uint8_t pltc[PORTS];
    uint8_t weak; /* Ports whose weak resistors are still on, by bit. */

    i386ex_csu_t *csu;
    i386ex_rcu_t *rcu;
    i386ex_wdt_t *wdt;
    serial_t     *uart[2];

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

static const uint16_t uart_base[2]   = { RBR0_PC, RBR1_PC };
static const uint16_t uart_ex[2]     = { RBR0, RBR1 };
static const uint16_t uart_remap[2]  = { REMAP_S0R, REMAP_S1R };
static const uint16_t uart_mcr[2]    = { MCR0_PC, MCR1_PC };
static const uint16_t uart_mcr_ex[2] = { MCR0, MCR1 };
static const uint8_t  uart_irq[2]    = { 4, 3 };
static const uint8_t  uart_intcfg[2] = { INTCFG_IR4, INTCFG_IR3 };

/* The weak pull-ups (1) and pull-downs (0) that hold the port pins from reset (Table 16-1). */
static const uint8_t port_weak[PORTS] = { 0x3f, 0x9f, 0x00 };

/* The pins of a port the chip drives, and to what. */
static uint8_t
i386ex_port_driven(const i386ex_t *dev, int n, uint8_t *val)
{
    /* In I/O mode, a complementary output drives its latch and an open-drain one only a 0. */
    uint8_t io     = ~dev->pcfg[n];
    uint8_t driven = io & (~dev->pdir[n] | ~dev->pltc[n]);

    *val = dev->pltc[n] & driven;

    return driven;
}

/* What the chip does to all the pins, as the machine's GPIO pins see it. */
static uint32_t
i386ex_port_pins(const i386ex_t *dev)
{
    uint32_t pins = 0xff000000;

    for (int n = 0; n < PORTS; n++) {
        uint8_t val;
        uint8_t driven   = i386ex_port_driven(dev, n, &val);
        uint8_t undriven = (dev->weak & (1 << n)) ? port_weak[n] : 0xff;

        pins |= (uint32_t) (val | (undriven & ~driven)) << (n << 3);
    }

    return pins;
}

static void
i386ex_port_update(const i386ex_t *dev)
{
    (void) machine_handle_gpio(1, i386ex_port_pins(dev));
}

static uint8_t
i386ex_port_read(uint16_t port, void *priv)
{
    const i386ex_t *dev = (i386ex_t *) priv;
    int             n;
    uint8_t         val;
    uint8_t         driven;

    if (port < P1PIN) {
        /* PnCFG at F820h, F822h and F824h. */
        n = (port - P1CFG) >> 1;
        return ((port & 1) || (n >= PORTS)) ? 0xff : dev->pcfg[n];
    }

    n = (port - P1PIN) >> 3;
    switch (port & 7) {
        case 0: /* PnPIN: the pins the chip drives, and the board's levels on the rest. */
            driven = i386ex_port_driven(dev, n, &val);
            return val | ((machine_handle_gpio(0, 0xffffffff) >> (n << 3)) & ~driven);
        case 2:
            return dev->pltc[n];
        case 4:
            return dev->pdir[n];
        default:
            return 0xff;
    }
}

static void
i386ex_port_write(uint16_t port, uint8_t val, void *priv)
{
    i386ex_t *dev = (i386ex_t *) priv;
    int       n;

    i386ex_log("386EX: [%04X] = %02X\n", port, val);

    if (port < P1PIN) {
        n = (port - P1CFG) >> 1;
        if ((port & 1) || (n >= PORTS))
            return;

        /* Any write turns the port's weak resistors off until the next reset. */
        dev->pcfg[n] = val;
        dev->weak &= ~(1 << n);
    } else {
        n = (port - P1PIN) >> 3;
        switch (port & 7) {
            case 2:
                dev->pltc[n] = val;
                break;
            case 4:
                dev->pdir[n] = val;
                break;
            default:
                return;
        }
    }

    i386ex_port_update(dev);
}

static int
i386ex_uart_present(const i386ex_t *dev, int n)
{
    return (dev->uart[n] != NULL) && com_ports[dev->uart[n]->inst].enabled;
}

/*
   The baud-rate generators run off the COMCLK pin or SERCLK, which is CLK2 / 4.
   A UART cannot be stopped, so one on a COMCLK the board does not drive keeps
   its clock.
 */
static void
i386ex_uart_clock(i386ex_t *dev)
{
    static const uint8_t bsrc[2] = { SIOCFG_S0BSRC, SIOCFG_S1BSRC };
    double               clock;

    for (int n = 0; n < 2; n++) {
        if (dev->uart[n] == NULL)
            continue;

        clock = (dev->siocfg & bsrc[n]) ? ((double) cpu_s->rspeed / 2.0) : dev->comclk;
        if (clock > 0.0)
            serial_set_clock_src(dev->uart[n], clock);
    }
}

/*
   A UART's IRQ: its IR while INTCFG's bit for the IR is clear or the UART's
   OUT2 is set, and none otherwise (Tables 5-1 and 5-2).
 */
static uint8_t
i386ex_uart_irq(const i386ex_t *dev, int n)
{
    if (!(dev->intcfg & uart_intcfg[n]) || (dev->uart[n]->mctrl & MCR_OUT2))
        return uart_irq[n];

    return 0xff;
}

static void
i386ex_uart_connect(const i386ex_t *dev)
{
    for (int n = 0; n < 2; n++) {
        if (i386ex_uart_present(dev, n))
            serial_irq(dev->uart[n], i386ex_uart_irq(dev, n));
    }
}

/*
   Follows the MCR writes, which the UART takes itself, for OUT2. This handler
   is added after the UART's, so it runs after the UART has stored the value.
 */
static void
i386ex_uart_mcr_write(UNUSED(uint16_t port), UNUSED(uint8_t val), void *priv)
{
    i386ex_uart_connect((i386ex_t *) priv);
}

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

    for (int n = 0; n < 2; n++) {
        if (!(mask & uart_remap[n]) || !i386ex_uart_present(dev, n))
            continue;

        if (!expanded) {
            /* Through serial_setup(), so that the UART knows its own address. */
            if (set)
                serial_setup(dev->uart[n], uart_base[n], i386ex_uart_irq(dev, n));
            else
                serial_remove(dev->uart[n]);
        } else
            io_handler(set, uart_ex[n], 8, serial_read, NULL, NULL, serial_write, NULL, NULL, dev->uart[n]);

        io_handler(set, expanded ? uart_mcr_ex[n] : uart_mcr[n], 1,
                   NULL, NULL, NULL, i386ex_uart_mcr_write, NULL, NULL, dev);
    }
}

static uint8_t i386ex_config_read(uint16_t port, void *priv);
static void    i386ex_config_write(uint16_t port, uint8_t val, void *priv);

static void
i386ex_expanded(i386ex_t *dev, int set)
{
    i386ex_log("386EX: expanded I/O space %s\n", set ? "enabled" : "disabled");

    i386ex_map_peripherals(dev, set, 1, REMAP_IMR | REMAP_ISR | REMAP_TR | REMAP_S0R | REMAP_S1R);

    io_handler(set, PWRCON, 1, i386ex_config_read, NULL, NULL, i386ex_config_write, NULL, NULL, dev);
    io_handler(set, CLKPRS, 2, i386ex_config_read, NULL, NULL, i386ex_config_write, NULL, NULL, dev);
    io_handler(set, DMACFG, 8, i386ex_config_read, NULL, NULL, i386ex_config_write, NULL, NULL, dev);
    io_handler(set, P1CFG, 6, i386ex_port_read, NULL, NULL, i386ex_port_write, NULL, NULL, dev);
    io_handler(set, P1PIN, 24, i386ex_port_read, NULL, NULL, i386ex_port_write, NULL, NULL, dev);

    i386ex_csu_handler(dev->csu, set);
    i386ex_rcu_handler(dev->rcu, set);
    i386ex_wdt_handler(dev->wdt, set);

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
        case PWRCON:
            return dev->pwrcon;
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
        case PWRCON:
            dev->pwrcon = val & 0x0f;
            break;
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
            i386ex_uart_connect(dev);
            break;
        case TMRCFG:
            dev->tmrcfg = val;
            break;
        case SIOCFG:
            dev->siocfg = val;
            i386ex_uart_clock(dev);
            break;

        default:
            break;
    }
}

i386ex_csu_t *
i386ex_get_csu(i386ex_t *dev)
{
    return dev->csu;
}

void
i386ex_set_wdtout(i386ex_t *dev, void (*func)(void *priv), void *priv)
{
    i386ex_wdt_set_out(dev->wdt, func, priv);
}

/* WDTOUT on RESET resets the whole chip, which is as good as a hard reset. */
void
i386ex_wdtout_reset(UNUSED(void *priv))
{
    pc_reset_hard();
}

void
i386ex_wdtout_nmi(UNUSED(void *priv))
{
    nmi_raise();
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

void
i386ex_set_comclk(i386ex_t *dev, double hz)
{
    dev->comclk = hz;
    i386ex_uart_clock(dev);
}

static void
i386ex_speed_changed(void *priv)
{
    i386ex_t *dev = (i386ex_t *) priv;

    i386ex_timer_clock(dev);
    i386ex_uart_clock(dev);
}

static void *
i386ex_init(const device_t *info)
{
    i386ex_t *dev = (i386ex_t *) calloc(1, sizeof(i386ex_t));

    dev->csu = device_add(&i386ex_csu_device);
    dev->rcu = device_add(&i386ex_rcu_device);
    dev->wdt = device_add(&i386ex_wdt_device);

    /* The UARTs take the next free COM ports, which on a board are COM1 and COM2. */
    if (info->local & I386EX_SIO0)
        dev->uart[0] = device_add_inst(&ns16450_device, 1);
    if (info->local & I386EX_SIO1)
        dev->uart[1] = device_add_inst(&ns16450_device, 2);

    i386ex_map_peripherals(dev, 1, 0, REMAP_S0R | REMAP_S1R);
    i386ex_uart_clock(dev);
    i386ex_timer_clock(dev);

    /* OUT1 and OUT2 still request refresh and drive the speaker, as on an AT. */
    if (pit_devs[0].set_out_func != NULL) {
        pit_devs[0].set_out_func(pit_devs[0].data, 1, i386ex_tmrout1);
        pit_devs[0].set_out_func(pit_devs[0].data, 2, i386ex_tmrout2);
    }

    /* The port pins come out of reset as inputs, held by their weak resistors. */
    for (int n = 0; n < PORTS; n++)
        dev->pdir[n] = dev->pltc[n] = 0xff;
    dev->weak = (1 << PORTS) - 1;
    i386ex_port_update(dev);

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
