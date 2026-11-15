/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Differential tests of the Intel 386EX DMA unit's 8237A-compatible
 *          view against 86Box's 8237A.
 *
 *          Each scenario programs channel 0 or 1 through the PC/AT ports,
 *          as DOS software does (addresses and counts by the byte pointer,
 *          page registers 87h and 83h, mode, mask, master clear and status),
 *          and moves data through dma.h as a device on the channel does. It
 *          runs once on 86Box's 8237A, set up as on an AT, and once on the
 *          386EX DMA unit mapped in the DOS I/O space with its DRQ pins wired
 *          to 86Box channels 0 and 1, and everything either one shows (what
 *          the device gets back, memory, the address and count registers,
 *          the status register) has to match.
 *
 *          Two things the 386EX does differently, as documented, are checked
 *          against the 386EX alone instead: a verify transfer goes through,
 *          counting and reaching TC, where 86Box's AT 8237A path gives the
 *          device no data in verify mode (its XT path goes through, as a real
 *          8237A does); and DMACLR, the 8237A's master clear, sets all DMA
 *          functions to their default states (272485, Table 12-4), DMAMOD1's
 *          verify mode included, where an 8237A's leaves the mode registers.
 *          A fly-by software request, which has no device on the channel to
 *          take or give the data, is checked on the 386EX too: nothing moves,
 *          but the channel counts to TC.
 *
 *          The I/O space, memory, timers and the rest of the machine are
 *          stubs here.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#include <stdarg.h>
#include <stdbool.h>
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
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/machine.h>
#include <86box/i386ex.h>

/* ---- The machine, as stubs ------------------------------------------------------------------- */

#define PORT_HANDLERS 4

typedef struct {
    uint8_t (*inb)(uint16_t addr, void *priv);
    void (*outb)(uint16_t addr, uint8_t val, void *priv);
    void *priv;
} port_handler_t;

static port_handler_t ports[0x10000][PORT_HANDLERS];

static void
io_set_one(uint16_t port, uint8_t (*inb)(uint16_t, void *), void (*outb)(uint16_t, uint8_t, void *), void *priv)
{
    for (int i = 0; i < PORT_HANDLERS; i++) {
        if ((ports[port][i].inb == NULL) && (ports[port][i].outb == NULL)) {
            ports[port][i] = (port_handler_t) { inb, outb, priv };
            return;
        }
    }
    fprintf(stderr, "too many handlers at port %04X\n", port);
    exit(2);
}

static void
io_remove_one(uint16_t port, uint8_t (*inb)(uint16_t, void *), void (*outb)(uint16_t, uint8_t, void *), void *priv)
{
    for (int i = 0; i < PORT_HANDLERS; i++) {
        if ((ports[port][i].inb == inb) && (ports[port][i].outb == outb) && (ports[port][i].priv == priv)) {
            memset(&ports[port][i], 0, sizeof(port_handler_t));
            return;
        }
    }
}

void
io_sethandler(uint16_t base, uint16_t size,
              uint8_t (*inb)(uint16_t addr, void *priv), uint16_t (*inw)(uint16_t addr, void *priv),
              uint32_t (*inl)(uint16_t addr, void *priv), void (*outb)(uint16_t addr, uint8_t val, void *priv),
              void (*outw)(uint16_t addr, uint16_t val, void *priv), void (*outl)(uint16_t addr, uint32_t val, void *priv),
              void *priv)
{
    (void) inw;
    (void) inl;
    (void) outw;
    (void) outl;
    for (int i = 0; i < size; i++)
        io_set_one(base + i, inb, outb, priv);
}

void
io_removehandler(uint16_t base, uint16_t size,
                 uint8_t (*inb)(uint16_t addr, void *priv), uint16_t (*inw)(uint16_t addr, void *priv),
                 uint32_t (*inl)(uint16_t addr, void *priv), void (*outb)(uint16_t addr, uint8_t val, void *priv),
                 void (*outw)(uint16_t addr, uint16_t val, void *priv), void (*outl)(uint16_t addr, uint32_t val, void *priv),
                 void *priv)
{
    (void) inw;
    (void) inl;
    (void) outw;
    (void) outl;
    for (int i = 0; i < size; i++)
        io_remove_one(base + i, inb, outb, priv);
}

void
io_handler(uint8_t set, uint16_t base, uint16_t size,
           uint8_t (*inb)(uint16_t addr, void *priv), uint16_t (*inw)(uint16_t addr, void *priv),
           uint32_t (*inl)(uint16_t addr, void *priv), void (*outb)(uint16_t addr, uint8_t val, void *priv),
           void (*outw)(uint16_t addr, uint16_t val, void *priv), void (*outl)(uint16_t addr, uint32_t val, void *priv),
           void *priv)
{
    if (set)
        io_sethandler(base, size, inb, inw, inl, outb, outw, outl, priv);
    else
        io_removehandler(base, size, inb, inw, inl, outb, outw, outl, priv);
}

/* As 86Box's I/O space: the readers' results are ANDed, and every writer sees a write. */
uint8_t
inb(uint16_t port)
{
    uint8_t ret = 0xff;

    for (int i = 0; i < PORT_HANDLERS; i++) {
        if (ports[port][i].inb != NULL)
            ret &= ports[port][i].inb(port, ports[port][i].priv);
    }

    return ret;
}

void
outb(uint16_t port, uint8_t val)
{
    for (int i = 0; i < PORT_HANDLERS; i++) {
        if (ports[port][i].outb != NULL)
            ports[port][i].outb(port, val, ports[port][i].priv);
    }
}

uint16_t
inw(uint16_t port)
{
    return inb(port) | (inb(port + 1) << 8);
}

void
outw(uint16_t port, uint16_t val)
{
    outb(port, val & 0xff);
    outb(port + 1, val >> 8);
}

#define RAM_SIZE (16 << 20)
static uint8_t test_ram[RAM_SIZE];

uint8_t
mem_readb_phys(uint32_t addr)
{
    return (addr < RAM_SIZE) ? test_ram[addr] : 0xff;
}

uint16_t
mem_readw_phys(uint32_t addr)
{
    return mem_readb_phys(addr) | (mem_readb_phys(addr + 1) << 8);
}

void
mem_writeb_phys(uint32_t addr, uint8_t val)
{
    if (addr < RAM_SIZE)
        test_ram[addr] = val;
}

void
mem_writew_phys(uint32_t addr, uint16_t val)
{
    mem_writeb_phys(addr, val & 0xff);
    mem_writeb_phys(addr + 1, val >> 8);
}

void
mem_read_phys(void *dest, uint32_t addr, int size)
{
    for (int i = 0; i < size; i++)
        ((uint8_t *) dest)[i] = mem_readb_phys(addr + i);
}

void
mem_write_phys(void *src, uint32_t addr, int size)
{
    for (int i = 0; i < size; i++)
        mem_writeb_phys(addr + i, ((uint8_t *) src)[i]);
}

uint32_t
mem_read_phys_block(void *dest, uint32_t addr, uint32_t len)
{
    mem_read_phys(dest, addr, (int) len);
    return len;
}

uint32_t
mem_write_phys_block(const void *src, uint32_t addr, uint32_t len)
{
    mem_write_phys((void *) src, addr, (int) len);
    return len;
}

/* Timers run only when a test runs them. */
#define TIMERS 8
static pc_timer_t *timers[TIMERS];
uint64_t           tsc;
uint64_t           TIMER_USEC = 1ULL << 32;

void
timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer)
{
    memset(timer, 0, sizeof(pc_timer_t));
    timer->callback = callback;
    timer->priv     = priv;
    for (int i = 0; i < TIMERS; i++) {
        if (timers[i] == NULL) {
            timers[i] = timer;
            break;
        }
    }
    if (start_timer)
        timer_enable(timer);
}

void
timer_enable(pc_timer_t *timer)
{
    timer->flags |= TIMER_ENABLED;
}

void
timer_disable(pc_timer_t *timer)
{
    timer->flags &= ~TIMER_ENABLED;
}

/* Runs every enabled timer until none is left enabled, or a bound is reached. */
static void
run_timers(void)
{
    for (int round = 0; round < 100000; round++) {
        int ran = 0;

        for (int i = 0; i < TIMERS; i++) {
            if ((timers[i] != NULL) && (timers[i]->flags & TIMER_ENABLED)) {
                timers[i]->flags &= ~TIMER_ENABLED;
                timers[i]->callback(timers[i]->priv);
                ran = 1;
            }
        }
        if (!ran)
            return;
    }
}

static int irq_raised[16];

void
picint_common(uint16_t num, int level, int set, uint8_t *irq_state)
{
    (void) level;
    (void) irq_state;
    for (int i = 0; i < 16; i++) {
        if (num & (1 << i))
            irq_raised[i] = set;
    }
}

static CPU cpu_25mhz = { .rspeed = 25000000 };
CPU       *cpu_s     = &cpu_25mhz;
cpu_state_t cpu_state;
double      cpu_busspeed = 8000000.0;
int         is286        = 1;
int         is_nec       = 0;
int         machine      = 0;
const machine_t machines[1];

int machine_at_wd76c10_init(const machine_t *model) { (void) model; return 0; }
int machine_xt_ibm5550_init(const machine_t *model) { (void) model; return 0; }
int machine_xt_ibm5535_init(const machine_t *model) { (void) model; return 0; }
void ibm5140_clock_wake(void) { }
bool m808x_86box_active(void) { return false; }
bool m808x_86box_dma_try_request_ex(unsigned wait_clocks, void (*ack_callback)(void *opaque), void *opaque) { (void) wait_clocks; (void) ack_callback; (void) opaque; return false; }
bool m808x_86box_dma_cancel_request_ex(void (*ack_callback)(void *opaque), void *opaque) { (void) ack_callback; (void) opaque; return false; }
void nmi_raise(void) { }
void ps2_cache_clean(void) { }
void refreshread(void) { }
void refreshread_vx0(void) { }
void sub_cycles(int c) { (void) c; }

void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(2);
}

/* ---- The two controllers ---------------------------------------------------------------------- */

typedef enum { WORLD_8237 = 0, WORLD_386EX = 1 } world_t;

static const char *world_name[2] = { "8237A", "386EX" };
static void       *i386ex_dma;

static void
world_start(world_t w)
{
    memset(ports, 0, sizeof(ports));
    memset(timers, 0, sizeof(timers));
    memset(test_ram, 0, sizeof(test_ram));
    memset(irq_raised, 0, sizeof(irq_raised));

    /* Every machine has the DMA core; an AT has the first 8237A and its page registers on it, a 386EX board its own unit (MACHINE_NONSTANDARD_DMA). */
    dma_set_at(1);
    dma_core_init();
    if (w == WORLD_8237)
        dma_init();
    else {
        i386ex_dma = i386ex_dma_device.init(&i386ex_dma_device);
        i386ex_dma_handler(i386ex_dma, 1, 0); /* REMAPCFG's DR bit clear: the DOS I/O space */
        i386ex_dma_set_channels(i386ex_dma, 0, 1);
    }
}

static void
world_stop(world_t w)
{
    if (w == WORLD_386EX) {
        i386ex_dma_device.close(i386ex_dma);
        i386ex_dma = NULL;
    }
    dma_set_controller(NULL, NULL);
}

/* ---- What a scenario sees --------------------------------------------------------------------- */

#define LOG_MAX 4096
#define LINE    160

typedef struct {
    char lines[LOG_MAX][LINE];
    int  n;
} log_t;

static log_t  logs[2];
static log_t *cur;

static void
note(const char *fmt, ...)
{
    va_list ap;

    if (cur->n >= LOG_MAX)
        return;
    va_start(ap, fmt);
    vsnprintf(cur->lines[cur->n++], LINE, fmt, ap);
    va_end(ap);
}

/* The PC/AT ports for channels 0 and 1 of the first 8237A. */
#define P_ADDR(ch)  ((ch) << 1)
#define P_COUNT(ch) (((ch) << 1) + 1)
#define P_PAGE(ch)  ((ch) ? 0x83 : 0x87)
#define P_STATUS    0x08
#define P_REQUEST   0x09
#define P_MASK1     0x0a
#define P_MODE      0x0b
#define P_CLRFF     0x0c
#define P_MCLR      0x0d
#define P_CLRMASK   0x0e
#define P_MASKALL   0x0f

static void
program(int ch, uint8_t mode, uint32_t addr, uint16_t count)
{
    outb(P_MASK1, 0x04 | ch);
    outb(P_MODE, mode | ch);
    outb(P_CLRFF, 0);
    outb(P_ADDR(ch), addr & 0xff);
    outb(P_ADDR(ch), (addr >> 8) & 0xff);
    outb(P_PAGE(ch), (addr >> 16) & 0xff);
    outb(P_CLRFF, 0);
    outb(P_COUNT(ch), count & 0xff);
    outb(P_COUNT(ch), count >> 8);
}

static void
note_regs(int ch)
{
    uint16_t a;
    uint16_t c;

    outb(P_CLRFF, 0);
    a = inb(P_ADDR(ch));
    a |= inb(P_ADDR(ch)) << 8;
    c = inb(P_COUNT(ch));
    c |= inb(P_COUNT(ch)) << 8;
    note("ch%d address %04X count %04X page %02X", ch, a, c, inb(P_PAGE(ch)));
}

/* The status register's TC bit for the channel, which reading clears, and its request bit. */
static void
note_status(int ch)
{
    uint8_t s = inb(P_STATUS);

    note("status: TC%d %d, request %d", ch, !!(s & (1 << ch)), !!(s & (0x10 << ch)));
}

static void
note_mem(uint32_t addr, int len)
{
    char buf[LINE];
    int  o = 0;

    for (int i = 0; (i < len) && (o < LINE - 4); i++)
        o += snprintf(buf + o, LINE - o, "%02X ", test_ram[addr + i]);
    note("mem %06X: %s", addr, buf);
}

static void
note_ret(const char *what, int i, int ret)
{
    if (ret == DMA_NODATA)
        note("%s %d: no data", what, i);
    else
        note("%s %d: %s%s%02X", what, i, (ret & DMA_OVER) ? "TC " : "", (ret & DMA_VERIFY) ? "verify " : "", ret & 0xff);
}

/* ---- Scenarios -------------------------------------------------------------------------------- */

/* A device writing memory (as a floppy read or a recording), single mode, then one more after TC. */
static void
sc_write_single(int ch)
{
    program(ch, 0x44, 0x23456, 7);
    outb(P_MASK1, ch);
    for (int i = 0; i < 9; i++)
        note_ret("write", i, dma_channel_write(ch, 0xa0 + i));
    note_mem(0x23454, 12);
    note_regs(ch);
    note_status(ch);
    note_status(ch);
}

/* A device reading memory (as playback), single mode, then once more after TC. */
static void
sc_read_single(int ch)
{
    for (int i = 0; i < 16; i++)
        test_ram[0x34560 + i] = 0x30 + i;
    program(ch, 0x48, 0x34560, 7);
    outb(P_MASK1, ch);
    for (int i = 0; i < 9; i++)
        note_ret("read", i, dma_channel_read(ch));
    note_regs(ch);
    note_status(ch);
}

/* Autoinitialize: the channel starts over at the end of the buffer and stays unmasked. */
static void
sc_read_autoinit(int ch)
{
    for (int i = 0; i < 16; i++)
        test_ram[0x45670 + i] = 0x50 + i;
    program(ch, 0x58, 0x45670, 3);
    outb(P_MASK1, ch);
    for (int i = 0; i < 10; i++)
        note_ret("read", i, dma_channel_read(ch));
    note_regs(ch);
    note_status(ch);
}

/* Address decrement. */
static void
sc_read_decrement(int ch)
{
    for (int i = 0; i < 16; i++)
        test_ram[0x56780 + i] = 0x70 + i;
    program(ch, 0x68, 0x56788, 4);
    outb(P_MASK1, ch);
    for (int i = 0; i < 6; i++)
        note_ret("read", i, dma_channel_read(ch));
    note_regs(ch);
}

/* The address stays in its 64K page: it wraps from FFFFh to 0000h. */
static void
sc_page_wrap(int ch)
{
    program(ch, 0x44, 0x1fffe, 3);
    outb(P_MASK1, ch);
    for (int i = 0; i < 4; i++)
        note_ret("write", i, dma_channel_write(ch, 0xc0 + i));
    note_mem(0x1fffc, 4);
    note_mem(0x10000, 4);
    note_mem(0x20000, 4);
    note_regs(ch);
}

/* Masks: the channel's own, all of them, clearing them all, and master clear. */
static void
sc_masks(int ch)
{
    for (int i = 0; i < 8; i++)
        test_ram[0x78900 + i] = 0x90 + i;
    program(ch, 0x48, 0x78900, 7);
    note_ret("masked read", 0, dma_channel_read(ch));
    outb(P_CLRMASK, 0);
    note_ret("after clear mask", 0, dma_channel_read(ch));
    outb(P_MASKALL, 0x0f);
    note_ret("after mask all", 0, dma_channel_read(ch));
    outb(P_MASKALL, 0x00);
    note_ret("after unmask all", 0, dma_channel_read(ch));
    outb(P_MCLR, 0);
    note_ret("after master clear", 0, dma_channel_read(ch));
}

/* The byte pointer: writes and reads take the low byte, then the high one, until it is cleared. */
static void
sc_flip_flop(int ch)
{
    outb(P_CLRFF, 0);
    outb(P_ADDR(ch), 0x12);
    outb(P_ADDR(ch), 0x34);
    outb(P_ADDR(ch), 0x56); /* the low byte again */
    outb(P_CLRFF, 0);
    note("address bytes %02X %02X", inb(P_ADDR(ch)), inb(P_ADDR(ch)));
    note("then without clearing: %02X", inb(P_ADDR(ch)));
    outb(P_CLRFF, 0);
    outb(P_COUNT(ch), 0x78);
    note("count low after one write, read high first: %02X", (inb(P_COUNT(ch)), inb(P_COUNT(ch))));
}

/* A device's DRQ shows in the status register's request bit. */
static void
sc_drq_status(int ch)
{
    program(ch, 0x48, 0x10000, 7);
    note_status(ch);
    dma_set_drq(ch, 1);
    note("dma_get_drq %d", dma_get_drq(ch));
    note_status(ch);
    dma_set_drq(ch, 0);
    note_status(ch);
}

/*
   The read_only and advance pair, as the Sound Blaster reads a sample: a
   fetch leaves the channel where it was until advance, and a second fetch
   without one gets the advance first. (Not across TC: 86Box's AT 8237A path
   fetches before it settles the advance, and so reads past TC there.)
 */
static void
sc_read_only_advance(int ch)
{
    for (int i = 0; i < 8; i++)
        test_ram[0x5a000 + i] = 0xd0 + i;
    program(ch, 0x48, 0x5a000, 4);
    outb(P_MASK1, ch);
    note_ret("read_only", 0, dma_channel_read_only(ch));
    note_ret("read_only again", 0, dma_channel_read_only(ch));
    note("advance 0: %d", dma_channel_advance(ch));
    for (int i = 1; i < 5; i++) {
        note_ret("read_only", i, dma_channel_read_only(ch));
        note("advance %d: %d", i, dma_channel_advance(ch));
    }
    note_ret("read_only after TC", 0, dma_channel_read_only(ch));
    note_regs(ch);
}

/* The mode the device sees: direction and autoinitialize. */
static void
sc_mode(int ch)
{
    program(ch, 0x58, 0x1000, 1);
    note("dma_mode: %02X", dma_mode(ch) & 0xfc);
    program(ch, 0x44, 0x1000, 1);
    note("dma_mode: %02X", dma_mode(ch) & 0xfc);
}

typedef struct {
    const char *name;
    void (*run)(int ch);
} scenario_t;

static const scenario_t scenarios[] = {
    { "write, single",              sc_write_single },
    { "read, single",               sc_read_single },
    { "read, autoinitialize",       sc_read_autoinit },
    { "read, decrement",            sc_read_decrement },
    { "64K page wrap",              sc_page_wrap },
    { "masks and master clear",     sc_masks },
    { "byte pointer",               sc_flip_flop },
    { "DRQ in the status register", sc_drq_status },
    { "read_only and advance",      sc_read_only_advance },
    { "mode for the device",        sc_mode },
};

/* ---- What the 386EX does differently, as documented ------------------------------------------ */

static int ex_failures;
static int ex_total;

static void
want(int ok, const char *what, int got, int expect)
{
    ex_total++;
    ex_failures += !ok;
    printf("%s 386EX: %s (%X, want %X)\n", ok ? "ok  " : "FAIL", what, got, expect);
}

static uint16_t
reg16(uint16_t port)
{
    uint16_t v;

    outb(P_CLRFF, 0);
    v = inb(port);
    v |= inb(port) << 8;
    return v;
}

/* A verify transfer goes through: the device gets DMA_VERIFY, the channel counts, and TC comes at the end. */
static void
ex_verify(int ch)
{
    int ret[4];

    world_start(WORLD_386EX);
    program(ch, 0x40, 0x67890, 2);
    outb(P_MASK1, ch);
    for (int i = 0; i < 4; i++)
        ret[i] = dma_channel_read(ch);
    want((ret[0] & DMA_VERIFY) && !(ret[0] & DMA_OVER), "verify: first transfer", ret[0], DMA_VERIFY);
    want((ret[2] & DMA_VERIFY) && (ret[2] & DMA_OVER), "verify: TC on the third", ret[2], DMA_VERIFY | DMA_OVER);
    want(ret[3] == DMA_NODATA, "verify: masked after TC", ret[3], DMA_NODATA);
    want(reg16(P_ADDR(ch)) == 0x7893, "verify: address moved on", reg16(P_ADDR(ch)), 0x7893);
    want(reg16(P_COUNT(ch)) == 0xffff, "verify: count expired", reg16(P_COUNT(ch)), 0xffff);
    world_stop(WORLD_386EX);
}

/* DMACLR sets DMAMOD1 back to its reset state, verify, and masks the channel; the address stays. */
static void
ex_master_clear(int ch)
{
    int ret;

    world_start(WORLD_386EX);
    for (int i = 0; i < 8; i++)
        test_ram[0x78900 + i] = 0x90 + i;
    program(ch, 0x48, 0x78900, 7);
    outb(P_MASK1, ch);
    (void) dma_channel_read(ch);
    outb(P_MCLR, 0);
    ret = dma_channel_read(ch);
    want(ret == DMA_NODATA, "master clear: masked", ret, DMA_NODATA);
    outb(P_MASK1, ch);
    ret = dma_channel_read(ch);
    want(ret == DMA_VERIFY, "master clear: DMAMOD1 back to verify", ret, DMA_VERIFY);
    want(reg16(P_ADDR(ch)) == 0x8902, "master clear: address kept", reg16(P_ADDR(ch)), 0x8902);
    world_stop(WORLD_386EX);
}

/* A fly-by software request has no device to take or give the data: nothing moves, but the channel counts to TC. */
static void
ex_flyby_software(int ch)
{
    int     untouched = 1;
    uint8_t status;

    world_start(WORLD_386EX);
    for (int i = 0; i < 16; i++)
        test_ram[0x6b000 + i] = 0x5a;
    program(ch, 0x84, 0x6b004, 3); /* block, write memory; DMAMOD2 is in its reset state, fly-by */
    outb(P_REQUEST, 0x04 | ch);
    run_timers();
    for (int i = 0; i < 16; i++)
        untouched &= (test_ram[0x6b000 + i] == 0x5a);
    want(untouched, "fly-by software request: memory untouched", untouched, 1);
    want(reg16(P_ADDR(ch)) == 0xb008, "fly-by software request: address moved on", reg16(P_ADDR(ch)), 0xb008);
    want(reg16(P_COUNT(ch)) == 0xffff, "fly-by software request: count expired", reg16(P_COUNT(ch)), 0xffff);
    status = inb(P_STATUS);
    want(!!(status & (1 << ch)), "fly-by software request: TC", status, 1 << ch);
    world_stop(WORLD_386EX);
}

int
main(int argc, char **argv)
{
    int verbose  = (argc > 1) && !strcmp(argv[1], "-v");
    int failures = 0;
    int total    = 0;

    for (size_t s = 0; s < sizeof(scenarios) / sizeof(scenarios[0]); s++) {
        for (int ch = 0; ch < 2; ch++) {
            for (int w = 0; w < 2; w++) {
                cur    = &logs[w];
                cur->n = 0;
                world_start((world_t) w);
                scenarios[s].run(ch);
                run_timers();
                world_stop((world_t) w);
            }

            int bad = (logs[0].n != logs[1].n);
            for (int i = 0; !bad && (i < logs[0].n); i++)
                bad = strcmp(logs[0].lines[i], logs[1].lines[i]) != 0;

            total++;
            failures += bad;
            printf("%s %s, channel %d\n", bad ? "FAIL" : "ok  ", scenarios[s].name, ch);
            if (bad || verbose) {
                int n = (logs[0].n > logs[1].n) ? logs[0].n : logs[1].n;
                for (int i = 0; i < n; i++) {
                    const char *a = (i < logs[0].n) ? logs[0].lines[i] : "";
                    const char *b = (i < logs[1].n) ? logs[1].lines[i] : "";
                    printf("  %c %-60s | %s\n", strcmp(a, b) ? '*' : ' ', a, b);
                }
                if (bad)
                    printf("    (left %s, right %s)\n", world_name[0], world_name[1]);
            }
        }
    }

    printf("%d of %d scenarios match\n", total - failures, total);

    for (int ch = 0; ch < 2; ch++) {
        printf("channel %d:\n", ch);
        ex_verify(ch);
        ex_master_clear(ch);
        ex_flyby_software(ch);
    }
    printf("%d of %d 386EX checks pass\n", ex_total - ex_failures, ex_total);

    return (failures || ex_failures) ? 1 : 0;
}
