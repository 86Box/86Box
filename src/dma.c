/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel DMA controllers.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/machine.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/m_ibm5140.h>
#include "808x_marty_86box.h"
#include <86box/plat_unused.h>

dma_t   dma[8];
uint8_t dma_e;
uint8_t dma_m;

static uint8_t  dmaregs[3][16];
static int      dma_wp[2];
static uint8_t  dma_stat;
static uint8_t  dma_stat_rq;
static uint8_t  dma_stat_rq_pc;
static uint8_t  dma_sw_req; /* the Request register: software requests, beside the DREQ lines in dma_stat_rq_pc */
static uint8_t  dma_stat_adv_pend;
static uint8_t  dma_command[2];
static uint8_t  dma_req_is_soft;
static uint8_t  dma_advanced;
static uint8_t  dma_at;
static uint8_t  dma_ibm5140;
static uint8_t  dma_ibm5140_diag;
static uint8_t  dma_buffer[65536];
static uint16_t dma_sg_base;
/* Scatter-gather status, per channel (82374EB 3.2.22). The data book's
   table leaves bit 1 reserved, but its text has a Terminate bit set by Stop
   and by the end of the list; bit 1 is where it is kept. */
#define DMA_SG_ACTIVE 0x01
#define DMA_SG_TERM   0x02
#define DMA_SG_CUR    0x04
#define DMA_SG_BASE   0x08
#define DMA_SG_EOP    0x20
#define DMA_SG_NNL    0x80
static uint8_t  dma_sg_int; /* channels whose list ended with IRQ13 */
/* How much of a scatter-gather descriptor is the byte count: sixteen bits
   on the PCI-ISA bridges, twenty-four on an EISA one (82374EB 6.6). */
static uint32_t dma_sg_count_mask = 0x0000fffe;
/* EISA buffer chaining and the ring buffer stop registers. */
static uint8_t  dma_chain_mode[8];
static uint8_t  dma_chain_int;
static uint8_t  dma_stop[8][3];
static uint8_t  dma_stop_en; /* bit 7 of each extended mode register */
static uint8_t  dma_eisa;
static uint16_t dma16_buffer[65536];
static uint32_t dma_mask;

static struct dma_ps2_t {
    int xfr_command;
    int xfr_channel;
    int byte_ptr;

    uint8_t arb_control;
    uint8_t arb_status;

    int is_ps2;
} dma_ps2;

/* 86BOX_XT8237_EXACT_INSTALLER_V1
 * NOCONA_XT_DMA_CONSOLIDATED_V1
 *
 * Cycle-aware Intel 8237 path for 8088/8086 PC/XT-class machines.
 *
 * The public 86Box DMA API is peripheral-pull rather than pin-clocked, so the
 * controller cannot expose every intermediate S0-S4 bus state. This state
 * machine nevertheless preserves the externally observable 8237 semantics:
 * request arbitration, fixed/rotating priority, demand/single/block service,
 * software requests, terminal count, EOP, auto-init, verify, address wrapping,
 * masks, status, the temporary register, and XT bus occupancy.
 */
typedef struct dma_xt8237_state_t {
    uint8_t sw_request;
    uint8_t demand_active;
    uint8_t block_active;
    uint8_t external_eop;
    uint8_t last_service;
    uint8_t temp;
    uint8_t dack;
    uint8_t in_mem_to_mem;
} dma_xt8237_state_t;

static dma_xt8237_state_t dma_xt8237;

/* 86BOX_MACHINE_EXACT_V1: PIT1 request latch, cleared by DACK0. */
static bool dma_xt_refresh_queued = false;
static bool dma_xt_refresh_scheduled = false;
static void dma_xt_refresh_try_schedule(void);
static void dma_xt_refresh_reconcile(void);

static int dma_xt8237_mem_to_mem(void);

/* Persistent override for machines where the usual is286-driven AT/XT DMA
   detection is wrong: a genuine single-8237 XT board paired with a 286-or-
   higher accelerator CPU (e.g. the Intel Inboard 386/PC on a stock 5150/
   5160). Set once via dma_set_force_xt() by the owning device at init time;
   every other machine's behavior is completely unchanged (dma_xt8237_active()
   still always returns 0, as before, when this is left at its default). */
static int dma_force_xt = 0;

void
dma_set_force_xt(int enable)
{
    dma_force_xt = enable;
}

/* True when this machine latches only the low 4 bits of a DMA page register,
   as the PC/XT does, rather than the full 8 bits an AT does. dma_at alone gets
   this wrong for an XT board fitted with a 286-or-higher accelerator: dma_at is
   assigned is286, so such a machine is handed a 24-bit DMA reach where the real
   hardware has 20-bit, and a driver placing its buffer above 1 MB appears to
   work in emulation while silently transferring from the wrong address. */
static int
dma_page_is_xt(void)
{
    return dma_force_xt || !dma_at;
}

int
dma_xt8237_active(void)
{
    if (dma_ibm5140)
        return 1;
#ifdef DMA_FORCE_REWRITE
    if (dma_force_xt)
        return !dma_advanced && !dma_ps2.is_ps2;
#endif
    return 0;
}

static uint8_t
dma_xt8237_raw_requests(void)
{
    return (dma_stat_rq_pc | dma_xt8237.sw_request) & 0x0f;
}

static uint8_t
dma_xt8237_service_requests(void)
{
    return (dma_xt8237_raw_requests() |
            dma_xt8237.demand_active |
            dma_xt8237.block_active) & 0x0f;
}

static int
dma_xt8237_priority_pick(uint8_t requests)
{
    int i;
    uint8_t owner;

    /* Software requests bypass the DREQ mask, but retain normal priority. */
    requests &= (uint8_t) (~dma_m | dma_xt8237.sw_request);
    requests &= dma_e & 0x0f;

    if (!requests || (dma_command[0] & 0x04))
        return -1;

    owner = requests &
            (dma_xt8237.demand_active | dma_xt8237.block_active);
    if (owner) {
        for (i = 0; i < 4; i++) {
            if (owner & (1 << i))
                return i;
        }
    }

    if (dma_command[0] & 0x10) {
        for (i = 1; i <= 4; i++) {
            int channel = (dma_xt8237.last_service + i) & 3;
            if (requests & (1 << channel))
                return channel;
        }
    } else {
        for (i = 0; i < 4; i++) {
            if (requests & (1 << i))
                return i;
        }
    }

    return -1;
}

static int
dma_xt8237_can_service(int channel)
{
    uint8_t requests;

    if ((channel < 0) || (channel > 3))
        return 0;
    if (dma_command[0] & 0x04)
        return 0;
    if (!(dma_e & (1 << channel)))
        return 0;
    if ((dma_m & ~dma_xt8237.sw_request) & (1 << channel))
        return 0;

    requests = dma_xt8237_service_requests();

    /*
     * Channel 0 refresh is the only request whose arbitration and DACK phase
     * are driven by the clocked Marty 8088 adapter. Existing 86Box devices
     * still use a synchronous peripheral-pull API: by the time a floppy,
     * sound, or storage device calls dma_channel_read/write(), that call
     * already represents its effective DACK/data phase and cannot be rejected
     * and retried later.
     *
     * Do not let another queued hardware request (especially PIT refresh on
     * channel 0) make a synchronous channel 1-3 callback return DMA_NODATA.
     * Software requests retain actual 8237 priority arbitration.
     */
    if ((channel != 0) && !dma_req_is_soft)
        requests &= (uint8_t) (1u << channel);

    if (!(requests & (1u << channel)))
        return 0;
    if (((dma[channel].mode >> 6) & 3) == 3)
        return 0;

    return dma_xt8237_priority_pick(requests) == channel;
}

static void
dma_xt8237_release_owner(int channel)
{
    uint8_t bit = 1 << channel;

    dma_xt8237.demand_active &= ~bit;
    dma_xt8237.block_active &= ~bit;
    dma_xt8237.dack &= ~bit;

    if (dma_command[0] & 0x10)
        dma_xt8237.last_service = channel;
}

static void
dma_xt8237_begin_service(int channel)
{
    uint8_t bit = 1 << channel;
    int service_mode = (dma[channel].mode >> 6) & 3;

    dma_xt8237.dack = bit;

    if (service_mode == 0)
        dma_xt8237.demand_active |= bit;
    else if (service_mode == 2)
        dma_xt8237.block_active |= bit;
}

static void
dma_xt8237_charge_bus(int channel)
{
    if (channel == 0) {
        /* The exact Marty adapter owns HOLD/HLDA and DMAWAIT. Calling
         * refreshread() here would add a second fixed four-clock charge. */
        if (!m808x_86box_active())
            is_nec ? refreshread_vx0() : refreshread();
    } else {
        /* Peripheral APIs remain synchronous. Preserve their established
         * occupancy until their data phases can be deferred to DACK. */
        sub_cycles((dma_command[0] & 0x08) ? 4 : 5);
    }
}


static void
dma_xt8237_advance_address(int channel)
{
    dma_t *dma_c = &dma[channel];

    if (dma_c->mode & 0x20)
        dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) |
                    ((dma_c->ac - 1) & 0xffff);
    else
        dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) |
                    ((dma_c->ac + 1) & 0xffff);
}

static int
dma_xt8237_finish_transfer(int channel)
{
    dma_t *dma_c = &dma[channel];
    uint8_t bit = 1 << channel;
    int service_mode = (dma_c->mode >> 6) & 3;
    int terminal = 0;

    dma_c->cc--;
    if (dma_c->cc < 0)
        terminal = 1;
    if (dma_xt8237.external_eop & bit)
        terminal = 1;

    if (terminal) {
        dma_stat |= bit;
        dma_xt8237.sw_request &= ~bit;
        dma_xt8237.external_eop &= ~bit;
        dma_stat_adv_pend &= ~bit;

        if (dma_c->mode & 0x10) {
            dma_c->cc = dma_c->cb;
            dma_c->ac = dma_c->ab;
        } else {
            dma_m |= bit;
        }

        dma_xt8237_release_owner(channel);
        return 1;
    }

    if (service_mode == 1) {
        dma_xt8237.dack &= ~bit;
        if (dma_command[0] & 0x10)
            dma_xt8237.last_service = channel;
    }

    return 0;
}

static void
dma_xt8237_master_clear(void)
{
    dma_wp[0] = 0;
    dma_command[0] = 0;
    dma_stat &= 0xf0;
    dma_stat_rq &= 0xf0;
    dma_stat_adv_pend &= 0xf0;
    dma_m |= 0x0f;

    memset(&dma_xt8237, 0, sizeof(dma_xt8237));
    dma_xt8237.last_service = 3;
    if (dma_ibm5140) {
        /* The integrated controller clears its standard channel registers,
         * unlike the discrete 8237 whose mode registers survive master clear. */
        for (int channel = 1; channel < 4; channel++) {
            dma[channel].mode = 0;
            dma[channel].ab = dma[channel].ac = 0;
            dma[channel].cb = dma[channel].cc = 0;
        }
    }

    /* Master clear resets the 8237, not external DREQ pins. Keep the physical
     * request bitmap and PIT1 latch intact; the caller reconciles/cancels any
     * pre-DACK CPU arbitration after masks and command state have changed. */
    if (dma_xt_refresh_queued)
        dma_stat_rq_pc |= 0x01;
}

#define DMA_PS2_IOA            (1 << 0)
#define DMA_PS2_AUTOINIT       (1 << 1)
#define DMA_PS2_XFER_MEM_TO_IO (1 << 2)
#define DMA_PS2_XFER_IO_TO_MEM (3 << 2)
#define DMA_PS2_XFER_MASK      (3 << 2)
#define DMA_PS2_DEC2           (1 << 4)
#define DMA_PS2_SIZE16         (1 << 6)

#ifdef ENABLE_DMA_LOG
int dma_do_log = ENABLE_DMA_LOG;

static void
dma_log(const char *fmt, ...)
{
    va_list ap;

    if (dma_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define dma_log(fmt, ...)
#endif

static void dma_ps2_run(int channel);

static uint8_t
dma_ps2_arb_level(int channel)
{
    /* Only channels 0 and 4 have programmable arbitration levels. */
    return ((channel & 3) == 0) ? (dma[channel].arb_level & 0x0f) : channel;
}

static void
dma_ps2_master_clear(void)
{
    dma_wp[0] = dma_wp[1] = 0;
    dma_ps2.byte_ptr       = 0;
    dma_m                  = 0xff;
    dma_stat               = 0;
    dma_stat_rq            = 0;
    dma_stat_adv_pend      = 0;
    dma_req_is_soft        = 0;
    dma_command[0]          = 0;
    dma_command[1]          = 0;
}

static void
dma_ps2_request(int request_channel)
{
    const uint8_t level = dma_ps2_arb_level(request_channel);
    uint8_t       matched = 0;

    dma_ps2.arb_status = (dma_ps2.arb_status & 0x60) | level;

    /* The Model 80 firmware uses reserved function B4 to request channel 4's
       arbitration level.  Every enabled DMA channel at that level is serviced
       in channel order.  MCA channel 4 remains a normal DMA channel, unlike
       the AT cascade input. */
    for (int channel = 0; channel < 8; channel++) {
        if (!(dma_m & (1 << channel)) && (dma_ps2_arb_level(channel) == level)) {
            matched = 1;
            dma_ps2_run(channel);
        }
    }

    if (!matched) {
        /* If neither an internal DMA channel nor an external bus master
           accepts the granted level, central arbitration times out. */
        dma_ps2.arb_status = 0x60 | level;
        if (dma_ps2.arb_control & 0x80)
            nmi_raise();
    }
}

static uint8_t
dma_ps2_arb_read(UNUSED(uint16_t addr), UNUSED(void *priv))
{
    const uint8_t ret = (dma_ps2.arb_control & 0x80) | dma_ps2.arb_status;

    if (dma_ps2.arb_status & 0x20)
        dma_ps2.arb_status &= ~0x40;

    return ret;
}

static void
dma_ps2_arb_write(UNUSED(uint16_t addr), uint8_t val, UNUSED(void *priv))
{
    dma_ps2.arb_control = val & 0xa0;

    /* Clearing the arbitration mask acknowledges a bus-timeout NMI. */
    if (!(val & 0x40))
        dma_ps2.arb_status &= 0x0f;
}


int
dma_get_drq(int channel)
{
    if ((channel < 0) || (channel > 7))
        return 0;
    return !!(dma_stat_rq_pc & (1 << channel));
}


void
dma_set_drq(int channel, int set)
{
    uint8_t bit;

    if ((channel < 0) || (channel > 7))
        return;

    bit = 1 << channel;
    dma_stat_rq_pc &= ~bit;
    if (set)
        dma_stat_rq_pc |= bit;
    if (dma_ibm5140 && set && channel > 0 && channel < 4)
        ibm5140_clock_wake();

    if (dma_xt8237_active() && (channel < 4) && !set &&
        (dma_xt8237.demand_active & bit)) {
        dma_xt8237_release_owner(channel);
    }

    /* A request/owner change may make refresh serviceable or invalidate a
     * pre-DACK schedule. Reconcile both directions, not only retries. */
    if (dma_xt8237_active() && dma_xt_refresh_queued)
        dma_xt_refresh_reconcile();
}

static void (*dma_service_handler[8])(void *);
static void  *dma_service_priv[8];

void
dma_set_service_handler(int channel, void (*handler)(void *), void *priv)
{
    if ((channel < 0) || (channel > 7))
        return;

    dma_service_handler[channel] = handler;
    dma_service_priv[channel]    = priv;
}

void
dma_set_eop(int channel, int set)
{
    uint8_t bit;

    if ((channel < 0) || (channel > 3))
        return;

    bit = 1 << channel;
    if (set)
        dma_xt8237.external_eop |= bit;
    else
        dma_xt8237.external_eop &= ~bit;
}

static int
dma_transfer_size(dma_t *dev)
{
    return dev->transfer_mode & 0xff;
}

/* EISA extended mode (82374EB ESC, 82357 ISP), transfer size, bits 3:2 of
   the channel's extended mode register: 00 8-bit I/O counted in bytes,
   01 16-bit I/O counted in words with the address shifted (the ISA
   16-bit channel), 11 16-bit I/O counted in bytes with the address not
   shifted -- the one Windows NT's EISA HAL programs for a 16-bit ISA
   device. Counted in bytes, each 16-bit transfer takes two from the count. */
static int
dma_ext_size(const dma_t *dev)
{
    return (dev->ext_mode >> 2) & 0x03;
}


/* A 16-bit channel's address register holds a word address (A1-A16) and
   its page register A17-A23 only while it counts in words; in the byte
   modes both hold the byte address as written. */
static int
dma_addr_shifted(const dma_t *dev)
{
    if (!dma_eisa)
        return (dev - dma) >= 4;
    return dma_ext_size(dev) == 0x01;
}

/* Whether a channel's controller is disabled. On an EISA controller the
   first controller hangs off channel 4 of the second, cascaded: disabling
   the second or masking channel 4 stops 0-3 as well (82374EB 3.2.1, 3.2.5). */
static int
dma_group_disabled(int channel)
{
    if (channel >= 4)
        return !!(dma_command[1] & 0x04);
    if (dma_command[0] & 0x04)
        return 1;
    return dma_eisa && ((dma_command[1] & 0x04) || (dma_m & 0x10));
}

/* Whether a channel is chaining: its base registers then hold the next
   buffer, and what the CPU writes to the address, count and page registers
   goes to them alone, leaving the current ones moving (82374EB 3.2.x,
   Base and Current registers; 3.2.17). */
static int
dma_chaining(int channel)
{
    return dma_eisa && (dma_chain_mode[channel] & 0x04);
}

/* IRQ13 falls when neither a chain nor a scatter-gather list is waiting
   on the CPU. */
static void
dma_irq13_update(void)
{
    if (!dma_chain_int && !dma_sg_int)
        picintc(1 << 13);
}

/* Master Clear on an EISA controller, one group: what the data book lists
   beyond the 8237's command, status, request, flip-flop and mask -- mode
   (channel 4 back to cascade), address, count and high count, low and high
   page, chaining mode, and compatibility addressing. The extended mode and
   stop registers are not affected (82374EB 3.2.x, 6.x). */
static void
dma_eisa_master_clear(int first)
{
    for (int c = first; c < first + 4; c++) {
        dma[c].mode   = (c == 4) ? 0xc0 : 0x00;
        dma[c].ab     = dma[c].ac = 0;
        dma[c].cb     = 0;
        dma[c].cc     = 0;
        dma[c].page   = dma[c].page_l = dma[c].page_h = 0;
        dma[c].xfer_n = 0;
        dma_chain_mode[c] = 0;
        dma[c].ext_addr   = 0;
        /* Scatter-gather: not active, Next Link Null clear, IRQ13 chosen
           for the end of the list (82374EB 3.2.21, 3.2.22). */
        dma[c].sg_status &= ~(DMA_SG_ACTIVE | DMA_SG_EOP | DMA_SG_NNL);
        dma_sg_int &= ~(1 << c);
    }
    dma_irq13_update();
}

/* ADDRESS COMPATIBILITY MODE (82374EB 6.7.1). Writing any of a channel's
   lower three address bytes -- the address register or the low page --
   clears its high page and puts it in compatibility mode, where the
   address counts within its 64K (128K shifted) and never into the page,
   as an 8237's. Writing the high page after them puts it in extended
   address mode, which counts through all 32 bits. Reset and Master Clear
   give compatibility mode. */
static void
dma_addr_compat(dma_t *dev)
{
    if (!dma_eisa)
        return;
    dev->ext_addr = 0;
    dev->page_h   = 0;
    dev->ab &= 0x00ffffff;
    if (!dma_chaining((int) (dev - dma)))
        dev->ac &= 0x00ffffff;
}

/* The bytes the next transfer moves: the channel's width, less at an odd
   start or at the end of an EISA count by bytes (82374EB, Extended Mode
   Register: "a partial word transfer during the first and last transfer"). */
static int
dma_xfer_bytes(const dma_t *dev)
{
    int      w = dev->transfer_mode & 0xff;
    int      n = w;
    uint32_t rem;

    if (!dma_eisa || (w < 2) || (dma_ext_size(dev) < 0x02))
        return w;
    if (!(dev->mode & 0x20) && (dev->ac & (w - 1)))
        n = w - (dev->ac & (w - 1));
    rem = (uint32_t) dev->cc + 1;
    if ((dev->cc >= 0) && (rem < (uint32_t) n))
        n = (int) rem;
    return n;
}

/* What a transfer of n bytes takes from the count: the bytes in the EISA
   count-by-bytes modes, one transfer otherwise. */
static int
dma_count_dec(const dma_t *dev, int n)
{
    return (dma_eisa && (dma_ext_size(dev) >= 0x02)) ? n : 1;
}

/* What the transfer just made takes from the count, and forget it. */
static int
dma_count_take(dma_t *dev)
{
    int n = dev->xfer_n ? dev->xfer_n : (dev->transfer_mode & 0xff);

    dev->xfer_n = 0;
    return dma_count_dec(dev, n);
}

/* Scatter-gather (82374EB 3.2.21-3.2.23, 6.6). A descriptor is a 32-bit
   memory address and a byte count, End of List in the count's top bit. The
   channel's own registers hold the buffers: the base set a prefetched
   reserve, the current set the one moving. */

/* Prefetch the descriptor at the pointer into the base registers. The
   pointer steps on by 8, except past a descriptor with EOL, which sets Next
   Link Null instead and ends the fetching. */
static void
dma_sg_fetch(dma_t *dev)
{
    uint32_t sgd[2];
    uint32_t bytes;
    uint32_t units;

    if (dev->sg_status & DMA_SG_NNL)
        return;

    dma_bm_read(dev->ptr & ~3, (uint8_t *) sgd, 8, 4);
    bytes = sgd[1] & dma_sg_count_mask;
    if (!bytes)
        bytes = (dma_sg_count_mask | 1) + 1;
    /* The count register counts what the channel counts: bytes, or words
       on a channel counting in words. */
    units = (dma_eisa && (dma_ext_size(dev) >= 0x02)) ? bytes : (bytes / dma_transfer_size(dev));
    if (!units)
        units = 1;

    dev->ab = sgd[0] & dma_mask;
    dev->cb = units - 1;
    dev->sg_status |= DMA_SG_BASE;
    if (sgd[1] & 0x80000000)
        dev->sg_status |= DMA_SG_NNL;
    else {
        dev->sg_status &= ~DMA_SG_NNL;
        dev->ptr += 8;
    }
    dma_log("DMA S/G fetch: %08X %08X\n", sgd[0], sgd[1]);
}

/* A transfer on an active channel whose current buffer has run out first
   moves the base buffer in, then prefetches the one after it (6.6 step 8:
   not before the next transfer starts). */
static void
dma_sg_load(dma_t *dev)
{
    if ((dev->sg_status & (DMA_SG_ACTIVE | DMA_SG_CUR | DMA_SG_BASE)) != (DMA_SG_ACTIVE | DMA_SG_BASE))
        return;

    dev->ac     = dev->ab;
    dev->cc     = (int) dev->cb;
    dev->page   = dev->page_l = (dev->ac >> 16) & 0xff;
    dev->page_h = (dev->ac >> 24) & 0xff;
    dev->sg_status = (dev->sg_status & ~DMA_SG_BASE) | DMA_SG_CUR;
    dma_sg_fetch(dev);
}

/* The list is over, by Stop or by the last buffer: Terminate, not active,
   the channel masked, and EOP to the device or IRQ13 to the CPU as the
   command register chose. Returns whether it is EOP. */
static int
dma_sg_end(int channel)
{
    dma_t *dev = &dma[channel];

    dev->sg_status = (dev->sg_status & ~DMA_SG_ACTIVE) | DMA_SG_TERM;
    dma_m |= (1 << channel);
    dma_sw_req &= ~(1 << channel);
    if (dev->sg_status & DMA_SG_EOP)
        return 1;
    dma_sg_int |= (1 << channel);
    picint(1 << 13);
    return 0;
}

/* The current buffer's count has run out. With a buffer waiting in the
   base registers the channel carries on; after the last one the list ends
   at terminal count (6.6 step 9). Returns whether TC goes to the device. */
static int
dma_sg_expire(int channel)
{
    dma_t *dev = &dma[channel];

    dev->sg_status &= ~DMA_SG_CUR;
    if (dev->sg_status & DMA_SG_BASE)
        return 0;
    dma_stat |= (1 << channel);
    return dma_sg_end(channel);
}

/* The count has gone past zero. Returns whether TC goes to the device. */
static int
dma_count_out(int channel)
{
    dma_t *dma_c = &dma[channel];

    if (dma_c->sg_status & DMA_SG_ACTIVE)
        return dma_sg_expire(channel);

    dma_sw_req &= ~(1 << channel); /* TC clears the channel's software request */
    dma_stat |= (1 << channel);

    if (dma_chaining(channel)) {
        /* A buffer has expired. Bit 4 chose how the CPU is told, so it can
           program the next: TC, or IRQ13 in its place. With the base
           registers marked programmed the channel moves on to them;
           otherwise the chain ends and the channel masks itself. Chaining
           stays on until the CPU turns it off (3.2.17, 3.2.18). */
        int eop = !!(dma_chain_mode[channel] & 0x10);

        if (!eop) {
            dma_chain_int |= (1 << channel);
            picint(1 << 13);
        }
        if (dma_chain_mode[channel] & 0x08) {
            dma_chain_mode[channel] &= ~0x08;
            dma_c->cc = dma_c->cb;
            dma_c->ac = dma_c->ab;
        } else
            dma_m |= (1 << channel);
        return eop;
    }

    if (dma_c->mode & 0x10) { /*Auto-init*/
        dma_c->cc = dma_c->cb;
        dma_c->ac = dma_c->ab;
    } else
        dma_m |= (1 << channel);
    return 1;
}

static void
dma_block_transfer(int channel)
{
    int bit16 = (channel >= 4);

    if (dma_advanced)
        bit16 = !!(dma_transfer_size(&(dma[channel])) == 2);

    dma_req_is_soft = 1;
    /* A block verify moves nothing but runs to terminal count, as the
       others do (82374EB Table 19). */
    if ((dma[channel].mode & 0x8c) == 0x80) {
        for (uint32_t i = 0; i < 0x1000000; i++) {
            int ret = dma_channel_write(channel, 0);

            if ((ret == DMA_NODATA) || (ret & DMA_OVER))
                break;
        }
        dma_req_is_soft = 0;
        return;
    }
    for (uint32_t i = 0; (i <= dma[channel].cb) && (i < 65536); i++) {
        if ((dma[channel].mode & 0x8c) == 0x84) {
            if (bit16)
                dma_channel_write(channel, dma16_buffer[i]);
            else
                dma_channel_write(channel, dma_buffer[i]);
        } else if ((dma[channel].mode & 0x8c) == 0x88) {
            if (bit16)
                dma16_buffer[i] = dma_channel_read(channel);
            else
                dma_buffer[i] = dma_channel_read(channel);
        }
    }
    dma_req_is_soft = 0;
}

static void
dma_mem_to_mem_transfer(void)
{
    int i;

    if ((dma[0].mode & 0x0c) != 0x08)
        fatal("DMA memory to memory transfer: channel 0 mode not read\n");
    if ((dma[1].mode & 0x0c) != 0x04)
        fatal("DMA memory to memory transfer: channel 1 mode not write\n");

    dma_req_is_soft = 1;

    for (i = 0; (i <= (int) dma[0].cb) && (i < 65536); i++)
        dma_buffer[i] = dma_channel_read(0);

    for (i = 0; (i <= (int) dma[1].cb) && (i < 65536); i++)
        dma_channel_write(1, dma_buffer[i]);

    dma_req_is_soft = 0;
}

/* The S-G Command register (3.2.21). Bit 6 lets bit 7 choose EOP (1) or
   IRQ13 (0) for the end of the list; bits 1:0 are the command. */
static void
dma_sg_command(int channel, uint8_t val)
{
    dma_t *dev = &dma[channel];

    dma_log("DMA S/G Cmd   : ch %i val = %02X\n", channel, val);

    if (val & 0x40)
        dev->sg_status = (dev->sg_status & ~DMA_SG_EOP) | ((val & 0x80) ? DMA_SG_EOP : 0);

    switch (val & 0x03) {
        case 0x01:
            /* Start: active, both buffers empty, not terminated, Next Link
               Null clear, and the first descriptor prefetched. */
            dev->sg_status = (dev->sg_status & DMA_SG_EOP) | DMA_SG_ACTIVE;
            dma_sg_int &= ~(1 << channel);
            dma_irq13_update();
            dma_sg_fetch(dev);
            break;
        case 0x02:
            /* Stop: halts at once, Terminate and the channel's mask set. */
            if (dev->sg_status & DMA_SG_ACTIVE)
                (void) dma_sg_end(channel);
            else {
                dev->sg_status |= DMA_SG_TERM;
                dma_m |= (1 << channel);
            }
            break;

        default:
            break;
    }
}

/* The S-G registers, offsets from the relocatable base: 10h-17h command
   (write only), 18h-1Fh status (read only), 20h-3Fh the descriptor table
   pointers, four bytes a channel. Wider accesses come here a byte at a
   time, each to its own channel. */
static void
dma_sg_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    uint8_t reg = port & 0xff;

    dma_log("DMA S/G BYTE  write: %04X       %02X\n", port, val);

    if (reg >= 0x20) {
        dma_t *dev   = &dma[(reg >> 2) & 7];
        int    shift = (reg & 3) * 8;

        dev->ptr = (dev->ptr & ~(0xffu << shift)) | ((uint32_t) val << shift);
    } else if (reg < 0x18)
        dma_sg_command(reg & 7, val);
}

static uint8_t
dma_sg_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t reg = port & 0xff;
    uint8_t ret = 0xff;

    if (reg >= 0x20)
        ret = dma[(reg >> 2) & 7].ptr >> ((reg & 3) * 8);
    else if (reg >= 0x18)
        ret = dma[reg & 7].sg_status;

    dma_log("DMA S/G BYTE  read : %04X       %02X\n", port, ret);

    return ret;
}

static void
dma_ext_mode_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    int channel = (val & 0x03);

    if (addr == 0x4d6)
        channel |= 4;

    dma[channel].ext_mode   = val & 0x7c;
    dma_chain_mode[channel] = 0; /* so does one to the extended mode register (3.2.17) */

    /* Only an EISA controller has stop registers for this to turn on. */
    if (dma_eisa && (val & 0x80))
        dma_stop_en |= (1 << channel);
    else
        dma_stop_en &= ~(1 << channel);

    switch ((val >> 2) & 0x03) {
        case 0x00:
            dma[channel].transfer_mode = 0x0101;
            dma[channel].size          = 0;
            break;
        case 0x01:
            dma[channel].transfer_mode = 0x0202;
            dma[channel].size          = 1;
            break;
        case 0x02:
            /* 32-bit I/O counted in bytes (82374EB 3.2.x, Extended Mode
               Register): the address steps by four, as the count does, for
               a 32-bit DMA slave (dma_channel_read32/write32). */
            dma[channel].transfer_mode = 0x0404;
            dma[channel].size          = 2;
            break;
        case 0x03:
            /* 16-bit I/O counted in bytes: the address steps by two, as
               the count does (dma_count_take). */
            dma[channel].transfer_mode = 0x0202;
            dma[channel].size          = 1;
            break;

        default:
            break;
    }
}

static uint8_t
dma_sg_int_status_read(UNUSED(uint16_t addr), UNUSED(void *priv))
{
    uint8_t ret = 0x00;

    ret |= dma_sg_int | dma_chain_int;

    return ret;
}


static uint8_t dma_read_legacy(uint16_t addr, void *priv);
static void dma_write_legacy(uint16_t addr, uint8_t val, void *priv);

static uint8_t
dma_ibm5140_diag_read(void)
{
    switch (dma_ibm5140_diag) {
        case 0: case 1: case 2:
            return dma[dma_ibm5140_diag + 1].page & 0x0f;
        case 3:
            return dma_m & 0x0e;
        case 7:
            return dma_command[0] & 0xc4;
        case 8: case 9: case 10:
            return dma[dma_ibm5140_diag - 7].mode & 0xec;
        case 11:
            return dma_xt8237.sw_request & 0x0e;
        default:
            return 0xff;
    }
}

static void
dma_ibm5140_service(void)
{
    int channel;
    uint8_t requests = dma_xt8237.sw_request;
    /* Software initiation is defined only for block-mode channels. Keep
     * other request bits latched and visible through the diagnostic port. */
    for (channel = 1; channel < 4; channel++)
        if ((dma[channel].mode & 0xc0) != 0x80)
            requests &= ~(1 << channel);
    dma_req_is_soft = 1;
    while ((channel = dma_xt8237_priority_pick(requests)) >= 0) {
        const int type = dma[channel].mode & 0x0c;
        int result;
        if (type == 0x0c)
            break;
        /* Verify advances the same address/count/TC state without touching
           memory. With no peripheral driving a software write, D0..7 float. */
        if (type == 4)
            result = dma_channel_write(channel, 0xff);
        else
            result = dma_channel_read(channel);
        if (result == DMA_NODATA)
            break;
        requests &= dma_xt8237.sw_request;
    }
    dma_req_is_soft = 0;
}

static uint8_t
dma_read(uint16_t addr, void *priv)
{
    uint8_t ret;

    if (dma_ibm5140 && ((addr & 0x0f) < 2))
        return (addr & 1) ? dma_ibm5140_diag_read() : 0xff;

    if (!dma_xt8237_active())
        return dma_read_legacy(addr, priv);

    switch (addr & 0x0f) {
        case 0x08:
            ret = (dma_xt8237_raw_requests() << 4) | (dma_stat & 0x0f);
            dma_stat &= ~0x0f;
            return ret;

        case 0x0d:
            return dma_xt8237.temp;

        default:
            return dma_read_legacy(addr, priv);
    }
}

static void
dma_write(uint16_t addr, uint8_t val, void *priv)
{
    int channel;
    uint8_t bit;

    if (dma_ibm5140) {
        switch (addr & 0x0f) {
            case 0:
                dma_ibm5140_diag = val & 0x0f;
                return;
            case 1:
                return;
            case 8:
                val &= 0xc4; /* No memory-to-memory, rotation, or compression. */
                break;
            case 9: case 10: case 11:
                if (!(val & 3))
                    return; /* Channel zero does not exist on the 5140. */
                if ((addr & 0x0f) == 11)
                    val &= ~0x10; /* No automatic reinitialization. */
                break;
        }
    }

    if (!dma_xt8237_active()) {
        dma_write_legacy(addr, val, priv);
        return;
    }

    dmaregs[0][addr & 0x0f] = val;

    switch (addr & 0x0f) {
        case 0x08:
            dma_command[0] = val;
            if (val & 0x04) {
                dma_xt8237.demand_active = 0;
                dma_xt8237.block_active = 0;
                dma_xt8237.dack = 0;
            }
            break;

        case 0x09:
            channel = val & 3;
            bit = (uint8_t)(1u << channel);
            if (val & 4) {
                dma_xt8237.sw_request |= bit;
                if ((channel == 0) && (dma_command[0] & 0x01))
                    (void)dma_xt8237_mem_to_mem();
                else if (!dma_ibm5140)
                    dma_block_transfer(channel);
            } else {
                dma_xt8237.sw_request &= (uint8_t)~bit;
                if (dma_xt8237.demand_active & bit)
                    dma_xt8237_release_owner(channel);
            }
            break;

        case 0x0a:
            channel = val & 3;
            bit = (uint8_t)(1u << channel);
            if (val & 4) {
                dma_m |= bit;
                dma_xt8237_release_owner(channel);
            } else {
                dma_m &= (uint8_t)~bit;
            }
            break;

        case 0x0b:
            channel = val & 3;
            bit = (uint8_t)(1u << channel);
            dma[channel].mode = val;
            dma_xt8237.demand_active &= (uint8_t)~bit;
            dma_xt8237.block_active &= (uint8_t)~bit;
            dma_xt8237.external_eop &= (uint8_t)~bit;
            dma_xt8237.dack &= (uint8_t)~bit;
            break;

        case 0x0c:
            dma_wp[0] = 0;
            break;

        case 0x0d:
            dma_xt8237_master_clear();
            break;

        case 0x0e:
            dma_m &= 0xf0;
            break;

        case 0x0f:
            dma_m = (dma_m & 0xf0) | (val & 0x0f);
            dma_xt8237.demand_active &= (uint8_t)~dma_m;
            dma_xt8237.block_active &= (uint8_t)~dma_m;
            dma_xt8237.dack &= (uint8_t)~dma_m;
            break;

        default:
            dma_write_legacy(addr, val, priv);
            break;
    }

    /* The previous dispatcher returned from every arm, so its refresh retry
     * was unreachable. Reconcile after every programming write. */
    dma_xt_refresh_reconcile();
    if (dma_ibm5140) {
        dma_m |= 1;
        dma_ibm5140_service();
    }
}

static uint8_t
dma_read_legacy(uint16_t addr, UNUSED(void *priv))
{
    int     channel = (addr >> 1) & 3;
    int     count;
    uint8_t ret = (dmaregs[0][addr & 0xf]);

    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[0] ^= 1;
            if (dma_eisa && dma_addr_shifted(&dma[channel])) {
                if (dma_wp[0])
                    ret = ((dma[channel].ac >> 1) & 0xff);
                else
                    ret = ((dma[channel].ac >> 9) & 0xff);
            } else if (dma_wp[0])
                ret = (dma[channel].ac & 0xff);
            else
                ret = ((dma[channel].ac >> 8) & 0xff);
            break;

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[0] ^= 1;
            count = dma[channel].cc/* + 1*/;
            if (dma_wp[0])
                ret = count & 0xff;
            else
                ret = count >> 8;
            break;

        case 8: /*Status register*/
            /* A peripheral with DRQ asserted may complete its transfer while
               software polls terminal count.  Service it before returning
               the controller status so the completion is visible at once. */
            for (channel = 0; channel < 4; channel++) {
                if ((dma_stat_rq_pc & (1 << channel)) &&
                    !(dma_m & (1 << channel)) && dma_service_handler[channel])
                    dma_service_handler[channel](dma_service_priv[channel]);
            }
            if (dma_ps2.is_ps2) {
                ret = (dma_stat_rq & 0x0f) << 4;
                ret |= dma_stat & 0x0f;
                dma_stat &= ~0x0f;
                dma_stat_rq &= ~0x0f;
            } else {
                ret = ((dma_stat_rq_pc | dma_sw_req) & 0x0f) << 4;
                ret |= dma_stat & 0x0f;
                dma_stat &= ~0x0f;
            }
            break;

        case 0xd: /*Temporary register*/
            ret = 0x00;
            break;

        case 0xf: /*Write All Mask Bits: readable on EISA, as the mask now*/
            if (dma_eisa)
                ret = dma_m & 0x0f;
            break;

        default:
            break;
    }

    dma_log("DMA: [R] %04X = %02X\n", addr, ret);

    return ret;
}

static void
dma_write_legacy(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    int channel = (addr >> 1) & 3;

    dma_log("DMA: [W] %04X = %02X\n", addr, val);

    dmaregs[0][addr & 0xf] = val;
    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[0] ^= 1;
            dma_addr_compat(&dma[channel]);
            if (dma_eisa && dma_addr_shifted(&dma[channel])) {
                /* Extended mode 01 on 0-3: a word address, as on 5-7. */
                if (dma_wp[0])
                    dma[channel].ab = (dma[channel].ab & 0xfffffe00 & dma_mask) | (val << 1);
                else
                    dma[channel].ab = (dma[channel].ab & 0xfffe01ff & dma_mask) | (val << 9);
            } else if (dma_wp[0])
                dma[channel].ab = (dma[channel].ab & 0xffffff00 & dma_mask) | val;
            else
                dma[channel].ab = (dma[channel].ab & 0xffff00ff & dma_mask) | (val << 8);
            if (!dma_chaining(channel))
                dma[channel].ac = dma[channel].ab;
            return;

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[0] ^= 1;
            if (dma_wp[0])
                dma[channel].cb = (dma[channel].cb & 0xff00) | val;
            else
                dma[channel].cb = (dma[channel].cb & 0x00ff) | (val << 8);
            if (!dma_chaining(channel))
                dma[channel].cc = dma[channel].cb;
            return;

        case 8: /*Control register*/
            dma_command[0] = val;
#ifdef ENABLE_DMA_LOG
            if (val & 0x01)
                dma_log("[%08X:%04X] Memory-to-memory enable\n", CS, cpu_state.pc);
#endif
            return;

        case 9: /*Request register */
            channel = (val & 3);
            if (val & 4) {
                dma_sw_req |= (1 << channel);
                /* The ESC has no memory-to-memory transfer (82374EB 3.2.1). */
                if ((channel == 0) && (dma_command[0] & 0x01) && !dma_eisa) {
                    dma_log("Memory to memory transfer start\n");
                    dma_mem_to_mem_transfer();
                } else
                    dma_block_transfer(channel);
            } else
                dma_sw_req &= ~(1 << channel);
            break;

        case 0xa: /*Mask*/
            channel = (val & 3);
            if (val & 4)
                dma_m |= (1 << channel);
            else
                dma_m &= ~(1 << channel);
            return;

        case 0xb: /*Mode*/
            channel           = (val & 3);
            dma[channel].mode = val;
            dma_chain_mode[channel] = 0; /* an access to the mode register resets chaining (3.2.17) */
            if (dma_ps2.is_ps2) {
                dma[channel].ps2_mode = 0;
                dma[channel].size     = 0;
                if (val & 0x10)
                    dma[channel].ps2_mode |= DMA_PS2_AUTOINIT;
                if (val & 0x20)
                    dma[channel].ps2_mode |= DMA_PS2_DEC2;
                if ((val & 0xc) == 8)
                    dma[channel].ps2_mode |= DMA_PS2_XFER_MEM_TO_IO;
                else if ((val & 0xc) == 4)
                    dma[channel].ps2_mode |= DMA_PS2_XFER_IO_TO_MEM;
            }
            return;

        case 0xc: /*Clear FF*/
            dma_wp[0] = 0;
            return;

        case 0xd: /*Master clear*/
            /* The Request register, not the DREQ lines a device holds. */
            dma_wp[0] = 0;
            for (int c = 0; c < 4; c++)
                dma_addr_compat(&dma[c]);
            dma_m |= 0xf;
            dma_sw_req &= ~0x0f;
            if (dma_eisa) {
                dma_command[0] = 0;
                dma_stat &= ~0x0f;
                dma_eisa_master_clear(0);
            }
            return;

        case 0xe: /*Clear mask*/
            dma_m &= 0xf0;
            return;

        case 0xf: /*Mask write*/
            dma_m = (dma_m & 0xf0) | (val & 0xf);
            return;

        default:
            break;
    }
}

static uint8_t
dma_ps2_read(uint16_t addr, UNUSED(void *priv))
{
    const dma_t  *dma_c = &dma[dma_ps2.xfr_channel];
    uint8_t temp  = 0xff;

    switch (addr) {
        case 0x1a:
            switch (dma_ps2.xfr_command) {
                case 0: /*I/O address*/
                    if (dma_ps2.byte_ptr)
                        temp = (dma_c->io_addr >> 8) & 0xff;
                    else
                        temp = dma_c->io_addr & 0xff;
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    break;

                case 2: /*Address*/
                case 3: {
                    const uint32_t address = (dma_ps2.xfr_command == 2) ? dma_c->ab : dma_c->ac;

                    switch (dma_ps2.byte_ptr) {
                        case 0:
                            temp             = address & 0xff;
                            dma_ps2.byte_ptr = 1;
                            break;
                        case 1:
                            temp             = (address >> 8) & 0xff;
                            dma_ps2.byte_ptr = 2;
                            break;
                        case 2:
                            temp             = (address >> 16) & 0xff;
                            dma_ps2.byte_ptr = 0;
                            break;

                        default:
                            break;
                    }
                    break;
                }

                case 4: /*Count*/
                case 5: {
                    const uint16_t count = (dma_ps2.xfr_command == 4) ? dma_c->cb : dma_c->cc;

                    if (dma_ps2.byte_ptr)
                        temp = count >> 8;
                    else
                        temp = count & 0xff;
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    break;
                }

                case 6: /*Read DMA status*/
                    if (dma_ps2.byte_ptr) {
                        temp = ((dma_stat_rq & 0xf0) >> 4) | (dma_stat & 0xf0);
                        dma_stat &= ~0xf0;
                        dma_stat_rq &= ~0xf0;
                    } else {
                        temp = (dma_stat_rq & 0x0f) | ((dma_stat & 0x0f) << 4);
                        dma_stat &= ~0xf;
                        dma_stat_rq &= ~0xf;
                    }
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    break;

                case 7: /*Mode*/
                    temp = dma_c->ps2_mode;
                    break;

                case 8: /*Arbitration Level*/
                    if ((dma_ps2.xfr_channel == 0) || (dma_ps2.xfr_channel == 4))
                        temp = dma_ps2_arb_level(dma_ps2.xfr_channel);
                    break;

                case 0xb:
                    if (!(dma_m & (1 << dma_ps2.xfr_channel)))
                        dma_ps2_run(dma_ps2.xfr_channel);
                    break;

                default:
                    /* Direct and reserved commands have no data phase. */
                    break;
            }
            break;

        default:
            break;
    }
    return temp;
}

static void
dma_ps2_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    dma_t  *dma_c;
    uint8_t mode;

    switch (addr) {
        case 0x18:
            dma_ps2.xfr_channel = val & 0x7;
            dma_ps2.xfr_command = val >> 4;
            dma_ps2.byte_ptr    = 0;
            switch (dma_ps2.xfr_command) {
                case 9: /*Set DMA mask*/
                    dma_m |= (1 << dma_ps2.xfr_channel);
                    break;

                case 0xa: /*Reset DMA mask*/
                    dma_m &= ~(1 << dma_ps2.xfr_channel);
                    break;

                case 0xb:
                    if (!(dma_m & (1 << dma_ps2.xfr_channel)))
                        dma_ps2_run(dma_ps2.xfr_channel);
                    else
                        dma_ps2_request(dma_ps2.xfr_channel);
                    break;

                case 0xc: /*Reset software DMA request*/
                    break;

                case 0xd: /*Master clear*/
                    dma_ps2_master_clear();
                    break;

                default:
                    break;
            }
            break;

        case 0x1a:
            dma_c = &dma[dma_ps2.xfr_channel];
            switch (dma_ps2.xfr_command) {
                case 0: /*I/O address*/
                    if (dma_ps2.byte_ptr)
                        dma_c->io_addr = (dma_c->io_addr & 0x00ff) | (val << 8);
                    else
                        dma_c->io_addr = (dma_c->io_addr & 0xff00) | val;
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    break;

                case 2: /*Address*/
                    switch (dma_ps2.byte_ptr) {
                        case 0:
                            dma_c->ac        = (dma_c->ac & 0xffff00) | val;
                            dma_ps2.byte_ptr = 1;
                            break;

                        case 1:
                            dma_c->ac        = (dma_c->ac & 0xff00ff) | (val << 8);
                            dma_ps2.byte_ptr = 2;
                            break;

                        case 2:
                            dma_c->ac        = (dma_c->ac & 0x00ffff) | (val << 16);
                            dma_ps2.byte_ptr = 0;
                            break;

                        default:
                            break;
                    }
                    dma_c->ab = dma_c->ac;
                    break;

                case 4: /*Count*/
                    if (dma_ps2.byte_ptr)
                        dma_c->cc = (dma_c->cc & 0xff) | (val << 8);
                    else
                        dma_c->cc = (dma_c->cc & 0xff00) | val;
                    dma_ps2.byte_ptr = (dma_ps2.byte_ptr + 1) & 1;
                    dma_c->cb        = dma_c->cc;
                    break;

                case 7: /*Mode register*/
                    mode = 0;
                    if (val & DMA_PS2_DEC2)
                        mode |= 0x20;
                    if ((val & DMA_PS2_XFER_MASK) == DMA_PS2_XFER_MEM_TO_IO)
                        mode |= 8;
                    else if ((val & DMA_PS2_XFER_MASK) == DMA_PS2_XFER_IO_TO_MEM)
                        mode |= 4;
                    dma_c->mode = (dma_c->mode & ~0x3c) | mode;
                    if (val & DMA_PS2_AUTOINIT)
                        dma_c->mode |= 0x10;
                    dma_c->ps2_mode = val;
                    dma_c->size     = val & DMA_PS2_SIZE16;
                    break;

                case 8: /*Arbitration Level*/
                    if ((dma_ps2.xfr_channel == 0) || (dma_ps2.xfr_channel == 4))
                        dma[dma_ps2.xfr_channel].arb_level = val & 0x0f;
                    break;

                case 0xb:
                    if (!(dma_m & (1 << dma_ps2.xfr_channel)))
                        dma_ps2_run(dma_ps2.xfr_channel);
                    break;

                default:
                    /* Direct and reserved commands have no data phase. */
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
dma16_read(uint16_t addr, UNUSED(void *priv))
{
    int     channel = ((addr >> 2) & 3) + 4;
#ifdef ENABLE_DMA_LOG
    uint16_t port = addr;
#endif
    uint8_t ret;
    int count;

    addr >>= 1;

    ret = dmaregs[1][addr & 0xf];

    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[1] ^= 1;
            if (dma_ps2.is_ps2 || !dma_addr_shifted(&dma[channel])) {
                if (dma_wp[1])
                    ret = (dma[channel].ac);
                else
                    ret = ((dma[channel].ac >> 8) & 0xff);
            } else if (dma_wp[1])
                ret = ((dma[channel].ac >> 1) & 0xff);
            else
                ret = ((dma[channel].ac >> 9) & 0xff);
            break;

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[1] ^= 1;
            count = dma[channel].cc/* + 1*/;
            if (dma_wp[1])
                ret = count & 0xff;
            else
                ret = count >> 8;
            break;

        case 8: /*Status register*/
            /* See the primary-controller status path above. */
            for (channel = 4; channel < 8; channel++) {
                if ((dma_stat_rq_pc & (1 << channel)) &&
                    !(dma_m & (1 << channel)) && dma_service_handler[channel])
                    dma_service_handler[channel](dma_service_priv[channel]);
            }
            if (dma_ps2.is_ps2) {
                ret = dma_stat_rq & 0xf0;
                ret |= (dma_stat & 0xf0) >> 4;
                dma_stat &= ~0xf0;
                dma_stat_rq &= ~0xf0;
            } else {
                ret = (dma_stat_rq_pc | dma_sw_req) & 0xf0;
                ret |= dma_stat >> 4;
                dma_stat &= ~0xf0;
            }
            break;

        case 0xf: /*Write All Mask Bits: readable on EISA, as the mask now*/
            if (dma_eisa)
                ret = dma_m >> 4;
            break;

        default:
            break;
    }

    dma_log("dma16_read(%08X) = %02X\n", addr, ret);

    return ret;
}

static void
dma16_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    int channel = ((addr >> 2) & 3) + 4;

    dma_log("dma16_write(%08X, %02X)\n", addr, val);

    addr >>= 1;

    dmaregs[1][addr & 0xf] = val;
    switch (addr & 0xf) {
        case 0:
        case 2:
        case 4:
        case 6: /*Address registers*/
            dma_wp[1] ^= 1;
            dma_addr_compat(&dma[channel]);
            if (dma_ps2.is_ps2) {
                if (dma_wp[1])
                    dma[channel].ab = (dma[channel].ab & 0xffffff00 & dma_mask) | val;
                else
                    dma[channel].ab = (dma[channel].ab & 0xffff00ff & dma_mask) | (val << 8);
            } else if (!dma_addr_shifted(&dma[channel])) {
                if (dma_wp[1])
                    dma[channel].ab = (dma[channel].ab & 0xffffff00 & dma_mask) | val;
                else
                    dma[channel].ab = (dma[channel].ab & 0xffff00ff & dma_mask) | (val << 8);
            } else {
                if (dma_wp[1])
                    dma[channel].ab = (dma[channel].ab & 0xfffffe00 & dma_mask) | (val << 1);
                else
                    dma[channel].ab = (dma[channel].ab & 0xfffe01ff & dma_mask) | (val << 9);
            }
            if (!dma_chaining(channel))
                dma[channel].ac = dma[channel].ab;
            return;

        case 1:
        case 3:
        case 5:
        case 7: /*Count registers*/
            dma_wp[1] ^= 1;
            if (dma_wp[1])
                dma[channel].cb = (dma[channel].cb & 0xff00) | val;
            else
                dma[channel].cb = (dma[channel].cb & 0x00ff) | (val << 8);
            if (!dma_chaining(channel))
                dma[channel].cc = dma[channel].cb;
            return;

        case 8: /*Control register*/
            /* The second controller's command register (0D0h); on EISA its
               disable bit stops the first controller too (dma_group_disabled). */
            if (dma_eisa)
                dma_command[1] = val;
            return;

        case 9: /*Request register */
            channel = (val & 3) + 4;
            if (val & 4) {
                dma_sw_req |= (1 << channel);
                dma_block_transfer(channel);
            } else
                dma_sw_req &= ~(1 << channel);
            break;

        case 0xa: /*Mask*/
            channel = (val & 3);
            if (val & 4)
                dma_m |= (0x10 << channel);
            else
                dma_m &= ~(0x10 << channel);
            return;

        case 0xb: /*Mode*/
            channel           = (val & 3) + 4;
            dma[channel].mode = val;
            dma_chain_mode[channel] = 0; /* an access to the mode register resets chaining (3.2.17) */
            if (dma_ps2.is_ps2) {
                dma[channel].ps2_mode = DMA_PS2_SIZE16;
                dma[channel].size     = DMA_PS2_SIZE16;
                if (val & 0x10)
                    dma[channel].ps2_mode |= DMA_PS2_AUTOINIT;
                if (val & 0x20)
                    dma[channel].ps2_mode |= DMA_PS2_DEC2;
                if ((val & 0xc) == 8)
                    dma[channel].ps2_mode |= DMA_PS2_XFER_MEM_TO_IO;
                else if ((val & 0xc) == 4)
                    dma[channel].ps2_mode |= DMA_PS2_XFER_IO_TO_MEM;
            }
            return;

        case 0xc: /*Clear FF*/
            dma_wp[1] = 0;
            return;

        case 0xd: /*Master clear*/
            dma_wp[1] = 0;
            for (int c = 4; c < 8; c++)
                dma_addr_compat(&dma[c]);
            dma_m |= 0xf0;
            dma_sw_req &= ~0xf0;
            if (dma_eisa) {
                dma_command[1] = 0;
                dma_stat &= ~0xf0;
                dma_eisa_master_clear(4);
            }
            return;

        case 0xe: /*Clear mask*/
            dma_m &= 0x0f;
            return;

        case 0xf: /*Mask write*/
            dma_m = (dma_m & 0x0f) | ((val & 0xf) << 4);
            return;

        default:
            break;
    }
}

#define CHANNELS               \
    {                          \
        8, 2, 3, 1, 8, 8, 8, 0 \
    }

static void
dma_page_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    uint8_t convert[8] = CHANNELS;

    dma_log("DMA: [W] %04X = %02X\n", addr, val);

#ifdef USE_DYNAREC
    if ((addr == 0x84) && cpu_use_dynarec)
        update_tsc();
#endif

    addr &= 0x0f;
    dmaregs[2][addr] = val;

    if (machines[machine].init == machine_xt_ibm5550_init) {
        if (addr >= 4)
            addr = 8;
    } else {
        if (addr >= 8)
            addr = convert[addr & 0x07] | 4;
        else
            addr = convert[addr & 0x07];
    }

    if (addr < 8) {
        dma[addr].page_l = val;
        dma_addr_compat(&dma[addr]);

        if (dma_addr_shifted(&dma[addr]) && (addr != 4)) {
            dma[addr].page = val & 0xfe;
            dma[addr].ab   = (dma[addr].ab & 0xff01ffff & dma_mask) | (dma[addr].page << 16);
            if (!dma_chaining(addr))
                dma[addr].ac = (dma[addr].ac & 0xff01ffff & dma_mask) | (dma[addr].page << 16);
        } else if (addr > 4) {
            /* Not shifted, the page is A16-A23 whole. */
            dma[addr].page = val;
            dma[addr].ab   = (dma[addr].ab & 0xff00ffff & dma_mask) | (dma[addr].page << 16);
            if (!dma_chaining(addr))
                dma[addr].ac = (dma[addr].ac & 0xff00ffff & dma_mask) | (dma[addr].page << 16);
        } else {
            dma[addr].page = dma_page_is_xt() ? (val & 0x0f) : val;
            dma[addr].ab   = (dma[addr].ab & 0xff00ffff & dma_mask) | (dma[addr].page << 16);
            if (!dma_chaining(addr))
                dma[addr].ac = (dma[addr].ac & 0xff00ffff & dma_mask) | (dma[addr].page << 16);
        }
    }
}

static uint8_t
dma_page_read(uint16_t addr, UNUSED(void *priv))
{
    uint8_t convert[8] = CHANNELS;
    uint8_t ret        = 0xff;

    if (((addr & 0xfffc) == 0x80) && (CS == 0xf000) &&
        ((cpu_state.pc & 0xfffffff8) == 0x00007278) &&
        (machines[machine].init == machine_at_wd76c10_init))  switch (addr) {
        /* The Amstrad MegaPC Quadtel BIOS times a sequence of:
               mov ax,di
               div bx
           And expects this value to be at least 0x06e0 for 20 MHz,
           and at least 0x0898 for 25 MHz, everything below 0x06e0
           is assumed to be 16 MHz. Given that for some reason, this
           does not occur on 86Box, we have to work around it here,
           we return 0x0580 for 16 MHz, because it logically follows
           in the sequence (0x06e0 = 0x0898 * (20 / 25), and
           0x0580 = 0x06e0 * (16 / 20)). */
        case 0x0081:
            if (cpu_busspeed >= 25000000)
                ret = 0x98;
            else if (cpu_busspeed >= 20000000)
                ret = 0xe0;
            else
                ret = 0x80;
            break;
        case 0x0082:
            if (cpu_busspeed >= 25000000)
                ret = 0x08;
            else if (cpu_busspeed >= 20000000)
                ret = 0x06;
            else
                ret = 0x05;
            break;
    } else {
        addr &= 0x0f;
        ret = dmaregs[2][addr];

        if (addr >= 8)
            addr = convert[addr & 0x07] | 4;
        else
            addr = convert[addr & 0x07];

        if (addr < 8) {
            ret = dma[addr].page_l;
            /* In extended address mode the Current Low Page is the third
               byte of the address counter, carried into as it counts. On a
               channel counting in words it is still an eight-bit register:
               "the least significant bit of the Low Page register is ignored
               when the address is driven out onto the bus" (82374EB 6.x,
               Extended Mode Register), not when it is read, and bit 16 of
               the counter there is address register bit 15. Phoenix's POST
               writes FFh to every page register and reads it back (test 06,
               F000:1E12 on the Siemens-Nixdorf D823), and FEh from channel 5
               stopped the board with a beep code. */
            if (dma_eisa && dma[addr].ext_addr) {
                ret = (dma[addr].ac >> 16) & 0xff;
                if (dma_addr_shifted(&dma[addr]))
                    ret = (ret & 0xfe) | (dma[addr].page_l & 0x01);
            }
        }
    }

    dma_log("DMA: [R] %04X = %02X\n", addr, ret);

    return ret;
}

static void
dma_high_page_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    uint8_t convert[8] = CHANNELS;

    addr &= 0x0f;

    if (addr >= 8)
        addr = convert[addr & 0x07] | 4;
    else
        addr = convert[addr & 0x07];

    if (addr < 8) {
        dma[addr].page_h = val;
        /* Written after the rest of the address: extended address mode. */
        if (dma_eisa)
            dma[addr].ext_addr = 1;

        dma[addr].ab = ((dma[addr].ab & 0xffffff) | (dma[addr].page_h << 24)) & dma_mask;
        if (!dma_chaining(addr))
            dma[addr].ac = ((dma[addr].ac & 0xffffff) | (dma[addr].page_h << 24)) & dma_mask;
    }
}

static uint8_t
dma_high_page_read(uint16_t addr, UNUSED(void *priv))
{
    uint8_t convert[8] = CHANNELS;
    uint8_t ret        = 0xff;

    addr &= 0x0f;

    if (addr >= 8)
        addr = convert[addr & 0x07] | 4;
    else
        addr = convert[addr & 0x07];

    if (addr < 8)
        ret = (dma_eisa && dma[addr].ext_addr) ? ((dma[addr].ac >> 24) & 0xff) : dma[addr].page_h;

    return ret;
}

void
dma_set_params(uint8_t advanced, uint32_t mask)
{
    dma_advanced = advanced;
    dma_mask     = mask;
}

void
dma_set_mask(uint32_t mask)
{
    dma_mask = mask;

    for (uint8_t i = 0; i < 8; i++) {
        dma[i].ab &= mask;
        dma[i].ac &= mask;
    }
}

void
dma_set_at(uint8_t at)
{
    dma_at = at;
}

void
dma_reset_legacy(void)
{
    int c;

    dma_wp[0] = dma_wp[1] = 0;
    dma_m                 = 0;

    dma_e = 0xff;

    for (c = 0; c < 16; c++)
        dmaregs[0][c] = dmaregs[1][c] = 0;

    for (c = 0; c < 8; c++) {
        memset(&(dma[c]), 0x00, sizeof(dma_t));
        dma[c].ps2_mode      = (dma_ps2.is_ps2 && (c & 4)) ? DMA_PS2_SIZE16 : 0;
        dma[c].size          = (c & 4) ? 1 : 0;
        dma[c].transfer_mode = (c & 4) ? 0x0202 : 0x0101;
        /* An EISA controller's extended mode after reset: the ISA widths,
           8-bit counted in bytes on 0-3, 16-bit counted in words with the
           address shifted on 5-7. */
        dma[c].ext_mode      = (c & 4) ? 0x04 : 0x00;
    }

    dma_stat          = 0x00;
    dma_stat_rq       = 0x00;
    dma_stat_rq_pc    = 0x00;
    dma_sw_req        = 0x00;
    dma_stat_adv_pend = 0x00;
    dma_req_is_soft   = 0;
    dma_advanced      = 0;

    memset(dma_buffer, 0x00, sizeof(dma_buffer));
    memset(dma16_buffer, 0x00, sizeof(dma16_buffer));

    dma_remove_sg();
    dma_sg_base = 0x0400;
    memset(dma_chain_mode, 0x00, sizeof(dma_chain_mode));
    dma_chain_int = 0x00;
    dma_sg_int    = 0x00;
    for (c = 0; c < 8; c++)
        dma[c].sg_status = DMA_SG_BASE; /* 08h after reset (3.2.22) */
    dma_stop_en   = 0x00;

    dma_mask = 0x00ffffff;

    /* A reset clears the registers, not the controller's kind. The power-on
       reset comes after the chipset has said what it has, so an EISA
       controller must come out of it an EISA controller again: full-width
       addresses, and the advanced paths its extensions live on. */
    if (dma_eisa) {
        dma_advanced = 1;
        dma_mask     = 0xffffffff;
        /* Reset sets the masks and puts channel 4 in cascade (82374EB
           3.2.2, 3.2.5). */
        dma_m        = 0xff;
        dma[4].mode  = 0xc0;
    }

    dma_at = is286;
}

void
dma_remove_sg(void)
{
    io_removehandler(dma_sg_base + 0x0a, 0x01,
                     dma_sg_int_status_read, NULL, NULL,
                     NULL, NULL, NULL,
                     NULL);

    for (uint8_t i = 0; i < 8; i++) {
        if (i == 4)
            continue; /* the cascade has none */
        io_removehandler(dma_sg_base + 0x10 + i, 0x01,
                         dma_sg_read, NULL, NULL, dma_sg_write, NULL, NULL, NULL);
        io_removehandler(dma_sg_base + 0x18 + i, 0x01,
                         dma_sg_read, NULL, NULL, dma_sg_write, NULL, NULL, NULL);
        io_removehandler(dma_sg_base + 0x20 + i * 4, 0x04,
                         dma_sg_read, NULL, NULL, dma_sg_write, NULL, NULL, NULL);
    }
}

void
dma_set_sg_base(uint8_t sg_base)
{
    dma_sg_base = sg_base << 8;

    io_sethandler(dma_sg_base + 0x0a, 0x01,
                  dma_sg_int_status_read, NULL, NULL,
                  NULL, NULL, NULL,
                  NULL);

    for (uint8_t i = 0; i < 8; i++) {
        if (i == 4)
            continue; /* the cascade has none */
        io_sethandler(dma_sg_base + 0x10 + i, 0x01,
                      dma_sg_read, NULL, NULL, dma_sg_write, NULL, NULL, NULL);
        io_sethandler(dma_sg_base + 0x18 + i, 0x01,
                      dma_sg_read, NULL, NULL, dma_sg_write, NULL, NULL, NULL);
        io_sethandler(dma_sg_base + 0x20 + i * 4, 0x04,
                      dma_sg_read, NULL, NULL, dma_sg_write, NULL, NULL, NULL);
    }
}

void
dma_ext_mode_init(void)
{
    io_sethandler(0x040b, 0x01,
                  NULL, NULL, NULL, dma_ext_mode_write, NULL, NULL, NULL);
    io_sethandler(0x04d6, 0x01,
                  NULL, NULL, NULL, dma_ext_mode_write, NULL, NULL, NULL);
}

void
dma_high_page_init(void)
{
    io_sethandler(0x0480, 8,
                  dma_high_page_read, NULL, NULL, dma_high_page_write, NULL, NULL, NULL);
}

/* EISA gives every channel a third byte of count, at 0401h/0403h/0405h/
   0407h for channels 0 to 3 and 04C6h/04CAh/04CEh for 5 to 7. Writing the
   ordinary count registers clears it, which the masks there already do, so
   software that knows nothing of it keeps getting sixteen bit counts. */
static int
dma_high_count_channel(uint16_t addr)
{
    if ((addr & 0xfff9) == 0x0401)
        return (addr >> 1) & 3;
    if ((addr == 0x04c6) || (addr == 0x04ca) || (addr == 0x04ce))
        return 4 | ((addr >> 2) & 3);
    return -1;
}

static uint8_t
dma_high_count_read(uint16_t addr, UNUSED(void *priv))
{
    int channel = dma_high_count_channel(addr);

    if (channel < 0)
        return 0xff;

    return (uint8_t) ((dma[channel].cc >> 16) & 0xff);
}

static void
dma_high_count_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    int channel = dma_high_count_channel(addr);

    if (channel < 0)
        return;

    dma[channel].cb = (dma[channel].cb & 0x00ffff) | ((uint32_t) val << 16);
    if (!dma_chaining(channel))
        dma[channel].cc = (int) dma[channel].cb;
}

/* 040Ah and 04D4h, written: the chaining mode of one channel, chosen by
   the low two bits the way the 8237's own mode register chooses. */
static void
dma_chain_mode_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    int channel = (val & 3) | ((addr == 0x04d4) ? 4 : 0);

    dma_chain_mode[channel] = val & 0x1c;

    /* Programming the next buffer, or giving chaining up, is what answers
       the interrupt. */
    if ((val & 0x08) || !(val & 0x04)) {
        dma_chain_int &= ~(1 << channel);
        dma_irq13_update();
    }
}

/* 04D4h, read: which channels have chaining on. */
static uint8_t
dma_chain_status_read(UNUSED(uint16_t addr), UNUSED(void *priv))
{
    uint8_t ret = 0x00;

    for (uint8_t i = 0; i < 8; i++)
        if ((i != 4) && (dma_chain_mode[i] & 0x04))
            ret |= (1 << i);

    return ret;
}

/* 040Ch, read: which channels signal an expired buffer with TC rather than
   IRQ13. */
static uint8_t
dma_chain_bec_read(UNUSED(uint16_t addr), UNUSED(void *priv))
{
    uint8_t ret = 0x00;

    for (uint8_t i = 0; i < 8; i++)
        if ((i != 4) && (dma_chain_mode[i] & 0x10))
            ret |= (1 << i);

    return ret;
}

/* 04E0h-04FEh: three bytes of stop address a channel, four ports apart,
   with nothing at channel 4's. The bottom two bits are not kept. */
static uint8_t
dma_stop_read(uint16_t addr, UNUSED(void *priv))
{
    int channel = (addr >> 2) & 7;
    int byte    = addr & 3;

    if ((channel == 4) || (byte == 3))
        return 0xff;

    return dma_stop[channel][byte];
}

static void
dma_stop_write(uint16_t addr, uint8_t val, UNUSED(void *priv))
{
    int channel = (addr >> 2) & 7;
    int byte    = addr & 3;

    if ((channel == 4) || (byte == 3))
        return;

    dma_stop[channel][byte] = byte ? val : (val & 0xfc);
}

/* What an EISA DMA controller has over the PCI-ISA bridges' one: the high
   count registers, the high page registers of the sixteen bit channels
   (0489h-048Bh, which the eight ports above stop short of), and twenty-four
   bit counts in scatter-gather descriptors. */
void
dma_eisa_init(void)
{
    for (uint16_t port = 0x0401; port <= 0x0407; port += 2)
        io_sethandler(port, 1, dma_high_count_read, NULL, NULL,
                      dma_high_count_write, NULL, NULL, NULL);
    for (uint16_t port = 0x04c6; port <= 0x04ce; port += 4)
        io_sethandler(port, 1, dma_high_count_read, NULL, NULL,
                      dma_high_count_write, NULL, NULL, NULL);

    io_sethandler(0x0488, 8,
                  dma_high_page_read, NULL, NULL, dma_high_page_write, NULL, NULL, NULL);

    /* 040Ah reads through the scatter-gather interrupt status handler,
       which is at the same place and reports chaining interrupts as well. */
    io_sethandler(0x040a, 1, NULL, NULL, NULL,
                  dma_chain_mode_write, NULL, NULL, NULL);
    io_sethandler(0x04d4, 1, dma_chain_status_read, NULL, NULL,
                  dma_chain_mode_write, NULL, NULL, NULL);
    io_sethandler(0x040c, 1, dma_chain_bec_read, NULL, NULL,
                  NULL, NULL, NULL, NULL);
    io_sethandler(0x04e0, 32, dma_stop_read, NULL, NULL,
                  dma_stop_write, NULL, NULL, NULL);

    dma_sg_count_mask = 0x00ffffff;
    dma_eisa          = 1;
}

void
dma_reset(void)
{
    dma_reset_legacy();
    if (dma_ibm5140)
        dma_e = 0x0e; /* RESET cannot create the absent refresh channel. */

    dma_command[0] = dma_command[1] = 0;
    memset(&dma_xt8237, 0, sizeof(dma_xt8237));
    dma_xt8237.last_service = 3;
    dma_xt_refresh_queued = false;
    dma_xt_refresh_scheduled = false;

    if (dma_ps2.is_ps2) {
        dma_m               = 0xff;
        dma_ps2.byte_ptr    = 0;
        dma_ps2.arb_control = 0;
        dma_ps2.arb_status  = 0;
        dma[0].arb_level    = 0;
        dma[4].arb_level    = 4;
    }

    if (dma_page_is_xt())
        dma_m = (dma_m & 0xf0) | 0x0f;
}

void
dma_init(void)
{
    dma_ibm5140 = dma_ibm5140_diag = 0;
    dma_ps2.is_ps2 = 0;
    /* What kind of controller this is, which only a new machine changes; an
       EISA chipset sets it again after this. */
    dma_eisa          = 0;
    dma_sg_count_mask = 0x0000fffe;
    dma_reset();

    io_sethandler(0x0000, 16,
                  dma_read, NULL, NULL, dma_write, NULL, NULL, NULL);
    io_sethandler(0x0080, 8,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_init_ibm5140(void)
{
    dma_ibm5140 = 1;
    dma_ibm5140_diag = 0;
    dma_e = 0x0e;
    io_removehandler(0x0080, 8,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x0081, 3,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma16_init(void)
{
    dma_reset();

    io_sethandler(0x00C0, 32,
                  dma16_read, NULL, NULL, dma16_write, NULL, NULL, NULL);
    io_sethandler(0x0088, 8,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_set(void)
{
    io_sethandler(0x0090, 2,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x0093, 13,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_set_piix(void)
{
    io_sethandler(0x0090, 1,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x0094, 3,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x0098, 1,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_sethandler(0x009C, 3,
                  dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_remove(void)
{
    io_removehandler(0x0090, 2,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x0093, 13,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
dma_alias_remove_piix(void)
{
    io_removehandler(0x0090, 1,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x0094, 3,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x0098, 1,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
    io_removehandler(0x009C, 3,
                     dma_page_read, NULL, NULL, dma_page_write, NULL, NULL, NULL);
}

void
ps2_dma_init(void)
{
    dma_ps2.is_ps2 = 1;
    dma_reset();

    io_sethandler(0x0018, 1,
                  dma_ps2_read, NULL, NULL, dma_ps2_write, NULL, NULL, NULL);
    io_sethandler(0x001a, 1,
                  dma_ps2_read, NULL, NULL, dma_ps2_write, NULL, NULL, NULL);
    io_sethandler(0x0090, 1,
                  dma_ps2_arb_read, NULL, NULL, dma_ps2_arb_write, NULL, NULL, NULL);
}


uint8_t
_dma_read(uint32_t addr, dma_t *dma_c)
{
    uint8_t temp = 0;

    if (dma_advanced) {
        dma_bm_read(addr, &temp, 1, dma_transfer_size(dma_c));
    } else
        temp = mem_readb_phys(addr);

    return temp;
}

static uint16_t
_dma_readw(uint32_t addr, dma_t *dma_c)
{
    uint16_t temp = 0;

    if (dma_advanced) {
        dma_bm_read(addr, (uint8_t *) &temp, 2, dma_transfer_size(dma_c));
    } else
        temp = _dma_read(addr, dma_c) | (_dma_read(addr + 1, dma_c) << 8);

    return temp;
}

static void
_dma_write(uint32_t addr, uint8_t val, dma_t *dma_c)
{
    if (dma_advanced) {
        dma_bm_write(addr, &val, 1, dma_transfer_size(dma_c));
    } else {
        mem_writeb_phys(addr, val);
        if (dma_at)
            mem_invalidate_range(addr, addr);
    }
}

static void
_dma_writew(uint32_t addr, uint16_t val, dma_t *dma_c)
{
    if (dma_advanced) {
        dma_bm_write(addr, (uint8_t *) &val, 2, dma_transfer_size(dma_c));
    } else {
        _dma_write(addr, val & 0xff, dma_c);
        _dma_write(addr + 1, val >> 8, dma_c);
    }
}

static uint32_t
_dma_readl(uint32_t addr, dma_t *dma_c)
{
    uint32_t temp = 0;

    dma_bm_read(addr, (uint8_t *) &temp, 4, dma_transfer_size(dma_c));

    return temp;
}

static void
_dma_writel(uint32_t addr, uint32_t val, dma_t *dma_c)
{
    dma_bm_write(addr, (uint8_t *) &val, 4, dma_transfer_size(dma_c));
}

static void
dma_retreat_n(dma_t *dma_c, int as)
{

    if (dma_c->sg_status & DMA_SG_ACTIVE) {
        dma_c->ac = (dma_c->ac - as) & dma_mask;

        dma_c->page = dma_c->page_l = (dma_c->ac >> 16) & 0xff;
        dma_c->page_h               = (dma_c->ac >> 24) & 0xff;
    } else if (dma_eisa && dma_c->ext_addr) {
        /* In extended address mode an EISA controller's address counter is
           the whole address: it carries into the page bits, and a transfer
           crosses 64 KB (128 KB on the 16-bit channels) with no wrap.
           Windows NT's EISA HAL writes the high page last to get this and
           does not split transfers at those boundaries. */
        dma_c->ac = (dma_c->ac - as) & dma_mask;
    } else if (dma_eisa ? dma_addr_shifted(dma_c) : (as == 2))
        dma_c->ac = ((dma_c->ac & 0xfffe0000) & dma_mask) | ((dma_c->ac - as) & 0x1ffff);
    else
        dma_c->ac = ((dma_c->ac & 0xffff0000) & dma_mask) | ((dma_c->ac - as) & 0xffff);
}

static void
dma_retreat(dma_t *dma_c)
{
    dma_retreat_n(dma_c, dma_c->transfer_mode >> 8);
}

static void
dma_advance_n(dma_t *dma_c, int as)
{

    if (dma_c->sg_status & DMA_SG_ACTIVE) {
        dma_c->ac = (dma_c->ac + as) & dma_mask;

        dma_c->page = dma_c->page_l = (dma_c->ac >> 16) & 0xff;
        dma_c->page_h               = (dma_c->ac >> 24) & 0xff;
    } else if (dma_eisa && dma_c->ext_addr) {
        /* No 64 KB wrap in extended address mode; see dma_retreat_n(). */
        dma_c->ac = (dma_c->ac + as) & dma_mask;
    } else if (dma_eisa ? dma_addr_shifted(dma_c) : (as == 2))
        dma_c->ac = ((dma_c->ac & 0xfffe0000) & dma_mask) | ((dma_c->ac + as) & 0x1ffff);
    else
        dma_c->ac = ((dma_c->ac & 0xffff0000) & dma_mask) | ((dma_c->ac + as) & 0xffff);
}

void
dma_advance(dma_t *dma_c)
{
    dma_advance_n(dma_c, dma_c->transfer_mode >> 8);
}


static int dma_channel_readable_legacy(int channel);
static int dma_channel_writable_legacy(int channel);
/* The ring buffer stop: a channel whose stop register is switched on halts
   when its address reaches the one in it, which is how the far end of a
   ring is kept from being overrun. The bottom two bits are not compared. */
static void
dma_stop_check(int channel)
{
    uint32_t stop;

    if (!(dma_stop_en & (1 << channel)))
        return;

    stop = dma_stop[channel][0] | (dma_stop[channel][1] << 8) | (dma_stop[channel][2] << 16);

    /* "The last address transferred before the channel is masked is the
       first address that matches the Stop register" (82374EB, Table 18):
       the address just transferred, not the next one, which has already
       been counted to. */
    {
        const dma_t *dma_c = &dma[channel];
        int          n     = dma_c->xfer_n ? dma_c->xfer_n : (dma_c->transfer_mode >> 8);
        uint32_t     last  = (dma_c->mode & 0x20) ? (dma_c->ac + n) : (dma_c->ac - n);

        if ((last & 0x00fffffc) == (stop & 0x00fffffc))
            dma_m |= (1 << channel);
    }
}

static int dma_channel_read_only_legacy(int channel);
static int dma_channel_advance_legacy(int channel);
static int dma_channel_read_legacy(int channel);
static int dma_channel_write_legacy(int channel, uint16_t val);

static int
dma_xt8237_mem_to_mem(void)
{
    dma_t *source = &dma[0];
    dma_t *dest = &dma[1];
    int terminal = 0;

    if (dma_xt8237.in_mem_to_mem)
        return 0;
    if (!(dma_command[0] & 0x01))
        return 0;
    if (!dma_xt8237_can_service(0))
        return 0;

    dma_xt8237.in_mem_to_mem = 1;
    dma_xt8237_begin_service(0);

    do {
        sub_cycles((dma_command[0] & 0x08) ? 8 : 10);
        dma_xt8237.temp = _dma_read(source->ac, source);
        _dma_write(dest->ac, dma_xt8237.temp, dest);

        if (!(dma_command[0] & 0x02))
            dma_xt8237_advance_address(0);
        dma_xt8237_advance_address(1);

        dest->cc--;
        if ((dest->cc < 0) || (dma_xt8237.external_eop & 0x02)) {
            terminal = 1;
            dma_stat |= 0x02;
            dma_xt8237.external_eop &= ~0x02;
            dma_xt8237.sw_request &= ~0x01;

            if (dest->mode & 0x10) {
                dest->cc = dest->cb;
                dest->ac = dest->ab;
                source->ac = source->ab;
            } else {
                dma_m |= 0x02;
            }

            dma_xt8237_release_owner(0);
        }
    } while (!terminal &&
             (((source->mode >> 6) & 3) == 2) &&
             dma_xt8237_can_service(0));

    if (((source->mode >> 6) & 3) == 1)
        dma_xt8237_release_owner(0);

    dma_xt8237.in_mem_to_mem = 0;
    return terminal;
}

int
dma_channel_readable(int channel)
{
    int type;

    if (!dma_xt8237_active())
        return dma_channel_readable_legacy(channel);

    if (!dma_xt8237_can_service(channel))
        return 0;

    type = dma[channel].mode & 0x0c;
    return (type == 0x08) || (type == 0x00);
}

int
dma_channel_writable(int channel)
{
    int type;

    if (!dma_xt8237_active())
        return dma_channel_writable_legacy(channel);

    if (!dma_xt8237_can_service(channel))
        return 0;

    type = dma[channel].mode & 0x0c;
    return (type == 0x04) || (type == 0x00);
}

/* Execute the channel-0 address-only refresh transfer at physical DACK0. */
static void
dma_xt_refresh_dack(void *opaque)
{
    (void)opaque;

    if (!m808x_86box_active()) {
        /* Legacy CPU paths receive their established refresh charge through
         * dma_channel_read(0). */
        (void)dma_channel_read(0);
    } else {
        /* PC/XT DRAM refresh is RAS/address-only. The Marty CPU already owns
         * HOLD/HLDA/DMAWAIT; do not perform a fake host memory read or charge
         * a second synthetic bus delay. */
        if (dma_stat_adv_pend & 0x01)
            (void)dma_channel_advance(0);

        dma_xt8237_begin_service(0);
        dma_xt8237_advance_address(0);
        dma_stat_rq |= 0x01;
        dma_stat_adv_pend &= (uint8_t)~0x01;
        (void)dma_xt8237_finish_transfer(0);
    }

    /* DACK0 resets the external request latch. Clear queue state before the
     * DRQ helper reconciles, so the falling edge cannot resubmit this transfer. */
    dma_xt_refresh_queued = false;
    dma_xt_refresh_scheduled = false;
    dma_set_drq(0, 0);
}

static void
dma_xt_refresh_try_schedule(void)
{
    if (!dma_xt_refresh_queued || dma_xt_refresh_scheduled)
        return;
    if (!dma_xt8237_active() || !dma_xt8237_can_service(0))
        return;

    if (m808x_86box_active()) {
        if (m808x_86box_dma_try_request_ex(6u, dma_xt_refresh_dack, NULL))
            dma_xt_refresh_scheduled = true;
    } else {
        dma_xt_refresh_scheduled = true;
        dma_xt_refresh_dack(NULL);
    }
}

static void
dma_xt_refresh_reconcile(void)
{
    const bool serviceable = dma_xt_refresh_queued &&
                             dma_xt8237_active() &&
                             dma_xt8237_can_service(0);

    if (dma_xt_refresh_scheduled && !serviceable &&
        m808x_86box_active()) {
        if (m808x_86box_dma_cancel_request_ex(dma_xt_refresh_dack, NULL))
            dma_xt_refresh_scheduled = false;
    }

    if (serviceable && !dma_xt_refresh_scheduled)
        dma_xt_refresh_try_schedule();
}

void
dma_xt_refresh_request(void)
{
    /* PIT1 sets an external request latch; DACK0 clears it. Repeated PIT edges
     * while the latch is already set do not accumulate transfers. */
    if (!dma_xt_refresh_queued) {
        dma_xt_refresh_queued = true;
        /* dma_set_drq() performs the first reconciliation. */
        dma_set_drq(0, 1);
    } else {
        dma_xt_refresh_reconcile();
    }
}

int
dma_channel_read_only(int channel)
{
    dma_t *dma_c;
    int temp;
    int type;

    if (!dma_xt8237_active())
        return dma_channel_read_only_legacy(channel);

    if (!dma_xt8237_can_service(channel))
        return DMA_NODATA;

    type = dma[channel].mode & 0x0c;
    if ((type != 0x08) && (type != 0x00))
        return DMA_NODATA;

    if (dma_stat_adv_pend & (1 << channel))
        (void) dma_channel_advance(channel);

    dma_c = &dma[channel];
    dma_xt8237_begin_service(channel);
    dma_xt8237_charge_bus(channel);

    if (type == 0x00)
        temp = DMA_VERIFY;
    else
        temp = _dma_read(dma_c->ac, dma_c);

    dma_xt8237_advance_address(channel);
    dma_stat_rq |= 1 << channel;
    dma_stat_adv_pend |= 1 << channel;

    return temp;
}

int
dma_channel_advance(int channel)
{
    int tc = 0;

    if (!dma_xt8237_active())
        return dma_channel_advance_legacy(channel);

    if ((channel < 0) || (channel > 3))
        return 0;

    if (dma_stat_adv_pend & (1 << channel)) {
        dma_stat_adv_pend &= ~(1 << channel);
        tc = dma_xt8237_finish_transfer(channel);
    }

    if ((channel != 0) && dma_xt_refresh_queued)
        dma_xt_refresh_reconcile();

    return tc;
}

int
dma_channel_read(int channel)
{
    dma_t *dma_c;
    int temp;
    int type;
    int tc;

    if (!dma_xt8237_active())
        return dma_channel_read_legacy(channel);

    if ((channel < 0) || (channel > 3))
        return DMA_NODATA;

    if (dma_stat_adv_pend & (1 << channel))
        (void) dma_channel_advance(channel);

    if (!dma_xt8237_can_service(channel))
        return DMA_NODATA;

    type = dma[channel].mode & 0x0c;
    if ((type != 0x08) && (type != 0x00))
        return DMA_NODATA;

    dma_c = &dma[channel];
    dma_xt8237_begin_service(channel);
    dma_xt8237_charge_bus(channel);

    if (type == 0x00)
        temp = DMA_VERIFY;
    else
        temp = _dma_read(dma_c->ac, dma_c);

    dma_xt8237_advance_address(channel);
    dma_stat_rq |= 1 << channel;
    tc = dma_xt8237_finish_transfer(channel);

    if ((channel != 0) && dma_xt_refresh_queued)
        dma_xt_refresh_reconcile();

    if (tc)
        return temp | DMA_OVER;
    return temp;
}

int
dma_channel_write(int channel, uint16_t val)
{
    dma_t *dma_c;
    int type;
    int tc;

    if (!dma_xt8237_active())
        return dma_channel_write_legacy(channel, val);

    if ((channel < 0) || (channel > 3))
        return DMA_NODATA;
    if (!dma_xt8237_can_service(channel))
        return DMA_NODATA;

    type = dma[channel].mode & 0x0c;
    if ((type != 0x04) && (type != 0x00))
        return DMA_NODATA;

    dma_c = &dma[channel];
    dma_xt8237_begin_service(channel);
    dma_xt8237_charge_bus(channel);

    if (type == 0x04)
        _dma_write(dma_c->ac, val & 0xff, dma_c);

    dma_xt8237_advance_address(channel);
    dma_stat_rq |= 1 << channel;
    dma_stat_adv_pend &= ~(1 << channel);
    tc = dma_xt8237_finish_transfer(channel);

    if ((channel != 0) && dma_xt_refresh_queued)
        dma_xt_refresh_reconcile();

    return tc ? DMA_OVER : 0;
}

/* A 32-bit DMA slave on an EISA channel whose extended mode is 32-bit I/O
   counted in bytes. Answers DMA_NODATA, or 0 or DMA_OVER at terminal
   count, as dma_channel_read does; the data goes through val. On any
   other channel it is the 16-bit transfer, zero-extended. */
static int
dma_channel_32_ready(int channel, int want)
{
    const dma_t *dma_c = &dma[channel];

    if (dma_group_disabled(channel))
        return 0;
    if (!(dma_e & (1 << channel)))
        return 0;
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        return 0;
    return ((dma_c->mode & 0x0c) == want) || ((want == 0x04) && !(dma_c->mode & 0x0c));
}

static int
dma_channel_32_finish(int channel, int n)
{
    dma_t *dma_c = &dma[channel];
    int    tc    = 0;

    dma_c->xfer_n = n;
    if (dma_c->mode & 0x20)
        dma_retreat_n(dma_c, n);
    else
        dma_advance_n(dma_c, n);

    dma_stat_rq |= (1 << channel);
    dma_stat_adv_pend &= ~(1 << channel);
    dma_stop_check(channel);

    dma_c->cc -= dma_count_take(dma_c);
    if (dma_c->cc < 0)
        tc = dma_count_out(channel);

    return tc ? DMA_OVER : 0;
}

int
dma_channel_read32(int channel, uint32_t *val)
{
    dma_t *dma_c = &dma[channel];
    int    ret;
    int    n;

    if (!dma_eisa || (dma_c->size != 2)) {
        ret = dma_channel_read(channel);
        if (ret == DMA_NODATA)
            return DMA_NODATA;
        *val = ret & 0xffff;
        return ret & DMA_OVER;
    }

    if (!dma_channel_32_ready(channel, 0x08))
        return DMA_NODATA;
    if (dma_stat_adv_pend & (1 << channel))
        dma_channel_advance(channel);
    dma_sg_load(dma_c);

    /* A partial dword at an unaligned start or at the end of the count. */
    n    = dma_xfer_bytes(dma_c);
    *val = 0;
    if (n == 4)
        *val = _dma_readl(dma_c->ac, dma_c);
    else
        for (int i = 0; i < n; i++)
            *val |= (uint32_t) (_dma_read(dma_c->ac + i, dma_c) & 0xff) << (i * 8);
    return dma_channel_32_finish(channel, n);
}

int
dma_channel_write32(int channel, uint32_t val)
{
    dma_t *dma_c = &dma[channel];
    int    n;

    if (!dma_eisa || (dma_c->size != 2))
        return dma_channel_write(channel, val & 0xffff);

    if (!dma_channel_32_ready(channel, 0x04))
        return DMA_NODATA;
    dma_sg_load(dma_c);

    n = dma_xfer_bytes(dma_c);
    if ((dma_c->mode & 0x0c) == 0x04) {
        if (n == 4)
            _dma_writel(dma_c->ac, val, dma_c);
        else
            for (int i = 0; i < n; i++)
                _dma_write(dma_c->ac + i, (val >> (i * 8)) & 0xff, dma_c);
    }
    return dma_channel_32_finish(channel, n);
}

int
dma_channel_readable_legacy(int channel)
{
    dma_t   *dma_c = &dma[channel];
    int      ret = 1;

    if (dma_group_disabled(channel))
        ret = 0;

    if (!(dma_e & (1 << channel)))
        ret = 0;
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        ret = 0;
    if ((dma_c->mode & 0xC) != 8)
        ret = 0;

    return ret;
}

int
dma_channel_writable_legacy(int channel)
{
    dma_t   *dma_c = &dma[channel];
    int      ret = 1;

    if (dma_group_disabled(channel))
        ret = 0;

    if (!(dma_e & (1 << channel)))
        ret = 0;
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        ret = 0;
    if ((dma_c->mode & 0xC) != 4)
        ret = 0;

    return ret;
}

int
dma_channel_read_only_legacy(int channel)
{
    dma_t   *dma_c = &dma[channel];
    uint16_t temp;

    if (dma_group_disabled(channel))
        return (DMA_NODATA);

    if (!(dma_e & (1 << channel)))
        return (DMA_NODATA);
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        return (DMA_NODATA);
    if ((dma_c->mode & 0xC) != 8)
        return (DMA_NODATA);

    dma_channel_advance(channel);
    dma_sg_load(dma_c);

    if (!dma_at && !channel)
        is_nec ? refreshread_vx0() : refreshread();

    if (dma_eisa) {
        /* An EISA channel moves its width, or less at an odd start or the
           end of a count by bytes, and steps the address by what it moved. */
        int n = dma_xfer_bytes(dma_c);

        temp          = (n == 1) ? _dma_read(dma_c->ac, dma_c) : _dma_readw(dma_c->ac, dma_c);
        dma_c->xfer_n = n;
        if (dma_c->mode & 0x20)
            dma_retreat_n(dma_c, n);
        else
            dma_advance_n(dma_c, n);
    } else if (!dma_c->size) {
        temp = _dma_read(dma_c->ac, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac--;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac - 1) & 0xffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac++;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac + 1) & 0xffff);
        }
    } else {
        temp = _dma_readw(dma_c->ac, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac -= 2;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac - 2) & 0x1ffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac += 2;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac + 2) & 0x1ffff);
        }
    }

    dma_stat_rq |= (1 << channel);

    dma_stat_adv_pend |= (1 << channel);

    return temp;
}

int
dma_channel_advance_legacy(int channel)
{
    dma_t   *dma_c = &dma[channel];
    int      tc = 0;

    if (dma_stat_adv_pend & (1 << channel)) {
        dma_stop_check(channel);

        dma_c->cc -= dma_count_take(dma_c);
        if (dma_c->cc < 0)
            tc = dma_count_out(channel);

        dma_stat_adv_pend &= ~(1 << channel);
    }

    return tc;
}

int
dma_channel_read_legacy(int channel)
{
    dma_t   *dma_c = &dma[channel];
    uint16_t temp;
    int      tc = 0;

    if (dma_group_disabled(channel))
        return (DMA_NODATA);

    if (!(dma_e & (1 << channel)))
        return (DMA_NODATA);
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        return (DMA_NODATA);
    if ((dma_c->mode & 0xC) != 8)
        return (DMA_NODATA);

    if (dma_stat_adv_pend & (1 << channel))
        dma_channel_advance(channel);
    dma_sg_load(dma_c);

    if (!dma_at && !channel)
        is_nec ? refreshread_vx0() : refreshread();

    if (dma_eisa) {
        /* An EISA channel moves its width, or less at an odd start or the
           end of a count by bytes, and steps the address by what it moved. */
        int n = dma_xfer_bytes(dma_c);

        temp          = (n == 1) ? _dma_read(dma_c->ac, dma_c) : _dma_readw(dma_c->ac, dma_c);
        dma_c->xfer_n = n;
        if (dma_c->mode & 0x20)
            dma_retreat_n(dma_c, n);
        else
            dma_advance_n(dma_c, n);
    } else if (!dma_c->size) {
        temp = _dma_read(dma_c->ac, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac--;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac - 1) & 0xffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac++;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac + 1) & 0xffff);
        }
    } else {
        temp = _dma_readw(dma_c->ac, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac -= 2;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac - 2) & 0x1ffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac += 2;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac + 2) & 0x1ffff);
        }
    }

    dma_stat_rq |= (1 << channel);

    dma_stop_check(channel);

    dma_c->cc -= dma_count_take(dma_c);
    if (dma_c->cc < 0)
        tc = dma_count_out(channel);

    if (tc)
        return (temp | DMA_OVER);

    return temp;
}

int
dma_channel_write_legacy(int channel, uint16_t val)
{
    dma_t *dma_c = &dma[channel];
    int    type;
    int    sg;
    int    tc = 0;

    if (dma_group_disabled(channel))
        return (DMA_NODATA);

    if (!(dma_e & (1 << channel)))
        return (DMA_NODATA);
    if ((dma_m & (1 << channel)) && !dma_req_is_soft)
        return (DMA_NODATA);
    type = dma_c->mode & 0x0c;
    if ((type != 0x04) && (type != 0x00))
        return (DMA_NODATA);
    dma_sg_load(dma_c);

    if (dma_eisa) {
        /* See the read: the width, or less at the ends of a count by bytes. */
        int n = dma_xfer_bytes(dma_c);

        if (type == 0x04) {
            if (n == 1)
                _dma_write(dma_c->ac, val & 0xff, dma_c);
            else
                _dma_writew(dma_c->ac, val, dma_c);
        }
        dma_c->xfer_n = n;
        if (dma_c->mode & 0x20)
            dma_retreat_n(dma_c, n);
        else
            dma_advance_n(dma_c, n);
    } else if (!dma_c->size) {
        if (type == 0x04)
            _dma_write(dma_c->ac, val & 0xff, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac--;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac - 1) & 0xffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac++;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xffff0000 & dma_mask) | ((dma_c->ac + 1) & 0xffff);
        }
    } else {
        if (type == 0x04)
            _dma_writew(dma_c->ac, val, dma_c);

        if (dma_c->mode & 0x20) {
            if (dma_ps2.is_ps2)
                dma_c->ac -= 2;
            else if (dma_advanced)
                dma_retreat(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac - 2) & 0x1ffff);
        } else {
            if (dma_ps2.is_ps2)
                dma_c->ac += 2;
            else if (dma_advanced)
                dma_advance(dma_c);
            else
                dma_c->ac = (dma_c->ac & 0xfffe0000 & dma_mask) | ((dma_c->ac + 2) & 0x1ffff);
        }
    }

    dma_stat_rq |= (1 << channel);

    dma_stat_adv_pend &= ~(1 << channel);

    dma_stop_check(channel);

    sg = (dma_c->sg_status & DMA_SG_ACTIVE) || dma_chaining(channel);
    dma_c->cc -= dma_count_take(dma_c);
    if (dma_c->cc < 0)
        tc = dma_count_out(channel);

    /* A scatter-gather list or a chain gives EOP only where it chose EOP. */
    if (sg ? tc : (dma_m & (1 << channel)))
        return DMA_OVER;

    return 0;
}

static void
dma_ps2_run(int channel)
{
    dma_t         *dma_c   = &dma[channel];
    const uint16_t io_addr = ((dma_c->ps2_mode & DMA_PS2_IOA) ? dma_c->io_addr : 0) &
                             (dma_c->size ? 0xfffe : 0xffff);

    dma_stat_rq |= (1 << channel);

    switch (dma_c->ps2_mode & DMA_PS2_XFER_MASK) {
        case DMA_PS2_XFER_MEM_TO_IO:
            do {
                if (!dma_c->size) {
                    uint8_t temp = _dma_read(dma_c->ac, dma_c);

                    outb(io_addr, temp);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac--;
                    else
                        dma_c->ac++;
                } else {
                    uint16_t temp = _dma_readw(dma_c->ac, dma_c);

                    outw(io_addr, temp);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac -= 2;
                    else
                        dma_c->ac += 2;
                }

                dma_c->cc--;
            } while (dma_c->cc >= 0);

            dma_stat |= (1 << channel);
            break;

        case DMA_PS2_XFER_IO_TO_MEM:
            do {
                if (!dma_c->size) {
                    uint8_t temp = inb(io_addr);

                    _dma_write(dma_c->ac, temp, dma_c);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac--;
                    else
                        dma_c->ac++;
                } else {
                    uint16_t temp = inw(io_addr);

                    _dma_writew(dma_c->ac, temp, dma_c);

                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac -= 2;
                    else
                        dma_c->ac += 2;
                }

                dma_c->cc--;
            } while (dma_c->cc >= 0);

            ps2_cache_clean();
            dma_stat |= (1 << channel);
            break;

        default: /*Memory verify*/
            do {
                if (!dma_c->size) {
                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac--;
                    else
                        dma_c->ac++;
                } else {
                    if (dma_c->ps2_mode & DMA_PS2_DEC2)
                        dma_c->ac -= 2;
                    else
                        dma_c->ac += 2;
                }

                dma_c->cc--;
            } while (dma_c->cc >= 0);

            dma_stat |= (1 << channel);
            break;
    }

    if (dma_c->ps2_mode & DMA_PS2_AUTOINIT) {
        dma_c->cc = dma_c->cb;
        dma_c->ac = dma_c->ab;
    } else
        dma_m |= (1 << channel);
}

int
dma_mode(int channel)
{
    return (dma[channel].mode);
}

/* DMA Bus Master Page Read/Write */
void
dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize)
{
    uint32_t n;
    uint32_t n2;
    uint8_t  bytes[4] = { 0, 0, 0, 0 };

    n  = TotalSize & ~(TransferSize - 1);
    n2 = TotalSize - n;

    /* Do the divisible block, if there is one. */
    if (n) {
        for (uint32_t i = 0; i < n; i += TransferSize)
            mem_read_phys((void *) &(DataRead[i]), PhysAddress + i, TransferSize);
    }

    /* Do the non-divisible block, if there is one. */
    if (n2) {
        mem_read_phys((void *) bytes, PhysAddress + n, TransferSize);
        memcpy((void *) &(DataRead[n]), bytes, n2);
    }
}

void
dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize)
{
    uint32_t n;
    uint32_t n2;
    uint8_t  bytes[4] = { 0, 0, 0, 0 };

    n  = TotalSize & ~(TransferSize - 1);
    n2 = TotalSize - n;

    /* Do the divisible block, if there is one. */
    if (n) {
        for (uint32_t i = 0; i < n; i += TransferSize)
            mem_write_phys((void *) &(DataWrite[i]), PhysAddress + i, TransferSize);
    }

    /* Do the non-divisible block, if there is one. */
    if (n2) {
        mem_read_phys((void *) bytes, PhysAddress + n, TransferSize);
        memcpy(bytes, (void *) &(DataWrite[n]), n2);
        mem_write_phys((void *) bytes, PhysAddress + n, TransferSize);
    }

    if (dma_at)
        mem_invalidate_range(PhysAddress, PhysAddress + TotalSize - 1);
}
