/*
 * 808x EU/BIU execution-flow model
 *
 * This is a ground-up replacement for the original standalone timing probe.
 * The externally visible bus follows Intel's T1/T2/T3/Tw/T4 description,
 * while the overlapped address pipeline (Tr/Ts/T0), queue policy, fetch aborts,
 * SUSP/CORR/FLUSH flow, RNI preload, and EU bus arbitration follow the model
 * used by MartyPC's cpu_808x core.
 *
 * MartyPC is MIT-licensed:
 *   https://github.com/dbalsom/martypc
 * Copyright 2022-2026 Daniel Balsom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * This file remains a self-contained validation core, but now decodes and
 * executes the complete 8086/8088 primary opcode map, including the original
 * group encodings and documented/undocumented aliases. Instruction bodies are
 * expressed as queue reads, EU cycles, microcode-line cycles, and BIU accesses;
 * there is no aggregate fallback timing path.
 *
 * Scope boundary: this models CPU execution/bus flow, not board-level analog
 * behavior. 86Box's existing 8087 operation tables remain authoritative for
 * ESC instructions and are called through the companion bridge in 808x.c.
 * XT refresh requests enter a clocked DREQ/HRQ/HOLDA/AEN/DACK scheduler.
 * Other 86Box DMA peripherals retain the existing synchronous device API and
 * are intentionally not misrepresented as pin-complete 8237 transfers.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/gdbstub.h>
#include <86box/io.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/timer.h>
#include <86box/video.h>

#include "cpu.h"
#include "x86.h"

#include "808x_marty_86box.h"

/* cpu.h exposes the architectural registers as preprocessor aliases.  This
 * translation unit uses the same short names for the private core state. */
#undef AX
#undef AL
#undef AH
#undef CX
#undef CL
#undef CH
#undef DX
#undef DL
#undef DH
#undef BX
#undef BL
#undef BH
#undef SP
#undef BP
#undef SI
#undef DI
#undef cycles

#define PFQ_MAX  6u
#define MC_NONE  0xffffu
#define MC_JUMP  0xfffeu
#define MC_CORR  0xfffdu
#define MC_RNI   0xfffcu

#ifndef ARRAY_LEN
# define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* C/P/A/Z/T/I/D/V are supplied by cpu.h.  86Box calls the sign bit N_FLAG. */
#define S_FLAG N_FLAG

/* Prefix bits. */
#define PFX_LOCK  0x01u
#define PFX_REPNE 0x02u
#define PFX_REPE  0x04u
#define PFX_SEG   0x08u

typedef enum {
    SEG_ES = 0,
    SEG_CS,
    SEG_SS,
    SEG_DS,
    SEG_NONE
} m808x_segment_t;

typedef enum {
    BUS_PASSIVE = 0,
    BUS_CODE,
    BUS_IOR,
    BUS_IOW,
    BUS_MEMR,
    BUS_MEMW,
    BUS_INTA,
    BUS_HALT
} m808x_bus_status_t;

typedef enum {
    T_INIT = 0,
    T_I,
    T_1,
    T_2,
    T_3,
    T_W,
    T_4
} m808x_t_cycle_t;

typedef enum {
    TA_DONE = 0,
    TA_TR,
    TA_TS,
    TA_T0,
    TA_ABORT
} m808x_ta_cycle_t;

typedef enum {
    FETCH_NORMAL = 0,
    FETCH_PAUSED_FULL,
    FETCH_DELAYED,
    FETCH_SUSPENDED,
    FETCH_HALTED
} m808x_fetch_kind_t;

typedef enum {
    PENDING_NONE = 0,
    PENDING_EU_EARLY,
    PENDING_EU_LATE
} m808x_pending_t;

typedef enum {
    XFER_BYTE = 1,
    XFER_WORD = 2
} m808x_transfer_size_t;

typedef enum {
    OPERAND_8 = 1,
    OPERAND_16 = 2
} m808x_operand_size_t;

typedef enum {
    QOP_IDLE = 0,
    QOP_FIRST,
    QOP_SUBSEQUENT,
    QOP_FLUSH
} m808x_queue_op_t;

typedef union {
    uint16_t x;
    struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint8_t l;
        uint8_t h;
#else
        uint8_t h;
        uint8_t l;
#endif
    } b;
} m808x_reg_t;

typedef struct {
    uint8_t data[PFQ_MAX];
    unsigned head;
    unsigned len;
    unsigned capacity;
    unsigned policy_len;
    bool preload_valid;
    uint8_t preload_byte;
    int was_read;
    m808x_queue_op_t op;
    m808x_queue_op_t last_op;
} m808x_pfq_t;

typedef struct {
    m808x_fetch_kind_t kind;
    unsigned delay;
} m808x_fetch_state_t;

typedef struct {
    m808x_t_cycle_t t_cycle;
    m808x_ta_cycle_t ta_cycle;

    m808x_bus_status_t bus_status;       /* pin-visible status; passive from T3 */
    m808x_bus_status_t bus_status_latch; /* operation retained through T4 */
    m808x_bus_status_t pl_status;        /* address-pipeline status */
    m808x_pending_t pending;
    m808x_fetch_state_t fetch;

    m808x_segment_t bus_segment;
    uint32_t address_bus;
    uint32_t address_latch;
    uint16_t data_bus;

    m808x_transfer_size_t transfer_size;
    m808x_operand_size_t operand_size;
    unsigned transfer_n;
    bool final_transfer;
    bool bhe;
    bool ale;
    bool transfer_done;
    unsigned precharged_wait;
    unsigned wait_remaining;

    m808x_pfq_t queue;
} m808x_biu_t;

typedef struct {
    uint8_t opcode;
    uint8_t prefixes;
    m808x_segment_t segment_override;
    bool has_segment_override;
    bool has_modrm;
    uint8_t modrm;
    uint8_t mod;
    uint8_t reg;
    uint8_t rm;
    uint16_t ea;
    m808x_segment_t ea_segment;
    uint16_t instruction_ip;
} m808x_instruction_t;

typedef struct {
    bool is_8086;
    bool trace;
    bool stop_on_halt;
    bool fatal;
    bool halted;
    bool waiting;
    bool test_pin;
    bool intr_pin;
    bool nmi_pin;
    uint8_t interrupt_vector;
    unsigned interrupt_shadow;
    unsigned trap_shadow;
    unsigned trap_disable_delay;

    uint64_t cycle_num;
    uint64_t instruction_count;
    uint64_t cycle_limit;
    unsigned configured_wait_states;

    uint16_t flags;
    uint16_t segs[4];
    m808x_reg_t regs[8];
    uint16_t pc; /* next instruction byte to fetch, not architectural IP */

    uint16_t ea_addr;
    m808x_segment_t ea_seg;
    uint16_t cpu_data;

    bool nx;
    bool rni;
    bool in_lock;
    uint8_t rep_prefix;

    uint16_t mc_line;
    const char *trace_comment;

    m808x_instruction_t ins;
    m808x_biu_t biu;
} m808x_cpu_t;

static m808x_cpu_t m808x_cpu;

void m808x_86box_export_arch_state(const m808x_cpu_t *icpu);
void m808x_86box_import_register_state(m808x_cpu_t *icpu);

#define AX icpu->regs[0].x
#define AL icpu->regs[0].b.l
#define AH icpu->regs[0].b.h
#define CX icpu->regs[1].x
#define CL icpu->regs[1].b.l
#define CH icpu->regs[1].b.h
#define DX icpu->regs[2].x
#define DL icpu->regs[2].b.l
#define DH icpu->regs[2].b.h
#define BX icpu->regs[3].x
#define BL icpu->regs[3].b.l
#define BH icpu->regs[3].b.h
#define SP icpu->regs[4].x
#define BP icpu->regs[5].x
#define SI icpu->regs[6].x
#define DI icpu->regs[7].x

static const char *const segment_name[] = {"ES", "CS", "SS", "DS", "--"};
static const char *const bus_name[] = {"PASV", "CODE", "IOR ", "IOW ", "MEMR", "MEMW", "INTA", "HALT"};
static const char *const t_name[] = {"T*", "Ti", "T1", "T2", "T3", "Tw", "T4"};
static const char *const ta_name[] = {"Td", "Tr", "Ts", "T0", "Ta"};
static const char *const qop_name[] = {"--", "F ", "S ", "FL"};

static void    m808x_host_cycle(void);

/* NOCONA_M808X_DMA_READY_CONSOLIDATED_V3
 *
 * The PC/XT motherboard does not stop the 8088 BIU through its HOLD input.
 * Board logic grants the 8237 independently and stalls an overlapping CPU bus
 * cycle through READY/DMAWAIT. Therefore HRQ/HOLDA must never suppress an
 * internal BIU request; the request is allowed to enter T1/T2/T3 and then wait
 * for READY exactly as the processor would on the motherboard.
 */
bool m808x_86box_dma_try_request_ex(unsigned wait_clocks,
                                    void (*ack_callback)(void *opaque),
                                    void *opaque);
bool m808x_86box_dma_cancel_request_ex(void (*ack_callback)(void *opaque),
                                       void *opaque);
uint64_t m808x_86box_cycle_number(void);

/* 86Box device handlers historically observe the cycle counter after the
 * complete four-clock access has been charged.  The pin-level core invokes
 * the data callback at the end of T3/final Tw, two clocks earlier than that
 * legacy observation point.  Present the established phase to contention
 * models while converting any cycle deduction into actual Tw states. */
#define M808X_HOST_CALLBACK_PHASE_BIAS 2
static bool m808x_in_host_bus_callback = false;
/* Only the legacy cycle-budget view is biased for compatibility with
 * contention handlers.  TSC and timers remain at the physical T3/final-Tw
 * transfer edge; advancing them here would deliver PIT/DMA edges early. */
#ifdef M808X_86BOX_TESTING
static uint64_t m808x_test_captured_wait_clocks = 0;
static uint64_t m808x_test_tw_clocks = 0;
static void (*m808x_test_cycle_hook)(const m808x_cpu_t *cpu) = NULL;
#endif
static bool m808x_host_override_vector(uint8_t vector, uint16_t *ip, uint16_t *segp);

static uint32_t
linear_address(const uint16_t seg, const uint16_t off)
{
    return ((((uint32_t)seg << 4) + off) & 0xfffffu);
}

static bool
bus_is_active(const m808x_bus_status_t status)
{
    return status != BUS_PASSIVE && status != BUS_HALT;
}

static bool
fetch_enabled(const m808x_cpu_t *icpu)
{
    return icpu->biu.fetch.kind != FETCH_SUSPENDED &&
           icpu->biu.fetch.kind != FETCH_HALTED;
}

static unsigned
queue_effective_len(const m808x_pfq_t *q)
{
    return q->len + (q->preload_valid ? 1u : 0u);
}

static void
queue_reset(m808x_pfq_t *q, const bool is_8086)
{
    memset(q, 0, sizeof(*q));
    q->capacity = is_8086 ? 6u : 4u;
    q->policy_len = is_8086 ? 4u : 3u;
    q->was_read = -1;
}

static bool
queue_push(m808x_pfq_t *q, const uint8_t value)
{
    if (q->len >= q->capacity)
        return false;

    q->data[(q->head + q->len) % PFQ_MAX] = value;
    q->len++;
    return true;
}

static uint8_t
queue_pop(m808x_pfq_t *q)
{
    assert(q->len > 0u);
    const uint8_t value = q->data[q->head];
    q->head = (q->head + 1u) % PFQ_MAX;
    q->len--;
    return value;
}

static void
queue_flush(m808x_pfq_t *q)
{
    q->head = 0u;
    q->len = 0u;
    q->preload_valid = false;
    q->op = QOP_FLUSH;
}

static bool
queue_has_room_for_fetch(const m808x_cpu_t *icpu)
{
    /* Reserve the physical fetch width, not merely the number of bytes that
     * an odd-address 8086 fetch will eventually enqueue. */
    return icpu->is_8086 ? icpu->biu.queue.len <= 4u
                        : icpu->biu.queue.len <= 3u;
}

static bool
queue_at_policy_len(const m808x_cpu_t *icpu)
{
    const m808x_pfq_t *q = &icpu->biu.queue;

    if (!icpu->is_8086)
        return q->len == 3u;

    return q->len == 3u || q->len == 4u;
}

static bool
queue_at_policy_threshold_before_read(const m808x_cpu_t *icpu)
{
    (void)icpu;
    return icpu->biu.queue.len == 3u;
}

static uint16_t
architectural_ip(const m808x_cpu_t *icpu)
{
    return (uint16_t)(icpu->pc - queue_effective_len(&icpu->biu.queue));
}

static char
hex_digit(unsigned n)
{
    return (char)(n < 10u ? ('0' + n) : ('A' + (n - 10u)));
}

static void
format_queue(const m808x_pfq_t *q, char *out, const size_t out_size)
{
    const size_t chars = q->capacity * 2u;

    if (out_size < chars + 1u) {
        if (out_size != 0u) {
            out[0] = '\0';
        }
        return;
    }

    memset(out, ' ', chars);
    out[chars] = '\0';

    for (unsigned i = 0; i < q->len; i++) {
        const uint8_t value = q->data[(q->head + i) % PFQ_MAX];
        out[i * 2u] = hex_digit(value >> 4);
        out[i * 2u + 1u] = hex_digit(value & 0x0fu);
    }
}

static void
trace_cycle(m808x_cpu_t *icpu)
{
    if (!icpu->trace) {
        icpu->trace_comment = NULL;
        return;
    }

    const m808x_biu_t *b = &icpu->biu;
    char queue[PFQ_MAX * 2u + 1u];
    char data[5] = "    ";
    const bool transfer = (b->t_cycle == T_3 || b->t_cycle == T_W) &&
                          bus_is_active(b->bus_status_latch);
    format_queue(&b->queue, queue, sizeof(queue));

    if (transfer) {
        data[0] = hex_digit((b->data_bus >> 12) & 0x0fu);
        data[1] = hex_digit((b->data_bus >> 8) & 0x0fu);
        data[2] = hex_digit((b->data_bus >> 4) & 0x0fu);
        data[3] = hex_digit(b->data_bus & 0x0fu);
    }

    const char *rw = "   ";
    switch (b->bus_status_latch) {
        case BUS_MEMR:
        case BUS_IOR:
        case BUS_CODE:
        case BUS_INTA:
            rw = "r->";
            break;
        case BUS_MEMW:
        case BUS_IOW:
            rw = "<-w";
            break;
        default:
            break;
    }

    printf("%7" PRIu64 " | %c | %05" PRIX32 " | %s | %-4s | %-2s | %-2s | %-3s | %s | %-12s | %-2s | ",
           icpu->cycle_num,
           b->ale ? 'A' : '-',
           b->address_latch & 0xfffffu,
           segment_name[b->bus_segment],
           bus_name[b->bus_status_latch],
           t_name[b->t_cycle],
           ta_name[b->ta_cycle],
           rw,
           data,
           queue,
           qop_name[b->queue.op]);

    if (icpu->mc_line == MC_NONE) {
        printf("----");
    } else if (icpu->mc_line == MC_JUMP) {
        printf("JUMP");
    } else if (icpu->mc_line == MC_CORR) {
        printf("CORR");
    } else if (icpu->mc_line == MC_RNI) {
        printf("RNI ");
    } else {
        printf("%03X ", icpu->mc_line);
    }

    if (b->queue.was_read >= 0) {
        printf(" | q->%02X", (unsigned)b->queue.was_read);
        icpu->biu.queue.was_read = -1;
    } else {
        printf(" |      ");
    }
    if (icpu->trace_comment != NULL) {
        printf(" | %s", icpu->trace_comment);
    }
    putchar('\n');
    icpu->trace_comment = NULL;
}

static void biu_make_fetch_decision(m808x_cpu_t *icpu);

static void biu_fetch_start(m808x_cpu_t *icpu);
static void biu_bus_begin_fetch(m808x_cpu_t *icpu);

static void
biu_address_start(m808x_cpu_t *icpu, const m808x_bus_status_t status)
{
    m808x_biu_t *b = &icpu->biu;

    b->ta_cycle = (b->ta_cycle == TA_ABORT) ? TA_TS : TA_TR;
    b->pl_status = status;
}

static void
biu_fetch_abort(m808x_cpu_t *icpu)
{
    icpu->trace_comment = "ABORT";
    icpu->biu.ta_cycle = TA_ABORT;
}

static void
biu_fetch_start(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    if ((b->pending == PENDING_EU_EARLY) || (b->pl_status == BUS_CODE))
        return;

    if (b->fetch.kind == FETCH_DELAYED)
        return;

    b->fetch.kind = FETCH_NORMAL;
    biu_address_start(icpu, BUS_CODE);
}

static void
biu_make_fetch_decision(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    if (!queue_has_room_for_fetch(icpu)) {
        b->fetch.kind = FETCH_PAUSED_FULL;
        return;
    }

    if ((b->pending == PENDING_EU_EARLY) || !fetch_enabled(icpu))
        return;

    if (queue_at_policy_len(icpu) && b->bus_status_latch == BUS_CODE) {
        if (b->ta_cycle == TA_DONE) {
            b->fetch.kind = FETCH_DELAYED;
            b->fetch.delay = 3u;
        }
    } else if (b->ta_cycle == TA_DONE)
        biu_fetch_start(icpu);
}

static void
biu_fetch_on_queue_read(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    if (b->bus_status == BUS_PASSIVE && queue_has_room_for_fetch(icpu)) {
        if (b->fetch.kind == FETCH_SUSPENDED || b->fetch.kind == FETCH_PAUSED_FULL) {
            if (b->t_cycle == T_I || b->fetch.kind == FETCH_SUSPENDED) {
                b->ta_cycle = TA_DONE;
                biu_fetch_start(icpu);
            }
        }
    }
}

static void
biu_do_bus_transfer(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;
    const uint32_t addr = b->address_latch & 0xfffffu;

    in_lock = icpu->in_lock ? 1 : 0;

    if (b->transfer_done)
        return;

    /* A large amount of 86Box hardware, notably IBM CGA, expresses READY
     * stretching by subtracting directly from the global cycle counter in
     * its memory callback.  Capture that delta and restore the scheduler
     * budget: the same number of clocks are then emitted below as physical
     * Tw states.  This also catches handlers that call sub_cycles(). */
    const int host_cycles_before = cpu_state._cycles;
    cpu_state._cycles -= M808X_HOST_CALLBACK_PHASE_BIAS;

    m808x_in_host_bus_callback = true;

    switch (b->bus_status_latch) {
        case BUS_CODE:
            if (b->transfer_size == XFER_WORD) {
                const uint32_t even = addr & ~1u;
                b->data_bus = read_mem_w(even);
            } else
                b->data_bus = read_mem_b(addr);
            break;

        case BUS_MEMR:
            if (b->transfer_size == XFER_WORD)
                b->data_bus = read_mem_w(addr & ~1u);
            else {
                const uint8_t byte = read_mem_b(addr);
                b->data_bus = b->bhe ? (uint16_t)((uint16_t)byte << 8) : (uint16_t)byte;
            }
            break;

        case BUS_MEMW:
            if (b->transfer_size == XFER_WORD)
                write_mem_w(addr, b->data_bus);
            else
                write_mem_b(addr, b->bhe ? (uint8_t)(b->data_bus >> 8) : (uint8_t)b->data_bus);
            if (addr >= 0xf0000u && addr <= 0xfffffu)
                last_addr = (uint16_t)addr;
            break;

        case BUS_IOR:
            if (b->transfer_size == XFER_WORD)
                b->data_bus = inw((uint16_t)addr);
            else {
                /*
                 * NOCONA_8086_IO_LANE_FIX_V1
                 *
                 * On an 8086, an odd-address byte transfer uses D8-D15 with
                 * BHE asserted. Keep the internal pin-level data bus on that
                 * lane, just as the memory path already does.
                 */
                const uint8_t byte = inb((uint16_t)addr);
                b->data_bus = b->bhe ? (uint16_t)((uint16_t)byte << 8)
                                     : (uint16_t)byte;
            }
            break;

        case BUS_IOW:
            if (b->transfer_size == XFER_WORD)
                outw((uint16_t)addr, b->data_bus);
            else
                outb((uint16_t)addr,
                     b->bhe ? (uint8_t)(b->data_bus >> 8)
                            : (uint8_t)b->data_bus);
            break;

        case BUS_INTA:
            /* 808x performs two physical acknowledge cycles.  The second
             * return value is the vector used by the interrupt microflow. */
            icpu->interrupt_vector = (uint8_t)pic_irq_ack();
            b->data_bus = icpu->interrupt_vector;
            break;

        case BUS_PASSIVE:
        case BUS_HALT:
            break;
    }

    m808x_in_host_bus_callback = false;
    const int charged_from_phase = host_cycles_before - M808X_HOST_CALLBACK_PHASE_BIAS;
    const int charged_clocks = charged_from_phase - cpu_state._cycles;
    cpu_state._cycles = host_cycles_before;

    if (charged_clocks > 0) {
        unsigned charge = (unsigned)charged_clocks;

        /* A video provider may have supplied this device delay
         * in T2.  Legacy video handlers may still subtract the same delay
         * from `cycles`; remove
         * only that duplicate and preserve any additional device charge. */
        if (b->precharged_wait) {
            const unsigned duplicate = (charge < b->precharged_wait) ?
                                       charge : b->precharged_wait;
            charge -= duplicate;
        }

#ifdef M808X_86BOX_TESTING
        m808x_test_captured_wait_clocks += charge;
#endif
        if (charge) {
            if (UINT_MAX - b->wait_remaining < charge) {
                icpu->fatal = true;
                return;
            }
            b->wait_remaining += charge;
        }
    }
    b->precharged_wait = 0;
    b->transfer_done = true;
}

static void
biu_finish_code_fetch(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;
    m808x_pfq_t *q = &b->queue;

    if (b->transfer_size == XFER_BYTE) {
        if (queue_push(q, (uint8_t)b->data_bus)) {
            icpu->pc = (uint16_t)(icpu->pc + 1u);
        }
        return;
    }

    if (icpu->pc & 1u) {
        if (queue_push(q, (uint8_t)(b->data_bus >> 8))) {
            icpu->pc = (uint16_t)(icpu->pc + 1u);
        }
    } else {
        unsigned pushed = 0u;
        if (queue_push(q, (uint8_t)b->data_bus)) {
            pushed++;
        }
        if (queue_push(q, (uint8_t)(b->data_bus >> 8))) {
            pushed++;
        }
        icpu->pc = (uint16_t)(icpu->pc + pushed);
    }
}

static void
biu_bus_begin_fetch(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    /* The TA_T0 caller also keeps the pipeline parked while this is true. */

    if (!queue_has_room_for_fetch(icpu)) {
        b->fetch.kind = FETCH_PAUSED_FULL;
        return;
    }

    b->fetch.kind = FETCH_NORMAL;
    b->pl_status = BUS_PASSIVE;
    b->bus_status = BUS_CODE;
    b->bus_status_latch = BUS_CODE;
    b->bus_segment = SEG_CS;
    b->t_cycle = T_INIT;
    b->address_bus = linear_address(icpu->segs[SEG_CS], icpu->pc);
    b->address_latch = b->address_bus;
    b->ale = true;
    b->data_bus = 0u;
    b->transfer_size = icpu->is_8086 ? XFER_WORD : XFER_BYTE;
    b->operand_size = icpu->is_8086 ? OPERAND_16 : OPERAND_8;
    b->transfer_n = 1u;
    b->final_transfer = true;
    b->wait_remaining = 0u;
    b->transfer_done = false;
    b->bhe = icpu->is_8086 && ((icpu->pc & 1u) != 0u);
}

static void
biu_bus_end(m808x_cpu_t *icpu)
{
    icpu->biu.ale = false;
}

/* IBM PC/XT DRAM refresh bus arbitration.  This is the measured 8088/8237
 * handshake used by MartyPC: DREQ -> HRQ -> HOLDA -> five controller
 * operating clocks, with READY stretched for six effective clocks.  The EU
 * may continue while the BIU is not requesting the bus; an active bus cycle
 * is stretched through Tw exactly as on hardware. */
typedef enum m808x_dma_state_t {
    M808X_DMA_IDLE = 0,
    M808X_DMA_DREQ,
    M808X_DMA_HRQ,
    M808X_DMA_HOLDA,
    M808X_DMA_OPERATING
} m808x_dma_state_t;

static m808x_dma_state_t m808x_dma_state = M808X_DMA_IDLE;
static bool m808x_dma_req = false;
static bool m808x_dma_holda = false;
static bool m808x_dma_ack = false;
static bool m808x_dma_aen = false;
static unsigned m808x_dma_operating_cycle = 0u;
static unsigned m808x_dma_wait_states = 0u;
static unsigned m808x_dma_wait_target = 4u;
typedef void (*m808x_dma_ack_callback_t)(void *opaque);
static m808x_dma_ack_callback_t m808x_dma_ack_callback = NULL;
static void *m808x_dma_ack_opaque = NULL;

static void
m808x_dma_tick(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    switch (m808x_dma_state) {
        case M808X_DMA_IDLE:
            if (m808x_dma_req)
                m808x_dma_state = M808X_DMA_DREQ;
            break;

        case M808X_DMA_DREQ:
            /* The 8237 raises HRQ one controller clock after observing DREQ. */
            m808x_dma_state = M808X_DMA_HRQ;
            break;

        case M808X_DMA_HRQ:
            /* The PC motherboard generates HOLDA when the bus status is
             * passive (S0=S1=1), or late enough in the current transfer that
             * it can complete normally. LOCK suppresses the grant. */
            if (!icpu->in_lock &&
                ((b->bus_status == BUS_PASSIVE) || (b->t_cycle == T_3) ||
                 (b->t_cycle == T_W) || (b->t_cycle == T_4))) {
                m808x_dma_holda = true;
                m808x_dma_state = M808X_DMA_HOLDA;
            }
            break;

        case M808X_DMA_HOLDA:
            /* One clock of hold acknowledge precedes 8237 S1/AEN. */
            if (b->wait_remaining < 2u) {
                m808x_dma_aen = true;
                m808x_dma_operating_cycle = 0u;
                m808x_dma_state = M808X_DMA_OPERATING;
            }
            break;

        case M808X_DMA_OPERATING:
            ++m808x_dma_operating_cycle;
            switch (m808x_dma_operating_cycle) {
                case 1u:
                    /* DMAWAIT after S1.  Seven is decremented at the end of
                     * this clock, leaving six effective READY-low clocks. */
                    m808x_dma_wait_states = m808x_dma_wait_target + 1u;
                    break;
                case 2u:
                    /* DACK is asserted after S2.  Execute the controller's
                     * transfer callback at the same pin phase, not at the PIT
                     * edge that originally raised DREQ. */
                    m808x_dma_req = false;
                    m808x_dma_ack = true;
                    if (m808x_dma_ack_callback != NULL) {
                        m808x_dma_ack_callback_t callback = m808x_dma_ack_callback;
                        void *opaque = m808x_dma_ack_opaque;
                        m808x_dma_ack_callback = NULL;
                        m808x_dma_ack_opaque = NULL;
                        callback(opaque);
                    }
                    break;
                case 4u:
                    m808x_dma_holda = false;
                    break;
                case 5u:
                    m808x_dma_aen = false;
                    m808x_dma_ack = false;
                    m808x_dma_operating_cycle = 0u;
                    m808x_dma_state = M808X_DMA_IDLE;
                    break;
                default:
                    break;
            }
            break;
    }
}

static void
biu_cycle_i(m808x_cpu_t *icpu, uint16_t mc_line, const char *comment)
{
    m808x_biu_t *b = &icpu->biu;
    /* READY for this T3/Tw is sampled before m808x_dma_tick() changes the
     * DMAWAIT level for the following physical clock. */
    const unsigned dma_wait_this_cycle = m808x_dma_wait_states;

    if (icpu->cycle_num >= icpu->cycle_limit) {
        icpu->fatal = true;
        return;
    }

    icpu->mc_line = mc_line;
    icpu->trace_comment = comment;

    if (b->t_cycle == T_INIT)
        b->t_cycle = T_1;

    switch (b->bus_status_latch) {
        case BUS_PASSIVE:
            if (b->fetch.kind == FETCH_DELAYED && b->fetch.delay == 0u) {
                b->fetch.kind = FETCH_NORMAL;
                biu_make_fetch_decision(icpu);
            } else if (b->fetch.kind == FETCH_PAUSED_FULL && queue_has_room_for_fetch(icpu))
                biu_make_fetch_decision(icpu);
            else if (b->t_cycle == T_I)
                biu_make_fetch_decision(icpu);
            break;

        case BUS_CODE:
        case BUS_IOR:
        case BUS_IOW:
        case BUS_MEMR:
        case BUS_MEMW:
        case BUS_INTA:
            switch (b->t_cycle) {
                case T_1:
                    break;
                case T_2:
                    b->ale = false;
                    b->wait_remaining = icpu->configured_wait_states;
                    b->precharged_wait = 0;

                    /* NOCONA_M808X_IO_WAIT_CONSOLIDATED_V1
                     * IBM 5150/5160 motherboard READY logic imposes at least
                     * one Tw on every processor-generated I/O bus cycle. Use
                     * max(configured, 1), because clone wait configuration may
                     * already include it. */
                    if ((b->bus_status_latch == BUS_IOR ||
                         b->bus_status_latch == BUS_IOW) &&
                        b->wait_remaining == 0u)
                        b->wait_remaining = 1u;

                    /*
                     * Ask the active video implementation for any READY delay.  This keeps
                     * machine-specific contention out of the CPU and commits VRAM/snow
                     * side effects on the final bus edge.
                     */
                    if ((b->bus_status_latch == BUS_CODE) ||
                        (b->bus_status_latch == BUS_MEMR) ||
                        (b->bus_status_latch == BUS_MEMW)) {
                        const unsigned video_wait = video_get_wait_states(
                            b->address_latch & 0xfffffu,
                            b->bus_status_latch == BUS_MEMW,
                            (unsigned)b->transfer_size,
                            icpu->cycle_num);

                        if (UINT_MAX - b->wait_remaining < video_wait) {
                            icpu->fatal = true;
                            return;
                        }

                        b->precharged_wait = video_wait;
                        b->wait_remaining += video_wait;
                    }

                    if (b->final_transfer)
                        biu_make_fetch_decision(icpu);
                    break;
                case T_3:
                    b->bus_status = BUS_PASSIVE;
                    if (b->wait_remaining == 0u &&
                        dma_wait_this_cycle == 0u &&
                        !b->transfer_done)
                        biu_do_bus_transfer(icpu);
                    break;
                case T_W:
                    if (b->wait_remaining <= 1u &&
                        dma_wait_this_cycle == 0u &&
                        !b->transfer_done)
                        biu_do_bus_transfer(icpu);
                    break;
                case T_4:
                    if (b->bus_status_latch == BUS_CODE)
                        biu_finish_code_fetch(icpu);

                    if (b->final_transfer)
                        biu_make_fetch_decision(icpu);
                    break;
                case T_INIT:
                case T_I:
                    break;
            }
            break;

        case BUS_HALT:
            break;
    }

#ifdef M808X_86BOX_TESTING
    if (b->t_cycle == T_W)
        m808x_test_tw_clocks++;
    if (m808x_test_cycle_hook != NULL)
        m808x_test_cycle_hook(icpu);
#endif

    trace_cycle(icpu);

    /* Clock the XT 8237 arbitration logic on every physical CPU clock. */
    m808x_dma_tick(icpu);

    if (b->fetch.kind == FETCH_DELAYED && b->t_cycle != T_W && b->fetch.delay > 0u)
        b->fetch.delay--;

    switch (b->ta_cycle) {
        case TA_TR:
            b->ta_cycle = TA_TS;
            break;
        case TA_TS:
            b->ta_cycle = TA_T0;
            break;
        case TA_T0:
            if (b->pl_status == BUS_CODE) {
                if (b->pending == PENDING_NONE) {
                    if (((b->t_cycle == T_I) || (b->t_cycle == T_4)) &&
                        fetch_enabled(icpu)) {
                        biu_bus_begin_fetch(icpu);
                        b->ta_cycle = TA_DONE;
                    }
                } else if (b->pending == PENDING_EU_LATE) {
                    if ((b->t_cycle == T_I) || (b->t_cycle == T_4))
                        biu_fetch_abort(icpu);
                }
            } else if ((b->t_cycle == T_I) || (b->t_cycle == T_4))
                b->ta_cycle = TA_DONE;
            break;
        case TA_DONE:
        case TA_ABORT:
            break;
    }

    switch (b->t_cycle) {
        case T_INIT:
            b->t_cycle = T_1;
            break;
        case T_I:
            if (b->bus_status_latch == BUS_PASSIVE)
                b->t_cycle = T_I;
            else if (b->bus_status_latch == BUS_HALT) {
                b->bus_status = BUS_PASSIVE;
                b->bus_status_latch = BUS_PASSIVE;
                b->ale = false;
                b->t_cycle = T_I;
            } else
                b->t_cycle = T_1;
            break;
        case T_1:
            if (b->bus_status_latch == BUS_HALT) {
                b->bus_status = BUS_PASSIVE;
                b->bus_status_latch = BUS_PASSIVE;
                b->ale = false;
                b->t_cycle = T_I;
            } else
                b->t_cycle = T_2;
            break;
        case T_2:
            b->t_cycle = T_3;
            break;
        case T_3:
            if (b->wait_remaining > 0u || dma_wait_this_cycle > 0u)
                b->t_cycle = T_W;
            else {
                biu_bus_end(icpu);
                b->t_cycle = T_4;
            }
            break;
        case T_W:
            if (b->wait_remaining <= 1u && dma_wait_this_cycle == 0u) {
                b->wait_remaining = 0u;
                biu_bus_end(icpu);
                b->t_cycle = T_4;
            } else {
                if (b->wait_remaining > 0u)
                    --b->wait_remaining;
                b->t_cycle = T_W;
            }
            break;
        case T_4:
            b->bus_status = BUS_PASSIVE;
            b->bus_status_latch = BUS_PASSIVE;
            b->t_cycle = T_I;
            break;
    }

    b->queue.last_op = b->queue.op;
    b->queue.op = QOP_IDLE;
    if (m808x_dma_wait_states > 0u)
        --m808x_dma_wait_states;
    icpu->cycle_num++;
    m808x_host_cycle();
}

static void
biu_cycle(m808x_cpu_t *icpu)
{
    biu_cycle_i(icpu, MC_NONE, NULL);
}

static void
cycles_mc(m808x_cpu_t *icpu, const uint16_t *lines, size_t count)
{
    for (size_t i = 0; i < count && !icpu->fatal; i++)
        biu_cycle_i(icpu, lines[i], NULL);
}

#define CYCLES_MC(cpu_ptr, ...) \
    do { \
        const uint16_t _mc_lines[] = {__VA_ARGS__}; \
        cycles_mc((cpu_ptr), _mc_lines, ARRAY_LEN(_mc_lines)); \
    } while (0)

static void
biu_bus_wait_finish(m808x_cpu_t *icpu)
{
    const m808x_biu_t *b = &icpu->biu;

    if (!bus_is_active(b->bus_status_latch))
        return;

    while (b->t_cycle != T_4 && !icpu->fatal)
        biu_cycle(icpu);
}

static bool
biu_is_terminal_transfer_state(const m808x_biu_t *b)
{
    return ((b->t_cycle == T_3) &&
            (b->wait_remaining == 0u) &&
            (m808x_dma_wait_states == 0u)) ||
           ((b->t_cycle == T_W) &&
            (b->wait_remaining <= 1u) &&
            (m808x_dma_wait_states == 0u));
}

static void
biu_bus_wait_until_tx(m808x_cpu_t *icpu)
{
    const m808x_biu_t *b = &icpu->biu;

    if (!bus_is_active(b->bus_status_latch))
        return;

    while (!biu_is_terminal_transfer_state(b) && !icpu->fatal)
        biu_cycle(icpu);
}

static void
biu_bus_wait_address(m808x_cpu_t *icpu)
{
    while ((icpu->biu.ta_cycle != TA_DONE) && (icpu->biu.ta_cycle != TA_ABORT) && !icpu->fatal)
        biu_cycle(icpu);
}

static bool
biu_bus_wait_delay(m808x_cpu_t *icpu)
{
    m808x_biu_t *b       = &icpu->biu;
    bool         delayed = false;

    if (b->fetch.kind == FETCH_DELAYED) {
        delayed = true;

        while (b->fetch.kind == FETCH_DELAYED && b->fetch.delay > 0u && !icpu->fatal)
            biu_cycle(icpu);

        b->ta_cycle = TA_ABORT;
    }

    return delayed;
}

static void
biu_bus_begin(m808x_cpu_t *icpu,
              const m808x_bus_status_t status,
              const m808x_segment_t segment,
              const uint32_t address,
              const uint16_t data,
              const m808x_transfer_size_t size,
              const m808x_operand_size_t operand_size,
              const bool first)
{
    m808x_biu_t *b = &icpu->biu;
    assert(status != BUS_CODE);

    bool fetch_abort = false;

    switch (b->t_cycle) {
        case T_I:
            biu_address_start(icpu, status);
            break;
        case T_INIT:
        case T_1:
        case T_2:
            b->pending = PENDING_EU_EARLY;
            if (b->ta_cycle == TA_DONE) {
                biu_address_start(icpu, status);
            }
            break;
        case T_3:
        case T_W:
        case T_4:
            if (b->pl_status == BUS_CODE) {
                b->pending = PENDING_EU_LATE;
                fetch_abort = true;
            } else if (!b->final_transfer) {
                b->pending = PENDING_EU_EARLY;
            }
            break;
    }

    biu_bus_wait_finish(icpu);

    const bool was_delayed = biu_bus_wait_delay(icpu);
    biu_bus_wait_address(icpu);

    if (was_delayed || fetch_abort) {
        biu_address_start(icpu, status);
        biu_bus_wait_address(icpu);
    }

    if (b->t_cycle == T_4 && b->bus_status_latch != BUS_CODE)
        biu_cycle(icpu);

    if (size == XFER_WORD) {
        b->transfer_n = 1u;
        b->final_transfer = true;
    } else if (first) {
        b->transfer_n = 1u;
        b->final_transfer = operand_size == OPERAND_8;
    } else {
        b->transfer_n = 2u;
        b->final_transfer = true;
    }

    b->bhe = icpu->is_8086 && (size == XFER_WORD || (address & 1u) != 0u);
    b->pending = PENDING_NONE;
    b->pl_status = BUS_PASSIVE;
    b->bus_status = status;
    b->bus_status_latch = status;
    b->bus_segment = segment;
    b->t_cycle = T_INIT;
    b->address_bus = address & 0xfffffu;
    b->address_latch = b->address_bus;
    b->ale = true;
    b->data_bus = data;
    b->transfer_size = size;
    b->operand_size = operand_size;
    b->wait_remaining = 0u;
    b->transfer_done = false;

    if (b->bhe && size == XFER_BYTE)
        b->data_bus <<= 8;
}

static uint8_t
biu_read_u8(m808x_cpu_t *icpu, const m808x_segment_t segment, const uint16_t offset)
{
    const uint32_t address = segment == SEG_NONE ? offset : linear_address(icpu->segs[segment], offset);
    biu_bus_begin(icpu, BUS_MEMR, segment, address, 0u, XFER_BYTE, OPERAND_8, true);
    biu_bus_wait_finish(icpu);
    return icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8) : (uint8_t)icpu->biu.data_bus;
}

static void
biu_write_u8(m808x_cpu_t *icpu, const m808x_segment_t segment,
             const uint16_t offset, const uint8_t value)
{
    const uint32_t address = segment == SEG_NONE ? offset : linear_address(icpu->segs[segment], offset);
    biu_bus_begin(icpu, BUS_MEMW, segment, address, value, XFER_BYTE, OPERAND_8, true);
    biu_bus_wait_until_tx(icpu);

    if ((address >= 0xf0000) && (address <= 0xfffff))
        last_addr = address & 0xffff;
}

static uint16_t
biu_read_u16(m808x_cpu_t *icpu, const m808x_segment_t segment, const uint16_t offset)
{
    if (icpu->is_8086 && (offset & 1u) == 0u) {
        const uint32_t address = segment == SEG_NONE ? offset : linear_address(icpu->segs[segment], offset);

        biu_bus_begin(icpu, BUS_MEMR, segment, address, 0u, XFER_WORD, OPERAND_16, true);
        biu_bus_wait_finish(icpu);

        return icpu->biu.data_bus;
    }

    /* A word read on the 8088 is one indivisible operand transfer made of
     * two byte bus cycles.  The first byte is not a final transfer and may
     * not admit an intervening prefetch. */
    const uint32_t address0 = segment == SEG_NONE ? offset : linear_address(icpu->segs[segment], offset);
    biu_bus_begin(icpu, BUS_MEMR, segment, address0, 0u, XFER_BYTE, OPERAND_16, true);
    biu_bus_wait_finish(icpu);
    const uint8_t lo = icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8) : (uint8_t)icpu->biu.data_bus;

    const uint16_t offset1 = (uint16_t)(offset + 1u);
    const uint32_t address1 = segment == SEG_NONE ? offset1 : linear_address(icpu->segs[segment], offset1);
    biu_bus_begin(icpu, BUS_MEMR, segment, address1, 0u, XFER_BYTE, OPERAND_16, false);
    biu_bus_wait_finish(icpu);
    const uint8_t hi = icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8) : (uint8_t)icpu->biu.data_bus;
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static void
biu_write_u16(m808x_cpu_t *icpu, const m808x_segment_t segment,
              const uint16_t offset, const uint16_t value)
{
    if (icpu->is_8086 && (offset & 1u) == 0u) {
        const uint32_t address = segment == SEG_NONE ? offset : linear_address(icpu->segs[segment], offset);

        biu_bus_begin(icpu, BUS_MEMW, segment, address, value, XFER_WORD, OPERAND_16, true);
        biu_bus_wait_until_tx(icpu);

        if ((address >= 0xf0000) && (address <= 0xfffff))
            last_addr = address & 0xffff;

        return;
    }

    const uint32_t address0 = segment == SEG_NONE ? offset : linear_address(icpu->segs[segment], offset);

    biu_bus_begin(icpu, BUS_MEMW, segment, address0, value & 0xffu, XFER_BYTE, OPERAND_16, true);
    biu_bus_wait_finish(icpu);

    const uint16_t offset1 = (uint16_t)(offset + 1u);
    const uint32_t address1 = segment == SEG_NONE ? offset1 : linear_address(icpu->segs[segment], offset1);
    biu_bus_begin(icpu, BUS_MEMW, segment, address1, value >> 8, XFER_BYTE, OPERAND_16, false);
    biu_bus_wait_until_tx(icpu);

    if ((address0 >= 0xf0000) && (address0 <= 0xfffff))
        last_addr = address0 & 0xffff;
}

static uint8_t
biu_io_read_u8(m808x_cpu_t *icpu, const uint16_t port)
{
    biu_bus_begin(icpu, BUS_IOR, SEG_NONE, port, 0u, XFER_BYTE, OPERAND_8, true);
    biu_bus_wait_finish(icpu);

    return icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8)
                         : (uint8_t)icpu->biu.data_bus;
}

static uint16_t
biu_io_read_u16(m808x_cpu_t *icpu, const uint16_t port)
{
    if (icpu->is_8086 && (port & 1u) == 0u) {
        biu_bus_begin(icpu, BUS_IOR, SEG_NONE, port, 0u, XFER_WORD, OPERAND_16, true);
        biu_bus_wait_finish(icpu);

        return icpu->biu.data_bus;
    }

    biu_bus_begin(icpu, BUS_IOR, SEG_NONE, port, 0u, XFER_BYTE, OPERAND_16, true);
    biu_bus_wait_finish(icpu);

    const uint8_t lo = icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8)
                                     : (uint8_t)icpu->biu.data_bus;

    biu_bus_begin(icpu, BUS_IOR, SEG_NONE, (uint16_t)(port + 1u), 0u, XFER_BYTE, OPERAND_16, false);
    biu_bus_wait_finish(icpu);

    const uint8_t hi = icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8)
                                     : (uint8_t)icpu->biu.data_bus;

    return (uint16_t) (lo | ((uint16_t) hi << 8));
}

static void
biu_io_write_u8(m808x_cpu_t *icpu, const uint16_t port, const uint8_t value)
{
    biu_bus_begin(icpu, BUS_IOW, SEG_NONE, port, value, XFER_BYTE, OPERAND_8, true);
    biu_bus_wait_until_tx(icpu);
}

static void
biu_io_write_u16(m808x_cpu_t *icpu, const uint16_t port, const uint16_t value)
{
    if (icpu->is_8086 && (port & 1u) == 0u) {
        biu_bus_begin(icpu, BUS_IOW, SEG_NONE, port, value, XFER_WORD, OPERAND_16, true);
        biu_bus_wait_until_tx(icpu);
        return;
    }

    biu_bus_begin(icpu, BUS_IOW, SEG_NONE, port, value & 0xffu, XFER_BYTE, OPERAND_16, true);
    biu_bus_wait_finish(icpu);
    biu_bus_begin(icpu, BUS_IOW, SEG_NONE, (uint16_t)(port + 1u), value >> 8, XFER_BYTE, OPERAND_16, false);
    biu_bus_wait_until_tx(icpu);
}

/* Reads a word from the memory and advances the BIU. */
static uint16_t
readmemw(const uint32_t s, const uint16_t a)
{
    m808x_cpu_t *icpu = &m808x_cpu;

    if (icpu->is_8086 && (a & 1u) == 0u) {
        const uint32_t address = s + a;

        biu_bus_begin(icpu, BUS_MEMR, SEG_NONE, address, 0u, XFER_WORD, OPERAND_16, true);
        biu_bus_wait_finish(icpu);

        return icpu->biu.data_bus;
    }

    /* A word read on the 8088 is one indivisible operand transfer made of
     * two byte bus cycles.  The first byte is not a final transfer and may
     * not admit an intervening prefetch. */
    const uint32_t address0 = s + a;
    biu_bus_begin(icpu, BUS_MEMR, SEG_NONE, address0, 0u, XFER_BYTE, OPERAND_16, true);
    biu_bus_wait_finish(icpu);
    const uint8_t lo = icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8) : (uint8_t)icpu->biu.data_bus;

    const uint32_t address1 = s + (uint16_t) (a + 1);
    biu_bus_begin(icpu, BUS_MEMR, SEG_NONE, address1, 0u, XFER_BYTE, OPERAND_16, false);
    biu_bus_wait_finish(icpu);
    const uint8_t hi = icpu->biu.bhe ? (uint8_t)(icpu->biu.data_bus >> 8) : (uint8_t)icpu->biu.data_bus;
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static uint32_t
readmeml(const uint32_t s, const uint16_t a)
{
    uint32_t temp = (uint32_t) (readmemw(s, a + 2)) << 16;
    temp |= readmemw(s, a);

    return temp;
}

static uint64_t
readmemq(const uint32_t s, const uint16_t a)
{
    uint64_t temp = (uint64_t) (readmeml(s, a + 4)) << 32;
    temp |= readmeml(s, a);

    return temp;
}

/* Writes a byte to the memory and advances the BIU. */
static void
writememb(const uint32_t s, const uint32_t a, const uint8_t v)
{
    m808x_cpu_t *icpu = &m808x_cpu;
    const uint32_t address = s + a;
    biu_bus_begin(icpu, BUS_MEMW, SEG_NONE, address, v, XFER_BYTE, OPERAND_8, true);
    biu_bus_wait_until_tx(icpu);

    if ((address >= 0xf0000) && (address <= 0xfffff))
        last_addr = address & 0xffff;
}

/* Writes a word to the memory and advances the BIU. */
static void
writememw(const uint32_t s, const uint32_t a, const uint16_t v)
{
    m808x_cpu_t *icpu = &m808x_cpu;

    if (icpu->is_8086 && (a & 1u) == 0u) {
        const uint32_t address = s + a;

        biu_bus_begin(icpu, BUS_MEMW, SEG_NONE, address, v, XFER_WORD, OPERAND_16, true);
        biu_bus_wait_until_tx(icpu);

        if ((address >= 0xf0000) && (address <= 0xfffff))
            last_addr = address & 0xffff;

        return;
    }

    const uint32_t address0 = s + a;

    biu_bus_begin(icpu, BUS_MEMW, SEG_NONE, address0, v & 0xffu, XFER_BYTE, OPERAND_16, true);
    biu_bus_wait_finish(icpu);

    const uint32_t address1 = s + (uint16_t) (a + 1);
    biu_bus_begin(icpu, BUS_MEMW, SEG_NONE, address1, v >> 8, XFER_BYTE, OPERAND_16, false);
    biu_bus_wait_until_tx(icpu);

    if ((address0 >= 0xf0000) && (address0 <= 0xfffff))
        last_addr = address0 & 0xffff;
}

static void
writememl(const uint32_t s, const uint32_t a, const uint32_t v)
{
    writememw(s, a, v & 0xffff);
    writememw(s, a + 2, v >> 16);
}

static void
writememq(const uint32_t s, const uint32_t a, const uint64_t v)
{
    writememl(s, a, v & 0xffffffff);
    writememl(s, a + 4, v >> 32);
}

/* Various things needed for 8087. */
extern int        tempc_fpu;
extern uint32_t   cpu_data;

#define OP_TABLE(name) ops_##name

#define CPU_BLOCK_END()
#define SEG_CHECK_READ(seg)
#define SEG_CHECK_WRITE(seg)
#define CHECK_READ(a, b, c)
#define CHECK_WRITE(a, b, c)
#define UN_USED(x) (void) (x)
#define fetch_ea_16(val)
#define fetch_ea_32(val)
#define PREFETCH_RUN(a, b, c, d, e, f, g, h)

#define CYCLES(val)        \
{                      \
wait_cycs(val, 0); \
}

#define CLOCK_CYCLES_ALWAYS(val) \
{                            \
wait_cycs(val, 0);       \
}

#if 0
#    define CLOCK_CYCLES_FPU(val) \
{                         \
wait_cycs(val, 0);    \
}

#    define CLOCK_CYCLES(val)          \
{                              \
if (fpu_cycles > 0) {      \
fpu_cycles -= (val);   \
if (fpu_cycles < 0) {  \
wait_cycs(val, 0); \
}                      \
} else {                   \
wait_cycs(val, 0);     \
}                          \
}

#    define CONCURRENCY_CYCLES(c) fpu_cycles = (c)
#else
#    define CLOCK_CYCLES(val)  \
{                      \
wait_cycs(val, 0); \
}

#    define CLOCK_CYCLES_FPU(val) \
{                         \
wait_cycs(val, 0);    \
}

#    define CONCURRENCY_CYCLES(c)
#endif

typedef int (*OpFn)(uint32_t fetchdat);

#undef getr8
#define getr8(r) ((r & 4) ? cpu_state.regs[r & 3].b.h : cpu_state.regs[r & 3].b.l)

#undef setr8
#define setr8(r, v)                    \
    if (r & 4)                         \
        cpu_state.regs[r & 3].b.h = v; \
    else                               \
        cpu_state.regs[r & 3].b.l = v;

/* Reads a word from the effective address. */
static uint16_t
geteaw(void)
{
    if (cpu_mod == 3)
        return cpu_state.regs[cpu_rm].w;

    return readmemw(easeg, cpu_state.eaaddr);
}

/* Neede for 8087 - memory only. */
static uint32_t
geteal(void)
{
    if (cpu_mod == 3) {
        fatal("808x register geteal()\n");
        return 0xffffffff;
    }

    return readmeml(easeg, cpu_state.eaaddr);
}

/* Neede for 8087 - memory only. */
static uint64_t
geteaq(void)
{
    if (cpu_mod == 3) {
        fatal("808x register geteaq()\n");
        return 0xffffffff;
    }

    return readmemq(easeg, cpu_state.eaaddr);
}

/* Writes a word to the effective address. */
static void
seteaw(const uint16_t val)
{
    if (cpu_mod == 3)
        cpu_state.regs[cpu_rm].w = val;
    else
        writememw(easeg, cpu_state.eaaddr, val);
}

static void
seteal(const uint32_t val)
{
    if (cpu_mod == 3) {
        fatal("808x register seteal()\n");
        return;
    } else
        writememl(easeg, cpu_state.eaaddr, val);
}

static void
seteaq(const uint64_t val)
{
    if (cpu_mod == 3) {
        fatal("808x register seteaq()\n");
        return;
    } else
        writememq(easeg, cpu_state.eaaddr, val);
}

/* Leave out the 686 stuff as it's not needed and
   complicates compiling. */
#define FPU_8087
#define tempc tempc_fpu
#include "x87_sf.h"
#include "x87.h"
#include "x87_ops.h"
#undef tempc
#undef FPU_8087

static bool
m808x_86box_fpu_busy(void)
{
    /*
       86Box's current 8087 implementations execute synchronously. Pending
       unmasked exceptions are still delivered by their existing code path.
     */
    return false;
}

static void
m808x_86box_fpu_exec(const uint8_t op, uint8_t modrm,
                     const uint16_t ea, const uint8_t segment_index)
{
    const uint8_t saved_opcode = opcode;
    const uint32_t saved_rmdat = rmdat;
    const uint32_t saved_easeg = easeg;
    const int32_t saved_rm_data = cpu_state.rm_data.rm_mod_reg_data;
    const uint16_t saved_pc = cpu_state.pc;

    opcode = op;
    rmdat = modrm;
    cpu_mod = (modrm >> 6) & 3;
    cpu_reg = (modrm >> 3) & 7;
    cpu_rm = modrm & 7;
    cpu_state.eaaddr = ea;

    switch (segment_index) {
        case 0: easeg = es; break;
        case 1: easeg = cs; break;
        case 2: easeg = ss; break;
        case 3: easeg = ds; break;
        default: easeg = ds; break;
    }

    if (!hasfpu) {
        if (cpu_mod != 3)
            (void) readmemw(easeg, ea);
    } else if (fpu_softfloat) {
        switch (op) {
            case 0xd8: ops_sf_fpu_8087_d8[(modrm >> 3) & 0x1f](modrm); break;
            case 0xd9: ops_sf_fpu_8087_d9[modrm](modrm); break;
            case 0xda: ops_sf_fpu_8087_da[modrm](modrm); break;
            case 0xdb: ops_sf_fpu_8087_db[modrm](modrm); break;
            case 0xdc: ops_sf_fpu_8087_dc[(modrm >> 3) & 0x1f](modrm); break;
            case 0xdd: ops_sf_fpu_8087_dd[modrm](modrm); break;
            case 0xde: ops_sf_fpu_8087_de[modrm](modrm); break;
            case 0xdf: ops_sf_fpu_8087_df[modrm](modrm); break;
            default: break;
        }
    } else {
        switch (op) {
            case 0xd8: ops_fpu_8087_d8[(modrm >> 3) & 0x1f](modrm); break;
            case 0xd9: ops_fpu_8087_d9[modrm](modrm); break;
            case 0xda: ops_fpu_8087_da[modrm](modrm); break;
            case 0xdb: ops_fpu_8087_db[modrm](modrm); break;
            case 0xdc: ops_fpu_8087_dc[(modrm >> 3) & 0x1f](modrm); break;
            case 0xdd: ops_fpu_8087_dd[modrm](modrm); break;
            case 0xde: ops_fpu_8087_de[modrm](modrm); break;
            case 0xdf: ops_fpu_8087_df[modrm](modrm); break;
            default: break;
        }
    }

    cpu_state.pc = saved_pc;
    cpu_state.rm_data.rm_mod_reg_data = saved_rm_data;
    easeg = saved_easeg;
    rmdat = saved_rmdat;
    opcode = saved_opcode;
}

static void
biu_fetch_suspend(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;
    b->fetch.kind = FETCH_SUSPENDED;

    if (b->bus_status_latch == BUS_CODE)
        biu_bus_wait_finish(icpu);

    b->ta_cycle = TA_DONE;
    b->pl_status = BUS_PASSIVE;
}

static void
biu_queue_flush(m808x_cpu_t *icpu)
{
    queue_flush(&icpu->biu.queue);
    icpu->biu.fetch.kind = FETCH_NORMAL;
    icpu->biu.fetch.delay = 0u;
    icpu->biu.ta_cycle = TA_DONE;
    icpu->biu.pl_status = BUS_PASSIVE;
    biu_fetch_start(icpu);
}

static void
biu_fetch_halt(m808x_cpu_t *icpu)
{
    icpu->biu.fetch.kind = FETCH_HALTED;
}

static void
biu_halt(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    b->fetch.kind = FETCH_HALTED;
    biu_bus_wait_finish(icpu);

    if (b->t_cycle == T_4)
        biu_cycle(icpu);

    b->t_cycle = T_I;
    biu_cycle(icpu);

    b->bus_status = BUS_HALT;
    b->bus_status_latch = BUS_HALT;
    b->bus_segment = SEG_CS;
    b->t_cycle = T_1;
    b->ale = true;
    b->data_bus = 0u;
    b->transfer_size = icpu->is_8086 ? XFER_WORD : XFER_BYTE;
    b->operand_size = OPERAND_8;
    b->transfer_n = 1u;
    b->final_transfer = true;
    biu_cycle_i(icpu, MC_NONE, "HALT");
}

static void
biu_bus_wait_halt(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    if (bus_is_active(b->bus_status_latch) && ((b->t_cycle == T_INIT) || (b->t_cycle == T_1)))
        biu_cycle(icpu);
}

static void
biu_cancel_fetch_delay_before_queue_read(m808x_cpu_t *icpu)
{
    m808x_biu_t *b = &icpu->biu;

    if ((b->fetch.kind == FETCH_DELAYED) && queue_at_policy_threshold_before_read(icpu))
        b->fetch.delay = 0u;
}

static uint8_t
queue_read(m808x_cpu_t *icpu, const bool first)
{
    m808x_pfq_t *q         = &icpu->biu.queue;
    const bool   was_empty = !q->preload_valid && (q->len == 0u);
    uint8_t      value;

    /* Delay cancellation is evaluated from the pre-read queue length. */
    biu_cancel_fetch_delay_before_queue_read(icpu);

    if (q->preload_valid) {
        value = q->preload_byte;
        q->preload_valid = false;
        q->last_op = QOP_FIRST;
        q->was_read = value;
        icpu->nx = false;
        biu_fetch_on_queue_read(icpu);

        return value;
    }

    while (q->len == 0u && !icpu->fatal)
        biu_cycle(icpu);

    if (icpu->fatal)
        return 0u;

    value = queue_pop(q);
    q->op = first ? QOP_FIRST : QOP_SUBSEQUENT;
    q->was_read = value;

    /*
       If this read had to wait for an empty queue, the hardware does not
       immediately issue FOQR again after consuming the just-fetched byte.
     */
    if (!was_empty)
        biu_fetch_on_queue_read(icpu);

    biu_cycle(icpu);

    return value;
}

static void
biu_fetch_next(m808x_cpu_t *icpu)
{
    m808x_pfq_t *q = &icpu->biu.queue;
    unsigned timeout = 0u;

    while (q->len == 0u && !icpu->fatal) {
        if (icpu->nx) {
            icpu->nx = false;
            icpu->rni = false;
        }

        biu_cycle_i(icpu, MC_RNI, "FETCH_NEXT");

        if (++timeout > 64u) {
            fprintf(stderr, "prefetch timeout at %04X:%04X\n", icpu->segs[SEG_CS], architectural_ip(icpu));
            icpu->fatal = true;
            return;
        }
    }

    q->preload_byte = queue_pop(q);
    q->preload_valid = true;
    q->op = QOP_FIRST;
    biu_fetch_on_queue_read(icpu);
    biu_cycle_i(icpu, MC_RNI, "RNI preload");
}

static void
corr(m808x_cpu_t *icpu)
{
    icpu->pc = architectural_ip(icpu);
    biu_cycle_i(icpu, MC_CORR, "PC -= Q");
}

static void
reljmp2(m808x_cpu_t *icpu, const int16_t rel, const bool jump_into)
{
    if (jump_into)
        biu_cycle_i(icpu, MC_JUMP, "RELJMP entry");

    biu_fetch_suspend(icpu);
    CYCLES_MC(icpu, 0x0d2u, 0x0d3u);
    corr(icpu);
    icpu->pc = (uint16_t)(icpu->pc + rel);
    biu_cycle_i(icpu, 0x0d4u, NULL);
    biu_queue_flush(icpu);
    biu_cycle_i(icpu, 0x0d5u, NULL);
}

static void
push_u16(m808x_cpu_t *icpu, const uint16_t value)
{
    SP = (uint16_t)(SP - 2u);
    biu_write_u16(icpu, SEG_SS, SP, value);
}

static void
interrupt_ack(m808x_cpu_t *icpu)
{
    biu_bus_begin(icpu, BUS_INTA, SEG_NONE, 0u, 0u, XFER_BYTE, OPERAND_16, true);
    biu_bus_wait_finish(icpu);
    biu_bus_begin(icpu, BUS_INTA, SEG_NONE, 0u, icpu->interrupt_vector, XFER_BYTE, OPERAND_16, false);
    biu_bus_wait_finish(icpu);
}

static void interrupt_routine(m808x_cpu_t *icpu, uint8_t vector, bool skip_first);

static void
hardware_interrupt(m808x_cpu_t *icpu, const bool acknowledge)
{
    uint8_t vector = icpu->interrupt_vector;
    if (icpu->halted) {
        icpu->halted = false;
        icpu->biu.fetch.kind = FETCH_SUSPENDED;
    }

    if (acknowledge) {
        interrupt_ack(icpu);
        vector = icpu->interrupt_vector;
        biu_fetch_suspend(icpu);
        CYCLES_MC(icpu, 0x19bu, 0x19cu);
        interrupt_routine(icpu, vector, false);
        icpu->intr_pin = false;
    } else {
        CYCLES_MC(icpu, 0x199u, MC_JUMP);
        interrupt_routine(icpu, vector, true);
        icpu->nmi_pin = false;
    }
}

/* Effective-address timings split around displacement fetch, preserving the
 * known 8086 EA microflow used by the original probe. */
static const uint8_t ea_pre[0xc0] = {
    [0x00] = 4, [0x01] = 5, [0x02] = 5, [0x03] = 4,
    [0x04] = 2, [0x05] = 2, [0x06] = 0, [0x07] = 2,
    [0x40] = 4, [0x41] = 5, [0x42] = 5, [0x43] = 4,
    [0x44] = 2, [0x45] = 2, [0x46] = 2, [0x47] = 2,
    [0x80] = 4, [0x81] = 5, [0x82] = 5, [0x83] = 4,
    [0x84] = 2, [0x85] = 2, [0x86] = 2, [0x87] = 2
};

static const uint8_t ea_post[0xc0] = {
    [0x06] = 1,
    [0x40] = 3, [0x41] = 3, [0x42] = 3, [0x43] = 3,
    [0x44] = 3, [0x45] = 3, [0x46] = 3, [0x47] = 3,
    [0x80] = 2, [0x81] = 2, [0x82] = 2, [0x83] = 2,
    [0x84] = 2, [0x85] = 2, [0x86] = 2, [0x87] = 2
};

static bool
opcode_has_modrm(const uint8_t iopcode)
{
    switch (iopcode) {
        default:
            return false;
        case 0x00 ... 0x3b:
            if (!(iopcode & 0x04))
                return true;
            return false;
        case 0x80 ... 0x8f:
        case 0xc4 ... 0xc7:
        case 0xd0 ... 0xd3:
        case 0xd8 ... 0xdf:
        case 0xf6 ... 0xf7:
        case 0xfe ... 0xff:
            return true;
    }
}

static void
decode_modrm(m808x_cpu_t *icpu)
{
    m808x_instruction_t *ins = &icpu->ins;
    ins->modrm = queue_read(icpu, false);
    ins->mod = ins->modrm >> 6;
    ins->reg = (ins->modrm >> 3) & 7u;
    ins->rm = ins->modrm & 7u;
    ins->has_modrm = true;

    if (ins->mod == 3u)
        return;

    biu_cycle_i(icpu, MC_NONE, "MODRM decode");
    const uint8_t timing_index = (uint8_t)((ins->modrm & 0xc0u) | ins->rm);

    for (unsigned i = 0; i < ea_pre[timing_index]; i++)
        biu_cycle_i(icpu, MC_NONE, "EA pre");

    int16_t disp = 0;
    if (ins->mod == 0u && ins->rm == 6u) {
        const uint8_t lo = queue_read(icpu, false);
        const uint8_t hi = queue_read(icpu, false);
        disp = (int16_t)(uint16_t)(lo | ((uint16_t)hi << 8));
    } else if (ins->mod == 1u)
        disp = (int8_t)queue_read(icpu, false);
    else if (ins->mod == 2u) {
        const uint8_t lo = queue_read(icpu, false);
        const uint8_t hi = queue_read(icpu, false);
        disp = (int16_t)(uint16_t)(lo | ((uint16_t)hi << 8));
    }

    uint16_t base = 0u;
    bool use_ss = false;
    switch (ins->rm) {
        case 0: base = (uint16_t)(BX + SI); break;
        case 1: base = (uint16_t)(BX + DI); break;
        case 2: base = (uint16_t)(BP + SI); use_ss = true; break;
        case 3: base = (uint16_t)(BP + DI); use_ss = true; break;
        case 4: base = SI; break;
        case 5: base = DI; break;
        case 6:
            if (ins->mod == 0u)
                base = 0u;
            else {
                base = BP;
                use_ss = true;
            }
            break;
        case 7: base = BX; break;
        default: break;
    }

    ins->ea = (uint16_t)(base + disp);
    ins->ea_segment = ins->has_segment_override ? ins->segment_override : (use_ss ? SEG_SS : SEG_DS);
    icpu->ea_addr = ins->ea;
    icpu->ea_seg = ins->ea_segment;

    for (unsigned i = 0; i < ea_post[timing_index]; i++)
        biu_cycle_i(icpu, MC_NONE, "EA post");
}

static bool
decode_prefix(m808x_cpu_t *icpu, const uint8_t iopcode)
{
    m808x_instruction_t *ins = &icpu->ins;
    switch (iopcode) {
        case 0x26u:
        case 0x2eu:
        case 0x36u:
        case 0x3eu:
            ins->segment_override = (m808x_segment_t)((iopcode >> 3) & 3u);
            ins->has_segment_override = true;
            ins->prefixes |= PFX_SEG;
            return true;
        case 0xf0u:
        case 0xf1u: /* F1 aliases LOCK on original 8086/8088 decode */
            ins->prefixes |= PFX_LOCK;
            icpu->in_lock = true;
            return true;
        case 0xf2u:
            ins->prefixes |= PFX_REPNE;
            icpu->rep_prefix = 1u;
            return true;
        case 0xf3u:
            ins->prefixes |= PFX_REPE;
            icpu->rep_prefix = 2u;
            return true;
        default:
            return false;
    }
}

static bool
decode_instruction(m808x_cpu_t *icpu)
{
    memset(&icpu->ins, 0, sizeof(icpu->ins));
    icpu->ins.segment_override = SEG_DS;
    icpu->ins.ea_segment = SEG_DS;
    icpu->ins.instruction_ip = architectural_ip(icpu);
    icpu->in_lock = false;
    icpu->rep_prefix = 0u;

    uint8_t iopcode = queue_read(icpu, true);

    while (decode_prefix(icpu, iopcode)) {
        biu_cycle_i(icpu, MC_NONE, "PREFIX");
        iopcode = queue_read(icpu, true);
    }

    icpu->ins.opcode = iopcode;

    if (opcode_has_modrm(iopcode))
        decode_modrm(icpu);

    if (icpu->trace)
        printf("-------- instruction %" PRIu64 " @ %04X:%04X opcode=%02X --------\n",
               icpu->instruction_count,
               icpu->segs[SEG_CS],
               icpu->ins.instruction_ip,
               icpu->ins.opcode);

    return !icpu->fatal;
}

static bool
condition_true(const m808x_cpu_t *icpu, const uint8_t iopcode)
{
    const bool cf = (icpu->flags & C_FLAG) != 0u;
    const bool pf = (icpu->flags & P_FLAG) != 0u;
    const bool zf = (icpu->flags & Z_FLAG) != 0u;
    const bool sf = (icpu->flags & S_FLAG) != 0u;
    const bool of = (icpu->flags & V_FLAG) != 0u;

    switch (iopcode & 0x0fu) {
        case 0x0: return of;
        case 0x1: return !of;
        case 0x2: return cf;
        case 0x3: return !cf;
        case 0x4: return zf;
        case 0x5: return !zf;
        case 0x6: return cf || zf;
        case 0x7: return !cf && !zf;
        case 0x8: return sf;
        case 0x9: return !sf;
        case 0xa: return pf;
        case 0xb: return !pf;
        case 0xc: return sf != of;
        case 0xd: return sf == of;
        case 0xe: return zf || (sf != of);
        case 0xf: return !zf && (sf == of);
        default: return false;
    }
}

static uint16_t
read_queue_u16(m808x_cpu_t *icpu)
{
    const uint8_t lo = queue_read(icpu, false);
    const uint8_t hi = queue_read(icpu, false);

    return (uint16_t) (lo | ((uint16_t) hi << 8));
}

typedef enum {
    ALU_ADD = 0,
    ALU_OR,
    ALU_ADC,
    ALU_SBB,
    ALU_AND,
    ALU_SUB,
    ALU_XOR,
    ALU_CMP
} alu_kind_t;

static bool
parity_even8(uint8_t value)
{
    value ^= (uint8_t)(value >> 4);
    value &= 0x0fu;

    return ((0x6996u >> value) & 1u) == 0u;
}

static void
set_flag_state(m808x_cpu_t *icpu, const uint16_t flag, const bool state)
{
    if (state)
        icpu->flags |= flag;
    else
        icpu->flags &= (uint16_t)~flag;
}

static void
set_szp8(m808x_cpu_t *icpu, const uint8_t value)
{
    set_flag_state(icpu, S_FLAG, (value & 0x80u) != 0u);
    set_flag_state(icpu, Z_FLAG, value == 0u);
    set_flag_state(icpu, P_FLAG, parity_even8(value));
}

static void
set_szp16(m808x_cpu_t *icpu, const uint16_t value)
{
    set_flag_state(icpu, S_FLAG, (value & 0x8000u) != 0u);
    set_flag_state(icpu, Z_FLAG, value == 0u);
    set_flag_state(icpu, P_FLAG, parity_even8((uint8_t)value));
}

static uint16_t
alu_binary(m808x_cpu_t *icpu, const alu_kind_t kind, const uint16_t lhs, const uint16_t rhs,
           const bool word)
{
    const uint32_t mask = word ? 0xffffu : 0xffu;
    const uint32_t sign = word ? 0x8000u : 0x80u;
    const uint32_t a = lhs & mask;
    const uint32_t b = rhs & mask;
    const uint32_t carry_in = (icpu->flags & C_FLAG) != 0u ? 1u : 0u;
    uint32_t result = 0u;
    bool carry = false;
    bool overflow = false;
    bool aux = false;

    switch (kind) {
        case ALU_ADD:
        case ALU_ADC: {
            const uint32_t cin = kind == ALU_ADC ? carry_in : 0u;
            const uint32_t wide = a + b + cin;
            result = wide & mask;
            carry = wide > mask;
            aux = ((a ^ b ^ result) & 0x10u) != 0u;
            overflow = ((~(a ^ b) & (a ^ result)) & sign) != 0u;
            break;
        }
        case ALU_SUB:
        case ALU_CMP:
        case ALU_SBB: {
            const uint32_t bin = kind == ALU_SBB ? carry_in : 0u;
            const uint32_t subtrahend = b + bin;
            result = (a - subtrahend) & mask;
            carry = a < subtrahend;
            aux = ((a ^ b ^ result) & 0x10u) != 0u;
            overflow = (((a ^ b) & (a ^ result)) & sign) != 0u;
            break;
        }
        case ALU_OR:
            result = a | b;
            break;
        case ALU_AND:
            result = a & b;
            break;
        case ALU_XOR:
            result = a ^ b;
            break;
    }

    if (kind == ALU_OR || kind == ALU_AND || kind == ALU_XOR) {
        set_flag_state(icpu, C_FLAG, false);
        set_flag_state(icpu, V_FLAG, false);
        set_flag_state(icpu, A_FLAG, false);
    } else {
        set_flag_state(icpu, C_FLAG, carry);
        set_flag_state(icpu, V_FLAG, overflow);
        set_flag_state(icpu, A_FLAG, aux);
    }

    if (word)
        set_szp16(icpu, (uint16_t)result);
    else
        set_szp8(icpu, (uint8_t)result);

    return (uint16_t)result;
}

static uint16_t
alu_incdec(m808x_cpu_t *icpu, const uint16_t value, const bool decrement, const bool word)
{
    const bool carry = (icpu->flags & C_FLAG) != 0u;
    const uint16_t result = alu_binary(icpu, decrement ? ALU_SUB : ALU_ADD, value, 1u, word);

    set_flag_state(icpu, C_FLAG, carry);

    return result;
}

static uint8_t
get_reg8(const m808x_cpu_t *icpu, unsigned index)
{
    index &= 7u;

    if (index < 4u)
        return icpu->regs[index].b.l;

    return icpu->regs[index - 4u].b.h;
}

static void
set_reg8(m808x_cpu_t *icpu, unsigned index, const uint8_t value)
{
    index &= 7u;

    if (index < 4u)
        icpu->regs[index].b.l = value;
    else
        icpu->regs[index - 4u].b.h = value;
}

static uint16_t
get_reg16(const m808x_cpu_t *icpu, unsigned index)
{
    return icpu->regs[index & 7u].x;
}

static void
set_reg16(m808x_cpu_t *icpu, const unsigned index, const uint16_t value)
{
    icpu->regs[index & 7u].x = value;
}

static bool
rm_is_memory(const m808x_cpu_t *icpu)
{
    return (icpu->ins.has_modrm) && (icpu->ins.mod != 3u);
}

static void
ea_load_return(m808x_cpu_t *icpu)
{
    CYCLES_MC(icpu, 0x1e2u, MC_JUMP);
}

static void
ea_done_return(m808x_cpu_t *icpu)
{
    CYCLES_MC(icpu, 0x1e3u, MC_JUMP);
}

static uint16_t
read_rm(m808x_cpu_t *icpu, const bool word)
{
    if (!rm_is_memory(icpu))
        return word ? get_reg16(icpu, icpu->ins.rm) : get_reg8(icpu, icpu->ins.rm);

    const uint16_t value = word ? biu_read_u16(icpu, icpu->ins.ea_segment, icpu->ins.ea)
                                : biu_read_u8(icpu, icpu->ins.ea_segment, icpu->ins.ea);

    ea_load_return(icpu);

    return value;
}

static void
write_rm(m808x_cpu_t *icpu, const bool word, const uint16_t value)
{
    if (!rm_is_memory(icpu)) {
        if (word)
            set_reg16(icpu, icpu->ins.rm, value);
        else
            set_reg8(icpu, icpu->ins.rm, (uint8_t)value);
        return;
    }

    if (word)
        biu_write_u16(icpu, icpu->ins.ea_segment, icpu->ins.ea, value);
    else
        biu_write_u8(icpu, icpu->ins.ea_segment, icpu->ins.ea, (uint8_t)value);
}

static uint16_t
read_reg_field(const m808x_cpu_t *icpu, const bool word)
{
    return word ? get_reg16(icpu, icpu->ins.reg) : get_reg8(icpu, icpu->ins.reg);
}

static void
write_reg_field(m808x_cpu_t *icpu, const bool word, const uint16_t value)
{
    if (word)
        set_reg16(icpu, icpu->ins.reg, value);
    else
        set_reg8(icpu, icpu->ins.reg, (uint8_t)value);
}

static uint16_t
segment_value(const m808x_cpu_t *icpu, unsigned index)
{
    return icpu->segs[index & 3u];
}

static void
set_segment_value(m808x_cpu_t *icpu, const unsigned index, const uint16_t value)
{
    const unsigned seg = index & 3u;

    icpu->segs[seg] = value;

    if (seg == SEG_SS) {
        icpu->interrupt_shadow = 1u;
        icpu->trap_shadow = 1u;
    }

    if (seg == SEG_CS) {
        biu_fetch_suspend(icpu);
        corr(icpu);

        biu_queue_flush(icpu);
    }
}

static uint16_t
pop_u16(m808x_cpu_t *icpu)
{
    const uint16_t value = biu_read_u16(icpu, SEG_SS, SP);
    SP = (uint16_t)(SP + 2u);

    return value;
}

static void
push_reg16(m808x_cpu_t *icpu, unsigned reg)
{
    reg &= 7u;
    CYCLES_MC(icpu, 0x028u, 0x029u, 0x02au);
    SP = (uint16_t)(SP - 2u);
    const uint16_t value = (reg == 4u) ? SP : get_reg16(icpu, reg);
    biu_write_u16(icpu, SEG_SS, SP, value);
}

static void
pop_reg16(m808x_cpu_t *icpu, unsigned reg)
{
    reg &= 7u;

    const uint16_t value = biu_read_u16(icpu, SEG_SS, SP);

    if (reg == 4u)
        SP = value;
    else {
        SP = (uint16_t)(SP + 2u);
        set_reg16(icpu, reg, value);
    }
}

static void
push_segment(m808x_cpu_t *icpu, const unsigned seg)
{
    CYCLES_MC(icpu, 0x02cu, 0x02du, 0x02eu);
    push_u16(icpu, segment_value(icpu, seg));
}

static void
pop_segment(m808x_cpu_t *icpu, const unsigned seg)
{
    const uint16_t value = pop_u16(icpu);
    set_segment_value(icpu, seg, value);
}

static void
pop_flags(m808x_cpu_t *icpu)
{
    const bool old_if = (icpu->flags & I_FLAG) != 0u;
    const bool old_tf = (icpu->flags & T_FLAG) != 0u;

    const uint16_t value = pop_u16(icpu);

    icpu->flags = (uint16_t)((value & 0x0fd5u) | 0xf002u);

    if (!old_if && ((icpu->flags & I_FLAG) != 0u))
        icpu->interrupt_shadow = 1u;

    if (!old_tf && ((icpu->flags & T_FLAG) != 0u))
        icpu->trap_shadow = 1u;

    if (old_tf && ((icpu->flags & T_FLAG) == 0u))
        icpu->trap_disable_delay = 1u;
}

static void
nearcall(m808x_cpu_t *icpu, const uint16_t new_ip)
{
    const uint16_t return_ip = icpu->pc;
    biu_cycle_i(icpu, MC_JUMP, "NEARCALL");
    icpu->pc = new_ip;
    biu_queue_flush(icpu);
    CYCLES_MC(icpu, 0x077u, 0x078u, 0x079u);
    push_u16(icpu, return_ip);
}

static void
farcall(m808x_cpu_t *icpu, const uint16_t new_cs, const uint16_t new_ip, const bool jump_into)
{
    if (jump_into)
        biu_cycle_i(icpu, MC_JUMP, "FARCALL entry");

    biu_fetch_suspend(icpu);
    CYCLES_MC(icpu, 0x06bu, 0x06cu);
    corr(icpu);
    biu_cycle_i(icpu, 0x06du, NULL);
    push_u16(icpu, icpu->segs[SEG_CS]);
    icpu->segs[SEG_CS] = new_cs;
    CYCLES_MC(icpu, 0x06eu, 0x06fu);
    nearcall(icpu, new_ip);
}

static void
farret(m808x_cpu_t *icpu, const bool far_return)
{
    biu_cycle_i(icpu, MC_JUMP, "FARRET entry");
    icpu->pc = pop_u16(icpu);
    biu_fetch_suspend(icpu);
    CYCLES_MC(icpu, 0x0c3u, 0x0c4u);

    if (far_return) {
        biu_cycle_i(icpu, MC_JUMP, NULL);
        icpu->segs[SEG_CS] = pop_u16(icpu);
        biu_queue_flush(icpu);
        CYCLES_MC(icpu, 0x0c7u, MC_JUMP);
    } else {
        biu_queue_flush(icpu);
        CYCLES_MC(icpu, 0x0c5u, MC_JUMP);
    }
}

static void
interrupt_routine(m808x_cpu_t *icpu, const uint8_t vector, const bool skip_first)
{
    if (!skip_first)
        biu_cycle_i(icpu, 0x19du, NULL);

    CYCLES_MC(icpu, 0x19eu, 0x19fu);
    const uint16_t table = (uint16_t)((uint16_t)vector * 4u);
    uint16_t new_ip = biu_read_u16(icpu, SEG_NONE, table);
    biu_cycle_i(icpu, 0x1a1u, NULL);
    uint16_t new_cs = biu_read_u16(icpu, SEG_NONE, (uint16_t)(table + 2u));
    (void) m808x_host_override_vector(vector, &new_ip, &new_cs);
    biu_fetch_suspend(icpu);
    CYCLES_MC(icpu, 0x1a3u, 0x1a4u, 0x1a5u);
    push_u16(icpu, icpu->flags);
    icpu->flags &= (uint16_t)~(I_FLAG | T_FLAG);
    biu_cycle_i(icpu, 0x1a6u, NULL);
    CYCLES_MC(icpu, MC_JUMP, 0x06cu);
    corr(icpu);
    biu_cycle_i(icpu, 0x06du, NULL);
    push_u16(icpu, icpu->segs[SEG_CS]);
    icpu->segs[SEG_CS] = new_cs;
    CYCLES_MC(icpu, 0x06eu, 0x06fu);
    nearcall(icpu, new_ip);
}

static void
software_interrupt(m808x_cpu_t *icpu, uint8_t vector)
{
    interrupt_routine(icpu, vector, false);
}

static void
divide_interrupt(m808x_cpu_t *icpu)
{
    CYCLES_MC(icpu, 0x1a7u, MC_JUMP);
    interrupt_routine(icpu, 0u, true);
}

static uint16_t
shift_rotate(m808x_cpu_t *icpu, const unsigned operation,
             const uint16_t value, const uint8_t count,
             const bool word)
{
    const uint16_t mask = word ? 0xffffu : 0x00ffu;
    const uint16_t sign = word ? 0x8000u : 0x0080u;
    const unsigned op = operation & 7u;
    uint16_t result = value & mask;

    /*
       D2/D3 with CL==0 performs the microcode path but the ALU leaves both
       the operand and every flag unchanged.
      */
    if (count == 0u)
        return result;

    /*
       Original NMOS decode: /6 is SETMO for D0/D1 and SETMOC for D2/D3.
       SETMOC differs only in that a zero CL is a no-op, handled above.
     */
    if (op == 6u) {
        result = mask;
        set_flag_state(icpu, C_FLAG, false);
        set_flag_state(icpu, A_FLAG, false);
        set_flag_state(icpu, V_FLAG, false);

        if (word)
            set_szp16(icpu, result);
        else
            set_szp8(icpu, (uint8_t)result);

        return result;
    }

    const uint16_t original = result;

    bool cf = (icpu->flags & C_FLAG) != 0u;
    bool of = (icpu->flags & V_FLAG) != 0u;

    /*
       Keep the already-perfect D0/D1 rotate path explicit. This is also the
       correct path for D2/D3 when CL == 1. No undefined non-C/O flags move.
     */
    if ((count == 1u) && (op < 4u)) {
        switch (op) {
            case 0u: { /* ROL */
                const bool outgoing = (result & sign) != 0u;
                result = (uint16_t)(((result << 1) | (outgoing ? 1u : 0u)) & mask);
                cf = outgoing;
                of = cf != ((result & sign) != 0u);
                break;
            }
            case 1u: { /* ROR */
                const bool outgoing = (result & 1u) != 0u;
                result = (uint16_t)((result >> 1) | (outgoing ? sign : 0u));
                cf = outgoing;
                of = ((result & sign) != 0u) != ((result & (sign >> 1)) != 0u);
                break;
            }
            case 2u: { /* RCL */
                const bool incoming = cf;
                const bool outgoing = (result & sign) != 0u;
                result = (uint16_t)(((result << 1) | (incoming ? 1u : 0u)) & mask);
                cf = outgoing;
                of = cf != ((result & sign) != 0u);
                break;
            }
            case 3u: { /* RCR */
                const bool incoming = cf;
                const bool outgoing = (result & 1u) != 0u;
                result = (uint16_t)((result >> 1) | (incoming ? sign : 0u));
                cf = outgoing;
                of = ((result & sign) != 0u) != ((result & (sign >> 1)) != 0u);
                break;
            }
            default:
                break;
        }

        set_flag_state(icpu, C_FLAG, cf);
        set_flag_state(icpu, V_FLAG, of);

        return result;
    }

    /*
       Direct transcription of MartyPC's current NMOS ALU iteration. OF is
       the value produced by the final physical micro-iteration, even where
       Intel documents it as undefined for counts greater than one.
     */
    for (unsigned i = 0; i < count; i++) {
        switch (op) {
            case 0u: { /* ROL */
                const bool outgoing = (result & sign) != 0u;
                result = (uint16_t)(((result << 1) | (outgoing ? 1u : 0u)) & mask);
                cf = outgoing;
                of = cf != ((result & sign) != 0u);
                break;
            }
            case 1u: { /* ROR */
                const bool outgoing = (result & 1u) != 0u;
                result >>= 1;
                of = outgoing != ((result & (sign >> 1)) != 0u);
                result = (uint16_t)(result | (outgoing ? sign : 0u));
                cf = outgoing;
                break;
            }
            case 2u: { /* RCL */
                const bool incoming = cf;
                const bool outgoing = (result & sign) != 0u;
                result = (uint16_t)((result << 1) & mask);
                result = (uint16_t)(result | (incoming ? 1u : 0u));
                cf = outgoing;
                of = cf != ((result & sign) != 0u);
                break;
            }
            case 3u: { /* RCR */
                const bool incoming = cf;
                const bool outgoing = (result & 1u) != 0u;
                result >>= 1;
                of = incoming != ((result & (sign >> 1)) != 0u);
                result = (uint16_t)(result | (incoming ? sign : 0u));
                cf = outgoing;
                break;
            }
            case 4u: { /* SHL/SAL */
                const bool outgoing = (result & sign) != 0u;
                result = (uint16_t)((result << 1) & mask);
                cf = outgoing;
                of = cf != ((result & sign) != 0u);
                break;
            }
            case 5u: { /* SHR */
                cf = (result & 1u) != 0u;
                result >>= 1;
                break;
            }
            case 7u: { /* SAR */
                cf = (result & 1u) != 0u;
                result = (uint16_t)((result >> 1) | (result & sign));
                break;
            }
            default:
                break;
        }
    }

    set_flag_state(icpu, C_FLAG, cf);
    switch (op) {
        case 0u ... 3u:
            set_flag_state(icpu, V_FLAG, of);
            break;

        case 4u:
            /* NMOS SHL exposes AF as bit 4 of the final shifted result. */
            set_flag_state(icpu, V_FLAG, of);
            set_flag_state(icpu, A_FLAG, (result & 0x10u) != 0u);
            if (word) set_szp16(icpu, result); else set_szp8(icpu, (uint8_t)result);
            break;

        case 5u:
            set_flag_state(icpu, V_FLAG, count == 1u && (original & sign) != 0u);
            set_flag_state(icpu, A_FLAG, false);
            if (word) set_szp16(icpu, result); else set_szp8(icpu, (uint8_t)result);
            break;

        case 7u:
            set_flag_state(icpu, V_FLAG, false);
            set_flag_state(icpu, A_FLAG, false);
            if (word) set_szp16(icpu, result); else set_szp8(icpu, (uint8_t)result);
            break;

        default:
            break;
    }
    return result;
}

static void
do_daa(m808x_cpu_t *icpu)
{
    const uint8_t old_al = AL;
    const bool old_cf = (icpu->flags & C_FLAG) != 0u;
    const bool old_af = (icpu->flags & A_FLAG) != 0u;
    const uint8_t al_check = old_af ? 0x9fu : 0x99u;

    /* Undefined OF is stable on the NMOS 8088 and is part of the V2 oracle. */
    set_flag_state(icpu, V_FLAG,
                   old_cf ? (old_al >= 0x1au && old_al <= 0x7fu)
                          : (old_al >= 0x7au && old_al <= 0x7fu));

    set_flag_state(icpu, C_FLAG, false);

    if ((old_al & 0x0fu) > 9u || old_af) {
        AL = (uint8_t)(AL + 6u);
        set_flag_state(icpu, A_FLAG, true);
    } else
        set_flag_state(icpu, A_FLAG, false);

    if (old_al > al_check || old_cf) {
        AL = (uint8_t)(AL + 0x60u);
        set_flag_state(icpu, C_FLAG, true);
    }

    set_szp8(icpu, AL);
}

static void
do_das(m808x_cpu_t *icpu)
{
    const uint8_t old_al = AL;
    const bool old_cf = (icpu->flags & C_FLAG) != 0u;
    const bool old_af = (icpu->flags & A_FLAG) != 0u;
    const uint8_t al_check = old_af ? 0x9fu : 0x99u;
    bool of = false;

    if (!old_af && !old_cf)
        of = old_al >= 0x9au && old_al <= 0xdfu;
    else if (old_af && !old_cf) {
        of = (old_al >= 0x80u && old_al <= 0x85u) ||
             (old_al >= 0xa0u && old_al <= 0xe5u);
    } else if (!old_af && old_cf)
        of = old_al >= 0x80u && old_al <= 0xdfu;
    else
        of = old_al >= 0x80u && old_al <= 0xe5u;

    set_flag_state(icpu, V_FLAG, of);
    set_flag_state(icpu, C_FLAG, false);

        if ((old_al & 0x0fu) > 9u || old_af) {
        AL = (uint8_t)(AL - 6u);
        set_flag_state(icpu, A_FLAG, true);
    } else
        set_flag_state(icpu, A_FLAG, false);

    if (old_al > al_check || old_cf) {
        AL = (uint8_t)(AL - 0x60u);
        set_flag_state(icpu, C_FLAG, true);
    }

    set_szp8(icpu, AL);
}

static void
do_aaa(m808x_cpu_t *icpu)
{
    const uint8_t old_al = AL;
    uint8_t       new_al;

    CYCLES_MC(icpu, 0x148u, 0x149u, 0x14au, 0x14bu, 0x14cu, 0x14du);

    if ((old_al & 0x0fu) > 9u || (icpu->flags & A_FLAG) != 0u) {
        AH = (uint8_t)(AH + 1u);
        new_al = (uint8_t)(old_al + 6u);
        AL = (uint8_t)(new_al & 0x0fu);
        set_flag_state(icpu, A_FLAG, true);
        set_flag_state(icpu, C_FLAG, true);
    } else {
        new_al = old_al;
        AL = (uint8_t)(old_al & 0x0fu);
        set_flag_state(icpu, A_FLAG, false);
        set_flag_state(icpu, C_FLAG, false);
        biu_cycle_i(icpu, MC_JUMP, NULL);
    }

    set_flag_state(icpu, Z_FLAG, new_al == 0u);
    set_flag_state(icpu, P_FLAG, parity_even8(new_al));
    set_flag_state(icpu, V_FLAG, old_al >= 0x7au && old_al <= 0x7fu);
    set_flag_state(icpu, S_FLAG, old_al >= 0x7au && old_al <= 0xf9u);
}

static void
do_aas(m808x_cpu_t *icpu)
{
    const uint8_t old_al = AL;
    const bool    old_af = (icpu->flags & A_FLAG) != 0u;
    uint8_t       new_al;

    CYCLES_MC(icpu, 0x148u, 0x149u, 0x14au, 0x14bu, MC_JUMP, 0x14du);

    if ((old_al & 0x0fu) > 9u || old_af) {
        new_al = (uint8_t)(old_al - 6u);
        AH = (uint8_t)(AH - 1u);
        AL = (uint8_t)(new_al & 0x0fu);
        set_flag_state(icpu, A_FLAG, true);
        set_flag_state(icpu, C_FLAG, true);
    } else {
        new_al = old_al;
        AL = (uint8_t)(old_al & 0x0fu);
        set_flag_state(icpu, A_FLAG, false);
        set_flag_state(icpu, C_FLAG, false);
        biu_cycle_i(icpu, MC_JUMP, NULL);
    }

    set_flag_state(icpu, V_FLAG, old_af && old_al >= 0x80u && old_al <= 0x85u);
    set_flag_state(icpu, S_FLAG, (!old_af && old_al >= 0x80u) ||
                                     (old_af && (old_al <= 0x05u || old_al >= 0x86u)));
    set_flag_state(icpu, Z_FLAG, new_al == 0u);
    set_flag_state(icpu, P_FLAG, parity_even8(new_al));
}

static uint16_t
width_mask(const unsigned bits)
{
    return (bits == 8u) ? 0x00ffu : 0xffffu;
}

static uint16_t
width_neg(uint16_t value, const unsigned bits, bool *carry)
{
    const uint16_t mask = width_mask(bits);

    value &= mask;
    *carry = value != 0u;

    return (uint16_t)((0u - value) & mask);
}

static uint16_t
width_rcr1(uint16_t value, const unsigned bits, const bool carry_in, bool *carry_out)
{
    const uint16_t mask = width_mask(bits);
    const uint16_t sign = bits == 8u ? 0x0080u : 0x8000u;

    value &= mask;
    *carry_out = (value & 1u) != 0u;

    return (uint16_t)(((value >> 1) | (carry_in ? sign : 0u)) & mask);
}

static uint16_t
width_add(const uint16_t lhs, const uint16_t rhs, const unsigned bits, bool *carry)
{
    const uint32_t mask = width_mask(bits);
    const uint32_t sum = (uint32_t)(lhs & mask) + (uint32_t)(rhs & mask);

    *carry = sum > mask;

    return (uint16_t)(sum & mask);
}

typedef struct {
    uint16_t a;
    uint16_t b;
    uint16_t c;
    bool carry;
    bool negate;
} mul_tmp_t;

static mul_tmp_t
mul_cor_negate_cycles(m808x_cpu_t *icpu, const unsigned bits, uint16_t tmpa, uint16_t tmpb,
                      uint16_t tmpc, bool negate, const bool skip)
{
    bool carry = false;
    uint16_t sigma = 0u;

    if (!skip) {
        /*
           NEGATE's tmpa/tmpb/tmpc datapath is the physical 16-bit ALU even
           when CORX itself is operating on bytes.  Keeping the sign-extended
           upper byte is architecturally invisible in AX, but IMULCOF exposes
           it through its otherwise-undefined S/Z/P/A flags.
         */
        sigma = (uint16_t)(0u - tmpc);
        carry = tmpc != 0u;
        tmpc = sigma;

        if (carry) {
            sigma = (uint16_t)~tmpa;
            CYCLES_MC(icpu, 0x1b6u, 0x1b7u, 0x1b8u, MC_JUMP, 0x1bau);
        } else {
            sigma = (uint16_t)(0u - tmpa);
            CYCLES_MC(icpu, 0x1b6u, 0x1b7u, 0x1b8u, 0x1b9u, 0x1bau);
        }

        tmpa = sigma;
        negate = !negate;
    }

    const uint16_t sign = (bits == 8u) ? 0x0080u : 0x8000u;

    carry = (tmpb & sign) != 0u;
    sigma = (uint16_t)(0u - tmpb);

    CYCLES_MC(icpu, 0x1bbu, 0x1bcu, 0x1bdu);

    if (!carry)
        CYCLES_MC(icpu, MC_JUMP, 0x1bfu, MC_JUMP);
    else {
        tmpb = sigma;
        negate = !negate;

        CYCLES_MC(icpu, 0x1beu, MC_JUMP);
    }

    return (mul_tmp_t){tmpa, tmpb, tmpc, carry, negate};
}

static mul_tmp_t
mul_corx_cycles(m808x_cpu_t *icpu, const unsigned bits, const uint16_t tmpb, uint16_t tmpc,
                bool carry)
{
    uint16_t tmpa = 0u;
    tmpc = width_rcr1(tmpc, bits, carry, &carry);
    unsigned counter = bits - 1u;
    CYCLES_MC(icpu, 0x17fu, 0x180u);

    for (;;) {
        biu_cycle_i(icpu, 0x181u, NULL);

        if (carry) {
            const uint16_t lhs = (uint16_t)(tmpa & width_mask(bits));
            const uint16_t rhs = (uint16_t)(tmpb & width_mask(bits));
            tmpa = width_add(lhs, rhs, bits, &carry);
            const uint16_t sign = bits == 8u ? 0x0080u : 0x8000u;
            const bool overflow = (((lhs ^ tmpa) & (rhs ^ tmpa) & sign) != 0u);
            const bool aux = (((lhs ^ rhs ^ tmpa) & 0x10u) != 0u);
            /*
               183 is F-marked. MUL later overwrites these flags in MULCOF/
               IMULCOF, but AAD intentionally exposes C/A/O from the final
               CORX addition while replacing only S/Z/P from its AL result.
             */
            set_flag_state(icpu, C_FLAG, carry);
            set_flag_state(icpu, V_FLAG, overflow);
            set_flag_state(icpu, A_FLAG, aux);

            if (bits == 8u)
                set_szp8(icpu, (uint8_t)tmpa); else set_szp16(icpu, tmpa);

            CYCLES_MC(icpu, 0x182u, 0x183u);
        } else
            biu_cycle_i(icpu, MC_JUMP, NULL);

        tmpa = width_rcr1(tmpa, bits, carry, &carry);
        tmpc = width_rcr1(tmpc, bits, carry, &carry);
        CYCLES_MC(icpu, 0x184u, 0x185u, 0x186u);

        if (counter == 0u)
            break;

        counter--;
        biu_cycle_i(icpu, MC_JUMP, NULL);
    }

    CYCLES_MC(icpu, 0x187u, MC_JUMP);

    return (mul_tmp_t){tmpa, tmpb, tmpc, carry, false};
}

static void
mul_microcode_cycles(m808x_cpu_t *icpu, const unsigned bits, const uint16_t accumulator,
                     const uint16_t operand, const bool signed_op, bool negate)
{
    const uint16_t mask = width_mask(bits);
    const uint16_t sign = bits == 8u ? 0x0080u : 0x8000u;
    const uint16_t entry0 = bits == 8u ? 0x150u : 0x158u;
    const uint16_t entry1 = bits == 8u ? 0x151u : 0x159u;
    const uint16_t call_corx = bits == 8u ? 0x152u : 0x15au;
    const uint16_t post_corx = bits == 8u ? 0x153u : 0x15bu;
    const uint16_t cof_entry = bits == 8u ? 0x154u : 0x15cu;
    const uint16_t move_low = bits == 8u ? 0x155u : 0x15du;
    const uint16_t call_mulcof = bits == 8u ? 0x156u : 0x15eu;

    uint16_t tmpa = 0u;
    uint16_t tmpb = operand & mask;
    uint16_t tmpc = accumulator & mask;
    bool carry = (tmpc & sign) != 0u;

    CYCLES_MC(icpu, entry0, entry1);

    if (signed_op) {
        /* PREIMUL also negates the 16-bit temporary, not an 8-bit-truncated
         * value.  CORX consumes only the selected width later. */
        const uint16_t sigma = (uint16_t)(0u - tmpc);
        CYCLES_MC(icpu, MC_JUMP, 0x1c0u, 0x1c1u);

        if (carry) {
            tmpc = sigma;
            negate = !negate;

            CYCLES_MC(icpu, 0x1c2u, 0x1c3u, MC_JUMP);
        } else
            biu_cycle_i(icpu, MC_JUMP, NULL);

        const mul_tmp_t n = mul_cor_negate_cycles(icpu, bits, 0u, tmpb, tmpc, negate, true);
        tmpb = n.b;
        tmpc = n.c;
        carry = n.carry;
        negate = n.negate;
    }

    CYCLES_MC(icpu, call_corx, MC_JUMP);

    const mul_tmp_t corx = mul_corx_cycles(icpu, bits, tmpb, tmpc, carry);
    tmpa = corx.a;
    tmpc = corx.c;
    carry = corx.carry;

    biu_cycle_i(icpu, post_corx, NULL);

    if (negate) {
        biu_cycle_i(icpu, MC_JUMP, NULL);

        const mul_tmp_t n = mul_cor_negate_cycles(icpu, bits, tmpa, tmpb, tmpc, negate, false);

        tmpa = n.a;
        tmpc = n.c;
    }

    biu_cycle_i(icpu, cof_entry, NULL);

    if (signed_op) {
        biu_cycle_i(icpu, MC_JUMP, NULL);
        const bool sign_low = (tmpc & sign) != 0u;
        const uint16_t addend = sign_low ? 1u : 0u;
        /*
           IMULCOF feeds tmpa through the 16-bit ALU for both byte and word
           multiplication. Do not mask the byte form before its flags are
           latched.
         */
        const uint16_t sigma = (uint16_t)(tmpa + addend);
        const bool aux = ((tmpa ^ addend ^ sigma) & 0x10u) != 0u;
        CYCLES_MC(icpu, 0x1cdu, 0x1ceu, 0x1cfu);
        set_flag_state(icpu, A_FLAG, aux);

        /*
           IMULCOF's SIGMA flags are generated by the 16-bit ALU even for
           the byte form; the V2 vectors expose those undefined bits.
         */
        set_szp16(icpu, sigma);

        if (sigma == 0u)
            CYCLES_MC(icpu, 0x1d0u, MC_JUMP, 0x1ccu, MC_JUMP);
        else
            CYCLES_MC(icpu, 0x1d0u, 0x1d1u, MC_JUMP);
        CYCLES_MC(icpu, move_low, MC_JUMP);
    } else {
        CYCLES_MC(icpu, move_low, call_mulcof, MC_JUMP, 0x1d2u, 0x1d3u, MC_JUMP);

        if (tmpa == 0u)
            CYCLES_MC(icpu, 0x1d0u, MC_JUMP, 0x1ccu, MC_JUMP);
        else
            CYCLES_MC(icpu, 0x1d0u, 0x1d1u, MC_JUMP);
    }
}

static uint16_t
width_rcl1(uint16_t value, const unsigned bits, const bool carry_in, bool *carry_out)
{
    const uint16_t mask = width_mask(bits);
    const uint16_t sign = bits == 8u ? 0x0080u : 0x8000u;
    value &= mask;
    *carry_out = (value & sign) != 0u;

    return (uint16_t)(((value << 1) | (carry_in ? 1u : 0u)) & mask);
}

static uint16_t
width_sub(uint16_t lhs, uint16_t rhs, const unsigned bits, bool *carry,
          bool *overflow, bool *aux)
{
    const uint16_t mask = width_mask(bits);
    const uint16_t sign = bits == 8u ? 0x0080u : 0x8000u;
    lhs &= mask;
    rhs &= mask;

    const uint16_t result = (uint16_t)((lhs - rhs) & mask);
    *carry = lhs < rhs;

    *overflow = (((lhs ^ rhs) & (lhs ^ result) & sign) != 0u);
    *aux = (((lhs ^ rhs ^ result) & 0x10u) != 0u);

    return result;
}

static void
set_width_sub_flags(m808x_cpu_t *icpu, const unsigned bits, const uint16_t result, const bool carry,
                    const bool overflow, const bool aux)
{
    set_flag_state(icpu, C_FLAG, carry);
    set_flag_state(icpu, V_FLAG, overflow);

    set_flag_state(icpu, A_FLAG, aux);

    if (bits == 8u)
        set_szp8(icpu, (uint8_t)result);
    else
        set_szp16(icpu, result);
}

typedef struct {
    uint16_t a;
    uint16_t c;
    bool carry;
    bool ok;
} m808x_cord_result_t;

static m808x_cord_result_t
cord_cycles(m808x_cpu_t *icpu, const unsigned bits, uint16_t tmpa, uint16_t tmpb,
            uint16_t tmpc)
{
    const uint16_t mask = width_mask(bits);
    tmpa &= mask;
    tmpb &= mask;
    tmpc &= mask;
    bool carry = false, overflow = false, aux = false;
    uint16_t sigma = width_sub(tmpa, tmpb, bits, &carry, &overflow, &aux);
    set_width_sub_flags(icpu, bits, sigma, carry, overflow, aux);
    unsigned counter = bits;
    CYCLES_MC(icpu, 0x188u, 0x189u, 0x18au);

    if (!carry) {
        biu_cycle_i(icpu, MC_JUMP, NULL);
        return (m808x_cord_result_t){tmpa, tmpc, carry, false};
    }

    while (counter > 0u && !icpu->fatal) {
        tmpc = width_rcl1(tmpc, bits, carry, &carry);
        tmpa = width_rcl1(tmpa, bits, carry, &carry);
        CYCLES_MC(icpu, 0x18bu, 0x18cu, 0x18du, 0x18eu);

        if (carry) {
            CYCLES_MC(icpu, MC_JUMP, 0x195u, 0x196u);
            carry = false;
            bool ignored_carry = false, ignored_overflow = false, ignored_aux = false;
            tmpa = width_sub(tmpa, tmpb, bits, &ignored_carry, &ignored_overflow, &ignored_aux);
            counter--;
            if (counter > 0u) {
                biu_cycle_i(icpu, MC_JUMP, NULL);
                continue;
            }
            CYCLES_MC(icpu, 0x197u, MC_JUMP);
        } else {
            sigma = width_sub(tmpa, tmpb, bits, &carry, &overflow, &aux);
            set_width_sub_flags(icpu, bits, sigma, carry, overflow, aux);
            CYCLES_MC(icpu, 0x18fu, 0x190u);
            if (!carry) {
                CYCLES_MC(icpu, MC_JUMP, 0x196u);
                tmpa = sigma;
                counter--;
                if (counter > 0u) {
                    biu_cycle_i(icpu, MC_JUMP, NULL);
                    continue;
                }
                CYCLES_MC(icpu, 0x197u, MC_JUMP);
            } else {
                biu_cycle_i(icpu, 0x191u, NULL);
                counter--;
                if (counter > 0u)
                    biu_cycle_i(icpu, MC_JUMP, NULL);
            }
        }
    }

    tmpc = width_rcl1(tmpc, bits, carry, &carry);
    CYCLES_MC(icpu, 0x192u, 0x193u);
    (void)width_rcl1(tmpc, bits, carry, &carry);
    set_flag_state(icpu, C_FLAG, carry);
    CYCLES_MC(icpu, 0x194u, MC_JUMP);

    return (m808x_cord_result_t){tmpa, tmpc, carry, true};
}

static mul_tmp_t
pre_idiv_cycles(m808x_cpu_t *icpu, const unsigned bits, const uint16_t tmpa, const uint16_t tmpb,
                const uint16_t tmpc, const bool negate)
{
    bool carry = false;

    (void) width_rcl1(tmpa, bits, false, &carry);

    CYCLES_MC(icpu, 0x1b4u, 0x1b5u);

    if (!carry) {
        biu_cycle_i(icpu, MC_JUMP, NULL);
        return mul_cor_negate_cycles(icpu, bits, tmpa, tmpb, tmpc, negate, true);
    }

    return mul_cor_negate_cycles(icpu, bits, tmpa, tmpb, tmpc, negate, false);
}

static m808x_cord_result_t
post_idiv_cycles(m808x_cpu_t *icpu, const unsigned bits, uint16_t tmpa, const uint16_t tmpb,
                 const uint16_t tmpc, const bool carry, const bool negate)
{
    const uint16_t mask = width_mask(bits);
    biu_cycle_i(icpu, 0x1c4u, NULL);

    if (!carry) {
        biu_cycle_i(icpu, MC_JUMP, NULL);
        return (m808x_cord_result_t){tmpa, tmpc, carry, false};
    }

    bool divisor_negative = false;
    (void) width_rcl1(tmpb, bits, false, &divisor_negative);
    bool ignored = false;
    uint16_t sigma = width_neg(tmpa, bits, &ignored);
    CYCLES_MC(icpu, 0x1c5u, 0x1c6u, 0x1c7u);

    if (!divisor_negative)
        biu_cycle_i(icpu, MC_JUMP, NULL);
    else {
        tmpa = sigma;
        biu_cycle_i(icpu, 0x1c8u, NULL);
    }

    sigma = (uint16_t)((tmpc + 1u) & mask);
    CYCLES_MC(icpu, 0x1c9u, 0x1cau);

    if (!negate) {
        sigma = (uint16_t)(~tmpc & mask);
        biu_cycle_i(icpu, 0x1cbu, NULL);
    } else
        biu_cycle_i(icpu, MC_JUMP, NULL);

    icpu->flags &= (uint16_t)~(C_FLAG | V_FLAG);
    CYCLES_MC(icpu, 0x1ccu, MC_JUMP);

    return (m808x_cord_result_t){tmpa, sigma, false, true};
}

static bool
div_microcode_cycles(m808x_cpu_t *icpu, const unsigned bits, const uint32_t dividend,
                     const uint16_t divisor, const bool signed_op, bool negate)
{
    const uint16_t mask = width_mask(bits);
    uint16_t tmpa = bits == 8u ? (uint16_t)((dividend >> 8) & 0xffu)
                               : (uint16_t)(dividend >> 16);
    uint16_t tmpc = (uint16_t)(dividend & mask);
    uint16_t tmpb = divisor & mask;

    CYCLES_MC(icpu,
              bits == 8u ? 0x160u : 0x168u,
              bits == 8u ? 0x161u : 0x169u,
              bits == 8u ? 0x162u : 0x16au);

    if (signed_op) {
        biu_cycle_i(icpu, MC_JUMP, NULL);
        const mul_tmp_t pre = pre_idiv_cycles(icpu, bits, tmpa, tmpb, tmpc, negate);
        tmpa = pre.a;
        tmpb = pre.b;
        tmpc = pre.c;
        negate = pre.negate;
    }

    CYCLES_MC(icpu, bits == 8u ? 0x163u : 0x16bu, MC_JUMP);
    m808x_cord_result_t cord = cord_cycles(icpu, bits, tmpa, tmpb, tmpc);

    if (!cord.ok)
        return false;

    tmpa = cord.a;
    tmpc = cord.c;

    const uint16_t original_hi = bits == 8u ? (uint16_t)((dividend >> 8) & 0xffu)
                                            : (uint16_t)(dividend >> 16);

    CYCLES_MC(icpu, bits == 8u ? 0x164u : 0x16cu,
              bits == 8u ? 0x165u : 0x16du);

    if (signed_op) {
        biu_cycle_i(icpu, MC_JUMP, NULL);
        cord = post_idiv_cycles(icpu, bits, tmpa, original_hi, tmpc, cord.carry, negate);

        if (!cord.ok)
            return false;
    }

    return !icpu->fatal;
}

static bool
rep_interrupt_pending(const m808x_cpu_t *icpu)
{
    return icpu->nmi_pin || (icpu->intr_pin && ((icpu->flags & I_FLAG) != 0u)) ||
           ((icpu->flags & T_FLAG) != 0u);
}

static void
rep_interrupt_rewind(m808x_cpu_t *icpu)
{
    biu_fetch_suspend(icpu);
    CYCLES_MC(icpu, 0x118u, 0x119u);
    corr(icpu);
    biu_cycle_i(icpu, 0x11au, NULL);
    biu_queue_flush(icpu);
    icpu->pc = (uint16_t)(icpu->pc - 2u);
}

static void
string_one(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const bool word = (iopcode & 1u) != 0u;
    const uint16_t delta = word ? 2u : 1u;
    const bool backwards = (icpu->flags & D_FLAG) != 0u;
    const m808x_segment_t src_seg = icpu->ins.has_segment_override ? icpu->ins.segment_override : SEG_DS;

    switch (iopcode & 0xfeu) {
        case 0xa4u: { /* MOVS */
            const uint16_t value = word ? biu_read_u16(icpu, src_seg, SI) : biu_read_u8(icpu, src_seg, SI);
            biu_cycle_i(icpu, 0x12eu, NULL);
            if (word) biu_write_u16(icpu, SEG_ES, DI, value); else biu_write_u8(icpu, SEG_ES, DI, (uint8_t)value);
            SI = backwards ? (uint16_t)(SI - delta) : (uint16_t)(SI + delta);
            DI = backwards ? (uint16_t)(DI - delta) : (uint16_t)(DI + delta);
            break;
        }
        case 0xa6u: { /* CMPS */
            biu_cycle_i(icpu, 0x121u, NULL);
            const uint16_t lhs = word ? biu_read_u16(icpu, src_seg, SI) : biu_read_u8(icpu, src_seg, SI);
            CYCLES_MC(icpu, 0x123u, 0x124u);
            const uint16_t rhs = word ? biu_read_u16(icpu, SEG_ES, DI) : biu_read_u8(icpu, SEG_ES, DI);
            CYCLES_MC(icpu, 0x126u, 0x127u, 0x128u);
            (void)alu_binary(icpu, ALU_CMP, lhs, rhs, word);
            SI = backwards ? (uint16_t)(SI - delta) : (uint16_t)(SI + delta);
            DI = backwards ? (uint16_t)(DI - delta) : (uint16_t)(DI + delta);
            break;
        }
        case 0xaau: /* STOS */
            if (word) biu_write_u16(icpu, SEG_ES, DI, AX); else biu_write_u8(icpu, SEG_ES, DI, AL);
            DI = backwards ? (uint16_t)(DI - delta) : (uint16_t)(DI + delta);
            break;
        case 0xacu: { /* LODS */
            const uint16_t value = word ? biu_read_u16(icpu, src_seg, SI) : biu_read_u8(icpu, src_seg, SI);
            if (word) AX = value; else AL = (uint8_t)value;
            SI = backwards ? (uint16_t)(SI - delta) : (uint16_t)(SI + delta);
            break;
        }
        case 0xaeu: { /* SCAS */
            CYCLES_MC(icpu, 0x121u, MC_JUMP);
            const uint16_t rhs = word ? biu_read_u16(icpu, SEG_ES, DI) : biu_read_u8(icpu, SEG_ES, DI);
            CYCLES_MC(icpu, 0x126u, 0x127u, 0x128u);
            (void)alu_binary(icpu, ALU_CMP, word ? AX : AL, rhs, word);
            DI = backwards ? (uint16_t)(DI - delta) : (uint16_t)(DI + delta);
            break;
        }
        default:
            break;
    }
}

static void
execute_string(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const uint8_t family = iopcode & 0xfeu;
    const bool compare = family == 0xa6u || family == 0xaeu;
    const bool rep = icpu->rep_prefix != 0u;
    const uint16_t entry = family == 0xaau ? 0x11cu : compare ? 0x120u : 0x12cu;
    biu_cycle_i(icpu, entry, NULL);

    if (!rep) {
        string_one(icpu, iopcode);
        if (family == 0xaau)
            CYCLES_MC(icpu, 0x11du, 0x11eu, MC_JUMP);
        else if (family == 0xa4u)
            CYCLES_MC(icpu, 0x12fu, 0x130u, MC_JUMP);
        else if (family == 0xacu)
            CYCLES_MC(icpu, 0x12eu, MC_JUMP, 0x1f8u);
        else if (compare)
            biu_cycle_i(icpu, MC_JUMP, NULL);
        return;
    }

    CYCLES_MC(icpu, MC_JUMP, 0x112u, 0x113u, 0x114u);

    if (CX == 0u)
        return;

    CYCLES_MC(icpu, MC_JUMP, 0x116u, MC_JUMP);

    while ((CX != 0u) && !icpu->fatal) {
        string_one(icpu, iopcode);

        if (family == 0xaau) { /* STOS: interrupt test precedes CX decrement. */
            CYCLES_MC(icpu, 0x11du, 0x11eu);
            biu_cycle_i(icpu, 0x11fu, NULL);

            if (rep_interrupt_pending(icpu)) {
                biu_cycle_i(icpu, MC_JUMP, NULL);
                rep_interrupt_rewind(icpu);
                return;
            }

            biu_cycle_i(icpu, 0x1f0u, NULL);
            CX = (uint16_t)(CX - 1u);

            if (CX != 0u)
                biu_cycle_i(icpu, MC_JUMP, "REP STOS");

            continue;
        }

        if (family == 0xa4u) { /* MOVS: decrement, then interrupt test. */
            CYCLES_MC(icpu, 0x12fu, 0x130u);
            CX = (uint16_t)(CX - 1u);

            if (rep_interrupt_pending(icpu)) {
                CYCLES_MC(icpu, 0x131u, MC_JUMP);
                rep_interrupt_rewind(icpu);
                return;
            }

            CYCLES_MC(icpu, 0x131u, 0x132u);

            if (CX != 0u)
                biu_cycle_i(icpu, MC_JUMP, "REP MOVS");

            continue;
        }

        if (family == 0xacu) {
            /* LODS follows the alternate 12cb entry path. */
            CYCLES_MC(icpu, 0x12eu, MC_JUMP, 0x1f8u, MC_JUMP, 0x131u);
            CX = (uint16_t)(CX - 1u);

            if (rep_interrupt_pending(icpu)) {
                biu_cycle_i(icpu, MC_JUMP, NULL);
                rep_interrupt_rewind(icpu);
                return;
            }

            biu_cycle_i(icpu, 0x132u, NULL);

            if (CX != 0u)
                biu_cycle_i(icpu, MC_JUMP, "REP LODS");

            continue;
        }

        /* CMPS/SCAS: decrement and test Z condition before interrupt/CX termination. */
        biu_cycle_i(icpu, 0x129u, NULL);
        CX = (uint16_t)(CX - 1u);
        const bool zf = (icpu->flags & Z_FLAG) != 0u;
        const bool repeat_condition = icpu->rep_prefix == 1u ? !zf : zf;

        if (!repeat_condition) {
            biu_cycle_i(icpu, MC_JUMP, NULL);
            break;
        }

        biu_cycle_i(icpu, 0x12au, NULL);

        if (rep_interrupt_pending(icpu)) {
            biu_cycle_i(icpu, MC_JUMP, NULL);
            rep_interrupt_rewind(icpu);
            return;
        }

        biu_cycle_i(icpu, 0x12bu, NULL);

        if (CX != 0u)
            biu_cycle_i(icpu, MC_JUMP, "REP compare");
    }
}

static alu_kind_t
alu_kind_from_index(const unsigned index)
{
    static const alu_kind_t kinds[8] = {
        ALU_ADD, ALU_OR, ALU_ADC, ALU_SBB, ALU_AND, ALU_SUB, ALU_XOR, ALU_CMP
    };

    return kinds[index & 7u];
}

static void
execute_alu_rm_reg(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const bool word = (iopcode & 1u) != 0u;
    const bool direction = (iopcode & 2u) != 0u;
    const alu_kind_t kind = alu_kind_from_index(iopcode >> 3);
    const uint16_t dst = direction ? read_reg_field(icpu, word) : read_rm(icpu, word);
    const uint16_t src = direction ? read_rm(icpu, word) : read_reg_field(icpu, word);

    biu_cycle_i(icpu, 0x008u, NULL);

    const uint16_t result = alu_binary(icpu, kind, dst, src, word);

    if (kind == ALU_CMP)
        return;

    if (!direction && rm_is_memory(icpu))
        CYCLES_MC(icpu, 0x009u, 0x00au);

    if (direction)
        write_reg_field(icpu, word, result);
    else
        write_rm(icpu, word, result);
}

static void
execute_alu_acc_imm(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const bool word = (iopcode & 1u) != 0u;
    const alu_kind_t kind = alu_kind_from_index(iopcode >> 3);

    const uint16_t imm = word ? read_queue_u16(icpu) : queue_read(icpu, false);

    if (!word)
        biu_cycle_i(icpu, MC_JUMP, NULL);

    const uint16_t dst = word ? AX : AL;
    const uint16_t result = alu_binary(icpu, kind, dst, imm, word);

    if (kind != ALU_CMP) {
        if (word)
            AX = result;
        else
            AL = (uint8_t)result;
    }
}

static void
execute_group1(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const bool     word = (iopcode == 0x81u) || (iopcode == 0x83u);
    const uint16_t dst  = read_rm(icpu, word);
    uint16_t       imm;

    if (iopcode == 0x81u)
        imm = read_queue_u16(icpu);
    else {
        const uint8_t b = queue_read(icpu, false);
        imm = iopcode == 0x83u ? (uint16_t)(int16_t)(int8_t)b : b;
        biu_cycle_i(icpu, MC_JUMP, NULL);
    }

    const alu_kind_t kind = alu_kind_from_index(icpu->ins.reg);
    const uint16_t result = alu_binary(icpu, kind, dst, imm, word);

    if (rm_is_memory(icpu))
        biu_cycle_i(icpu, 0x00eu, NULL);

    if (kind != ALU_CMP)
        write_rm(icpu, word, result);
}

static void
execute_mov_rm_reg(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const bool word = ((iopcode & 1u) != 0u);
    const bool direction = ((iopcode & 2u) != 0u);
    const uint16_t value = direction ? read_rm(icpu, word) : read_reg_field(icpu, word);

    if (!direction && rm_is_memory(icpu)) {
        ea_done_return(icpu);
        CYCLES_MC(icpu, 0x000u, 0x001u);
    }

    if (direction)
        write_reg_field(icpu, word, value);
    else
        write_rm(icpu, word, value);
}

static void
execute_shift_group(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const bool word = ((iopcode & 1u) != 0u);
    const bool from_cl = ((iopcode & 2u) != 0u);
    const uint8_t count = from_cl ? CL : 1u;
    const uint16_t value = read_rm(icpu, word);

    if (from_cl) {
        CYCLES_MC(icpu, 0x08cu, 0x08du, 0x08eu, MC_JUMP, 0x090u, 0x091u);
        for (unsigned i = 0; i < count; i++)
            CYCLES_MC(icpu, MC_JUMP, 0x08fu, 0x090u, 0x091u);

        if (rm_is_memory(icpu))
            biu_cycle_i(icpu, 0x092u, NULL);
    } else if (rm_is_memory(icpu))
        CYCLES_MC(icpu, 0x088u, 0x089u);

    const uint16_t result = shift_rotate(icpu, icpu->ins.reg, value, count, word);

    write_rm(icpu, word, result);
}

static void
execute_group3(m808x_cpu_t *icpu, uint8_t iopcode)
{
    const bool word = (iopcode & 1u) != 0u;
    const unsigned subop = icpu->ins.reg;
    const uint16_t operand = read_rm(icpu, word);

    switch (subop) {
        default:
            return;
        case 0u: case 1u: {
            const uint16_t imm = word ? read_queue_u16(icpu) : queue_read(icpu, false);

            if (!word)
                biu_cycle_i(icpu, MC_JUMP, NULL);

            biu_cycle_i(icpu, 0x09au, NULL);
            (void) alu_binary(icpu, ALU_AND, operand, imm, word);
            break;
        }
        case 2u: {
            const uint16_t result = (uint16_t)~operand;

            if (rm_is_memory(icpu))
                CYCLES_MC(icpu, 0x04cu, 0x04du);
            else
                biu_cycle_i(icpu, 0x04cu, NULL);

            write_rm(icpu, word, result);
            break;
        }
        case 3u: {
            const uint16_t result = alu_binary(icpu, ALU_SUB, 0u, operand, word);

            if (rm_is_memory(icpu))
                CYCLES_MC(icpu, 0x050u, 0x051u);
            else
                biu_cycle_i(icpu, 0x050u, NULL);

            write_rm(icpu, word, result);
            break;
        }
        case 4u: case 5u: {
            const bool signed_op = subop == 5u;
            if (word) {
                const uint16_t accumulator = AX;
                mul_microcode_cycles(icpu, 16u, accumulator, operand, signed_op, icpu->rep_prefix != 0u);
                int64_t product;

                if (signed_op)
                    product = (int32_t)(int16_t)AX * (int32_t)(int16_t)operand;
                else
                    product = (uint32_t)AX * (uint32_t)operand;

                if (icpu->rep_prefix != 0u) product = -product;
                AX = (uint16_t) product;
                DX = (uint16_t) ((uint64_t) product >> 16);

                const bool overflow = signed_op ? ((int32_t)product != (int32_t)(int16_t)AX) : DX != 0u;

                set_flag_state(icpu, C_FLAG, overflow);
                set_flag_state(icpu, V_FLAG, overflow);

                if (!signed_op) {
                    set_szp16(icpu, DX);
                    set_flag_state(icpu, A_FLAG, false);
                }
            } else {
                const uint16_t accumulator = AL;
                mul_microcode_cycles(icpu, 8u, accumulator, operand, signed_op, icpu->rep_prefix != 0u);

                int32_t product;

                if (signed_op)
                    product = (int16_t)(int8_t)AL * (int16_t)(int8_t)operand;
                else
                    product = (uint16_t)AL * (uint16_t)(uint8_t)operand;

                if (icpu->rep_prefix != 0u)
                    product = -product;

                AX = (uint16_t) product;

                const bool overflow = signed_op ? ((int16_t)product != (int16_t)(int8_t)AL) : AH != 0u;

                set_flag_state(icpu, C_FLAG, overflow);
                set_flag_state(icpu, V_FLAG, overflow);

                if (!signed_op) {
                    set_szp8(icpu, AH);
                    set_flag_state(icpu, A_FLAG, false);
                }
            }

            if (!rm_is_memory(icpu))
                biu_cycle(icpu);
            break;
        }
        case 6u: case 7u: {
            const bool signed_op = subop == 7u;

            if (!rm_is_memory(icpu))
                biu_cycle(icpu);

            const uint32_t dividend = word ? (((uint32_t) DX << 16) | AX) : AX;

            if (!div_microcode_cycles(icpu, word ? 16u : 8u, dividend, operand,
                                      signed_op, icpu->rep_prefix != 0u)) {
                divide_interrupt(icpu);
                return;
            }

            if (operand == 0u) {
                divide_interrupt(icpu);
                return;
            }

            if (word) {
                if (signed_op) {
                    const int32_t dividend2 = (int32_t) (((uint32_t) DX << 16) | AX);
                    const int32_t divisor = (int16_t) operand;
                    if ((divisor == 0) || ((dividend2 == INT32_MIN) && (divisor == -1))) {
                        divide_interrupt(icpu);
                        return;
                    }

                    int32_t q = dividend2 / divisor;
                    int32_t r = dividend2 % divisor;

                    if (icpu->rep_prefix != 0u) q = -q;

                    if (q < INT16_MIN || q > INT16_MAX) {
                        divide_interrupt(icpu);
                        return;
                    }

                    AX = (uint16_t) (int16_t) q;
                    DX = (uint16_t) (int16_t) r;
                } else {
                    const uint32_t dividend2 = ((uint32_t)DX << 16) | AX;

                    uint32_t q = dividend2 / operand;

                    const uint32_t r = dividend2 % operand;

                    if (icpu->rep_prefix != 0u)
                        q = (uint16_t) (-(int32_t) q);

                    if (q > 0xffffu) {
                        divide_interrupt(icpu);
                        return;
                    }

                    AX = (uint16_t) q;
                    DX = (uint16_t) r;
                }
            } else {
                if (signed_op) {
                    const int16_t dividend2 = (int16_t)AX;
                    const int16_t divisor = (int8_t)operand;

                    if ((divisor == 0) || ((dividend2 == INT16_MIN) && (divisor == -1))) {
                        divide_interrupt(icpu);
                        return;
                    }

                    int16_t q = (int16_t)(dividend2 / divisor);

                    const int16_t r = (int16_t)(dividend2 % divisor);

                    if (icpu->rep_prefix != 0u)
                        q = (int16_t) -q;

                    if ((q < INT8_MIN) || (q > INT8_MAX)) {
                        divide_interrupt(icpu);
                        return;
                    }

                    AL = (uint8_t) (int8_t) q;
                    AH = (uint8_t) (int8_t) r;
                } else {
                    uint16_t q = (uint16_t)(AX / (uint8_t) operand);

                    const uint16_t r = (uint16_t)(AX % (uint8_t) operand);

                    if (icpu->rep_prefix != 0u)
                        q = (uint8_t)(-(int16_t)q);

                    if (q > 0xffu) {
                        divide_interrupt(icpu);
                        return;
                    }

                    AL = (uint8_t) q;
                    AH = (uint8_t) r;
                }
            }

            break;
        }
    }
}

static void
execute_group45(m808x_cpu_t *icpu, const uint8_t iopcode)
{
    const bool     word    = iopcode == 0xffu;
    const unsigned subop   = icpu->ins.reg;
    uint16_t       operand = 0u;

    /* Far CALL/JMP consume a 32-bit pointer and must not perform the generic
     * eager r/m read first.  All other group-4/5 operations use one operand. */
    if (subop != 3u && subop != 5u)
        operand = read_rm(icpu, word);

    switch (subop) {
        default:
            return;
        case 0u: case 1u: {
            const uint16_t result = alu_incdec(icpu, operand, subop == 1u, word);

            biu_cycle_i(icpu, 0x020u, NULL);

            if (rm_is_memory(icpu))
                biu_cycle_i(icpu, 0x021u, NULL);

            write_rm(icpu, word, result);
            break;
        }
        case 2u: { /* near call */
            if (!rm_is_memory(icpu))
                biu_cycle_i(icpu, 0x074u, NULL);

            biu_fetch_suspend(icpu);
            CYCLES_MC(icpu, 0x074u, 0x075u);
            corr(icpu);
            biu_cycle_i(icpu, 0x076u, NULL);
            const uint16_t return_ip = icpu->pc;
            icpu->pc = operand;
            biu_queue_flush(icpu);
            CYCLES_MC(icpu, 0x077u, 0x078u, 0x079u);

            if (word)
                push_u16(icpu, return_ip);
            else {
                SP = (uint16_t)(SP - 2u);
                biu_write_u8(icpu, SEG_SS, SP, (uint8_t)return_ip);
            }
            break;
        }
        case 3u: {
            /* far call */
            uint16_t off;
            uint16_t seg;

            if (rm_is_memory(icpu)) {
                off = read_rm(icpu, true);
                biu_cycle_i(icpu, 0x068u, NULL);
                seg = biu_read_u16(icpu, icpu->ins.ea_segment, (uint16_t)(icpu->ins.ea + 2u));
            } else {
                const m808x_segment_t default_seg = icpu->ins.has_segment_override ? icpu->ins.segment_override : SEG_DS;

                biu_cycle_i(icpu, 0x069u, NULL);
                off = 0x0004u;
                seg = biu_read_u16(icpu, default_seg, off);
                biu_cycle_i(icpu, 0x06au, NULL);
            }

            farcall(icpu, seg, off, true);
            break;
        }
        case 4u: { /* near jump */
            if (!rm_is_memory(icpu))
                biu_cycle(icpu);

            biu_fetch_suspend(icpu);
            biu_cycle_i(icpu, 0x0d8u, NULL);
            icpu->pc = operand;
            biu_queue_flush(icpu);
            break;
        }
        case 5u: { /* far jump */
            uint16_t off;
            uint16_t seg;

            if (rm_is_memory(icpu)) {
                off = read_rm(icpu, true);
                biu_cycle_i(icpu, 0x0dcu, NULL);
                biu_fetch_suspend(icpu);
                biu_cycle_i(icpu, 0x0ddu, NULL);
                seg = biu_read_u16(icpu, icpu->ins.ea_segment, (uint16_t)(icpu->ins.ea + 2u));
            } else {
                const m808x_segment_t default_seg = icpu->ins.has_segment_override ? icpu->ins.segment_override : SEG_DS;
                biu_cycle(icpu);
                biu_fetch_suspend(icpu);
                biu_cycle(icpu);
                off = 0x0004u;
                seg = biu_read_u16(icpu, default_seg, off);
            }

            icpu->segs[SEG_CS] = seg;
            icpu->pc = off;

            biu_queue_flush(icpu);
            break;
        }
        case 6u: case 7u: { /* PUSH, /7 alias */
            CYCLES_MC(icpu, 0x024u, 0x025u, 0x026u);
            SP = (uint16_t)(SP - 2u);
            /* On the original 8086/8088, every encoding that reads SP as the
             * source of PUSH observes it after the predecrement.  The generic
             * r/m read happened earlier, so repair FF /6 and its /7 alias here. */
            const uint16_t pushed = word && !rm_is_memory(icpu) && (icpu->ins.rm == 4u) ? SP : operand;

            if (word)
                biu_write_u16(icpu, SEG_SS, SP, pushed);
            else
                biu_write_u8(icpu, SEG_SS, SP, (uint8_t) pushed);
            break;
        }
    }
}

static bool
execute_instruction(m808x_cpu_t *icpu)
{
    const uint8_t iopcode = icpu->ins.opcode;

    if (icpu->nx) {
        biu_cycle_i(icpu, MC_RNI, "loaded RNI");
        icpu->nx = false;
    } else if (icpu->biu.queue.last_op == QOP_FIRST)
        biu_cycle(icpu);

    if ((iopcode <= 0x3du) && ((iopcode & 7u) <= 5u)) {
        if ((iopcode & 7u) <= 3u)
            execute_alu_rm_reg(icpu, iopcode);
        else
            execute_alu_acc_imm(icpu, iopcode);

        return !icpu->fatal;
    }

    if ((iopcode >= 0x40u )&& (iopcode <= 0x4fu)) {
        const unsigned reg = iopcode & 7u;
        set_reg16(icpu, reg, alu_incdec(icpu, get_reg16(icpu, reg), iopcode >= 0x48u, true));
        return true;
    }

    if ((iopcode >= 0x50u) && (iopcode <= 0x57u)) {
        push_reg16(icpu, iopcode & 7u);
        return !icpu->fatal;
    }

    if ((iopcode >= 0x58u) && (iopcode <= 0x5fu)) {
        pop_reg16(icpu, iopcode & 7u);
        return !icpu->fatal;
    }

    if ((iopcode >= 0x60u) && (iopcode <= 0x7fu)) {
        const bool taken = condition_true(icpu, iopcode);
        const int8_t rel = (int8_t)queue_read(icpu, false);
        biu_cycle_i(icpu, 0x0e9u, NULL);

        if (taken)
            reljmp2(icpu, rel, true);

        return !icpu->fatal;
    }

    if ((iopcode >= 0xb0u) && (iopcode <= 0xb7u)) {
        const uint8_t imm = queue_read(icpu, false);
        biu_cycle_i(icpu, MC_JUMP, NULL);
        set_reg8(icpu, iopcode & 7u, imm);
        return true;
    }

    if ((iopcode >= 0xb8u) && (iopcode <= 0xbfu)) {
        set_reg16(icpu, iopcode & 7u, read_queue_u16(icpu));
        return true;
    }

    switch (iopcode) {
        case 0x06u: push_segment(icpu, SEG_ES); return !icpu->fatal;
        case 0x07u: pop_segment(icpu, SEG_ES); return !icpu->fatal;
        case 0x0eu: push_segment(icpu, SEG_CS); return !icpu->fatal;
        case 0x0fu: pop_segment(icpu, SEG_CS); return !icpu->fatal;
        case 0x16u: push_segment(icpu, SEG_SS); return !icpu->fatal;
        case 0x17u: pop_segment(icpu, SEG_SS); return !icpu->fatal;
        case 0x1eu: push_segment(icpu, SEG_DS); return !icpu->fatal;
        case 0x1fu: pop_segment(icpu, SEG_DS); return !icpu->fatal;

        case 0x27u:
            CYCLES_MC(icpu, 0x144u, 0x145u);
            do_daa(icpu);
            return true;
        case 0x2fu:
            CYCLES_MC(icpu, 0x144u, 0x145u);
            do_das(icpu);
            return true;
        case 0x37u: do_aaa(icpu); return true;
        case 0x3fu: do_aas(icpu); return true;

        case 0x80u:
        case 0x81u:
        case 0x82u:
        case 0x83u:
            execute_group1(icpu, iopcode);
            return !icpu->fatal;

        case 0x84u:
        case 0x85u: {
            const bool word = (iopcode & 1u) != 0u;
            const uint16_t lhs = read_rm(icpu, word);
            const uint16_t rhs = read_reg_field(icpu, word);
            (void)alu_binary(icpu, ALU_AND, lhs, rhs, word);
            biu_cycle_i(icpu, 0x094u, NULL);
            return !icpu->fatal;
        }

        case 0x86u:
        case 0x87u: {
            const bool word = (iopcode & 1u) != 0u;
            const uint16_t rm_value = read_rm(icpu, word);
            const uint16_t reg_value = read_reg_field(icpu, word);
            CYCLES_MC(icpu, 0x0a4u, 0x0a5u);
            if (rm_is_memory(icpu)) {
                icpu->in_lock = true;
                CYCLES_MC(icpu, 0x0a6u, 0x0a7u);
            }
            write_reg_field(icpu, word, rm_value);
            write_rm(icpu, word, reg_value);
            return !icpu->fatal;
        }

        case 0x88u:
        case 0x89u:
        case 0x8au:
        case 0x8bu:
            execute_mov_rm_reg(icpu, iopcode);
            return !icpu->fatal;

        case 0x8cu: {
            const uint16_t value = segment_value(icpu, icpu->ins.reg);
            if (rm_is_memory(icpu)) { ea_done_return(icpu); biu_cycle_i(icpu, 0x0ecu, NULL); }
            write_rm(icpu, true, value);
            return !icpu->fatal;
        }

        case 0x8du:
            if (rm_is_memory(icpu)) ea_done_return(icpu);
            write_reg_field(icpu, true, rm_is_memory(icpu) ? icpu->ins.ea : icpu->ea_addr);
            return true;

        case 0x8eu: {
            const uint16_t value = read_rm(icpu, true);
            set_segment_value(icpu, icpu->ins.reg, value);
            return !icpu->fatal;
        }

        case 0x8fu: {
            if (rm_is_memory(icpu)) ea_done_return(icpu);
            biu_cycle_i(icpu, 0x040u, NULL);
            const uint16_t value = pop_u16(icpu);
            biu_cycle_i(icpu, 0x042u, NULL);
            if (rm_is_memory(icpu)) CYCLES_MC(icpu, 0x043u, 0x044u);
            write_rm(icpu, true, value);
            return !icpu->fatal;
        }

        case 0x90u: case 0x91u: case 0x92u: case 0x93u:
        case 0x94u: case 0x95u: case 0x96u: case 0x97u: {
            const unsigned reg = iopcode & 7u;
            const uint16_t old_ax = AX;
            const uint16_t old_reg = get_reg16(icpu, reg);
            biu_cycle_i(icpu, 0x084u, NULL);
            AX = old_reg;
            set_reg16(icpu, reg, old_ax);
            return true;
        }

        case 0x98u:
            AH = (AL & 0x80u) != 0u ? 0xffu : 0u;
            return true;

        case 0x99u:
            CYCLES_MC(icpu, 0x058u, 0x059u, 0x05au);
            if ((AX & 0x8000u) != 0u) {
                biu_cycle_i(icpu, MC_JUMP, NULL);
                DX = 0xffffu;
            } else
                DX = 0u;
            return true;

        case 0x9au: {
            const uint16_t off = read_queue_u16(icpu);
            const uint16_t seg = read_queue_u16(icpu);
            farcall(icpu, seg, off, true);
            return !icpu->fatal;
        }

        case 0x9bu:
            CYCLES_MC(icpu, 0x0f8u, 0x0f9u, 0x0fau);
            if (icpu->test_pin) icpu->waiting = true;
            return true;

        case 0x9cu:
            CYCLES_MC(icpu, 0x030u, 0x031u, 0x032u);
            push_u16(icpu, icpu->flags);
            return !icpu->fatal;

        case 0x9du:
            pop_flags(icpu);
            return !icpu->fatal;

        case 0x9eu:
            CYCLES_MC(icpu, 0x100u, 0x101u);
            icpu->flags = (uint16_t) ((icpu->flags & 0xff02u) | (AH & 0xd5u));
            return true;

        case 0x9fu:
            AH = (uint8_t) ((icpu->flags & 0xd5u) | 0x02u);
            return true;

        case 0xa0u:
        case 0xa1u: {
            const bool word = (iopcode & 1u) != 0u;
            const uint16_t off = read_queue_u16(icpu);
            const m808x_segment_t seg = icpu->ins.has_segment_override ? icpu->ins.segment_override : SEG_DS;
            if (word) AX = biu_read_u16(icpu, seg, off); else AL = biu_read_u8(icpu, seg, off);
            return !icpu->fatal;
        }

        case 0xa2u:
        case 0xa3u: {
            const bool word = (iopcode & 1u) != 0u;
            const uint16_t off = read_queue_u16(icpu);
            biu_cycle_i(icpu, 0x064u, NULL);
            const m808x_segment_t seg = icpu->ins.has_segment_override ? icpu->ins.segment_override : SEG_DS;
            if (word) biu_write_u16(icpu, seg, off, AX); else biu_write_u8(icpu, seg, off, AL);
            return !icpu->fatal;
        }

        case 0xa4u: case 0xa5u: case 0xa6u: case 0xa7u:
        case 0xaau: case 0xabu: case 0xacu: case 0xadu:
        case 0xaeu: case 0xafu:
            execute_string(icpu, iopcode);
            return !icpu->fatal;

        case 0xa8u:
        case 0xa9u: {
            const bool word = (iopcode & 1u) != 0u;
            const uint16_t imm = word ? read_queue_u16(icpu) : queue_read(icpu, false);
            if (!word) biu_cycle_i(icpu, MC_JUMP, NULL);
            (void)alu_binary(icpu, ALU_AND, word ? AX : AL, imm, word);
            return true;
        }

        case 0xc0u:
        case 0xc2u:
        case 0xc8u:
        case 0xcau: {
            const uint16_t release = read_queue_u16(icpu);
            farret(icpu, (iopcode & 8u) != 0u);
            biu_cycle_i(icpu, 0x0ceu, NULL);
            SP = (uint16_t)(SP + release);
            return !icpu->fatal;
        }

        case 0xc1u:
        case 0xc3u: {
            icpu->pc = pop_u16(icpu);
            biu_fetch_suspend(icpu);
            biu_cycle_i(icpu, 0x0bdu, NULL);
            biu_queue_flush(icpu);
            CYCLES_MC(icpu, 0x0beu, 0x0bfu);
            return !icpu->fatal;
        }

        case 0xc4u:
        case 0xc5u: {
            uint16_t off;
            uint16_t seg;
            if (rm_is_memory(icpu)) {
                /* EALOAD has already obtained the offset word before the LES/LDS
                 * micro-routine; only the segment word at EA+2 is read here. */
                off = read_rm(icpu, true);
                CYCLES_MC(icpu, iopcode == 0xc4u ? 0x0f0u : 0x0f4u,
                               iopcode == 0xc4u ? 0x0f1u : 0x0f5u);
                seg = biu_read_u16(icpu, icpu->ins.ea_segment, (uint16_t)(icpu->ins.ea + 2u));
            } else {
                /* Invalid register form follows the uninitialised IND path.
                 * A fresh core has last-EA zero; retain ea_addr as that latch. */
                CYCLES_MC(icpu, iopcode == 0xc4u ? 0x0f0u : 0x0f4u,
                               iopcode == 0xc4u ? 0x0f1u : 0x0f5u);
                const m808x_segment_t dseg = icpu->ins.has_segment_override ? icpu->ins.segment_override : SEG_DS;
                off = biu_read_u16(icpu, dseg, icpu->ea_addr);
                seg = biu_read_u16(icpu, dseg, (uint16_t)(icpu->ea_addr + 2u));
            }
            write_reg_field(icpu, true, off);
            icpu->segs[iopcode == 0xc4u ? SEG_ES : SEG_DS] = seg;
            return !icpu->fatal;
        }

        case 0xc6u:
        case 0xc7u: {
            const bool word = (iopcode & 1u) != 0u;
            /* Write-only ModR/M operands take EADONE before the immediate
             * bytes are consumed by the instruction micro-routine. */
            if (rm_is_memory(icpu))
                ea_done_return(icpu);

            const uint16_t imm = word ? read_queue_u16(icpu) : queue_read(icpu, false);

            if (!word)
                biu_cycle_i(icpu, MC_JUMP, NULL);

            if (rm_is_memory(icpu))
                biu_cycle_i(icpu, 0x016u, NULL);

            write_rm(icpu, word, imm);
            return !icpu->fatal;
        }

        case 0xc9u:
        case 0xcbu:
            biu_cycle_i(icpu, 0x0c0u, NULL);
            farret(icpu, true);
            return !icpu->fatal;

        case 0xccu:
            CYCLES_MC(icpu, 0x1b1u, 0x1b2u, MC_JUMP);
            software_interrupt(icpu, 3u);
            return !icpu->fatal;

        case 0xcdu: {
            const uint8_t vector = queue_read(icpu, false);
            software_interrupt(icpu, vector);
            return !icpu->fatal;
        }

        case 0xceu:
            if ((icpu->flags & V_FLAG) != 0u) {
                CYCLES_MC(icpu, 0x1acu, 0x1adu, MC_JUMP, 0x1afu);
                software_interrupt(icpu, 4u);
            } else
                CYCLES_MC(icpu, 0x1acu, 0x1adu);
            return !icpu->fatal;

        case 0xcfu:
            biu_cycle_i(icpu, 0x0c8u, NULL);
            farret(icpu, true);
            pop_flags(icpu);
            biu_cycle_i(icpu, 0x0cau, NULL);
            m808x_86box_iret_complete();
            return !icpu->fatal;

        case 0xd0u:
        case 0xd1u:
        case 0xd2u:
        case 0xd3u:
            execute_shift_group(icpu, iopcode);
            return !icpu->fatal;

        case 0xd4u: {
            const uint8_t base = queue_read(icpu, false);
            CYCLES_MC(icpu, 0x175u, 0x176u, MC_JUMP);
            const m808x_cord_result_t aam_cord = cord_cycles(icpu, 8u, 0u, base, AL);
            if (!aam_cord.ok) {
                divide_interrupt(icpu);
                return !icpu->fatal;
            }
            AH = (uint8_t)(AL / base);
            AL = (uint8_t)(AL % base);
            biu_cycle_i(icpu, 0x177u, NULL);
            set_szp8(icpu, AL);
            icpu->flags &= (uint16_t)~(A_FLAG | C_FLAG | V_FLAG);
            return true;
        }

        case 0xd5u: {
            const uint8_t base = queue_read(icpu, false);
            CYCLES_MC(icpu, 0x170u, 0x171u, MC_JUMP);
            const mul_tmp_t aad_product = mul_corx_cycles(icpu, 8u, AH, base, false);
            /* AAD's final ADD is F-marked.  It replaces CORX's C/A/O with
             * those from AL + product, then the following microflow leaves
             * S/Z/P from the final AL value. */
            AL = (uint8_t)alu_binary(icpu, ALU_ADD, AL, aad_product.c, false);
            AH = 0u;
            CYCLES_MC(icpu, 0x172u, 0x173u);
            set_szp8(icpu, AL);
            return true;
        }

        case 0xd6u:
            biu_cycle_i(icpu, 0x0a0u, NULL);
            if ((icpu->flags & C_FLAG) != 0u) {
                biu_cycle_i(icpu, MC_JUMP, NULL);
                AL = 0xffu;
            } else
                AL = 0u;
            return true;

        case 0xd7u: {
            const m808x_segment_t seg = icpu->ins.has_segment_override ? icpu->ins.segment_override : SEG_DS;
            CYCLES_MC(icpu, 0x10cu, 0x10du, 0x10eu);
            AL = biu_read_u8(icpu, seg, (uint16_t)(BX + AL));
            return !icpu->fatal;
        }

        case 0xd8u: case 0xd9u: case 0xdau: case 0xdbu:
        case 0xdcu: case 0xddu: case 0xdeu: case 0xdfu:
            if (rm_is_memory(icpu))
                (void)read_rm(icpu, true);

            m808x_86box_export_arch_state(icpu);
            m808x_86box_fpu_exec(iopcode, icpu->ins.modrm, icpu->ins.ea,
                                 (uint8_t)icpu->ins.ea_segment);
            m808x_86box_import_register_state(icpu);
            return !icpu->fatal;

        case 0xe0u:
        case 0xe1u: {
            CX = (uint16_t)(CX - 1u);
            CYCLES_MC(icpu, 0x138u, 0x139u);
            const int8_t rel = (int8_t)queue_read(icpu, false);
            const bool is_loope = (iopcode & 1u) != 0u;
            const bool zf = (icpu->flags & Z_FLAG) != 0u;

            if (is_loope != zf)
                biu_cycle_i(icpu, MC_JUMP, NULL);
            else if (CX != 0u) {
                biu_cycle_i(icpu, 0x13bu, NULL);
                reljmp2(icpu, rel, true);
            }

            return !icpu->fatal;
        }

        case 0xe2u: {
            CX = (uint16_t)(CX - 1u);
            CYCLES_MC(icpu, 0x140u, 0x141u);
            const int8_t rel = (int8_t)queue_read(icpu, false);
            if (CX != 0u) reljmp2(icpu, rel, true); else biu_cycle(icpu);
            return !icpu->fatal;
        }

        case 0xe3u: {
            CYCLES_MC(icpu, 0x134u, 0x135u);
            const int8_t rel = (int8_t)queue_read(icpu, false);
            if (CX != 0u) biu_cycle_i(icpu, MC_JUMP, NULL); else { biu_cycle_i(icpu, 0x137u, NULL); reljmp2(icpu, rel, true); }
            return !icpu->fatal;
        }

        case 0xe4u:
        case 0xe5u: {
            const uint8_t port = queue_read(icpu, false);
            biu_cycle_i(icpu, 0x0adu, NULL);
            if (iopcode & 1u) AX = biu_io_read_u16(icpu, port); else AL = biu_io_read_u8(icpu, port);
            return !icpu->fatal;
        }

        case 0xe6u:
        case 0xe7u: {
            const uint8_t port = queue_read(icpu, false);
            CYCLES_MC(icpu, 0x0b1u, 0x0b2u);
            if (iopcode & 1u) biu_io_write_u16(icpu, port, AX); else biu_io_write_u8(icpu, port, AL);
            return !icpu->fatal;
        }

        case 0xe8u: {
            const int16_t rel = (int16_t)read_queue_u16(icpu);
            biu_fetch_suspend(icpu);
            CYCLES_MC(icpu, 0x07eu, 0x07fu);
            corr(icpu);
            biu_cycle_i(icpu, 0x080u, NULL);
            const uint16_t return_ip = icpu->pc;
            icpu->pc = (uint16_t)(icpu->pc + rel);
            biu_queue_flush(icpu);
            CYCLES_MC(icpu, 0x081u, 0x082u, MC_JUMP);
            push_u16(icpu, return_ip);
            return !icpu->fatal;
        }

        case 0xe9u:
            reljmp2(icpu, (int16_t)read_queue_u16(icpu), false);
            return !icpu->fatal;

        case 0xeau: {
            const uint16_t off = read_queue_u16(icpu);
            const uint16_t seg = read_queue_u16(icpu);
            biu_fetch_suspend(icpu);
            CYCLES_MC(icpu, 0x0e4u, 0x0e5u);
            icpu->segs[SEG_CS] = seg;
            icpu->pc = off;
            biu_queue_flush(icpu);
            biu_cycle_i(icpu, 0x0e6u, NULL);
            return !icpu->fatal;
        }

        case 0xebu:
            reljmp2(icpu, (int8_t)queue_read(icpu, false), true);
            return !icpu->fatal;

        case 0xecu:
        case 0xedu:
            if (iopcode & 1u)
                AX = biu_io_read_u16(icpu, DX);
            else
                AL = biu_io_read_u8(icpu, DX);
            return !icpu->fatal;

        case 0xeeu:
        case 0xefu:
            biu_cycle_i(icpu, 0x0b8u, NULL);
            if (iopcode & 1u)
                biu_io_write_u16(icpu, DX, AX);
            else
                biu_io_write_u8(icpu, DX, AL);
            return !icpu->fatal;

        case 0xf4u:
            biu_bus_wait_halt(icpu);
            biu_fetch_halt(icpu);
            biu_bus_wait_finish(icpu);
            if (icpu->intr_pin && (icpu->flags & I_FLAG) != 0u)
                CYCLES_MC(icpu, MC_JUMP, 0x1f0u);
            else {
                icpu->halted = true;
                biu_halt(icpu);
            }
            return !icpu->fatal;

        case 0xf5u: icpu->flags ^= C_FLAG; return true;

        case 0xf6u:
        case 0xf7u:
            execute_group3(icpu, iopcode);
            return !icpu->fatal;

        case 0xf8u: icpu->flags &= (uint16_t)~C_FLAG; return true;
        case 0xf9u: icpu->flags |= C_FLAG; return true;
        case 0xfau: icpu->flags &= (uint16_t)~I_FLAG; return true;
        case 0xfbu:
            icpu->flags |= I_FLAG;
            icpu->interrupt_shadow = 1u;
            return true;
        case 0xfcu: icpu->flags &= (uint16_t)~D_FLAG; return true;
        case 0xfdu: icpu->flags |= D_FLAG; return true;

        case 0xfeu:
        case 0xffu:
            execute_group45(icpu, iopcode);
            return !icpu->fatal;

        default:
            fprintf(stderr, "internal decode hole for opcode %02X at %04X:%04X\n",
                    iopcode, icpu->segs[SEG_CS], icpu->ins.instruction_ip);
            icpu->fatal = true;
            return false;
    }
}

static void
finish_instruction(m808x_cpu_t *icpu)
{
    bool interrupt_blocked = false;
    bool trap_blocked = false;
    if (icpu->interrupt_shadow > 0u) {
        icpu->interrupt_shadow--;
        interrupt_blocked = true;
    }
    if (icpu->trap_shadow > 0u) {
        icpu->trap_shadow--;
        trap_blocked = true;
    }
    const bool trap_pending = !trap_blocked &&
        (((icpu->flags & T_FLAG) != 0u) || (icpu->trap_disable_delay != 0u));
    if (icpu->trap_disable_delay > 0u)
        icpu->trap_disable_delay--;

    if (icpu->nmi_pin) {
        icpu->interrupt_vector = 2u;
        hardware_interrupt(icpu, false);
    } else if (icpu->intr_pin && ((icpu->flags & I_FLAG) != 0u) && !interrupt_blocked)
        hardware_interrupt(icpu, true);
    else if (trap_pending) {
        CYCLES_MC(icpu, 0x198u, MC_JUMP);
        interrupt_routine(icpu, 1u, true);
    } else if (!icpu->halted && !icpu->waiting)
        biu_fetch_next(icpu);

    icpu->in_lock = false;
    icpu->rep_prefix = 0u;
}

static void
reset_cpu(m808x_cpu_t *icpu)
{
    i808x_hook_prefetch_queue(m808x_86box_pfq_set_pos, m808x_86box_pfq_set_ip, m808x_86box_pfq_set_prefetching,
                              m808x_86box_pfq_get_pos, m808x_86box_pfq_get_ip, m808x_86box_pfq_get_prefetching,
                              m808x_86box_pfq_get_size, m808x_86box_wait);

    memset(icpu->regs, 0, sizeof(icpu->regs));
    icpu->flags = 0xf002u;
    icpu->segs[SEG_ES] = 0u;
    icpu->segs[SEG_CS] = 0xffffu;
    icpu->segs[SEG_SS] = 0u;
    icpu->segs[SEG_DS] = 0u;
    icpu->pc = 0u;
    icpu->ea_seg = SEG_DS;
    icpu->mc_line = MC_NONE;

    memset(&icpu->biu, 0, sizeof(icpu->biu));
    icpu->biu.t_cycle = T_I;
    icpu->biu.ta_cycle = TA_DONE;
    icpu->biu.bus_status = BUS_PASSIVE;
    icpu->biu.bus_status_latch = BUS_PASSIVE;
    icpu->biu.pl_status = BUS_PASSIVE;
    icpu->biu.bus_segment = SEG_NONE;
    icpu->biu.fetch.kind = FETCH_SUSPENDED;
    queue_reset(&icpu->biu.queue, icpu->is_8086);

    /* Intel specifies RESET high for at least four clocks. The two additional
     * internal clocks retain the original test harness's measured startup
     * sequence before FLUSH launches the first reset-vector fetch. */
    for (unsigned i = 0; i < 6u; i++) {
        biu_cycle_i(icpu, MC_NONE, "RESET");
    }
    biu_queue_flush(icpu);
}

/* -------------------------------------------------------------------------
 * 86Box ABI adapter
 * ------------------------------------------------------------------------- */

static bool m808x_initialized = false;
static bool m808x_suppress_host_cycles = false;
static int m808x_restore_pfq_pos = -1;
static uint16_t m808x_restore_pfq_ip = 0u;
static bool m808x_restore_pfq_ip_valid = false;

static uint16_t
m808x_arch_ip(void)
{
    return architectural_ip(&m808x_cpu);
}

bool
m808x_86box_should_use(void)
{
    static int cached = -1;

    if (cached < 0) {
        const char *legacy = getenv("86BOX_LEGACY_808X");
        cached = (legacy && *legacy && strcmp(legacy, "0") != 0) ? 0 : 1;
    }

    return cached != 0 && !is186 && !is_nec;
}

bool
m808x_86box_active(void)
{
    return m808x_initialized && m808x_86box_should_use();
}

static void
m808x_host_cycle(void)
{
    if (m808x_suppress_host_cycles)
        return;

    cpu_state._cycles--;
    tsc += ((uint64_t)xt_cpu_multi >> 32ULL);
    if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint64_t)tsc))
        timer_process();
}

static bool
m808x_host_override_vector(const uint8_t vector, uint16_t *ip, uint16_t *segp)
{
    if ((vector == 2u) && use_custom_nmi_vector) {
        *ip = (uint16_t)(custom_nmi_vector & 0xffffu);
        *segp = (uint16_t)(custom_nmi_vector >> 16);

        return true;
    }
    return false;
}

static void
m808x_set_seg(x86seg *dst, const uint16_t selector)
{
    dst->seg = selector;
    dst->base = (uint32_t) selector << 4;
    dst->limit = 0xffffu;
    dst->limit_low = 0u;
    dst->limit_high = 0xffffu;
    dst->access = 0;
    dst->ar_high = 0;
    dst->checked = 1;
}

void
m808x_86box_export_arch_state(const m808x_cpu_t *icpu)
{
    for (unsigned i = 0; i < 8u; ++i)
        cpu_state.regs[i].w = icpu->regs[i].x;

    cpu_state.flags = (uint16_t)((icpu->flags & 0x0fd7u) | 0x0002u);
    m808x_set_seg(&cpu_state.seg_es, icpu->segs[SEG_ES]);
    m808x_set_seg(&cpu_state.seg_cs, icpu->segs[SEG_CS]);
    m808x_set_seg(&cpu_state.seg_ss, icpu->segs[SEG_SS]);
    m808x_set_seg(&cpu_state.seg_ds, icpu->segs[SEG_DS]);
    cpu_state.pc = architectural_ip(icpu);
    cpu_state.eaaddr = icpu->ea_addr;
    in_lock = icpu->in_lock ? 1 : 0;
    pfq_pos = (int) queue_effective_len(&icpu->biu.queue);
}

void
m808x_86box_import_register_state(m808x_cpu_t *icpu)
{
    for (unsigned i = 0; i < 8u; ++i)
        icpu->regs[i].x = cpu_state.regs[i].w;
    icpu->flags = (uint16_t)((cpu_state.flags & 0x0fd7u) | 0xf002u);
}

static void
m808x_import_arch_state(void)
{
    m808x_86box_import_register_state(&m808x_cpu);
    m808x_cpu.segs[SEG_ES] = cpu_state.seg_es.seg;
    m808x_cpu.segs[SEG_CS] = cpu_state.seg_cs.seg;
    m808x_cpu.segs[SEG_SS] = cpu_state.seg_ss.seg;
    m808x_cpu.segs[SEG_DS] = cpu_state.seg_ds.seg;

    const uint16_t external_ip = cpu_state.pc;

    if (external_ip != m808x_arch_ip()) {
        m808x_cpu.pc = external_ip;
        biu_fetch_suspend(&m808x_cpu);

        biu_queue_flush(&m808x_cpu);
    }
}

static void
m808x_update_input_pins(void)
{
    m808x_cpu.intr_pin = pic.int_pending != 0;
    m808x_cpu.nmi_pin = nmi && nmi_enable && nmi_mask;
    m808x_cpu.test_pin = m808x_86box_fpu_busy();
}

static void
m808x_consume_host_nmi(void)
{
    nmi_enable = 0;
#ifndef OLD_NMI_BEHAVIOR
    nmi = 0;
#endif
}

void
m808x_86box_reset(UNUSED(int hard))
{
    memset(&m808x_cpu, 0, sizeof(m808x_cpu));
    m808x_cpu.cycle_limit = UINT64_MAX;
    m808x_cpu.is_8086 = (is8086 != 0);
    m808x_cpu.trace = false;
    m808x_cpu.stop_on_halt = false;
    m808x_cpu.interrupt_vector = 8u;
    m808x_cpu.configured_wait_states = is_mazovia ? 1u : 0u;

    m808x_in_host_bus_callback = false;
#ifdef M808X_86BOX_TESTING
    m808x_test_captured_wait_clocks = 0;
    m808x_test_tw_clocks = 0;
#endif

    /* reset_cpu() emits internal clocks; clear stale external DMA state first. */
    m808x_dma_state = M808X_DMA_IDLE;
    m808x_dma_req = false;
    m808x_dma_holda = false;
    m808x_dma_ack = false;
    m808x_dma_aen = false;
    m808x_dma_operating_cycle = 0u;
    m808x_dma_wait_states = 0u;
    m808x_dma_wait_target = 4u;
    m808x_dma_ack_callback = NULL;
    m808x_dma_ack_opaque = NULL;

    m808x_suppress_host_cycles = true;
    reset_cpu(&m808x_cpu);
    m808x_suppress_host_cycles = false;
    m808x_cpu.cycle_num = 0u;
    m808x_cpu.instruction_count = 0u;
    m808x_restore_pfq_pos = -1;
    m808x_restore_pfq_ip = 0u;
    m808x_restore_pfq_ip_valid = false;
    m808x_initialized = true;

    rammask = 0xfffffu;
    use_custom_nmi_vector = 0u;
    custom_nmi_vector = 0u;
    m808x_86box_export_arch_state(&m808x_cpu);
}

void
m808x_86box_wait(int clocks, UNUSED(int bus))
{
    if (!m808x_86box_active() || clocks <= 0)
        return;
    for (int i = 0; i < clocks; ++i)
        biu_cycle(&m808x_cpu);
}

void
m808x_86box_external_sub_cycles(int clocks)
{
    if (clocks <= 0)
        return;

    if (m808x_in_host_bus_callback) {
        /* biu_do_bus_transfer() captures this deduction and converts it to
         * clocked Tw states, so do not also advance TSC here. */
        cpu_state._cycles -= clocks;
        return;
    }

    /* This path is retained for the rare out-of-band caller.  Bus callbacks
     * use the captured path above, including handlers that modify cycles
     * directly rather than calling sub_cycles(). */
    cpu_state._cycles -= clocks;
    tsc += (uint64_t)clocks * ((uint64_t)xt_cpu_multi >> 32ULL);

    /*
     * Do not dispatch timers recursively from a device callback. DMA devices
     * can charge cycles while timer_process() is already executing (the FDC
     * byte callback is one example). The outer timer_process() loop observes
     * the advanced TSC after the callback returns; an out-of-band caller is
     * caught by the next host cycle.
     */
}

bool
m808x_86box_dma_try_request_ex(unsigned wait_clocks,
                               void (*ack_callback)(void *opaque),
                               void *opaque)
{
    if (!m808x_86box_active() || wait_clocks == 0u)
        return false;

    /* Before DACK, req/callback identify the current level request and suppress
     * duplicate PIT edges. DACK clears both before invoking the callback, so a
     * new edge during S3/S4/release is a valid pending request. */
    if (m808x_dma_req || m808x_dma_ack_callback != NULL)
        return false;

    m808x_dma_wait_target = wait_clocks;
    m808x_dma_ack_callback = ack_callback;
    m808x_dma_ack_opaque = opaque;
    m808x_dma_req = true;
    return true;
}

bool
m808x_86box_dma_cancel_request_ex(void (*ack_callback)(void *opaque),
                                  void *opaque)
{
    if (!m808x_86box_active())
        return false;
    if (m808x_dma_ack || m808x_dma_holda || !m808x_dma_req)
        return false;
    if (m808x_dma_ack_callback != ack_callback ||
        m808x_dma_ack_opaque != opaque)
        return false;

    m808x_dma_req = false;
    m808x_dma_ack_callback = NULL;
    m808x_dma_ack_opaque = NULL;
    m808x_dma_wait_target = 4u;
    if (m808x_dma_state == M808X_DMA_DREQ ||
        m808x_dma_state == M808X_DMA_HRQ)
        m808x_dma_state = M808X_DMA_IDLE;
    return true;
}

void m808x_86box_dma_request_ex(unsigned wait_clocks,
                                void (*ack_callback)(void *opaque),
                                void *opaque)
{
    (void)m808x_86box_dma_try_request_ex(wait_clocks, ack_callback, opaque);
}

void m808x_86box_dma_request(unsigned wait_clocks)
{
    m808x_86box_dma_request_ex(wait_clocks, NULL, NULL);
}

void m808x_86box_refresh(void)
{
    m808x_86box_dma_request(4u);
}

uint64_t
m808x_86box_cycle_number(void)
{
    return m808x_86box_active() ? m808x_cpu.cycle_num : 0u;
}

void
m808x_86box_iret_complete(void)
{
    nmi_enable = 1;
}

void
execx86_new(int cycs)
{
    if (!m808x_initialized)
        m808x_86box_reset(1);

    cpu_state._cycles += cycs;
    m808x_import_arch_state();

    while (cpu_state._cycles > 0 && !m808x_cpu.fatal) {
        m808x_update_input_pins();

        if (m808x_cpu.waiting) {
            if (!m808x_cpu.test_pin) {
                m808x_cpu.waiting = false;
            } else {
                biu_cycle_i(&m808x_cpu, 0x0fbu, "WAIT idle");
                m808x_86box_export_arch_state(&m808x_cpu);
                continue;
            }
        }

        if (m808x_cpu.halted) {
            if (m808x_cpu.nmi_pin) {
                /* Unlike the normal instruction-boundary path, HLT wake-up
                 * bypasses finish_instruction(), so set the architectural NMI
                 * vector here instead of reusing a stale INTR/software vector. */
                m808x_cpu.interrupt_vector = 2u;
                hardware_interrupt(&m808x_cpu, false);
                m808x_consume_host_nmi();
                biu_fetch_next(&m808x_cpu);
            } else if (m808x_cpu.intr_pin && (m808x_cpu.flags & I_FLAG) != 0u) {
                hardware_interrupt(&m808x_cpu, true);
                biu_fetch_next(&m808x_cpu);
            } else {
                biu_cycle_i(&m808x_cpu, MC_NONE, "HALTED");
            }
            m808x_86box_export_arch_state(&m808x_cpu);
            continue;
        }

        cpu_state.oldpc = m808x_arch_ip();
        if (!decode_instruction(&m808x_cpu))
            break;
        opcode = m808x_cpu.ins.opcode;
        if (!execute_instruction(&m808x_cpu))
            break;
        ++m808x_cpu.instruction_count;

        const bool nmi_was_pending = m808x_cpu.nmi_pin;
        finish_instruction(&m808x_cpu);
        if (nmi_was_pending && !m808x_cpu.nmi_pin)
            m808x_consume_host_nmi();
        m808x_86box_export_arch_state(&m808x_cpu);

#ifdef USE_GDBSTUB
        if (gdbstub_instruction())
            break;
#endif
    }

    m808x_86box_export_arch_state(&m808x_cpu);
}

void m808x_86box_interrupt(uint16_t vector)
{
    if (!m808x_86box_active())
        return;
    software_interrupt(&m808x_cpu, (uint8_t)vector);
    m808x_86box_export_arch_state(&m808x_cpu);
}

static void m808x_rebuild_saved_prefetch_queue(void)
{
    if (!m808x_86box_active() || m808x_restore_pfq_pos < 0 || !m808x_restore_pfq_ip_valid)
        return;

    const unsigned max_len = m808x_cpu.is_8086 ? 6u : 4u;
    const unsigned len = (unsigned)m808x_restore_pfq_pos > max_len
        ? max_len : (unsigned)m808x_restore_pfq_pos;
    uint16_t arch_ip = cpu_state.pc;

    /* A normal queue snapshot satisfies fetch_ip - architectural_ip == len.
     * If an older state file did not restore PC first, derive the start from
     * the two queue fields instead. */
    if ((uint16_t)(m808x_restore_pfq_ip - arch_ip) != (uint16_t)len)
        arch_ip = (uint16_t)(m808x_restore_pfq_ip - (uint16_t)len);

    queue_flush(&m808x_cpu.biu.queue);
    for (unsigned i = 0; i < len; ++i) {
        const uint32_t addr = linear_address(m808x_cpu.segs[SEG_CS], (uint16_t)(arch_ip + i));
        (void)queue_push(&m808x_cpu.biu.queue, read_mem_b(addr));
    }
    m808x_cpu.pc = m808x_restore_pfq_ip;
}

void m808x_86box_pfq_set_pos(int pos)
{
    if (!m808x_86box_active())
        return;
    m808x_restore_pfq_pos = pos < 0 ? 0 : pos;
    m808x_rebuild_saved_prefetch_queue();
}

void m808x_86box_pfq_set_ip(uint16_t ip)
{
    if (!m808x_86box_active())
        return;
    m808x_restore_pfq_ip = ip;
    m808x_restore_pfq_ip_valid = true;
    m808x_cpu.pc = ip;
    m808x_rebuild_saved_prefetch_queue();
}

void m808x_86box_pfq_set_prefetching(int enabled)
{
    if (!m808x_86box_active())
        return;
    m808x_cpu.biu.fetch.kind = enabled ? FETCH_NORMAL : FETCH_SUSPENDED;
}

int m808x_86box_pfq_get_pos(void)
{
    return m808x_86box_active() ? (int)queue_effective_len(&m808x_cpu.biu.queue) : 0;
}

uint16_t m808x_86box_pfq_get_ip(void)
{
    return m808x_86box_active() ? m808x_cpu.pc : 0u;
}

int m808x_86box_pfq_get_prefetching(void)
{
    return m808x_86box_active() && fetch_enabled(&m808x_cpu);
}

int m808x_86box_pfq_get_size(void)
{
    return m808x_cpu.is_8086 ? 6 : 4;
}

#ifdef M808X_86BOX_TESTING
bool m808x_86box_test_halted(void)
{
    return m808x_86box_active() && m808x_cpu.halted;
}

uint64_t m808x_86box_test_captured_wait_clocks(void)
{
    return m808x_test_captured_wait_clocks;
}

uint64_t m808x_86box_test_tw_clocks(void)
{
    return m808x_test_tw_clocks;
}

unsigned m808x_86box_test__state(void)
{
    return (unsigned)m808x__state;
}

unsigned m808x_86box_test__wait_states(void)
{
    return m808x__wait_states;
}

bool m808x_86box_test_dma_holda(void)
{
    return m808x_dma_holda;
}

bool m808x_86box_test_dma_ack(void)
{
    return m808x_dma_ack;
}

bool m808x_86box_test_dma_aen(void)
{
    return m808x_dma_aen;
}

unsigned m808x_86box_test_t_cycle(void)
{
    return (unsigned)m808x_cpu.biu.t_cycle;
}

unsigned m808x_86box_test_bus_status(void)
{
    return (unsigned)m808x_cpu.biu.bus_status;
}
#endif
