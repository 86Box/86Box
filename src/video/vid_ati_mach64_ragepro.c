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
#include <86box/debug_cmd.h>

/* Block 1 (byte offsets in the 1K block below block 0). */
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
#define SRC_BM_OP_TO_REG 3 /* system memory to the bus master host data register */

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

/* The block 1 registers of the Rage Pro: AGP, the scaler's colour control
   and coefficients, the command FIFO, the bus master, the planar scaler
   buffers and the setup engine. */
static int
mach64_ragepro_blk1_reg(uint32_t addr)
{
    return ((addr >= RP1_AGP_BASE) && (addr <= RP1_SCALER_H_COEFF4)) || ((addr >= RP1_GUI_CMDFIFO_DEBUG) && (addr <= RP1_BM_STATUS)) ||
           (addr == RP1_BM_GUI_TABLE) || (addr == RP1_BM_SYSTEM_TABLE) ||
           ((addr >= RP1_SCALER_BUF0_OFFSET_U) && (addr <= RP1_SCALER_BUF1_OFFSET_V)) || ((addr >= RP1_SETUP_FIRST) && (addr <= RP1_SETUP_LAST));
}

void
mach64_ragepro_reset(mach64_t *mach64)
{
    memset(&mach64->rp, 0, sizeof(mach64->rp));
}

/* A register write from the bus master: idx is ATI's MM index, block 0 in
   00h-FFh, block 1 in 100h-1FFh. It goes in as the CPU's would. */
static void
mach64_ragepro_bm_reg_write(mach64_t *mach64, uint32_t idx, uint32_t val)
{
    idx &= 0x1ff;
    if (idx < 0x100)
        mach64_ext_writel(0x400 | (idx << 2), val, mach64);
    else
        mach64_ext_writel((idx & 0xff) << 2, val, mach64);
}

/* A dword of a GUI transfer to the aperture offset target. */
static void
mach64_ragepro_bm_gui_dword(mach64_t *mach64, uint32_t target, uint32_t val)
{
    svga_t  *svga = &mach64->svga;
    uint32_t reg;

    if ((target & 0xfff800) != BM_APERTURE_REGS) {
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
        if (!mach64->rp.pkt_count) {
            mach64->rp.pkt_reg   = val & 0xffff;
            mach64->rp.pkt_count = ((val >> 16) & 0xffff) + 1;
            return;
        }
        mach64_ragepro_bm_reg_write(mach64, mach64->rp.pkt_reg, val);
        mach64->rp.pkt_reg++;
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
 * A system transfer (8.10.3), started by a write of BM_SYSTEM_TABLE: the
 * table's address in 31:4, SYSTEM_TRIGGER in 2:0. Trigger 0 copies system
 * memory to the frame buffer, as the guide's example has it; 1 is taken as
 * the other way. The source's low two address bits give the alignment.
 */
static void
mach64_ragepro_system_master(mach64_t *mach64, uint32_t table)
{
    svga_t  *svga    = &mach64->svga;
    uint32_t addr    = table & ~0x0f;
    int      to_host = table & 1;
    uint8_t  buf[4096];

    mach64_wait_fifo_idle(mach64);
    mach64->rp.running = 1;
    for (int n = 0; n < BM_MAX_DESCRIPTORS; n++) {
        uint32_t count;
        uint32_t fb;
        uint32_t sys;

        mach64_ragepro_bm_descriptor(mach64, addr);
        addr += 16;

        count = mach64_ragepro_bm_count(mach64->rp.command);
        fb    = mach64->rp.frame_buf_offset & 0x00ffffff;
        sys   = mach64->rp.system_mem_addr;
        if (to_host)
            sys = (sys & ~3) | (fb & 3);
        else
            fb = (fb & ~3) | (sys & 3);

        if (count) {
            if (to_host) {
                for (uint32_t i = 0; i < count; i++)
                    buf[i] = svga->vram[(fb + i) & mach64->vram_mask];
                dma_bm_write(sys, buf, count, 1);
            } else {
                dma_bm_read(sys, buf, count, 1);
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t a = (fb + i) & mach64->vram_mask;

                    svga->vram[a]          = buf[i];
                    svga->changedvram[a >> 12] = svga->monitor->mon_changeframecount;
                }
            }
        }
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

    if (!mach64_ragepro_blk1_reg(reg))
        return 0;

    v = (mach64->rp.blk1[reg >> 2] & ~mask) | ((val << shift) & mask);
    mach64->rp.blk1[reg >> 2] = v;
    if (((addr & 3) + bytes) < 4)
        return 1;

    if ((reg >= RP1_SETUP_FIRST) && (reg <= RP1_SETUP_LAST)) {
        mach64_3d_setup_write(mach64, reg >> 2, v);
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
            mach64->rp.gui_cntl = v & 0x3;
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

    if (!mach64_ragepro_blk1_reg(reg))
        return 0;

    if ((reg >= RP1_SETUP_FIRST) && (reg <= RP1_SETUP_LAST)) {
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
            if (!(mach64->rp.src_cntl & SRC_BM_ENABLE) || (((mach64->rp.src_cntl >> SRC_BM_OP_SHIFT) & 3) != SRC_BM_OP_TO_REG) ||
                !mach64_ragepro_bm_enabled(mach64))
                return 0;
            mach64_ragepro_gui_master(mach64);
            return 1;
        default:
            return 0;
    }
}

/* debug_cmd "dev": the bus master and the setup engine. */
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
    mach64_3d_ragepro_dump(mach64);
}

static mach64_t *mach64_ragepro_debug_dev;

static void
mach64_ragepro_debug_hook(const char *args)
{
    if (!mach64_ragepro_debug_dev)
        return;
    if (args && !strncmp(args, "wlog", 4)) {
        mach64_ragepro_debug_dev->rp.wlog = atoi(args + 4);
        return;
    }
    if (args && !strncmp(args, "tris", 4)) {
        mach64_ragepro_debug_dev->rp.trace = atoi(args + 4);
        return;
    }
    mach64_ragepro_dump(mach64_ragepro_debug_dev);
}

/* The card answers debug_cmd's "dev" while it is in. */
void
mach64_ragepro_debug_attach(mach64_t *mach64, int attach)
{
    if (attach) {
        mach64_ragepro_debug_dev = mach64;
        debug_cmd_set_device_hook(mach64_ragepro_debug_hook);
    } else if (mach64_ragepro_debug_dev == mach64) {
        mach64_ragepro_debug_dev = NULL;
        debug_cmd_set_device_hook(NULL);
    }
}
