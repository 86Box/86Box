/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3DFX Voodoo emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2008-2020 Sarah Walker.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_banshee.h>
#include <86box/vid_voodoo_banshee_blitter.h>
#include <86box/vid_voodoo_fb.h>
#include <86box/vid_voodoo_fifo.h>
#include <86box/vid_voodoo_reg.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>
#include <86box/vid_voodoo_texture.h>

#ifdef ENABLE_VOODOO_FIFO_LOG
int voodoo_fifo_do_log = ENABLE_VOODOO_FIFO_LOG;

static void
voodoo_fifo_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoo_fifo_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define voodoo_fifo_log(fmt, ...)
#endif

#define WAKE_DELAY_DEFAULT (TIMER_USEC * 100)

/* Per-card wake delay: keep all Voodoo cards at the default */
#define WAKE_DELAY_OF(v) (WAKE_DELAY_DEFAULT)

static __inline uint8_t
voodoo_queue_color_buf_tag(const voodoo_t *voodoo, int buf)
{
    if (buf == voodoo->queued_disp_buffer)
        return VOODOO_BUF_FRONT;
    if (buf == voodoo->queued_draw_buffer)
        return VOODOO_BUF_BACK;
    return VOODOO_BUF_UNKNOWN;
}

static __inline void
voodoo_queue_recalc_buffers(voodoo_t *voodoo)
{
    switch (voodoo->queued_lfbMode & LFB_WRITE_MASK) {
        case LFB_WRITE_FRONT:
            voodoo->queued_fb_write_buffer = voodoo->queued_disp_buffer;
            break;
        case LFB_WRITE_BACK:
            voodoo->queued_fb_write_buffer = voodoo->queued_draw_buffer;
            break;
        default:
            voodoo->queued_fb_write_buffer = voodoo->queued_disp_buffer;
            break;
    }

    switch (voodoo->queued_fbzMode & FBZ_DRAW_MASK) {
        case FBZ_DRAW_FRONT:
            voodoo->queued_fb_draw_buffer = voodoo->queued_disp_buffer;
            break;
        case FBZ_DRAW_BACK:
            voodoo->queued_fb_draw_buffer = voodoo->queued_draw_buffer;
            break;
        default:
            voodoo->queued_fb_draw_buffer = voodoo->queued_draw_buffer;
            break;
    }
}

static __inline void
voodoo_queue_apply_reg(voodoo_t *voodoo, uint32_t addr, uint32_t val)
{
    switch (addr & 0x3fc) {
        case SST_lfbMode:
            voodoo->queued_lfbMode = val;
            voodoo_queue_recalc_buffers(voodoo);
            break;
        case SST_fbzMode:
            voodoo->queued_fbzMode = val;
            voodoo_queue_recalc_buffers(voodoo);
            break;
        case SST_swapbufferCMD:
            if (TRIPLE_BUFFER) {
                voodoo->queued_disp_buffer = (voodoo->queued_disp_buffer + 1) % 3;
                voodoo->queued_draw_buffer = (voodoo->queued_draw_buffer + 1) % 3;
            } else {
                voodoo->queued_disp_buffer = !voodoo->queued_disp_buffer;
                voodoo->queued_draw_buffer = !voodoo->queued_draw_buffer;
            }
            voodoo_queue_recalc_buffers(voodoo);
            break;
        default:
            break;
    }
}

static __inline uint8_t
voodoo_queue_reg_target_buf(voodoo_t *voodoo, uint32_t addr)
{
    switch (addr & 0x3fc) {
        case SST_triangleCMD:
        case SST_ftriangleCMD:
        case SST_fastfillCMD:
            return voodoo_queue_color_buf_tag(voodoo, voodoo->queued_fb_draw_buffer);
        case SST_lfbMode:
        case SST_fbzMode:
        case SST_swapbufferCMD:
            return VOODOO_BUF_UNKNOWN;
        default:
            return VOODOO_BUF_NONE;
    }
}

static __inline void
voodoo_cmdfifo_reg_writel(voodoo_t *voodoo, uint32_t addr, uint32_t val)
{
    voodoo_reg_writel(addr, val, voodoo);
    voodoo_queue_apply_reg(voodoo, addr, val);
}
void
voodoo_wake_fifo_thread(voodoo_t *voodoo)
{
    if (!timer_is_enabled(&voodoo->wake_timer)) {
        /*Don't wake FIFO thread immediately - if we do that it will probably
          process one word and go back to sleep, requiring it to be woken on
          almost every write. Instead, wait a short while so that the CPU
          emulation writes more data so we have more batched-up work.*/
        timer_set_delay_u64(&voodoo->wake_timer, WAKE_DELAY_OF(voodoo));
    }
}

void
voodoo_wake_fifo_thread_now(voodoo_t *voodoo)
{
    thread_set_event(voodoo->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

void
voodoo_wake_timer(void *priv)
{
    voodoo_t *voodoo = (voodoo_t *) priv;

    thread_set_event(voodoo->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

void
voodoo_queue_command(voodoo_t *voodoo, uint32_t addr_type, uint32_t val)
{
    fifo_entry_t *fifo = &voodoo->fifo[voodoo->fifo_write_idx & FIFO_MASK];
    uint64_t      fifo_wait_start = 0;
    uint64_t      fifo_wait_spins = 0;
    int           fifo_wait_active = 0;

    while (FIFO_FULL) {
        if (voodoo->wait_stats_enabled) {
            if (!fifo_wait_active) {
                fifo_wait_active = 1;
                fifo_wait_start  = plat_timer_read();
                voodoo->fifo_full_waits++;
            }
            fifo_wait_spins++;
        }
        thread_reset_event(voodoo->fifo_not_full_event);
        if (FIFO_FULL) {
            thread_wait_event(voodoo->fifo_not_full_event, 1); /*Wait for room in ringbuffer*/
            if (FIFO_FULL)
                voodoo_wake_fifo_thread_now(voodoo);
        }
    }

    if (fifo_wait_active) {
        voodoo->fifo_full_wait_ticks += plat_timer_read() - fifo_wait_start;
        voodoo->fifo_full_spin_checks += fifo_wait_spins;
    }

#ifdef _WIN32
    /* Reset only after an empty signal to avoid heavy ResetEvent churn on Windows. */
    if (ATOMIC_LOAD(voodoo->fifo_empty_signaled)) {
        ATOMIC_STORE(voodoo->fifo_empty_signaled, 0);
        thread_reset_event(voodoo->fifo_empty_event);
    }
#else
    thread_reset_event(voodoo->fifo_empty_event);
#endif

    fifo->val        = val;
    fifo->addr_type  = addr_type;
    fifo->target_buf = VOODOO_BUF_NONE;

    if (((addr_type & FIFO_TYPE) == FIFO_WRITEW_FB) ||
        ((addr_type & FIFO_TYPE) == FIFO_WRITEL_FB)) {
        fifo->target_buf = voodoo_queue_color_buf_tag(voodoo, voodoo->queued_fb_write_buffer);
        ATOMIC_INC(voodoo->pending_fb_writes_buf[fifo->target_buf]);
    } else if ((addr_type & FIFO_TYPE) == FIFO_WRITEL_REG) {
        uint8_t reg_buf = voodoo_queue_reg_target_buf(voodoo, addr_type & FIFO_ADDR);

        if (reg_buf != VOODOO_BUF_NONE) {
            fifo->target_buf = reg_buf;
            ATOMIC_INC(voodoo->pending_draw_cmds_buf[fifo->target_buf]);
        }
        voodoo_queue_apply_reg(voodoo, addr_type & FIFO_ADDR, val);
    }

    voodoo->fifo_write_idx++;
    voodoo->cmd_status &= ~(1 << 24);

    if (FIFO_ENTRIES > 0xe000)
        voodoo_wake_fifo_thread(voodoo);
}

void
voodoo_flush(voodoo_t *voodoo)
{
    voodoo->flush = 1;
    while (!FIFO_EMPTY) {
        voodoo_wake_fifo_thread_now(voodoo);
        thread_wait_event(voodoo->fifo_empty_event, -1);
    }
    voodoo_wait_for_render_thread_idle(voodoo);
    voodoo->flush = 0;
}

void
voodoo_wake_fifo_threads(voodoo_set_t *set, voodoo_t *voodoo)
{
    voodoo_wake_fifo_thread(voodoo);
    if (SLI_ENABLED && voodoo->type != VOODOO_2 && set->voodoos[0] == voodoo)
        voodoo_wake_fifo_thread(set->voodoos[1]);
}

void
voodoo_wait_for_swap_complete(voodoo_t *voodoo)
{
    while (voodoo->swap_pending) {
        /* Avoid waiting on wake_fifo_thread here; main thread may be draining the FIFO. */
        thread_wait_mutex(voodoo->swap_mutex);
        if ((voodoo->swap_pending && voodoo->flush) || FIFO_FULL) {
            /*Main thread is waiting for FIFO to empty, so skip vsync wait and just swap*/
            memset(voodoo->dirty_line, 1, sizeof(voodoo->dirty_line));
            voodoo->front_offset = voodoo->params.front_offset;
            if (voodoo->swap_count > 0)
                voodoo->swap_count--;
            voodoo->swap_pending = 0;
            thread_release_mutex(voodoo->swap_mutex);
            break;
        } else
            thread_release_mutex(voodoo->swap_mutex);
        /* Yield briefly while waiting for the swap to complete. */
#ifdef _WIN32
        /* Sleep(1) can add measurable stalls on Windows. */
        plat_delay_ms(0);
#else
        plat_delay_ms(1);
#endif
    }
}

static uint32_t
cmdfifo_get(voodoo_t *voodoo)
{
    uint32_t val;

    if (!voodoo->cmdfifo_in_sub) {
        while (voodoo->fifo_thread_run && (voodoo->cmdfifo_depth_rd == voodoo->cmdfifo_depth_wr)) {
            thread_wait_event(voodoo->wake_fifo_thread, -1);
            thread_reset_event(voodoo->wake_fifo_thread);
        }
    }

    if (voodoo->cmdfifo_in_agp)
        val = mem_readl_phys(voodoo->cmdfifo_rp);
    else
        val = *(uint32_t *) &voodoo->fb_mem[voodoo->cmdfifo_rp & voodoo->fb_mask];

    if (!voodoo->cmdfifo_in_sub)
        voodoo->cmdfifo_depth_rd++;
    voodoo->cmdfifo_rp += 4;

    //        voodoo_fifo_log("  CMDFIFO get %08x\n", val);
    return val;
}

static inline float
cmdfifo_get_f(voodoo_t *voodoo)
{
    union {
        uint32_t i;
        float    f;
    } tempif;

    tempif.i = cmdfifo_get(voodoo);
    return tempif.f;
}

static uint32_t
cmdfifo_get_2(voodoo_t *voodoo)
{
    uint32_t val;

    if (!voodoo->cmdfifo_in_sub_2) {
        while (voodoo->fifo_thread_run && (voodoo->cmdfifo_depth_rd_2 == voodoo->cmdfifo_depth_wr_2)) {
            thread_wait_event(voodoo->wake_fifo_thread, -1);
            thread_reset_event(voodoo->wake_fifo_thread);
        }
    }

    if (voodoo->cmdfifo_in_agp_2)
        val = mem_readl_phys(voodoo->cmdfifo_rp_2);
    else
        val = *(uint32_t *) &voodoo->fb_mem[voodoo->cmdfifo_rp_2 & voodoo->fb_mask];

    if (!voodoo->cmdfifo_in_sub_2)
        voodoo->cmdfifo_depth_rd_2++;
    voodoo->cmdfifo_rp_2 += 4;

    //        voodoo_fifo_log("  CMDFIFO get %08x\n", val);
    return val;
}

static inline float
cmdfifo_get_f_2(voodoo_t *voodoo)
{
    union {
        uint32_t i;
        float    f;
    } tempif;

    tempif.i = cmdfifo_get_2(voodoo);
    return tempif.f;
}

enum {
    CMDFIFO3_PC_MASK_RGB   = (1 << 10),
    CMDFIFO3_PC_MASK_ALPHA = (1 << 11),
    CMDFIFO3_PC_MASK_Z     = (1 << 12),
    CMDFIFO3_PC_MASK_Wb    = (1 << 13),
    CMDFIFO3_PC_MASK_W0    = (1 << 14),
    CMDFIFO3_PC_MASK_S0_T0 = (1 << 15),
    CMDFIFO3_PC_MASK_W1    = (1 << 16),
    CMDFIFO3_PC_MASK_S1_T1 = (1 << 17),

    CMDFIFO3_PC = (1 << 28)
};

void
voodoo_fifo_thread(void *param)
{
    voodoo_t *voodoo = (voodoo_t *) param;

    while (voodoo->fifo_thread_run) {
        thread_set_event(voodoo->fifo_not_full_event);
        thread_wait_event(voodoo->wake_fifo_thread, -1);
        thread_reset_event(voodoo->wake_fifo_thread);
        voodoo->voodoo_busy = 1;
        while (!FIFO_EMPTY) {
            uint64_t      start_time = plat_timer_read();
            uint64_t      end_time;
            fifo_entry_t *fifo = &voodoo->fifo[voodoo->fifo_read_idx & FIFO_MASK];

            switch (fifo->addr_type & FIFO_TYPE) {
                case FIFO_WRITEL_REG:
                    while ((fifo->addr_type & FIFO_TYPE) == FIFO_WRITEL_REG) {
                        uint32_t reg_addr  = fifo->addr_type & FIFO_ADDR;
                        uint8_t  target_buf = fifo->target_buf;

                        voodoo_reg_writel(reg_addr, fifo->val, voodoo);
                        fifo->addr_type = FIFO_INVALID;
                        voodoo->fifo_read_idx++;
                        if (target_buf < VOODOO_BUF_COUNT) {
                            ATOMIC_DEC(voodoo->pending_draw_cmds_buf[target_buf]);
                        }
                        if (FIFO_EMPTY)
                            break;
                        fifo = &voodoo->fifo[voodoo->fifo_read_idx & FIFO_MASK];
                    }
                    break;
                case FIFO_WRITEW_FB:
                    voodoo_wait_for_render_thread_idle(voodoo);
                    while ((fifo->addr_type & FIFO_TYPE) == FIFO_WRITEW_FB) {
                        uint8_t target_buf = fifo->target_buf;

                        voodoo_fb_writew(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                        fifo->addr_type = FIFO_INVALID;
                        voodoo->fifo_read_idx++;
                        if (target_buf < VOODOO_BUF_COUNT) {
                            ATOMIC_DEC(voodoo->pending_fb_writes_buf[target_buf]);
                        }
                        if (FIFO_EMPTY)
                            break;
                        fifo = &voodoo->fifo[voodoo->fifo_read_idx & FIFO_MASK];
                    }
                    break;
                case FIFO_WRITEL_FB:
                    voodoo_wait_for_render_thread_idle(voodoo);
                    while ((fifo->addr_type & FIFO_TYPE) == FIFO_WRITEL_FB) {
                        uint8_t target_buf = fifo->target_buf;

                        voodoo_fb_writel(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                        fifo->addr_type = FIFO_INVALID;
                        voodoo->fifo_read_idx++;
                        if (target_buf < VOODOO_BUF_COUNT) {
                            ATOMIC_DEC(voodoo->pending_fb_writes_buf[target_buf]);
                        }
                        if (FIFO_EMPTY)
                            break;
                        fifo = &voodoo->fifo[voodoo->fifo_read_idx & FIFO_MASK];
                    }
                    break;
                case FIFO_WRITEL_TEX:
                    while ((fifo->addr_type & FIFO_TYPE) == FIFO_WRITEL_TEX) {
                        if (!(fifo->addr_type & 0x400000))
                            voodoo_tex_writel(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                        fifo->addr_type = FIFO_INVALID;
                        voodoo->fifo_read_idx++;
                        if (FIFO_EMPTY)
                            break;
                        fifo = &voodoo->fifo[voodoo->fifo_read_idx & FIFO_MASK];
                    }
                    break;
                case FIFO_WRITEL_2DREG:
                    while ((fifo->addr_type & FIFO_TYPE) == FIFO_WRITEL_2DREG) {
                        voodoo_2d_reg_writel(voodoo, fifo->addr_type & FIFO_ADDR, fifo->val);
                        fifo->addr_type = FIFO_INVALID;
                        voodoo->fifo_read_idx++;
                        if (FIFO_EMPTY)
                            break;
                        fifo = &voodoo->fifo[voodoo->fifo_read_idx & FIFO_MASK];
                    }
                    break;

                default:
                    fatal("Unknown fifo entry %08x\n", fifo->addr_type);
            }

            if (FIFO_ENTRIES > 0xe000)
                thread_set_event(voodoo->fifo_not_full_event);

            end_time = plat_timer_read();
            voodoo->time += end_time - start_time;
        }

        voodoo->cmd_status |= (1 << 24);
        voodoo->cmd_status_2 |= (1 << 24);
        thread_set_event(voodoo->fifo_empty_event);
        ATOMIC_STORE(voodoo->fifo_empty_signaled, 1);

        while (voodoo->cmdfifo_enabled && (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr || voodoo->cmdfifo_in_sub)) {
            uint64_t start_time = plat_timer_read();
            uint64_t end_time;
            uint32_t header = cmdfifo_get(voodoo);
            uint32_t addr;
            uint32_t mask;
            int      smode;
            int      num;
            int      num_verticies;
            int      v_num;

#if 0
            voodoo_fifo_log(" CMDFIFO header %08x at %08x\n", header, voodoo->cmdfifo_rp);
#endif

            voodoo->cmd_status &= ~7;
            voodoo->cmd_status |= (header & 7);
            voodoo->cmd_status |= (1 << 11);
            switch (header & 7) {
                case 0:
#if 0
                    voodoo_fifo_log("CMDFIFO0\n");
#endif
                    voodoo->cmd_status = (voodoo->cmd_status & 0xffff8fff) | (((header >> 3) & 7) << 12);
                    switch ((header >> 3) & 7) {
                        case 0: /*NOP*/
                            break;

                        case 1: /*JSR*/
#if 0
                            voodoo_fifo_log("JSR %08x\n", (header >> 4) & 0xfffffc);
#endif
                            voodoo->cmdfifo_ret_addr = voodoo->cmdfifo_rp;
                            voodoo->cmdfifo_rp       = (header >> 4) & 0xfffffc;
                            voodoo->cmdfifo_in_sub   = 1;
                            break;

                        case 2: /*RET*/
                            voodoo->cmdfifo_rp     = voodoo->cmdfifo_ret_addr;
                            voodoo->cmdfifo_in_sub = 0;
                            break;

                        case 3: /*JMP local frame buffer*/
                            voodoo->cmdfifo_rp     = (header >> 4) & 0xfffffc;
                            voodoo->cmdfifo_in_agp = 0;
#if 0
                            voodoo_fifo_log("JMP LFB to %08x %04x\n", voodoo->cmdfifo_rp, header);
#endif
                            break;

                        case 4: /*JMP AGP*/
                            if (UNLIKELY(voodoo->type < VOODOO_BANSHEE))
                                fatal("CMDFIFO0: Not Banshee %08x\n", header);

                            voodoo->cmdfifo_rp     = ((header >> 4) & 0x1fffffc) | (cmdfifo_get(voodoo) << 25);
                            voodoo->cmdfifo_in_agp = 1;
#if 0
                            voodoo_fifo_log("JMP AGP to %08x %04x\n", voodoo->cmdfifo_rp, header);
#endif
                            break;

                        default:
                            fatal("Bad CMDFIFO0 %08x\n", header);
                    }
                    voodoo->cmd_status = (voodoo->cmd_status & ~(1 << 27)) | (voodoo->cmdfifo_in_sub << 27);
                    break;

                case 1:
                    num  = header >> 16;
                    addr = (header & 0x7ff8) >> 1;
#if 0
                    voodoo_fifo_log("CMDFIFO1 addr=%08x\n",addr);
#endif
                    while (num--) {
                        uint32_t val = cmdfifo_get(voodoo);
                        if ((addr & (1 << 13)) && voodoo->type >= VOODOO_BANSHEE) {
#if 0
                            if (voodoo->type != VOODOO_BANSHEE)
                                fatal("CMDFIFO1: Not Banshee\n");
#endif

#if 0
                            voodoo_fifo_log("CMDFIFO1: write %08x %08x\n", addr, val);
#endif
                            voodoo_2d_reg_writel(voodoo, addr, val);
                        } else {
                            if ((addr & 0x3ff) == SST_triangleCMD || (addr & 0x3ff) == SST_ftriangleCMD || (addr & 0x3ff) == SST_fastfillCMD || (addr & 0x3ff) == SST_nopCMD)
                                voodoo->cmd_written_fifo++;

                            if (voodoo->type >= VOODOO_BANSHEE && (addr & 0x3ff) == SST_swapbufferCMD)
                                voodoo->cmd_written_fifo++;
                            voodoo_cmdfifo_reg_writel(voodoo, addr, val);
                        }

                        if (header & (1 << 15))
                            addr += 4;
                    }
                    break;

                case 2:
                    if (voodoo->type < VOODOO_2)
                        fatal("CMDFIFO2: Not Voodoo 2\n");
                    mask = (header >> 3);
                    addr = 8;
                    while (mask) {
                        if (mask & 1) {
                            uint32_t val = cmdfifo_get(voodoo);

                            voodoo_2d_reg_writel(voodoo, addr, val);
                        }

                        addr += 4;
                        mask >>= 1;
                    }
                    break;

                case 3:
                    num   = (header >> 29) & 7;
                    mask  = header; //(header >> 10) & 0xff;
                    smode = (header >> 22) & 0xf;
                    voodoo_cmdfifo_reg_writel(voodoo, SST_sSetupMode, ((header >> 10) & 0xff) | (smode << 16));
                    num_verticies = (header >> 6) & 0xf;
                    v_num         = 0;
                    if (((header >> 3) & 7) == 2)
                        v_num = 1;
#if 0
                    voodoo_fifo_log("CMDFIFO3: num=%i verts=%i mask=%02x\n", num, num_verticies, (header >> 10) & 0xff);
                    voodoo_fifo_log("CMDFIFO3 %02x %i\n", (header >> 10), (header >> 3) & 7);
#endif

                    while (num_verticies--) {
                        voodoo->verts[3].sVx = cmdfifo_get_f(voodoo);
                        voodoo->verts[3].sVy = cmdfifo_get_f(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_RGB) {
                            if (header & CMDFIFO3_PC) {
                                uint32_t val            = cmdfifo_get(voodoo);
                                voodoo->verts[3].sBlue  = (float) (val & 0xff);
                                voodoo->verts[3].sGreen = (float) ((val >> 8) & 0xff);
                                voodoo->verts[3].sRed   = (float) ((val >> 16) & 0xff);
                                voodoo->verts[3].sAlpha = (float) ((val >> 24) & 0xff);
                            } else {
                                voodoo->verts[3].sRed   = cmdfifo_get_f(voodoo);
                                voodoo->verts[3].sGreen = cmdfifo_get_f(voodoo);
                                voodoo->verts[3].sBlue  = cmdfifo_get_f(voodoo);
                            }
                        }
                        if ((mask & CMDFIFO3_PC_MASK_ALPHA) && !(header & CMDFIFO3_PC))
                            voodoo->verts[3].sAlpha = cmdfifo_get_f(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_Z)
                            voodoo->verts[3].sVz = cmdfifo_get_f(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_Wb)
                            voodoo->verts[3].sWb = cmdfifo_get_f(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_W0)
                            voodoo->verts[3].sW0 = cmdfifo_get_f(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_S0_T0) {
                            voodoo->verts[3].sS0 = cmdfifo_get_f(voodoo);
                            voodoo->verts[3].sT0 = cmdfifo_get_f(voodoo);
                        }
                        if (mask & CMDFIFO3_PC_MASK_W1)
                            voodoo->verts[3].sW1 = cmdfifo_get_f(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_S1_T1) {
                            voodoo->verts[3].sS1 = cmdfifo_get_f(voodoo);
                            voodoo->verts[3].sT1 = cmdfifo_get_f(voodoo);
                        }
                        if (v_num)
                            voodoo_cmdfifo_reg_writel(voodoo, SST_sDrawTriCMD, 0);
                        else
                            voodoo_cmdfifo_reg_writel(voodoo, SST_sBeginTriCMD, 0);
                        v_num++;
                        if (v_num == 3 && ((header >> 3) & 7) == 0)
                            v_num = 0;
                    }
                    while (num--)
                        cmdfifo_get(voodoo);
                    break;

                case 4:
                    num  = (header >> 29) & 7;
                    mask = (header >> 15) & 0x3fff;
                    addr = (header & 0x7ff8) >> 1;
#if 0
                    voodoo_fifo_log("CMDFIFO4 addr=%08x\n",addr);
#endif
                    while (mask) {
                        if (mask & 1) {
                            uint32_t val = cmdfifo_get(voodoo);

                            if ((addr & (1 << 13)) && voodoo->type >= VOODOO_BANSHEE) {
                                if (voodoo->type < VOODOO_BANSHEE)
                                    fatal("CMDFIFO1: Not Banshee\n");
#if 0
                                voodoo_fifo_log("CMDFIFO1: write %08x %08x\n", addr, val);
#endif

                                voodoo_2d_reg_writel(voodoo, addr, val);
                            } else {
                                if ((addr & 0x3ff) == SST_triangleCMD || (addr & 0x3ff) == SST_ftriangleCMD || (addr & 0x3ff) == SST_fastfillCMD || (addr & 0x3ff) == SST_nopCMD)
                                    voodoo->cmd_written_fifo++;

                                if (voodoo->type >= VOODOO_BANSHEE && (addr & 0x3ff) == SST_swapbufferCMD)
                                    voodoo->cmd_written_fifo++;
                                voodoo_cmdfifo_reg_writel(voodoo, addr, val);
                            }
                        }

                        addr += 4;
                        mask >>= 1;
                    }
                    while (num--)
                        cmdfifo_get(voodoo);
                    break;

                case 5:
#if 0
                    if (header & 0x3fc00000)
                        fatal("CMDFIFO packet 5 has byte disables set %08x\n", header);
#endif
                    num  = (header >> 3) & 0x7ffff;
                    addr = cmdfifo_get(voodoo) & 0xffffff;
                    if (!num)
                        num = 1;
#if 0
                    voodoo_fifo_log("CMDFIFO5 addr=%08x num=%i\n", addr, num);
#endif
                    switch (header >> 30) {
                        case 0: /*Linear framebuffer (Banshee)*/
                        case 1: /*Planar YUV*/
                            if (voodoo->texture_present[0][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT]) {
#if 0
                                voodoo_fifo_log("texture_present at %08x %i\n", addr, (addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT);
#endif
                                flush_texture_cache(voodoo, addr & voodoo->texture_mask, 0);
                            }
                            if (voodoo->texture_present[1][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT]) {
#if 0
                                voodoo_fifo_log("texture_present at %08x %i\n", addr, (addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT);
#endif
                                flush_texture_cache(voodoo, addr & voodoo->texture_mask, 1);
                            }
                            while (num--) {
                                uint32_t val = cmdfifo_get(voodoo);
                                if (addr <= voodoo->fb_mask)
                                    *(uint32_t *) &voodoo->fb_mem[addr] = val;
                                addr += 4;
                            }
                            break;
                        case 2: /*Framebuffer*/
                            while (num--) {
                                uint32_t val = cmdfifo_get(voodoo);
                                voodoo_fb_writel(addr, val, voodoo);
                                addr += 4;
                            }
                            break;
                        case 3: /*Texture*/
                            while (num--) {
                                uint32_t val = cmdfifo_get(voodoo);
                                voodoo_tex_writel(addr, val, voodoo);
                                addr += 4;
                            }
                            break;

                        default:
                            fatal("CMDFIFO packet 5 bad space %08x %08x\n", header, voodoo->cmdfifo_rp);
                    }
                    break;

                case 6:
                    if (UNLIKELY(voodoo->type < VOODOO_BANSHEE)) {
                        fatal("CMDFIFO6: Not Banshee %08x %08x\n", header, voodoo->cmdfifo_rp);
                    } else {
                        uint32_t val = cmdfifo_get(voodoo);
                        banshee_cmd_write(voodoo->priv, 0x00, val >> 5);            /* agpReqSize */
                        banshee_cmd_write(voodoo->priv, 0x04, cmdfifo_get(voodoo)); /* agpHostAddressLow */
                        banshee_cmd_write(voodoo->priv, 0x08, cmdfifo_get(voodoo)); /* agpHostAddressHigh */
                        banshee_cmd_write(voodoo->priv, 0x0c, cmdfifo_get(voodoo)); /* agpGraphicsAddress */
                        banshee_cmd_write(voodoo->priv, 0x10, cmdfifo_get(voodoo)); /* agpGraphicsStride */
                        banshee_cmd_write(voodoo->priv, 0x14, (val & 0x18) | 0x00); /* agpMoveCMD - start transfer */
#if 0
                        voodoo_fifo_log("CMDFIFO6 addr=%08x num=%i\n", addr, banshee->agpReqSize);
#endif
                    }
                    break;

                default:
                    fatal("Bad CMDFIFO packet %08x %08x\n", header, voodoo->cmdfifo_rp);
            }

            end_time = plat_timer_read();
            voodoo->time += end_time - start_time;
        }

        while (voodoo->cmdfifo_enabled_2 && (voodoo->cmdfifo_depth_rd_2 != voodoo->cmdfifo_depth_wr_2 || voodoo->cmdfifo_in_sub_2)) {
            uint64_t start_time = plat_timer_read();
            uint64_t end_time;
            uint32_t header = cmdfifo_get_2(voodoo);
            uint32_t addr;
            uint32_t mask;
            int      smode;
            int      num;
            int      num_verticies;
            int      v_num;

#if 0
            voodoo_fifo_log(" CMDFIFO header %08x at %08x\n", header, voodoo->cmdfifo_rp);
#endif

            voodoo->cmd_status_2 &= ~7;
            voodoo->cmd_status_2 |= (header & 7);
            voodoo->cmd_status_2 |= (1 << 11);
            switch (header & 7) {
                case 0:
#if 0
                    voodoo_fifo_log("CMDFIFO0\n");
#endif
                    voodoo->cmd_status_2 = (voodoo->cmd_status_2 & 0xffff8fff) | (((header >> 3) & 7) << 12);
                    switch ((header >> 3) & 7) {
                        case 0: /*NOP*/
                            break;

                        case 1: /*JSR*/
#if 0
                            voodoo_fifo_log("JSR %08x\n", (header >> 4) & 0xfffffc);
#endif
                            voodoo->cmdfifo_ret_addr_2 = voodoo->cmdfifo_rp_2;
                            voodoo->cmdfifo_rp_2       = (header >> 4) & 0xfffffc;
                            voodoo->cmdfifo_in_sub_2   = 1;
                            break;

                        case 2: /*RET*/
                            voodoo->cmdfifo_rp_2     = voodoo->cmdfifo_ret_addr_2;
                            voodoo->cmdfifo_in_sub_2 = 0;
                            break;

                        case 3: /*JMP local frame buffer*/
                            voodoo->cmdfifo_rp_2     = (header >> 4) & 0xfffffc;
                            voodoo->cmdfifo_in_agp_2 = 0;
#if 0
                            voodoo_fifo_log("JMP LFB to %08x %04x\n", voodoo->cmdfifo_rp_2, header);
#endif
                            break;

                        case 4: /*JMP AGP*/
                            if (UNLIKELY(voodoo->type < VOODOO_BANSHEE))
                                fatal("CMDFIFO0: Not Banshee %08x\n", header);

                            voodoo->cmdfifo_rp_2     = ((header >> 4) & 0x1fffffc) | (cmdfifo_get_2(voodoo) << 25);
                            voodoo->cmdfifo_in_agp_2 = 1;
#if 0
                            voodoo_fifo_log("JMP AGP to %08x %04x\n", voodoo->cmdfifo_rp_2, header);
#endif
                            break;

                        default:
                            fatal("Bad CMDFIFO0 %08x\n", header);
                    }
                    voodoo->cmd_status_2 = (voodoo->cmd_status_2 & ~(1 << 27)) | (voodoo->cmdfifo_in_sub_2 << 27);
                    break;

                case 1:
                    num  = header >> 16;
                    addr = (header & 0x7ff8) >> 1;
#if 0
                    voodoo_fifo_log("CMDFIFO1 addr=%08x\n",addr);
#endif
                    while (num--) {
                        uint32_t val = cmdfifo_get_2(voodoo);
                        if ((addr & (1 << 13)) && voodoo->type >= VOODOO_BANSHEE) {
#if 0
                            if (voodoo->type != VOODOO_BANSHEE)
                                fatal("CMDFIFO1: Not Banshee\n");
#endif

#if 0
                            voodoo_fifo_log("CMDFIFO1: write %08x %08x\n", addr, val);
#endif
                            voodoo_2d_reg_writel(voodoo, addr, val);
                        } else {
                            if ((addr & 0x3ff) == SST_triangleCMD || (addr & 0x3ff) == SST_ftriangleCMD || (addr & 0x3ff) == SST_fastfillCMD || (addr & 0x3ff) == SST_nopCMD)
                                voodoo->cmd_written_fifo_2++;

                            if (voodoo->type >= VOODOO_BANSHEE && (addr & 0x3ff) == SST_swapbufferCMD)
                                voodoo->cmd_written_fifo_2++;
                            voodoo_cmdfifo_reg_writel(voodoo, addr, val);
                        }

                        if (header & (1 << 15))
                            addr += 4;
                    }
                    break;

                case 2:
                    if (voodoo->type < VOODOO_2)
                        fatal("CMDFIFO2: Not Voodoo 2\n");
                    mask = (header >> 3);
                    addr = 8;
                    while (mask) {
                        if (mask & 1) {
                            uint32_t val = cmdfifo_get_2(voodoo);

                            voodoo_2d_reg_writel(voodoo, addr, val);
                        }

                        addr += 4;
                        mask >>= 1;
                    }
                    break;

                case 3:
                    num   = (header >> 29) & 7;
                    mask  = header; //(header >> 10) & 0xff;
                    smode = (header >> 22) & 0xf;
                    voodoo_cmdfifo_reg_writel(voodoo, SST_sSetupMode, ((header >> 10) & 0xff) | (smode << 16));
                    num_verticies = (header >> 6) & 0xf;
                    v_num         = 0;
                    if (((header >> 3) & 7) == 2)
                        v_num = 1;
#if 0
                    voodoo_fifo_log("CMDFIFO3: num=%i verts=%i mask=%02x\n", num, num_verticies, (header >> 10) & 0xff);
                    voodoo_fifo_log("CMDFIFO3 %02x %i\n", (header >> 10), (header >> 3) & 7);
#endif

                    while (num_verticies--) {
                        voodoo->verts[3].sVx = cmdfifo_get_f_2(voodoo);
                        voodoo->verts[3].sVy = cmdfifo_get_f_2(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_RGB) {
                            if (header & CMDFIFO3_PC) {
                                uint32_t val            = cmdfifo_get_2(voodoo);
                                voodoo->verts[3].sBlue  = (float) (val & 0xff);
                                voodoo->verts[3].sGreen = (float) ((val >> 8) & 0xff);
                                voodoo->verts[3].sRed   = (float) ((val >> 16) & 0xff);
                                voodoo->verts[3].sAlpha = (float) ((val >> 24) & 0xff);
                            } else {
                                voodoo->verts[3].sRed   = cmdfifo_get_f_2(voodoo);
                                voodoo->verts[3].sGreen = cmdfifo_get_f_2(voodoo);
                                voodoo->verts[3].sBlue  = cmdfifo_get_f_2(voodoo);
                            }
                        }
                        if ((mask & CMDFIFO3_PC_MASK_ALPHA) && !(header & CMDFIFO3_PC))
                            voodoo->verts[3].sAlpha = cmdfifo_get_f_2(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_Z)
                            voodoo->verts[3].sVz = cmdfifo_get_f_2(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_Wb)
                            voodoo->verts[3].sWb = cmdfifo_get_f_2(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_W0)
                            voodoo->verts[3].sW0 = cmdfifo_get_f_2(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_S0_T0) {
                            voodoo->verts[3].sS0 = cmdfifo_get_f_2(voodoo);
                            voodoo->verts[3].sT0 = cmdfifo_get_f_2(voodoo);
                        }
                        if (mask & CMDFIFO3_PC_MASK_W1)
                            voodoo->verts[3].sW1 = cmdfifo_get_f_2(voodoo);
                        if (mask & CMDFIFO3_PC_MASK_S1_T1) {
                            voodoo->verts[3].sS1 = cmdfifo_get_f_2(voodoo);
                            voodoo->verts[3].sT1 = cmdfifo_get_f_2(voodoo);
                        }
                        if (v_num)
                            voodoo_cmdfifo_reg_writel(voodoo, SST_sDrawTriCMD, 0);
                        else
                            voodoo_cmdfifo_reg_writel(voodoo, SST_sBeginTriCMD, 0);
                        v_num++;
                        if (v_num == 3 && ((header >> 3) & 7) == 0)
                            v_num = 0;
                    }
                    while (num--)
                        cmdfifo_get_2(voodoo);
                    break;

                case 4:
                    num  = (header >> 29) & 7;
                    mask = (header >> 15) & 0x3fff;
                    addr = (header & 0x7ff8) >> 1;
#if 0
                    voodoo_fifo_log("CMDFIFO4 addr=%08x\n",addr);
#endif
                    while (mask) {
                        if (mask & 1) {
                            uint32_t val = cmdfifo_get_2(voodoo);

                            if ((addr & (1 << 13)) && voodoo->type >= VOODOO_BANSHEE) {
                                if (voodoo->type < VOODOO_BANSHEE)
                                    fatal("CMDFIFO1: Not Banshee\n");
#if 0
                                voodoo_fifo_log("CMDFIFO1: write %08x %08x\n", addr, val);
#endif

                                voodoo_2d_reg_writel(voodoo, addr, val);
                            } else {
                                if ((addr & 0x3ff) == SST_triangleCMD || (addr & 0x3ff) == SST_ftriangleCMD || (addr & 0x3ff) == SST_fastfillCMD || (addr & 0x3ff) == SST_nopCMD)
                                    voodoo->cmd_written_fifo_2++;

                                if (voodoo->type >= VOODOO_BANSHEE && (addr & 0x3ff) == SST_swapbufferCMD)
                                    voodoo->cmd_written_fifo_2++;
                                voodoo_cmdfifo_reg_writel(voodoo, addr, val);
                            }
                        }

                        addr += 4;
                        mask >>= 1;
                    }
                    while (num--)
                        cmdfifo_get_2(voodoo);
                    break;

                case 5:
#if 0
                    if (header & 0x3fc00000)
                        fatal("CMDFIFO packet 5 has byte disables set %08x\n", header);
#endif
                    num  = (header >> 3) & 0x7ffff;
                    addr = cmdfifo_get_2(voodoo) & 0xffffff;
                    if (!num)
                        num = 1;
#if 0
                    voodoo_fifo_log("CMDFIFO5 addr=%08x num=%i\n", addr, num);
#endif
                    switch (header >> 30) {
                        case 0: /*Linear framebuffer (Banshee)*/
                        case 1: /*Planar YUV*/
                            if (voodoo->texture_present[0][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT]) {
#if 0
                                voodoo_fifo_log("texture_present at %08x %i\n", addr, (addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT);
#endif
                                flush_texture_cache(voodoo, addr & voodoo->texture_mask, 0);
                            }
                            if (voodoo->texture_present[1][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT]) {
#if 0
                                voodoo_fifo_log("texture_present at %08x %i\n", addr, (addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT);
#endif
                                flush_texture_cache(voodoo, addr & voodoo->texture_mask, 1);
                            }
                            while (num--) {
                                uint32_t val = cmdfifo_get_2(voodoo);
                                if (addr <= voodoo->fb_mask)
                                    *(uint32_t *) &voodoo->fb_mem[addr] = val;
                                addr += 4;
                            }
                            break;
                        case 2: /*Framebuffer*/
                            while (num--) {
                                uint32_t val = cmdfifo_get_2(voodoo);
                                voodoo_fb_writel(addr, val, voodoo);
                                addr += 4;
                            }
                            break;
                        case 3: /*Texture*/
                            while (num--) {
                                uint32_t val = cmdfifo_get_2(voodoo);
                                voodoo_tex_writel(addr, val, voodoo);
                                addr += 4;
                            }
                            break;

                        default:
                            fatal("CMDFIFO packet 5 bad space %08x %08x\n", header, voodoo->cmdfifo_rp);
                    }
                    break;

                case 6:
                    if (UNLIKELY(voodoo->type < VOODOO_BANSHEE)) {
                        fatal("CMDFIFO6: Not Banshee %08x %08x\n", header, voodoo->cmdfifo_rp);
                    } else {
                        uint32_t val = cmdfifo_get_2(voodoo);
                        banshee_cmd_write(voodoo->priv, 0x00, val >> 5);              /* agpReqSize */
                        banshee_cmd_write(voodoo->priv, 0x04, cmdfifo_get_2(voodoo)); /* agpHostAddressLow */
                        banshee_cmd_write(voodoo->priv, 0x08, cmdfifo_get_2(voodoo)); /* agpHostAddressHigh */
                        banshee_cmd_write(voodoo->priv, 0x0c, cmdfifo_get_2(voodoo)); /* agpGraphicsAddress */
                        banshee_cmd_write(voodoo->priv, 0x10, cmdfifo_get_2(voodoo)); /* agpGraphicsStride */
                        banshee_cmd_write(voodoo->priv, 0x14, (val & 0x18) | 0x20);   /* agpMoveCMD - start transfer */
#if 0
                        voodoo_fifo_log("CMDFIFO6 addr=%08x num=%i\n", addr, banshee->agpReqSize);
#endif
                    }
                    break;

                default:
                    fatal("Bad CMDFIFO packet %08x %08x\n", header, voodoo->cmdfifo_rp);
            }

            end_time = plat_timer_read();
            voodoo->time += end_time - start_time;
        }

        voodoo->voodoo_busy = 0;
    }
}
