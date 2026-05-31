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
            WRITE8(addr ^ 2, mach64->dst_y_x, val);
            break;
        case 0x110 ... 0x111:
            WRITE8(addr + 2, mach64->dst_height_width, val);
            break;
        case 0x114 ... 0x115:
        case 0x118 ... 0x11b:
        case 0x11e ... 0x11f:
            WRITE8(addr, mach64->dst_height_width, val);
            fallthrough;
        case 0x113:
            if (((addr & 0x3ff) == 0x11b || (addr & 0x3ff) == 0x11f || (addr & 0x3ff) == 0x113) && !(val & 0x80)) {
start_blit_op:
                mach64_start_fill(mach64);
                mach64_log("%i %i %i %i %i %08x\n", (mach64->dst_height_width & 0x7ff), (mach64->dst_height_width & 0x7ff0000),
                           ((mach64->dp_src & 7) != SRC_HOST), (((mach64->dp_src >> 8) & 7) != SRC_HOST),
                           (((mach64->dp_src >> 16) & 3) != MONO_SRC_HOST), mach64->dp_src);
                if ((mach64->dst_height_width & 0x7ff) && (mach64->dst_height_width & 0x7ff0000) && ((mach64->dp_src & 7) != SRC_HOST) && (((mach64->dp_src >> 8) & 7) != SRC_HOST) && (((mach64->dp_src >> 16) & 3) != MONO_SRC_HOST))
                    mach64_blit(0, -1, mach64);
            }
            break;

        case 0x2ec ... 0x2ef:
            WRITE8(addr ^ 2, mach64->dst_height_width, val);
            if ((addr & 0x3ff) == 0x2ef) {
                goto start_blit_op;
            }
            break;
        case 0x120 ... 0x123:
            WRITE8(addr, mach64->dst_bres_lnth, val);
            if ((addr & 0x3ff) == 0x123 && !(val & 0x80)) {
                mach64_start_line(mach64);

                if ((mach64->dst_bres_lnth & 0x7fff) && ((mach64->dp_src & 7) != SRC_HOST) && (((mach64->dp_src >> 8) & 7) != SRC_HOST) && (((mach64->dp_src >> 16) & 3) != MONO_SRC_HOST))
                    mach64_blit(0, -1, mach64);
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
        case 0x184 ... 0x185:
            WRITE8(addr, mach64->src_y_x, val);
            break;
        case 0x188 ... 0x189:
            WRITE8(addr + 2, mach64->src_y_x, val);
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
            WRITE8(addr, mach64->src_y_x_start, val);
            break;
        case 0x1a0 ... 0x1a1:
            WRITE8(addr + 2, mach64->src_y_x_start, val);
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

    if (addr & 2
    && (addr >= 0x200 && addr <= 0x23e))
        mach64_blit(val, 16, mach64);
    else
    {
        switch (addr & 0x3fe) {
            case 0x2fc:
                mach64->dp_set_gui_engine |= (mach64->dp_set_gui_engine & 0xffff0000) | val;
                mach64_recalc_dp_set_engine(mach64);
                break;
            case 0x2fe:
                mach64->dp_set_gui_engine |= (mach64->dp_set_gui_engine & 0xffff) | (val << 16);
                mach64_recalc_dp_set_engine(mach64);
                break;
            case 0x32c:
                mach64->context_load_cntl = (mach64->context_load_cntl & 0xffff0000) | val;
                break;
            case 0x32e:
                mach64->context_load_cntl = (mach64->context_load_cntl & 0x0000ffff) | (val << 16);
                if (val & 0x30000)
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
    {
        if (mach64->accel.source_host || (mach64->dp_pix_width & DP_BYTE_PIX_ORDER))
            mach64_blit(val, 32, mach64);
        else
            mach64_blit(((val & 0xff000000) >> 24) | ((val & 0x00ff0000) >> 8) | ((val & 0x0000ff00) << 8) | ((val & 0x000000ff) << 24), 32, mach64);
    }
    else
    {
        switch (addr & 0x3fc) {
            case 0x32c:
                mach64->context_load_cntl = val;
                if (val & 0x30000)
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

void
mach64_wait_fifo_idle(mach64_t *mach64)
{
    while (!FIFO_EMPTY) {
        mach64_wake_fifo_thread(mach64);
        thread_wait_event(mach64->fifo_not_full_event, 1);
    }
}

void
mach64_fifo_thread(void *param)
{
    mach64_t *mach64 = (mach64_t *) param;

    while (mach64->thread_run) {
        thread_set_event(mach64->fifo_not_full_event);
        thread_wait_event(mach64->wake_fifo_thread, -1);
        thread_reset_event(mach64->wake_fifo_thread);
        mach64->blitter_busy = 1;
        while (!FIFO_EMPTY) {
            uint64_t      start_time = plat_timer_read();
            uint64_t      end_time;
            fifo_entry_t *fifo = &mach64->fifo[mach64->fifo_read_idx & FIFO_MASK];
            uint32_t      val  = fifo->val;

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

            mach64->fifo_read_idx++;
            fifo->addr_type = FIFO_INVALID;

            if (FIFO_ENTRIES > 0xe000)
                thread_set_event(mach64->fifo_not_full_event);

            end_time = plat_timer_read();
            mach64->blitter_time += end_time - start_time;
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

    // Before me, there was some code that checked if the address was 0x11b (if a byte), 0x11a (if a word), or 0x118 (if a dword), and only fired the FIFO thread
    // if there were 16 or more entries in the FIFO. It was introduced in a commit on 8/21/2024 with no discussion that I can find even related to it. 
    // It didn't break anything to remove it and I can't think of any design reason for it to exist.

    if (FIFO_FULL) {
        thread_reset_event(mach64->fifo_not_full_event);
        if (FIFO_FULL)
            thread_wait_event(mach64->fifo_not_full_event, -1); /*Wait for room in ringbuffer*/
    }

    fifo->val       = val;
    fifo->addr_type = (addr & FIFO_ADDR) | type;

    mach64->fifo_write_idx++;

    if (FIFO_ENTRIES > 0xe000)
        mach64_wake_fifo_thread(mach64);
    if (FIFO_ENTRIES > 0xe000 || FIFO_ENTRIES < 8)
        mach64_wake_fifo_thread(mach64);
}

#define READ(addr, dat, width)                                                         \
    if (width == 0)                                                                    \
        dat = svga->vram[((addr)) & mach64->vram_mask];                                \
    else if (width == 1)                                                               \
        dat = *(uint16_t *) &svga->vram[((addr) << 1) & mach64->vram_mask];            \
    else if (width == 2)                                                               \
        dat = *(uint32_t *) &svga->vram[((addr) << 2) & mach64->vram_mask];            \
    else if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)                                 \
        dat = (svga->vram[((addr) >> 3) & mach64->vram_mask] >> ((addr) &7)) & 1;      \
    else                                                                               \
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
            dest_dat = (dest_dat + src_dat) >> 1;                \
            break;                                               \
    }

#define WRITE(addr, width)                                                                                  \
    if (width == 0) {                                                                                       \
        svga->vram[(addr) &mach64->vram_mask]                = dest_dat;                                    \
        svga->changedvram[((addr) &mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount;         \
    } else if (width == 1) {                                                                                \
        *(uint16_t *) &svga->vram[((addr) << 1) & mach64->vram_mask] = dest_dat;                            \
        svga->changedvram[(((addr) << 1) & mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount; \
    } else if (width == 2) {                                                                                \
        *(uint32_t *) &svga->vram[((addr) << 2) & mach64->vram_mask] = dest_dat;                            \
        svga->changedvram[(((addr) << 2) & mach64->vram_mask) >> 12] = svga->monitor->mon_changeframecount; \
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

void
mach64_start_fill(mach64_t *mach64)
{
    mach64->accel.dst_pix_width  = mach64->dp_pix_width & 7;
    mach64->accel.src_pix_width  = (mach64->dp_pix_width >> 8) & 7;
    mach64->accel.host_pix_width = (mach64->dp_pix_width >> 16) & 7;

    mach64->accel.dst_size  = mach64_width[mach64->accel.dst_pix_width];
    mach64->accel.src_size  = mach64_width[mach64->accel.src_pix_width];
    mach64->accel.host_size = mach64_width[mach64->accel.host_pix_width];

    mach64->accel.dst_x = 0;
    mach64->accel.dst_y = 0;

    mach64->accel.dst_x_start = (mach64->dst_y_x >> 16) & 0xfff;
    if ((mach64->dst_y_x >> 16) & 0x1000)
        mach64->accel.dst_x_start |= ~0xfff;
    mach64->accel.dst_y_start = mach64->dst_y_x & 0x3fff;
    if (mach64->dst_y_x & 0x4000)
        mach64->accel.dst_y_start |= ~0x3fff;

    mach64->accel.dst_width  = (mach64->dst_height_width >> 16) & 0x1fff;
    mach64->accel.dst_height = mach64->dst_height_width & 0x1fff;

    if ((((mach64->dp_src >> 16) & 7) == MONO_SRC_BLITSRC) &&
        ((mach64->src_cntl & (SRC_LINEAR_EN | SRC_BYTE_ALIGN)) == (SRC_LINEAR_EN | SRC_BYTE_ALIGN))) {
        if (mach64->accel.dst_width & 7)
            mach64->accel.dst_width = (mach64->accel.dst_width & ~7) + 8;
    }

    mach64->accel.x_count  = mach64->accel.dst_width;
    mach64->accel.xx_count = 0;

    mach64->accel.src_x = 0;
    mach64->accel.src_y = 0;

    mach64->accel.src_x_start = (mach64->src_y_x >> 16) & 0xfff;
    if ((mach64->src_y_x >> 16) & 0x1000)
        mach64->accel.src_x_start |= ~0xfff;
    mach64->accel.src_y_start = mach64->src_y_x & 0x3fff;
    if (mach64->src_y_x & 0x4000)
        mach64->accel.src_y_start |= ~0x3fff;

    if (mach64->src_cntl & SRC_LINEAR_EN)
        mach64->accel.src_x_count = 0x7ffffff; /*Essentially infinite*/
    else
        mach64->accel.src_x_count = (mach64->src_height1_width1 >> 16) & 0x7fff;
    if (!(mach64->src_cntl & SRC_PATT_EN))
        mach64->accel.src_y_count = 0x7ffffff; /*Essentially infinite*/
    else
        mach64->accel.src_y_count = mach64->src_height1_width1 & 0x1fff;

    mach64->accel.src_width1  = (mach64->src_height1_width1 >> 16) & 0x7fff;
    mach64->accel.src_height1 = mach64->src_height1_width1 & 0x1fff;
    mach64->accel.src_width2  = (mach64->src_height2_width2 >> 16) & 0x7fff;
    mach64->accel.src_height2 = mach64->src_height2_width2 & 0x1fff;

    mach64_log("src %i %i  %i %i  %08X %08X\n", mach64->accel.src_x_count,
               mach64->accel.src_y_count,
               mach64->accel.src_width1,
               mach64->accel.src_height1,
               mach64->src_height1_width1,
               mach64->src_height2_width2);

    mach64->accel.src_pitch  = (mach64->src_off_pitch >> 22) << 3;
    mach64->accel.src_offset = (mach64->src_off_pitch & 0xfffff) << 3;

    mach64->accel.dst_pitch  = (mach64->dst_off_pitch >> 22) << 3;
    mach64->accel.dst_offset = (mach64->dst_off_pitch & 0xfffff) << 3;

    mach64->accel.mix_fg = (mach64->dp_mix >> 16) & 0x1f;
    mach64->accel.mix_bg = mach64->dp_mix & 0x1f;

    mach64->accel.source_bg  = mach64->dp_src & 7;
    mach64->accel.source_fg  = (mach64->dp_src >> 8) & 7;
    mach64->accel.source_mix = (mach64->dp_src >> 16) & 7;

    if (mach64->accel.src_size == WIDTH_1BIT)
        mach64->accel.src_offset <<= 3;
    else
        mach64->accel.src_offset >>= mach64->accel.src_size;

    if (mach64->accel.dst_size == WIDTH_1BIT)
        mach64->accel.dst_offset <<= 3;
    else
        mach64->accel.dst_offset >>= mach64->accel.dst_size;

    mach64->accel.xinc = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
    mach64->accel.yinc = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;

    mach64->accel.source_host = ((mach64->dp_src & 7) == SRC_HOST) || (((mach64->dp_src >> 8) & 7) == SRC_HOST);

    if (mach64->pat_cntl & 1) {
        for (uint8_t y = 0; y < 8; y++) {
            for (uint8_t x = 0; x < 8; x++) {
                uint32_t temp                   = (y & 4) ? mach64->pat_reg1 : mach64->pat_reg0;
                mach64->accel.pattern[y][7 - x] = (temp >> (x + ((y & 3) << 3))) & 1;
            }
        }
    }

    if (mach64->pat_cntl & 2) {
        mach64->accel.pattern_clr4x2[0][0] = (mach64->pat_reg0 & 0xff);
        mach64->accel.pattern_clr4x2[0][1] = ((mach64->pat_reg0 >> 8) & 0xff);
        mach64->accel.pattern_clr4x2[0][2] = ((mach64->pat_reg0 >> 16) & 0xff);
        mach64->accel.pattern_clr4x2[0][3] = ((mach64->pat_reg0 >> 24) & 0xff);
        mach64->accel.pattern_clr4x2[1][0] = (mach64->pat_reg1 & 0xff);
        mach64->accel.pattern_clr4x2[1][1] = ((mach64->pat_reg1 >> 8) & 0xff);
        mach64->accel.pattern_clr4x2[1][2] = ((mach64->pat_reg1 >> 16) & 0xff);
        mach64->accel.pattern_clr4x2[1][3] = ((mach64->pat_reg1 >> 24) & 0xff);
    }

    if (mach64->pat_cntl & 4) {
        mach64->accel.pattern_clr8x1[0] = (mach64->pat_reg0 & 0xff);
        mach64->accel.pattern_clr8x1[1] = ((mach64->pat_reg0 >> 8) & 0xff);
        mach64->accel.pattern_clr8x1[2] = ((mach64->pat_reg0 >> 16) & 0xff);
        mach64->accel.pattern_clr8x1[3] = ((mach64->pat_reg0 >> 24) & 0xff);
        mach64->accel.pattern_clr8x1[4] = (mach64->pat_reg1 & 0xff);
        mach64->accel.pattern_clr8x1[5] = ((mach64->pat_reg1 >> 8) & 0xff);
        mach64->accel.pattern_clr8x1[6] = ((mach64->pat_reg1 >> 16) & 0xff);
        mach64->accel.pattern_clr8x1[7] = ((mach64->pat_reg1 >> 24) & 0xff);
    }

    if ((mach64->src_cntl & SRC_8x8x8_BRUSH) && !(mach64->src_cntl & SRC_8x8x8_BRUSH_LOADED))
    {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                mach64->accel.pattern_clr8x8[y][x] = mach64->svga.vram[(mach64->accel.src_offset & ~7) + (y * 8) + x];
    }

    mach64->accel.sc_left   = mach64->sc_left_right & 0x1fff;
    mach64->accel.sc_right  = (mach64->sc_left_right >> 16) & 0x1fff;
    mach64->accel.sc_top    = mach64->sc_top_bottom & 0x7fff;
    mach64->accel.sc_bottom = (mach64->sc_top_bottom >> 16) & 0x7fff;

    mach64->accel.dp_frgd_clr = mach64->dp_frgd_clr;
    mach64->accel.dp_bkgd_clr = mach64->dp_bkgd_clr;
    mach64->accel.write_mask  = mach64->write_mask;

    mach64->accel.clr_cmp_clr  = mach64->clr_cmp_clr & mach64->clr_cmp_mask;
    mach64->accel.clr_cmp_mask = mach64->clr_cmp_mask;
    mach64->accel.clr_cmp_fn   = mach64->clr_cmp_cntl & 7;
    mach64->accel.clr_cmp_src  = mach64->clr_cmp_cntl & (1 << 24);

    mach64->accel.poly_draw = 0;

    mach64->accel.busy = 1;

    mach64->accel.op = OP_RECT;
}

void
mach64_start_line(mach64_t *mach64)
{
    mach64->accel.dst_x = (mach64->dst_y_x >> 16) & 0xfff;
    if ((mach64->dst_y_x >> 16) & 0x1000)
        mach64->accel.dst_x |= ~0xfff;
    mach64->accel.dst_y = mach64->dst_y_x & 0x3fff;
    if (mach64->dst_y_x & 0x4000)
        mach64->accel.dst_y |= ~0x3fff;

    mach64->accel.src_x = (mach64->src_y_x >> 16) & 0xfff;
    if ((mach64->src_y_x >> 16) & 0x1000)
        mach64->accel.src_x |= ~0xfff;
    mach64->accel.src_y = mach64->src_y_x & 0x3fff;
    if (mach64->src_y_x & 0x4000)
        mach64->accel.src_y |= ~0x3fff;

    mach64->accel.src_pitch  = (mach64->src_off_pitch >> 22) << 3;
    mach64->accel.src_offset = (mach64->src_off_pitch & 0xfffff) << 3;

    mach64->accel.dst_pitch  = (mach64->dst_off_pitch >> 22) << 3;
    mach64->accel.dst_offset = (mach64->dst_off_pitch & 0xfffff) << 3;

    mach64->accel.mix_fg = (mach64->dp_mix >> 16) & 0x1f;
    mach64->accel.mix_bg = mach64->dp_mix & 0x1f;

    mach64->accel.source_bg  = mach64->dp_src & 7;
    mach64->accel.source_fg  = (mach64->dp_src >> 8) & 7;
    mach64->accel.source_mix = (mach64->dp_src >> 16) & 7;

    mach64->accel.dst_pix_width  = mach64->dp_pix_width & 7;
    mach64->accel.src_pix_width  = (mach64->dp_pix_width >> 8) & 7;
    mach64->accel.host_pix_width = (mach64->dp_pix_width >> 16) & 7;

    mach64->accel.dst_size  = mach64_width[mach64->accel.dst_pix_width];
    mach64->accel.src_size  = mach64_width[mach64->accel.src_pix_width];
    mach64->accel.host_size = mach64_width[mach64->accel.host_pix_width];

    if (mach64->accel.src_size == WIDTH_1BIT)
        mach64->accel.src_offset <<= 3;
    else
        mach64->accel.src_offset >>= mach64->accel.src_size;

    if (mach64->accel.dst_size == WIDTH_1BIT)
        mach64->accel.dst_offset <<= 3;
    else
        mach64->accel.dst_offset >>= mach64->accel.dst_size;

    mach64->accel.source_host = ((mach64->dp_src & 7) == SRC_HOST) || (((mach64->dp_src >> 8) & 7) == SRC_HOST);

    if (mach64->pat_cntl & 1) {
        for (uint8_t y = 0; y < 8; y++) {
            for (uint8_t x = 0; x < 8; x++) {
                uint32_t temp                   = (y & 4) ? mach64->pat_reg1 : mach64->pat_reg0;
                mach64->accel.pattern[y][7 - x] = (temp >> (x + ((y & 3) << 3))) & 1;
            }
        }
    }
    mach64->accel.sc_left   = mach64->sc_left_right & 0x1fff;
    mach64->accel.sc_right  = (mach64->sc_left_right >> 16) & 0x1fff;
    mach64->accel.sc_top    = mach64->sc_top_bottom & 0x7fff;
    mach64->accel.sc_bottom = (mach64->sc_top_bottom >> 16) & 0x7fff;

    mach64->accel.dp_frgd_clr = mach64->dp_frgd_clr;
    mach64->accel.dp_bkgd_clr = mach64->dp_bkgd_clr;
    mach64->accel.write_mask  = mach64->write_mask;

    mach64->accel.x_count = mach64->dst_bres_lnth & 0x7fff;
    mach64->accel.err     = (mach64->dst_bres_err & 0x3ffff) | ((mach64->dst_bres_err & 0x40000) ? 0xfffc0000 : 0);

    mach64->accel.clr_cmp_clr  = mach64->clr_cmp_clr & mach64->clr_cmp_mask;
    mach64->accel.clr_cmp_mask = mach64->clr_cmp_mask;
    mach64->accel.clr_cmp_fn   = mach64->clr_cmp_cntl & 7;
    mach64->accel.clr_cmp_src  = mach64->clr_cmp_cntl & (1 << 24);

    mach64->accel.xinc = (mach64->dst_cntl & DST_X_DIR) ? 1 : -1;
    mach64->accel.yinc = (mach64->dst_cntl & DST_Y_DIR) ? 1 : -1;

    mach64->accel.busy = 1;
    mach64_log("mach64_start_line\n");

    mach64->accel.op = OP_LINE;
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

void
mach64_blit_rect(uint32_t cpu_dat, int count, mach64_t* mach64)
{
    svga_t *svga    = &mach64->svga;
    int     cmp_clr = 0;
    int     mix = 0;

    while (count) {
        uint8_t  write_mask = 0;
        uint32_t src_dat = 0;
        uint32_t dest_dat;
        uint32_t host_dat = 0;
        uint32_t old_dest_dat;
        int      dst_x, dst_y;
        int      src_x, src_y;

        dst_x = (mach64->accel.dst_x + mach64->accel.dst_x_start) & 0xfff;
        dst_y = (mach64->accel.dst_y + mach64->accel.dst_y_start) & 0x3fff;

        if (mach64->src_cntl & SRC_LINEAR_EN)
            src_x = mach64->accel.src_x;
        else
            src_x = (mach64->accel.src_x + mach64->accel.src_x_start) & 0xfff;

        src_y = (mach64->accel.src_y + mach64->accel.src_y_start) & 0x3fff;

        if (mach64->accel.source_host) {
            host_dat = cpu_dat;

            if (mach64->accel.host_size < 2)
                cpu_dat >>= (8 << mach64->accel.host_size); // shift by 8 for a word, 16 for a word, not at all for a dword.

            count -= (8 << mach64->accel.host_size);
        } else
            count--;

        switch (mach64->accel.source_mix) {
            case MONO_SRC_HOST:
                if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER) {
                    mix = cpu_dat & 1;
                    cpu_dat >>= 1;
                } else {
                    mix = cpu_dat >> 0x1f;
                    cpu_dat <<= 1;
                }
                break;
            case MONO_SRC_PAT:
                if (mach64->dst_cntl & DST_24_ROT_EN) {
                    if (!mach64->accel.xx_count)
                        mix = mach64->accel.pattern[dst_y & 7][(dst_x / 3) & 7];
                } else
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

        if (dst_x >= mach64->accel.sc_left && dst_x <= mach64->accel.sc_right && dst_y >= mach64->accel.sc_top && dst_y <= mach64->accel.sc_bottom) {
            switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg) {
                case SRC_HOST:
                    src_dat = host_dat;
                    break;
                case SRC_BLITSRC:
                    if (mach64->accel.src_size == 0 && mach64->type == MACH64_VT3 && (mach64->src_cntl & SRC_8x8x8_BRUSH))
                        src_dat = mach64->accel.pattern_clr8x8[dst_y & 7][dst_x & 7];
                    else
                        READ(mach64->accel.src_offset + (src_y * mach64->accel.src_pitch) + src_x, src_dat, mach64->accel.src_size);
                    break;
                case SRC_FG:
                    if (mach64->dst_cntl & DST_24_ROT_EN) {
                        if (mach64->accel.xinc == -1) {
                            if (mach64->accel.xx_count == 2)
                                src_dat = mach64->accel.dp_frgd_clr & 0xff;
                            else if (mach64->accel.xx_count == 1)
                                src_dat = (mach64->accel.dp_frgd_clr >> 8) & 0xff;
                            else
                                src_dat = (mach64->accel.dp_frgd_clr >> 16) & 0xff;
                        } else {
                            if (mach64->accel.xx_count == 2)
                                src_dat = (mach64->accel.dp_frgd_clr >> 16) & 0xff;
                            else if (mach64->accel.xx_count == 1)
                                src_dat = (mach64->accel.dp_frgd_clr >> 8) & 0xff;
                            else
                                src_dat = mach64->accel.dp_frgd_clr & 0xff;
                        }
                    } else
                        src_dat = mach64->accel.dp_frgd_clr;
                    break;
                case SRC_BG:
                    if (mach64->dst_cntl & DST_24_ROT_EN) {
                        if (mach64->accel.xinc == -1) {
                            if (mach64->accel.xx_count == 2)
                                src_dat = mach64->accel.dp_bkgd_clr & 0xff;
                            else if (mach64->accel.xx_count == 1)
                                src_dat = (mach64->accel.dp_bkgd_clr >> 8) & 0xff;
                            else
                                src_dat = (mach64->accel.dp_bkgd_clr >> 16) & 0xff;
                        } else {
                            if (mach64->accel.xx_count == 2)
                                src_dat = (mach64->accel.dp_bkgd_clr >> 16) & 0xff;
                            else if (mach64->accel.xx_count == 1)
                                src_dat = (mach64->accel.dp_bkgd_clr >> 8) & 0xff;
                            else
                                src_dat = mach64->accel.dp_bkgd_clr & 0xff;
                        }
                    } else
                        src_dat = mach64->accel.dp_bkgd_clr;
                    break;
                case SRC_PAT:
                    if (mach64->pat_cntl & 2) {
                        src_dat = mach64->accel.pattern_clr4x2[dst_y & 1][dst_x & 3];
                        break;
                    } else if (mach64->pat_cntl & 4) {
                        src_dat = mach64->accel.pattern_clr8x1[dst_x & 7];
                        break;
                    }

                default:
                    src_dat = 0;
                    break;
            }

            if (mach64->dst_cntl & DST_POLYGON_EN) {
                int poly_src;
                READ(mach64->accel.src_offset + (src_y * mach64->accel.src_pitch) + src_x, poly_src, mach64->accel.src_size);
                if (poly_src)
                    mach64->accel.poly_draw = !mach64->accel.poly_draw;
            }

            if (!(mach64->dst_cntl & DST_POLYGON_EN) || mach64->accel.poly_draw) {
                READ(mach64->accel.dst_offset + ((dst_y) *mach64->accel.dst_pitch) + (dst_x), dest_dat, mach64->accel.dst_size);

                cmp_clr = mach64_blit_calc_cmp_clr(mach64, src_dat, dest_dat);

                if (!cmp_clr) {
                    old_dest_dat = dest_dat;
                    MIX
                    if (mach64->dst_cntl & DST_24_ROT_EN) {
                        if (mach64->accel.xinc == -1) {
                            if (mach64->accel.xx_count == 2)
                                write_mask = mach64->accel.write_mask & 0xff;
                            else if (mach64->accel.xx_count == 1)
                                write_mask = (mach64->accel.write_mask >> 8) & 0xff;
                            else
                                write_mask = (mach64->accel.write_mask >> 16) & 0xff;
                        } else {
                            if (mach64->accel.xx_count == 2)
                                write_mask = (mach64->accel.write_mask >> 16) & 0xff;
                            else if (mach64->accel.xx_count == 1)
                                write_mask = (mach64->accel.write_mask >> 8) & 0xff;
                            else
                                write_mask = mach64->accel.write_mask & 0xff;
                        }
                        dest_dat = (dest_dat & write_mask) | (old_dest_dat & ~write_mask);
                    } else
                        dest_dat = (dest_dat & mach64->accel.write_mask) | (old_dest_dat & ~mach64->accel.write_mask);
                }

                WRITE(mach64->accel.dst_offset + ((dst_y) * mach64->accel.dst_pitch) + (dst_x), mach64->accel.dst_size);
            }
        }

        mach64->accel.src_x += mach64->accel.xinc;
        mach64->accel.dst_x += mach64->accel.xinc;
        if (!(mach64->src_cntl & SRC_LINEAR_EN)) {
            mach64->accel.src_x_count--;
            if (mach64->accel.src_x_count <= 0) {
                mach64->accel.src_x = 0;
                if ((mach64->src_cntl & (SRC_PATT_ROT_EN | SRC_PATT_EN)) == (SRC_PATT_ROT_EN | SRC_PATT_EN)) {
                    mach64->accel.src_x_start = (mach64->src_y_x_start >> 16) & 0xfff;
                    if ((mach64->src_y_x_start >> 16) & 0x1000)
                        mach64->accel.src_x_start |= ~0xfff;
                    mach64->accel.src_x_count = mach64->accel.src_width2;
                } else
                    mach64->accel.src_x_count = mach64->accel.src_width1;
            }
        }

        mach64->accel.x_count--;
        mach64->accel.xx_count = (mach64->accel.xx_count + 1) % 3;
        if (mach64->accel.x_count <= 0) {
            mach64->accel.x_count  = mach64->accel.dst_width;
            mach64->accel.xx_count = 0;
            mach64->accel.dst_x    = 0;
            mach64->accel.dst_y += mach64->accel.yinc;
            mach64->accel.src_x_start = (mach64->src_y_x >> 16) & 0xfff;
            mach64->accel.src_x_count = mach64->accel.src_width1;

            if (!(mach64->src_cntl & SRC_LINEAR_EN)) {
                mach64->accel.src_x = 0;
                mach64->accel.src_y += mach64->accel.yinc;
                mach64->accel.src_y_count--;
                if (mach64->accel.src_y_count <= 0) {
                    mach64->accel.src_y = 0;
                    if ((mach64->src_cntl & (SRC_PATT_ROT_EN | SRC_PATT_EN)) == (SRC_PATT_ROT_EN | SRC_PATT_EN)) {
                        mach64->accel.src_y_start = mach64->src_y_x_start & 0x3fff;
                        if (mach64->src_y_x_start & 0x4000)
                            mach64->accel.src_y_start |= ~0x3fff;
                        mach64->accel.src_y_count = mach64->accel.src_height2;
                    } else
                        mach64->accel.src_y_count = mach64->accel.src_height1;
                }
            }

            mach64->accel.poly_draw = 0;
            mach64->accel.dst_height--;
            if (mach64->accel.dst_height <= 0) {
                /*Blit finished*/
                mach64_log("mach64 blit finished\n");
                mach64->accel.busy = 0;
                if (mach64->dst_cntl & DST_X_TILE)
                    mach64->dst_y_x = (mach64->dst_y_x & 0xfff) | ((mach64->dst_y_x + (mach64->accel.dst_width << 16)) & 0xfff0000);
                if (mach64->dst_cntl & DST_Y_TILE)
                    mach64->dst_y_x = (mach64->dst_y_x & 0xfff0000) | ((mach64->dst_y_x + (mach64->dst_height_width & 0x1fff)) & 0xfff);
                return;
            }
            if (mach64->host_cntl & HOST_BYTE_ALIGN) {
                if (mach64->accel.source_mix == MONO_SRC_HOST) {
                    if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER)
                        cpu_dat >>= (count & 7);
                    else
                        cpu_dat <<= (count & 7);

                    count &= ~7;
                }
            }
        }
    }
}

void
mach64_blit_line(uint32_t cpu_dat, int count, mach64_t* mach64)
{
    svga_t *svga    = &mach64->svga;
    int     cmp_clr = 0;

    if (((mach64->crtc_gen_cntl >> 8) & 7) == BPP_24) {
        int x = 0;
        while (count) {
            uint32_t src_dat = 0;
            uint32_t dest_dat;
            uint32_t host_dat = 0;
            int      mix      = 0;
            int      draw_pixel = !(mach64->dst_cntl & DST_POLYGON_EN);

            if (mach64->dst_cntl & DST_POLYGON_EN) {
                if (mach64->dst_cntl & DST_Y_MAJOR)
                    draw_pixel = 1;
                else if (mach64->accel.err >= 0)
                    draw_pixel = 1;
            }

            if (mach64->accel.source_host) {
                host_dat = cpu_dat;

                if (mach64->accel.host_size < 2)
                    cpu_dat >>= (8 << mach64->accel.host_size); // shift by 8 for a word, 16 for a word, not at all for a dword.

                count -= (8 << mach64->accel.host_size);
            } else
                count--;

            switch (mach64->accel.source_mix) {
                case MONO_SRC_HOST:
                    if (mach64->dp_pix_width & DP_BYTE_PIX_ORDER) {
                        mix = cpu_dat & 1;
                        cpu_dat >>= 1;
                    } else {
                        mix = cpu_dat >> 31;
                        cpu_dat <<= 1;
                    }
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

            if ((mach64->accel.dst_x >= mach64->accel.sc_left) && (mach64->accel.dst_x <= mach64->accel.sc_right) && (mach64->accel.dst_y >= mach64->accel.sc_top) && (mach64->accel.dst_y <= mach64->accel.sc_bottom) && draw_pixel) {
                switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg) {
                    case SRC_HOST:
                        src_dat = host_dat;
                        break;
                    case SRC_BLITSRC:
                        if (mach64->accel.src_size == 0 && mach64->type == MACH64_VT3 && (mach64->src_cntl & SRC_8x8x8_BRUSH))
                            src_dat = mach64->accel.pattern_clr8x8[mach64->accel.dst_y & 7][mach64->accel.dst_x & 7];
                        else
                            READ(mach64->accel.src_offset + (mach64->accel.src_y * mach64->accel.src_pitch) + mach64->accel.src_x, src_dat, mach64->accel.src_size);
                        break;
                    case SRC_FG:
                        src_dat = mach64->accel.dp_frgd_clr;
                        break;
                    case SRC_BG:
                        src_dat = mach64->accel.dp_bkgd_clr;
                        break;
                    case SRC_PAT:
                        if (mach64->pat_cntl & 2) {
                            src_dat = mach64->accel.pattern_clr4x2[mach64->accel.dst_y & 1][mach64->accel.dst_x & 3];
                            break;
                        } else if (mach64->pat_cntl & 4) {
                            src_dat = mach64->accel.pattern_clr8x1[mach64->accel.dst_x & 7];
                            break;
                        }
                    default:
                        src_dat = 0;
                        break;
                }

                READ(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, dest_dat, mach64->accel.dst_size);

                cmp_clr = mach64_blit_calc_cmp_clr(mach64, src_dat, dest_dat);

                if (!cmp_clr)
                    MIX

                if (!(mach64->dst_cntl & DST_Y_MAJOR)) {
                    if (!x)
                        dest_dat &= ~1;
                } else {
                    if (x == (mach64->accel.x_count - 1))
                        dest_dat &= ~1;
                }

                WRITE(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, mach64->accel.dst_size);
            }

            x++;
            if (x >= mach64->accel.x_count) {
                mach64->accel.busy = 0;
                mach64_log("mach64 line24 finished\n");
                return;
            }

            if (mach64->dst_cntl & DST_Y_MAJOR) {
                mach64->accel.dst_y += mach64->accel.yinc;
                mach64->accel.src_y += mach64->accel.yinc;
                if (mach64->accel.err >= 0) {
                    mach64->accel.err += mach64->dst_bres_dec;
                    mach64->accel.dst_x += mach64->accel.xinc;
                    mach64->accel.src_x += mach64->accel.xinc;
                } else {
                    mach64->accel.err += mach64->dst_bres_inc;
                }
            } else {
                mach64->accel.dst_x += mach64->accel.xinc;
                mach64->accel.src_x += mach64->accel.xinc;
                if (mach64->accel.err >= 0) {
                    mach64->accel.err += mach64->dst_bres_dec;
                    mach64->accel.dst_y += mach64->accel.yinc;
                    mach64->accel.src_y += mach64->accel.yinc;
                } else {
                    mach64->accel.err += mach64->dst_bres_inc;
                }
            }
        }
    } else {
        while (count) {
            uint32_t src_dat = 0;
            uint32_t dest_dat;
            uint32_t host_dat   = 0;
            int      mix        = 0;
            int      draw_pixel = !(mach64->dst_cntl & DST_POLYGON_EN);

            if (mach64->accel.source_host) {
                host_dat = cpu_dat;
                if (mach64->accel.host_size < 2)
                    cpu_dat >>= (8 << mach64->accel.host_size); // shift by 8 for a word, 16 for a word, not at all for a dword.

                count -= (8 << mach64->accel.host_size);
            } else
                count--;

            switch (mach64->accel.source_mix) {
                case MONO_SRC_HOST:
                    mix = cpu_dat >> 31;
                    cpu_dat <<= 1;
                    break;
                case MONO_SRC_PAT:
                    mix = mach64->accel.pattern[mach64->accel.dst_y & 7][mach64->accel.dst_x & 7];
                    break;
                case MONO_SRC_1:
                default:
                    mix = 1;
                    break;
            }

            if (mach64->dst_cntl & DST_POLYGON_EN) {
                if (mach64->dst_cntl & DST_Y_MAJOR)
                    draw_pixel = 1;
                else if (mach64->accel.err >= 0)
                    draw_pixel = 1;
            }

            if (mach64->accel.x_count == 1 && !(mach64->dst_cntl & DST_LAST_PEL))
                draw_pixel = 0;

            if (mach64->accel.dst_x >= mach64->accel.sc_left && mach64->accel.dst_x <= mach64->accel.sc_right && mach64->accel.dst_y >= mach64->accel.sc_top && mach64->accel.dst_y <= mach64->accel.sc_bottom && draw_pixel) {
                switch (mix ? mach64->accel.source_fg : mach64->accel.source_bg) {
                    case SRC_HOST:
                        src_dat = host_dat;
                        break;
                    case SRC_BLITSRC:
                        READ(mach64->accel.src_offset + (mach64->accel.src_y * mach64->accel.src_pitch) + mach64->accel.src_x, src_dat, mach64->accel.src_size);
                        break;
                    case SRC_FG:
                        src_dat = mach64->accel.dp_frgd_clr;
                        break;
                    case SRC_BG:
                        src_dat = mach64->accel.dp_bkgd_clr;
                        break;
                    default:
                        src_dat = 0;
                        break;
                }

                READ(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, dest_dat, mach64->accel.dst_size);

                cmp_clr = mach64_blit_calc_cmp_clr(mach64, src_dat, dest_dat);

                if (!cmp_clr)
                    MIX

                WRITE(mach64->accel.dst_offset + (mach64->accel.dst_y * mach64->accel.dst_pitch) + mach64->accel.dst_x, mach64->accel.dst_size);
            }

            mach64->accel.x_count--;
            if (mach64->accel.x_count <= 0) {
                /*Blit finished*/
                mach64_log("mach64 blit finished\n");
                mach64->accel.busy = 0;
                return;
            }

            if (mach64->dst_cntl & DST_Y_MAJOR) {
                mach64->accel.dst_y += mach64->accel.yinc;
                mach64->accel.src_y += mach64->accel.yinc;
                if (mach64->accel.err >= 0) {
                    mach64->accel.err += mach64->dst_bres_dec;
                    mach64->accel.dst_x += mach64->accel.xinc;
                    mach64->accel.src_x += mach64->accel.xinc;
                } else {
                    mach64->accel.err += mach64->dst_bres_inc;
                }
            } else {
                mach64->accel.dst_x += mach64->accel.xinc;
                mach64->accel.src_x += mach64->accel.xinc;
                if (mach64->accel.err >= 0) {
                    mach64->accel.err += mach64->dst_bres_dec;
                    mach64->accel.dst_y += mach64->accel.yinc;
                    mach64->accel.src_y += mach64->accel.yinc;
                } else {
                    mach64->accel.err += mach64->dst_bres_inc;
                }
            }
        }
    }
}

void
mach64_blit(uint32_t cpu_dat, int count, mach64_t *mach64)
{
    if (!mach64->accel.busy) {
        mach64_log("mach64_blit : return as not busy\n");
        return;
    }

    switch (mach64->accel.op) {
        case OP_RECT:
            mach64_blit_rect(cpu_dat, count, mach64);
            break;
        case OP_LINE:
            mach64_blit_line(cpu_dat, count, mach64);
            break;
        default:
            break;
    }
}

void
mach64_load_context(mach64_t *mach64)
{
    svga_t  *svga = &mach64->svga;
    uint32_t addr;

    while (mach64->context_load_cntl & 0x30000) {
        addr                 = ((0x3fff - (mach64->context_load_cntl & 0x3fff)) * 256) & mach64->vram_mask;
        mach64->context_mask = *(uint32_t *) &svga->vram[addr];
        mach64_log("mach64_load_context %08X from %08X : mask %08X\n", mach64->context_load_cntl, addr, mach64->context_mask);

        if (mach64->context_mask & (1 << 2))
            mach64_accel_write_fifo_l(mach64, 0x100, *(uint32_t *) &svga->vram[addr + 0x08]);
        if (mach64->context_mask & (1 << 3))
            mach64_accel_write_fifo_l(mach64, 0x10c, *(uint32_t *) &svga->vram[addr + 0x0c]);
        if (mach64->context_mask & (1 << 4))
            mach64_accel_write_fifo_l(mach64, 0x118, *(uint32_t *) &svga->vram[addr + 0x10]);
        if (mach64->context_mask & (1 << 5))
            mach64_accel_write_fifo_l(mach64, 0x124, *(uint32_t *) &svga->vram[addr + 0x14]);
        if (mach64->context_mask & (1 << 6))
            mach64_accel_write_fifo_l(mach64, 0x128, *(uint32_t *) &svga->vram[addr + 0x18]);
        if (mach64->context_mask & (1 << 7))
            mach64_accel_write_fifo_l(mach64, 0x12c, *(uint32_t *) &svga->vram[addr + 0x1c]);
        if (mach64->context_mask & (1 << 8))
            mach64_accel_write_fifo_l(mach64, 0x180, *(uint32_t *) &svga->vram[addr + 0x20]);
        if (mach64->context_mask & (1 << 9))
            mach64_accel_write_fifo_l(mach64, 0x18c, *(uint32_t *) &svga->vram[addr + 0x24]);
        if (mach64->context_mask & (1 << 10))
            mach64_accel_write_fifo_l(mach64, 0x198, *(uint32_t *) &svga->vram[addr + 0x28]);
        if (mach64->context_mask & (1 << 11))
            mach64_accel_write_fifo_l(mach64, 0x1a4, *(uint32_t *) &svga->vram[addr + 0x2c]);
        if (mach64->context_mask & (1 << 12))
            mach64_accel_write_fifo_l(mach64, 0x1b0, *(uint32_t *) &svga->vram[addr + 0x30]);
        if (mach64->context_mask & (1 << 13))
            mach64_accel_write_fifo_l(mach64, 0x280, *(uint32_t *) &svga->vram[addr + 0x34]);
        if (mach64->context_mask & (1 << 14))
            mach64_accel_write_fifo_l(mach64, 0x284, *(uint32_t *) &svga->vram[addr + 0x38]);
        if (mach64->context_mask & (1 << 15))
            mach64_accel_write_fifo_l(mach64, 0x2a8, *(uint32_t *) &svga->vram[addr + 0x3c]);
        if (mach64->context_mask & (1 << 16))
            mach64_accel_write_fifo_l(mach64, 0x2b4, *(uint32_t *) &svga->vram[addr + 0x40]);
        if (mach64->context_mask & (1 << 17))
            mach64_accel_write_fifo_l(mach64, 0x2c0, *(uint32_t *) &svga->vram[addr + 0x44]);
        if (mach64->context_mask & (1 << 18))
            mach64_accel_write_fifo_l(mach64, 0x2c4, *(uint32_t *) &svga->vram[addr + 0x48]);
        if (mach64->context_mask & (1 << 19))
            mach64_accel_write_fifo_l(mach64, 0x2c8, *(uint32_t *) &svga->vram[addr + 0x4c]);
        if (mach64->context_mask & (1 << 20))
            mach64_accel_write_fifo_l(mach64, 0x2cc, *(uint32_t *) &svga->vram[addr + 0x50]);
        if (mach64->context_mask & (1 << 21))
            mach64_accel_write_fifo_l(mach64, 0x2d0, *(uint32_t *) &svga->vram[addr + 0x54]);
        if (mach64->context_mask & (1 << 22))
            mach64_accel_write_fifo_l(mach64, 0x2d4, *(uint32_t *) &svga->vram[addr + 0x58]);
        if (mach64->context_mask & (1 << 23))
            mach64_accel_write_fifo_l(mach64, 0x2d8, *(uint32_t *) &svga->vram[addr + 0x5c]);
        if (mach64->context_mask & (1 << 24))
            mach64_accel_write_fifo_l(mach64, 0x300, *(uint32_t *) &svga->vram[addr + 0x60]);
        if (mach64->context_mask & (1 << 25))
            mach64_accel_write_fifo_l(mach64, 0x304, *(uint32_t *) &svga->vram[addr + 0x64]);
        if (mach64->context_mask & (1 << 26))
            mach64_accel_write_fifo_l(mach64, 0x308, *(uint32_t *) &svga->vram[addr + 0x68]);
        if (mach64->context_mask & (1 << 27))
            mach64_accel_write_fifo_l(mach64, 0x330, *(uint32_t *) &svga->vram[addr + 0x6c]);

        mach64->context_load_cntl = *(uint32_t *) &svga->vram[addr + 0x70];
    }
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
