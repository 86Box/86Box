/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATi Mach64 graphics card 2D-accelerated portion.
 *          The ATi Mach64 is a 1994 Windows 2D accelerator.
 *          Technical information is available at: https://bitsavers.org/components/ati/RRG-S00700-05_mach64_Register_Reference_Guide_1999410.pdf
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde, <mario64crashed@gmail.com> <https://starfrost.net>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2026 Connor Hyde.
 */

#include "vid_ati_mach64.h"

static void
mach64_accel_write_fifo_l(mach64_t *mach64, uint32_t addr, uint32_t val);

static void
mach64_recalc_dp_set_engine(mach64_t *mach64)
{
    static const unsigned int pitches[16] =
    {
        [0] = 320, // fallback
        [1] = 320,
        [2] = 352,
        [3] = 384,
        [4] = 640,
        [5] = 800,
        [6] = 896,
        [7] = 512,
        [8] = 1024,
        [9] = 1152,
        [10] = 1280,
        [11] = 400,
        [12] = 832,
        [13] = 1600,
        [14] = 448,
        [15] = 2048
    };

    mach64->dst_y_x = 0;
    mach64->dst_height_width = 0;
    mach64->src_y_x = 0;
    mach64->sc_top_bottom = 0x3FFF0000;
    mach64->sc_left_right = 0x1FFF0000;
    mach64->write_mask = ~0u;
    mach64->clr_cmp_clr = 0;
    mach64->src_y_x_start = 0;
    mach64->src_cntl &= ~((3 << 13) | (1 << 5) | (1 << 12));
    mach64->dst_cntl &= ~(7 << 13);
    mach64->dp_pix_width &= (1 << 13);

    mach64->dp_pix_width = (mach64->dp_pix_width & ~7) | ((mach64->dp_set_gui_engine >> 3) & 7);
    mach64->dp_pix_width = (mach64->dp_pix_width & ~(0xf << 8)) | ((mach64->dp_set_gui_engine & (1 << 6)) ? ((mach64->dp_pix_width & 7) << 8) : 0);

    mach64->dst_off_pitch = (262144 * ((mach64->dp_set_gui_engine >> 7) & 3)) & ((1 << 20) - 1);
    mach64->dst_off_pitch |= (((pitches[(mach64->dp_set_gui_engine >> 10) & 0xF]) * ((mach64->dp_set_gui_engine & (1 << 14)) ? 2 : 1)) / 8) << 22;

    mach64->src_off_pitch = 0;
    if (mach64->dp_set_gui_engine & (1 << 15))
        mach64->src_off_pitch = mach64->dst_off_pitch;

    switch ((mach64->dp_set_gui_engine >> 16) & 3)
    {
        case 0:
            mach64->src_height1_width1 = mach64->src_height2_width2 = (0x00080008);
            break;
        case 1:
            mach64->src_height1_width1 = mach64->src_height2_width2 = (0x00200001);
            break;
        case 2:
            mach64->src_height1_width1 = mach64->src_height2_width2 = (0x00180008);
            break;
    }

    switch ((mach64->dp_set_gui_engine >> 20) & 0xf)
    {
        case 0:
            pclog("unknown drawing combo2\n");
            break;
        case 1:
            mach64->dp_mix = 0x070003;
            mach64->dp_src = 0x0000100;
            mach64->gui_traj_cntl = 0x23;
            break;
        case 2:
            mach64->dp_src = 0x200;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0x3;
            break;
        case 3:
            mach64->dp_src = 0x20100;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0x3;
            break;
        case 4:
            mach64->dp_src = 0x100;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0x3;
            break;
        case 5:
            mach64->dp_src = 0x10100;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0x01000003;
            break;
        case 6:
            mach64->dp_src = 0x100;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0x3;
            break;
        case 7:
            mach64->dp_src = 0x300;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0x30003;
            break;
        case 8:
            mach64->dp_src = 0x300;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0;
            break;
        case 9:
            mach64->dp_src = 0x300;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 1;
            break;
        case 10:
            mach64->dp_src = 0x300;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 2;
            break;
        case 11:
            mach64->dp_src = 0x300;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 3;
            break;
        case 12:
            mach64->dp_src = 0x20100;
            mach64->dp_mix = 0x70003;
            mach64->gui_traj_cntl = 0x1004001B;
            break;
        case 13:
            mach64->dp_src = 0x20100;
            mach64->dp_mix = 0x70003;
            mach64->gui_traj_cntl = 0x0004001B;
            break;
        case 14:
            pclog("unknown drawing combo\n");
            break;
        case 15:
            mach64->dp_src = 0x300;
            mach64->dp_mix = 0x70007;
            mach64->gui_traj_cntl = 0x0004001B;
            break;
    }
    mach64_accel_write_fifo_l(mach64, 0x330, mach64->gui_traj_cntl);
}

static void
mach64_accel_write_fifo(mach64_t *mach64, uint32_t addr, uint8_t val)
{
    switch (addr & 0x3ff) {
        case 0x100 ... 0x103:
            WRITE8(addr, mach64->dst_off_pitch, val);
            break;
        case 0x104 ... 0x105:
        case 0x11c ... 0x11d:
            WRITE8(addr + 2, mach64->dst_y_x, val);
            break;
        case 0x108 ... 0x109:
            WRITE8(addr, mach64->dst_y_x, val);
            break;
        case 0x10c ... 0x10f:
            WRITE8(addr, mach64->dst_y_x, val);
            break;
        case 0x2e8 ... 0x2eb:
            /* DST_X_Y (0_BA) and DST_WIDTH_HEIGHT (0_BB) are VT-B registers,
               in neither the GX nor the VT book. */
            if (mach64->type >= MACH64_VT3)
                WRITE8(addr ^ 2, mach64->dst_y_x, val);
            break;
        case 0x110 ... 0x111:
            WRITE8(addr + 2, mach64->dst_height_width, val);
            /* Writing DST_WIDTH also writes DST_BRES_LNTH (RRG 3-51). */
            mach64->dst_bres_lnth = (mach64->dst_bres_lnth & ~0x7fff) | ((mach64->dst_height_width >> 16) & 0x1fff);
            break;
        case 0x114 ... 0x115:
        case 0x118 ... 0x11b:
        case 0x11e ... 0x11f:
            WRITE8(addr, mach64->dst_height_width, val);
            mach64->dst_bres_lnth = (mach64->dst_bres_lnth & ~0x7fff) | ((mach64->dst_height_width >> 16) & 0x1fff);
            fallthrough;
        case 0x113:
            if (((addr & 0x3ff) == 0x11b || (addr & 0x3ff) == 0x11f || (addr & 0x3ff) == 0x113) && !(val & 0x80)) {
start_blit_op:
                mach64_start_fill(mach64);
                /* DST_WIDTH is 13 bits and DST_HEIGHT 15 (RRG 3-48, 3-51). */
                if ((mach64->dst_height_width & 0x7fff) && (mach64->dst_height_width & 0x1fff0000) && !mach64->accel.source_host)
                    mach64_blit(0, -1, mach64);
                else if (!(mach64->dst_height_width & 0x7fff) || !(mach64->dst_height_width & 0x1fff0000))
                    mach64->accel.busy = 0;
            }
            break;

        case 0x2ec ... 0x2ef:
            if (mach64->type < MACH64_VT3)
                break;
            WRITE8(addr ^ 2, mach64->dst_height_width, val);
            mach64->dst_bres_lnth = (mach64->dst_bres_lnth & ~0x7fff) | ((mach64->dst_height_width >> 16) & 0x1fff);
            if ((addr & 0x3ff) == 0x2ef) {
                goto start_blit_op;
            }
            break;
        case 0x120 ... 0x123:
            WRITE8(addr, mach64->dst_bres_lnth, val);
            /* ... and DST_BRES_LNTH, DST_WIDTH (RRG 3-45). */
            mach64->dst_height_width = (mach64->dst_height_width & 0x7fff) | ((mach64->dst_bres_lnth & 0x1fff) << 16);
            if ((addr & 0x3ff) == 0x123 && !(val & 0x80)) {
                mach64_start_line(mach64);

                if ((mach64->dst_bres_lnth & 0x7fff) && !mach64->accel.source_host)
                    mach64_blit(0, -1, mach64);
                else if (!(mach64->dst_bres_lnth & 0x7fff))
                    mach64->accel.busy = 0;
            }
            break;
        case 0x124 ... 0x127:
            WRITE8(addr, mach64->dst_bres_err, val);
            break;
        case 0x128 ... 0x12b:
            WRITE8(addr, mach64->dst_bres_inc, val);
            break;
        case 0x12c ... 0x12f:
            WRITE8(addr, mach64->dst_bres_dec, val);
            break;
        case 0x130 ... 0x133:
            WRITE8(addr, mach64->dst_cntl, val);
            break;
        case 0x180 ... 0x183:
            WRITE8(addr, mach64->src_off_pitch, val);
            break;
        /* SRC_Y_X: Y in 14:0, X in 28:16 (RRG 3-99); SRC_X and SRC_Y are its
           halves, as SRC_X_START and SRC_Y_START are SRC_Y_X_START's. */
        case 0x184 ... 0x185:
            WRITE8(addr + 2, mach64->src_y_x, val);
            break;
        case 0x188 ... 0x189:
            WRITE8(addr, mach64->src_y_x, val);
            break;
        case 0x18c ... 0x18f:
            WRITE8(addr, mach64->src_y_x, val);
            break;
        case 0x190 ... 0x191:
            WRITE8(addr + 2, mach64->src_height1_width1, val);
            break;
        case 0x194 ... 0x195:
            WRITE8(addr, mach64->src_height1_width1, val);
            break;
        case 0x198 ... 0x19b:
            WRITE8(addr, mach64->src_height1_width1, val);
            break;
        case 0x19c ... 0x19d:
            WRITE8(addr + 2, mach64->src_y_x_start, val);
            break;
        case 0x1a0 ... 0x1a1:
            WRITE8(addr, mach64->src_y_x_start, val);
            break;
        case 0x1a4 ... 0x1a7:
            WRITE8(addr, mach64->src_y_x_start, val);
            break;
        case 0x1a8 ... 0x1a9:
            WRITE8(addr + 2, mach64->src_height2_width2, val);
            break;
        case 0x1ac ... 0x1ad:
            WRITE8(addr, mach64->src_height2_width2, val);
            break;
        case 0x1b0 ... 0x1b3:
            WRITE8(addr, mach64->src_height2_width2, val);
            break;
        case 0x1b4 ... 0x1b7:
            WRITE8(addr, mach64->src_cntl, val);
#ifdef DMA_BM
            if (mach64->src_cntl & (1 << 9))
                pclog("Bus master enabled\n");
            else
                pclog("Bus master disabled\n");
#endif
            break;
        case 0x200 ... 0x23f:
            mach64_blit(val, 8, mach64);
            break;
        case 0x240 ... 0x243:
            WRITE8(addr, mach64->host_cntl, val);
            break;
        case 0x280 ... 0x283:
            WRITE8(addr, mach64->pat_reg0, val);
            break;
        case 0x284 ... 0x287:
            WRITE8(addr, mach64->pat_reg1, val);
            break;
        case 0x288 ... 0x28b:
            WRITE8(addr, mach64->pat_cntl, val);
            break;
        case 0x2a0 ... 0x2a1:
        case 0x2a8 ... 0x2a9:
            WRITE8(addr, mach64->sc_left_right, val);
            break;
        case 0x2a4 ... 0x2a5: // doesn't seem right.
            addr += 2;
            fallthrough;
        case 0x2aa ... 0x2ab:
            WRITE8(addr, mach64->sc_left_right, val);
            break;
        case 0x2ac ... 0x2ad:
        case 0x2b4 ... 0x2b5:
            WRITE8(addr, mach64->sc_top_bottom, val);
            break;
        case 0x2b0 ... 0x2b1:
            addr += 2;
            fallthrough;
        case 0x2b6 ... 0x2b7:
            WRITE8(addr, mach64->sc_top_bottom, val);
            break;
        case 0x2c0 ... 0x2c3:
            WRITE8(addr, mach64->dp_bkgd_clr, val);
            break;
        case 0x2c4 ... 0x2c7:
            WRITE8(addr, mach64->dp_frgd_clr, val);
            break;
        case 0x2c8 ... 0x2cb:
            WRITE8(addr, mach64->write_mask, val);
            break;
        case 0x2cc ... 0x2cf:
            WRITE8(addr, mach64->chain_mask, val);
            break;
        case 0x2d0 ... 0x2d3:
            WRITE8(addr, mach64->dp_pix_width, val);
            break;
        case 0x2d4 ... 0x2d7:
            WRITE8(addr, mach64->dp_mix, val);
            break;
        case 0x2d8 ... 0x2db:
            WRITE8(addr, mach64->dp_src, val);
            break;
        case 0x2fc ... 0x2ff:
            WRITE8(addr, mach64->dp_set_gui_engine, val);
            mach64_recalc_dp_set_engine(mach64);
            break;
        case 0x300 ... 0x303:
            WRITE8(addr, mach64->clr_cmp_clr, val);
            break;
        case 0x304 ... 0x307:
            WRITE8(addr, mach64->clr_cmp_mask, val);
            break;
        case 0x308 ... 0x30b:
            WRITE8(addr, mach64->clr_cmp_cntl, val);
            break;
        case 0x320 ... 0x323:
            WRITE8(addr, mach64->context_mask, val);
            break;
        case 0x330 ... 0x331:
            WRITE8(addr, mach64->dst_cntl, val);
            break;
        case 0x332:
            WRITE8(addr - 2, mach64->src_cntl, val);
            break;
        case 0x333:
            WRITE8(addr - 3, mach64->pat_cntl, val & 7);
            if (val & 0x10)
                mach64->host_cntl |= HOST_BYTE_ALIGN;
            else
                mach64->host_cntl &= ~HOST_BYTE_ALIGN;
            /* HOST_BIG_ENDIAN_EN, bit 29, on the CT and later (RRG 4-105). */
            if (mach64->type >= MACH64_CT)
                mach64->host_cntl = (mach64->host_cntl & ~2) | ((val & 0x20) ? 2 : 0);
            break;

        default:
            break;
    }
}
static void
mach64_accel_write_fifo_w(mach64_t *mach64, uint32_t addr, uint16_t val)
{
    // if the address is word aligned and we are between 200-23e, do a 16 pixel blit.

    addr &= 0x3fe;

    if (addr >= 0x200 && addr <= 0x23e)
        mach64_blit(val, 16, mach64);
    else {
        switch (addr & 0x3fe) {
            case 0x2fc:
                mach64->dp_set_gui_engine = (mach64->dp_set_gui_engine & 0xffff0000) | val;
                mach64_recalc_dp_set_engine(mach64);
                break;
            case 0x2fe:
                mach64->dp_set_gui_engine = (mach64->dp_set_gui_engine & 0xffff) | (val << 16);
                mach64_recalc_dp_set_engine(mach64);
                break;
            case 0x32c:
                mach64->context_load_cntl = (mach64->context_load_cntl & 0xffff0000) | val;
                break;
            case 0x32e:
                mach64->context_load_cntl = (mach64->context_load_cntl & 0x0000ffff) | ((uint32_t) val << 16);
                if ((mach64->context_load_cntl >> 16) & 3)
                    mach64_load_context(mach64);
                break;
            default:
                mach64_accel_write_fifo(mach64, addr, val);
                mach64_accel_write_fifo(mach64, addr + 1, val >> 8);
                break;
            }
    }

}
static void
mach64_accel_write_fifo_l(mach64_t *mach64, uint32_t addr, uint32_t val)
{
    addr &= 0x3fc;

    if (addr >= 0x200 && addr <= 0x23c)
        mach64_blit(val, 32, mach64); /* consumed in the order of PRG 2-35 (mach64_host_pixels) */
    else {
        switch (addr & 0x3fc) {
            case 0x32c:
                mach64->context_load_cntl = val;
                if ((val >> 16) & 3)
                    mach64_load_context(mach64);
                break;
            case 0x2fc:
                mach64->dp_set_gui_engine = val;
                mach64_recalc_dp_set_engine(mach64);
                break;
            default:
                mach64_accel_write_fifo_w(mach64, addr, val);
                mach64_accel_write_fifo_w(mach64, addr + 2, val >> 16);
                break;
        }
    }
}

#ifdef DMA_BM
static void
run_dma(mach64_t *mach64)
{
    int words_transferred = 0;

    thread_wait_mutex(mach64->dma.lock);
    thread_release_mutex(mach64->dma.lock);

}
#endif

__inline void
mach64_wake_fifo_thread(mach64_t *mach64)
{
    thread_set_event(mach64->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

/* Runs the oldest FIFO entry. The caller holds fifo_mutex, so the entries
   run one at a time and in order, whichever thread runs them; the read
   index moves only after the entry is done, so an empty FIFO means every
   write has reached the engine. */
static void
mach64_fifo_run_one(mach64_t *mach64)
{
    uint64_t      start_time = plat_timer_read();
    fifo_entry_t *fifo       = &mach64->fifo[mach64->fifo_read_idx & FIFO_MASK];
    uint32_t      val        = fifo->val;

    switch (fifo->addr_type & FIFO_TYPE) {
        case FIFO_WRITE_BYTE:
            mach64_accel_write_fifo(mach64, fifo->addr_type & FIFO_ADDR, val);
            break;
        case FIFO_WRITE_WORD:
            mach64_accel_write_fifo_w(mach64, fifo->addr_type & FIFO_ADDR, val);
            break;
        case FIFO_WRITE_DWORD:
            mach64_accel_write_fifo_l(mach64, fifo->addr_type & FIFO_ADDR, val);
            break;
        default:
            break;
    }

    fifo->addr_type = FIFO_INVALID;
    mach64->fifo_read_idx++;

    mach64->blitter_time += plat_timer_read() - start_time;
}

/* The emulated CPU needs the engine to have taken every queued write (a
   register read, a reset, a full queue): it runs them itself rather than
   waking the FIFO thread and sleeping until it has. A sleep is a whole
   host timer tick on Windows, and on ISA every byte of an engine register
   read came here, so M64DIAG's register tests ran at a few percent. */
void
mach64_wait_fifo_idle(mach64_t *mach64)
{
    if (FIFO_EMPTY)
        return;

    thread_wait_mutex(mach64->fifo_mutex);
    while (!FIFO_EMPTY)
        mach64_fifo_run_one(mach64);
    thread_release_mutex(mach64->fifo_mutex);
}

/* GEN_GUI_EN going to 0 resets the draw engine, which is how software
   recovers from a locked FIFO (RRG 3-55, 3-59); the queued writes are
   lost with it rather than run. */
void
mach64_fifo_discard(mach64_t *mach64)
{
    thread_wait_mutex(mach64->fifo_mutex);
    while (!FIFO_EMPTY) {
        mach64->fifo[mach64->fifo_read_idx & FIFO_MASK].addr_type = FIFO_INVALID;
        mach64->fifo_read_idx++;
    }
    mach64->accel.busy = 0;
    thread_release_mutex(mach64->fifo_mutex);
}

void
mach64_fifo_thread(void *param)
{
    mach64_t *mach64 = (mach64_t *) param;

    while (mach64->thread_run) {
        thread_wait_event(mach64->wake_fifo_thread, -1);
        thread_reset_event(mach64->wake_fifo_thread);
        mach64->blitter_busy = 1;
        while (!FIFO_EMPTY) {
            thread_wait_mutex(mach64->fifo_mutex);
            if (!FIFO_EMPTY)
                mach64_fifo_run_one(mach64);
            thread_release_mutex(mach64->fifo_mutex);
        }
#ifdef DMA_BM
        run_dma(mach64);
#endif
        mach64->blitter_busy = 0;
    }
}

void
mach64_queue(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type)
{
    fifo_entry_t *fifo = &mach64->fifo[mach64->fifo_write_idx & FIFO_MASK];
    int limit = 0;

    /*FIXME: I know it's a hack, but the way the threading is done causes some desyncs in the FIFO queue on some stuff
      (particularly accelerated 24bpp using Calculator on NT 3.x), so, until a proper solution is found, slow down only on
      initialization of the bitblt engine when the FIFO entries are more than 16.*/
    switch (type) {
        case FIFO_WRITE_BYTE:
            switch (addr & 0x3ff) {
                case 0x11b:
                    limit = 1;
                    break;
                default:
                    break;
            }
            break;
        case FIFO_WRITE_WORD:
            switch (addr & 0x3fe) {
                case 0x11a:
                    limit = 1;
                    break;
                default:
                    break;
            }
            break;
        case FIFO_WRITE_DWORD:
            switch (addr & 0x3fc) {
                case 0x118:
                    limit = 1;
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    /* Room in the ring, and the 16 entries above: the caller runs what is
       queued itself, as mach64_wait_fifo_idle does. */
    if ((limit && (FIFO_ENTRIES >= 16)) || FIFO_FULL)
        mach64_wait_fifo_idle(mach64);

    fifo->val       = val;
    fifo->addr_type = (addr & FIFO_ADDR) | type;

    mach64->fifo_write_idx++;

    if (FIFO_ENTRIES > 0xe000)
        mach64_wake_fifo_thread(mach64);
    if (FIFO_ENTRIES > 0xe000 || FIFO_ENTRIES < 8)
        mach64_wake_fifo_thread(mach64);
}

/* Pixel access by the draw engine. Width is a mach64_width[] code: 0, 1
   and 2 are 8, 16 and 32 bits, WIDTH_1BIT and WIDTH_4BIT pack eight and
   two pixels to the byte, in the order DP_BYTE_PIX_ORDER gives: 0 is from
   the MSBit (nibble) down, 1 from the LSBit (nibble) up (RRG 3-39). */
#define READ(addr, dat, width)                                                            \
    if (width == 0)                                                                       \
        dat = svga->vram[((addr)) & mach64->vram_mask];                                   \
    else if (width == 1)                                                                  \
        dat = *(uint16_t *) &svga->vram[((addr) << 1) & mach64->vram_mask];               \
    else if (width == 2)                                                                  \
        dat = *(uint32_t *) &svga->vram[((addr) << 2) & mach64->vram_mask];               \
    else if (width == WIDTH_4BIT) {                                                       \
        uint8_t nb_ = svga->vram[((addr) >> 1) & mach64->vram_mask];                      \
        if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)                                     \
            dat = ((addr) & 1) ? (nb_ >> 4) : (nb_ & 0xf);                                \
        else                                                                              \
            dat = ((addr) & 1) ? (nb_ & 0xf) : (nb_ >> 4);                                \
    } else if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)                                  \
        dat = (svga->vram[((addr) >> 3) & mach64->vram_mask] >> ((addr) &7)) & 1;         \
    else                                                                                  \
        dat = (svga->vram[((addr) >> 3) & mach64->vram_mask] >> (7 - ((addr) &7))) & 1;

#define MIX                                                      \
    switch (mix ? mach64->accel.mix_fg : mach64->accel.mix_bg) { \
        case 0x0:                                                \
            dest_dat = ~dest_dat;                                \
            break;                                               \
        case 0x1:                                                \
            dest_dat = 0;                                        \
            break;                                               \
        case 0x2:                                                \
            dest_dat = 0xffffffff;                               \
            break;                                               \
        case 0x3:                                                \
            dest_dat = dest_dat;                                 \
            break;                                               \
        case 0x4:                                                \
            dest_dat = ~src_dat;                                 \
            break;                                               \
        case 0x5:                                                \
            dest_dat = src_dat ^ dest_dat;                       \
            break;                                               \
        case 0x6:                                                \
            dest_dat = ~(src_dat ^ dest_dat);                    \
            break;                                               \
        case 0x7:                                                \
            dest_dat = src_dat;                                  \
            break;                                               \
        case 0x8:                                                \
            dest_dat = ~(src_dat & dest_dat);                    \
            break;                                               \
        case 0x9:                                                \
            dest_dat = ~src_dat | dest_dat;                      \
            break;                                               \
        case 0xa:                                                \
            dest_dat = src_dat | ~dest_dat;                      \
            break;                                               \
        case 0xb:                                                \
            dest_dat = src_dat | dest_dat;                       \
            break;                                               \
        case 0xc:                                                \
            dest_dat = src_dat & dest_dat;                       \
            break;                                               \
        case 0xd:                                                \
            dest_dat = src_dat & ~dest_dat;                      \
            break;                                               \
        case 0xe:                                                \
            dest_dat = ~src_dat & dest_dat;                      \
            break;                                               \
        case 0xf:                                                \
            dest_dat = ~(src_dat | dest_dat);                    \
            break;                                               \
        case 0x17:                                               \
            dest_dat = mach64_mix_avg(mach64, src_dat, dest_dat); \
            break;                                               \
        default:                                                 \
            break;                                               \
    }

/* MEM_BNDRY_EN (MEM_CNTL bit 18): "all draw engine functions that access
   the memory below the boundary are inhibited" (Programmer's Guide 2-65). */
static inline int
mach64_below_bndry(const mach64_t *mach64, uint32_t addr, int width)
{
    uint32_t byte;

    if (!mach64_mem_bndry_en(mach64))
        return 0;
    switch (width) {
        case 0:
            byte = addr;
            break;
        case 1:
            byte = addr << 1;
            break;
        case 2:
            byte = addr << 2;
            break;
        case WIDTH_4BIT:
            byte = addr >> 1;
            break;
        default:
            byte = addr >> 3;
            break;
    }
    return (byte & mach64->vram_mask) < mach64_mem_bndry(mach64);
}

#define WRITE(addr, width)                                                                                  \
    if (mach64_below_bndry(mach64, (addr), (width))) {                                                      \
    } else if (width == 0) {                                                                                \
        svga->vram[(addr) &mach64->vram_mask]                = dest_dat;                                    \
        svga->changedvram[((addr) &mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount;         \
    } else if (width == 1) {                                                                                \
        *(uint16_t *) &svga->vram[((addr) << 1) & mach64->vram_mask] = dest_dat;                            \
        svga->changedvram[(((addr) << 1) & mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount; \
    } else if (width == 2) {                                                                                \
        *(uint32_t *) &svga->vram[((addr) << 2) & mach64->vram_mask] = dest_dat;                            \
        svga->changedvram[(((addr) << 2) & mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount; \
    } else if (width == WIDTH_4BIT) {                                                                       \
        uint8_t *nb_ = &svga->vram[((addr) >> 1) & mach64->vram_mask];                                      \
        int      hi_ = (mach64->dp_pix_width & DP_BYTE_PIX_ORDER) ? ((addr) & 1) : !((addr) & 1);             \
        if (hi_)                                                                                            \
            *nb_ = (*nb_ & 0x0f) | ((dest_dat & 0xf) << 4);                                                 \
        else                                                                                                \
            *nb_ = (*nb_ & 0xf0) | (dest_dat & 0xf);                                                        \
        svga->changedvram[(((addr) >> 1) & mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount; \
    } else {                                                                                                \
        if (dest_dat & 1) {                                                                                 \
            if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)                                                   \
                svga->vram[((addr) >> 3) & mach64->vram_mask] |= 1 << ((addr) &7);                          \
            else                                                                                            \
                svga->vram[((addr) >> 3) & mach64->vram_mask] |= 1 << (7 - ((addr) &7));                    \
        } else {                                                                                            \
            if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)                                                   \
                svga->vram[((addr) >> 3) & mach64->vram_mask] &= ~(1 << ((addr) &7));                       \
            else                                                                                            \
                svga->vram[((addr) >> 3) & mach64->vram_mask] &= ~(1 << (7 - ((addr) &7)));                 \
        }                                                                                                   \
        svga->changedvram[(((addr) >> 3) & mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount; \
    }

static inline int
mach64_sext(uint32_t v, int bits)
{
    return (int) (v << (32 - bits)) >> (32 - bits);
}

/* The bits one pixel of this width takes. */
static int
mach64_pix_bits(int size)
{
    switch (size) {
        case WIDTH_1BIT:
            return 1;
        case WIDTH_4BIT:
            return 4;
        case 0:
            return 8;
        case 1:
            return 16;
        default:
            return 32;
    }
}

/* Offsets are in 64-bit units; the engine addresses pixels. */
static uint32_t
mach64_offset_pixels(uint32_t off_pitch, int size)
{
    uint32_t bytes = (off_pitch & 0xfffff) << 3;

    if (size == WIDTH_1BIT)
        return bytes << 3;
    if (size == WIDTH_4BIT)
        return bytes << 1;
    return bytes >> size;
}

/* Mix 17h, (DST+SRC)/2: an add whose carry DP_CHAIN_MSK breaks at every '1',
   and implicitly at the top of each 16 bits, then shifted within each piece
   (RRG 3-35, 3-37). */
static uint32_t
mach64_mix_avg(mach64_t *mach64, uint32_t s, uint32_t d)
{
    int      bits  = mach64_pix_bits(mach64->accel.dst_size);
    uint32_t chain = (mach64->chain_mask & 0x7fff) | 0x8000;
    uint32_t ret   = 0;
    int      start = 0;

    if (bits < 16)
        chain |= 1u << (bits - 1);
    for (int i = 0; i < ((bits < 16) ? bits : 32); i++) {
        if (((chain >> (i & 15)) & 1) || (i == ((bits < 16) ? bits : 32) - 1)) {
            int      w    = i - start + 1;
            uint32_t m    = (w >= 32) ? 0xffffffff : ((1u << w) - 1);
            uint32_t sum  = ((s >> start) & m) + ((d >> start) & m);

            ret |= ((sum >> 1) & m) << start;
            start = i + 1;
        }
    }
    return ret;
}

/* The pixels a HOST_DATA write of `bits` bits carries, in the order the
   engine consumes them (PRG 2-35). Bytes go lowest first left to right,
   highest first right to left; within a byte, 1 bpp and 4 bpp pixels go
   from the top when the pixel order is 0 left to right, or 1 right to
   left, and from the bottom otherwise. Lines consume as left to right. The
   CT's HOST_BIG_ENDIAN_EN swaps the bytes of each word (15/16 bpp) or
   dword (32 bpp) first (RRG 3-64). Returns the count; byte_of[] gives the
   byte each came from, for HOST_BYTE_ALIGN. */
static int
mach64_host_pixels(mach64_t *mach64, uint32_t val, int bits, uint32_t *out, uint8_t *byte_of)
{
    int w      = (mach64->accel.source_mix == MONO_SRC_HOST) ? 1 : mach64_pix_bits(mach64->accel.host_size);
    int ltr    = (mach64->accel.op == OP_LINE) || (mach64->accel.xinc > 0);
    int order  = !!(mach64->dp_pix_width & DP_BYTE_PIX_ORDER);
    int nbytes = bits >> 3;
    int n      = 0;

    if ((mach64->type >= MACH64_CT) && (mach64->host_cntl & 2) && (w >= 16)) {
        if (w == 16)
            val = ((val & 0x00ff00ff) << 8) | ((val & 0xff00ff00) >> 8);
        else
            val = (val >> 24) | ((val >> 8) & 0xff00) | ((val << 8) & 0xff0000) | (val << 24);
    }

    if (w >= 8) {
        int per = w >> 3;
        int cnt = nbytes / per;

        if (!cnt)
            cnt = 1, w = bits; /* narrower than a pixel: the bits written */
        for (int i = 0; i < cnt; i++) {
            int idx = ltr ? i : (cnt - 1 - i);

            out[n]     = (w >= 32) ? val : ((val >> (idx * w)) & ((1u << w) - 1));
            byte_of[n] = idx * (w >> 3);
            n++;
        }
    } else {
        int per   = 8 / w;
        int hi    = (!order && ltr) || (order && !ltr);
        uint32_t m = (1u << w) - 1;

        for (int b = 0; b < nbytes; b++) {
            int     bi   = ltr ? b : (nbytes - 1 - b);
            uint8_t byte = (val >> (bi * 8)) & 0xff;

            for (int k = 0; k < per; k++) {
                int pos    = hi ? (per - 1 - k) : k;
                out[n]     = (byte >> (pos * w)) & m;
                byte_of[n] = b;
                n++;
            }
        }
    }
    return n;
}

static void
mach64_accel_common(mach64_t *mach64)
{
    mach64->accel.dst_pix_width  = mach64->dp_pix_width & 7;
    mach64->accel.src_pix_width  = (mach64->dp_pix_width >> 8) & 7;
    mach64->accel.host_pix_width = (mach64->dp_pix_width >> 16) & 7;

    mach64->accel.dst_size  = mach64_width[mach64->accel.dst_pix_width];
    mach64->accel.src_size  = mach64_width[mach64->accel.src_pix_width];
    mach64->accel.host_size = mach64_width[mach64->accel.host_pix_width];

    mach64->accel.src_pitch  = (mach64->src_off_pitch >> 22) << 3;
    mach64->accel.src_offset = mach64_offset_pixels(mach64->src_off_pitch, mach64->accel.src_size);
    mach64->accel.dst_pitch  = (mach64->dst_off_pitch >> 22) << 3;
    mach64->accel.dst_offset = mach64_offset_pixels(mach64->dst_off_pitch, mach64->accel.dst_size);
    /* The polygon boundary is an implicit 1 bpp source (PRG 2-43). */
    mach64->accel.poly_offset = mach64_offset_pixels(mach64->src_off_pitch, WIDTH_1BIT);

    mach64->accel.mix_fg = (mach64->dp_mix >> 16) & 0x1f;
    mach64->accel.mix_bg = mach64->dp_mix & 0x1f;

    mach64->accel.source_bg  = mach64->dp_src & 7;
    mach64->accel.source_fg  = (mach64->dp_src >> 8) & 7;
    mach64->accel.source_mix = (mach64->dp_src >> 16) & 7;
    mach64->accel.source_host = ((mach64->dp_src & 7) == SRC_HOST) || (((mach64->dp_src >> 8) & 7) == SRC_HOST) ||
                                (((mach64->dp_src >> 16) & 7) == MONO_SRC_HOST);

    /* Mono 8x8 pattern, by DP_BYTE_PIX_ORDER (PRG 2-36): order 0 takes
       column n from bit 7-n of each byte, order 1 from bit n. */
    if (mach64->pat_cntl & 1) {
        for (uint8_t y = 0; y < 8; y++) {
            for (uint8_t x = 0; x < 8; x++) {
                uint32_t temp = (y & 4) ? mach64->pat_reg1 : mach64->pat_reg0;
                int      bit  = (mach64->dp_pix_width & DP_BYTE_PIX_ORDER) ? x : (7 - x);

                mach64->accel.pattern[y][x] = (temp >> (bit + ((y & 3) << 3))) & 1;
            }
        }
    }
    if (mach64->pat_cntl & 2) {
        for (int i = 0; i < 4; i++) {
            mach64->accel.pattern_clr4x2[0][i] = (mach64->pat_reg0 >> (i * 8)) & 0xff;
            mach64->accel.pattern_clr4x2[1][i] = (mach64->pat_reg1 >> (i * 8)) & 0xff;
        }
    }
    if (mach64->pat_cntl & 4) {
        for (int i = 0; i < 4; i++) {
            mach64->accel.pattern_clr8x1[i]     = (mach64->pat_reg0 >> (i * 8)) & 0xff;
            mach64->accel.pattern_clr8x1[i + 4] = (mach64->pat_reg1 >> (i * 8)) & 0xff;
        }
    }

    /* Scissors are signed: 13 bits across, 15 down (RRG 3-78..3-83). */
    mach64->accel.sc_left   = mach64_sext(mach64->sc_left_right & 0x1fff, 13);
    mach64->accel.sc_right  = mach64_sext((mach64->sc_left_right >> 16) & 0x1fff, 13);
    mach64->accel.sc_top    = mach64_sext(mach64->sc_top_bottom & 0x7fff, 15);
    mach64->accel.sc_bottom = mach64_sext((mach64->sc_top_bottom >> 16) & 0x7fff, 15);

    mach64->accel.dp_frgd_clr = mach64->dp_frgd_clr;
    mach64->accel.dp_bkgd_clr = mach64->dp_bkgd_clr;
    mach64->accel.write_mask  = mach64->write_mask;

    mach64->accel.clr_cmp_clr  = mach64->clr_cmp_clr & mach64->clr_cmp_mask;
    mach64->accel.clr_cmp_mask = mach64->clr_cmp_mask;
    mach64->accel.clr_cmp_fn   = mach64->clr_cmp_cntl & 7;
    mach64->accel.clr_cmp_src  = mach64->clr_cmp_cntl & (1 << 24);

    mach64->accel.xinc = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
    mach64->accel.yinc = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;

    mach64->accel.skip_byte = -1;
}

void
mach64_start_fill(mach64_t *mach64)
{
    mach64_accel_common(mach64);

    mach64->accel.dst_x = 0;
    mach64->accel.dst_y = 0;

    /* DST_X signed 13 bits, DST_Y signed 15 (RRG 3-52, 3-54). */
    mach64->accel.dst_x_start = mach64_sext((mach64->dst_y_x >> 16) & 0x1fff, 13);
    mach64->accel.dst_y_start = mach64_sext(mach64->dst_y_x & 0x7fff, 15);

    /* DST_WIDTH 13 bits, DST_HEIGHT 15 (RRG 3-48, 3-51). */
    mach64->accel.dst_width  = (mach64->dst_height_width >> 16) & 0x1fff;
    mach64->accel.dst_height = mach64->dst_height_width & 0x7fff;
    mach64->accel.x_count    = mach64->accel.dst_width;

    mach64->accel.src_x = 0;
    mach64->accel.src_y = 0;
    mach64->accel.src_x_start = mach64_sext((mach64->src_y_x >> 16) & 0x1fff, 13);
    mach64->accel.src_y_start = mach64_sext(mach64->src_y_x & 0x7fff, 15);

    if (mach64->src_cntl & SRC_LINEAR_EN)
        mach64->accel.src_x_count = 0x7ffffff; /*Essentially infinite*/
    else
        mach64->accel.src_x_count = (mach64->src_height1_width1 >> 16) & 0x1fff;
    if (!(mach64->src_cntl & SRC_PATT_EN))
        mach64->accel.src_y_count = 0x7ffffff; /*Essentially infinite*/
    else
        mach64->accel.src_y_count = mach64->src_height1_width1 & 0x7fff;

    mach64->accel.src_width1  = (mach64->src_height1_width1 >> 16) & 0x1fff;
    mach64->accel.src_height1 = mach64->src_height1_width1 & 0x7fff;
    mach64->accel.src_width2  = (mach64->src_height2_width2 >> 16) & 0x1fff;
    mach64->accel.src_height2 = mach64->src_height2_width2 & 0x7fff;

    if ((mach64->src_cntl & SRC_8x8x8_BRUSH) && !(mach64->src_cntl & SRC_8x8x8_BRUSH_LOADED)) {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                mach64->accel.pattern_clr8x8[y][x] = mach64->svga.vram[((mach64->accel.src_offset & ~7) + (y * 8) + x) & mach64->vram_mask];
    }

    /* Packed 24 bpp: the colour component under the first byte, from
       DST_24_ROT -- the dword of the starting byte mod 6 -- and the byte
       within its dword (PRG 4-7). */
    mach64->accel.rot0 = (((((mach64->dst_cntl >> 8) & 7) * 4) + (mach64->accel.dst_x_start & 3)) % 3);
    mach64->accel.rot  = mach64->accel.rot0;

    mach64->accel.poly_draw = 0;
    mach64->accel.busy      = 1;
    mach64->accel.op        = OP_RECT;
}

void
mach64_start_line(mach64_t *mach64)
{
    mach64_accel_common(mach64);

    mach64->accel.dst_x = mach64_sext((mach64->dst_y_x >> 16) & 0x1fff, 13);
    mach64->accel.dst_y = mach64_sext(mach64->dst_y_x & 0x7fff, 15);
    mach64->accel.src_x = mach64_sext((mach64->src_y_x >> 16) & 0x1fff, 13);
    mach64->accel.src_y = mach64_sext(mach64->src_y_x & 0x7fff, 15);

    /* The Bresenham terms are signed 18 bits (RRG 3-42..3-44). */
    mach64->accel.x_count = mach64->dst_bres_lnth & 0x7fff;
    mach64->accel.err     = mach64_sext(mach64->dst_bres_err & 0x3ffff, 18);
    mach64->accel.inc     = mach64_sext(mach64->dst_bres_inc & 0x3ffff, 18);
    mach64->accel.dec     = mach64_sext(mach64->dst_bres_dec & 0x3ffff, 18);

    mach64->accel.busy = 1;
    mach64->accel.op   = OP_LINE;
}

// calculates colour compare function for mach64 blit
int32_t
mach64_blit_calc_cmp_clr(mach64_t* mach64, uint32_t src_dat, uint32_t dest_dat)
{
    int32_t cmp_clr = 0;

    switch (mach64->accel.clr_cmp_fn) {
        case 1: /*TRUE*/
            cmp_clr = 1;
            break;
        case 4: /*DST_CLR != CLR_CMP_CLR*/
            cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) != mach64->accel.clr_cmp_clr;
            break;
        case 5: /*DST_CLR == CLR_CMP_CLR*/
            cmp_clr = (((mach64->accel.clr_cmp_src) ? src_dat : dest_dat) & mach64->accel.clr_cmp_mask) == mach64->accel.clr_cmp_clr;
            break;
        default:
            break;
    }

    return cmp_clr;
}

/* A colour, write mask or pattern byte in packed 24 bpp: the component
   under the current byte. */
static uint32_t
mach64_rot_byte(mach64_t *mach64, uint32_t v)
{
    return (v >> (8 * mach64->accel.rot)) & 0xff;
}

/* One destination pixel of a rectangle fill or blit. host is the host
   pixel (colour, or the mono bit) for this pixel when the host feeds it.
   Returns 1 when the operation has finished. */
static int
mach64_rect_pixel(mach64_t *mach64, uint32_t host)
{
    svga_t  *svga    = &mach64->svga;
    int      rot     = !!(mach64->dst_cntl & DST_24_ROT_EN);
    int      mix     = 0;
    uint32_t src_dat = 0;
    uint32_t dest_dat;
    uint32_t old_dest_dat;
    int      dst_x = mach64->accel.dst_x + mach64->accel.dst_x_start;
    int      dst_y = mach64->accel.dst_y + mach64->accel.dst_y_start;
    int      src_x, src_y;
    int      draw = 1;

    if (mach64->src_cntl & SRC_LINEAR_EN)
        src_x = mach64->accel.src_x;
    else
        src_x = mach64->accel.src_x + mach64->accel.src_x_start;
    src_y = mach64->accel.src_y + mach64->accel.src_y_start;

    switch (mach64->accel.source_mix) {
        case MONO_SRC_HOST:
            mix = host & 1;
            break;
        case MONO_SRC_PAT:
            if (rot)
                mix = mach64->accel.pattern[dst_y & 7][((dst_x - ((dst_x < 0) ? 2 : 0)) / 3) & 7];
            else
                mix = mach64->accel.pattern[dst_y & 7][dst_x & 7];
            break;
        case MONO_SRC_1:
            mix = 1;
            break;
        case MONO_SRC_BLITSRC:
            if (mach64->src_cntl & SRC_LINEAR_EN) {
                READ(mach64->accel.src_offset + src_x, mix, WIDTH_1BIT);
            } else {
                READ(mach64->accel.src_offset + (src_y * mach64->accel.src_pitch) + src_x, mix, WIDTH_1BIT);
            }
            break;
        default:
            break;
    }

    /* Polygon fill: the boundary source toggles the fill, both edges drawn
       (PRG 2-43); on the CT, DST_POLYGON_RTEDGE_DIS leaves the right one
       out (RRG 3-46). */
    if (mach64->dst_cntl & DST_POLYGON_EN) {
        int bound;
        int was = mach64->accel.poly_draw;

        READ(mach64->accel.poly_offset + (src_y * mach64->accel.src_pitch) + src_x, bound, WIDTH_1BIT);
        if (bound)
            mach64->accel.poly_draw = !mach64->accel.poly_draw;
        draw = was || mach64->accel.poly_draw;
        if (bound && was && !mach64->accel.poly_draw && (mach64->type >= MACH64_CT) && (mach64->dst_cntl & DST_POLYGON_RTEDGE_DIS))
            draw = 0;
    }

    if (draw && (dst_x >= mach64->accel.sc_left) && (dst_x <= mach64->accel.sc_right) &&
        (dst_y >= mach64->accel.sc_top) && (dst_y <= mach64->accel.sc_bottom)) {
        switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg) {
            case SRC_HOST:
                src_dat = host;
                break;
            case SRC_BLITSRC:
                if (mach64->accel.src_size == 0 && mach64->type == MACH64_VT3 && (mach64->src_cntl & SRC_8x8x8_BRUSH))
                    src_dat = mach64->accel.pattern_clr8x8[dst_y & 7][dst_x & 7];
                else if (mach64->src_cntl & SRC_LINEAR_EN) {
                    READ(mach64->accel.src_offset + src_x, src_dat, mach64->accel.src_size);
                } else {
                    READ(mach64->accel.src_offset + (src_y * mach64->accel.src_pitch) + src_x, src_dat, mach64->accel.src_size);
                }
                break;
            case SRC_FG:
                src_dat = rot ? mach64_rot_byte(mach64, mach64->accel.dp_frgd_clr) : mach64->accel.dp_frgd_clr;
                break;
            case SRC_BG:
                src_dat = rot ? mach64_rot_byte(mach64, mach64->accel.dp_bkgd_clr) : mach64->accel.dp_bkgd_clr;
                break;
            case SRC_PAT:
                if (mach64->pat_cntl & 2)
                    src_dat = mach64->accel.pattern_clr4x2[dst_y & 1][dst_x & 3];
                else if (mach64->pat_cntl & 4)
                    src_dat = mach64->accel.pattern_clr8x1[dst_x & 7];
                break;
            default:
                break;
        }

        READ(mach64->accel.dst_offset + (dst_y * mach64->accel.dst_pitch) + dst_x, dest_dat, mach64->accel.dst_size);

        if (!mach64_blit_calc_cmp_clr(mach64, src_dat, dest_dat)) {
            uint32_t wm = rot ? mach64_rot_byte(mach64, mach64->accel.write_mask) : mach64->accel.write_mask;

            old_dest_dat = dest_dat;
            MIX
            dest_dat = (dest_dat & wm) | (old_dest_dat & ~wm);
        }

        WRITE(mach64->accel.dst_offset + (dst_y * mach64->accel.dst_pitch) + dst_x, mach64->accel.dst_size);
    }

    /* Next pixel: the source trajectory, then the destination. */
    if (mach64->src_cntl & SRC_LINEAR_EN)
        mach64->accel.src_x++;
    else {
        mach64->accel.src_x += mach64->accel.xinc;
        if (--mach64->accel.src_x_count <= 0) {
            mach64->accel.src_x = 0;
            if ((mach64->src_cntl & (SRC_PATT_ROT_EN | SRC_PATT_EN)) == (SRC_PATT_ROT_EN | SRC_PATT_EN)) {
                mach64->accel.src_x_start = mach64_sext((mach64->src_y_x_start >> 16) & 0x1fff, 13);
                mach64->accel.src_x_count = mach64->accel.src_width2;
            } else
                mach64->accel.src_x_count = mach64->accel.src_width1;
        }
    }
    mach64->accel.dst_x += mach64->accel.xinc;
    mach64->accel.rot = (mach64->accel.xinc > 0) ? ((mach64->accel.rot + 1) % 3) : ((mach64->accel.rot + 2) % 3);

    if (--mach64->accel.x_count > 0)
        return 0;

    /* End of a row. */
    mach64->accel.x_count = mach64->accel.dst_width;
    mach64->accel.rot     = mach64->accel.rot0;
    mach64->accel.dst_x   = 0;
    mach64->accel.dst_y += mach64->accel.yinc;
    mach64->accel.src_x_start = mach64_sext((mach64->src_y_x >> 16) & 0x1fff, 13);
    mach64->accel.src_x_count = (mach64->src_cntl & SRC_LINEAR_EN) ? 0x7ffffff : mach64->accel.src_width1;

    if (mach64->src_cntl & SRC_LINEAR_EN) {
        /* SRC_BYTE_ALIGN: a 1 bpp or 4 bpp linear source skips to the next
           byte as the destination steps down (RRG 3-86). */
        if (mach64->src_cntl & SRC_BYTE_ALIGN) {
            int sz = ((mach64->accel.source_mix == MONO_SRC_BLITSRC) || (mach64->accel.src_size == WIDTH_1BIT)) ? WIDTH_1BIT : mach64->accel.src_size;

            if ((sz == WIDTH_1BIT) || (sz == WIDTH_4BIT)) {
                uint32_t per = (sz == WIDTH_1BIT) ? 8 : 2;
                uint32_t a   = mach64->accel.src_offset + mach64->accel.src_x;

                a = (a + per - 1) & ~(per - 1);
                mach64->accel.src_x = a - mach64->accel.src_offset;
            }
        }
    } else {
        mach64->accel.src_x = 0;
        mach64->accel.src_y += mach64->accel.yinc;
        if (--mach64->accel.src_y_count <= 0) {
            mach64->accel.src_y = 0;
            if ((mach64->src_cntl & (SRC_PATT_ROT_EN | SRC_PATT_EN)) == (SRC_PATT_ROT_EN | SRC_PATT_EN)) {
                mach64->accel.src_y_start = mach64_sext(mach64->src_y_x_start & 0x7fff, 15);
                mach64->accel.src_y_count = mach64->accel.src_height2;
            } else
                mach64->accel.src_y_count = mach64->accel.src_height1;
        }
    }

    mach64->accel.poly_draw = 0;
    /* HOST_BYTE_ALIGN: the rest of the current host byte is skipped. */
    mach64->accel.row_ended = 1;

    if (--mach64->accel.dst_height > 0)
        return 0;

    /*Blit finished*/
    mach64_log("mach64 blit finished\n");
    mach64->accel.busy = 0;
    /* Side effects (RRG 3-47): DST_X moves by DST_WIDTH, DST_Y by
       DST_HEIGHT, in the direction drawn, when tiling. */
    if (mach64->dst_cntl & DST_X_TILE) {
        int x = mach64_sext((mach64->dst_y_x >> 16) & 0x1fff, 13) + mach64->accel.xinc * (int) ((mach64->dst_height_width >> 16) & 0x1fff);

        mach64->dst_y_x = (mach64->dst_y_x & 0x7fff) | (((uint32_t) x & 0x1fff) << 16);
    }
    if (mach64->dst_cntl & DST_Y_TILE) {
        int y = mach64_sext(mach64->dst_y_x & 0x7fff, 15) + mach64->accel.yinc * (int) (mach64->dst_height_width & 0x7fff);

        mach64->dst_y_x = (mach64->dst_y_x & 0x1fff0000) | ((uint32_t) y & 0x7fff);
    }
    return 1;
}

/* One pixel of a Bresenham line. There is no 24 bpp line: "the line draw
   engine does not function in 24bpp packed mode" (PRG 4-7). The source
   is rectangular and never advances in Y; its X goes by SRC_LINE_X_DIR
   (RRG 3-86, 3-87). Returns 1 when the line has finished. */
static int
mach64_line_pixel(mach64_t *mach64, uint32_t host)
{
    svga_t  *svga    = &mach64->svga;
    int      mix     = 0;
    uint32_t src_dat = 0;
    uint32_t dest_dat;
    uint32_t old_dest_dat;
    int      err_pos = (mach64->accel.err > 0) || ((mach64->accel.err == 0) && !(mach64->dst_cntl & DST_BRES_SIGN));
    int      draw    = 1;
    int      dx      = mach64->accel.dst_x;

    switch (mach64->accel.source_mix) {
        case MONO_SRC_HOST:
            mix = host & 1;
            break;
        case MONO_SRC_PAT:
            mix = mach64->accel.pattern[mach64->accel.dst_y & 7][mach64->accel.dst_x & 7];
            break;
        case MONO_SRC_1:
            mix = 1;
            break;
        case MONO_SRC_BLITSRC:
            READ(mach64->accel.src_offset + (mach64->accel.src_y * mach64->accel.src_pitch) + mach64->accel.src_x, mix, WIDTH_1BIT);
            break;
        default:
            break;
    }

    /* Polygon outline: one pixel a scan line, none for horizontal lines,
       saturated to the left scissor (RRG 3-47). */
    if (mach64->dst_cntl & DST_POLYGON_EN) {
        draw = (mach64->dst_cntl & DST_Y_MAJOR) || err_pos;
        if (dx < mach64->accel.sc_left)
            dx = mach64->accel.sc_left;
    }

    if ((mach64->accel.x_count == 1) && !(mach64->dst_cntl & DST_LAST_PEL))
        draw = 0;

    if (draw && (dx >= mach64->accel.sc_left) && (dx <= mach64->accel.sc_right) &&
        (mach64->accel.dst_y >= mach64->accel.sc_top) && (mach64->accel.dst_y <= mach64->accel.sc_bottom)) {
        switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg) {
            case SRC_HOST:
                src_dat = host;
                break;
            case SRC_BLITSRC:
                if (mach64->accel.src_size == 0 && mach64->type == MACH64_VT3 && (mach64->src_cntl & SRC_8x8x8_BRUSH))
                    src_dat = mach64->accel.pattern_clr8x8[mach64->accel.dst_y & 7][dx & 7];
                else {
                    READ(mach64->accel.src_offset + (mach64->accel.src_y * mach64->accel.src_pitch) + mach64->accel.src_x, src_dat, mach64->accel.src_size);
                }
                break;
            case SRC_FG:
                src_dat = mach64->accel.dp_frgd_clr;
                break;
            case SRC_BG:
                src_dat = mach64->accel.dp_bkgd_clr;
                break;
            case SRC_PAT:
                if (mach64->pat_cntl & 2)
                    src_dat = mach64->accel.pattern_clr4x2[mach64->accel.dst_y & 1][dx & 3];
                else if (mach64->pat_cntl & 4)
                    src_dat = mach64->accel.pattern_clr8x1[dx & 7];
                break;
            default:
                break;
        }

        READ(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + dx, dest_dat, mach64->accel.dst_size);

        if (!mach64_blit_calc_cmp_clr(mach64, src_dat, dest_dat)) {
            old_dest_dat = dest_dat;
            MIX
            dest_dat = (dest_dat & mach64->accel.write_mask) | (old_dest_dat & ~mach64->accel.write_mask);
        }

        WRITE(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + dx, mach64->accel.dst_size);
    }

    if (--mach64->accel.x_count <= 0) {
        /*Blit finished: DST_X and DST_Y rest on the last pixel (PRG 2-43).*/
        mach64_log("mach64 line finished\n");
        mach64->accel.busy = 0;
        mach64->dst_y_x    = (((uint32_t) mach64->accel.dst_x & 0x1fff) << 16) | ((uint32_t) mach64->accel.dst_y & 0x7fff);
        return 1;
    }

    mach64->accel.src_x += (mach64->src_cntl & SRC_LINE_X_DIR) ? 1 : -1;
    if (mach64->dst_cntl & DST_Y_MAJOR) {
        mach64->accel.dst_y += mach64->accel.yinc;
        if (err_pos) {
            mach64->accel.err += mach64->accel.dec;
            mach64->accel.dst_x += mach64->accel.xinc;
        } else
            mach64->accel.err += mach64->accel.inc;
    } else {
        mach64->accel.dst_x += mach64->accel.xinc;
        if (err_pos) {
            mach64->accel.err += mach64->accel.dec;
            mach64->accel.dst_y += mach64->accel.yinc;
        } else
            mach64->accel.err += mach64->accel.inc;
    }
    return 0;
}

static int
mach64_pixel(mach64_t *mach64, uint32_t host)
{
    if (mach64->accel.op == OP_LINE)
        return mach64_line_pixel(mach64, host);
    return mach64_rect_pixel(mach64, host);
}

/* Runs the operation under way. count is -1 when nothing comes from the
   host: it runs to its end. Otherwise cpu_dat is a HOST_DATA write of
   count bits, whose pixels are consumed one a destination pixel; what is
   left when the operation ends is dropped. */
void
mach64_blit(uint32_t cpu_dat, int count, mach64_t *mach64)
{
    uint32_t pix[32];
    uint8_t  byte_of[32];
    int      n;

    if (!mach64->accel.busy) {
        mach64_log("mach64_blit : return as not busy\n");
        return;
    }

    if (count == -1) {
        while (mach64->accel.busy && !mach64_pixel(mach64, 0))
            ;
        return;
    }

    n = mach64_host_pixels(mach64, cpu_dat, count, pix, byte_of);
    for (int i = 0; (i < n) && mach64->accel.busy; i++) {
        /* HOST_BYTE_ALIGN: after a row, 1 bpp and 4 bpp host data resumes at
           the next byte (RRG 3-64). */
        if (mach64->accel.row_ended) {
            mach64->accel.row_ended = 0;
            if ((mach64->host_cntl & HOST_BYTE_ALIGN) && (mach64->accel.op == OP_RECT) && (i > 0)) {
                int w = (mach64->accel.source_mix == MONO_SRC_HOST) ? 1 : mach64_pix_bits(mach64->accel.host_size);

                if (w < 8) {
                    while ((i < n) && (byte_of[i] == byte_of[i - 1]))
                        i++;
                    if (i >= n)
                        break;
                }
            }
        }
        mach64_pixel(mach64, pix[i]);
    }
}

/* Loads a context from the save area and runs its command: 1 load only,
   2 load and fill a rectangle, 3 load and draw a line; each entry's own
   CONTEXT_LOAD_CNTL chains to the next. CONTEXT_LOAD_DIS stops the
   command written, and is ignored in the chain (RRG 3-15). */
void
mach64_load_context(mach64_t *mach64)
{
    svga_t  *svga   = &mach64->svga;
    uint32_t cntl   = mach64->context_load_cntl;
    uint32_t chunks = (uint32_t) (mach64->vram_size << 20) >> 8;
    uint32_t addr;
    int      cmd;
    static const uint16_t regs[28] = {
        [2] = 0x100, [3] = 0x10c, [4] = 0x118, [5] = 0x124, [6] = 0x128, [7] = 0x12c,
        [8] = 0x180, [9] = 0x18c, [10] = 0x198, [11] = 0x1a4, [12] = 0x1b0, [13] = 0x280,
        [14] = 0x284, [15] = 0x2a8, [16] = 0x2b4, [17] = 0x2c0, [18] = 0x2c4, [19] = 0x2c8,
        [20] = 0x2cc, [21] = 0x2d0, [22] = 0x2d4, [23] = 0x2d8, [24] = 0x300, [25] = 0x304,
        [26] = 0x308, [27] = 0x330
    };

    if (cntl & (1u << 31))
        return;
    cmd = (cntl >> 16) & 3;
    while (cmd) {
        addr                 = ((chunks - 1 - ((cntl & 0xffff) % chunks)) * 256) & mach64->vram_mask;
        mach64->context_mask = *(uint32_t *) &svga->vram[addr];
        mach64_log("mach64_load_context %08X from %08X : mask %08X\n", cntl, addr, mach64->context_mask);

        for (int e = 2; e < 28; e++) {
            uint32_t v;

            if (!(mach64->context_mask & (1u << e)))
                continue;
            v = *(uint32_t *) &svga->vram[(addr + e * 4) & mach64->vram_mask];
            if (e == 4) {
                /* DST_HEIGHT_WIDTH is stored, not written: a write would
                   start a fill before the rest is loaded. */
                mach64->dst_height_width = v;
                mach64->dst_bres_lnth    = (mach64->dst_bres_lnth & ~0x7fff) | ((v >> 16) & 0x1fff);
            } else
                mach64_accel_write_fifo_l(mach64, regs[e], v);
        }

        if (cmd == 2) {
            mach64_start_fill(mach64);
            if (!mach64->accel.source_host && (mach64->dst_height_width & 0x7fff) && (mach64->dst_height_width & 0x1fff0000))
                mach64_blit(0, -1, mach64);
        } else if (cmd == 3) {
            mach64_start_line(mach64);
            if (!mach64->accel.source_host && (mach64->dst_bres_lnth & 0x7fff))
                mach64_blit(0, -1, mach64);
        }

        cntl = *(uint32_t *) &svga->vram[(addr + 0x70) & mach64->vram_mask];
        cmd  = (cntl >> 16) & 3;
    }
    mach64->context_load_cntl = cntl;
}

//
// Overlay
//

#define CLAMP(x)                      \
    do {                              \
        if ((x) & ~0xff)              \
            x = ((x) < 0) ? 0 : 0xff; \
    } while (0)

#define DECODE_ARGB1555()                                            \
    do {                                                             \
        for (x = 0; x < mach64->svga.overlay_latch.cur_xsize; x++) { \
            uint16_t dat = ((uint16_t *) src)[x];                    \
                                                                     \
            int b = dat & 0x1f;                                      \
            int g = (dat >> 5) & 0x1f;                               \
            int r = (dat >> 10) & 0x1f;                              \
                                                                     \
            b = (b << 3) | (b >> 2);                                 \
            g = (g << 3) | (g >> 2);                                 \
            r = (r << 3) | (r >> 2);                                 \
                                                                     \
            mach64->overlay_dat[x] = (r << 16) | (g << 8) | b;       \
        }                                                            \
    } while (0)

#define DECODE_RGB565()                                              \
    do {                                                             \
        for (x = 0; x < mach64->svga.overlay_latch.cur_xsize; x++) { \
            uint16_t dat = ((uint16_t *) src)[x];                    \
                                                                     \
            int b = dat & 0x1f;                                      \
            int g = (dat >> 5) & 0x3f;                               \
            int r = (dat >> 11) & 0x1f;                              \
                                                                     \
            b = (b << 3) | (b >> 2);                                 \
            g = (g << 2) | (g >> 4);                                 \
            r = (r << 3) | (r >> 2);                                 \
                                                                     \
            mach64->overlay_dat[x] = (r << 16) | (g << 8) | b;       \
        }                                                            \
    } while (0)

#define DECODE_ARGB8888()                                            \
    do {                                                             \
        for (x = 0; x < mach64->svga.overlay_latch.cur_xsize; x++) { \
            int b = src[0];                                          \
            int g = src[1];                                          \
            int r = src[2];                                          \
            src += 4;                                                \
                                                                     \
            mach64->overlay_dat[x] = (r << 16) | (g << 8) | b;       \
        }                                                            \
    } while (0)

#define DECODE_VYUY422()                                                 \
    do {                                                                 \
        for (x = 0; x < src_w; x += 1) {                                 \
            uint8_t y1, y2;                                              \
            int8_t  u, v;                                                \
            int     dR, dG, dB;                                          \
            int     r, g, b;                                             \
                                                                         \
            y1 = src[0];                                                 \
            u  = src[1] - 0x80;                                          \
            y2 = src[2];                                                 \
            v  = src[3] - 0x80;                                          \
            src += 4;                                                    \
                                                                         \
            dR = (359 * v) >> 8;                                         \
            dG = (88 * u + 183 * v) >> 8;                                \
            dB = (453 * u) >> 8;                                         \
                                                                         \
            r = y1 + dR;                                                 \
            CLAMP(r);                                                    \
            g = y1 - dG;                                                 \
            CLAMP(g);                                                    \
            b = y1 + dB;                                                 \
            CLAMP(b);                                                    \
            mach64->overlay_dat[x * 2] = (r << 16) | (g << 8) | b;       \
                                                                         \
            r = y2 + dR;                                                 \
            CLAMP(r);                                                    \
            g = y2 - dG;                                                 \
            CLAMP(g);                                                    \
            b = y2 + dB;                                                 \
            CLAMP(b);                                                    \
            mach64->overlay_dat[(x * 2) + 1] = (r << 16) | (g << 8) | b; \
        }                                                                \
    } while (0)

#define DECODE_YVYU422()                                                 \
    do {                                                                 \
        for (x = 0; x < src_w; x += 1) {                                 \
            uint8_t y1, y2;                                              \
            int8_t  u, v;                                                \
            int     dR, dG, dB;                                          \
            int     r, g, b;                                             \
                                                                         \
            u  = src[0] - 0x80;                                          \
            y1 = src[1];                                                 \
            v  = src[2] - 0x80;                                          \
            y2 = src[3];                                                 \
            src += 4;                                                    \
                                                                         \
            dR = (359 * v) >> 8;                                         \
            dG = (88 * u + 183 * v) >> 8;                                \
            dB = (453 * u) >> 8;                                         \
                                                                         \
            r = y1 + dR;                                                 \
            CLAMP(r);                                                    \
            g = y1 - dG;                                                 \
            CLAMP(g);                                                    \
            b = y1 + dB;                                                 \
            CLAMP(b);                                                    \
            mach64->overlay_dat[x * 2] = (r << 16) | (g << 8) | b;       \
                                                                         \
            r = y2 + dR;                                                 \
            CLAMP(r);                                                    \
            g = y2 - dG;                                                 \
            CLAMP(g);                                                    \
            b = y2 + dB;                                                 \
            CLAMP(b);                                                    \
            mach64->overlay_dat[(x * 2) + 1] = (r << 16) | (g << 8) | b; \
        }                                                                \
    } while (0)

#define DECODE_YUV12_PACKED()                                            \
    do {                                                                 \
        for (x = 0; x < src_w; x += 1) {                                 \
            uint8_t y1, y2;                                              \
            int8_t  u, v;                                                \
            int     dR, dG, dB;                                          \
            int     r, g, b;                                             \
                                                                         \
            u  = uvsrc[3] - 0x80;                                        \
            y1 = src[0];                                                 \
            v  = uvsrc[2] - 0x80;                                        \
            y2 = src[1];                                                 \
            src += 4;                                                    \
            uvsrc += 4;                                                  \
                                                                         \
            dR = (359 * v) >> 8;                                         \
            dG = (88 * u + 183 * v) >> 8;                                \
            dB = (453 * u) >> 8;                                         \
                                                                         \
            r = y1 + dR;                                                 \
            CLAMP(r);                                                    \
            g = y1 - dG;                                                 \
            CLAMP(g);                                                    \
            b = y1 + dB;                                                 \
            CLAMP(b);                                                    \
            mach64->overlay_dat[x * 2] = (r << 16) | (g << 8) | b;       \
                                                                         \
            r = y2 + dR;                                                 \
            CLAMP(r);                                                    \
            g = y2 - dG;                                                 \
            CLAMP(g);                                                    \
            b = y2 + dB;                                                 \
            CLAMP(b);                                                    \
            mach64->overlay_dat[(x * 2) + 1] = (r << 16) | (g << 8) | b; \
        }                                                                \
    } while (0)

void
mach64_overlay_draw(svga_t *svga, int displine)
{
    mach64_t *mach64 = (mach64_t *) svga->priv;
    int       x;
    int       h_acc = 0;
    int       h_max = (mach64->scaler_height_width >> 16) & 0x3ff;
    int       src_w = h_max;
    int       h_inc = mach64->overlay_scale_inc >> 16;
    int       v_max = mach64->scaler_height_width & 0x3ff;
    int       v_inc = mach64->overlay_scale_inc & 0xffff;
    uint32_t *p;
    uint8_t  *src   = &svga->vram[svga->overlay.addr];
    uint8_t  *uvsrc = src;
    int       old_y = mach64->overlay_v_acc;
    int       y_diff;
    int       video_key_fn    = mach64->overlay_key_cntl & 5;
    int       graphics_key_fn = (mach64->overlay_key_cntl >> 4) & 5;
    int       overlay_cmp_mix = (mach64->overlay_key_cntl >> 8) & 0xf;
    int       gfx_src         = 0;
    int       desktop_x = mach64->svga.overlay_latch.x;
    int       desktop_y = displine - svga->y_add;

    p = &buffer32->line[displine][svga->x_add + mach64->svga.overlay_latch.x];

    if (mach64->overlay_cur_y >= 2) {
        /* Avoid corrupt UV data on YUV12 packed modes */
        uvsrc = &svga->vram[mach64->overlay_base + svga->overlay.pitch * 2 * (!(mach64->overlay_cur_y & 1) ? (mach64->overlay_cur_y + 1) : mach64->overlay_cur_y)];
    }

    if (mach64->scaler_update) {
        switch (mach64->scaler_format) {
            case 0x3:
                DECODE_ARGB1555();
                break;
            case 0x4:
                DECODE_RGB565();
                break;
            case 0x6:
                DECODE_ARGB8888();
                break;
            case 0xa:
                DECODE_YUV12_PACKED();
                break;
            case 0xb:
                DECODE_VYUY422();
                break;
            case 0xc:
                DECODE_YVYU422();
                break;
            default:
                pclog("Unknown Mach64 scaler format %x\n", mach64->scaler_format);
                /*Fill buffer with something recognisably wrong*/
                for (x = 0; x < mach64->svga.overlay_latch.cur_xsize; x++)
                    mach64->overlay_dat[x] = 0xff00ff;
                break;
        }
    }

    if (overlay_cmp_mix == 2) {
        for (x = 0; x < mach64->svga.overlay_latch.cur_xsize; x++) {
            int h = h_acc >> 12;

            p[x] = mach64->overlay_dat[h];

            h_acc += h_inc;
            if (h_acc > (h_max << 12))
                h_acc = (h_max << 12);
        }
    } else {
        for (x = 0; x < mach64->svga.overlay_latch.cur_xsize; x++) {
            int h         = h_acc >> 12;
            int gr_cmp    = 0;
            int vid_cmp   = 0;
            int use_video = 0;

            switch (video_key_fn) {
                case 0:
                    vid_cmp = 0;
                    break;
                case 1:
                    vid_cmp = 1;
                    break;
                case 4:
                    vid_cmp = ((mach64->overlay_dat[h] ^ mach64->overlay_video_key_clr) & mach64->overlay_video_key_msk);
                    break;
                case 5:
                    vid_cmp = !((mach64->overlay_dat[h] ^ mach64->overlay_video_key_clr) & mach64->overlay_video_key_msk);
                    break;
                default:
                    break;
            }
            switch (svga->bpp) {
                case 8:
                    gfx_src = svga->vram[desktop_y * (svga->rowoffset * 8) + (desktop_x + x) * 1 + svga->memaddr_latch * 4];
                    break;
                case 15:
                case 16:
                    gfx_src = *(uint16_t*)&svga->vram[desktop_y * (svga->rowoffset * 8) + (desktop_x + x) * 2 + svga->memaddr_latch * 4];
                    break;
                case 24:
                    gfx_src = svga->vram[desktop_y * (svga->rowoffset * 8) + (desktop_x + x) * 3 + svga->memaddr_latch * 4]
                            | (svga->vram[desktop_y * (svga->rowoffset * 8) + (desktop_x + x) * 3 + 1 + svga->memaddr_latch * 4] << 8)
                            | (svga->vram[desktop_y * (svga->rowoffset * 8) + (desktop_x + x) * 3 + 2 + svga->memaddr_latch * 4] << 16);
                    break;
                case 32:
                    gfx_src = *(uint32_t*)&svga->vram[desktop_y * (svga->rowoffset * 8) + (desktop_x + x) * 4 + svga->memaddr_latch * 4];
                    break;
            }
            switch (graphics_key_fn) {
                case 0:
                    gr_cmp = 0;
                    break;
                case 1:
                    gr_cmp = 1;
                    break;
                case 4:
                    gr_cmp = ((gfx_src ^ mach64->overlay_graphics_key_clr) & mach64->overlay_graphics_key_msk & 0xffffff);
                    break;
                case 5:
                    gr_cmp = !((gfx_src ^ mach64->overlay_graphics_key_clr) & mach64->overlay_graphics_key_msk & 0xffffff);
                    break;
                default:
                    break;
            }
            vid_cmp = vid_cmp ?-1 : 0;
            gr_cmp  = gr_cmp ? -1 : 0;

            switch (overlay_cmp_mix) {
                case 0x0:
                    use_video = gr_cmp;
                    break;
                case 0x1:
                    use_video = 0;
                    break;
                case 0x2:
                    use_video = ~0;
                    break;
                case 0x3:
                    use_video = ~gr_cmp;
                    break;
                case 0x4:
                    use_video = ~vid_cmp;
                    break;
                case 0x5:
                    use_video = gr_cmp ^ vid_cmp;
                    break;
                case 0x6:
                    use_video = ~gr_cmp ^ vid_cmp;
                    break;
                case 0x7:
                    use_video = vid_cmp;
                    break;
                case 0x8:
                    use_video = ~gr_cmp | ~vid_cmp;
                    break;
                case 0x9:
                    use_video = gr_cmp | ~vid_cmp;
                    break;
                case 0xa:
                    use_video = ~gr_cmp | vid_cmp;
                    break;
                case 0xb:
                    use_video = gr_cmp | vid_cmp;
                    break;
                case 0xc:
                    use_video = gr_cmp & vid_cmp;
                    break;
                case 0xd:
                    use_video = ~gr_cmp & vid_cmp;
                    break;
                case 0xe:
                    use_video = gr_cmp & ~vid_cmp;
                    break;
                case 0xf:
                    use_video = ~gr_cmp & ~vid_cmp;
                    break;
                default:
                    break;
            }

            if (use_video)
                p[x] = mach64->overlay_dat[h];

            h_acc += h_inc;
            if (h_acc > (h_max << 12))
                h_acc = (h_max << 12);
        }
    }

    mach64->overlay_v_acc += v_inc;
    if (mach64->overlay_v_acc > (v_max << 12))
        mach64->overlay_v_acc = v_max << 12;

    y_diff = (mach64->overlay_v_acc >> 12) - (old_y >> 12);

    if (mach64->scaler_format == 6)
        svga->overlay.addr += svga->overlay.pitch * 4 * y_diff;
    else
        svga->overlay.addr += svga->overlay.pitch * 2 * y_diff;

    mach64->scaler_update = y_diff;
    mach64->overlay_cur_y += y_diff;
}
