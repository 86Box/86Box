/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI 3D Rage Pro (Mach64 GT-B family): the registers of its own,
 *          its bus master and its AGP interface.
 *
 *          The 3D Rage Pro is the GT-B's 2D and 3D engines and front-end
 *          scaler (vid_ati_mach64_accel.c, vid_ati_mach64_3d.c) with a
 *          triangle setup engine, a second texture and the bus master of
 *          the VT-B/GT-B, which its drivers use as a "virtual FIFO": a ring
 *          of descriptors in system memory, each pointing at a run of
 *          register writes the engine takes as if the CPU made them.
 *
 *          Bus master: "RAGE PRO and Derivatives Programmer's Guide",
 *          PRG-215R3-00-10, 8.10 (descriptors, system and GUI transfers,
 *          the circular buffer, register packets). Register offsets and the
 *          rest from the open drivers: X.Org's mach64 driver (atiregs.h) and
 *          the DRI driver (mach64_dma.c, which also has a software copy of
 *          the GUI master, mach64_do_dispatch_pseudo_dma).
 *
 * Authors: Marcos Alves, <https://github.com/Xeon3D>
 *
 *          Copyright 2026 Marcos Alves.
 */

#include "vid_ati_mach64.h"
#include <86box/dma.h>

/* Block 1 (byte offsets in the 1K block below block 0). */
#define RP1_MPP_CONFIG         0x0c0 /* MPP_CONFIG ... MPP_DATA again */
#define RP1_MPP_DATA           0x0cc
#define RP1_AGP_BASE           0x148
#define RP1_AGP_CNTL           0x14c
#define RP1_SCALER_COLOUR_CNTL 0x150
#define RP1_SCALER_H_COEFF4    0x164
#define RP1_GUI_CMDFIFO_DEBUG  0x170
#define RP1_GUI_CMDFIFO_DATA   0x174
#define RP1_GUI_CNTL           0x178
#define RP1_BM_FRAME_BUF_OFFSET 0x180
#define RP1_BM_SYSTEM_MEM_ADDR 0x184
#define RP1_BM_COMMAND         0x188
#define RP1_BM_STATUS          0x18c
#define RP1_BM_GUI_TABLE       0x1b8
#define RP1_BM_SYSTEM_TABLE    0x1bc
#define RP1_SCALER_BUF0_OFFSET_U 0x1d4
#define RP1_SCALER_BUF1_OFFSET_V 0x1e0
#define RP1_SETUP_FIRST        0x240 /* VERTEX_1_S */
#define RP1_SETUP_LAST         0x33c /* VERTEX_2_SECONDARY_W */

/* Block 0 (byte offsets in block 0). */
#define RP0_DST_WIDTH        0x110
#define RP0_DST_HEIGHT_WIDTH 0x118
#define RP0_SRC_CNTL         0x1b4
#define RP0_DP_SET_GUI_ENGINE 0x2fc
#define RP0_HOST_DATA0       0x200
#define RP0_BM_HOSTDATA      0x244
#define RP0_BM_ADDR          0x248
#define RP0_BM_GUI_TABLE_CMD 0x24c

/* SRC_CNTL */
#define SRC_BM_ENABLE    (1u << 8)
#define SRC_BM_OP_SHIFT  10
#define SRC_BM_OP_TO_HOST 0 /* frame buffer to system memory */
#define SRC_BM_OP_TO_FB   1 /* system memory to frame buffer */
#define SRC_BM_OP_TO_REG  3 /* system memory to the bus master host data register */

/* BUS_CNTL */
#define BUS_MASTER_DIS (1u << 6)

/* BM_COMMAND, the third dword of a descriptor */
#define BM_CMD_COUNT       0x00001fff /* up to 4096 bytes */
#define BM_CMD_HOLD_OFFSET (1u << 30)
#define BM_CMD_EOL         (1u << 31)

/* The register file at the top of an 8 MB aperture, as a descriptor names
   it: block 1 at 7FF800h, block 0 at 7FFC00h. */
#define BM_APERTURE_REGS 0x7ff800

/* What a guest table can make the chip do at most in one go: a full
   circular buffer of 128K, or a system table of as many descriptors. */
#define BM_MAX_DESCRIPTORS 8192

/* The block 1 registers of the Rage Pro: the MPP's again, AGP, the scaler's colour control
   and coefficients, the command FIFO, the bus master, the planar scaler
   buffers and the setup engine. */
static int
mach64_ragepro_blk1_reg(const mach64_t *mach64, uint32_t addr)
{
    /* The IIC shares descriptor DMA, without the Pro-only registers. */
    if (mach64->rage2c)
        return ((addr >= RP1_BM_FRAME_BUF_OFFSET) && (addr <= RP1_BM_STATUS)) ||
               (addr == RP1_BM_GUI_TABLE) || (addr == RP1_BM_SYSTEM_TABLE);
    return ((addr >= RP1_MPP_CONFIG) && (addr <= RP1_MPP_DATA)) || ((addr >= RP1_AGP_BASE) && (addr <= RP1_SCALER_H_COEFF4)) || ((addr >= RP1_GUI_CMDFIFO_DEBUG) && (addr <= RP1_BM_STATUS)) ||
           (addr == RP1_BM_GUI_TABLE) || (addr == RP1_BM_SYSTEM_TABLE) ||
           ((addr >= RP1_SCALER_BUF0_OFFSET_U) && (addr <= RP1_SCALER_BUF1_OFFSET_V)) || ((addr >= RP1_SETUP_FIRST) && (addr <= RP1_SETUP_LAST)) ||
           mach64_idct_reg(mach64, addr);
}

void
mach64_ragepro_reset(mach64_t *mach64)
{
    memset(&mach64->rp, 0, sizeof(mach64->rp));
}

/*
 * The Rage Pro's I2C engine: I2C_CNTL_0 (block 0, 3Ch) and I2C_CNTL_1 (BCh),
 * bits as atiregs.h names them. Bytes written to I2C_CNTL_1's 7:0 go into a
 * FIFO. A write of I2C_CNTL_0's control byte (15:8) with START (8), STOP
 * (9), GO (10) or RECEIVE (11) runs a step of a transfer: START sends a start
 * and the FIFO's first byte as the slave's address and read bit, then the
 * rest of the ADDR_COUNT (18:16) address bytes go out, then DATA_COUNT
 * (15:8) bytes are written from the FIFO or, with RECEIVE, read into it;
 * STOP ends it. Without START the step goes on with the slave addressed
 * last. It sets DONE (0), or NACK (1) for a byte nobody acknowledged. SEL
 * (22) picks the bus: the monitor's DDC lines, or the GP_IO lines of the
 * TV-out's I2C.
 *
 * ATI's Windows 9x driver (macxw4.vxd 5.30) sets bit 4 (an enable), resets
 * the FIFO with HPTR_RST, pushes 71h with DATA_COUNT 1 and ADDR_COUNT 0 and
 * writes START alone to probe for a TV encoder at 38h, then waits on the
 * status nibble with no time limit.
 */
#define I2C_STAT       0x0000000f
#define I2C_DONE       0x00000001
#define I2C_NACK       0x00000002
#define I2C_HPTR_RST   0x00000020
#define I2C_START      0x00000100
#define I2C_STOP       0x00000200
#define I2C_GO         0x00000400
#define I2C_RECEIVE    0x00000800
#define I2C_ABORT      0x00001000
#define I2C_DATA_COUNT(c1) (((c1) >> 8) & 0xff)
#define I2C_ADDR_COUNT(c1) (((c1) >> 16) & 7)
#define I2C_SEL        0x00400000

static void
mach64_ragepro_i2c_step(mach64_t *mach64)
{
    void    *bus     = i2c_gpio_get_bus((mach64->rp.i2c_cntl1 & I2C_SEL) ? mach64->i2c_tv : mach64->i2c);
    uint32_t cntl0   = mach64->rp.i2c_cntl0;
    int      receive = !!(cntl0 & I2C_RECEIVE);
    int      naddr   = I2C_ADDR_COUNT(mach64->rp.i2c_cntl1);
    int      ndata   = I2C_DATA_COUNT(mach64->rp.i2c_cntl1);
    int      nout    = naddr + (receive ? 0 : ndata);
    int      ack     = 1;
    int      p       = 0;

    if (cntl0 & I2C_START) {
        mach64->rp.i2c_slave = mach64->rp.i2c_fifo[0];
        ack                  = i2c_start(bus, mach64->rp.i2c_slave >> 1, mach64->rp.i2c_slave & 1);
        p                    = 1;
    }
    for (; ack && (p < nout); p++)
        ack = i2c_write(bus, mach64->rp.i2c_slave >> 1, mach64->rp.i2c_fifo[p & 15]);
    mach64->rp.i2c_rptr = 0;
    if (receive) {
        mach64->rp.i2c_wptr = 0;
        for (int i = 0; ack && (i < ndata); i++)
            mach64->rp.i2c_fifo[mach64->rp.i2c_wptr++ & 15] = i2c_read(bus, mach64->rp.i2c_slave >> 1);
    }
    if ((cntl0 & I2C_STOP) || !ack)
        i2c_stop(bus, mach64->rp.i2c_slave >> 1);

    mach64->rp.i2c_cntl0 = (cntl0 & ~(I2C_GO | I2C_STAT)) | (ack ? I2C_DONE : I2C_NACK);
    mach64_log("Rage Pro I2C: bus %d slave %02X%s%s%s, %d out, %d in: %s\n", !!(mach64->rp.i2c_cntl1 & I2C_SEL), mach64->rp.i2c_slave,
               (cntl0 & I2C_START) ? " start" : "", receive ? " receive" : "", (cntl0 & I2C_STOP) ? " stop" : "", nout,
               receive ? ndata : 0, ack ? "done" : "no acknowledge");
}

/* reg is the byte's offset in block 0, 3Ch-3Fh or BCh-BFh. */
uint8_t
mach64_ragepro_i2c_readb(mach64_t *mach64, uint32_t reg)
{
    if ((reg & 0xfc) == 0x3c)
        return mach64->rp.i2c_cntl0 >> ((reg & 3) * 8);
    if (!(reg & 3))
        return mach64->rp.i2c_fifo[mach64->rp.i2c_rptr++ & 15];
    return mach64->rp.i2c_cntl1 >> ((reg & 3) * 8);
}

void
mach64_ragepro_i2c_writeb(mach64_t *mach64, uint32_t reg, uint8_t val)
{
    const int shift = (reg & 3) * 8;

    if ((reg & 0xfc) == 0xbc) {
        if (!shift)
            mach64->rp.i2c_fifo[mach64->rp.i2c_wptr++ & 15] = val;
        else
            mach64->rp.i2c_cntl1 = (mach64->rp.i2c_cntl1 & ~(0xffu << shift)) | ((uint32_t) val << shift);
        return;
    }

    if (!shift) {
        /* The status bits clear where a 1 is written; HPTR_RST resets the
           FIFO's pointers and does not stay set. */
        mach64->rp.i2c_cntl0 = (mach64->rp.i2c_cntl0 & ~0xffu & ~(uint32_t) (val & I2C_STAT)) |
                               (mach64->rp.i2c_cntl0 & I2C_STAT & ~(uint32_t) val) | (val & ~(I2C_STAT | I2C_HPTR_RST));
        if (val & I2C_HPTR_RST)
            mach64->rp.i2c_wptr = mach64->rp.i2c_rptr = 0;
        return;
    }
    mach64->rp.i2c_cntl0 = (mach64->rp.i2c_cntl0 & ~(0xffu << shift)) | ((uint32_t) val << shift);
    if (shift != 8)
        return;
    if (val & (I2C_ABORT >> 8)) {
        i2c_stop(i2c_gpio_get_bus((mach64->rp.i2c_cntl1 & I2C_SEL) ? mach64->i2c_tv : mach64->i2c), mach64->rp.i2c_slave >> 1);
        mach64->rp.i2c_cntl0 &= ~(I2C_GO | I2C_ABORT | I2C_START | I2C_STOP | I2C_RECEIVE | I2C_STAT);
    } else if (val & ((I2C_START | I2C_STOP | I2C_GO | I2C_RECEIVE) >> 8))
        mach64_ragepro_i2c_step(mach64);
}

/* A register write from the bus master: idx is ATI's MM index, block 0 in
   00h-FFh, block 1 in 100h-1FFh. It goes in as the CPU's would, but only to
   what BM_ADDR's packets are for (XL/XC RRG 6-13): the command FIFO's draw
   engine (block 0 from 100h on) and the 3D setup engine and IDCT (block 1
   from 240h, VERTEX_1_S, on). The rest -- the bus master's own registers among them -- is
   not reached that way: a stream out of step with its headers would
   otherwise rewrite BM_SYSTEM_TABLE and BM_GUI_TABLE and start transfers
   from whatever its data says. */
static void
mach64_ragepro_bm_reg_write(mach64_t *mach64, uint32_t idx, uint32_t val)
{
    /* GUIREG_ADR is not a wrapping register address. In particular, an
       invalid coefficient-as-header must not alias back into GUI state. */
    if ((idx > 0x1ff) || (idx < 0x40) || ((idx >= 0x100) && (idx < (0x100 | (RP1_SETUP_FIRST >> 2)))))
        return;
    if (idx < 0x100)
        mach64_ext_writel(0x400 | (idx << 2), val, mach64);
    else if (mach64->idct && mach64->rp.pkt_triplets >= 0 && (idx == 0x1e0 || idx == 0x1e1))
        mach64_queue(mach64, FIFO_ADDR_IDCT | FIFO_ADDR_IDCT_BM | ((idx & 0xff) << 2), val, FIFO_WRITE_DWORD);
    else
        mach64_ext_writel((idx & 0xff) << 2, val, mach64);
}

/* BM_ADDR's IDCT packets (Rage XL/XC RRG: BM_ADDR, p. 6-13 / PDF 225). */
#define BM_ADDR_IDCT          (1u << 31)
#define BM_ADDR_IDCT_FLAGS(v) (((v) >> 29) & 3)
#define IDCT_FLAGS_END        0 /* end of block, no data */
#define IDCT_FLAGS_TRIPLETS   1 /* RUN, LEVEL, LEVEL ... to IDCT_RUNS and IDCT_LEVELS */
/* A BM_ADDR header: the packet it starts. */
static void
mach64_ragepro_bm_header(mach64_t *mach64, uint32_t val)
{
    uint32_t counter = (val >> 16) & (mach64->idct ? 0x1f : 0xffff);

    mach64->rp.pkt_reg      = val & 0xffff;
    mach64->rp.pkt_triplets = -1;
    if (!mach64->idct || !(val & BM_ADDR_IDCT)) {
        mach64->rp.pkt_count = counter + 1;
        return;
    }

    /* The Rage XL's IDCT: GUIREG_COUNTER is 5 bits there (20:16), the
       IDCT flags above it. */
    counter &= 0x1f;
    switch (BM_ADDR_IDCT_FLAGS(val)) {
        case IDCT_FLAGS_END:
            mach64_queue(mach64, FIFO_ADDR_IDCT_END, 0, FIFO_WRITE_DWORD);
            mach64->rp.pkt_count = 0;
            break;
        case IDCT_FLAGS_TRIPLETS:
            /* GUIREG_ADR is IDCT_RUNS; the two LEVELs go to the register
               after it. The guide's count is 3T-1, ATI's command player
               (atimcrae.dll @b010c400) sends 0 for one triplet. */
            mach64->rp.pkt_triplets = 0;
            mach64->rp.pkt_count    = (counter / 3 + 1) * 3;
            break;
        default:
            /* 10, IDCT register writes, auto-incremented as a GUI packet's;
               11 is reserved and taken the same way. */
            mach64->rp.pkt_count = counter + 1;
            break;
    }
}

/* A dword of a GUI transfer to the aperture offset target. */
static void
mach64_ragepro_bm_gui_dword(mach64_t *mach64, uint32_t target, uint32_t val)
{
    svga_t  *svga = &mach64->svga;
    uint32_t reg;
    /* Optional raw command capture for diagnosing decoder packet framing. */
    static FILE *trace;
    static int trace_init, trace_count;
    if (!trace_init) {
        const char *path = getenv("MACH64_BM_TRACE");
        trace_init = 1;
        if (path && *path)
            trace = fopen(path, "w");
    }
    if (trace && trace_count++ < 500000) {
        fprintf(trace, "%08x %08x %d %03x %d %llu %08x\n", target, val,
                mach64->rp.pkt_count, mach64->rp.pkt_reg, mach64->rp.pkt_triplets,
                (unsigned long long) mach64->rp.descriptors, mach64->rp.command);
        if (!(trace_count & 4095))
            fflush(trace);
    }

    /* The registers are told by bits 22:11 alone: ATI's DirectDraw driver
       (macxdd32 4.11, its MPEG motion compensation) sends its register
       packets to 7FFFE48h, its Direct3D driver to 7FFE48h. */
    if ((target & 0x7ff800) != BM_APERTURE_REGS) {
        /* A run into the frame buffer. */
        target &= mach64->vram_mask & ~3;
        *(uint32_t *) &svga->vram[target]   = val;
        svga->changedvram[target >> 12]     = svga->monitor->mon_changeframecount;
        return;
    }

    reg = target & 0x7fc;
    if (reg == (0x400 | RP0_BM_ADDR)) {
        /* BM_ADDR/BM_DATA: a packet, the MM index of the first register
           with the count less one in 31:16 (GUIREG_COUNTER), then the
           data of that many registers in a row. The packet carries on
           across descriptors. */
        /* CINMST32's native XL stream can leave a preallocated triplet
           header immediately before its IDCT_CONTROL synchronization
           packet (captured with MMC 6.2). C00001EF is not a legal clear
           RUNS word: its C0/EF bytes exceed 63 and are not FF. Accept the
           explicit synchronization header at that RUN boundary instead
           of feeding subsequent GUI state to the coefficient port. The
           A00201E0 triplet header is also impossible as a clear run word
           or MPEG level pair, and allows recovery after a malformed tail.
           This recovery must not examine an authenticated ciphertext word. */
        if (mach64->idct && !mach64_idct_authenticated(mach64) &&
            (val == 0xa00201e0 || (mach64->rp.pkt_triplets == 0 &&
             mach64->rp.pkt_reg == 0x1e0 && val == 0xc00001ef)))
            mach64->rp.pkt_count = 0;
        if (!mach64->rp.pkt_count) {
            mach64_ragepro_bm_header(mach64, val);
            return;
        }
        if (mach64->rp.pkt_triplets >= 0) {
            /* RUN to GUIREG_ADR, LEVEL and LEVEL to the next register. */
            mach64_ragepro_bm_reg_write(mach64, mach64->rp.pkt_reg + !!mach64->rp.pkt_triplets, val);
            mach64->rp.pkt_triplets = (mach64->rp.pkt_triplets + 1) % 3;
        } else
            mach64_ragepro_bm_reg_write(mach64, mach64->rp.pkt_reg++, val);
        mach64->rp.pkt_count--;
        return;
    }
    if (reg == (0x400 | RP0_BM_HOSTDATA)) {
        /* BM_HOSTDATA: host data for the engine, as HOST_DATA0. */
        mach64_ext_writel(0x400 | RP0_HOST_DATA0, val, mach64);
        return;
    }
    mach64_ext_writel(reg, val, mach64);
}

/* Reads a descriptor at addr into BM_FRAME_BUF_OFFSET .. BM_STATUS. */
static void
mach64_ragepro_bm_descriptor(mach64_t *mach64, uint32_t addr)
{
    uint32_t d[4];

    dma_bm_read(addr, (uint8_t *) d, 16, 4);
    mach64->rp.frame_buf_offset = d[0];
    mach64->rp.system_mem_addr  = d[1];
    mach64->rp.command          = d[2];
    mach64->rp.status           = d[3];
    mach64->rp.descriptors++;
}

static uint32_t
mach64_ragepro_bm_count(uint32_t command)
{
    uint32_t count = command & BM_CMD_COUNT;

    return (count > 4096) ? 4096 : count;
}

/* A descriptor aimed at the register aperture is a register transfer,
   including when submitted through BM_SYSTEM_TABLE. ATIMIAAE's native
   authentication helper uses precisely that route (7FFFE48, C0000008). */
static void
mach64_ragepro_bm_register_transfer(mach64_t *mach64)
{
    uint32_t count = mach64_ragepro_bm_count(mach64->rp.command);
    uint32_t target = mach64->rp.frame_buf_offset & 0x00ffffff;
    uint8_t buf[4096];
    if (!count)
        return;
    dma_bm_read(mach64->rp.system_mem_addr & ~3u, buf, (count + 3) & ~3u, 4);
    for (uint32_t i = 0; i < count; i += 4) {
        uint32_t value;
        memcpy(&value, buf + i, 4);
        mach64_ragepro_bm_gui_dword(mach64, target, value);
        if (!(mach64->rp.command & BM_CMD_HOLD_OFFSET))
            target += 4;
    }
}

/*
 * A GUI master operation (8.10.4): the descriptors from BM_GUI_TABLE on, in
 * its circular buffer, to the end of the list. The transfer is run at once:
 * the writes reach the engine in order, and the CPU sees an engine that is
 * idle when it next looks, BM_GUI_TABLE one descriptor past the last.
 */
static void
mach64_ragepro_gui_master(mach64_t *mach64)
{
    uint32_t size  = 16384u << (mach64->rp.gui_table & 3);
    uint32_t base  = mach64->rp.gui_table & ~(size - 1);
    uint32_t addr  = mach64->rp.gui_table & ~0x0f;
    uint8_t  buf[4096];
    int      limit = size / 16;

    mach64->rp.running = 1;
    for (int n = 0; n < limit; n++) {
        uint32_t count;
        uint32_t target;
        uint32_t sys;
        int      hold;
        int      eol;

        mach64_ragepro_bm_descriptor(mach64, addr);
        addr                  = base | ((addr + 16) & (size - 1));
        mach64->rp.gui_table  = addr | (mach64->rp.gui_table & 3);

        count  = mach64_ragepro_bm_count(mach64->rp.command);
        target = mach64->rp.frame_buf_offset & 0x00ffffff;
        sys    = mach64->rp.system_mem_addr;
        hold   = !!(mach64->rp.command & BM_CMD_HOLD_OFFSET);
        eol    = !!(mach64->rp.command & BM_CMD_EOL);

        if (count) {
            dma_bm_read(sys & ~3, buf, (count + 3) & ~3, 4);
            for (uint32_t i = 0; i < count; i += 4) {
                uint32_t val;

                memcpy(&val, &buf[i], 4);
                mach64_ragepro_bm_gui_dword(mach64, target, val);
                if (!hold)
                    target += 4;
            }
        }
        if (eol)
            break;
    }
    mach64->rp.running = 0;
    mach64_ragepro_bm_eol(mach64);
}

/*
 * The current descriptor's block between the frame buffer and system
 * memory, to the host or from it. The source's low two address bits give
 * the alignment.
 */
static void
mach64_ragepro_bm_copy(mach64_t *mach64, int to_host)
{
    svga_t  *svga  = &mach64->svga;
    uint32_t count = mach64_ragepro_bm_count(mach64->rp.command);
    uint32_t fb    = mach64->rp.frame_buf_offset & 0x00ffffff;
    uint32_t sys   = mach64->rp.system_mem_addr;
    uint8_t  buf[4096];

    if (!count)
        return;
    /* The framebuffer aperture includes the register window. ATIMIAAE
       submits its authentication packets with BUS_MASTER_OP=1, not 3:
       copying those bytes into backing VRAM loses the register writes. */
    if (!to_host && ((fb & 0x7ff800) == BM_APERTURE_REGS)) {
        mach64_ragepro_bm_register_transfer(mach64);
        return;
    }
    if (to_host) {
        sys = (sys & ~3) | (fb & 3);
        for (uint32_t i = 0; i < count; i++)
            buf[i] = svga->vram[(fb + i) & mach64->vram_mask];
        dma_bm_write(sys, buf, count, 1);
    } else {
        fb = (fb & ~3) | (sys & 3);
        dma_bm_read(sys, buf, count, 1);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t a = (fb + i) & mach64->vram_mask;

            svga->vram[a]              = buf[i];
            svga->changedvram[a >> 12] = svga->monitor->mon_changeframecount;
        }
    }
}

/*
 * A system transfer (8.10.3), started by a write of BM_SYSTEM_TABLE: the
 * table's address in 31:4, SYSTEM_TRIGGER in 2:0. Trigger 0 copies system
 * memory to the frame buffer, as the guide's example has it; 1 is taken as
 * the other way.
 */
static void
mach64_ragepro_system_master(mach64_t *mach64, uint32_t table)
{
    uint32_t addr    = table & ~0x0f;
    int      to_host = table & 1;

    mach64_wait_fifo_idle(mach64);
    mach64->rp.running = 1;
    for (int n = 0; n < BM_MAX_DESCRIPTORS; n++) {
        mach64_ragepro_bm_descriptor(mach64, addr);
        addr += 16;
        if (!to_host && ((mach64->rp.frame_buf_offset & 0x7ff800) == BM_APERTURE_REGS))
            mach64_ragepro_bm_register_transfer(mach64);
        else
            mach64_ragepro_bm_copy(mach64, to_host);
        if (mach64->rp.command & BM_CMD_EOL)
            break;
    }
    mach64->rp.running = 0;
    mach64_ragepro_bm_eol(mach64);
}

/*
 * A GUI bus master copy: SRC_CNTL's BUS_MASTER_OP 0 (frame buffer to system
 * memory) or 1 (system memory to frame buffer, XL/XC RRG 5-21), started like
 * the GUI master by a DST_WIDTH or DST_HEIGHT_WIDTH write, over the
 * descriptors from BM_GUI_TABLE on in its circular buffer (RRG 6-14) to the
 * end of the list. ATI's Windows 98 drivers test the bus master this way at
 * start: one 256-byte descriptor to frame buffer offset 0, checked there,
 * and they set BUS_MASTER_DIS for good when it does not arrive.
 */
static void
mach64_ragepro_gui_copy(mach64_t *mach64, int to_host)
{
    uint32_t size  = 16384u << (mach64->rp.gui_table & 3);
    uint32_t base  = mach64->rp.gui_table & ~(size - 1);
    uint32_t addr  = mach64->rp.gui_table & ~0x0f;
    int      limit = size / 16;

    mach64->rp.running = 1;
    for (int n = 0; n < limit; n++) {
        mach64_ragepro_bm_descriptor(mach64, addr);
        addr                 = base | ((addr + 16) & (size - 1));
        mach64->rp.gui_table = addr | (mach64->rp.gui_table & 3);
        mach64_ragepro_bm_copy(mach64, to_host);
        if (mach64->rp.command & BM_CMD_EOL)
            break;
    }
    mach64->rp.running = 0;
    mach64_ragepro_bm_eol(mach64);
}

static int
mach64_ragepro_bm_enabled(const mach64_t *mach64)
{
    return !(mach64->bus_cntl & BUS_MASTER_DIS) && (mach64->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_L_BM);
}

/*
 * A write to block 1 at addr (0-3FCh) of bytes bytes. Returns 0 for a
 * register that is not the Rage Pro's own. A partial write lands in the
 * register and acts once its top byte is written.
 */
int
mach64_ragepro_blk1_write(mach64_t *mach64, uint32_t addr, uint32_t val, int bytes)
{
    uint32_t reg   = addr & 0x3fc;
    unsigned shift = (addr & 3) * 8;
    uint32_t mask  = (bytes >= 4) ? 0xffffffff : (((1u << (bytes * 8)) - 1) << shift);
    uint32_t v;

    if (!mach64_ragepro_blk1_reg(mach64, reg))
        return 0;

    if (reg <= RP1_MPP_DATA) {
        mach64_mpp_write(mach64, (reg - RP1_MPP_CONFIG) >> 2, val << shift, mask);
        return 1;
    }

    v = (mach64->rp.blk1[reg >> 2] & ~mask) | ((val << shift) & mask);
    mach64->rp.blk1[reg >> 2] = v;
    if (((addr & 3) + bytes) < 4)
        return 1;

    if ((reg >= RP1_SETUP_FIRST) && (reg <= RP1_SETUP_LAST)) {
        mach64_queue(mach64, FIFO_ADDR_SETUP | reg, v, FIFO_WRITE_DWORD);
        return 1;
    }
    if (mach64_idct_reg(mach64, reg)) {
        if (reg == 0x388 || reg == 0x38c) {
            mach64_wait_fifo_idle(mach64);
            mach64_idct_auth_write(mach64, reg, v);
            return 1;
        }
        /* The IDCT takes its data in order with the draws that use it. */
        mach64_queue(mach64, FIFO_ADDR_IDCT | reg, v, FIFO_WRITE_DWORD);
        return 1;
    }

    switch (reg) {
        case RP1_AGP_BASE:
            mach64->rp.agp_base = v;
            break;
        case RP1_AGP_CNTL:
            mach64->rp.agp_cntl = v;
            break;
        case RP1_GUI_CNTL:
            /* XL RRG 5-64: IDCT_PRSR_MODE (16), BLOCK_GUI_INITIATOR (17). */
            mach64->rp.gui_cntl = v & (mach64->idct ? 0x30003 : 0x3);
            break;
        case RP1_BM_GUI_TABLE:
            mach64->rp.gui_table = v & ~0x0c;
            break;
        case RP1_BM_SYSTEM_TABLE:
            mach64->rp.system_table = v;
            if (mach64_ragepro_bm_enabled(mach64))
                mach64_ragepro_system_master(mach64, v);
            break;
        default:
            break;
    }
    return 1;
}

int
mach64_ragepro_blk1_read(mach64_t *mach64, uint32_t addr, uint32_t *val)
{
    uint32_t reg = addr & 0x3fc;

    if (mach64->idct && (reg == 0x388 || reg == 0x38c)) {
        mach64_wait_fifo_idle(mach64);
        return mach64_idct_auth_read(mach64, reg, val);
    }

    if (!mach64_ragepro_blk1_reg(mach64, reg))
        return 0;

    if (reg <= RP1_MPP_DATA) {
        *val = mach64_mpp_read(mach64, (reg - RP1_MPP_CONFIG) >> 2);
        return 1;
    }

    if ((reg >= RP1_SETUP_FIRST) && (reg <= RP1_SETUP_LAST)) {
        mach64_wait_fifo_idle(mach64);
        if (!mach64_3d_setup_read(mach64, reg >> 2, val))
            *val = mach64->rp.blk1[reg >> 2];
        return 1;
    }

    switch (reg) {
        case RP1_AGP_BASE:
            *val = mach64->rp.agp_base;
            break;
        case RP1_AGP_CNTL:
            *val = mach64->rp.agp_cntl;
            break;
        case RP1_GUI_CNTL:
            *val = mach64->rp.gui_cntl;
            break;
        case RP1_GUI_CMDFIFO_DEBUG:
        case RP1_GUI_CMDFIFO_DATA:
            *val = 0;
            break;
        case RP1_BM_FRAME_BUF_OFFSET:
            *val = mach64->rp.frame_buf_offset;
            break;
        case RP1_BM_SYSTEM_MEM_ADDR:
            *val = mach64->rp.system_mem_addr;
            break;
        case RP1_BM_COMMAND:
            *val = mach64->rp.command;
            break;
        case RP1_BM_STATUS:
            *val = mach64->rp.status;
            break;
        case RP1_BM_GUI_TABLE:
            *val = mach64->rp.gui_table;
            break;
        case RP1_BM_SYSTEM_TABLE:
            *val = mach64->rp.system_table;
            break;
        default:
            *val = mach64->rp.blk1[reg >> 2];
            break;
    }
    return 1;
}

/*
 * A GUI register write (block 0, 100h-3FFh) on its way to the engine. SRC_CNTL
 * is followed here, ahead of the FIFO, for the bus master's settings; with
 * SRC_BM_ENABLE and BUS_MASTER_OP 3 a DST_WIDTH or DST_HEIGHT_WIDTH write
 * starts the GUI master (8.10.4 step 10) instead of a blit. BM_GUI_TABLE_CMD
 * is block 1's BM_GUI_TABLE at a block 0 address. Returns 1 when the write
 * goes no further.
 */
int
mach64_ragepro_gui_write(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type)
{
    uint32_t reg   = addr & 0x3fc;
    int      bytes = (type == FIFO_WRITE_DWORD) ? 4 : ((type == FIFO_WRITE_WORD) ? 2 : 1);
    unsigned shift = (addr & 3) * 8;
    uint32_t mask  = (bytes >= 4) ? 0xffffffff : (((1u << (bytes * 8)) - 1) << shift);
    int      done  = ((addr & 3) + bytes) >= 4;

    if (mach64->rp.wlog > 0) {
        mach64->rp.wlog--;
        pclog("Rage Pro write: %03X = %08X (%d)%s\n", addr & 0x3ff, val, bytes, mach64->rp.running ? " BM" : "");
    }

    switch (reg) {
        case RP0_SRC_CNTL:
            mach64->rp.src_cntl = (mach64->rp.src_cntl & ~mask) | ((val << shift) & mask);
            return 0;
        case RP0_DP_SET_GUI_ENGINE:
            /* Its 27:24 are SRC_CNTL's 11:8: BUS_MASTER_OP, SRC_BM_ENABLE
               and SRC_BM_SYNC (the LT Pro's layout of the register). */
            if (done) {
                uint32_t v = (mach64->rp.dp_set_gui_engine & ~mask) | ((val << shift) & mask);

                mach64->rp.dp_set_gui_engine = v;
                mach64->rp.src_cntl = (mach64->rp.src_cntl & ~0xf00) | ((((v >> 24) & 3) << SRC_BM_OP_SHIFT) | (((v >> 26) & 1) << 8) | (((v >> 27) & 1) << 9));
            } else
                mach64->rp.dp_set_gui_engine = (mach64->rp.dp_set_gui_engine & ~mask) | ((val << shift) & mask);
            return 0;
        case RP0_BM_GUI_TABLE_CMD:
            mach64->rp.blk1[RP1_BM_GUI_TABLE >> 2] = (mach64->rp.blk1[RP1_BM_GUI_TABLE >> 2] & ~mask) | ((val << shift) & mask);
            if (done)
                mach64->rp.gui_table = mach64->rp.blk1[RP1_BM_GUI_TABLE >> 2] & ~0x0c;
            return 1;
        case RP0_BM_HOSTDATA:
        case RP0_BM_ADDR:
            /* The bus master's own ports: the CPU has nothing to send there. */
            return 1;
        case RP0_DST_WIDTH:
        case RP0_DST_HEIGHT_WIDTH:
            if (!done || mach64->rp.running)
                return 0;
            if (!(mach64->rp.src_cntl & SRC_BM_ENABLE) || !mach64_ragepro_bm_enabled(mach64))
                return 0;
            switch ((mach64->rp.src_cntl >> SRC_BM_OP_SHIFT) & 3) {
                case SRC_BM_OP_TO_FB:
                    mach64_ragepro_gui_copy(mach64, 0);
                    return 1;
                case SRC_BM_OP_TO_HOST:
                    mach64_ragepro_gui_copy(mach64, 1);
                    return 1;
                case SRC_BM_OP_TO_REG:
                    mach64_ragepro_gui_master(mach64);
                    return 1;
                default:
                    /* 2, foreground register to system memory: not done. */
                    return 0;
            }
        default:
            return 0;
    }
}

/* The bus master and the setup engine, to the log. */
void
mach64_ragepro_dump(mach64_t *mach64)
{
    pclog("Rage Pro: PCI %04X, AGP_BASE %08X AGP_CNTL %08X, AGP command %02X%02X%02X%02X\n", mach64->pci_id, mach64->rp.agp_base,
          mach64->rp.agp_cntl, mach64->pci_regs[0x5b], mach64->pci_regs[0x5a], mach64->pci_regs[0x59], mach64->pci_regs[0x58]);
    pclog("Rage Pro: SRC_CNTL %08X BUS_CNTL %08X BM_GUI_TABLE %08X BM_SYSTEM_TABLE %08X, %llu descriptors run\n", mach64->rp.src_cntl,
          mach64->bus_cntl, mach64->rp.gui_table, mach64->rp.system_table, (unsigned long long) mach64->rp.descriptors);
    pclog("Rage Pro: last descriptor %08X %08X %08X %08X, packet %d left at %03X\n", mach64->rp.frame_buf_offset,
          mach64->rp.system_mem_addr, mach64->rp.command, mach64->rp.status, mach64->rp.pkt_count, mach64->rp.pkt_reg);
    pclog("Rage Pro 2D: busy %d host %d op %d DP_SRC %08X DP_MIX %08X SRC_CNTL %08X DST_CNTL %08X DST_Y_X %08X DST_HEIGHT_WIDTH %08X "
          "DP_SET_GUI_ENGINE %08X FIFO %d/%d blitter %d\n",
          mach64->accel.busy, mach64->accel.source_host, mach64->accel.op, mach64->dp_src, mach64->dp_mix, mach64->src_cntl, mach64->dst_cntl,
          mach64->dst_y_x, mach64->dst_height_width, mach64->dp_set_gui_engine, (int) mach64->fifo_read_idx, (int) mach64->fifo_write_idx,
          (int) mach64->blitter_busy);
    mach64_timing_dump(mach64);
    pclog("Rage Pro: GUI_STAT %08X FIFO_STAT %08X\n", mach64_ext_readl(0x738, mach64), mach64_ext_readl(0x710, mach64));
    {
        char pll[64 * 3 + 1];

        for (int i = 0; i < 64; i++)
            sprintf(&pll[i * 3], " %02X", mach64->pll_regs[i]);
        pclog("Rage Pro: PLL%s\n", pll);
        pclog("Rage Pro: CLOCK_CNTL %08X MEM_CNTL %08X pixel clocks %.0f %.0f %.0f %.0f Hz\n", mach64->clock_cntl, mach64->mem_cntl,
              mach64->pll_freq[0], mach64->pll_freq[1], mach64->pll_freq[2], mach64->pll_freq[3]);
        if (MACH64_LT_REGS(mach64))
            pclog("Rage XL/LT: CONFIG_STAT1 %02X%02X%02X%02X LCD_INDEX %02X, LCD 0-9 %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
                  mach64->gtb_regs[0x97], mach64->gtb_regs[0x96], mach64->gtb_regs[0x95], mach64->gtb_regs[0x94], mach64->gtb_regs[0xa4],
                  mach64->lcd_regs[0], mach64->lcd_regs[1], mach64->lcd_regs[2], mach64->lcd_regs[3], mach64->lcd_regs[4],
                  mach64->lcd_regs[5], mach64->lcd_regs[6], mach64->lcd_regs[7], mach64->lcd_regs[8], mach64->lcd_regs[9]);
    }
    mach64_3d_ragepro_dump(mach64);
}

