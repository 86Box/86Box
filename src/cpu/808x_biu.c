/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          808x BIU emulation.
 *
 * Authors: Andrew Jenner, <https://www.reenigne.org>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2015-2020 Andrew Jenner.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/ppi.h>
#include <86box/timer.h>
#include <86box/gdbstub.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#include "808x_biu.h"

#define do_cycle()           wait(1)
#define do_cycle_i()         do_cycle()

uint8_t            biu_preload_byte      = 0x00;

uint16_t           last_addr             = 0x0000;

/* The IP equivalent of the current prefetch queue position. */
uint16_t           pfq_ip                = 0x0000;
uint16_t           pfq_in                = 0x0000;

int                bus_request_type      = 0;
int                in_lock               = 0;
int                pic_data              = -1;
int                biu_queue_preload     = 0;

/* Variables to aid with the prefetch queue operation. */
int                pfq_pos               = 0;
int                pfq_size              = 0;

/* The prefetch queue (4 bytes for 8088, 6 bytes for 8086). */
static uint8_t     pfq[6];

static int         biu_cycles            = 0;
static int         biu_wait              = 0;
static int         biu_wait_length       = 0;
static int         refresh               = 0;
static uint16_t    mem_data              = 0;
static uint32_t    mem_seg               = 0;
static uint16_t    mem_addr              = 0;
static int         biu_state             = 0;
static int         biu_next_state        = 0;
static int         biu_scheduled_state   = 0;
static int         biu_state_length      = 0;
static int         biu_state_total_len   = 0;
static int         dma_state             = 0;
static int         dma_state_length      = 0;
static int         cycdiff               = 0;
static int         wait_states           = 0;
static int         fetch_suspended       = 0;
static int         ready                 = 1;
static int         dma_wait_states       = 0;

#define BUS_CYCLE       (biu_cycles & 3)
#define BUS_CYCLE_T1    biu_cycles = 0
#define BUS_CYCLE_NEXT  biu_cycles = (biu_cycles + 1) & 3

enum {
    BUS_T1 = 0,
    BUS_T2,
    BUS_T3,
    BUS_T4
};

enum {
    BIU_STATE_IDLE,
    BIU_STATE_SUSP,
    BIU_STATE_DELAY,
    BIU_STATE_RESUME,
    BIU_STATE_WAIT,
    BIU_STATE_PF,
    BIU_STATE_EU
};

enum {
    DMA_STATE_IDLE,
    DMA_STATE_TIMER,
    DMA_STATE_DREQ,
    DMA_STATE_HRQ,
    DMA_STATE_HLDA,
    DMA_STATE_OPERATING
};

/* DEBUG stuff. */
const char *lpBiuStates[7] = { "Ti   ", "Ti S ", "Ti D ", "Ti R ", "Tw   ", "T%i PF", "T%i EU" };

#ifdef ENABLE_808X_BIU_LOG
int x808x_biu_do_log = ENABLE_808X_BIU_LOG;

static void
x808x_biu_log(const char *fmt, ...)
{
    va_list ap;

    if (x808x_biu_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define x808x_biu_log(fmt, ...)
#endif

static void pfq_add(void);

static void
pfq_resume(int delay)
{
    biu_state = BIU_STATE_RESUME;
    biu_state_length = delay;
    biu_state_total_len = delay;
}

static void
pfq_switch_to_pf(int delay)
{
    biu_next_state = BIU_STATE_RESUME;
    biu_state_length = delay;
    biu_state_total_len = delay;
}

static void
pfq_schedule(void)
{
    if (biu_state == BIU_STATE_EU) {
        if (!fetch_suspended && (pfq_pos < 4))
            biu_next_state = BIU_STATE_PF;
            // pfq_switch_to_pf(2);
        else
            biu_next_state = BIU_STATE_IDLE;
    } else {
        if (pfq_pos == 3) {
            biu_next_state = BIU_STATE_DELAY;
            biu_state_length = 3;
            biu_state_total_len = 3;
        } else
            biu_next_state = BIU_STATE_PF;
    }
}

void
biu_reset(void)
{
    BUS_CYCLE_T1;
    biu_cycles          = 0;
    biu_wait            = 0;
    refresh             = 0;
    bus_request_type    = 0;
    biu_queue_preload   = 0;
    pic_data            = -1;
    mem_data            = 0;
    mem_seg             = 0;
    mem_addr            = 0;
    wait_states         = 0;
    dma_state           = DMA_STATE_IDLE;
    dma_state_length    = 0;
    biu_state           = BIU_STATE_IDLE;
    biu_next_state      = BIU_STATE_IDLE;
    biu_scheduled_state = BIU_STATE_IDLE;
    biu_state_length    = 0;
    pfq_size            = is8086 ? 6 : 4;
    pfq_in              = 0x0000;
}

void
clock_end(void)
{
    int diff = cycdiff - cycles;

    /* On 808x systems, clock speed is usually crystal frequency divided by an integer. */
    tsc += ((uint64_t) diff * (xt_cpu_multi >> 32ULL)); /* Shift xt_cpu_multi by 32 bits to
                                                           the right and then multiply. */
    if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t) tsc))
        timer_process();
}

void
clock_start(void)
{
    cycdiff = cycles;
}

void
process_timers(void)
{
    clock_end();
    clock_start();
}

static int cycles_ex = 0;

void
cycles_forward(int c)
{
    cycles -= c;

    if (!is286)
        process_timers();

    cycles_ex++;
}

static void
bus_outb(uint16_t port, uint8_t val)
{
    outb(port, val);
}

static void
bus_outw(uint16_t port, uint16_t val)
{
    outw(port, val);
}

static uint8_t
bus_inb(uint16_t port)
{
    uint8_t ret;

    ret = inb(port);

    return ret;
}

static uint16_t
bus_inw(uint16_t port)
{
    uint16_t ret;

    ret = inw(port);

    return ret;
}

static void
bus_do_io(int io_type)
{
    int      old_cycles = cycles;

    x808x_biu_log("(%02X) bus_do_io(%02X): %04X\n", opcode, io_type, cpu_state.eaaddr);

    if (io_type & BUS_OUT) {
        if (io_type & BUS_WIDE)
            bus_outw((uint16_t) cpu_state.eaaddr, AX);
        else if (io_type & BUS_HIGH)
            bus_outb(((uint16_t) cpu_state.eaaddr + 1) & 0xffff, AH);
        else
            bus_outb((uint16_t) cpu_state.eaaddr, AL);
    } else {
        if (io_type & BUS_WIDE)
            AX = bus_inw((uint16_t) cpu_state.eaaddr);
        else if (io_type & BUS_HIGH)
            AH = bus_inb(((uint16_t) cpu_state.eaaddr + 1) & 0xffff);
        else
            AL = bus_inb((uint16_t) cpu_state.eaaddr);
    }

    resub_cycles(old_cycles);
}

static void
bus_writeb(uint32_t seg, uint32_t addr, uint8_t val)
{
    write_mem_b(seg + addr, val);
}

static void
bus_writew(uint32_t seg, uint32_t addr, uint16_t val)
{
    write_mem_w(seg + addr, val);
}

static uint8_t
bus_readb(uint32_t seg, uint32_t addr)
{
    uint8_t ret = read_mem_b(seg + addr);

    return ret;
}

static uint16_t
bus_readw(uint32_t seg, uint32_t addr)
{
    uint16_t ret = read_mem_w(seg + addr);

    return ret;
}

static void
bus_do_mem(int io_type)
{
    int      old_cycles = cycles;

    if (io_type & BUS_OUT) {
        if (io_type & BUS_WIDE)
            bus_writew(mem_seg, (uint32_t) mem_addr, mem_data);
        else if (io_type & BUS_HIGH) {
            if (is186 && !is_nec)
                bus_writeb(mem_seg, ((uint32_t) mem_addr) + 1, mem_data >> 8);
            else
                bus_writeb(mem_seg, (uint32_t) ((mem_addr + 1) & 0xffff), mem_data >> 8);
        } else
            bus_writeb(mem_seg, (uint32_t) mem_addr, mem_data & 0xff);
    } else {
        if (io_type & BUS_WIDE)
            mem_data = bus_readw(mem_seg, (uint32_t) mem_addr);
        else if (io_type & BUS_HIGH) {
            if (is186 && !is_nec)
                mem_data = (mem_data & 0x00ff) |
                           (((uint16_t) bus_readb(mem_seg, ((uint32_t) mem_addr) + 1)) << 8);
            else
                mem_data = (mem_data & 0x00ff) |
                           (((uint16_t) bus_readb(mem_seg, (uint32_t) ((mem_addr + 1) & 0xffff))) << 8);
        } else
            mem_data = (mem_data & 0xff00) | ((uint16_t) bus_readb(mem_seg, (uint32_t) mem_addr));
    }

    resub_cycles(old_cycles);
}

static void
biu_print_cycle(void)
{
    // if ((CS == 0xf000) && (cpu_state.pc >= 0x0101) && (cpu_state.pc < 0xe000))
        // fatal("Fatal!\n");

    if ((CS == DEBUG_SEG) && (cpu_state.pc >= DEBUG_OFF_L) && (cpu_state.pc <= DEBUG_OFF_H)) {
        if (biu_state >= BIU_STATE_PF) {
            if (biu_wait)
                pclog("[%04X:%04X] [%i, %i] (%i) %s\n", CS, cpu_state.pc, dma_state, dma_wait_states,
                      pfq_pos, lpBiuStates[BIU_STATE_WAIT]);
            else {
                char temp[16] = { 0 };

                sprintf(temp, lpBiuStates[biu_state], biu_cycles + 1);
                pclog("[%04X:%04X] [%i, %i] (%i) %s\n", CS, cpu_state.pc, dma_state, dma_wait_states,
                      pfq_pos, temp);
            }
        } else
            pclog("[%04X:%04X] [%i, %i] (%i) %s\n", CS, cpu_state.pc, dma_state, dma_wait_states,
                  pfq_pos, lpBiuStates[biu_state]);
    }
}

static void
do_wait(void)
{
    if (wait_states > 0)
        wait_states--;

    if (dma_wait_states > 0)
        dma_wait_states--;
}

static void
run_dma_cycle(void)
{
    int bus_cycle_check = ((biu_state < BIU_STATE_PF) || (BUS_CYCLE == BUS_T3) ||
                           (BUS_CYCLE == BUS_T4) || biu_wait) && !in_lock;

    switch (dma_state) {
#if 0
        case DMA_STATE_IDLE:
            if (refresh > 0) {
                refresh--;
                dma_state = DMA_STATE_DREQ;
                dma_state_length = 1;
            }
            break;
#endif
        case DMA_STATE_TIMER:
            if ((CS == DEBUG_SEG) && (cpu_state.pc >= DEBUG_OFF_L) && (cpu_state.pc <= DEBUG_OFF_H))
                pclog("DREQ\n");
            dma_state = DMA_STATE_DREQ;
            dma_state_length = 1;
            break;
        case DMA_STATE_DREQ:
            dma_state = DMA_STATE_HRQ;
            dma_state_length = 1;
            break;
        case DMA_STATE_HRQ:
            if (!in_lock && bus_cycle_check) {
                dma_state = DMA_STATE_HLDA;
                dma_state_length = 1;
            }
            break;
        case DMA_STATE_HLDA:
            dma_state = DMA_STATE_OPERATING;
            dma_state_length = 4;
            break;
        case DMA_STATE_OPERATING:
            dma_state_length--;
            if (dma_state_length == 3) {
                dma_wait_states = 7;
                ready = 0;
            } else if (dma_state_length == 0) {
                dma_state = DMA_STATE_IDLE;
                dma_state_length = 1;
            }
            break;
    }
}

static void
biu_cycle_idle(int type)
{
    if ((CS == DEBUG_SEG) && (cpu_state.pc >= DEBUG_OFF_L) && (cpu_state.pc <= DEBUG_OFF_H))
        pclog("[%04X:%04X] [%i, %i] (%i) %s\n", CS, cpu_state.pc, dma_state, dma_wait_states,
              pfq_pos, lpBiuStates[type]);

    run_dma_cycle();
    cycles_forward(1);

    do_wait();
}

/* Reads a byte from the memory but does not advance the BIU. */
static uint8_t
readmembf(uint32_t a)
{
    uint8_t ret;

    a   = cs + (a & 0xffff);
    ret = read_mem_b(a);

    return ret;
}

static uint16_t
readmemwf(uint16_t a)
{
    uint16_t ret;

    ret = read_mem_w(cs + (a & 0xffff));

    return ret;
}

static void
do_bus_access(void)
{
    int io_type = (biu_state == BIU_STATE_EU) ? bus_request_type : BUS_CODE;

    x808x_biu_log("[%04X:%04X] %02X bus access %02X\n", CS, cpu_state.pc, opcode, io_type);

    if (io_type != 0) {
        wait_states = 0;
        switch (io_type & BUS_ACCESS_TYPE) {
            case BUS_CODE:
                // pfq_add();
                if (is8086)
                    pfq_in = readmemwf(pfq_ip);
                else
                    pfq_in = readmembf(pfq_ip);
                break;
            case BUS_IO:
                wait_states++;
                bus_do_io(io_type);
                // if (cpu_state.eaaddr == 0x41)
                    // pclog("I/O port 41h: First Tw: cycles = %i\n", cycles_ex);
                break;
            case BUS_MEM:
                bus_do_mem(io_type);
                break;
            case BUS_PIC:
                pic_data = pic_irq_ack();
                break;
            default:
                break;
        }
    }
}

void
resub_cycles(int old_cycles)
{
    if (old_cycles > cycles)
        wait_states = old_cycles - cycles;

    cycles = old_cycles;
}

static uint8_t
biu_queue_has_room(void)
{
    if (is8086)
        return pfq_pos < 5;
    else
        return pfq_pos < 4;
}

static void
biu_do_cycle(void)
{
    biu_print_cycle();

    switch (biu_state) {
        default:
            fatal("Invalid BIU state: %02X\n", biu_state);
            break;
        case BIU_STATE_RESUME:
            // run_dma_cycle();
            if (biu_state_length > 0) {
                biu_state_length--;
                if (biu_state_length == 0) {
                    biu_state = BIU_STATE_PF;
                    biu_next_state = BIU_STATE_PF;
               }
            } else {
                biu_state = BIU_STATE_PF;
                biu_next_state = BIU_STATE_PF;
            }
            break;
        case BIU_STATE_IDLE:
        case BIU_STATE_SUSP:
            // run_dma_cycle();
            biu_state = biu_next_state;
            break;
        case BIU_STATE_DELAY:
            // run_dma_cycle();
            if (biu_state_length > 0) {
                biu_state_length--;
                if (biu_state_length == 0) {
                    if (biu_queue_has_room()) {
                        biu_state = BIU_STATE_PF;
                        biu_next_state = BIU_STATE_PF;
                    } else {
                        biu_state = BIU_STATE_IDLE;
                        biu_next_state = BIU_STATE_IDLE;
                   }
                }
            } else {
                if (biu_queue_has_room()) {
                    biu_state = BIU_STATE_PF;
                    biu_next_state = BIU_STATE_PF;
                } else {
                    biu_state = BIU_STATE_IDLE;
                    biu_next_state = BIU_STATE_IDLE;
               }
            }
            break;
        case BIU_STATE_PF:
        case BIU_STATE_EU:
            if (biu_wait) {
                // run_dma_cycle();

                if ((wait_states == 0) && (dma_wait_states == 0)) {
                    biu_wait = 0;
                    BUS_CYCLE_NEXT;
                }
            } else {
                // run_dma_cycle();

                if (BUS_CYCLE == BUS_T4) {
                    if (biu_state == BIU_STATE_PF)
                        pfq_add();
                    biu_state = biu_next_state;
                }

                if ((BUS_CYCLE == BUS_T3) && ((wait_states != 0) || (dma_wait_states != 0)))
                    biu_wait = 1;
                else {
                    biu_wait = 0;
                    BUS_CYCLE_NEXT;
                }
            }
            break;
    }
}

static int
biu_is_last_tw(void)
{
    return ((biu_state >= BIU_STATE_PF) && biu_wait && ((wait_states + dma_wait_states) == 1));
}

static void
biu_cycle(void)
{
    if (biu_state >= BIU_STATE_PF) {
        if (BUS_CYCLE == BUS_T2)
            pfq_schedule();
        else if ((BUS_CYCLE == BUS_T3) || biu_is_last_tw())
            do_bus_access();
    }

    run_dma_cycle();

    biu_do_cycle();

    cycles_forward(1);

    do_wait();
}

static void
biu_eu_request(void)
{
    if ((CS == DEBUG_SEG) && (cpu_state.pc >= DEBUG_OFF_L) && (cpu_state.pc <= DEBUG_OFF_H))
        pclog("biu_eu_request()\n");

    switch (biu_state) {
        default:
            fatal("Invalid BIU state: %02X\n", biu_state);
            break;
        case BIU_STATE_RESUME:
            /* Resume it - leftover cycles. */
            for (uint8_t i = 0; i < (biu_state_total_len - biu_state_length); i++)
                biu_cycle_idle(biu_state);
            break;
        case BIU_STATE_IDLE:
        case BIU_STATE_SUSP:
            /* Resume it - 3 cycles. */
            for (uint8_t i = 0; i < 3; i++)
                biu_cycle_idle(biu_state);
            break;
        case BIU_STATE_DELAY:
        case BIU_STATE_EU:
            /* Do the request immediately (needs hardware testing). */
            biu_state_length = 0;
            break;
        case BIU_STATE_PF:
            /* Transition the state. */
            switch (BUS_CYCLE) {
                case BUS_T1:
                case BUS_T2:
                    /* Leftover BIU cycles. */
                    do
                       biu_cycle();
                    while (BUS_CYCLE != BUS_T1); 
                    break;
                case BUS_T3:
                case BUS_T4:
                    /* Leftover BIU cycles. */
                    do
                       biu_cycle();
                    while (BUS_CYCLE != BUS_T1); 
                    /* The two abort cycles. */
                    for (uint8_t i = 0; i < 2; i++)
                        biu_cycle_idle(BIU_STATE_IDLE);
                    break;

                default:
                    break;
            }
            break;
    }

    biu_state        = BIU_STATE_EU;
    biu_next_state   = BIU_STATE_EU;

    biu_state_length = 0;
}

void
wait(int c)
{
    // if ((CS == DEBUG_SEG) && (cpu_state.pc >= DEBUG_OFF_L) && (cpu_state.pc <= DEBUG_OFF_H))
        // pclog("[%02X] wait(%i, %i)\n", opcode, c, bus); 

    if (c < 0)
        pclog("Negative cycles: %i!\n", c);

    x808x_biu_log("[%04X:%04X] %02X %i cycles (%i)\n", CS, cpu_state.pc, opcode, c, bus);

    for (uint8_t i = 0; i < c; i++)
        biu_cycle();
}

/* This is for external subtraction of cycles, ie. wait states. */
void
sub_cycles(int c)
{
    cycles -= c;
}

void
biu_begin_eu(void)
{
    biu_eu_request();
}

static void
biu_wait_for_write_finish(void)
{
    while (BUS_CYCLE != BUS_T4) {
        biu_cycle();
        if (biu_wait_length == 1)
            break;
    }
}

void
biu_wait_for_read_finish(void)
{
    biu_wait_for_write_finish();
    biu_cycle();
}

void
cpu_io(int bits, int out, uint16_t port)
{
    if (out) {
        if (bits == 16) {
            if (is8086 && !(port & 1)) {
                bus_request_type = BUS_IO | BUS_OUT | BUS_WIDE;
                biu_begin_eu();
                biu_wait_for_write_finish();
            } else {
                bus_request_type = BUS_IO | BUS_OUT;
                biu_begin_eu();
                biu_wait_for_write_finish();
                biu_cycle();
                biu_state = BIU_STATE_EU;
                biu_state_length = 0;
                bus_request_type = BUS_IO | BUS_OUT | BUS_HIGH;
                biu_wait_for_write_finish();
            }
        } else {
            bus_request_type = BUS_IO | BUS_OUT;
            biu_begin_eu();
            biu_wait_for_write_finish();
        }
    } else {
        if (bits == 16) {
            if (is8086 && !(port & 1)) {
                bus_request_type = BUS_IO | BUS_WIDE;
                biu_begin_eu();
                biu_wait_for_read_finish();
            } else {
                bus_request_type = BUS_IO;
                biu_begin_eu();
                biu_wait_for_read_finish();
                biu_state = BIU_STATE_EU;
                biu_state_length = 0;
                bus_request_type = BUS_IO | BUS_HIGH;
                biu_wait_for_read_finish();
            }
        } else {
            bus_request_type = BUS_IO;
            biu_begin_eu();
            biu_wait_for_read_finish();
        }
    }

    bus_request_type = 0;
}

/* Reads a byte from the memory and advances the BIU. */
uint8_t
readmemb(uint32_t s, uint16_t a)
{
    uint8_t ret;

    mem_seg          = s;
    mem_addr         = a;
    bus_request_type = BUS_MEM;
    biu_begin_eu();
    biu_wait_for_read_finish();
    ret              = mem_data & 0xff;
    bus_request_type = 0;

    return ret;
}

/* Reads a word from the memory and advances the BIU. */
uint16_t
readmemw(uint32_t s, uint16_t a)
{
    uint16_t ret;

    mem_seg  = s;
    mem_addr = a;
    if (is8086 && !(a & 1)) {
        bus_request_type = BUS_MEM | BUS_WIDE;
        biu_begin_eu();
        biu_wait_for_read_finish();
    } else {
        bus_request_type = BUS_MEM | BUS_HIGH;
        biu_begin_eu();
        biu_wait_for_read_finish();
        biu_state = BIU_STATE_EU;
        biu_state_length = 0;
        bus_request_type = BUS_MEM;
        biu_wait_for_read_finish();
    }
    ret              = mem_data;
    bus_request_type = 0;

    return ret;
}

uint16_t
readmem(uint32_t s)
{
    if (opcode & 1)
        return readmemw(s, cpu_state.eaaddr);
    else
        return (uint16_t) readmemb(s, cpu_state.eaaddr);
}

uint32_t
readmeml(uint32_t s, uint16_t a)
{
    uint32_t temp;

    temp = (uint32_t) (readmemw(s, a + 2)) << 16;
    temp |= readmemw(s, a);

    return temp;
}

uint64_t
readmemq(uint32_t s, uint16_t a)
{
    uint64_t temp;

    temp = (uint64_t) (readmeml(s, a + 4)) << 32;
    temp |= readmeml(s, a);

    return temp;
}

/* Writes a byte to the memory and advances the BIU. */
void
writememb(uint32_t s, uint32_t a, uint8_t v)
{
    uint32_t addr = s + a;

    // if (CS == DEBUG_SEG)
        // fatal("writememb(%08X, %08X, %02X)\n", s, a, v);

    mem_seg          = s;
    mem_addr         = a;
    mem_data         = v;
    bus_request_type = BUS_MEM | BUS_OUT;
    biu_begin_eu();
    biu_wait_for_write_finish();
    bus_request_type = 0;

    if ((addr >= 0xf0000) && (addr <= 0xfffff))
        last_addr = addr & 0xffff;
}

/* Writes a word to the memory and advances the BIU. */
void
writememw(uint32_t s, uint32_t a, uint16_t v)
{
    uint32_t addr = s + a;

    // if ((CS == 0xf000) && (cpu_state.pc == 0xf176))
        // pclog("writememw(%08X, %08X, %04X): begin\n", s, a, v);

    mem_seg  = s;
    mem_addr = a;
    mem_data = v;
    if (is8086 && !(a & 1)) {
        bus_request_type = BUS_MEM | BUS_OUT | BUS_WIDE;
        biu_begin_eu();
        biu_wait_for_write_finish();
    } else {
        bus_request_type = BUS_MEM | BUS_OUT | BUS_HIGH;
        biu_begin_eu();
        biu_wait_for_write_finish();
        biu_cycle();
        biu_state = BIU_STATE_EU;
        biu_state_length = 0;
        bus_request_type = BUS_MEM | BUS_OUT;
        biu_wait_for_write_finish();
    }
    bus_request_type = 0;

    if ((addr >= 0xf0000) && (addr <= 0xfffff))
        last_addr = addr & 0xffff;
}

void
writemem(uint32_t s, uint16_t v)
{
    if (opcode & 1)
        writememw(s, cpu_state.eaaddr, v);
    else
        writememb(s, cpu_state.eaaddr, (uint8_t) (v & 0xff));
}

void
writememl(uint32_t s, uint32_t a, uint32_t v)
{
    writememw(s, a, v & 0xffff);
    writememw(s, a + 2, v >> 16);
}

void
writememq(uint32_t s, uint32_t a, uint64_t v)
{
    writememl(s, a, v & 0xffffffff);
    writememl(s, a + 4, v >> 32);
}

static void
pfq_write(void)
{
    uint16_t tempw;
    /* Byte fetch on odd addres on 8086 to simulate the HL toggle. */
    int fetch_word = is8086 && !(pfq_ip & 1);

    if (fetch_word && (pfq_pos < (pfq_size - 1))) {
        /* The 8086 fetches 2 bytes at a time, and only if there's at least 2 bytes
           free in the queue. */
        // tempw                         = readmemwf(pfq_ip);
        tempw                         = pfq_in;
        *(uint16_t *) &(pfq[pfq_pos]) = tempw;
        pfq_ip                        = (pfq_ip + 2) & 0xffff;
        pfq_pos += 2;
    } else if (!fetch_word && (pfq_pos < pfq_size)) {
        /* The 8088 fetches 1 byte at a time, and only if there's at least 1 byte
           free in the queue. */
        if (pfq_pos >= 0)
            pfq[pfq_pos] = pfq_in & 0xff;
            // pfq[pfq_pos] = readmembf(pfq_ip);
        pfq_ip       = (pfq_ip + 1) & 0xffff;
        pfq_pos++;
    }

    if (pfq_pos >= pfq_size)
        pfq_pos = pfq_size;
}

uint8_t
pfq_read(void)
{
    uint8_t temp;

    temp = pfq[0];
    for (int i = 0; i < (pfq_size - 1); i++)
        pfq[i] = pfq[i + 1];
    pfq_pos--;
    if (pfq_pos < 0)
        pfq_pos = 0;
    cpu_state.pc = (cpu_state.pc + 1) & 0xffff;
    return temp;
}

void
biu_resume_on_queue_read(void)
{
    // pclog("biu_resume_on_queue_read(%i, %i)\n", pfq_pos, biu_state);
    if ((biu_next_state == BIU_STATE_IDLE) && (pfq_pos == 3))
        pfq_switch_to_pf(3);
}

/* Fetches a byte from the prefetch queue, or from memory if the queue has
   been drained.

   Cycles: 1                         If fetching from the queue;
           (4 - (biu_cycles & 3))    If fetching from the bus - fetch into the queue;
           1                         If fetching from the bus - delay. */
uint8_t
pfq_fetchb_common(void)
{
    uint8_t temp;

    // pclog("pfq_fetch_common(%i, %i, %i)\n", biu_queue_preload, pfq_pos, biu_state);

    if (biu_queue_preload) {
        if (nx)
            nx = 0;

        biu_queue_preload = 0;
        return biu_preload_byte;
    }

    if (pfq_pos > 0) {
        if (biu_state == BIU_STATE_DELAY) {
            while (biu_state == BIU_STATE_DELAY)
                biu_cycle();
        }

        temp = pfq_read();
        biu_resume_on_queue_read();
    } else {
        /* Fill the queue. */
        while (pfq_pos == 0)
            biu_cycle();

        /* Fetch. */
        temp = pfq_read();
    }

    do_cycle();
    return temp;
}

/* The timings are above. */
uint8_t
pfq_fetchb(void)
{
    uint8_t ret;

    ret = pfq_fetchb_common();
    return ret;
}

/* Fetches a word from the prefetch queue, or from memory if the queue has
   been drained. */
uint16_t
pfq_fetchw(void)
{
    uint16_t temp;

    temp = pfq_fetchb_common();
    temp |= (pfq_fetchb_common() << 8);

    return temp;
}

uint16_t
pfq_fetch(void)
{
    if (opcode & 1)
        return pfq_fetchw();
    else
        return (uint16_t) pfq_fetchb();
}

/* Adds bytes to the prefetch queue based on the instruction's cycle count. */
static void
pfq_add(void)
{
    if ((biu_state == BIU_STATE_PF) && (pfq_pos < pfq_size))
        pfq_write();
}

void
biu_update_pc(void)
{
    pfq_ip = cpu_state.pc;
    biu_queue_preload = 0;
}

/* Clear the prefetch queue - called on reset and on anything that affects either CS or IP. */
void
biu_queue_flush(void)
{
    pfq_pos        = 0;
    biu_update_pc();

    fetch_suspended = 0;

    /* FLUSH command. */
    if ((biu_state == BIU_STATE_SUSP) || (biu_state == BIU_STATE_IDLE))
        pfq_resume(3);
}

static void
biu_bus_wait_finish(void)
{
    while (BUS_CYCLE != BUS_T4)
        biu_cycle();
}

void
biu_suspend_fetch(void)
{
    biu_state_length = 0;
    fetch_suspended = 1;

    if (biu_state == BIU_STATE_PF) {
        biu_bus_wait_finish();
        biu_cycle();
        biu_state = BIU_STATE_IDLE;
        biu_next_state = BIU_STATE_IDLE;
    } else {
        if (biu_state == BIU_STATE_EU)
            BUS_CYCLE_T1;
        biu_state = BIU_STATE_IDLE;
        biu_next_state = BIU_STATE_IDLE;
    }
}

/* Memory refresh read - called by reads and writes on DMA channel 0. */
void
refreshread(void)
{
    // refresh++;
    if (dma_state == DMA_STATE_IDLE) {
        dma_state = DMA_STATE_TIMER;
        dma_state_length = 1;
    }
}
