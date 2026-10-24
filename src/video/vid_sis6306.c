/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SiS 530/620 6306 integrated 3D VGA
 *          (PCI ID 1039:6306).
 *
 * Authors: mw308
 *
 *          Copyright 2026 mw308
 *
 * Implements PCI/AGP identity and standard VGA, SiS enhanced packed modes,
 * segment-selection banking, interlaced display handling, threaded 2D/3D
 * acceleration with native whole-pixel 2D ROPs, VGA transition barriers,
 * a main-thread sync hint that keeps idle DOS/VGA framebuffer accesses out of
 * the accelerator atomic/event path, the SiS 64x64 two-bit hardware cursor, MMIO cursor
 * positioning, Win9x host-to-VRAM BitBlt staging, and the SiS 6306 3D
 * rendering engine used by the Windows Direct3D driver.
 */

#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/i2c.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>


/* Run the 2D and 3D engines on one ordered accelerator worker */
#define SIS6306_ACCEL_QUEUE_SIZE 1024U
#define SIS6306_ACCEL_QUEUE_MASK (SIS6306_ACCEL_QUEUE_SIZE - 1U)
/* 2D and 3D render-lane counts are runtime device settings, 1..4 */
#define SIS6306_MAX_RENDER_THREADS     4U
#define SIS6306_2D_REG_BASE       0x8200U
#define SIS6306_2D_REG_SIZE       0x0300U
#define SIS6306_3D_VERTEX_BASE    0x8800U
#define SIS6306_3D_VERTEX_SIZE    0x0060U
#define SIS6306_3D_PRIMITIVE_REG  0x89f8U
#define SIS6306_3D_SETUP_BASE     0x8a00U
#define SIS6306_3D_SETUP_SIZE     0x0084U
#define SIS6306_HW_QUEUE_UNITS        21U
#define SIS6306_TIMING_QUEUE_SIZE     32U
#define SIS6306_TIMING_QUEUE_MASK     (SIS6306_TIMING_QUEUE_SIZE - 1U)
#define SIS6306_TRIANGLE_SETUP_USEC   4.0
#define SIS6306_AREA_PIXELS_PER_USEC 15.5

typedef struct sis6306_3d_state_t {
    uint8_t vertex[SIS6306_3D_VERTEX_SIZE];
    uint8_t primitive[4];
    uint8_t setup[SIS6306_3D_SETUP_SIZE];
} sis6306_3d_state_t;

typedef struct sis6306_3d_vertex_t {
    float x, y, z;
    float u, v, w;
    uint8_t a, r, g, b;
    uint8_t fog;
} sis6306_3d_vertex_t;


/* Triangle setup */
typedef struct sis6306_3d_prepared_t {
    sis6306_3d_vertex_t v[3];

    uint32_t primitive;
    uint32_t dst_base;
    uint32_t zbase;
    uint32_t fog;

    unsigned shade;
    unsigned pitch;
    unsigned zmode;
    unsigned zpitch;

    int ztest_en;
    int zwrite_en;
    int alpha_test_en;
    unsigned alpha_test_mode;
    uint8_t alpha_ref;
    int fog_en;
    int blend_en;
    int tex_en;
    int persp_en;

    unsigned tex_fmt;
    unsigned tex_min;
    int tex_mag_linear;
    unsigned tex_blend;
    unsigned tex_alpha_blend;
    unsigned blend_dst;
    unsigned blend_src;

    uint32_t mem_size;
    int changeframe;
    int tex_w;
    int tex_h;

    uint32_t tex_level_base[10];
    unsigned tex_level_pitch[10];
    int tex_level_w[10];
    int tex_level_h[10];
    unsigned tex_max_level;

    int bgr;
    unsigned dst_bytespp;
    int min_x, max_x, min_y, max_y;
    float area;
    float inv_area;

    float tu_num_dx, tv_num_dx, tw_den_dx;
    float tu_num_dy, tv_num_dy, tw_den_dy;
    float tu_aff_dx, tv_aff_dx;
    float tu_aff_dy, tv_aff_dy;

    float iz_dx;
    float ia_dx;
    float ir_dx;
    float ig_dx;
    float ib_dx;
    float ifog_dx;
    float it0_dx;
    float it1_dx;
    float it2_dx;

    int common_fog565_fast;
    int common_texfog565_fast;
    float common_fog_r;
    float common_fog_g;
    float common_fog_b;

    float e0_dx;
    float e1_dx;
    float e2_dx;

    int use_row_span;
    unsigned span_ea[3];
    unsigned span_eb[3];
    float span_slope[3];
    float span_ymin[3];
    float span_ymax[3];
    int span_horizontal[3];
} sis6306_3d_prepared_t;

enum {
    SIS6306_ACCEL_WORK_2D = 1,
    SIS6306_ACCEL_WORK_3D = 2
};

typedef struct sis6306_accel_work_t {
    uint8_t  type;
    uint8_t  bpp;
    uint16_t reserved;
    uint32_t command;
    union {
        uint8_t reg2d[SIS6306_2D_REG_SIZE];
        sis6306_3d_state_t reg3d;
    } regs;
} sis6306_accel_work_t;

typedef struct sis6306_3d_work_t {
    sis6306_3d_prepared_t prepared;
} sis6306_3d_work_t;

#define SIS6306_ZWATCH_COUNT   16
#define SIS6306_ZWATCH_HISTORY 4
#define SIS6306_ZWATCH_HASH    64

typedef struct sis6306_zwriter_t {
    uint64_t triangle;
    uint32_t flip;
    uint32_t primitive;
    uint32_t enable;
    uint32_t z1;
    uint32_t zbase;
    uint32_t dst_base;
    uint32_t misc2;
    uint32_t tex1;
    uint32_t argb[3];
    uint32_t vz[3];
    uint32_t vx[3];
    uint32_t vy[3];
    uint16_t z;
    uint8_t  out_r;
    uint8_t  out_g;
    uint8_t  out_b;
    uint8_t  zmode;
    uint8_t  tex_en;
    uint8_t  blend_en;
    int8_t   blend_class;
    uint8_t  fog_en;
    uint8_t  valid;
} sis6306_zwriter_t;

typedef struct sis6306_zwatch_t {
    uint32_t zaddr;
    uint32_t armed_flip;
    uint64_t armed_triangle;
    uint64_t writes;
    uint64_t failures;
    uint16_t armed_zdst;
    uint16_t x;
    uint16_t y;
    uint8_t  valid;
    uint8_t  history_head;
    uint8_t  history_count;
    uint8_t  reports;

    uint8_t  life_active;
    uint8_t  life_finalized;
    uint8_t  life_final_valid;
    uint8_t  life_bgr;
    uint32_t life_flip;
    uint32_t life_color_addr;
    uint64_t life_owner_triangle;
    uint64_t life_candidates[5];
    uint64_t life_writes[5];
    uint64_t life_tex_candidates;
    uint64_t life_tex_writes;
    uint64_t life_blend_candidates;
    uint64_t life_blend_writes;
    uint64_t life_texblend_candidates;
    uint64_t life_texblend_writes;
    uint64_t life_tex_out_white;
    uint64_t life_tex_out_nonwhite;
    uint64_t life_zpassed;
    uint64_t life_zfailed;
    uint64_t life_out_white;
    uint64_t life_out_nonwhite;
    uint16_t life_owner_z;
    uint8_t  life_last_r;
    uint8_t  life_last_g;
    uint8_t  life_last_b;
    uint8_t  life_final_r;
    uint8_t  life_final_g;
    uint8_t  life_final_b;

    uint16_t pre_owner_z;
    uint8_t  pre_owner_r;
    uint8_t  pre_owner_g;
    uint8_t  pre_owner_b;
    uint8_t  pre_owner_valid;

    sis6306_zwriter_t history[SIS6306_ZWATCH_HISTORY];
} sis6306_zwatch_t;


typedef struct sis6306_t {
    svga_t svga;
    uint8_t pci_slot;
    uint8_t pci_command;
    uint8_t irq_line;

    uint32_t lfb_base;
    uint32_t mmio_base;
    uint32_t io_base;
    uint16_t io_active_base;

    mem_mapping_t lfb_mapping;
    mem_mapping_t mmio_mapping;

    uint8_t mmio_regs[0x10000];
    uint8_t io_regs[0x80];
    uint8_t agp_cmd[4];

    void *i2c;
    void *ddc;
    uint32_t ddc_reads;
    uint32_t ddc_writes;
    uint32_t panel_ddc_reads;
    uint32_t panel_ddc_writes;
    uint8_t  ddc_seen;
    uint8_t  panel_ddc_seen;

    uint8_t ext_seq[256];
    uint8_t ext_seq_unlocked;

    /* SiS530 SR38[1:0] selects one of three banks at SR13/SR2A/SR2B.
     * Bank 0: internal VCLK; bank 1: 25 MHz slot; bank 2: 28 MHz slot. */
    uint8_t vclk_regs[3][3];        /* [bank][SR13, SR2A, SR2B] */
    uint8_t vclk_written[3];        /* SR2A bit 0, SR2B bit 1 */

    uint8_t sis_crtc_index;
    uint8_t ext_crtc[256];
    uint8_t ext_crtc_unlocked;

    /* SiS segment-selection ports 3CDh/3CBh. */
    uint8_t segment0;
    uint8_t segment1;

    uint64_t engine_total;
    uint64_t engine_pixels;
    uint32_t engine_type_count[16];
    uint32_t engine_unsupported;
    uint32_t engine_srcsel_count[4];
    uint32_t engine_pat_count[4];
    uint32_t cursor_updates;


    uint32_t cpu_blt_skipped;
    uint32_t cpu_blt_replayed;


    uint32_t cursor_scan_count;
    uint32_t cursor_pattern_base;
    uint8_t  cursor_pattern_valid;

    /* initial SiS 530 3D setup-engine renderer. */
    uint64_t d3_triangle_total;
    uint64_t d3_triangle_pixels;
    uint32_t d3_triangle_unsupported;


    uint64_t d3_zwrite_fires;
    uint64_t d3_ztest_fires;
    uint64_t d3_alpha_write_fires;
    uint64_t d3_alpha_test_fires;
    uint64_t d3_alpha_buffer_fires;
    uint64_t d3_texture_fires;
    uint64_t d3_perspective_fires;
    uint64_t d3_texture_transparency_fires;
    uint64_t d3_specular_fires;
    uint64_t d3_fog_fires;
    uint64_t d3_blend_fires;
    uint64_t d3_transparency_fires;
    uint64_t d3_dither_fires;


    uint64_t d3_z_pixels_tested;
    uint64_t d3_z_pixels_passed;
    uint64_t d3_z_pixels_written;
    uint64_t d3_fog_pixels;


    uint64_t d3_tex_pixels;
    uint64_t d3_tex_bilinear_pixels;
    uint64_t d3_mip_pixels[10];
    uint64_t d3_mip_trilinear_pixels;
    uint64_t d3_mip_minified_pixels;
    uint64_t d3_blend_pixels;


    uint64_t d3_fire_width[8][3];
    uint64_t d3_bz_tested[3];
    uint64_t d3_bz_passed[3];
    uint64_t d3_bz_failed[3];
    uint64_t d3_bz_fail_delta[3][7];
    uint64_t d3_bz_fail_sampled[3];
    uint64_t d3_bz_fail_sample_white[3];
    uint64_t d3_bz_fail_sample_white_tex[3];
    uint64_t d3_bz_fail_sample_white_fog[3];


    sis6306_zwatch_t d3_zwatch[SIS6306_ZWATCH_COUNT];
    uint8_t  d3_zwatch_hash[SIS6306_ZWATCH_HASH];
    uint32_t d3_zwatch_count;
    uint64_t d3_zwatch_writes_total;
    uint64_t d3_zwatch_failures_total;
    uint64_t d3_zwatch_hits;
    uint64_t d3_zwatch_misses;

    uint32_t d3_wlife_armed;
    uint32_t d3_wlife_finalized;

    uint64_t d3_wsurf_candidate_tris;
    uint64_t d3_wsurf_candidate_pixels;
    uint64_t d3_wsurf_candidate_white;
    uint32_t d3_wsurf_watch_arms;

    uint64_t d3_wsurf_pre_seen;
    uint64_t d3_wsurf_pre_sampled;
    uint64_t d3_wsurf_pre_readfail;
    uint64_t d3_wsurf_pre_zclear;
    uint64_t d3_wsurf_pre_zoccupied;
    uint64_t d3_wsurf_pre_old_white;
    uint64_t d3_wsurf_pre_old_black;
    uint64_t d3_wsurf_pre_old_other;
    uint64_t d3_wsurf_pre_zclear_white;
    uint64_t d3_wsurf_pre_zclear_black;
    uint64_t d3_wsurf_pre_zclear_other;
    uint64_t d3_wsurf_pre_zoccupied_white;
    uint64_t d3_wsurf_pre_zoccupied_black;
    uint64_t d3_wsurf_pre_zoccupied_other;

    uint64_t d3_vp_mmio_seq;
    uint64_t d3_vp_write_count[6][3];
    uint64_t d3_vp_last_seq[6];
    uint64_t d3_vp_last_tri[6];
    uint32_t d3_vp_last_post[6];
    uint32_t d3_vp_last_value[6];
    uint16_t d3_vp_last_off[6];
    uint8_t  d3_vp_last_width[6];
    uint64_t d3_wsurf_vp_fresh[6];
    uint64_t d3_wsurf_vp_width4[6];
    uint64_t d3_wsurf_vp_all_fresh;
    uint64_t d3_wsurf_vp_all_width4;
    uint64_t d3_wsurf_vp_exact_white_ff;
    uint64_t d3_wsurf_vp_fire[8];


    uint64_t d3_fogdist_tri[4][3];
    uint64_t d3_fogdist_pix[4];
    uint64_t d3_fogdist_ff[4][6];
    uint64_t d3_fogdist_std_changed[4];
    uint64_t d3_fogdist_inv_changed[4];
    uint64_t d3_fogdist_std_white[4];
    uint64_t d3_fogdist_inv_white[4];
    uint64_t d3_fogdist_ff_sum[4];


    uint64_t d3_geom_last_tri[15];
    uint64_t d3_geom_last_seq[15];
    uint32_t d3_geom_last_post[15];
    uint8_t  d3_geom_last_width[15];
    uint64_t d3_state_last_tri[3];
    uint64_t d3_state_last_seq[3];
    uint32_t d3_state_last_post[3];
    uint8_t  d3_state_last_width[3];
    uint64_t d3_psetup_total[2];
    uint64_t d3_psetup_map_valid[2];
    uint64_t d3_psetup_ysorted[2];
    uint64_t d3_psetup_dir_side[2][2][2];
    uint64_t d3_psetup_vertex_coherent[2];
    uint64_t d3_psetup_vertex_fresh[2];
    uint64_t d3_psetup_vertex_mixed[2];


    uint64_t d3_mem_dst_tris;
    uint64_t d3_mem_z_tris;
    uint64_t d3_mem_tex_tris;
    uint64_t d3_mem_tex_local_tris;
    uint64_t d3_mem_tex_sysmem_tris;
    uint64_t d3_mem_tex_level_refs;
    uint32_t d3_mem_cfg_first;
    uint32_t d3_mem_cfg_last;
    uint32_t d3_mem_cfg_min;
    uint32_t d3_mem_cfg_max;
    uint8_t  d3_mem_sr0c_first;
    uint8_t  d3_mem_sr0c_last;
    uint8_t  d3_mem_cfg_seen;
    uint32_t d3_mem_dst_base[8];
    uint32_t d3_mem_dst_end[8];
    uint64_t d3_mem_dst_count[8];
    uint32_t d3_mem_dst_used;
    uint64_t d3_mem_dst_overflow;
    uint32_t d3_mem_z_base[8];
    uint32_t d3_mem_z_end[8];
    uint64_t d3_mem_z_count[8];
    uint32_t d3_mem_z_used;
    uint64_t d3_mem_z_overflow;
    uint32_t d3_mem_tex_base[32];
    uint32_t d3_mem_tex_end[32];
    uint64_t d3_mem_tex_count[32];
    uint32_t d3_mem_tex_used;
    uint64_t d3_mem_tex_overflow;
    uint32_t d3_mem_tex_min_base;
    uint32_t d3_mem_tex_max_base;
    uint32_t d3_mem_tex_max_end;
    uint32_t d3_mem_tex_last_base;
    uint32_t d3_mem_tex_last_end;
    uint8_t  d3_mem_tex_last_sysmem;
    uint8_t  d3_mem_tex_last_valid;


    uint64_t d3_z_tex_tested;
    uint64_t d3_z_tex_passed;
    uint64_t d3_z_blend_tested;
    uint64_t d3_z_blend_passed;
    uint64_t d3_z_texblend_tested;
    uint64_t d3_z_texblend_passed;
    uint64_t d3_z_overlay_fail_p1;
    uint64_t d3_z_overlay_fail_p2_4;
    uint64_t d3_z_overlay_fail_p5_16;
    uint64_t d3_z_overlay_fail_gt16;


    uint64_t d3_tex_old_white;
    uint64_t d3_tex_old_white_out_white;
    uint64_t d3_tex_old_white_out_nonwhite;
    uint64_t d3_tex_out_white;
    uint64_t d3_tex_out_nonwhite;
    uint64_t d3_blend_old_white;
    uint64_t d3_blend_old_white_out_white;
    uint64_t d3_blend_old_white_out_nonwhite;
    uint64_t d3_tex_sample_r_sum;
    uint64_t d3_tex_sample_g_sum;
    uint64_t d3_tex_sample_b_sum;

    uint64_t d3_unsup_core;
    uint64_t d3_unsup_zstate;
    uint64_t d3_unsup_fogmode;
    uint64_t d3_unsup_blendmode;
    uint64_t d3_unsup_tex_sysmem;
    uint64_t d3_unsup_tex_format;
    uint64_t d3_unsup_tex_blend;
    uint64_t d3_unsup_tex_pitch;
    uint64_t d3_unsup_tex_base;
    uint64_t d3_unsup_tex_size;
    uint64_t d3_unsup_tex_map;
    uint64_t d3_unsup_persp_without_tex;
    uint64_t d3_unsup_dst_format;
    uint64_t d3_unsup_nan;
    uint64_t d3_unsup_shade;

    uint64_t d3_tex_format_seen[256];

    /* Guest-visible 3D timing queue.  Submission and TSTATUS reads both
     * happen on the main emulation thread, so these indices do not need
     * atomics.  The host raster worker remains asynchronous and independent. */
    uint64_t *r180_finish_tsc;
    uint32_t r180_timing_write;
    uint32_t r180_timing_read;
    uint64_t r180_last_finish_tsc;
    uint64_t r181_virtual_tsc;

    uint8_t              accel_regs[0x10000];
    sis6306_accel_work_t *accel_queue;
    ATOMIC_UINT          accel_write_idx;
    ATOMIC_UINT          accel_read_idx;
    ATOMIC_INT           accel_busy;
    volatile int         accel_thread_run;
    uint8_t              accel_bpp;
    uint8_t              d3_render_threads;
    uint8_t              d2_render_threads;
    uint8_t              d2_active_threads;
    uint32_t             d2_command;

    volatile int         d2_thread_run[SIS6306_MAX_RENDER_THREADS];
    thread_t            *d2_thread[SIS6306_MAX_RENDER_THREADS];
    event_t             *d2_wake_event[SIS6306_MAX_RENDER_THREADS];
    event_t             *d2_done_event[SIS6306_MAX_RENDER_THREADS];
    uint32_t             d2_changed_lo[SIS6306_MAX_RENDER_THREADS];
    uint32_t             d2_changed_hi[SIS6306_MAX_RENDER_THREADS];

    uint8_t              accel_needs_sync;

    thread_t            *accel_thread;
    event_t             *accel_wake_event;
    event_t             *accel_not_full_event;
    event_t             *accel_idle_event;

    sis6306_3d_work_t   *d3_queue;
    ATOMIC_UINT          d3_write_idx;
    ATOMIC_UINT          d3_read_idx[SIS6306_MAX_RENDER_THREADS];
    ATOMIC_INT           d3_busy[SIS6306_MAX_RENDER_THREADS];
    volatile int         d3_thread_run[SIS6306_MAX_RENDER_THREADS];
    thread_t            *d3_thread[SIS6306_MAX_RENDER_THREADS];
    event_t             *d3_wake_event[SIS6306_MAX_RENDER_THREADS];
    event_t             *d3_not_full_event[SIS6306_MAX_RENDER_THREADS];
    event_t             *d3_idle_event[SIS6306_MAX_RENDER_THREADS];

    /* SiS extended screen-start / fast page-flip state. */
    uint32_t fast_page_start;
    uint32_t fast_page_flip_count;
    uint8_t  fast_page_valid;
} sis6306_t;

static void sis6306_accel_sync(sis6306_t *dev);


static video_timings_t timing_sis6306 = {
    .type = VIDEO_AGP,
    .write_b = 2, .write_w = 2, .write_l = 1,
    .read_b = 10, .read_w = 10, .read_l = 10
};

static inline int
sis6306_packed_graphics(const sis6306_t *dev)
{
    return !!(dev->ext_seq[0x06] & 0x1e);
}

static inline int
sis6306_enhanced_window_addr(sis6306_t *dev, uint32_t addr, int write,
                             uint32_t *vram_addr)
{
    svga_t   *svga   = &dev->svga;
    uint32_t  legacy = addr & 0x1ffff;
    uint32_t  offset;

    if (!sis6306_packed_graphics(dev) || (legacy >= 0x10000))
        return 0;

    offset = legacy + (write ? svga->write_bank : svga->read_bank);
    if (offset >= svga->vram_max) {
        *vram_addr = 0xffffffff;
        return 1;
    }

    *vram_addr = offset & svga->vram_mask;
    return 1;
}

static uint8_t
sis6306_legacy_readb_nosync(sis6306_t *dev, uint32_t addr)
{
    uint32_t vram_addr;

    if (!sis6306_enhanced_window_addr(dev, addr, 0, &vram_addr))
        return svga_read(addr, &dev->svga);

    if (vram_addr == 0xffffffff)
        return 0xff;

    return dev->svga.vram[vram_addr];
}

static uint8_t
sis6306_legacy_readb(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;

    sis6306_accel_sync(dev);
    return sis6306_legacy_readb_nosync(dev, addr);
}

static inline int
sis6306_enhanced_window_span(sis6306_t *dev, uint32_t addr, int write,
                             unsigned bytes, uint32_t *vram_addr)
{
    svga_t *svga = &dev->svga;
    uint32_t legacy = addr & 0x1ffff;
    uint32_t offset;

    if (!sis6306_packed_graphics(dev) || !bytes ||
        (legacy >= 0x10000) || (bytes > (0x10000U - legacy)))
        return 0;

    offset = legacy + (write ? svga->write_bank : svga->read_bank);
    if ((offset >= svga->vram_max) || (bytes > (svga->vram_max - offset)))
        return 0;

    *vram_addr = offset & svga->vram_mask;
    if (bytes > (svga->vram_max - *vram_addr))
        return 0;
    return 1;
}

static uint16_t
sis6306_legacy_readw(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint32_t vram_addr;

    /* Standard-VGA word accesses must not race queued Win9x 2D/3D
     * work during a transition into DOS/VGA modes. */
    sis6306_accel_sync(dev);

    if (!sis6306_packed_graphics(dev))
        return svga_readw(addr, &dev->svga);

    if (sis6306_enhanced_window_span(dev, addr, 0, 2, &vram_addr)) {
        const uint8_t *p = &dev->svga.vram[vram_addr];
        return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
    }

    return sis6306_legacy_readb_nosync(dev, addr) |
           ((uint16_t) sis6306_legacy_readb_nosync(dev, addr + 1) << 8);
}

static uint32_t
sis6306_legacy_readl(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint32_t vram_addr;

    sis6306_accel_sync(dev);

    if (!sis6306_packed_graphics(dev))
        return svga_readl(addr, &dev->svga);

    if (sis6306_enhanced_window_span(dev, addr, 0, 4, &vram_addr)) {
        const uint8_t *p = &dev->svga.vram[vram_addr];
        return (uint32_t) p[0] |
               ((uint32_t) p[1] << 8) |
               ((uint32_t) p[2] << 16) |
               ((uint32_t) p[3] << 24);
    }

    return sis6306_legacy_readb_nosync(dev, addr) |
           ((uint32_t) sis6306_legacy_readb_nosync(dev, addr + 1) << 8) |
           ((uint32_t) sis6306_legacy_readb_nosync(dev, addr + 2) << 16) |
           ((uint32_t) sis6306_legacy_readb_nosync(dev, addr + 3) << 24);
}

static void
sis6306_legacy_writeb_nosync(sis6306_t *dev, uint32_t addr, uint8_t val)
{
    uint32_t vram_addr;

    if (!sis6306_enhanced_window_addr(dev, addr, 1, &vram_addr)) {
        svga_write(addr, val, &dev->svga);
        return;
    }

    if (vram_addr == 0xffffffff)
        return;

    dev->svga.vram[vram_addr] = val;
    dev->svga.changedvram[vram_addr >> 12] =
        dev->svga.monitor->mon_changeframecount;
}

static inline int
sis6306_standard_chain4_fast_eligible(const sis6306_t *dev)
{
    const svga_t *svga = &dev->svga;

    if (sis6306_packed_graphics(dev))
        return 0;
    if (!(svga->gdcreg[0x06] & 0x01))
        return 0; /* Text/alpha mode */
    if (!svga->chain4 || svga->packed_chain4 || svga->force_old_addr ||
        svga->fb_only)
        return 0;
    if (svga->writemode != 0)
        return 0;
    if (svga->writemask != 0x0f)
        return 0;
    if (svga->gdcreg[0x08] != 0xff)
        return 0;
    if (svga->gdcreg[0x03] & 0x1f)
        return 0; /* Rotate or logical operation */
    if (svga->gdcreg[0x01])
        return 0; /* Enable set/reset */
    if (svga->adv_flags &
        (FLAG_ADDR_BY8 | FLAG_ADDR_BY16 | FLAG_EXT_WRITE | FLAG_LATCH8))
        return 0;
    if (svga->translate_address || svga->xga)
        return 0;

    if (((svga->gdcreg[0x06] >> 2) & 3) > 1)
        return 0;

    return 1;
}

static inline int
sis6306_standard_chain4_translate_write(svga_t *svga, uint32_t cpu_addr,
                                         uint32_t *vram_addr)
{
    uint32_t addr = cpu_addr & 0x1ffffU;
    const unsigned memory_map_mode = (svga->gdcreg[0x06] >> 2) & 3;
    const uint32_t plane = addr & 3U;

    /*
     * Mirror svga_decode_addr() for the two map modes accepted above.
     * Mode 0: 128K at A0000.
     * Mode 1:  64K at A0000.
     */
    if ((memory_map_mode == 1) && (addr >= 0x10000U))
        return 0;

    addr += svga->write_bank;

    /*
     * Mirror the generic non-packed chain-4 address transform used by
     * svga_write_common():
     *
     *   addr &= ~3;
     *   addr = ((addr & 0xfffc) << 2) |
     *          ((addr & 0x30000) >> 14) |
     *          (addr & ~0x3ffff);
     *   store at addr | plane.
     */
    addr &= ~3U;
    addr = ((addr & 0xfffcU) << 2) |
           ((addr & 0x30000U) >> 14) |
           (addr & ~0x3ffffU);

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0;

    addr &= svga->vram_mask;
    *vram_addr = addr | plane;
    return 1;
}

static inline int
sis6306_standard_chain4_contiguous_span(svga_t *svga, uint32_t addr,
                                        unsigned bytes, uint32_t *vram_addr)
{
    uint32_t va;
    uint32_t offset = addr & 0x1ffffU;
    const unsigned map = (svga->gdcreg[0x06] >> 2) & 3;

    /* Only aligned word / dword accesses can remain in one VGA chain-4
     * four-plane group (or a 2-byte sub-group).  Never cross a VGA aperture,
     * decoded VRAM limit, VRAM wrapping point, or dirty page boundary. */
    if ((addr & (bytes - 1U)) || ((bytes != 2) && (bytes != 4)))
        return 0;
    if ((map == 1) && ((offset + bytes) > 0x10000U))
        return 0;
    if ((offset + bytes) > 0x20000U)
        return 0;
    if (!sis6306_standard_chain4_translate_write(svga, addr, &va))
        return 0;
    if (svga->vram_max < bytes || svga->vram_mask < (bytes - 1U))
        return 0;
    if (va > (svga->vram_max - bytes) ||
        va > (svga->vram_mask - (bytes - 1U)))
        return 0;
    if ((va >> 12) != ((va + bytes - 1U) >> 12))
        return 0;

    /* Translation of the aligned base followed by VGA plane selection yields
     * exactly va..va+bytes-1 for these widths and alignment conditions. */
    *vram_addr = va;
    return 1;
}

static inline void
sis6306_standard_chain4_fast_write(sis6306_t *dev, uint32_t addr,
                                    uint32_t val, unsigned bytes)
{
    svga_t *svga = &dev->svga;
    uint32_t va;

    /* Preserve generic chain-4 guest timing whether the host uses a
     * contiguous copy or individual plane writes. */
    cycles -= svga->monitor->mon_video_timing_write_b * (int) bytes;

    if ((bytes == 4) &&
        sis6306_standard_chain4_contiguous_span(svga, addr, 4, &va)) {
        const uint8_t little[4] = {
            (uint8_t) val,
            (uint8_t) (val >> 8),
            (uint8_t) (val >> 16),
            (uint8_t) (val >> 24)
        };
        memcpy(&svga->vram[va], little, 4);
        svga->changedvram[va >> 12] =
            svga->monitor->mon_changeframecount;
        return;
    }
    if ((bytes == 2) &&
        sis6306_standard_chain4_contiguous_span(svga, addr, 2, &va)) {
        const uint8_t little[2] = {
            (uint8_t) val, (uint8_t) (val >> 8)
        };
        memcpy(&svga->vram[va], little, 2);
        svga->changedvram[va >> 12] =
            svga->monitor->mon_changeframecount;
        return;
    }


    /* Preserve precise map/decode/wrap and dirty-page behaviour for
     * unaligned or noncontiguous writes. */
    for (unsigned i = 0; i < bytes; i++) {
        uint32_t byte_addr;
        if (!sis6306_standard_chain4_translate_write(svga, addr + i,
                                                      &byte_addr))
            continue;
        svga->vram[byte_addr] = (uint8_t) (val >> (i * 8));
        svga->changedvram[byte_addr >> 12] =
            svga->monitor->mon_changeframecount;
    }
}

static void
sis6306_legacy_writeb(uint32_t addr, uint8_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;

    sis6306_accel_sync(dev);

    if (sis6306_standard_chain4_fast_eligible(dev)) {
        sis6306_standard_chain4_fast_write(dev, addr, val, 1);
        return;
    }


    sis6306_legacy_writeb_nosync(dev, addr, val);
}

static inline void
sis6306_legacy_mark_span_changed(sis6306_t *dev, uint32_t addr, unsigned bytes)
{
    const uint32_t first_page = addr >> 12;
    const uint32_t last_page = (addr + bytes - 1U) >> 12;

    dev->svga.changedvram[first_page] = dev->svga.monitor->mon_changeframecount;
    if (last_page != first_page)
        dev->svga.changedvram[last_page] = dev->svga.monitor->mon_changeframecount;
}

static void
sis6306_legacy_writew(uint32_t addr, uint16_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint32_t vram_addr;

    sis6306_accel_sync(dev);

    if (!sis6306_packed_graphics(dev)) {
        if (sis6306_standard_chain4_fast_eligible(dev)) {
            sis6306_standard_chain4_fast_write(dev, addr, val, 2);
            return;
        }


        svga_writew(addr, val, &dev->svga);
        return;
    }

    if (sis6306_enhanced_window_span(dev, addr, 1, 2, &vram_addr)) {
        uint8_t *p = &dev->svga.vram[vram_addr];
        p[0] = (uint8_t) val;
        p[1] = (uint8_t) (val >> 8);
        sis6306_legacy_mark_span_changed(dev, vram_addr, 2);
        return;
    }

    sis6306_legacy_writeb_nosync(dev, addr, (uint8_t) val);
    sis6306_legacy_writeb_nosync(dev, addr + 1, (uint8_t) (val >> 8));
}

static void
sis6306_legacy_writel(uint32_t addr, uint32_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint32_t vram_addr;

    sis6306_accel_sync(dev);

    if (!sis6306_packed_graphics(dev)) {
        if (sis6306_standard_chain4_fast_eligible(dev)) {
            sis6306_standard_chain4_fast_write(dev, addr, val, 4);
            return;
        }


        svga_writel(addr, val, &dev->svga);
        return;
    }

    if (sis6306_enhanced_window_span(dev, addr, 1, 4, &vram_addr)) {
        uint8_t *p = &dev->svga.vram[vram_addr];
        p[0] = (uint8_t) val;
        p[1] = (uint8_t) (val >> 8);
        p[2] = (uint8_t) (val >> 16);
        p[3] = (uint8_t) (val >> 24);
        sis6306_legacy_mark_span_changed(dev, vram_addr, 4);
        return;
    }

    sis6306_legacy_writeb_nosync(dev, addr, (uint8_t) val);
    sis6306_legacy_writeb_nosync(dev, addr + 1, (uint8_t) (val >> 8));
    sis6306_legacy_writeb_nosync(dev, addr + 2, (uint8_t) (val >> 16));
    sis6306_legacy_writeb_nosync(dev, addr + 3, (uint8_t) (val >> 24));
}


static void
sis6306_update_banks(sis6306_t *dev)
{
    svga_t *svga = &dev->svga;

    /*
     * SR0B bit 3 selects dual-segment mode.
     *
     * Dual-segment mode:
     *   3CDh bits 6:0 = write segment
     *   3CBh bits 6:0 = read segment
     *
     * Single-segment mode:
     *   3CDh bits 7:4 = write segment
     *   3CDh bits 3:0 = read segment
     *
     * Each segment selects a 64 KiB bank in the legacy A0000h aperture.
     */
    if (dev->ext_seq[0x0b] & 0x08) {
        svga->write_bank = (uint32_t) (dev->segment0 & 0x7f) << 16;
        svga->read_bank  = (uint32_t) (dev->segment1 & 0x7f) << 16;
    } else {
        svga->write_bank = (uint32_t) ((dev->segment0 >> 4) & 0x0f) << 16;
        svga->read_bank  = (uint32_t) (dev->segment0 & 0x0f) << 16;
    }
}

static uint32_t
sis6306_cursor_color(const sis6306_t *dev, int which)
{
    const int base = which ? 0x17 : 0x14;
    return makecol32(video_6to8[dev->ext_seq[base] & 0x3f],
                     video_6to8[dev->ext_seq[base + 1] & 0x3f],
                     video_6to8[dev->ext_seq[base + 2] & 0x3f]);
}

static uint32_t
sis6306_configured_vram_size(const sis6306_t *dev)
{
    const unsigned code = ((dev->ext_seq[0x0c] & 0x10) >> 2) |
                          ((dev->ext_seq[0x0c] & 0x06) >> 1);
    uint32_t size;

    switch (code) {
        case 0x0: size = 1U << 20; break;
        case 0x1:
        case 0x5: size = 2U << 20; break;
        case 0x2:
        case 0x6: size = 4U << 20; break;
        case 0x7: size = 8U << 20; break;
        default:  size = dev->svga.vram_max; break;
    }

    if (!size || (size > dev->svga.vram_max))
        size = dev->svga.vram_max;
    return size;
}

static uint32_t
sis6306_cursor_choose_base(sis6306_t *dev)
{
    const uint32_t mem_size = sis6306_configured_vram_size(dev);
    const uint32_t pat = (dev->ext_seq[0x1e] >> 4) & 0x0f;
    uint32_t base = 0;

    /*
     * The SiS Win9x driver reserves the final 16 KiB of configured display
     * memory for sixteen 1 KiB 64x64x2 cursor patterns.
     */
    if (mem_size >= 0x4000U)
        base = (mem_size - 0x4000U) + (pat << 10);
    base &= dev->svga.vram_mask;

    dev->cursor_pattern_base = base;
    dev->cursor_pattern_valid = (mem_size >= 0x4000U) &&
                                ((base + 1024U) <= dev->svga.vram_max);


    return base;
}

static void
sis6306_update_cursor(sis6306_t *dev)
{
    svga_t *svga = &dev->svga;
    uint32_t base;

    svga->hwcursor.ena       = !!(dev->ext_seq[0x06] & 0x40);
    svga->hwcursor.cur_xsize = 64;
    svga->hwcursor.cur_ysize = 64;
    if (dev->ext_seq[0x1b] & 0x80) {
        /*
         * In MMIO cursor mode Win9x updates 32-bit cursor position registers
         * at 85F8h (X) and 85FCh (Y).  Position is in the low 12 bits; upper
         * bits are control/off-screen state and must not become coordinates.
         */
        uint32_t mmio_x = (uint32_t) dev->mmio_regs[0x85f8] |
                          ((uint32_t) dev->mmio_regs[0x85f9] << 8) |
                          ((uint32_t) dev->mmio_regs[0x85fa] << 16) |
                          ((uint32_t) dev->mmio_regs[0x85fb] << 24);
        uint32_t mmio_y = (uint32_t) dev->mmio_regs[0x85fc] |
                          ((uint32_t) dev->mmio_regs[0x85fd] << 8) |
                          ((uint32_t) dev->mmio_regs[0x85fe] << 16) |
                          ((uint32_t) dev->mmio_regs[0x85ff] << 24);
        svga->hwcursor.x = mmio_x & 0x0fff;
        svga->hwcursor.y = mmio_y & 0x0fff;
    } else {
        svga->hwcursor.x = dev->ext_seq[0x1a] |
                           ((dev->ext_seq[0x1b] & 0x0f) << 8);
        svga->hwcursor.y = dev->ext_seq[0x1d] |
                           ((dev->ext_seq[0x1e] & 0x07) << 8);
    }

    if (svga->interlace)
        svga->hwcursor.y <<= 1;
    svga->hwcursor.xoff      = dev->ext_seq[0x1c] & 0x3f;
    svga->hwcursor.yoff      = dev->ext_seq[0x1f] & 0x3f;

    /*
     * The Win9x driver lays the sixteen hardware-cursor patterns out in the
     * final 16 KiB of the configured framebuffer; SR1E[7:4] selects a 1 KiB
     * slot.
     */
    base = sis6306_cursor_choose_base(dev);
    svga->hwcursor.addr = (base + (uint32_t) svga->hwcursor.yoff * 16) &
                          svga->vram_mask;
    svga->fullchange = svga->monitor->mon_changeframecount;

}

static void
sis6306_hwcursor_draw_row(svga_t *svga, int displine, uint32_t addr,
                          int xbase, uint32_t col0, uint32_t col1)
{
    uint8_t row[16];

    if ((displine < 0) || (displine >= 2048) ||
        !svga->monitor->target_buffer ||
        !svga->monitor->target_buffer->line[displine])
        return;

    for (int i = 0; i < 16; i++)
        row[i] = svga->vram[(addr + i) & svga->vram_mask];

    for (int x = 0; x < 64; x++) {
        const int group = x >> 3;
        const int within = x & 7;
        const uint16_t bits = ((uint16_t) row[group * 2] << 8) |
                              row[group * 2 + 1];
        const int shift = 14 - (within * 2);
        const int and_bit = (bits >> (shift + 1)) & 1;
        const int xor_bit = (bits >> shift) & 1;
        const int dx = xbase + x;

        if ((dx < 0) || (dx >= 2048))
            continue;

        /* Standard monochrome AND/XOR cursor truth table. */
        if (!and_bit)
            svga->monitor->target_buffer->line[displine][dx] = xor_bit ? col1 : col0;
        else if (xor_bit)
            svga->monitor->target_buffer->line[displine][dx] ^= 0x00ffffff;
    }
}

static void
sis6306_hwcursor_draw(svga_t *svga, int displine)
{
    sis6306_t *dev = (sis6306_t *) svga->priv;
    const uint32_t col0 = sis6306_cursor_color(dev, 0);
    const uint32_t col1 = sis6306_cursor_color(dev, 1);
    const int xbase = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff + svga->x_add;
    const uint32_t base_addr = svga->hwcursor_latch.addr;

    if (!dev->cursor_pattern_valid)
        return;

    if (svga->interlace) {
        const uint32_t addr0 = base_addr;
        const uint32_t addr1 = (base_addr + 16) & svga->vram_mask;

        if (svga->hwcursor_oddeven) {
            sis6306_hwcursor_draw_row(svga, displine, addr1, xbase, col0, col1);
            sis6306_hwcursor_draw_row(svga, displine - 1, addr0, xbase, col0, col1);
        } else {
            sis6306_hwcursor_draw_row(svga, displine, addr0, xbase, col0, col1);
            sis6306_hwcursor_draw_row(svga, displine + 1, addr1, xbase, col0, col1);
        }

        svga->hwcursor_latch.addr = (base_addr + 32) & svga->vram_mask;
    } else {
        sis6306_hwcursor_draw_row(svga, displine, base_addr, xbase, col0, col1);
        svga->hwcursor_latch.addr = (base_addr + 16) & svga->vram_mask;
    }
}

/*
 * The SiS driver uses a true interlaced 1024x768 mode on this monitor (PB A520).  The
 * generic SVGA core presents one field at each refresh, which is correct at
 * the signal level but leaves moving objects visibly striped on a progressive
 * host display: the other parity still contains the previous field.
 * Windows programs the extended CRTC registers in separate writes during a
 * high-resolution mode switch.  Intermediate timing values can request
 * scanlines outside the host bitmap.  Guard SiS packed-pixel scanout before
 * invoking the generic SVGA renderers; other adapters are unaffected.
 */
static int
sis6306_packed_scanout_unsafe(svga_t *svga)
{
    bitmap_t *target = svga->monitor->target_buffer;
    const int64_t y = (int64_t) svga->displine + svga->y_add;
    const int64_t x = svga->x_add;
    const int64_t end = x + (int64_t) svga->hdisp +
                        (int64_t) svga->scrollcache + 32;
    const int unsafe = !target || !target->dat || y < 0 ||
                       y >= target->h || y >= 2112 ||
                       x < 0 || x >= target->w ||
                       svga->hdisp == 0 || end > target->w ||
                       end <= x || !target->line[y];

    return unsafe;
}

static void
sis6306_render_packed_base(svga_t *svga)
{
    if (sis6306_packed_scanout_unsafe(svga))
        return;

    switch (svga->bpp) {
        case 32:
            svga_render_32bpp_highres(svga);
            break;
        case 24:
            svga_render_24bpp_highres(svga);
            break;
        case 16:
            svga_render_16bpp_highres(svga);
            break;
        case 15:
            svga_render_15bpp_highres(svga);
            break;
        default:
            svga_render_8bpp_highres(svga);
            break;
    }
}

static void
sis6306_render_interlaced_weave(svga_t *svga)
{
    const uint32_t saved_memaddr = svga->memaddr;
    const int      saved_displine = svga->displine;
    const uint32_t row_bytes = (svga->adv_flags & FLAG_NO_SHIFT3) ?
                               svga->rowoffset : (svga->rowoffset << 3);

    /* Render the field line the generic SVGA core requested. */
    sis6306_render_packed_base(svga);

    /*
     * Also render the opposite field's adjacent source row.  Even field rows
     * pair with the following odd row; odd field rows pair with the preceding
     * even row.  Restore the core's address/line state afterwards so its
     * interlace timing and row stepping remain untouched.
     */
    svga->memaddr = (saved_memaddr + ((saved_displine & 1) ?
                     (0u - row_bytes) : row_bytes)) & svga->vram_display_mask;
    svga->displine = saved_displine ^ 1;
    sis6306_render_packed_base(svga);

    svga->memaddr = saved_memaddr;
    svga->displine = saved_displine;
}

static uint32_t
sis6306_normal_screen_start(const sis6306_t *dev)
{
    const svga_t *svga = &dev->svga;
    uint32_t start;

    /*
     * SiS extends the standard VGA CRTC start address to 21 bits:
     *   CR0C/CR0D       bits 15:0
     *   SR27.D3:D0      bits 19:16
     *   SR26.D0         bit 20
     *
     * Keep the value in the generic SVGA core's native start-address units;
     * svga_poll() applies the normal <<2 conversion at frame start.
     */
    start = ((uint32_t) (dev->ext_seq[0x26] & 0x01) << 20) |
            ((uint32_t) (dev->ext_seq[0x27] & 0x0f) << 16) |
            ((uint32_t) svga->crtc[0x0c] << 8) |
            (uint32_t) svga->crtc[0x0d];

    start += (svga->crtc[8] & 0x60) >> 5;
    return start & 0x1fffff;
}

static void
sis6306_update_display_start(sis6306_t *dev)
{
    svga_t *svga = &dev->svga;

    if ((dev->ext_seq[0x2f] & 0x10) && dev->fast_page_valid)
        svga->memaddr_latch = dev->fast_page_start & 0x1fffff;
    else
        svga->memaddr_latch = sis6306_normal_screen_start(dev);
}

static void
sis6306_fast_page_flip_latch(sis6306_t *dev)
{
    if (!(dev->ext_seq[0x2f] & 0x10))
        return;

    /*
     * SR30/SR31 supply bits 15:0 and SR32.D3:D0 bits 19:16.
     * The manual's SR32.D4 description is a typographical truncation; it is
     * the only remaining bit and extends the address to bit 20, matching the
     * normal SR26/SR27 21-bit screen-start field and the 8 MB aperture.
     * Writing SR32 latches the complete page-flip address.
     */
    dev->fast_page_start =
        ((uint32_t) (dev->ext_seq[0x32] & 0x1f) << 16) |
        ((uint32_t) dev->ext_seq[0x31] << 8) |
        (uint32_t) dev->ext_seq[0x30];
    dev->fast_page_valid = 1;
    dev->svga.memaddr_latch = dev->fast_page_start;
    dev->svga.fullchange = changeframecount;
    dev->fast_page_flip_count++;
}

/*
 * SR38.D2 disables the VGA line-compare function on the SiS530.  The
 * standard VGA line-compare counter is only 10 bits wide, and its disabled
 * (all-ones) programming still matches at line 1024.  In 1200-line modes,
 * that would reset the scanout memory address and repeat the top of the
 * desktop instead of displaying the final 176 lines.
 */
static int
sis6306_line_compare(svga_t *svga)
{
    const sis6306_t *dev = (const sis6306_t *) svga->priv;
    return !(dev->ext_seq[0x38] & 0x04);
}

/*
 * SiS530 internal dual-clock synthesizer (datasheet 5.7.2.4 and
 * SR13/SR2A/SR2B/SR38). The extended VCLK registers are banked for the
 * original 25 MHz, 28 MHz and programmable internal video clock slots.
 */
static double
sis6306_vclk_hz(const sis6306_t *dev, unsigned bank)
{
    const uint8_t *r = dev->vclk_regs[bank];
    const unsigned int numer = (r[1] & 0x7f) + 1;
    const unsigned int denom = (r[2] & 0x1f) + 1;
    unsigned int postscale = ((r[2] >> 5) & 3) + 1;
    const unsigned int divider = (r[1] & 0x80) ? 2 : 1;

    if ((dev->vclk_written[bank] & 3) != 3)
        return 0.0;

    if (r[0] & 0x40) {
        /* Extended post-scale codes 10 and 11 denote /6 and /8.
         * Codes 00 and 01 are reserved. */
        if (postscale < 3)
            return 0.0;
        postscale = (postscale == 3) ? 6 : 8;
    }

    return 14318180.0 * (double) numer / (double) denom *
           (double) divider / (double) postscale;
}

static void
sis6306_recalctimings(svga_t *svga)
{
    sis6306_t *dev = (sis6306_t *) svga->priv;
    const uint8_t mode = dev->ext_seq[0x06];
    const int packed = !!(mode & 0x1e);
    const unsigned int clk_sel = (svga->miscout >> 2) & 3;
    const unsigned int bank = (clk_sel == 3) ? 0 : (clk_sel + 1);

    if (bank < 3) {
        const double hz = sis6306_vclk_hz(dev, bank);
        if (hz > 0.0)
            svga->clock = (cpuclock * (double) (1ULL << 32)) / hz;
    }

    /* The generic VGA timing counters are limited to 10 vertical bits and
     * 8 horizontal character-clock bits.  SiS530 extends these in SRA/SR0A
     * and SR12.  Preserve the generic +1/+2 VGA register adjustments made
     * by svga_recalctimings() and apply only the missing high bits here.
     *
     * Without SR0A[3:0], 1280x1024 has its retrace/blank/total counters
     * truncated, and 1600x1200 also loses its display end.  At 1600x1200,
     * SR12[0] is required for a horizontal total over 2047 pixels.
     */
    const uint8_t vext = dev->ext_seq[0x0a];
    const uint8_t hext = dev->ext_seq[0x12];

    svga->hoverride = packed && !!(hext & 0x01);

    if (vext & 0x01)
        svga->vtotal += 0x400;
    if (vext & 0x02)
        svga->dispend += 0x400;
    if (vext & 0x04)
        svga->vblankstart += 0x400;
    if (vext & 0x08)
        svga->vsyncstart += 0x400;

    if (hext & 0x01)
        svga->htotal += 0x100;
    if (hext & 0x02) {
        svga->hdisp_time += 0x100;
        svga->hdisp += 0x100 * svga->dots_per_clock;
    }
    if (hext & 0x04)
        svga->hblankstart += 0x100;
    /* SR12[3] extends horizontal retrace start. */
    if (hext & 0x10) {
        svga->hblank_end_val |= 0x40;
        svga->hblank_end_mask = 0x7f;
    }

    /* SR06.D5 is the SiS interlace enable.  The generic SVGA core handles
       field stepping once this flag is exposed to it. */
    svga->interlace = packed && !!(mode & 0x20);

    /*
     * CR13 contains screen-offset bits 7:0 and SRA/SR0A bits 7:4 extend
     * that value.  In interlaced SiS modes the programmed offset describes
     * the two-field stride; svga_poll() already advances two row offsets per
     * displayed line, so halve the callback row offset here.
     */
    if (packed) {
        svga->rowoffset = (uint32_t) svga->crtc[0x13] |
                          ((uint32_t) (dev->ext_seq[0x0a] & 0xf0) << 4);
        if (svga->interlace)
            svga->rowoffset >>= 1;
    }

    /*
     * The Win9x SiS530 driver selects 32-bpp by setting SR09.D7 while
     * leaving SR06 in enhanced-graphics mode (02/82/C2), rather than also
     * asserting SR06.D4.  Treat SR09.D7 as the 32-bpp override whenever a
     * packed graphics mode is active.
     */
    if (packed && (dev->ext_seq[0x09] & 0x80)) {
        svga->bpp    = 32;
        svga->render = svga->interlace ? sis6306_render_interlaced_weave : sis6306_render_packed_base;
        svga->rowcount = 0;
    } else if (mode & 0x10) {
        svga->bpp    = 24;
        svga->render = svga->interlace ? sis6306_render_interlaced_weave : sis6306_render_packed_base;
        svga->rowcount = 0;
    } else if (mode & 0x08) {
        svga->bpp       = 16;
        svga->render    = svga->interlace ? sis6306_render_interlaced_weave : sis6306_render_packed_base;
        svga->rowcount  = 0;
    } else if (mode & 0x04) {
        svga->bpp       = 15;
        svga->render    = svga->interlace ? sis6306_render_interlaced_weave : sis6306_render_packed_base;
        svga->rowcount  = 0;
    } else if (mode & 0x02) {
        svga->bpp       = 8;
        svga->map8      = svga->pallook;
        svga->render    = svga->interlace ? sis6306_render_interlaced_weave : sis6306_render_packed_base;
        /* Enhanced packed 8-bpp bypasses the VGA AR12 planar mask. */
        svga->plane_mask = 0x0f;
        svga->rowcount   = 0;
    } else {
        /* Standard VGA uses 8-bpp pixel interpretation. */
        svga->bpp        = 8;
        svga->plane_mask = svga->attrregs[0x12] & 0x0f;
    }

    /* Restore SiS extended/fast page-flip start bits after generic timing recalc. */
    sis6306_update_display_start(dev);
}

static void
sis6306_update_graphics_mode(sis6306_t *dev)
{
    const int packed = sis6306_packed_graphics(dev);
    const int old_packed = dev->svga.fb_only;
    const int old_bpp = dev->svga.bpp;

    dev->svga.fb_only       = packed;
    dev->svga.packed_chain4 = packed;

    /* Clear the enhanced-mode colour depth before VGA timing calculation. */
    if (!packed)
        dev->svga.bpp = 8;

    svga_recalctimings(&dev->svga);

    /*
     * Entering SiS packed 256-colour mode must expose all eight pixel bits
     * to the palette lookup.  The PEL mask at 3C6 is persistent across mode
     * changes; if a previous BIOS/driver mode left a narrower mask behind,
     * high Windows palette indices alias to low entries (the fixed low
     * system colours still look right, while shell icon yellows can turn
     * grey).
     *
     * Normalize the mask once on the transition into packed 8-bpp.  Later
     * guest writes to 3C6 remain fully honoured, preserving real PEL-mask
     * behaviour instead of forcing FF continuously.
     */
    if (packed && (dev->svga.bpp == 8) && (!old_packed || (old_bpp != 8)))
        dev->svga.dac_mask = 0xff;

    sis6306_update_cursor(dev);
}

static void
sis6306_out(uint16_t addr, uint8_t val, void *priv)
{
    sis6306_t *dev  = (sis6306_t *) priv;
    svga_t    *svga = &dev->svga;
    uint8_t    old;

    if ((((addr & 0xfff0) == 0x03d0) || ((addr & 0xfff0) == 0x03b0)) &&
        !(svga->miscout & 1))
        addr ^= 0x60;

    /*
	 * This fixes the Windows->DOS transtion corruption due to the async
	 * still using VRAM at time of change.
	 */
    if ((addr == 0x03c0) || (addr == 0x03c2) || (addr == 0x03cf))
        sis6306_accel_sync(dev);

    switch (addr) {
        case 0x03c6:
            svga_out(addr, val, svga);
            return;

        case 0x03cb:
            sis6306_accel_sync(dev);
            dev->segment1 = val;
            sis6306_update_banks(dev);
            return;

        case 0x03cd:
            sis6306_accel_sync(dev);
            dev->segment0 = val;
            sis6306_update_banks(dev);
            return;

        case 0x03d4:
            /* Keep all eight index bits for the SiS video-extension bank. */
            dev->sis_crtc_index = val;
            svga->crtcreg       = val;
            return;

        case 0x03d5:
            /* CR80 is the video-extension password/identification register. */
            if (dev->sis_crtc_index == 0x80) {
                dev->ext_crtc_unlocked = (val == 0x86);
                return;
            }

            /* CRCB is the second SiS DDC/status path. */
            if (dev->sis_crtc_index == 0xcb) {
                if (dev->ext_crtc_unlocked) {
                    dev->ext_crtc[0xcb] = val & 0x30;
                    dev->panel_ddc_writes++;
                    if (!dev->panel_ddc_seen) {
                        dev->panel_ddc_seen = 1;
                    }
                    if (dev->i2c)
                        i2c_gpio_set(dev->i2c,
                                     !!(val & 0x20), !!(val & 0x10));
                }
                return;
            }

            if (dev->sis_crtc_index >= 0x20) {
                if (dev->ext_crtc_unlocked) {
                    dev->ext_crtc[dev->sis_crtc_index] = val;
                }
                return;
            }

            sis6306_accel_sync(dev);
            if (svga->crtcreg & 0x20)
                return;

            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;

            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);

            old = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;

            if (old != val) {
                if ((svga->crtcreg < 0x0e) || (svga->crtcreg > 0x10)) {
                    if ((svga->crtcreg == 0x0c) || (svga->crtcreg == 0x0d)) {
                        sis6306_accel_sync(dev);
                        svga->fullchange = 3;
                        sis6306_update_display_start(dev);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            return;

        case 0x03c5:
            /* SR05 is the extended-register password/identification register. */
            if (svga->seqaddr == 0x05) {
                dev->ext_seq_unlocked = (val == 0x86);
                return;
            }

            if (svga->seqaddr >= 0x06) {
                if (dev->ext_seq_unlocked) {
                    const uint8_t index = svga->seqaddr;

                    /* SR0D/SR0E are hardware-configuration status registers. */
                    if ((index == 0x0d) || (index == 0x0e)) {
                        return;
                    }

                    const uint8_t old_ext = dev->ext_seq[index];
                    if ((old_ext != val) &&
                        ((index == 0x06) || (index == 0x09) ||
                         (index == 0x0a) || (index == 0x0b) ||
                         (index == 0x12)))
                        sis6306_accel_sync(dev);
                    dev->ext_seq[index] = val;

                    /* SR38.D1:0 selects which VCLK register bank is exposed
                     * at SR13, SR2A and SR2B. Live clock select is still
                     * VGA Misc Output[3:2]. */
                    if ((index == 0x13) || (index == 0x2a) || (index == 0x2b)) {
                        const unsigned int bank = dev->ext_seq[0x38] & 3;
                        if (bank < 3) {
                            const unsigned int slot = (index == 0x13) ? 0 :
                                                      ((index == 0x2a) ? 1 : 2);
                            dev->vclk_regs[bank][slot] = val;
                            if (slot != 0)
                                dev->vclk_written[bank] |= (uint8_t) (1U << (slot - 1));
                            svga_recalctimings(svga);
                        }
                    }

                    /* SR11 bit 0=SCL, bit 1=SDA on the monitor DDC bus. */
                    if (index == 0x11) {
                        dev->ddc_writes++;
                        if (!dev->ddc_seen) {
                            dev->ddc_seen = 1;
                        }
                        if (dev->i2c)
                            i2c_gpio_set(dev->i2c,
                                         !!(val & 0x01), !!(val & 0x02));
                    }

                    if ((index == 0x06) && ((old_ext ^ val) & 0x5e))
                        sis6306_update_graphics_mode(dev);
                    if ((index == 0x09) && ((old_ext ^ val) & 0x80))
                        sis6306_update_graphics_mode(dev);
                    if (((index == 0x0a) || (index == 0x12)) &&
                        (old_ext != val))
                        svga_recalctimings(svga);
                    if ((index == 0x0b) && (old_ext != val)) {
                        if ((old_ext ^ val) & 0x08)
                            sis6306_update_banks(dev);
                    }

                    /*
                     * SR26/SR27 extend the normal CRTC screen-start address.
                     * SR2F.D4 selects the dedicated fast-page-flip latch; SR32
                     * is the documented commit write for SR30/SR31/SR32.
                     */
                    if (((index == 0x26) || (index == 0x27)) &&
                        (old_ext != val)) {
                        sis6306_accel_sync(dev);
                        sis6306_update_display_start(dev);
                        svga->fullchange = changeframecount;
                    }

                    if ((index == 0x2f) && (old_ext != val)) {
                        sis6306_accel_sync(dev);
                        if (!(val & 0x10))
                            dev->fast_page_valid = 0;
                        sis6306_update_display_start(dev);
                        svga->fullchange = changeframecount;
                    }

                    if (index == 0x32) {
                        /* SR32 is the fast-page-flip commit write. */
                        sis6306_accel_sync(dev);
                        sis6306_fast_page_flip_latch(dev);
                    }

                    if (((index >= 0x14) && (index <= 0x1f)) ||
                        (index == 0x23) || (index == 0x38) ||
                        (index == 0x3e))
                        sis6306_update_cursor(dev);
                }
                return;
            }

            sis6306_accel_sync(dev);
            break;

        default:
            break;
    }

    svga_out(addr, val, svga);
}

static uint8_t
sis6306_in(uint16_t addr, void *priv)
{
    sis6306_t *dev  = (sis6306_t *) priv;
    svga_t    *svga = &dev->svga;

    if ((((addr & 0xfff0) == 0x03d0) || ((addr & 0xfff0) == 0x03b0)) &&
        !(svga->miscout & 1))
        addr ^= 0x60;


    switch (addr) {
        case 0x03cb: {
            uint8_t ret = (dev->ext_seq[0x0b] & 0x08) ?
                          (dev->segment1 & 0x7f) : 0x00;
            return ret;
        }

        case 0x03cd:
            return dev->segment0;

        case 0x03d4:
            return dev->sis_crtc_index;

        case 0x03d5:
            if (dev->sis_crtc_index == 0x80) {
                uint8_t ret = dev->ext_crtc_unlocked ? 0xa1 : 0x21;
                return ret;
            }

            if (dev->sis_crtc_index == 0xcb) {
                uint8_t ret = 0x00;
                dev->panel_ddc_reads++;
                if (!dev->panel_ddc_seen) {
                    dev->panel_ddc_seen = 1;
                }
                if (!dev->ext_crtc_unlocked)
                    return ret;

                ret = dev->ext_crtc[0xcb] & 0x30;
                /* Bits 3 and 2 report the SCL and SDA bus levels.
                 * Both lines are pulled high when no bus is available. */
                if (dev->i2c) {
                    if (i2c_gpio_get_scl(dev->i2c))
                        ret |= 0x08;
                    if (i2c_gpio_get_sda(dev->i2c))
                        ret |= 0x04;
                } else {
                    ret |= 0x0c;
                }
                return ret;
            }

            if (dev->sis_crtc_index >= 0x20) {
                uint8_t ret = dev->ext_crtc_unlocked ?
                              dev->ext_crtc[dev->sis_crtc_index] : 0x00;
                return ret;
            }

            if (svga->crtcreg & 0x20)
                return 0xff;

            return svga->crtc[svga->crtcreg];

        case 0x03c5:
            if (svga->seqaddr == 0x05) {
                uint8_t ret = dev->ext_seq_unlocked ? 0xa1 : 0x21;
                return ret;
            }

            if (svga->seqaddr >= 0x06) {
                if (!dev->ext_seq_unlocked) {
                    return 0x00;
                }

                /*
                 * IN530 / SiS 530 board straps:
                 *   SR0D D4 = internal AGP bus enabled
                 *   SR0D D0 = shared-framebuffer architecture
                 *   SR0E D6 = integrated VGA enabled
                 *
                 * These are read-only status bits on the real chipset.  The
                 * VGA BIOS uses the shared/local status when programming the
                 * host bridge's shared-memory control.
                 */
                if (svga->seqaddr == 0x0d) {
                    return 0x11;
                }
                if (svga->seqaddr == 0x0e) {
                    return 0x40;
                }

                if ((svga->seqaddr == 0x13) || (svga->seqaddr == 0x2a) ||
                    (svga->seqaddr == 0x2b)) {
                    const unsigned int bank = dev->ext_seq[0x38] & 3;
                    if (bank < 3) {
                        const unsigned int slot = (svga->seqaddr == 0x13) ? 0 :
                                                  ((svga->seqaddr == 0x2a) ? 1 : 2);
                        return dev->vclk_regs[bank][slot];
                    }
                }

                if (svga->seqaddr == 0x11) {
                    uint8_t ret = dev->ext_seq[0x11] & ~0x03;
                    dev->ddc_reads++;
                    if (!dev->ddc_seen) {
                        dev->ddc_seen = 1;
                    }
                    if (dev->i2c) {
                        if (i2c_gpio_get_scl(dev->i2c))
                            ret |= 0x01;
                        if (i2c_gpio_get_sda(dev->i2c))
                            ret |= 0x02;
                    } else {
                        ret |= 0x03;
                    }
                    return ret;
                }

                return dev->ext_seq[svga->seqaddr];
            }
            break;

        default:
            break;
    }

    {
        uint8_t ret = svga_in(addr, svga);
        return ret;
    }
}

static uint8_t
sis6306_iobar_read(uint16_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->io_active_base);

    /* BAR2 + 30h..5Fh relocates VGA 3B0h..3DFh. */
    if ((off >= 0x30) && (off <= 0x5f))
        return sis6306_in((uint16_t) (0x0380 + off), dev);

    return dev->io_regs[off & 0x7f];
}

static void
sis6306_iobar_write(uint16_t addr, uint8_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint8_t off = (uint8_t) (addr - dev->io_active_base);

    if ((off >= 0x30) && (off <= 0x5f))
        sis6306_out((uint16_t) (0x0380 + off), val, dev);
    else
        dev->io_regs[off & 0x7f] = val;
}

static void
sis6306_update_mappings(sis6306_t *dev)
{
    io_removehandler(0x03a0, 0x0040,
                     sis6306_in, NULL, NULL,
                     sis6306_out, NULL, NULL,
                     dev);

    if (dev->pci_command & PCI_COMMAND_IO)
        io_sethandler(0x03a0, 0x0040,
                      sis6306_in, NULL, NULL,
                      sis6306_out, NULL, NULL,
                      dev);

    if (dev->pci_command & PCI_COMMAND_MEM)
        mem_mapping_enable(&dev->svga.mapping);
    else
        mem_mapping_disable(&dev->svga.mapping);

    mem_mapping_disable(&dev->lfb_mapping);
    if ((dev->pci_command & PCI_COMMAND_MEM) && dev->lfb_base)
        mem_mapping_set_addr(&dev->lfb_mapping, dev->lfb_base, 8 << 20);

    mem_mapping_disable(&dev->mmio_mapping);
    if ((dev->pci_command & PCI_COMMAND_MEM) && dev->mmio_base)
        mem_mapping_set_addr(&dev->mmio_mapping, dev->mmio_base, 0x10000);

    if (dev->io_active_base) {
        io_removehandler(dev->io_active_base, 0x0080,
                         sis6306_iobar_read, NULL, NULL,
                         sis6306_iobar_write, NULL, NULL, dev);
        dev->io_active_base = 0;
    }
    if ((dev->pci_command & PCI_COMMAND_IO) && dev->io_base &&
        ((dev->io_base & 0xffff0000U) == 0)) {
        dev->io_active_base = (uint16_t) (dev->io_base & 0xff80);
        io_sethandler(dev->io_active_base, 0x0080,
                      sis6306_iobar_read, NULL, NULL,
                      sis6306_iobar_write, NULL, NULL, dev);
    }
}

static inline uint16_t
sis6306_engine_reg16(const sis6306_t *dev, uint32_t off)
{
    return (uint16_t) (dev->accel_regs[off] |
                       ((uint16_t) dev->accel_regs[off + 1] << 8));
}

static inline uint32_t
sis6306_engine_reg32(const sis6306_t *dev, uint32_t off)
{
    return (uint32_t) dev->accel_regs[off] |
           ((uint32_t) dev->accel_regs[off + 1] << 8) |
           ((uint32_t) dev->accel_regs[off + 2] << 16) |
           ((uint32_t) dev->accel_regs[off + 3] << 24);
}

/*
 * The SiS engine uses the standard 8-bit ternary Windows ROP truth table.
 * Index bits are P:S:D, therefore F0=P, CC=S and AA=D as expected.
 */
static inline unsigned
sis6306_engine_bytespp(const sis6306_t *dev)
{
    switch (dev->accel_bpp) {
        case 8:  return 1;
        case 15:
        case 16: return 2;
        case 24: return 3;
        case 32: return 4;
        default: return 1;
    }
}

static inline uint32_t
sis6306_engine_read_pixel(const sis6306_t *dev, uint32_t addr, unsigned bytespp)
{
    uint32_t v = 0;

    switch (bytespp) {
        case 1:
            return dev->svga.vram[addr];

        case 2: {
            uint16_t w;
            memcpy(&w, &dev->svga.vram[addr], sizeof(w));
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
            w = (uint16_t) ((w >> 8) | (w << 8));
#endif
            return w;
        }

        case 3:
            return (uint32_t) dev->svga.vram[addr] |
                   ((uint32_t) dev->svga.vram[addr + 1] << 8) |
                   ((uint32_t) dev->svga.vram[addr + 2] << 16);

        case 4:
            memcpy(&v, &dev->svga.vram[addr], sizeof(v));
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
            v = ((v & 0x000000ffU) << 24) |
                ((v & 0x0000ff00U) << 8)  |
                ((v & 0x00ff0000U) >> 8)  |
                ((v & 0xff000000U) >> 24);
#endif
            return v;

        default:
            return 0;
    }
}

static inline void
sis6306_engine_write_pixel(sis6306_t *dev, uint32_t addr,
                           unsigned bytespp, uint32_t value)
{
    switch (bytespp) {
        case 1:
            dev->svga.vram[addr] = (uint8_t) value;
            break;

        case 2: {
            uint16_t w = (uint16_t) value;
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
            w = (uint16_t) ((w >> 8) | (w << 8));
#endif
            memcpy(&dev->svga.vram[addr], &w, sizeof(w));
            break;
        }

        case 3:
            dev->svga.vram[addr]     = (uint8_t) value;
            dev->svga.vram[addr + 1] = (uint8_t) (value >> 8);
            dev->svga.vram[addr + 2] = (uint8_t) (value >> 16);
            break;

        case 4:
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
            value = ((value & 0x000000ffU) << 24) |
                    ((value & 0x0000ff00U) << 8)  |
                    ((value & 0x00ff0000U) >> 8)  |
                    ((value & 0xff000000U) >> 24);
#endif
            memcpy(&dev->svga.vram[addr], &value, sizeof(value));
            break;

        default:
            break;
    }
}

static inline uint32_t
sis6306_engine_rop_pixel(uint8_t rop, uint32_t d, uint32_t p,
                         uint32_t src, unsigned bytespp)
{
    const uint32_t mask = (bytespp == 1) ? 0x000000ffU :
                          (bytespp == 2) ? 0x0000ffffU :
                          (bytespp == 3) ? 0x00ffffffU : 0xffffffffU;
    uint32_t out = 0;

    d &= mask;
    p &= mask;
    src &= mask;

    switch (rop) {
        case 0x00: return 0;
        case 0xff: return mask;
        case 0xaa: return d;          /* D */
        case 0xcc: return src;        /* S */
        case 0xf0: return p;          /* P */
        case 0x55: return (~d) & mask;
        case 0x33: return (~src) & mask;
        case 0x0f: return (~p) & mask;
        default:   break;
    }

    if (rop & 0x01) out |= ~p & ~src & ~d;
    if (rop & 0x02) out |= ~p & ~src &  d;
    if (rop & 0x04) out |= ~p &  src & ~d;
    if (rop & 0x08) out |= ~p &  src &  d;
    if (rop & 0x10) out |=  p & ~src & ~d;
    if (rop & 0x20) out |=  p & ~src &  d;
    if (rop & 0x40) out |=  p &  src & ~d;
    if (rop & 0x80) out |=  p &  src &  d;

    return out & mask;
}

static inline int
sis6306_engine_clip_pixel(const sis6306_t *dev, uint32_t command,
                          int x, int y, uint16_t dst_pitch,
                          uint16_t dst_height, unsigned bytespp)
{
    if (command & (1U << 18)) {
        int16_t left   = (int16_t) sis6306_engine_reg16(dev, 0x8234);
        int16_t top    = (int16_t) sis6306_engine_reg16(dev, 0x8236);
        int16_t right  = (int16_t) sis6306_engine_reg16(dev, 0x8238);
        int16_t bottom = (int16_t) sis6306_engine_reg16(dev, 0x823a);

        if ((x < left) || (x > right) || (y < top) || (y > bottom))
            return 0;
    }

    if (!(command & (1U << 26))) {
        if ((x < 0) || (y < 0))
            return 0;
        if (dst_height && (y >= dst_height))
            return 0;
        if (dst_pitch && bytespp && (x >= (int) (dst_pitch / bytespp)))
            return 0;
    }

    return 1;
}

static inline uint32_t
sis6306_engine_pattern(const sis6306_t *dev, uint32_t command,
                       int x, int y, unsigned bytespp)
{
    uint32_t fg = sis6306_engine_reg32(dev, 0x821c);
    uint32_t bg = sis6306_engine_reg32(dev, 0x8220);

    switch ((command >> 6) & 3) {
        case 0:
            return fg;

        case 1: {
            /* 8x8 color pattern. 8bpp=64 bytes; high-color=128 bytes. */
            uint32_t off = 0x8300 + ((uint32_t) (y & 7) * 8U +
                                     (uint32_t) (x & 7)) * bytespp;
            uint32_t v = 0;
            for (unsigned i = 0; i < bytespp; i++)
                v |= (uint32_t) dev->accel_regs[(off + i) & 0xffff] << (i * 8);
            return v;
        }

        case 2: {
            uint8_t mask = dev->accel_regs[0x822c + (y & 7)];
            return (mask & (0x80U >> (x & 7))) ? fg : bg;
        }

        default:
            return fg;
    }
}

static void
sis6306_engine_mark_changed(sis6306_t *dev, uint32_t lo, uint32_t hi)
{
    if ((lo == 0xffffffffU) || (lo >= dev->svga.vram_max))
        return;
    if (hi >= dev->svga.vram_max)
        hi = dev->svga.vram_max - 1;

    for (uint32_t page = lo >> 12; page <= (hi >> 12); page++)
        dev->svga.changedvram[page] = dev->svga.monitor->mon_changeframecount;

    dev->svga.fullchange = dev->svga.monitor->mon_changeframecount;
}

static inline void
sis6306_engine_finish_changed(sis6306_t *dev, uint32_t lo, uint32_t hi,
                              uint32_t *lo_out, uint32_t *hi_out)
{
    if (lo_out && hi_out) {
        *lo_out = lo;
        *hi_out = hi;
    } else {
        sis6306_engine_mark_changed(dev, lo, hi);
    }
}

static uint64_t
sis6306_engine_bitblt(sis6306_t *dev, uint32_t command,
                       unsigned lane, unsigned lanes,
                       uint32_t *changed_lo_out, uint32_t *changed_hi_out)
{
    svga_t *svga = &dev->svga;
    const unsigned bytespp = sis6306_engine_bytespp(dev);
    const uint32_t src_base = sis6306_engine_reg32(dev, 0x8200) & 0x007fffffU;
    const uint16_t src_pitch = sis6306_engine_reg16(dev, 0x8204) & 0x1fff;
    const int src_y = sis6306_engine_reg16(dev, 0x8208) & 0x0fff;
    const int src_x = sis6306_engine_reg16(dev, 0x820a) & 0x0fff;
    const int dst_y = sis6306_engine_reg16(dev, 0x820c) & 0x0fff;
    const int dst_x = sis6306_engine_reg16(dev, 0x820e) & 0x0fff;
    const uint32_t dst_base = sis6306_engine_reg32(dev, 0x8210) & 0x007fffffU;
    const uint16_t dst_pitch = sis6306_engine_reg16(dev, 0x8214) & 0x1fff;
    const uint16_t dst_height = sis6306_engine_reg16(dev, 0x8216) & 0x1fff;
    const uint16_t width = sis6306_engine_reg16(dev, 0x8218) & 0x1fff;
    const uint16_t height = sis6306_engine_reg16(dev, 0x821a) & 0x1fff;
    const int xstep = (command & (1U << 16)) ? 1 : -1;
    const int ystep = (command & (1U << 17)) ? 1 : -1;
    const uint8_t rop = (command >> 8) & 0xff;
    const unsigned srcsel = (command >> 4) & 3;
    uint32_t changed_lo = 0xffffffffU, changed_hi = 0;
    uint64_t pixels = 0;

    if (!width || !height || !dst_pitch || !bytespp)
        return 0;

    for (uint32_t yy = lane; yy < height; yy += lanes) {
        int dy = dst_y + ystep * (int) yy;
        int sy = src_y + ystep * (int) yy;

        for (uint32_t xx = 0; xx < width; xx++) {
            int dx = dst_x + xstep * (int) xx;
            int sx = src_x + xstep * (int) xx;
            uint32_t da, src = 0, pat, dst, out;

            if (!sis6306_engine_clip_pixel(dev, command, dx, dy,
                                           dst_pitch, dst_height, bytespp))
                continue;
            if ((dx < 0) || (dy < 0))
                continue;

            da = dst_base + (uint32_t) dy * dst_pitch +
                 (uint32_t) dx * bytespp;
            if ((da + bytespp) > svga->vram_max)
                continue;

            if ((srcsel <= 1) && (sx >= 0) && (sy >= 0) && src_pitch) {
                uint32_t sa = src_base + (uint32_t) sy * src_pitch +
                              (uint32_t) sx * bytespp;
                if ((sa + bytespp) <= svga->vram_max)
                    src = sis6306_engine_read_pixel(dev, sa, bytespp);
            }

            if (rop == 0xaa) {
                /* DST: a true no-op. */
                pixels++;
                continue;
            } else if (rop == 0xcc) {
                out = src;
            } else if (rop == 0xf0) {
                out = sis6306_engine_pattern(dev, command, dx, dy, bytespp);
            } else if (rop == 0x00) {
                out = 0;
            } else if (rop == 0xff) {
                out = (bytespp == 1) ? 0x000000ffU :
                      (bytespp == 2) ? 0x0000ffffU :
                      (bytespp == 3) ? 0x00ffffffU : 0xffffffffU;
            } else {
                pat = sis6306_engine_pattern(dev, command, dx, dy, bytespp);
                dst = sis6306_engine_read_pixel(dev, da, bytespp);
                out = sis6306_engine_rop_pixel(rop, dst, pat, src, bytespp);
            }
            sis6306_engine_write_pixel(dev, da, bytespp, out);

            if (da < changed_lo) changed_lo = da;
            if ((da + bytespp - 1) > changed_hi) changed_hi = da + bytespp - 1;
            pixels++;
        }
    }

    sis6306_engine_finish_changed(dev, changed_lo, changed_hi,
                                  changed_lo_out, changed_hi_out);
    return pixels;
}

static uint64_t
sis6306_engine_color_expand(sis6306_t *dev, uint32_t command, int enhanced,
                             unsigned lane, unsigned lanes,
                             uint32_t *changed_lo_out, uint32_t *changed_hi_out)
{
    svga_t *svga = &dev->svga;
    const unsigned bytespp = sis6306_engine_bytespp(dev);
    const uint32_t src_base = sis6306_engine_reg32(dev, 0x8200) & 0x007fffffU;
    const uint16_t src_pitch = sis6306_engine_reg16(dev, 0x8204) & 0x1fff;
    const int src_y = sis6306_engine_reg16(dev, 0x8208) & 0x0fff;
    const int src_x = sis6306_engine_reg16(dev, 0x820a) & 0x0fff;
    const int dst_y = sis6306_engine_reg16(dev, 0x820c) & 0x0fff;
    const int dst_x = sis6306_engine_reg16(dev, 0x820e) & 0x0fff;
    const uint32_t dst_base = sis6306_engine_reg32(dev, 0x8210) & 0x007fffffU;
    const uint16_t dst_pitch = sis6306_engine_reg16(dev, 0x8214) & 0x1fff;
    const uint16_t dst_height = sis6306_engine_reg16(dev, 0x8216) & 0x1fff;
    const uint16_t width = sis6306_engine_reg16(dev, 0x8218) & 0x1fff;
    const uint16_t height = sis6306_engine_reg16(dev, 0x821a) & 0x1fff;
    const int xstep = (command & (1U << 16)) ? 1 : -1;
    const int ystep = (command & (1U << 17)) ? 1 : -1;
    const uint8_t rop = (command >> 8) & 0xff;
    const uint32_t fg = sis6306_engine_reg32(dev, 0x8224);
    const uint32_t bg = sis6306_engine_reg32(dev, 0x8228);
    const int transparent = !!(command & (1U << 20));
    uint32_t changed_lo = 0xffffffffU, changed_hi = 0;
    uint64_t pixels = 0;

    if (!width || !height || !dst_pitch || !bytespp)
        return 0;

    for (uint32_t yy = lane; yy < height; yy += lanes) {
        int dy = dst_y + ystep * (int) yy;
        int sy = src_y + ystep * (int) yy;

        for (uint32_t xx = 0; xx < width; xx++) {
            int dx = dst_x + xstep * (int) xx;
            int sx = src_x + xstep * (int) xx;
            int mono = 0;
            uint32_t da, src, pat, dst, out;

            if (enhanced) {
                if ((sx >= 0) && (sy >= 0) && src_pitch) {
                    uint32_t sa = src_base + (uint32_t) sy * src_pitch +
                                  ((uint32_t) sx >> 3);
                    if (sa < svga->vram_max)
                        mono = !!(svga->vram[sa] & (0x80U >> (sx & 7)));
                }
            } else {
                const uint32_t row_bytes = ((uint32_t) width + 7U) >> 3;
                const uint32_t byte = yy * row_bytes + (xx >> 3);
                if (byte < 0x180U)
                    mono = !!(dev->accel_regs[0x8300U + byte] &
                              (0x80U >> (xx & 7U)));
            }

            if (transparent && !mono)
                continue;
            if (!sis6306_engine_clip_pixel(dev, command, dx, dy,
                                           dst_pitch, dst_height, bytespp))
                continue;
            if ((dx < 0) || (dy < 0))
                continue;

            da = dst_base + (uint32_t) dy * dst_pitch +
                 (uint32_t) dx * bytespp;
            if ((da + bytespp) > svga->vram_max)
                continue;

            src = mono ? fg : bg;
            if (rop == 0xaa) {
                pixels++;
                continue;
            } else if (rop == 0xcc) {
                out = src;
            } else if (rop == 0xf0) {
                out = sis6306_engine_pattern(dev, command, dx, dy, bytespp);
            } else if (rop == 0x00) {
                out = 0;
            } else if (rop == 0xff) {
                out = (bytespp == 1) ? 0x000000ffU :
                      (bytespp == 2) ? 0x0000ffffU :
                      (bytespp == 3) ? 0x00ffffffU : 0xffffffffU;
            } else {
                pat = sis6306_engine_pattern(dev, command, dx, dy, bytespp);
                dst = sis6306_engine_read_pixel(dev, da, bytespp);
                out = sis6306_engine_rop_pixel(rop, dst, pat, src, bytespp);
            }
            sis6306_engine_write_pixel(dev, da, bytespp, out);
            if (da < changed_lo) changed_lo = da;
            if ((da + bytespp - 1) > changed_hi) changed_hi = da + bytespp - 1;
            pixels++;
        }
    }

    sis6306_engine_finish_changed(dev, changed_lo, changed_hi,
                                  changed_lo_out, changed_hi_out);
    return pixels;
}

static uint64_t
sis6306_engine_multi_scanline(sis6306_t *dev, uint32_t command,
                               unsigned lane, unsigned lanes,
                               uint32_t *changed_lo_out, uint32_t *changed_hi_out)
{
    svga_t *svga = &dev->svga;
    const unsigned bytespp = sis6306_engine_bytespp(dev);
    const unsigned count = sis6306_engine_reg16(dev, 0x8208) & 0x7f;
    const int ystart = sis6306_engine_reg16(dev, 0x820a) & 0x0fff;
    const uint32_t dst_base = sis6306_engine_reg32(dev, 0x8210) & 0x007fffffU;
    const uint16_t dst_pitch = sis6306_engine_reg16(dev, 0x8214) & 0x1fff;
    const uint16_t dst_height = sis6306_engine_reg16(dev, 0x8216) & 0x1fff;
    const uint8_t rop = (command >> 8) & 0xff;
    const unsigned ydir = (command >> 16) & 3;
    const int omit_last = !!(command & (1U << 21));
    uint32_t points_base;
    uint32_t changed_lo = 0xffffffffU, changed_hi = 0;
    uint64_t pixels = 0;

    if (!count || !dst_pitch || !bytespp)
        return 0;

    if (bytespp == 1)
        points_base = 0x8340;
    else if (bytespp == 2)
        points_base = 0x8380;
    else
        points_base = 0x8400;


    for (unsigned line = lane; line < count; line += lanes) {
        uint16_t xs, xe;
        int y;

        if (line == 0) {
            xs = sis6306_engine_reg16(dev, 0x820c) & 0x0fff;
            xe = sis6306_engine_reg16(dev, 0x820e) & 0x0fff;
        } else if (line == 1) {
            xs = sis6306_engine_reg16(dev, 0x8244) & 0x0fff;
            xe = sis6306_engine_reg16(dev, 0x8246) & 0x0fff;
        } else {
            uint32_t poff = points_base + (line - 2) * 4;
            uint32_t point;
            if ((poff + 3) >= 0x10000)
                break;
            point = sis6306_engine_reg32(dev, poff);
            xs = point & 0x0fff;
            xe = (point >> 16) & 0x0fff;
        }

        if (ydir == 0)
            y = ystart - (int) line;
        else if (ydir == 2)
            y = ystart + (int) line;
        else
            y = ystart;

        int xfirst = xs;
        int xlast = xe;
        int xstep = (xlast >= xfirst) ? 1 : -1;
        if (omit_last && (xlast != xfirst))
            xlast -= xstep;

        for (int x = xfirst;; x += xstep) {
            uint32_t da, pat, dst, out;

            if (sis6306_engine_clip_pixel(dev, command, x, y,
                                          dst_pitch, dst_height, bytespp) &&
                (x >= 0) && (y >= 0)) {
                da = dst_base + (uint32_t) y * dst_pitch +
                     (uint32_t) x * bytespp;
                if ((da + bytespp) <= svga->vram_max) {
                    pat = sis6306_engine_pattern(dev, command, x, y, bytespp);
                    dst = sis6306_engine_read_pixel(dev, da, bytespp);
                    out = sis6306_engine_rop_pixel(rop, dst, pat, 0, bytespp);
                    sis6306_engine_write_pixel(dev, da, bytespp, out);
                    if (da < changed_lo) changed_lo = da;
                    if ((da + bytespp - 1) > changed_hi) changed_hi = da + bytespp - 1;
                    pixels++;
                }
            }

            if (x == xlast)
                break;
        }
    }

    sis6306_engine_finish_changed(dev, changed_lo, changed_hi,
                                  changed_lo_out, changed_hi_out);
    return pixels;
}


static void
sis6306_engine_execute(sis6306_t *dev, uint32_t command)
{
    const unsigned type = command & 0x0f;
    const unsigned srcsel = (command >> 4) & 3;

    if ((type == 0) && (srcsel == 1)) {
        /*
         * The Win9x driver stages host data into off-screen VRAM first and
         * only then submits a source-select=1 BitBlt.  The programmed source
         * base therefore already contains the pixels when the command arrives.
         */
    }

    if ((dev->accel_bpp == 8) || (dev->accel_bpp == 15) ||
        (dev->accel_bpp == 16) || (dev->accel_bpp == 24) ||
        (dev->accel_bpp == 32)) {
        switch (type) {
            case 0x0:
                sis6306_engine_bitblt(dev, command, 0, 1, NULL, NULL);
                break;
            case 0x1:
                sis6306_engine_color_expand(dev, command, 0, 0, 1, NULL, NULL);
                break;
            case 0x2:
                sis6306_engine_color_expand(dev, command, 1, 0, 1, NULL, NULL);
                break;
            case 0x3:
                sis6306_engine_multi_scanline(dev, command, 0, 1, NULL, NULL);
                break;
            default:
                break;
        }
    }


}

static inline void
sis6306_engine_rect_range(uint32_t base, uint16_t pitch,
                          int x, int y, int xstep, int ystep,
                          uint16_t width, uint16_t height, unsigned bytespp,
                          uint32_t *lo_out, uint32_t *hi_out)
{
    int x2 = x + xstep * ((int) width - 1);
    int y2 = y + ystep * ((int) height - 1);
    int xmin = (x < x2) ? x : x2;
    int xmax = (x > x2) ? x : x2;
    int ymin = (y < y2) ? y : y2;
    int ymax = (y > y2) ? y : y2;

    if (xmin < 0) xmin = 0;
    if (ymin < 0) ymin = 0;
    if (xmax < 0) xmax = 0;
    if (ymax < 0) ymax = 0;

    uint64_t lo = (uint64_t) base + (uint64_t) ymin * pitch +
                  (uint64_t) xmin * bytespp;
    uint64_t hi = (uint64_t) base + (uint64_t) ymax * pitch +
                  (uint64_t) xmax * bytespp + bytespp - 1U;

    *lo_out = (lo > 0xffffffffULL) ? 0xffffffffU : (uint32_t) lo;
    *hi_out = (hi > 0xffffffffULL) ? 0xffffffffU : (uint32_t) hi;
}

static inline int
sis6306_engine_ranges_overlap(uint32_t a_lo, uint32_t a_hi,
                              uint32_t b_lo, uint32_t b_hi)
{
    return (a_lo <= b_hi) && (b_lo <= a_hi);
}

static unsigned
sis6306_2d_effective_threads(const sis6306_t *dev, uint32_t command)
{
    unsigned threads = dev->d2_render_threads;
    const unsigned type = command & 0x0f;
    unsigned rows = 1;


    if (threads < 2) {
        return 1;
    }

    if (type <= 2)
        rows = sis6306_engine_reg16(dev, 0x821a) & 0x1fff;
    else {
        return 1;
    }

    if (rows < 2) {
        return 1;
    }
    if (threads > rows)
        threads = rows;

    if (type <= 1) {
        const unsigned bytespp = sis6306_engine_bytespp(dev);
        const uint16_t width = sis6306_engine_reg16(dev, 0x8218) & 0x1fff;
        const uint16_t dst_pitch = sis6306_engine_reg16(dev, 0x8214) & 0x1fff;
        if (!width || !dst_pitch || !bytespp ||
            ((uint32_t) dst_pitch < (uint32_t) width * bytespp)) {
            return 1;
        }
    }

    if (type == 0) {
        const unsigned bytespp = sis6306_engine_bytespp(dev);
        const unsigned srcsel = (command >> 4) & 3;
        const uint8_t rop = (command >> 8) & 0xff;
        const uint16_t width = sis6306_engine_reg16(dev, 0x8218) & 0x1fff;
        const uint16_t height = sis6306_engine_reg16(dev, 0x821a) & 0x1fff;

        /* These ROPs do not consume source VRAM, so source/destination
         * overlap cannot create an ordering dependency. */
        const int source_used = !((rop == 0xaa) || (rop == 0xf0) ||
                                  (rop == 0x00) || (rop == 0xff) ||
                                  (rop == 0x55) || (rop == 0x0f));

        if (source_used && (srcsel <= 1) && width && height && bytespp) {
            const uint32_t src_base = sis6306_engine_reg32(dev, 0x8200) & 0x007fffffU;
            const uint16_t src_pitch = sis6306_engine_reg16(dev, 0x8204) & 0x1fff;
            const int src_y = sis6306_engine_reg16(dev, 0x8208) & 0x0fff;
            const int src_x = sis6306_engine_reg16(dev, 0x820a) & 0x0fff;
            const uint32_t dst_base = sis6306_engine_reg32(dev, 0x8210) & 0x007fffffU;
            const uint16_t dst_pitch = sis6306_engine_reg16(dev, 0x8214) & 0x1fff;
            const int dst_y = sis6306_engine_reg16(dev, 0x820c) & 0x0fff;
            const int dst_x = sis6306_engine_reg16(dev, 0x820e) & 0x0fff;
            const int xstep = (command & (1U << 16)) ? 1 : -1;
            const int ystep = (command & (1U << 17)) ? 1 : -1;
            uint32_t src_lo, src_hi, dst_lo, dst_hi;

            if (!src_pitch || !dst_pitch) {
                return 1;
            }

            sis6306_engine_rect_range(src_base, src_pitch, src_x, src_y,
                                      xstep, ystep, width, height, bytespp,
                                      &src_lo, &src_hi);
            sis6306_engine_rect_range(dst_base, dst_pitch, dst_x, dst_y,
                                      xstep, ystep, width, height, bytespp,
                                      &dst_lo, &dst_hi);
            if (sis6306_engine_ranges_overlap(src_lo, src_hi, dst_lo, dst_hi)) {
                return 1;
            }
        }
    } else if (type == 2) {
        /* Enhanced color expansion reads its monochrome source from VRAM.
         * Keep it serial until its source/destination dependency is classified. */
        return 1;
    }

    return threads;
}

static uint64_t
sis6306_engine_execute_parallel_lane(sis6306_t *dev, uint32_t command,
                                     unsigned lane, unsigned lanes,
                                     uint32_t *changed_lo,
                                     uint32_t *changed_hi)
{
    const unsigned type = command & 0x0f;

    *changed_lo = 0xffffffffU;
    *changed_hi = 0;

    if (!((dev->accel_bpp == 8) || (dev->accel_bpp == 15) ||
          (dev->accel_bpp == 16) || (dev->accel_bpp == 24) ||
          (dev->accel_bpp == 32)))
        return 0;

    switch (type) {
        case 0x0:
            return sis6306_engine_bitblt(dev, command, lane, lanes,
                                         changed_lo, changed_hi);
        case 0x1:
            return sis6306_engine_color_expand(dev, command, 0, lane, lanes,
                                               changed_lo, changed_hi);
        case 0x2:
            return sis6306_engine_color_expand(dev, command, 1, lane, lanes,
                                               changed_lo, changed_hi);
        case 0x3:
            return sis6306_engine_multi_scanline(dev, command, lane, lanes,
                                                 changed_lo, changed_hi);
        default:
            return 0;
    }
}


#if defined(_MSC_VER) && !defined(__clang__)
#define SIS6306_THREAD_LOCAL __declspec(thread)
#else
#define SIS6306_THREAD_LOCAL __thread
#endif
static SIS6306_THREAD_LOCAL const sis6306_3d_state_t *sis6306_3d_thread_state = NULL;

static inline uint32_t
sis6306_3d_bytes32(const uint8_t *p)
{
    return (uint32_t) p[0] |
           ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static inline uint32_t
sis6306_3d_reg32(const sis6306_t *dev, uint32_t off)
{
    if (sis6306_3d_thread_state != NULL) {
        if ((off >= SIS6306_3D_VERTEX_BASE) &&
            (off + 4U <= SIS6306_3D_VERTEX_BASE + SIS6306_3D_VERTEX_SIZE))
            return sis6306_3d_bytes32(&sis6306_3d_thread_state->vertex[
                off - SIS6306_3D_VERTEX_BASE]);

        if (off == SIS6306_3D_PRIMITIVE_REG)
            return sis6306_3d_bytes32(sis6306_3d_thread_state->primitive);

        if ((off >= SIS6306_3D_SETUP_BASE) &&
            (off + 4U <= SIS6306_3D_SETUP_BASE + SIS6306_3D_SETUP_SIZE))
            return sis6306_3d_bytes32(&sis6306_3d_thread_state->setup[
                off - SIS6306_3D_SETUP_BASE]);
    }

    return (uint32_t) dev->accel_regs[off] |
           ((uint32_t) dev->accel_regs[off + 1] << 8) |
           ((uint32_t) dev->accel_regs[off + 2] << 16) |
           ((uint32_t) dev->accel_regs[off + 3] << 24);
}

static inline uint32_t
sis6306_3d_live_reg32(const sis6306_t *dev, uint32_t off)
{
    return (uint32_t) dev->mmio_regs[off] |
           ((uint32_t) dev->mmio_regs[off + 1] << 8) |
           ((uint32_t) dev->mmio_regs[off + 2] << 16) |
           ((uint32_t) dev->mmio_regs[off + 3] << 24);
}


static inline float
sis6306_3d_float(uint32_t value)
{
    union {
        uint32_t u;
        float    f;
    } v;

    v.u = value;
    return v.f;
}

static inline int
sis6306_3d_s12(uint32_t value)
{
    const int mag = (int) (value & 0x0fff);
    return (value & 0x1000) ? -mag : mag;
}

static inline float
sis6306_3d_edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static inline uint8_t
sis6306_3d_clamp8(float value)
{
    int v = (int) (value + 0.5f);

    if (v < 0)
        v = 0;
    else if (v > 255)
        v = 255;
    return (uint8_t) v;
}

static void
sis6306_3d_get_vertex(const sis6306_t *dev, unsigned which,
                      sis6306_3d_vertex_t *v)
{
    const uint32_t base = 0x8800 + (which * 0x20);
    const uint32_t fs   = sis6306_3d_reg32(dev, base + 0x00);
    const uint32_t argb = sis6306_3d_reg32(dev, base + 0x10);

    v->z = sis6306_3d_float(sis6306_3d_reg32(dev, base + 0x04));
    v->x = sis6306_3d_float(sis6306_3d_reg32(dev, base + 0x08));
    v->y = sis6306_3d_float(sis6306_3d_reg32(dev, base + 0x0c));
    v->u = sis6306_3d_float(sis6306_3d_reg32(dev, base + 0x14));
    v->v = sis6306_3d_float(sis6306_3d_reg32(dev, base + 0x18));
    v->w = sis6306_3d_float(sis6306_3d_reg32(dev, base + 0x1c));
    v->a = (uint8_t) (argb >> 24);
    v->r = (uint8_t) (argb >> 16);
    v->g = (uint8_t) (argb >> 8);
    v->b = (uint8_t) argb;
    v->fog = (uint8_t) (fs >> 24);
}

static inline uint16_t
sis6306_3d_z16(float z)
{
    if (!(z == z) || (z <= 0.0f))
        return 0;
    if (z >= 1.0f)
        return 0x7fff;
    return (uint16_t) ((z * 32767.0f) + 0.5f);
}

static inline int
sis6306_3d_ztest(unsigned mode, uint16_t znew, uint16_t zdst)
{
    switch (mode & 7) {
        case 0: return 0;
        case 1: return znew <  zdst;
        case 2: return znew == zdst;
        case 3: return znew <= zdst;
        case 4: return znew >  zdst;
        case 5: return znew != zdst;
        case 6: return znew >= zdst;
        case 7: return 1;
        default: return 0;
    }
}

/* Documented SiS530 alpha-test compare modes.  TAREF and Anew are
 * both A8 values; Anew is the post-texture-combine alpha value. */
static inline int
sis6306_3d_alpha_test(unsigned mode, uint8_t anew, uint8_t aref)
{
    switch (mode & 7) {
        case 0: return 0;
        case 1: return anew <  aref;
        case 2: return anew == aref;
        case 3: return anew <= aref;
        case 4: return anew >  aref;
        case 5: return anew != aref;
        case 6: return anew >= aref;
        case 7: return 1;
        default: return 0;
    }
}

static inline uint16_t
sis6306_3d_read16(const sis6306_t *dev, uint32_t addr)
{
    uint16_t value;

    memcpy(&value, &dev->svga.vram[addr], sizeof(value));
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    value = (uint16_t) ((value >> 8) | (value << 8));
#endif
    return value;
}

static inline void
sis6306_3d_write16(sis6306_t *dev, uint32_t addr, uint16_t value)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    value = (uint16_t) ((value >> 8) | (value << 8));
#endif
    memcpy(&dev->svga.vram[addr], &value, sizeof(value));
}

static inline void
sis6306_3d_read_dst_pixel_fast(const sis6306_t *dev, uint32_t addr,
                                  unsigned bytespp, int bgr,
                                  float *r, float *g, float *b)
{
    if (bytespp == 2) {
        const uint16_t p = sis6306_3d_read16(dev, addr);
        if (!bgr) {
            *r = (float) (((p >> 11) & 0x1f) * 255 / 31);
            *g = (float) (((p >> 5)  & 0x3f) * 255 / 63);
            *b = (float) (( p        & 0x1f) * 255 / 31);
        } else {
            *b = (float) (((p >> 11) & 0x1f) * 255 / 31);
            *g = (float) (((p >> 5)  & 0x3f) * 255 / 63);
            *r = (float) (( p        & 0x1f) * 255 / 31);
        }
    } else {
        if (!bgr) {
            *b = (float) dev->svga.vram[addr];
            *g = (float) dev->svga.vram[addr + 1];
            *r = (float) dev->svga.vram[addr + 2];
        } else {
            *r = (float) dev->svga.vram[addr];
            *g = (float) dev->svga.vram[addr + 1];
            *b = (float) dev->svga.vram[addr + 2];
        }
    }
}

static inline void
sis6306_3d_write_dst_pixel_fast(sis6306_t *dev, uint32_t addr,
                                   unsigned bytespp, uint8_t r,
                                   uint8_t g, uint8_t b, int bgr,
                                   int changeframe)
{
    if (bytespp == 2) {
        uint16_t pixel;
        if (!bgr)
            pixel = (uint16_t) (((uint16_t) (r >> 3) << 11) |
                                ((uint16_t) (g >> 2) << 5) |
                                ((uint16_t) (b >> 3)));
        else
            pixel = (uint16_t) (((uint16_t) (b >> 3) << 11) |
                                ((uint16_t) (g >> 2) << 5) |
                                ((uint16_t) (r >> 3)));
        dev->svga.vram[addr] = (uint8_t) pixel;
        dev->svga.vram[addr + 1] = (uint8_t) (pixel >> 8);
    } else {
        if (!bgr) {
            dev->svga.vram[addr] = b;
            dev->svga.vram[addr + 1] = g;
            dev->svga.vram[addr + 2] = r;
        } else {
            dev->svga.vram[addr] = r;
            dev->svga.vram[addr + 1] = g;
            dev->svga.vram[addr + 2] = b;
        }
        dev->svga.vram[addr + 3] = 0;
    }

    {
        const uint32_t p0 = addr >> 12;
        const uint32_t p1 = (addr + bytespp - 1) >> 12;
        dev->svga.changedvram[p0] = changeframe;
        if (p1 != p0)
            dev->svga.changedvram[p1] = changeframe;
    }
}

static inline void
sis6306_3d_write_rgb565_opaque_fast(sis6306_t *dev, uint32_t addr,
                                       float r, float g, float b, int bgr,
                                       int changeframe, uint32_t *last_page)
{
    const uint8_t r8 = sis6306_3d_clamp8(r);
    const uint8_t g8 = sis6306_3d_clamp8(g);
    const uint8_t b8 = sis6306_3d_clamp8(b);
    uint16_t pixel;

    if (!bgr)
        pixel = (uint16_t) (((uint16_t) (r8 >> 3) << 11) |
                            ((uint16_t) (g8 >> 2) << 5) |
                            ((uint16_t) (b8 >> 3)));
    else
        pixel = (uint16_t) (((uint16_t) (b8 >> 3) << 11) |
                            ((uint16_t) (g8 >> 2) << 5) |
                            ((uint16_t) (r8 >> 3)));

    dev->svga.vram[addr] = (uint8_t) pixel;
    dev->svga.vram[addr + 1] = (uint8_t) (pixel >> 8);

    {
        const uint32_t p0 = addr >> 12;
        if (p0 != *last_page) {
            dev->svga.changedvram[p0] = changeframe;
            *last_page = p0;
        }

        if ((addr & 0xfffU) == 0xfffU) {
            const uint32_t p1 = p0 + 1;
            if (p1 != *last_page) {
                dev->svga.changedvram[p1] = changeframe;
                *last_page = p1;
            }
        }
    }
}


static inline int
sis6306_3d_floori(float v)
{
    int i = (int) v;
    if ((float) i > v)
        i--;
    return i;
}

static inline int
sis6306_3d_wrap_coord(int v, int size)
{
    if (size <= 0)
        return 0;

    if ((size & (size - 1)) == 0)
        return (int) ((unsigned) v & (unsigned) (size - 1));

    v %= size;
    if (v < 0)
        v += size;
    return v;
}

static inline int
sis6306_3d_clamp_coord(int v, int size)
{
    if (v < 0)
        return 0;
    if (v >= size)
        return size - 1;
    return v;
}

static inline void
sis6306_3d_texcoord_pair(int v0, int size, int wrap, int *c0, int *c1)
{
    if (wrap && (size > 0) && ((size & (size - 1)) == 0)) {
        const unsigned mask = (unsigned) (size - 1);
        const unsigned u0 = (unsigned) v0 & mask;
        *c0 = (int) u0;
        *c1 = (int) ((u0 + 1U) & mask);
    } else if (wrap) {
        *c0 = sis6306_3d_wrap_coord(v0, size);
        *c1 = sis6306_3d_wrap_coord(v0 + 1, size);
    } else {
        *c0 = sis6306_3d_clamp_coord(v0, size);
        *c1 = sis6306_3d_clamp_coord(v0 + 1, size);
    }
}

static inline int
sis6306_3d_texel565_resolved(const sis6306_t *dev, uint32_t mem_size,
                             uint32_t base, unsigned pitch, int x, int y, int bgr,
                             uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint64_t addr64 = (uint64_t) base + ((uint64_t) y * pitch) + ((uint64_t) x << 1);
    uint16_t p;

    if (addr64 + 1 >= mem_size)
        return 0;

    p = sis6306_3d_read16(dev, (uint32_t) addr64);
    if (!bgr) {
        *r = (uint8_t) (((p >> 11) & 0x1f) * 255 / 31);
        *g = (uint8_t) (((p >> 5)  & 0x3f) * 255 / 63);
        *b = (uint8_t) (( p        & 0x1f) * 255 / 31);
    } else {
        *b = (uint8_t) (((p >> 11) & 0x1f) * 255 / 31);
        *g = (uint8_t) (((p >> 5)  & 0x3f) * 255 / 63);
        *r = (uint8_t) (( p        & 0x1f) * 255 / 31);
    }
    return 1;
}

static inline int
sis6306_3d_texel565(const sis6306_t *dev, uint32_t mem_size,
                    uint32_t base, unsigned pitch,
                    int width, int height, int x, int y,
                    int wrap_u, int wrap_v, int bgr,
                    uint8_t *r, uint8_t *g, uint8_t *b)
{
    x = wrap_u ? sis6306_3d_wrap_coord(x, width) : sis6306_3d_clamp_coord(x, width);
    y = wrap_v ? sis6306_3d_wrap_coord(y, height) : sis6306_3d_clamp_coord(y, height);
    return sis6306_3d_texel565_resolved(dev, mem_size, base, pitch, x, y, bgr, r, g, b);
}

static inline int
sis6306_3d_sample_texture565(const sis6306_t *dev, uint32_t mem_size,
                             uint32_t base, unsigned pitch,
                             int width, int height, float u, float v,
                             int wrap_u, int wrap_v, int linear, int bgr,
                             float *r, float *g, float *b)
{
    float sx, sy;

    if ((width <= 0) || (height <= 0) || !pitch)
        return 0;

    if (!linear) {
        int x = sis6306_3d_floori(u * (float) width);
        int y = sis6306_3d_floori(v * (float) height);
        uint8_t tr, tg, tb;
        if (!sis6306_3d_texel565(dev, mem_size, base, pitch, width, height, x, y,
                                 wrap_u, wrap_v, bgr, &tr, &tg, &tb))
            return 0;
        *r = (float) tr;
        *g = (float) tg;
        *b = (float) tb;
        return 1;
    }

    /* D3D-style normalized coordinates: integer texel centres are at n+0.5. */
    sx = (u * (float) width) - 0.5f;
    sy = (v * (float) height) - 0.5f;
    {
        const int raw_x0 = sis6306_3d_floori(sx);
        const int raw_y0 = sis6306_3d_floori(sy);
        const float fx = sx - (float) raw_x0;
        const float fy = sy - (float) raw_y0;
        int x0, x1, y0, y1;
        uint8_t r00, g00, b00, r10, g10, b10;
        uint8_t r01, g01, b01, r11, g11, b11;
        float r0, g0, b0, r1, g1, b1;

        sis6306_3d_texcoord_pair(raw_x0, width, wrap_u, &x0, &x1);
        sis6306_3d_texcoord_pair(raw_y0, height, wrap_v, &y0, &y1);
        {
            /* The four bilinear taps occupy two texture rows.  Build and
             * validate each row once instead of repeating y*pitch and a VRAM
             * bounds test independently for all four texels. */
            const uint64_t row0 = (uint64_t) base + ((uint64_t) y0 * pitch);
            const uint64_t row1 = (uint64_t) base + ((uint64_t) y1 * pitch);
            const uint32_t xo0 = (uint32_t) x0 << 1;
            const uint32_t xo1 = (uint32_t) x1 << 1;
            const uint32_t xmax = ((xo0 > xo1) ? xo0 : xo1) + 1U;
            uint16_t p00, p10, p01, p11;

            if ((row0 + xmax >= mem_size) || (row1 + xmax >= mem_size))
                return 0;

            p00 = sis6306_3d_read16(dev, (uint32_t) row0 + xo0);
            p10 = sis6306_3d_read16(dev, (uint32_t) row0 + xo1);
            p01 = sis6306_3d_read16(dev, (uint32_t) row1 + xo0);
            p11 = sis6306_3d_read16(dev, (uint32_t) row1 + xo1);

            if (!bgr) {
                r00 = (uint8_t) (((p00 >> 11) & 0x1f) * 255 / 31);
                g00 = (uint8_t) (((p00 >> 5)  & 0x3f) * 255 / 63);
                b00 = (uint8_t) (( p00        & 0x1f) * 255 / 31);
                r10 = (uint8_t) (((p10 >> 11) & 0x1f) * 255 / 31);
                g10 = (uint8_t) (((p10 >> 5)  & 0x3f) * 255 / 63);
                b10 = (uint8_t) (( p10        & 0x1f) * 255 / 31);
                r01 = (uint8_t) (((p01 >> 11) & 0x1f) * 255 / 31);
                g01 = (uint8_t) (((p01 >> 5)  & 0x3f) * 255 / 63);
                b01 = (uint8_t) (( p01        & 0x1f) * 255 / 31);
                r11 = (uint8_t) (((p11 >> 11) & 0x1f) * 255 / 31);
                g11 = (uint8_t) (((p11 >> 5)  & 0x3f) * 255 / 63);
                b11 = (uint8_t) (( p11        & 0x1f) * 255 / 31);
            } else {
                b00 = (uint8_t) (((p00 >> 11) & 0x1f) * 255 / 31);
                g00 = (uint8_t) (((p00 >> 5)  & 0x3f) * 255 / 63);
                r00 = (uint8_t) (( p00        & 0x1f) * 255 / 31);
                b10 = (uint8_t) (((p10 >> 11) & 0x1f) * 255 / 31);
                g10 = (uint8_t) (((p10 >> 5)  & 0x3f) * 255 / 63);
                r10 = (uint8_t) (( p10        & 0x1f) * 255 / 31);
                b01 = (uint8_t) (((p01 >> 11) & 0x1f) * 255 / 31);
                g01 = (uint8_t) (((p01 >> 5)  & 0x3f) * 255 / 63);
                r01 = (uint8_t) (( p01        & 0x1f) * 255 / 31);
                b11 = (uint8_t) (((p11 >> 11) & 0x1f) * 255 / 31);
                g11 = (uint8_t) (((p11 >> 5)  & 0x3f) * 255 / 63);
                r11 = (uint8_t) (( p11        & 0x1f) * 255 / 31);
            }
        }

        r0 = (float) r00 + ((float) r10 - (float) r00) * fx;
        g0 = (float) g00 + ((float) g10 - (float) g00) * fx;
        b0 = (float) b00 + ((float) b10 - (float) b00) * fx;
        r1 = (float) r01 + ((float) r11 - (float) r01) * fx;
        g1 = (float) g01 + ((float) g11 - (float) g01) * fx;
        b1 = (float) b01 + ((float) b11 - (float) b01) * fx;

        *r = r0 + (r1 - r0) * fy;
        *g = g0 + (g1 - g0) * fy;
        *b = b0 + (b1 - b0) * fy;
    }
    return 1;
}

static inline int
sis6306_3d_texel555x_resolved(const sis6306_t *dev, uint32_t mem_size,
                              uint32_t base, unsigned pitch, int x, int y,
                              int has_alpha,
                              uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint64_t addr64 = (uint64_t) base + ((uint64_t) y * pitch) + ((uint64_t) x << 1);
    uint16_t p;

    if (addr64 + 1 >= mem_size)
        return 0;

    p = sis6306_3d_read16(dev, (uint32_t) addr64);
    *a = has_alpha ? ((p & 0x8000) ? 255 : 0) : 255;
    *r = (uint8_t) (((p >> 10) & 0x1f) * 255 / 31);
    *g = (uint8_t) (((p >> 5)  & 0x1f) * 255 / 31);
    *b = (uint8_t) (( p        & 0x1f) * 255 / 31);
    return 1;
}

static inline int
sis6306_3d_sample_texture555x(const sis6306_t *dev, uint32_t mem_size,
                              uint32_t base, unsigned pitch,
                              int width, int height, float u, float v,
                              int wrap_u, int wrap_v, int linear, int has_alpha,
                              float *a, float *r, float *g, float *b)
{
    float sx, sy;

    if ((width <= 0) || (height <= 0) || !pitch)
        return 0;

    if (!linear) {
        int x = sis6306_3d_floori(u * (float) width);
        int y = sis6306_3d_floori(v * (float) height);
        uint8_t ta, tr, tg, tb;

        x = wrap_u ? sis6306_3d_wrap_coord(x, width) : sis6306_3d_clamp_coord(x, width);
        y = wrap_v ? sis6306_3d_wrap_coord(y, height) : sis6306_3d_clamp_coord(y, height);
        if (!sis6306_3d_texel555x_resolved(dev, mem_size, base, pitch, x, y,
                                           has_alpha, &ta, &tr, &tg, &tb))
            return 0;
        *a = (float) ta;
        *r = (float) tr;
        *g = (float) tg;
        *b = (float) tb;
        return 1;
    }

    sx = (u * (float) width) - 0.5f;
    sy = (v * (float) height) - 0.5f;
    {
        const int raw_x0 = sis6306_3d_floori(sx);
        const int raw_y0 = sis6306_3d_floori(sy);
        const float fx = sx - (float) raw_x0;
        const float fy = sy - (float) raw_y0;
        int x0, x1, y0, y1;
        uint8_t a00, r00, g00, b00, a10, r10, g10, b10;
        uint8_t a01, r01, g01, b01, a11, r11, g11, b11;
        float a0, r0, g0, b0, a1, r1, g1, b1;

        sis6306_3d_texcoord_pair(raw_x0, width, wrap_u, &x0, &x1);
        sis6306_3d_texcoord_pair(raw_y0, height, wrap_v, &y0, &y1);

        if (!sis6306_3d_texel555x_resolved(dev, mem_size, base, pitch, x0, y0,
                                           has_alpha, &a00, &r00, &g00, &b00) ||
            !sis6306_3d_texel555x_resolved(dev, mem_size, base, pitch, x1, y0,
                                           has_alpha, &a10, &r10, &g10, &b10) ||
            !sis6306_3d_texel555x_resolved(dev, mem_size, base, pitch, x0, y1,
                                           has_alpha, &a01, &r01, &g01, &b01) ||
            !sis6306_3d_texel555x_resolved(dev, mem_size, base, pitch, x1, y1,
                                           has_alpha, &a11, &r11, &g11, &b11))
            return 0;

        a0 = (float) a00 + ((float) a10 - (float) a00) * fx;
        r0 = (float) r00 + ((float) r10 - (float) r00) * fx;
        g0 = (float) g00 + ((float) g10 - (float) g00) * fx;
        b0 = (float) b00 + ((float) b10 - (float) b00) * fx;
        a1 = (float) a01 + ((float) a11 - (float) a01) * fx;
        r1 = (float) r01 + ((float) r11 - (float) r01) * fx;
        g1 = (float) g01 + ((float) g11 - (float) g01) * fx;
        b1 = (float) b01 + ((float) b11 - (float) b01) * fx;

        *a = a0 + (a1 - a0) * fy;
        *r = r0 + (r1 - r0) * fy;
        *g = g0 + (g1 - g0) * fy;
        *b = b0 + (b1 - b0) * fy;
    }
    return 1;
}


static inline int
sis6306_3d_texel4444_resolved(const sis6306_t *dev, uint32_t mem_size,
                              uint32_t base, unsigned pitch, int x, int y,
                              uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint64_t addr64 = (uint64_t) base + ((uint64_t) y * pitch) + ((uint64_t) x << 1);
    uint16_t p;

    if (addr64 + 1 >= mem_size)
        return 0;

    p = sis6306_3d_read16(dev, (uint32_t) addr64);

    /* TTXFM=53h: RGB-order ARGB4444, A:R:G:B = 4:4:4:4. */
    *a = (uint8_t) (((p >> 12) & 0x0f) * 17);
    *r = (uint8_t) (((p >> 8)  & 0x0f) * 17);
    *g = (uint8_t) (((p >> 4)  & 0x0f) * 17);
    *b = (uint8_t) (( p        & 0x0f) * 17);
    return 1;
}

static inline int
sis6306_3d_texel4444(const sis6306_t *dev, uint32_t mem_size,
                     uint32_t base, unsigned pitch,
                     int width, int height, int x, int y,
                     int wrap_u, int wrap_v,
                     uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b)
{
    x = wrap_u ? sis6306_3d_wrap_coord(x, width) : sis6306_3d_clamp_coord(x, width);
    y = wrap_v ? sis6306_3d_wrap_coord(y, height) : sis6306_3d_clamp_coord(y, height);
    return sis6306_3d_texel4444_resolved(dev, mem_size, base, pitch, x, y, a, r, g, b);
}

static inline int
sis6306_3d_sample_texture4444(const sis6306_t *dev, uint32_t mem_size,
                              uint32_t base, unsigned pitch,
                              int width, int height, float u, float v,
                              int wrap_u, int wrap_v, int linear,
                              float *a, float *r, float *g, float *b)
{
    float sx, sy;

    if ((width <= 0) || (height <= 0) || !pitch)
        return 0;

    if (!linear) {
        int x = sis6306_3d_floori(u * (float) width);
        int y = sis6306_3d_floori(v * (float) height);
        uint8_t ta, tr, tg, tb;
        if (!sis6306_3d_texel4444(dev, mem_size, base, pitch, width, height, x, y,
                                  wrap_u, wrap_v, &ta, &tr, &tg, &tb))
            return 0;
        *a = (float) ta;
        *r = (float) tr;
        *g = (float) tg;
        *b = (float) tb;
        return 1;
    }

    sx = (u * (float) width) - 0.5f;
    sy = (v * (float) height) - 0.5f;
    {
        const int raw_x0 = sis6306_3d_floori(sx);
        const int raw_y0 = sis6306_3d_floori(sy);
        const float fx = sx - (float) raw_x0;
        const float fy = sy - (float) raw_y0;
        int x0, x1, y0, y1;
        uint8_t a00, r00, g00, b00, a10, r10, g10, b10;
        uint8_t a01, r01, g01, b01, a11, r11, g11, b11;
        float a0, r0, g0, b0, a1, r1, g1, b1;

        sis6306_3d_texcoord_pair(raw_x0, width, wrap_u, &x0, &x1);
        sis6306_3d_texcoord_pair(raw_y0, height, wrap_v, &y0, &y1);
        {
            const uint64_t row0 = (uint64_t) base + ((uint64_t) y0 * pitch);
            const uint64_t row1 = (uint64_t) base + ((uint64_t) y1 * pitch);
            const uint32_t xo0 = (uint32_t) x0 << 1;
            const uint32_t xo1 = (uint32_t) x1 << 1;
            const uint32_t xmax = ((xo0 > xo1) ? xo0 : xo1) + 1U;
            uint16_t p00, p10, p01, p11;

            if ((row0 + xmax >= mem_size) || (row1 + xmax >= mem_size))
                return 0;

            p00 = sis6306_3d_read16(dev, (uint32_t) row0 + xo0);
            p10 = sis6306_3d_read16(dev, (uint32_t) row0 + xo1);
            p01 = sis6306_3d_read16(dev, (uint32_t) row1 + xo0);
            p11 = sis6306_3d_read16(dev, (uint32_t) row1 + xo1);

            a00 = (uint8_t) (((p00 >> 12) & 0x0f) * 17);
            r00 = (uint8_t) (((p00 >> 8)  & 0x0f) * 17);
            g00 = (uint8_t) (((p00 >> 4)  & 0x0f) * 17);
            b00 = (uint8_t) (( p00        & 0x0f) * 17);
            a10 = (uint8_t) (((p10 >> 12) & 0x0f) * 17);
            r10 = (uint8_t) (((p10 >> 8)  & 0x0f) * 17);
            g10 = (uint8_t) (((p10 >> 4)  & 0x0f) * 17);
            b10 = (uint8_t) (( p10        & 0x0f) * 17);
            a01 = (uint8_t) (((p01 >> 12) & 0x0f) * 17);
            r01 = (uint8_t) (((p01 >> 8)  & 0x0f) * 17);
            g01 = (uint8_t) (((p01 >> 4)  & 0x0f) * 17);
            b01 = (uint8_t) (( p01        & 0x0f) * 17);
            a11 = (uint8_t) (((p11 >> 12) & 0x0f) * 17);
            r11 = (uint8_t) (((p11 >> 8)  & 0x0f) * 17);
            g11 = (uint8_t) (((p11 >> 4)  & 0x0f) * 17);
            b11 = (uint8_t) (( p11        & 0x0f) * 17);
        }

        a0 = (float) a00 + ((float) a10 - (float) a00) * fx;
        r0 = (float) r00 + ((float) r10 - (float) r00) * fx;
        g0 = (float) g00 + ((float) g10 - (float) g00) * fx;
        b0 = (float) b00 + ((float) b10 - (float) b00) * fx;
        a1 = (float) a01 + ((float) a11 - (float) a01) * fx;
        r1 = (float) r01 + ((float) r11 - (float) r01) * fx;
        g1 = (float) g01 + ((float) g11 - (float) g01) * fx;
        b1 = (float) b01 + ((float) b11 - (float) b01) * fx;

        *a = a0 + (a1 - a0) * fy;
        *r = r0 + (r1 - r0) * fy;
        *g = g0 + (g1 - g0) * fy;
        *b = b0 + (b1 - b0) * fy;
    }
    return 1;
}

/* TTXFM=73h is documented RGB-order ARGB8888.  The logical
 * texel DWORD is AARRGGBB, so local-frame-buffer bytes are BB GG RR AA
 * on the little-endian guest. */
static inline int
sis6306_3d_texel8888_resolved(const sis6306_t *dev, uint32_t mem_size,
                              uint32_t base, unsigned pitch, int x, int y,
                              uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint64_t addr64 = (uint64_t) base + ((uint64_t) y * pitch) + ((uint64_t) x << 2);

    if (addr64 + 3 >= mem_size)
        return 0;

    *b = dev->svga.vram[(uint32_t) addr64];
    *g = dev->svga.vram[(uint32_t) addr64 + 1];
    *r = dev->svga.vram[(uint32_t) addr64 + 2];
    *a = dev->svga.vram[(uint32_t) addr64 + 3];
    return 1;
}

static inline int
sis6306_3d_texel8888(const sis6306_t *dev, uint32_t mem_size,
                     uint32_t base, unsigned pitch,
                     int width, int height, int x, int y,
                     int wrap_u, int wrap_v,
                     uint8_t *a, uint8_t *r, uint8_t *g, uint8_t *b)
{
    x = wrap_u ? sis6306_3d_wrap_coord(x, width) : sis6306_3d_clamp_coord(x, width);
    y = wrap_v ? sis6306_3d_wrap_coord(y, height) : sis6306_3d_clamp_coord(y, height);
    return sis6306_3d_texel8888_resolved(dev, mem_size, base, pitch, x, y, a, r, g, b);
}

/* Packed ARGB8888 bilinear interpolation.  The four byte channels are
 * carried as two independent 32-bit lanes inside uint64_t values (B/R and
 * G/A).  12-bit U/V weights retain sub-texel precision while cutting the
 * scalar per-channel interpolation work in the dominant 32-bit texture path. */
static inline uint32_t
sis6306_3d_load_argb8888(const uint8_t *vram, uint32_t addr)
{
    return (uint32_t) vram[addr] |
           ((uint32_t) vram[addr + 1] << 8) |
           ((uint32_t) vram[addr + 2] << 16) |
           ((uint32_t) vram[addr + 3] << 24);
}

static inline void
sis6306_3d_bilerp_argb8888(uint32_t p00, uint32_t p10,
                            uint32_t p01, uint32_t p11,
                            float fx, float fy,
                            float *a, float *r, float *g, float *b)
{
    enum { WSHIFT = 12, WSCALE = 1 << WSHIFT };
    unsigned wx = (unsigned) (fx * (float) WSCALE + 0.5f);
    unsigned wy = (unsigned) (fy * (float) WSCALE + 0.5f);
    unsigned ix, iy;
    uint64_t br00, br10, br01, br11;
    uint64_t ga00, ga10, ga01, ga11;
    uint64_t br0, br1, ga0, ga1, brv, gav;
    const float inv_w2 = 1.0f / (float) (WSCALE * WSCALE);

    if (wx > WSCALE) wx = WSCALE;
    if (wy > WSCALE) wy = WSCALE;
    ix = WSCALE - wx;
    iy = WSCALE - wy;

    br00 = (uint64_t) (p00 & 0xffU) | ((uint64_t) ((p00 >> 16) & 0xffU) << 32);
    br10 = (uint64_t) (p10 & 0xffU) | ((uint64_t) ((p10 >> 16) & 0xffU) << 32);
    br01 = (uint64_t) (p01 & 0xffU) | ((uint64_t) ((p01 >> 16) & 0xffU) << 32);
    br11 = (uint64_t) (p11 & 0xffU) | ((uint64_t) ((p11 >> 16) & 0xffU) << 32);

    ga00 = (uint64_t) ((p00 >> 8) & 0xffU) | ((uint64_t) ((p00 >> 24) & 0xffU) << 32);
    ga10 = (uint64_t) ((p10 >> 8) & 0xffU) | ((uint64_t) ((p10 >> 24) & 0xffU) << 32);
    ga01 = (uint64_t) ((p01 >> 8) & 0xffU) | ((uint64_t) ((p01 >> 24) & 0xffU) << 32);
    ga11 = (uint64_t) ((p11 >> 8) & 0xffU) | ((uint64_t) ((p11 >> 24) & 0xffU) << 32);

    br0 = br00 * ix + br10 * wx;
    br1 = br01 * ix + br11 * wx;
    ga0 = ga00 * ix + ga10 * wx;
    ga1 = ga01 * ix + ga11 * wx;

    brv = br0 * iy + br1 * wy;
    gav = ga0 * iy + ga1 * wy;

    *b = (float) (uint32_t) brv * inv_w2;
    *r = (float) (uint32_t) (brv >> 32) * inv_w2;
    *g = (float) (uint32_t) gav * inv_w2;
    *a = (float) (uint32_t) (gav >> 32) * inv_w2;
}

static inline int
sis6306_3d_sample_texture8888(const sis6306_t *dev, uint32_t mem_size,
                              uint32_t base, unsigned pitch,
                              int width, int height, float u, float v,
                              int wrap_u, int wrap_v, int linear,
                              float *a, float *r, float *g, float *b)
{
    float sx, sy;

    if ((width <= 0) || (height <= 0) || !pitch)
        return 0;

    if (!linear) {
        int x = sis6306_3d_floori(u * (float) width);
        int y = sis6306_3d_floori(v * (float) height);
        uint8_t ta, tr, tg, tb;
        if (!sis6306_3d_texel8888(dev, mem_size, base, pitch, width, height, x, y,
                                  wrap_u, wrap_v, &ta, &tr, &tg, &tb))
            return 0;
        *a = (float) ta;
        *r = (float) tr;
        *g = (float) tg;
        *b = (float) tb;
        return 1;
    }

    sx = (u * (float) width) - 0.5f;
    sy = (v * (float) height) - 0.5f;
    {
        const int raw_x0 = sis6306_3d_floori(sx);
        const int raw_y0 = sis6306_3d_floori(sy);
        const float fx = sx - (float) raw_x0;
        const float fy = sy - (float) raw_y0;
        int x0, x1, y0, y1;
        uint32_t p00, p10, p01, p11;

        sis6306_3d_texcoord_pair(raw_x0, width, wrap_u, &x0, &x1);
        sis6306_3d_texcoord_pair(raw_y0, height, wrap_v, &y0, &y1);
        {
            const uint64_t row0 = (uint64_t) base + ((uint64_t) y0 * pitch);
            const uint64_t row1 = (uint64_t) base + ((uint64_t) y1 * pitch);
            const uint32_t xo0 = (uint32_t) x0 << 2;
            const uint32_t xo1 = (uint32_t) x1 << 2;
            const uint32_t xmax = ((xo0 > xo1) ? xo0 : xo1) + 3U;
            const uint8_t *vram = dev->svga.vram;
            uint32_t addr00, addr10, addr01, addr11;

            if ((row0 + xmax >= mem_size) || (row1 + xmax >= mem_size))
                return 0;

            addr00 = (uint32_t) row0 + xo0;
            addr10 = (uint32_t) row0 + xo1;
            addr01 = (uint32_t) row1 + xo0;
            addr11 = (uint32_t) row1 + xo1;

            p00 = sis6306_3d_load_argb8888(vram, addr00);
            p10 = sis6306_3d_load_argb8888(vram, addr10);
            p01 = sis6306_3d_load_argb8888(vram, addr01);
            p11 = sis6306_3d_load_argb8888(vram, addr11);
        }

        sis6306_3d_bilerp_argb8888(p00, p10, p01, p11, fx, fy,
                                    a, r, g, b);
    }
    return 1;
}

static inline unsigned
sis6306_3d_texture_pitch_bytes(unsigned ctl)
{
    /*
     * The SiS manual calls TTXnPCTL a "Pitch Control" field rather than
     * a byte pitch.  The Windows driver programs 180h, 100h, 080h, 000h
     * for the 16x16 RGB565 mip chain whose actual row pitches are
     * 32, 16, 8, 4 bytes.  That is a log2 pitch in DWORD units:
     *     pitch_bytes = 4 << (ctl >> 7)..
     */
    if ((ctl & 0x7f) || ((ctl >> 7) > 9))
        return 0;
    return 4U << (ctl >> 7);
}

static inline unsigned
sis6306_3d_texture_pitch_for_format(unsigned ctl, unsigned tex_fmt)
{
    (void) tex_fmt;
    return sis6306_3d_texture_pitch_bytes(ctl);
}

static int
sis6306_3d_prepare(sis6306_t *dev, sis6306_3d_prepared_t *out)
{


    sis6306_3d_vertex_t v[3];
    const uint32_t primitive = sis6306_3d_reg32(dev, 0x89f8);
    const uint32_t enable    = sis6306_3d_reg32(dev, 0x8a00);
    const uint32_t dst1      = sis6306_3d_reg32(dev, 0x8a14);
    const uint32_t dst_base  = sis6306_3d_reg32(dev, 0x8a18);
    const uint32_t clip_tb   = sis6306_3d_reg32(dev, 0x8a30);
    const uint32_t clip_lr   = sis6306_3d_reg32(dev, 0x8a34);
    const uint32_t z1        = sis6306_3d_reg32(dev, 0x8a04);
    const uint32_t zbase     = sis6306_3d_reg32(dev, 0x8a08);
    const uint32_t fog       = sis6306_3d_reg32(dev, 0x8a20);
    const uint32_t misc2     = sis6306_3d_reg32(dev, 0x8a28);
    const uint32_t tex1      = sis6306_3d_reg32(dev, 0x8a38);
    const uint32_t tex2      = sis6306_3d_reg32(dev, 0x8a3c);
    const uint32_t tex0base  = sis6306_3d_reg32(dev, 0x8a44);
    const uint32_t texp01    = sis6306_3d_reg32(dev, 0x8a6c);
    const uint32_t texsize   = sis6306_3d_reg32(dev, 0x8a80);
    const unsigned draw      = primitive & 7;
    const unsigned shade     = (primitive >> 18) & 7;
    const unsigned rop       = (dst1 >> 24) & 0x0f;
    const unsigned dst_fmt   = (dst1 >> 16) & 0x7f;
    const unsigned pitch     = dst1 & 0x3fff;
    const unsigned zfmt      = (z1 >> 20) & 3;
    const unsigned zmode     = (z1 >> 16) & 7;
    const unsigned zpitch    = z1 & 0x3fff;
    const int ztest_en       = !!(enable & (1U << 20));
    const int zwrite_en      = !!(enable & (1U << 21));
    const int alpha_test_en  = !!(enable & (1U << 17));
    const uint32_t alpha1_live = sis6306_3d_reg32(dev, 0x8a0c);
    const unsigned alpha_test_mode = (alpha1_live >> 24) & 7;
    const uint8_t alpha_ref = (uint8_t) (alpha1_live >> 16);
    const int fog_en         = !!(enable & (1U << 3));
    const int blend_en       = !!(enable & (1U << 2));
    const int tex_en         = !!(enable & (1U << 10));
    const int persp_en       = !!(enable & (1U << 9));
    const unsigned tex_fmt   = (tex1 >> 24) & 0xff;
    const unsigned tex_map   = (tex1 >> 16) & 0xff;
    const unsigned tex_min   = tex1 & 7U;
    const int tex_mag_linear = !!(tex1 & 8);
    const int tex_sysmem     = !!(tex1 & (1U << 5));
    const unsigned tex_blend = (tex2 >> 26) & 0x3f;
    const unsigned tex_alpha_blend = (tex2 >> 24) & 0x03;
    const unsigned blend_dst = (misc2 >> 28) & 0x0f;
    const unsigned blend_src = (misc2 >> 24) & 0x0f;
    const unsigned tex_pitch0_ctl = (texp01 >> 16) & 0x7ff;
    const unsigned tex_pitch0 = sis6306_3d_texture_pitch_for_format(tex_pitch0_ctl, tex_fmt);
    const uint32_t mem_size = sis6306_configured_vram_size(dev);
    const int changeframe = dev->svga.monitor->mon_changeframecount;
    const int tex_w = 1 << ((texsize >> 28) & 0x0f);
    const int tex_h = 1 << ((texsize >> 24) & 0x0f);
    const unsigned tex_level_ctl = (tex1 >> 8) & 0x0f;
    uint32_t tex_level_base[10] = {0};
    unsigned tex_level_pitch[10] = {0};
    int tex_level_w[10] = {0};
    int tex_level_h[10] = {0};
    unsigned tex_max_level = 0;
    int left   = sis6306_3d_s12((clip_lr >> 13) & 0x1fff);
    int right  = sis6306_3d_s12(clip_lr & 0x1fff);
    int top    = sis6306_3d_s12((clip_tb >> 13) & 0x1fff);
    int bottom = sis6306_3d_s12(clip_tb & 0x1fff);
    int bgr = 0;
    unsigned dst_bytespp = 0;
    int min_x, max_x, min_y, max_y;
    float area;
    float inv_area;
    float bw0_dx = 0.0f, bw1_dx = 0.0f, bw2_dx = 0.0f;
    float bw0_dy = 0.0f, bw1_dy = 0.0f, bw2_dy = 0.0f;
    float tu_num_dx = 0.0f, tv_num_dx = 0.0f, tw_den_dx = 0.0f;
    float tu_num_dy = 0.0f, tv_num_dy = 0.0f, tw_den_dy = 0.0f;
    float tu_aff_dx = 0.0f, tv_aff_dx = 0.0f;
    float tu_aff_dy = 0.0f, tv_aff_dy = 0.0f;


    if ((draw != 2) || !(enable & (1U << 11)) ||
        (enable & ((1U << 18) | (1U << 16) |
                   (1U << 8) | (1U << 1))) ||
        (rop != 0x0c) || !pitch) {
        return 0;
    }

    if (ztest_en && ((zfmt != 1) || !zpitch ||
                     (zbase >= mem_size))) {
        return 0;
    }

    if (fog_en && !(fog & (1U << 24))) {
        return 0;
    }

    if (blend_en && !(((blend_dst == 1) && (blend_src == 1)) ||
                      ((blend_dst == 1) && (blend_src == 4)) ||
                      ((blend_dst == 5) && (blend_src == 4)) ||
                      ((blend_dst == 0) && (blend_src == 8)) ||
                      ((blend_dst == 2) && (blend_src == 0)) ||
                      ((blend_dst == 2) && (blend_src == 8)))) {
        return 0;
    }

    if (tex_en) {
        const int tex555_replace_ok = (tex_fmt == 0x50) && (tex_blend == 0) &&
                                      (tex_alpha_blend <= 2);
        const int tex555_passthrough_ok = (tex_fmt == 0x50) && (tex_blend == 1) &&
                                          (tex_alpha_blend <= 2);
        const int tex555_modulate_ok = (tex_fmt == 0x50) && (tex_blend == 2) &&
                                       (tex_alpha_blend <= 2);
        const int tex555_ok = tex555_replace_ok || tex555_passthrough_ok ||
                              tex555_modulate_ok;
        const int tex565_replace_ok = (tex_fmt == 0x51) && (tex_blend == 0) &&
                                      ((tex_alpha_blend == 0) || (tex_alpha_blend == 1));
        const int tex565_modulate_ok = (tex_fmt == 0x51) && (tex_blend == 2) &&
                                       ((tex_alpha_blend == 0) || (tex_alpha_blend == 1));
        const int tex565_ok = tex565_replace_ok || tex565_modulate_ok;
        const int tex1555_replace_ok = (tex_fmt == 0x52) && (tex_blend == 0) &&
                                       (tex_alpha_blend <= 2);
        const int tex1555_modulate_ok = (tex_fmt == 0x52) && (tex_blend == 2) &&
                                        (tex_alpha_blend <= 2);
        const int tex1555_ok = tex1555_replace_ok || tex1555_modulate_ok;
        const int tex4444_replace_ok = (tex_fmt == 0x53) && (tex_blend == 0) &&
                                       ((tex_alpha_blend == 0) || (tex_alpha_blend == 2));
        /* TTXBLAMD is independent of TTXBLCMD for ARGB textures. */
        const int tex4444_modulate_ok = (tex_fmt == 0x53) && (tex_blend == 2) &&
                                        (tex_alpha_blend <= 2);
        const int tex4444_ok = tex4444_replace_ok || tex4444_modulate_ok;
        /* ARGB8888 states:
         *   TTXBLCMD=0, TTXBLAMD=0: Cout=Ctex,      Aout=Atex
         *   TTXBLCMD=0, TTXBLAMD=1: Cout=Ctex,      Aout=Apix
         *   TTXBLCMD=0, TTXBLAMD=2: Cout=Ctex,      Aout=Apix*Atex
         *   TTXBLCMD=2, TTXBLAMD=0: Cout=Cpix*Ctex, Aout=Atex
         *   TTXBLCMD=2, TTXBLAMD=1: Cout=Cpix*Ctex, Aout=Apix
         */
        const int tex8888_replace_ok = (tex_fmt == 0x73) && (tex_blend == 0) &&
                                       (tex_alpha_blend <= 2);
        const int tex8888_modulate_ok = (tex_fmt == 0x73) && (tex_blend == 2) &&
                                        (tex_alpha_blend <= 1);
        const int tex8888_ok = tex8888_replace_ok || tex8888_modulate_ok;
        if (tex_sysmem || !(tex555_ok || tex565_ok || tex1555_ok ||
                            tex4444_ok || tex8888_ok) ||
            !tex_pitch0 || (tex0base >= mem_size) ||
            (tex_w > 512) || (tex_h > 512) ||
            ((tex_map & 0x03) != 0x03)) {
            return 0;
        }
    } else if (persp_en) {
        return 0;
    }

    if (tex_en) {
        unsigned max_dim_level = ((texsize >> 28) & 0x0f);
        const unsigned h_level = ((texsize >> 24) & 0x0f);
        if (h_level > max_dim_level)
            max_dim_level = h_level;
        tex_max_level = tex_level_ctl;
        if (tex_max_level > max_dim_level)
            tex_max_level = max_dim_level;
        if (tex_max_level > 9)
            tex_max_level = 9;

        for (unsigned level = 0; level <= tex_max_level; level++) {
            const uint32_t pctl = sis6306_3d_reg32(dev, 0x8a6c + ((level >> 1) * 4));
            const unsigned ctl = (level & 1) ? (pctl & 0x7ff) : ((pctl >> 16) & 0x7ff);
            tex_level_base[level] = sis6306_3d_reg32(dev, 0x8a44 + (level * 4));
            tex_level_pitch[level] = sis6306_3d_texture_pitch_for_format(ctl, tex_fmt);
            tex_level_w[level] = tex_w >> level;
            tex_level_h[level] = tex_h >> level;
            if (tex_level_w[level] < 1) tex_level_w[level] = 1;
            if (tex_level_h[level] < 1) tex_level_h[level] = 1;
        }

    }

    if (dst_fmt == 0x11) {
        bgr = 0;
        dst_bytespp = 2;
    } else if (dst_fmt == 0x51) {
        bgr = 1;
        dst_bytespp = 2;
    } else if (dst_fmt == 0x34) {
        bgr = 0;
        dst_bytespp = 4;
    } else {
        return 0;
    }

    sis6306_3d_get_vertex(dev, 0, &v[0]);
    sis6306_3d_get_vertex(dev, 1, &v[1]);
    sis6306_3d_get_vertex(dev, 2, &v[2]);

    if (!((v[0].x == v[0].x) && (v[0].y == v[0].y) &&
          (v[1].x == v[1].x) && (v[1].y == v[1].y) &&
          (v[2].x == v[2].x) && (v[2].y == v[2].y))) {
        return 0;
    }

    if ((right < left) || (bottom < top)) {
        return 0;
    }

    /* The destination pitch is authoritative for the horizontal extent. */
    {
        const int pitch_right = (int) (pitch / dst_bytespp) - 1;
        if (pitch_right < right)
            right = pitch_right;
    }

    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if ((right < left) || (bottom < top))
        return 0;


    min_x = (int) v[0].x;
    max_x = (int) v[0].x;
    min_y = (int) v[0].y;
    max_y = (int) v[0].y;

    for (unsigned n = 1; n < 3; n++) {
        const int x = (int) v[n].x;
        const int y = (int) v[n].y;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }

    if (min_x < left)   min_x = left;
    if (max_x > right)  max_x = right;
    if (min_y < top)    min_y = top;
    if (max_y > bottom) max_y = bottom;
    if ((max_x < min_x) || (max_y < min_y))
        return 0;

    area = sis6306_3d_edge(v[0].x, v[0].y, v[1].x, v[1].y,
                           v[2].x, v[2].y);
    if ((area > -0.000001f) && (area < 0.000001f))
        return 0;
    inv_area = 1.0f / area;

    bw0_dx = (v[2].y - v[1].y) * inv_area;
    bw1_dx = (v[0].y - v[2].y) * inv_area;
    bw2_dx = (v[1].y - v[0].y) * inv_area;
    bw0_dy = -(v[2].x - v[1].x) * inv_area;
    bw1_dy = -(v[0].x - v[2].x) * inv_area;
    bw2_dy = -(v[1].x - v[0].x) * inv_area;

    if (tex_en) {
        if (persp_en) {
            tu_num_dx = bw0_dx * v[0].u * v[0].w +
                        bw1_dx * v[1].u * v[1].w +
                        bw2_dx * v[2].u * v[2].w;
            tv_num_dx = bw0_dx * v[0].v * v[0].w +
                        bw1_dx * v[1].v * v[1].w +
                        bw2_dx * v[2].v * v[2].w;
            tw_den_dx = bw0_dx * v[0].w + bw1_dx * v[1].w + bw2_dx * v[2].w;
            tu_num_dy = bw0_dy * v[0].u * v[0].w +
                        bw1_dy * v[1].u * v[1].w +
                        bw2_dy * v[2].u * v[2].w;
            tv_num_dy = bw0_dy * v[0].v * v[0].w +
                        bw1_dy * v[1].v * v[1].w +
                        bw2_dy * v[2].v * v[2].w;
            tw_den_dy = bw0_dy * v[0].w + bw1_dy * v[1].w + bw2_dy * v[2].w;
        } else {
            tu_aff_dx = bw0_dx * v[0].u + bw1_dx * v[1].u + bw2_dx * v[2].u;
            tv_aff_dx = bw0_dx * v[0].v + bw1_dx * v[1].v + bw2_dx * v[2].v;
            tu_aff_dy = bw0_dy * v[0].u + bw1_dy * v[1].u + bw2_dy * v[2].u;
            tv_aff_dy = bw0_dy * v[0].v + bw1_dy * v[1].v + bw2_dy * v[2].v;
        }
    }

    const float iz_dx = ztest_en ?
        (bw0_dx * v[0].z + bw1_dx * v[1].z + bw2_dx * v[2].z) : 0.0f;
    const float ia_dx = (shade == 4) ?
        (bw0_dx * v[0].a + bw1_dx * v[1].a + bw2_dx * v[2].a) : 0.0f;
    const float ir_dx = (shade == 4) ?
        (bw0_dx * v[0].r + bw1_dx * v[1].r + bw2_dx * v[2].r) : 0.0f;
    const float ig_dx = (shade == 4) ?
        (bw0_dx * v[0].g + bw1_dx * v[1].g + bw2_dx * v[2].g) : 0.0f;
    const float ib_dx = (shade == 4) ?
        (bw0_dx * v[0].b + bw1_dx * v[1].b + bw2_dx * v[2].b) : 0.0f;
    const float ifog_dx = fog_en ?
        (bw0_dx * v[0].fog + bw1_dx * v[1].fog + bw2_dx * v[2].fog) : 0.0f;
    const float it0_dx = tex_en ? (persp_en ? tw_den_dx : tu_aff_dx) : 0.0f;
    const float it1_dx = tex_en ? (persp_en ? tu_num_dx : tv_aff_dx) : 0.0f;
    const float it2_dx = (tex_en && persp_en) ? tv_num_dx : 0.0f;

    const int common_fog565_fast = (dst_bytespp == 2) &&
                                   ztest_en && zwrite_en && (zmode == 3) && fog_en &&
                                   !alpha_test_en && !tex_en && !blend_en && (shade == 4);

    const int common_texfog565_fast = (dst_fmt == 0x11) &&
                                      (dst_bytespp == 2) && !bgr &&
                                      ztest_en && zwrite_en && (zmode == 3) && (zfmt == 1) &&
                                      fog_en && !blend_en && !alpha_test_en &&
                                      tex_en && persp_en && (shade == 4) &&
                                      (tex_fmt == 0x51) && (tex_min == 5) && tex_mag_linear &&
                                      (tex_blend == 2) && (tex_alpha_blend == 1) &&
                                      !tex_sysmem && (rop == 0x0c) && (tex_map == 0x03);
    const float common_fog_r = (float) ((fog >> 16) & 0xff);
    const float common_fog_g = (float) ((fog >> 8) & 0xff);
    const float common_fog_b = (float) (fog & 0xff);

    const float e0_dx = v[2].y - v[1].y;
    const float e1_dx = v[0].y - v[2].y;
    const float e2_dx = v[1].y - v[0].y;

    const int use_row_span = 1;
    const unsigned span_ea[3] = {1U, 2U, 0U};
    const unsigned span_eb[3] = {2U, 0U, 1U};
    float span_slope[3] = {0.0f, 0.0f, 0.0f};
    float span_ymin[3], span_ymax[3];
    int span_horizontal[3] = {0, 0, 0};

    if (use_row_span) {
        for (unsigned se = 0; se < 3; se++) {
            const sis6306_3d_vertex_t *a = &v[span_ea[se]];
            const sis6306_3d_vertex_t *b = &v[span_eb[se]];
            const float dy = b->y - a->y;

            span_ymin[se] = (a->y < b->y) ? a->y : b->y;
            span_ymax[se] = (a->y > b->y) ? a->y : b->y;
            if (dy == 0.0f)
                span_horizontal[se] = 1;
            else
                span_slope[se] = (b->x - a->x) / dy;
        }
    }


    memcpy(out->v, v, sizeof(out->v));
    out->primitive = primitive;
    out->dst_base = dst_base;
    out->zbase = zbase;
    out->fog = fog;
    out->shade = shade;
    out->pitch = pitch;
    out->zmode = zmode;
    out->zpitch = zpitch;
    out->ztest_en = ztest_en;
    out->zwrite_en = zwrite_en;
    out->alpha_test_en = alpha_test_en;
    out->alpha_test_mode = alpha_test_mode;
    out->alpha_ref = alpha_ref;
    out->fog_en = fog_en;
    out->blend_en = blend_en;
    out->tex_en = tex_en;
    out->persp_en = persp_en;
    out->tex_fmt = tex_fmt;
    out->tex_min = tex_min;
    out->tex_mag_linear = tex_mag_linear;
    out->tex_blend = tex_blend;
    out->tex_alpha_blend = tex_alpha_blend;
    out->blend_dst = blend_dst;
    out->blend_src = blend_src;
    out->mem_size = mem_size;
    out->changeframe = changeframe;
    out->tex_w = tex_w;
    out->tex_h = tex_h;
    memcpy(out->tex_level_base, tex_level_base, sizeof(out->tex_level_base));
    memcpy(out->tex_level_pitch, tex_level_pitch, sizeof(out->tex_level_pitch));
    memcpy(out->tex_level_w, tex_level_w, sizeof(out->tex_level_w));
    memcpy(out->tex_level_h, tex_level_h, sizeof(out->tex_level_h));
    out->tex_max_level = tex_max_level;
    out->bgr = bgr;
    out->dst_bytespp = dst_bytespp;
    out->min_x = min_x;
    out->max_x = max_x;
    out->min_y = min_y;
    out->max_y = max_y;
    out->area = area;
    out->inv_area = inv_area;
    out->tu_num_dx = tu_num_dx;
    out->tv_num_dx = tv_num_dx;
    out->tw_den_dx = tw_den_dx;
    out->tu_num_dy = tu_num_dy;
    out->tv_num_dy = tv_num_dy;
    out->tw_den_dy = tw_den_dy;
    out->tu_aff_dx = tu_aff_dx;
    out->tv_aff_dx = tv_aff_dx;
    out->tu_aff_dy = tu_aff_dy;
    out->tv_aff_dy = tv_aff_dy;
    out->iz_dx = iz_dx;
    out->ia_dx = ia_dx;
    out->ir_dx = ir_dx;
    out->ig_dx = ig_dx;
    out->ib_dx = ib_dx;
    out->ifog_dx = ifog_dx;
    out->it0_dx = it0_dx;
    out->it1_dx = it1_dx;
    out->it2_dx = it2_dx;
    out->common_fog565_fast = common_fog565_fast;
    out->common_texfog565_fast = common_texfog565_fast;
    out->common_fog_r = common_fog_r;
    out->common_fog_g = common_fog_g;
    out->common_fog_b = common_fog_b;
    out->e0_dx = e0_dx;
    out->e1_dx = e1_dx;
    out->e2_dx = e2_dx;
    out->use_row_span = use_row_span;
    memcpy(out->span_ea, span_ea, sizeof(out->span_ea));
    memcpy(out->span_eb, span_eb, sizeof(out->span_eb));
    memcpy(out->span_slope, span_slope, sizeof(out->span_slope));
    memcpy(out->span_ymin, span_ymin, sizeof(out->span_ymin));
    memcpy(out->span_ymax, span_ymax, sizeof(out->span_ymax));
    memcpy(out->span_horizontal, span_horizontal, sizeof(out->span_horizontal));

    return 1;
}

static void
sis6306_3d_raster_prepared(sis6306_t *dev, unsigned render_lane,
                           const sis6306_3d_prepared_t *p)
{

    const sis6306_3d_vertex_t *v = p->v;
    const uint32_t primitive = p->primitive;
    const uint32_t dst_base = p->dst_base;
    const uint32_t zbase = p->zbase;
    const uint32_t fog = p->fog;
    const unsigned shade = p->shade;
    const unsigned pitch = p->pitch;
    const unsigned zmode = p->zmode;
    const unsigned zpitch = p->zpitch;
    const int ztest_en = p->ztest_en;
    const int zwrite_en = p->zwrite_en;
    const int alpha_test_en = p->alpha_test_en;
    const unsigned alpha_test_mode = p->alpha_test_mode;
    const uint8_t alpha_ref = p->alpha_ref;
    const int fog_en = p->fog_en;
    const int blend_en = p->blend_en;
    const int tex_en = p->tex_en;
    const int persp_en = p->persp_en;
    const unsigned tex_fmt = p->tex_fmt;
    const unsigned tex_min = p->tex_min;
    const int tex_mag_linear = p->tex_mag_linear;
    const unsigned tex_blend = p->tex_blend;
    const unsigned tex_alpha_blend = p->tex_alpha_blend;
    const unsigned blend_dst = p->blend_dst;
    const unsigned blend_src = p->blend_src;
    const uint32_t mem_size = p->mem_size;
    const int changeframe = p->changeframe;
    uint32_t opaque565_changed_page = UINT32_MAX;
    const int tex_w = p->tex_w;
    const int tex_h = p->tex_h;
    const uint32_t *tex_level_base = p->tex_level_base;
    const unsigned *tex_level_pitch = p->tex_level_pitch;
    const int *tex_level_w = p->tex_level_w;
    const int *tex_level_h = p->tex_level_h;
    const unsigned tex_max_level = p->tex_max_level;
    const int bgr = p->bgr;
    const unsigned dst_bytespp = p->dst_bytespp;
    const int min_x = p->min_x;
    const int max_x = p->max_x;
    const int min_y = p->min_y;
    const int max_y = p->max_y;
    const float area = p->area;
    const float inv_area = p->inv_area;
    const float tu_num_dx = p->tu_num_dx;
    const float tv_num_dx = p->tv_num_dx;
    const float tw_den_dx = p->tw_den_dx;
    const float tu_num_dy = p->tu_num_dy;
    const float tv_num_dy = p->tv_num_dy;
    const float tw_den_dy = p->tw_den_dy;
    const float tu_aff_dx = p->tu_aff_dx;
    const float tv_aff_dx = p->tv_aff_dx;
    const float tu_aff_dy = p->tu_aff_dy;
    const float tv_aff_dy = p->tv_aff_dy;
    const float iz_dx = p->iz_dx;
    const float ia_dx = p->ia_dx;
    const float ir_dx = p->ir_dx;
    const float ig_dx = p->ig_dx;
    const float ib_dx = p->ib_dx;
    const float ifog_dx = p->ifog_dx;
    const float it0_dx = p->it0_dx;
    const float it1_dx = p->it1_dx;
    const float it2_dx = p->it2_dx;
    const int common_fog565_fast = p->common_fog565_fast;
    const int common_texfog565_fast = p->common_texfog565_fast;
    const float common_fog_r = p->common_fog_r;
    const float common_fog_g = p->common_fog_g;
    const float common_fog_b = p->common_fog_b;
    const float e0_dx = p->e0_dx;
    const float e1_dx = p->e1_dx;
    const float e2_dx = p->e2_dx;
    const int use_row_span = p->use_row_span;
    const unsigned *span_ea = p->span_ea;
    const unsigned *span_eb = p->span_eb;
    const float *span_slope = p->span_slope;
    const float *span_ymin = p->span_ymin;
    const float *span_ymax = p->span_ymax;
    const int *span_horizontal = p->span_horizontal;
    int first_y = min_y;
    const unsigned render_threads = dev->d3_render_threads;
    const unsigned first_class = (unsigned) first_y % render_threads;
    first_y += (int) ((render_lane + render_threads - first_class) %
                     render_threads);
    for (int y = first_y; y <= max_y; y += (int) render_threads) {
        const float py = (float) y + 0.5f;
        int row_min_x = min_x;
        int row_max_x = max_x;
        int row_span_authoritative = 0;

        if (use_row_span) {
            float span_lo = 0.0f, span_hi = 0.0f;
            unsigned span_hits = 0;

            for (unsigned se = 0; se < 3; se++) {
                const sis6306_3d_vertex_t *a = &v[span_ea[se]];
                const sis6306_3d_vertex_t *b = &v[span_eb[se]];

                if (span_horizontal[se]) {
                    if (py == a->y) {
                        const float x0 = a->x;
                        const float x1 = b->x;
                        if (!span_hits) {
                            span_lo = span_hi = x0;
                        } else {
                            if (x0 < span_lo) span_lo = x0;
                            if (x0 > span_hi) span_hi = x0;
                        }
                        span_hits++;
                        if (x1 < span_lo) span_lo = x1;
                        if (x1 > span_hi) span_hi = x1;
                        span_hits++;
                    }
                } else if ((py >= span_ymin[se]) && (py <= span_ymax[se])) {
                    const float xhit = a->x + (py - a->y) * span_slope[se];
                    if (!span_hits) {
                        span_lo = span_hi = xhit;
                    } else {
                        if (xhit < span_lo) span_lo = xhit;
                        if (xhit > span_hi) span_hi = xhit;
                    }
                    span_hits++;
                }
            }

            if (span_hits >= 2) {
                const int candidate_min = -sis6306_3d_floori(0.5f - span_lo);
                const int candidate_max = sis6306_3d_floori(span_hi - 0.5f);
                if (candidate_min > row_min_x) row_min_x = candidate_min;
                if (candidate_max < row_max_x) row_max_x = candidate_max;
                if (row_max_x < row_min_x)
                    continue;
                row_span_authoritative = 1;
            }
        }

        const float px0 = (float) row_min_x + 0.5f;
        float e0 = sis6306_3d_edge(v[1].x, v[1].y,
                                    v[2].x, v[2].y, px0, py);
        float e1 = sis6306_3d_edge(v[2].x, v[2].y,
                                    v[0].x, v[0].y, px0, py);
        float e2 = sis6306_3d_edge(v[0].x, v[0].y,
                                    v[1].x, v[1].y, px0, py);

        const float row_w0 = e0 * inv_area;
        const float row_w1 = e1 * inv_area;
        const float row_w2 = e2 * inv_area;
        float iz = ztest_en ?
            (row_w0 * v[0].z + row_w1 * v[1].z + row_w2 * v[2].z) : 0.0f;
        float ia = (shade == 4) ?
            (row_w0 * v[0].a + row_w1 * v[1].a + row_w2 * v[2].a) : 0.0f;
        float ir = (shade == 4) ?
            (row_w0 * v[0].r + row_w1 * v[1].r + row_w2 * v[2].r) : 0.0f;
        float ig = (shade == 4) ?
            (row_w0 * v[0].g + row_w1 * v[1].g + row_w2 * v[2].g) : 0.0f;
        float ib = (shade == 4) ?
            (row_w0 * v[0].b + row_w1 * v[1].b + row_w2 * v[2].b) : 0.0f;
        float ifog = fog_en ?
            (row_w0 * v[0].fog + row_w1 * v[1].fog + row_w2 * v[2].fog) : 0.0f;
        float it0 = 0.0f, it1 = 0.0f, it2 = 0.0f;
        if (tex_en) {
            if (persp_en) {
                it0 = row_w0 * v[0].w + row_w1 * v[1].w + row_w2 * v[2].w;
                it1 = row_w0 * v[0].u * v[0].w +
                      row_w1 * v[1].u * v[1].w +
                      row_w2 * v[2].u * v[2].w;
                it2 = row_w0 * v[0].v * v[0].w +
                      row_w1 * v[1].v * v[1].w +
                      row_w2 * v[2].v * v[2].w;
            } else {
                it0 = row_w0 * v[0].u + row_w1 * v[1].u + row_w2 * v[2].u;
                it1 = row_w0 * v[0].v + row_w1 * v[1].v + row_w2 * v[2].v;
            }
        }

        uint64_t dst64_step = (uint64_t) dst_base +
                              ((uint64_t) y * pitch) +
                              ((uint64_t) row_min_x * dst_bytespp);
        uint64_t z64_step = ztest_en ?
                            ((uint64_t) zbase + ((uint64_t) y * zpitch) +
                             ((uint64_t) row_min_x << 1)) : 0;

        const uint64_t row_span_pixels = (uint64_t) (row_max_x - row_min_x);
        const uint64_t dst64_last = dst64_step + row_span_pixels * dst_bytespp;
        const int row_dst_in_bounds = (dst64_last + dst_bytespp <= mem_size);
        const uint64_t z64_last = ztest_en ?
                                  (z64_step + (row_span_pixels << 1)) : 0;
        const int row_z_in_bounds = !ztest_en || (z64_last + 1 < mem_size);

        if (common_fog565_fast) {
            for (int x = row_min_x; x <= row_max_x;
                 x++,
                 iz += iz_dx, ir += ir_dx, ig += ig_dx, ib += ib_dx,
                 ifog += ifog_dx, dst64_step += 2, z64_step += 2) {
                if (!row_span_authoritative) {
                    const int outside = (area > 0.0f) ?
                        ((e0 < 0.0f) || (e1 < 0.0f) || (e2 < 0.0f)) :
                        ((e0 > 0.0f) || (e1 > 0.0f) || (e2 > 0.0f));
                    e0 += e0_dx;
                    e1 += e1_dx;
                    e2 += e2_dx;
                    if (outside) {
                        continue;
                    }
                }

                const uint64_t z64 = z64_step;
                if (!row_z_in_bounds && (z64 + 1 >= mem_size)) {
                    continue;
                }

                const uint32_t zaddr = (uint32_t) z64;
                const uint16_t znew = sis6306_3d_z16(iz);
                const uint16_t zdst = sis6306_3d_read16(dev, zaddr);
                if (znew > zdst) {
                    continue;
                }

                float r = ir;
                float g = ig;
                float b = ib;
                float ff = ifog;
                if (ff < 0.0f)
                    ff = 0.0f;
                else if (ff > 255.0f)
                    ff = 255.0f;

                {
                    const float invf = 255.0f - ff;
                    r = ((r * ff) + (common_fog_r * invf)) / 255.0f;
                    g = ((g * ff) + (common_fog_g * invf)) / 255.0f;
                    b = ((b * ff) + (common_fog_b * invf)) / 255.0f;
                }

                const uint64_t dst64 = dst64_step;
                if (!row_dst_in_bounds && (dst64 + dst_bytespp > mem_size)) {
                    continue;
                }

                sis6306_3d_write_rgb565_opaque_fast(dev, (uint32_t) dst64,
                                                     r, g, b, bgr, changeframe,
                                                     &opaque565_changed_page);
                sis6306_3d_write16(dev, zaddr, znew);
            }
            continue;
        }

        if (common_texfog565_fast) {
            int lod_span_start_x = row_min_x;
            int lod_span_end_x = row_min_x - 1;
            int lod_span_valid = 0;
            unsigned lod_level0 = 0, lod_level1 = 0;
            float lod_level_frac = 0.0f;

            for (int x = row_min_x; x <= row_max_x;
                 x++,
                 iz += iz_dx, ir += ir_dx, ig += ig_dx, ib += ib_dx,
                 ifog += ifog_dx, it0 += it0_dx, it1 += it1_dx, it2 += it2_dx,
                 dst64_step += 2, z64_step += 2) {
                if (((x - row_min_x) & 7) == 0) {
                    lod_span_start_x = x;
                    lod_span_end_x = x + 7;
                    if (lod_span_end_x > row_max_x)
                        lod_span_end_x = row_max_x;
                    lod_span_valid = 0;
                }

                if (!row_span_authoritative) {
                    const int outside = (area > 0.0f) ?
                        ((e0 < 0.0f) || (e1 < 0.0f) || (e2 < 0.0f)) :
                        ((e0 > 0.0f) || (e1 > 0.0f) || (e2 > 0.0f));
                    e0 += e0_dx;
                    e1 += e1_dx;
                    e2 += e2_dx;
                    if (outside) {
                        continue;
                    }
                }

                const uint64_t z64 = z64_step;
                if (!row_z_in_bounds && (z64 + 1 >= mem_size)) {
                    continue;
                }

                const uint32_t zaddr = (uint32_t) z64;
                const uint16_t znew = sis6306_3d_z16(iz);
                const uint16_t zdst = sis6306_3d_read16(dev, zaddr);
                if (znew > zdst) {
                    continue;
                }

                const float rw = it0;
                const float un = it1;
                const float vn = it2;
                float tu, tv;
                float tr, tg, tb;

                if ((rw > -0.00000001f) && (rw < 0.00000001f)) {
                    continue;
                }

                {
                    const float inv_rw = 1.0f / rw;
                    tu = un * inv_rw;
                    tv = vn * inv_rw;
                }

                if (!lod_span_valid) {
                    const int mid_off = (lod_span_end_x - lod_span_start_x) / 2;
                    float mrw = it0 + it0_dx * (float) (lod_span_start_x + mid_off - x);
                    float mun = it1 + it1_dx * (float) (lod_span_start_x + mid_off - x);
                    float mvn = it2 + it2_dx * (float) (lod_span_start_x + mid_off - x);
                    float lambda = 0.0f;

                    if ((mrw > -0.00000001f) && (mrw < 0.00000001f)) {
                        mrw = rw;
                        mun = un;
                        mvn = vn;
                    }

                    if (tex_max_level > 0) {
                        const float inv_mrw = 1.0f / mrw;
                        const float inv_mrw2 = inv_mrw * inv_mrw;
                        const float mdudx = ((tu_num_dx * mrw) - (mun * tw_den_dx)) * inv_mrw2;
                        const float mdvdx = ((tv_num_dx * mrw) - (mvn * tw_den_dx)) * inv_mrw2;
                        const float mdudy = ((tu_num_dy * mrw) - (mun * tw_den_dy)) * inv_mrw2;
                        const float mdvdy = ((tv_num_dy * mrw) - (mvn * tw_den_dy)) * inv_mrw2;
                        const double dxu = (double) mdudx * tex_w;
                        const double dxv = (double) mdvdx * tex_h;
                        const double dyu = (double) mdudy * tex_w;
                        const double dyv = (double) mdvdy * tex_h;
                        const double rx2 = (dxu * dxu) + (dxv * dxv);
                        const double ry2 = (dyu * dyu) + (dyv * dyv);
                        const double rho2 = (rx2 > ry2) ? rx2 : ry2;

                        if (rho2 > 1.0) {
                            lambda = (float) (0.5 * log2(rho2));
                            if (lambda < 0.0f) lambda = 0.0f;
                            if (lambda > (float) tex_max_level) lambda = (float) tex_max_level;
                        }
                    }

                    lod_level0 = 0;
                    lod_level1 = 0;
                    lod_level_frac = 0.0f;
                    if (lambda > 0.0f) {
                        int lo = (int) lambda;
                        if (lo < 0) lo = 0;
                        if ((unsigned) lo > tex_max_level) lo = (int) tex_max_level;
                        lod_level0 = (unsigned) lo;
                        lod_level1 = (lod_level0 < tex_max_level) ? lod_level0 + 1 : lod_level0;
                        lod_level_frac = lambda - (float) lod_level0;
                        if (lod_level_frac < 0.0f) lod_level_frac = 0.0f;
                        if (lod_level_frac > 1.0f) lod_level_frac = 1.0f;
                    }
                    lod_span_valid = 1;
                }

                const unsigned level0 = lod_level0;
                const unsigned level1 = lod_level1;
                const float level_frac = lod_level_frac;

                if ((level0 > tex_max_level) || !tex_level_pitch[level0] ||
                    (tex_level_base[level0] >= mem_size)) {
                    continue;
                }

                {
                    float r0s, g0s, b0s;
                    if (!sis6306_3d_sample_texture565(dev, mem_size,
                                                      tex_level_base[level0],
                                                      tex_level_pitch[level0],
                                                      tex_level_w[level0],
                                                      tex_level_h[level0],
                                                      tu, tv, 1, 1, 1, 0,
                                                      &r0s, &g0s, &b0s)) {
                        continue;
                    }

                    if ((level1 != level0) && (level_frac > 0.0f)) {
                        float r1s, g1s, b1s;
                        if (!tex_level_pitch[level1] ||
                            (tex_level_base[level1] >= mem_size)) {
                            continue;
                        }
                        if (!sis6306_3d_sample_texture565(dev, mem_size,
                                                         tex_level_base[level1],
                                                         tex_level_pitch[level1],
                                                         tex_level_w[level1],
                                                         tex_level_h[level1],
                                                         tu, tv, 1, 1, 1, 0,
                                                         &r1s, &g1s, &b1s)) {
                            continue;
                        }
                        r0s += (r1s - r0s) * level_frac;
                        g0s += (g1s - g0s) * level_frac;
                        b0s += (b1s - b0s) * level_frac;
                    }
                    tr = r0s;
                    tg = g0s;
                    tb = b0s;
                }

                /* RGB565 TTXBLCMD=2: Cout = Cpix * Ctex. */
                float r = (ir * tr) / 255.0f;
                float g = (ig * tg) / 255.0f;
                float b = (ib * tb) / 255.0f;
                float ff = ifog;
                if (ff < 0.0f)
                    ff = 0.0f;
                else if (ff > 255.0f)
                    ff = 255.0f;
                {
                    const float invf = 255.0f - ff;
                    r = ((r * ff) + (common_fog_r * invf)) / 255.0f;
                    g = ((g * ff) + (common_fog_g * invf)) / 255.0f;
                    b = ((b * ff) + (common_fog_b * invf)) / 255.0f;
                }

                const uint64_t dst64 = dst64_step;
                if (!row_dst_in_bounds && (dst64 + 2 > mem_size)) {
                    continue;
                }

                sis6306_3d_write_rgb565_opaque_fast(dev, (uint32_t) dst64,
                                                     r, g, b, 0, changeframe,
                                                     &opaque565_changed_page);
                sis6306_3d_write16(dev, zaddr, znew);
            }
            continue;
        }

        for (int x = row_min_x; x <= row_max_x;
             x++,
             iz += iz_dx, ia += ia_dx, ir += ir_dx, ig += ig_dx, ib += ib_dx,
             ifog += ifog_dx, it0 += it0_dx, it1 += it1_dx, it2 += it2_dx,
             dst64_step += dst_bytespp, z64_step += 2) {
            float a, r, g, b;
            uint16_t znew = 0;
            uint32_t zaddr = 0;

            if (!row_span_authoritative) {
                const int outside = (area > 0.0f) ?
                    ((e0 < 0.0f) || (e1 < 0.0f) || (e2 < 0.0f)) :
                    ((e0 > 0.0f) || (e1 > 0.0f) || (e2 > 0.0f));
                e0 += e0_dx;
                e1 += e1_dx;
                e2 += e2_dx;
                if (outside) {
                    continue;
                }
            }


            if (ztest_en) {
                const float zf = iz;
                const uint64_t z64 = z64_step;
                uint16_t zdst;

                if (!row_z_in_bounds && (z64 + 1 >= mem_size)) {
                    continue;
                }

                zaddr = (uint32_t) z64;
                znew = sis6306_3d_z16(zf);
                zdst = sis6306_3d_read16(dev, zaddr);
                if (!sis6306_3d_ztest(zmode, znew, zdst)) {
                    continue;
                }
            }

            if (shade == 4) {
                a = ia;
                r = ir;
                g = ig;
                b = ib;
            } else if ((shade >= 1) && (shade <= 3)) {
                unsigned src;
                if (shade == 1)
                    src = (primitive >> 16) & 3;
                else if (shade == 2)
                    src = (primitive >> 14) & 3;
                else
                    src = (primitive >> 12) & 3;
                if (src > 2) {
                    return;
                }
                a = v[src].a;
                r = v[src].r;
                g = v[src].g;
                b = v[src].b;
            } else {
                return;
            }

            if (tex_en) {
                float tu, tv, ta = 255.0f, tr, tg, tb;
                float dudx, dvdx, dudy, dvdy;
                float lambda = 0.0f;
                unsigned level0 = 0, level1 = 0;
                float level_frac = 0.0f;
                int linear0 = 0, linear1 = 0;
                int sampled = 0;

                if (persp_en) {
                    const float rw = it0;
                    const float un = it1;
                    const float vn = it2;
                    float inv_rw, inv_rw2;
                    if ((rw > -0.00000001f) && (rw < 0.00000001f)) {
                        continue;
                    }

                    inv_rw = 1.0f / rw;
                    tu = un * inv_rw;
                    tv = vn * inv_rw;
                    inv_rw2 = inv_rw * inv_rw;
                    dudx = ((tu_num_dx * rw) - (un * tw_den_dx)) * inv_rw2;
                    dvdx = ((tv_num_dx * rw) - (vn * tw_den_dx)) * inv_rw2;
                    dudy = ((tu_num_dy * rw) - (un * tw_den_dy)) * inv_rw2;
                    dvdy = ((tv_num_dy * rw) - (vn * tw_den_dy)) * inv_rw2;
                } else {
                    tu = it0;
                    tv = it1;
                    dudx = tu_aff_dx; dvdx = tv_aff_dx;
                    dudy = tu_aff_dy; dvdy = tv_aff_dy;
                }

                if ((tex_max_level > 0) && (tex_min >= 2) && (tex_min <= 5)) {
                    const double dxu = (double) dudx * tex_w;
                    const double dxv = (double) dvdx * tex_h;
                    const double dyu = (double) dudy * tex_w;
                    const double dyv = (double) dvdy * tex_h;
                    const double rx2 = (dxu * dxu) + (dxv * dxv);
                    const double ry2 = (dyu * dyu) + (dyv * dyv);
                    const double rho2 = (rx2 > ry2) ? rx2 : ry2;
                    if (rho2 > 1.0) {
                        lambda = (float) (0.5 * log2(rho2));
                        if (lambda < 0.0f) lambda = 0.0f;
                        if (lambda > (float) tex_max_level) lambda = (float) tex_max_level;
                    }
                }

                if (lambda <= 0.0f) {
                    level0 = level1 = 0;
                    linear0 = linear1 = tex_mag_linear;
                } else if ((tex_min == 2) || (tex_min == 4)) {
                    int nearest = (int) (lambda + 0.5f);
                    if (nearest < 0) nearest = 0;
                    if ((unsigned) nearest > tex_max_level) nearest = (int) tex_max_level;
                    level0 = level1 = (unsigned) nearest;
                    linear0 = linear1 = (tex_min == 4);
                } else if ((tex_min == 3) || (tex_min == 5)) {
                    int lo = (int) lambda;
                    if (lo < 0) lo = 0;
                    if ((unsigned) lo > tex_max_level) lo = (int) tex_max_level;
                    level0 = (unsigned) lo;
                    level1 = (level0 < tex_max_level) ? level0 + 1 : level0;
                    level_frac = lambda - (float) level0;
                    if (level_frac < 0.0f) level_frac = 0.0f;
                    if (level_frac > 1.0f) level_frac = 1.0f;
                    linear0 = linear1 = (tex_min == 5);
                } else {
                    level0 = level1 = 0;
                    linear0 = linear1 = (tex_min == 1);
                }

                if ((level0 > tex_max_level) || !tex_level_pitch[level0] ||
                    (tex_level_base[level0] >= mem_size)) {
                    continue;
                }

                if (tex_fmt == 0x50) {
                    float a0s, r0s, g0s, b0s;
                    sampled = sis6306_3d_sample_texture555x(dev, mem_size, tex_level_base[level0],
                                                           tex_level_pitch[level0],
                                                           tex_level_w[level0], tex_level_h[level0],
                                                           tu, tv, 1, 1, linear0, 0,
                                                           &a0s, &r0s, &g0s, &b0s);
                    if (sampled && (level1 != level0) && (level_frac > 0.0f)) {
                        float a1s, r1s, g1s, b1s;
                        if (!tex_level_pitch[level1] ||
                            (tex_level_base[level1] >= mem_size) ||
                            !sis6306_3d_sample_texture555x(dev, mem_size, tex_level_base[level1],
                                                          tex_level_pitch[level1],
                                                          tex_level_w[level1], tex_level_h[level1],
                                                          tu, tv, 1, 1, linear1, 0,
                                                          &a1s, &r1s, &g1s, &b1s))
                            sampled = 0;
                        else {
                            r0s += (r1s - r0s) * level_frac;
                            g0s += (g1s - g0s) * level_frac;
                            b0s += (b1s - b0s) * level_frac;
                        }
                    }
                    tr = r0s; tg = g0s; tb = b0s;
                } else if (tex_fmt == 0x51) {
                    float r0s, g0s, b0s;
                    sampled = sis6306_3d_sample_texture565(dev, mem_size, tex_level_base[level0],
                                                          tex_level_pitch[level0],
                                                          tex_level_w[level0], tex_level_h[level0],
                                                          tu, tv, 1, 1, linear0, 0,
                                                          &r0s, &g0s, &b0s);
                    if (sampled && (level1 != level0) && (level_frac > 0.0f)) {
                        float r1s, g1s, b1s;
                        if (!tex_level_pitch[level1] ||
                            (tex_level_base[level1] >= mem_size) ||
                            !sis6306_3d_sample_texture565(dev, mem_size, tex_level_base[level1],
                                                         tex_level_pitch[level1],
                                                         tex_level_w[level1], tex_level_h[level1],
                                                         tu, tv, 1, 1, linear1, 0,
                                                         &r1s, &g1s, &b1s))
                            sampled = 0;
                        else {
                            r0s += (r1s - r0s) * level_frac;
                            g0s += (g1s - g0s) * level_frac;
                            b0s += (b1s - b0s) * level_frac;
                        }
                    }
                    tr = r0s; tg = g0s; tb = b0s;
                } else if (tex_fmt == 0x52) {
                    float a0s, r0s, g0s, b0s;
                    sampled = sis6306_3d_sample_texture555x(dev, mem_size, tex_level_base[level0],
                                                           tex_level_pitch[level0],
                                                           tex_level_w[level0], tex_level_h[level0],
                                                           tu, tv, 1, 1, linear0, 1,
                                                           &a0s, &r0s, &g0s, &b0s);
                    if (sampled && (level1 != level0) && (level_frac > 0.0f)) {
                        float a1s, r1s, g1s, b1s;
                        if (!tex_level_pitch[level1] ||
                            (tex_level_base[level1] >= mem_size) ||
                            !sis6306_3d_sample_texture555x(dev, mem_size, tex_level_base[level1],
                                                          tex_level_pitch[level1],
                                                          tex_level_w[level1], tex_level_h[level1],
                                                          tu, tv, 1, 1, linear1, 1,
                                                          &a1s, &r1s, &g1s, &b1s))
                            sampled = 0;
                        else {
                            a0s += (a1s - a0s) * level_frac;
                            r0s += (r1s - r0s) * level_frac;
                            g0s += (g1s - g0s) * level_frac;
                            b0s += (b1s - b0s) * level_frac;
                        }
                    }
                    ta = a0s; tr = r0s; tg = g0s; tb = b0s;
                } else if (tex_fmt == 0x53) {
                    float a0s, r0s, g0s, b0s;
                    sampled = sis6306_3d_sample_texture4444(dev, mem_size, tex_level_base[level0],
                                                           tex_level_pitch[level0],
                                                           tex_level_w[level0], tex_level_h[level0],
                                                           tu, tv, 1, 1, linear0,
                                                           &a0s, &r0s, &g0s, &b0s);
                    if (sampled && (level1 != level0) && (level_frac > 0.0f)) {
                        float a1s, r1s, g1s, b1s;
                        if (!tex_level_pitch[level1] ||
                            (tex_level_base[level1] >= mem_size) ||
                            !sis6306_3d_sample_texture4444(dev, mem_size, tex_level_base[level1],
                                                          tex_level_pitch[level1],
                                                          tex_level_w[level1], tex_level_h[level1],
                                                          tu, tv, 1, 1, linear1,
                                                          &a1s, &r1s, &g1s, &b1s))
                            sampled = 0;
                        else {
                            a0s += (a1s - a0s) * level_frac;
                            r0s += (r1s - r0s) * level_frac;
                            g0s += (g1s - g0s) * level_frac;
                            b0s += (b1s - b0s) * level_frac;
                        }
                    }
                    ta = a0s; tr = r0s; tg = g0s; tb = b0s;
                } else {
                    float a0s, r0s, g0s, b0s;
                    sampled = sis6306_3d_sample_texture8888(dev, mem_size, tex_level_base[level0],
                                                           tex_level_pitch[level0],
                                                           tex_level_w[level0], tex_level_h[level0],
                                                           tu, tv, 1, 1, linear0,
                                                           &a0s, &r0s, &g0s, &b0s);
                    if (sampled && (level1 != level0) && (level_frac > 0.0f)) {
                        float a1s, r1s, g1s, b1s;
                        if (!tex_level_pitch[level1] ||
                            (tex_level_base[level1] >= mem_size) ||
                            !sis6306_3d_sample_texture8888(dev, mem_size, tex_level_base[level1],
                                                          tex_level_pitch[level1],
                                                          tex_level_w[level1], tex_level_h[level1],
                                                          tu, tv, 1, 1, linear1,
                                                          &a1s, &r1s, &g1s, &b1s))
                            sampled = 0;
                        else {
                            a0s += (a1s - a0s) * level_frac;
                            r0s += (r1s - r0s) * level_frac;
                            g0s += (g1s - g0s) * level_frac;
                            b0s += (b1s - b0s) * level_frac;
                        }
                    }
                    ta = a0s; tr = r0s; tg = g0s; tb = b0s;
                }
                if (!sampled) {
                    continue;
                }


                if (tex_fmt == 0x50) {
                    if (tex_blend == 0) {
                        r = tr;
                        g = tg;
                        b = tb;
                    } else if (tex_blend == 2) {
                        r = (r * tr) / 255.0f;
                        g = (g * tg) / 255.0f;
                        b = (b * tb) / 255.0f;
                    } else {
                        /* TTXBLCMD=1 for RGB textures is the
                         * documented pass-through path: Cout = Cpix.
                         * Leave interpolated r/g/b unchanged. */
                    }
                    /* RGB textures have no alpha channel; documented
                     * TTXBLAMD 0/1/2 all leave Aout=Apix. */
                } else if (tex_fmt == 0x51) {
                    if (tex_blend == 0) {
                        /* TTXBLCMD=0, RGB565: Cout = Ctex. */
                        r = tr;
                        g = tg;
                        b = tb;
                    } else {
                        /* TTXBLCMD=2, RGB565: Cout = Cpix * Ctex. */
                        r = (r * tr) / 255.0f;
                        g = (g * tg) / 255.0f;
                        b = (b * tb) / 255.0f;
                    }
                } else if (tex_fmt == 0x52) {
                    if (tex_blend == 0) {
                        r = tr;
                        g = tg;
                        b = tb;
                    } else {
                        r = (r * tr) / 255.0f;
                        g = (g * tg) / 255.0f;
                        b = (b * tb) / 255.0f;
                    }
                    if (tex_alpha_blend == 0)
                        a = ta;
                    else if (tex_alpha_blend == 2)
                        a = (a * ta) / 255.0f;
                    /* TTXBLAMD=1 leaves Aout=Apix. */
                } else if (tex_fmt == 0x53) {
                    if (tex_blend == 0) {
                        /*
                         * TTXFM=53h ARGB4444, TTXBLCMD=0:
                         *   Cout = Ctex
                         * TTXBLAMD=0: Aout = Atex
                         * TTXBLAMD=2: Aout = Apix * Atex
                         */
                        r = tr;
                        g = tg;
                        b = tb;
                        if (tex_alpha_blend == 0)
                            a = ta;
                        else
                            a = (a * ta) / 255.0f;
                    } else {
                        /*
                         * ARGB4444 texture modulate:
                         *   TTXBLCMD=2: Cout = Cpix * Ctex
                         *   TTXBLAMD=0: Aout = Atex
                         *   TTXBLAMD=1: Aout = Apix
                         *   TTXBLAMD=2: Aout = Apix * Atex
                         */
                        r = (r * tr) / 255.0f;
                        g = (g * tg) / 255.0f;
                        b = (b * tb) / 255.0f;
                        if (tex_alpha_blend == 0)
                            a = ta;
                        else if (tex_alpha_blend == 2)
                            a = (a * ta) / 255.0f;
                        /* TTXBLAMD=1 leaves a as Apix. */
                    }
                } else {
                    if (tex_blend == 0) {
                        /*
                         * ARGB8888 texture replace:
                         *   TTXBLCMD=0: Cout = Ctex
                         *   TTXBLAMD=0: Aout = Atex
                         *   TTXBLAMD=1: Aout = Apix
                         *   TTXBLAMD=2: Aout = Apix * Atex
                         */
                        r = tr;
                        g = tg;
                        b = tb;
                        if (tex_alpha_blend == 0)
                            a = ta;
                        else if (tex_alpha_blend == 2)
                            a = (a * ta) / 255.0f;
                        /* TTXBLAMD=1 leaves a as Apix. */
                    } else {
                        /*
                         * ARGB8888 texture modulate:
                         *   TTXBLCMD=2: Cout = Cpix * Ctex
                         *   TTXBLAMD=0: Aout = Atex
                         *   TTXBLAMD=1: Aout = Apix
                         */
                        r = (r * tr) / 255.0f;
                        g = (g * tg) / 255.0f;
                        b = (b * tb) / 255.0f;
                        if (tex_alpha_blend == 0)
                            a = ta;
                        /* TTXBLAMD=1 intentionally leaves a as Apix. */
                    }
                }

            }

            if (alpha_test_en) {
                if (!sis6306_3d_alpha_test(alpha_test_mode,
                                           sis6306_3d_clamp8(a), alpha_ref)) {
                    continue;
                }
            }

            if (fog_en) {
                float ff = ifog;
                const float fr = (float) ((fog >> 16) & 0xff);
                const float fg = (float) ((fog >> 8) & 0xff);
                const float fb = (float) (fog & 0xff);

                if (ff < 0.0f)
                    ff = 0.0f;
                else if (ff > 255.0f)
                    ff = 255.0f;

                {
                    const float invf = 255.0f - ff;
                    r = ((r * ff) + (fr * invf)) / 255.0f;
                    g = ((g * ff) + (fg * invf)) / 255.0f;
                    b = ((b * ff) + (fb * invf)) / 255.0f;
                }

            }

            {
                const uint64_t dst64 = dst64_step;
                if (row_dst_in_bounds || (dst64 + dst_bytespp <= mem_size)) {

                    if (blend_en) {
                        float dr, dg, db;
                        sis6306_3d_read_dst_pixel_fast(dev, (uint32_t) dst64,
                                                      dst_bytespp, bgr,
                                                      &dr, &dg, &db);

                        if ((blend_dst == 1) && (blend_src == 1)) {
                            r += dr;
                            g += dg;
                            b += db;
                        } else if ((blend_dst == 1) && (blend_src == 4)) {
                            float af = a / 255.0f;
                            if (af < 0.0f) af = 0.0f;
                            else if (af > 1.0f) af = 1.0f;
                            r = (r * af) + dr;
                            g = (g * af) + dg;
                            b = (b * af) + db;
                        } else if ((blend_dst == 5) && (blend_src == 4)) {
                            float af = a / 255.0f;
                            if (af < 0.0f) af = 0.0f;
                            else if (af > 1.0f) af = 1.0f;
                            r = (r * af) + (dr * (1.0f - af));
                            g = (g * af) + (dg * (1.0f - af));
                            b = (b * af) + (db * (1.0f - af));
                        } else if ((blend_dst == 2) && (blend_src == 8)) {
                            r = (2.0f * r * dr) / 255.0f;
                            g = (2.0f * g * dg) / 255.0f;
                            b = (2.0f * b * db) / 255.0f;
                        } else {
                            r = (r * dr) / 255.0f;
                            g = (g * dg) / 255.0f;
                            b = (b * db) / 255.0f;
                        }
                    }


                    if (!blend_en && (dst_bytespp == 2)) {
                        sis6306_3d_write_rgb565_opaque_fast(dev, (uint32_t) dst64,
                                                           r, g, b, bgr,
                                                           changeframe,
                                                           &opaque565_changed_page);
                    } else {
                        sis6306_3d_write_dst_pixel_fast(dev, (uint32_t) dst64,
                                                       dst_bytespp,
                                                       sis6306_3d_clamp8(r),
                                                       sis6306_3d_clamp8(g),
                                                       sis6306_3d_clamp8(b), bgr,
                                                       changeframe);
                    }


                    if (ztest_en && zwrite_en) {
                        sis6306_3d_write16(dev, zaddr, znew);
                    }
                }
            }
        }
    }
}


static inline uint32_t
sis6306_d3_entries(const sis6306_t *dev, unsigned lane)
{
    return ATOMIC_LOAD(dev->d3_write_idx) - ATOMIC_LOAD(dev->d3_read_idx[lane]);
}

static inline int
sis6306_d3_full(const sis6306_t *dev, unsigned lane)
{
    return sis6306_d3_entries(dev, lane) >= SIS6306_ACCEL_QUEUE_SIZE;
}

static inline int
sis6306_d3_pending_lane(const sis6306_t *dev, unsigned lane)
{
    return (ATOMIC_LOAD(dev->d3_read_idx[lane]) != ATOMIC_LOAD(dev->d3_write_idx)) ||
           ATOMIC_LOAD(dev->d3_busy[lane]);
}

static inline int
sis6306_d3_pending(const sis6306_t *dev)
{
    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
        if (sis6306_d3_pending_lane(dev, lane))
            return 1;
    }
    return 0;
}

static inline int
sis6306_d3_any_full(const sis6306_t *dev)
{
    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
        if (sis6306_d3_full(dev, lane))
            return 1;
    }
    return 0;
}

static void
sis6306_d3_wake(sis6306_t *dev)
{
    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++)
        thread_set_event(dev->d3_wake_event[lane]);
}

static sis6306_3d_work_t *
sis6306_d3_reserve(sis6306_t *dev)
{
    while (sis6306_d3_any_full(dev)) {
        for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
            if (sis6306_d3_full(dev, lane))
                thread_reset_event(dev->d3_not_full_event[lane]);
        }

        sis6306_d3_wake(dev);

        for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
            if (sis6306_d3_full(dev, lane))
                thread_wait_event(dev->d3_not_full_event[lane], -1);
        }
    }

    return &dev->d3_queue[ATOMIC_LOAD(dev->d3_write_idx) & SIS6306_ACCEL_QUEUE_MASK];
}

static void
sis6306_d3_queue_triangle(sis6306_t *dev, const sis6306_accel_work_t *src)
{
    sis6306_3d_work_t *dst = sis6306_d3_reserve(dev);

    sis6306_3d_thread_state = &src->regs.reg3d;
    if (!sis6306_3d_prepare(dev, &dst->prepared)) {
        sis6306_3d_thread_state = NULL;
        return;
    }
    sis6306_3d_thread_state = NULL;

    ATOMIC_INC(dev->d3_write_idx);

    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
        if (sis6306_d3_entries(dev, lane) < 4U) {
            sis6306_d3_wake(dev);
            break;
        }
    }
}

static void
sis6306_d3_render_thread(sis6306_t *dev, unsigned lane)
{
    while (dev->d3_thread_run[lane]) {
        thread_set_event(dev->d3_not_full_event[lane]);
        thread_wait_event(dev->d3_wake_event[lane], -1);
        thread_reset_event(dev->d3_wake_event[lane]);

        ATOMIC_STORE(dev->d3_busy[lane], 1);

        while (ATOMIC_LOAD(dev->d3_read_idx[lane]) != ATOMIC_LOAD(dev->d3_write_idx)) {
            const uint32_t read_idx = ATOMIC_LOAD(dev->d3_read_idx[lane]);
            const sis6306_3d_work_t *work =
                &dev->d3_queue[read_idx & SIS6306_ACCEL_QUEUE_MASK];

            sis6306_3d_raster_prepared(dev, lane, &work->prepared);

            ATOMIC_INC(dev->d3_read_idx[lane]);
            thread_set_event(dev->d3_not_full_event[lane]);
        }

        ATOMIC_STORE(dev->d3_busy[lane], 0);
        thread_set_event(dev->d3_idle_event[lane]);
    }

    sis6306_3d_thread_state = NULL;
    ATOMIC_STORE(dev->d3_busy[lane], 0);
    thread_set_event(dev->d3_not_full_event[lane]);
    thread_set_event(dev->d3_idle_event[lane]);
}

static void
sis6306_d3_thread_0(void *priv)
{
    sis6306_d3_render_thread((sis6306_t *) priv, 0);
}

static void
sis6306_d3_thread_1(void *priv)
{
    sis6306_d3_render_thread((sis6306_t *) priv, 1);
}

static void
sis6306_d3_thread_2(void *priv)
{
    sis6306_d3_render_thread((sis6306_t *) priv, 2);
}

static void
sis6306_d3_thread_3(void *priv)
{
    sis6306_d3_render_thread((sis6306_t *) priv, 3);
}

static void
sis6306_d3_sync(sis6306_t *dev)
{
    while (sis6306_d3_pending(dev)) {
        sis6306_d3_wake(dev);

        for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
            if (sis6306_d3_pending_lane(dev, lane)) {
                thread_reset_event(dev->d3_idle_event[lane]);
                if (sis6306_d3_pending_lane(dev, lane))
                    thread_wait_event(dev->d3_idle_event[lane], 1);
            }
        }
    }
}

static inline uint32_t
sis6306_accel_entries(const sis6306_t *dev)
{
    return ATOMIC_LOAD(dev->accel_write_idx) - ATOMIC_LOAD(dev->accel_read_idx);
}

static inline int
sis6306_accel_full(const sis6306_t *dev)
{
    return sis6306_accel_entries(dev) >= SIS6306_ACCEL_QUEUE_SIZE;
}

static inline int
sis6306_accel_pending(const sis6306_t *dev)
{
    return (ATOMIC_LOAD(dev->accel_write_idx) != ATOMIC_LOAD(dev->accel_read_idx)) ||
           ATOMIC_LOAD(dev->accel_busy) || sis6306_d3_pending(dev);
}

static void
sis6306_accel_wake(sis6306_t *dev)
{
    thread_set_event(dev->accel_wake_event);
}

static sis6306_accel_work_t *
sis6306_accel_reserve(sis6306_t *dev)
{
    while (sis6306_accel_full(dev)) {
        thread_reset_event(dev->accel_not_full_event);
        if (sis6306_accel_full(dev)) {
            sis6306_accel_wake(dev);
            thread_wait_event(dev->accel_not_full_event, -1);
        }
    }

    return &dev->accel_queue[ATOMIC_LOAD(dev->accel_write_idx) & SIS6306_ACCEL_QUEUE_MASK];
}

static void
sis6306_accel_commit(sis6306_t *dev)
{
    dev->accel_needs_sync = 1;
    thread_reset_event(dev->accel_idle_event);
    ATOMIC_INC(dev->accel_write_idx);
    sis6306_accel_wake(dev);
}

static void
sis6306_accel_queue_2d(sis6306_t *dev, uint32_t command)
{
    sis6306_accel_work_t *work = sis6306_accel_reserve(dev);

    work->type = SIS6306_ACCEL_WORK_2D;
    work->bpp = (uint8_t) dev->svga.bpp;
    work->command = command;
    memcpy(work->regs.reg2d, &dev->mmio_regs[SIS6306_2D_REG_BASE],
           SIS6306_2D_REG_SIZE);
    sis6306_accel_commit(dev);
}

static inline uint32_t
sis6306_r180_timing_depth(const sis6306_t *dev)
{
    return dev->r180_timing_write - dev->r180_timing_read;
}

static void
sis6306_r180_timing_retire(sis6306_t *dev, uint64_t now)
{
    if (!dev->r180_finish_tsc)
        return;

    while (dev->r180_timing_read != dev->r180_timing_write) {
        const uint64_t finish =
            dev->r180_finish_tsc[dev->r180_timing_read & SIS6306_TIMING_QUEUE_MASK];
        if (finish > now)
            break;
        dev->r180_timing_read++;
    }
}

static double
sis6306_r180_live_triangle_area(const sis6306_t *dev)
{
    const float ax = sis6306_3d_float(sis6306_3d_live_reg32(dev, 0x8808));
    const float ay = sis6306_3d_float(sis6306_3d_live_reg32(dev, 0x880c));
    const float bx = sis6306_3d_float(sis6306_3d_live_reg32(dev, 0x8828));
    const float by = sis6306_3d_float(sis6306_3d_live_reg32(dev, 0x882c));
    const float cx = sis6306_3d_float(sis6306_3d_live_reg32(dev, 0x8848));
    const float cy = sis6306_3d_float(sis6306_3d_live_reg32(dev, 0x884c));
    const float area = sis6306_3d_edge(ax, ay, bx, by, cx, cy);

    if (!(area == area))
        return 0.0;
    return fabs((double) area) * 0.5;
}

static inline uint64_t
sis6306_r181_effective_tsc(const sis6306_t *dev)
{
    return (dev->r181_virtual_tsc > tsc) ? dev->r181_virtual_tsc : tsc;
}

static void
sis6306_r180_timing_submit(sis6306_t *dev)
{
    uint64_t now = sis6306_r181_effective_tsc(dev);
    const double area_pixels = sis6306_r180_live_triangle_area(dev);
    const double service_usec = SIS6306_TRIANGLE_SETUP_USEC +
        (area_pixels / SIS6306_AREA_PIXELS_PER_USEC);
    uint64_t service_cycles;
    uint64_t start;
    uint64_t finish;
    uint32_t depth;

    if (!dev->r180_finish_tsc)
        return;

    sis6306_r180_timing_retire(dev, now);
    depth = sis6306_r180_timing_depth(dev);

    while (depth >= SIS6306_HW_QUEUE_UNITS) {
        const uint64_t oldest_finish =
            dev->r180_finish_tsc[dev->r180_timing_read & SIS6306_TIMING_QUEUE_MASK];

        if (oldest_finish > now) {
            uint64_t wait_cycles = oldest_finish - now;

            while (wait_cycles) {
                const uint64_t chunk64 = (wait_cycles > 0x3fffffffULL) ?
                    0x3fffffffULL : wait_cycles;
                cycles -= (int32_t) chunk64;
                now += chunk64;
                wait_cycles -= chunk64;
            }

            if (now > dev->r181_virtual_tsc)
                dev->r181_virtual_tsc = now;
        }

        sis6306_r180_timing_retire(dev, now);
        depth = sis6306_r180_timing_depth(dev);

        if (depth >= SIS6306_HW_QUEUE_UNITS &&
            dev->r180_finish_tsc[dev->r180_timing_read & SIS6306_TIMING_QUEUE_MASK] <= now) {
            return;
        }
    }

    if (depth >= SIS6306_TIMING_QUEUE_SIZE) {
        return;
    }

    service_cycles = (uint64_t) ((service_usec * cpuclock / 1000000.0) + 0.5);
    if (!service_cycles)
        service_cycles = 1;

    start = dev->r180_last_finish_tsc;
    if ((dev->r180_timing_read == dev->r180_timing_write) || (start < now))
        start = now;
    finish = start + service_cycles;

    dev->r180_finish_tsc[dev->r180_timing_write & SIS6306_TIMING_QUEUE_MASK] = finish;
    dev->r180_timing_write++;
    dev->r180_last_finish_tsc = finish;


}

static void
sis6306_accel_queue_3d(sis6306_t *dev)
{
    sis6306_accel_work_t *work = sis6306_accel_reserve(dev);

    sis6306_3d_state_t *state = &work->regs.reg3d;

    work->type = SIS6306_ACCEL_WORK_3D;
    work->bpp = (uint8_t) dev->svga.bpp;
    work->command = 0;

    memcpy(state->vertex, &dev->mmio_regs[SIS6306_3D_VERTEX_BASE],
           SIS6306_3D_VERTEX_SIZE);
    memcpy(state->primitive, &dev->mmio_regs[SIS6306_3D_PRIMITIVE_REG],
           sizeof(state->primitive));
    memcpy(state->setup, &dev->mmio_regs[SIS6306_3D_SETUP_BASE],
           SIS6306_3D_SETUP_SIZE);

    sis6306_r180_timing_submit(dev);
    sis6306_accel_commit(dev);
}

static void
sis6306_d2_render_thread(sis6306_t *dev, unsigned lane)
{
    while (dev->d2_thread_run[lane]) {
        thread_wait_event(dev->d2_wake_event[lane], -1);
        thread_reset_event(dev->d2_wake_event[lane]);

        if (!dev->d2_thread_run[lane])
            break;

        sis6306_engine_execute_parallel_lane(dev, dev->d2_command,
                                             lane, dev->d2_active_threads,
                                             &dev->d2_changed_lo[lane],
                                             &dev->d2_changed_hi[lane]);
        thread_set_event(dev->d2_done_event[lane]);
    }

    thread_set_event(dev->d2_done_event[lane]);
}

static void
sis6306_d2_thread_1(void *priv)
{
    sis6306_d2_render_thread((sis6306_t *) priv, 1);
}

static void
sis6306_d2_thread_2(void *priv)
{
    sis6306_d2_render_thread((sis6306_t *) priv, 2);
}

static void
sis6306_d2_thread_3(void *priv)
{
    sis6306_d2_render_thread((sis6306_t *) priv, 3);
}

static void
sis6306_d2_execute(sis6306_t *dev, uint32_t command)
{
    const unsigned lanes = sis6306_2d_effective_threads(dev, command);

    if (lanes <= 1) {
        sis6306_engine_execute(dev, command);
        return;
    }
    dev->d2_command = command;
    dev->d2_active_threads = (uint8_t) lanes;

    for (unsigned lane = 1; lane < lanes; lane++) {
        dev->d2_changed_lo[lane] = 0xffffffffU;
        dev->d2_changed_hi[lane] = 0;
        thread_reset_event(dev->d2_done_event[lane]);
        thread_set_event(dev->d2_wake_event[lane]);
    }

    sis6306_engine_execute_parallel_lane(dev, command, 0, lanes,
                                         &dev->d2_changed_lo[0],
                                         &dev->d2_changed_hi[0]);

    for (unsigned lane = 1; lane < lanes; lane++)
        thread_wait_event(dev->d2_done_event[lane], -1);

    uint32_t changed_lo = dev->d2_changed_lo[0];
    uint32_t changed_hi = dev->d2_changed_hi[0];
    for (unsigned lane = 1; lane < lanes; lane++) {
        if (dev->d2_changed_lo[lane] < changed_lo)
            changed_lo = dev->d2_changed_lo[lane];
        if (dev->d2_changed_hi[lane] > changed_hi)
            changed_hi = dev->d2_changed_hi[lane];
    }

    sis6306_engine_mark_changed(dev, changed_lo, changed_hi);
}

static void
sis6306_accel_thread(void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;

    while (dev->accel_thread_run) {
        thread_set_event(dev->accel_not_full_event);
        thread_wait_event(dev->accel_wake_event, -1);
        thread_reset_event(dev->accel_wake_event);

        ATOMIC_STORE(dev->accel_busy, 1);

        while (ATOMIC_LOAD(dev->accel_read_idx) != ATOMIC_LOAD(dev->accel_write_idx)) {
            const uint32_t read_idx = ATOMIC_LOAD(dev->accel_read_idx);
            sis6306_accel_work_t *work =
                &dev->accel_queue[read_idx & SIS6306_ACCEL_QUEUE_MASK];

            if (work->type == SIS6306_ACCEL_WORK_2D) {
                sis6306_d3_sync(dev);
                memcpy(&dev->accel_regs[SIS6306_2D_REG_BASE], work->regs.reg2d,
                       SIS6306_2D_REG_SIZE);
                dev->accel_bpp = work->bpp;
                sis6306_d2_execute(dev, work->command);
            } else if (work->type == SIS6306_ACCEL_WORK_3D) {
                sis6306_d3_queue_triangle(dev, work);
            }

            ATOMIC_INC(dev->accel_read_idx);
            thread_set_event(dev->accel_not_full_event);
        }

        ATOMIC_STORE(dev->accel_busy, 0);
        thread_set_event(dev->accel_idle_event);
    }

    ATOMIC_STORE(dev->accel_busy, 0);
    thread_set_event(dev->accel_not_full_event);
    thread_set_event(dev->accel_idle_event);
}

static void
sis6306_accel_sync(sis6306_t *dev)
{
    if (!dev->accel_needs_sync)
        return;

    while (sis6306_accel_pending(dev)) {
        const int accel_pending =
            (ATOMIC_LOAD(dev->accel_write_idx) != ATOMIC_LOAD(dev->accel_read_idx)) ||
            ATOMIC_LOAD(dev->accel_busy);

        sis6306_accel_wake(dev);
        sis6306_d3_wake(dev);

        if (accel_pending) {
            thread_reset_event(dev->accel_idle_event);
            if ((ATOMIC_LOAD(dev->accel_write_idx) != ATOMIC_LOAD(dev->accel_read_idx)) ||
                ATOMIC_LOAD(dev->accel_busy))
                thread_wait_event(dev->accel_idle_event, 1);
        } else {
            for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
                if (sis6306_d3_pending_lane(dev, lane)) {
                    thread_reset_event(dev->d3_idle_event[lane]);
                    if (sis6306_d3_pending_lane(dev, lane))
                        thread_wait_event(dev->d3_idle_event[lane], 1);
                }
            }
        }
    }

    dev->accel_needs_sync = 0;
}

static void
sis6306_3d_after_writel(sis6306_t *dev, uint32_t off)
{
    const uint32_t primitive = sis6306_3d_live_reg32(dev, 0x89f8);
    const unsigned fire = (primitive >> 8) & 0x0f;
    int do_fire = 0;

    switch (fire) {
        case 0: do_fire = (off == 0x89fc); break; /* TFIRE */
        case 1: do_fire = (off == 0x8810); break; /* TSARGBa */
        case 2: do_fire = (off == 0x881c); break; /* TSWa */
        case 3: do_fire = (off == 0x8830); break; /* TSARGBb */
        case 4: do_fire = (off == 0x883c); break; /* TSWb */
        case 5: do_fire = (off == 0x8850); break; /* TSARGBc */
        case 6: do_fire = (off == 0x885c); break; /* TSWc */
        case 7: do_fire = (off == 0x8858); break; /* TSVc */
        default:
            break;
    }

    if (do_fire)
        sis6306_accel_queue_3d(dev);
}

static inline uint32_t
sis6306_3d_tstatus(sis6306_t *dev)
{
    uint32_t depth;
    uint32_t available;
    int idle;

    sis6306_r180_timing_retire(dev, sis6306_r181_effective_tsc(dev));
    depth = sis6306_r180_timing_depth(dev);
    available = (depth >= SIS6306_HW_QUEUE_UNITS) ? 0U :
        (SIS6306_HW_QUEUE_UNITS - depth);

    idle = (depth == 0U) && !sis6306_accel_pending(dev);

    return (available << 16) | (idle ? 0x00000003U : 0U);
}

static uint8_t
sis6306_mmio_readb_raw(uint32_t addr, sis6306_t *dev)
{
    uint32_t offset = addr & 0xffff;

    if ((offset >= 0x89fc) && (offset <= 0x89ff)) {
        const uint32_t status = sis6306_3d_tstatus(dev);
        return (status >> ((offset - 0x89fc) * 8)) & 0xff;
    }

    if ((offset >= 0x8240) && (offset <= 0x8243)) {
        const uint32_t status = 0xe0001fffU;
        return (status >> ((offset - 0x8240) * 8)) & 0xff;
    }

    return dev->mmio_regs[offset];
}

static uint8_t
sis6306_mmio_readb(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint8_t ret = sis6306_mmio_readb_raw(addr, dev);
    return ret;
}

static uint16_t
sis6306_mmio_readw(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint16_t ret = sis6306_mmio_readb_raw(addr, dev) |
                   ((uint16_t) sis6306_mmio_readb_raw(addr + 1, dev) << 8);
    return ret;
}

static uint32_t
sis6306_mmio_readl(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    uint32_t ret = sis6306_mmio_readb_raw(addr, dev) |
                   ((uint32_t) sis6306_mmio_readb_raw(addr + 1, dev) << 8) |
                   ((uint32_t) sis6306_mmio_readb_raw(addr + 2, dev) << 16) |
                   ((uint32_t) sis6306_mmio_readb_raw(addr + 3, dev) << 24);
    return ret;
}

static void
sis6306_update_cursor_mmio(sis6306_t *dev)
{
    uint32_t xraw, yraw;
    int x, y;

    if (!(dev->ext_seq[0x06] & 0x40) || !(dev->ext_seq[0x1b] & 0x80))
        return;

    xraw = (uint32_t) dev->mmio_regs[0x85f8] |
           ((uint32_t) dev->mmio_regs[0x85f9] << 8) |
           ((uint32_t) dev->mmio_regs[0x85fa] << 16) |
           ((uint32_t) dev->mmio_regs[0x85fb] << 24);
    yraw = (uint32_t) dev->mmio_regs[0x85fc] |
           ((uint32_t) dev->mmio_regs[0x85fd] << 8) |
           ((uint32_t) dev->mmio_regs[0x85fe] << 16) |
           ((uint32_t) dev->mmio_regs[0x85ff] << 24);

    x = (int) (xraw & 0x0fff);
    y = (int) (yraw & 0x0fff);

    dev->svga.hwcursor.x = x;
    dev->svga.hwcursor.y = dev->svga.interlace ? (y << 1) : y;
    dev->svga.fullchange = dev->svga.monitor->mon_changeframecount;

}

static void
sis6306_cursor_mmio_after_write(sis6306_t *dev, uint32_t addr)
{
    const uint32_t off = addr & 0xffff;

    if (((off >= 0x85f8) && (off <= 0x85ff)))
        sis6306_update_cursor_mmio(dev);
}

static void
sis6306_mmio_writeb_raw(uint32_t addr, uint8_t val, sis6306_t *dev)
{
    uint32_t offset = addr & 0xffff;

    if ((offset >= 0x8240) && (offset <= 0x8243))
        return;
    dev->mmio_regs[offset] = val;
}

static void
sis6306_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_mmio_writeb_raw(addr, val, dev);
    sis6306_cursor_mmio_after_write(dev, addr);
}

static void
sis6306_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_mmio_writeb_raw(addr, val, dev);
    sis6306_mmio_writeb_raw(addr + 1, val >> 8, dev);
    sis6306_cursor_mmio_after_write(dev, addr);
}

static void
sis6306_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_mmio_writeb_raw(addr, val, dev);
    sis6306_mmio_writeb_raw(addr + 1, val >> 8, dev);
    sis6306_mmio_writeb_raw(addr + 2, val >> 16, dev);
    sis6306_mmio_writeb_raw(addr + 3, val >> 24, dev);
    sis6306_cursor_mmio_after_write(dev, addr);

    if ((addr & 0xffff) == 0x823c)
        sis6306_accel_queue_2d(dev, val);

    sis6306_3d_after_writel(dev, addr & 0xffff);
}

static int
sis6306_range_overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{
    const uint64_t ae = (uint64_t) a + n;
    const uint64_t be = (uint64_t) b + m;
    return ((uint64_t) a < be) && ((uint64_t) b < ae);
}

static void
sis6306_cursor_track_lfb_write(sis6306_t *dev, uint32_t addr, unsigned width)
{
    const uint32_t mem_size = sis6306_configured_vram_size(dev);
    const uint32_t off = addr & dev->svga.vram_mask;
    const uint32_t tail = (mem_size >= 0x4000U) ? (mem_size - 0x4000U) : 0;

    if (!width || (mem_size < 0x4000U))
        return;

    if (sis6306_range_overlap(off, width, tail, 0x4000U)) {
        dev->cursor_pattern_valid = 1;
        dev->svga.fullchange = dev->svga.monitor->mon_changeframecount;
    }
}

static uint8_t
sis6306_lfb_readb(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_accel_sync(dev);
    return svga_readb_linear(addr, &dev->svga);
}

static uint16_t
sis6306_lfb_readw(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_accel_sync(dev);
    return svga_readw_linear(addr, &dev->svga);
}

static uint32_t
sis6306_lfb_readl(uint32_t addr, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_accel_sync(dev);
    return svga_readl_linear(addr, &dev->svga);
}

static void
sis6306_lfb_writeb(uint32_t addr, uint8_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_accel_sync(dev);
    svga_writeb_linear(addr, val, &dev->svga);
    sis6306_cursor_track_lfb_write(dev, addr, 1);

}

static void
sis6306_lfb_writew(uint32_t addr, uint16_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_accel_sync(dev);
    svga_writew_linear(addr, val, &dev->svga);
    sis6306_cursor_track_lfb_write(dev, addr, 2);

}

static void
sis6306_lfb_writel(uint32_t addr, uint32_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    sis6306_accel_sync(dev);
    svga_writel_linear(addr, val, &dev->svga);
    sis6306_cursor_track_lfb_write(dev, addr, 4);

}

static uint8_t
sis6306_pci_read(int func, int addr, int len, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    (void) func;
    (void) len;

    switch (addr) {
        case 0x00: return 0x39;
        case 0x01: return 0x10;
        case 0x02: return 0x06;
        case 0x03: return 0x63;

        /* Datasheet reset dword at 04h: 02200004h. */
        case 0x04: return (dev->pci_command & 0x03) | PCI_COMMAND_L_BM;
        case 0x05: return 0x00;
        case 0x06: return 0x30;
        case 0x07: return 0x02;

        case 0x08: return 0x2a;
        case 0x09: return 0x00;
        case 0x0a: return 0x00;
        case 0x0b: return 0x03;

        case 0x0c: return 0x00;
        case 0x0d: return 0x00;
        case 0x0e: return 0x00;
        case 0x0f: return 0x00;

        /* BAR0: observed 8 MB prefetchable framebuffer aperture. */
        case 0x10: return (dev->lfb_base & 0xff) | 0x08;
        case 0x11: return (dev->lfb_base >> 8) & 0xff;
        case 0x12: return (dev->lfb_base >> 16) & 0xff;
        case 0x13: return (dev->lfb_base >> 24) & 0xff;

        /* BAR1: 64 KB non-prefetchable MMIO. */
        case 0x14: return dev->mmio_base & 0xff;
        case 0x15: return (dev->mmio_base >> 8) & 0xff;
        case 0x16: return (dev->mmio_base >> 16) & 0xff;
        case 0x17: return (dev->mmio_base >> 24) & 0xff;

        /* BAR2: 128-byte relocatable VGA I/O aperture. */
        case 0x18: return (dev->io_base & 0xff) | 0x01;
        case 0x19: return (dev->io_base >> 8) & 0xff;
        case 0x1a: return (dev->io_base >> 16) & 0xff;
        case 0x1b: return (dev->io_base >> 24) & 0xff;

        case 0x2c: return 0x39;
        case 0x2d: return 0x10;
        case 0x2e: return 0x06;
        case 0x2f: return 0x63;

        case 0x30: return 0x00;
        case 0x31: return 0x80;
        case 0x32: return 0xff;
        case 0x33: return 0xff;

        /* PCI Power Management capability, followed by AGP. */
        case 0x34: return 0x40;

        case 0x3c: return dev->irq_line;
        case 0x3d: return 0x01;
        case 0x3e: return 0x00;
        case 0x3f: return 0x00;

        case 0x40: return 0x01;
        case 0x41: return 0x50;
        case 0x42: return 0x01;
        case 0x43: return 0x00;
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
            return 0x00;

        case 0x50: return 0x02;
        case 0x51: return 0x00;
        case 0x52: return 0x10;
        case 0x53: return 0x00;
        case 0x54: return 0x03;
        case 0x55: return 0x00;
        case 0x56: return 0x00;
        case 0x57: return 0x01;
        case 0x58:
        case 0x59:
        case 0x5a:
        case 0x5b:
            return dev->agp_cmd[addr & 3];

        default:
            return 0x00;
    }
}

static void
sis6306_pci_write(int func, int addr, int len, uint8_t val, void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    (void) func;
    (void) len;

    switch (addr) {
        case 0x04:
            dev->pci_command = (val & (PCI_COMMAND_IO | PCI_COMMAND_MEM)) |
                               PCI_COMMAND_L_BM;
            sis6306_update_mappings(dev);

            break;

        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13: {
            unsigned shift = (addr - 0x10) * 8;
            uint32_t raw = dev->lfb_base;
            raw = (raw & ~(0xffU << shift)) | ((uint32_t) val << shift);
            dev->lfb_base = raw & 0xff800000U;
            sis6306_update_mappings(dev);
            break;
        }

        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17: {
            unsigned shift = (addr - 0x14) * 8;
            uint32_t raw = dev->mmio_base;
            raw = (raw & ~(0xffU << shift)) | ((uint32_t) val << shift);
            dev->mmio_base = raw & 0xffff0000U;
            sis6306_update_mappings(dev);
            break;
        }

        case 0x18:
        case 0x19:
        case 0x1a:
        case 0x1b: {
            unsigned shift = (addr - 0x18) * 8;
            uint32_t raw = dev->io_base;
            raw = (raw & ~(0xffU << shift)) | ((uint32_t) val << shift);
            dev->io_base = raw & 0xffffff80U;
            sis6306_update_mappings(dev);
            break;
        }

        case 0x3c:
            dev->irq_line = val;
            break;

        case 0x58:
            dev->agp_cmd[0] = val & 0x03;   /* 1X / 2X enable */
            break;
        case 0x59:
            dev->agp_cmd[1] = val & 0x01;   /* AGP enable; sideband not supported */
            break;
        case 0x5b:
            dev->agp_cmd[3] = val;          /* request queue depth */
            break;

        default:
            break;
    }
}

static void *
sis6306_init(const device_t *info)
{
    sis6306_t *dev = (sis6306_t *) calloc(1, sizeof(sis6306_t));

    int render_threads = device_get_config_int("render_threads");

    if ((render_threads != 1) && (render_threads != 2) && (render_threads != 4))
        render_threads = 2;

    dev->d3_render_threads = (uint8_t) render_threads;
    dev->d2_render_threads = (uint8_t) render_threads;
    dev->d2_active_threads = 1;

    dev->r180_finish_tsc = (uint64_t *) calloc(
        SIS6306_TIMING_QUEUE_SIZE, sizeof(uint64_t));

    dev->d3_queue = (sis6306_3d_work_t *) calloc(
        SIS6306_ACCEL_QUEUE_SIZE, sizeof(sis6306_3d_work_t));
    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
        dev->d3_wake_event[lane] = thread_create_event();
        dev->d3_not_full_event[lane] = thread_create_event();
        dev->d3_idle_event[lane] = thread_create_event();
        dev->d3_thread_run[lane] = 1;
        thread_set_event(dev->d3_not_full_event[lane]);
        thread_set_event(dev->d3_idle_event[lane]);
    }
    dev->d3_thread[0] = thread_create_named(sis6306_d3_thread_0, dev,
                                             "SiS 6306 3D raster 0");
    if (dev->d3_render_threads > 1)
        dev->d3_thread[1] = thread_create_named(sis6306_d3_thread_1, dev,
                                                "SiS 6306 3D raster 1");
    if (dev->d3_render_threads > 2)
        dev->d3_thread[2] = thread_create_named(sis6306_d3_thread_2, dev,
                                                "SiS 6306 3D raster 2");
    if (dev->d3_render_threads > 3)
        dev->d3_thread[3] = thread_create_named(sis6306_d3_thread_3, dev,
                                                "SiS 6306 3D raster 3");

    for (unsigned lane = 1; lane < dev->d2_render_threads; lane++) {
        dev->d2_wake_event[lane] = thread_create_event();
        dev->d2_done_event[lane] = thread_create_event();
        dev->d2_thread_run[lane] = 1;
        thread_set_event(dev->d2_done_event[lane]);
    }
    if (dev->d2_render_threads > 1)
        dev->d2_thread[1] = thread_create_named(sis6306_d2_thread_1, dev,
                                                "SiS 6306 2D raster 1");
    if (dev->d2_render_threads > 2)
        dev->d2_thread[2] = thread_create_named(sis6306_d2_thread_2, dev,
                                                "SiS 6306 2D raster 2");
    if (dev->d2_render_threads > 3)
        dev->d2_thread[3] = thread_create_named(sis6306_d2_thread_3, dev,
                                                "SiS 6306 2D raster 3");

    dev->accel_queue = (sis6306_accel_work_t *) calloc(
        SIS6306_ACCEL_QUEUE_SIZE, sizeof(sis6306_accel_work_t));
    dev->accel_wake_event = thread_create_event();
    dev->accel_not_full_event = thread_create_event();
    dev->accel_idle_event = thread_create_event();
    dev->accel_thread_run = 1;
    dev->accel_bpp = 8;
    thread_set_event(dev->accel_not_full_event);
    thread_set_event(dev->accel_idle_event);
    dev->accel_thread = thread_create_named(sis6306_accel_thread, dev,
                                             "SiS 6306 accelerator");


    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_sis6306);


    svga_init(info, &dev->svga, dev, 8 << 20,
              sis6306_recalctimings,
              sis6306_in, sis6306_out,
              sis6306_hwcursor_draw, NULL);

    dev->svga.line_compare = sis6306_line_compare;

    /*
     * Replace only the legacy-memory callbacks.  Standard VGA modes still
     * delegate to the generic SVGA handlers; enhanced mode gets the SiS
     * packed A0000h bank window implemented above.
     */
    mem_mapping_set_handler(&dev->svga.mapping,
                            sis6306_legacy_readb,
                            sis6306_legacy_readw,
                            sis6306_legacy_readl,
                            sis6306_legacy_writeb,
                            sis6306_legacy_writew,
                            sis6306_legacy_writel);
    mem_mapping_set_p(&dev->svga.mapping, dev);

    dev->svga.bpp     = 8;
    dev->svga.miscout = 1;

    dev->pci_command = PCI_COMMAND_L_BM;

    dev->i2c = i2c_gpio_init("ddc_sis6306");
    if (dev->i2c) {
        dev->ddc = ddc_init(i2c_gpio_get_bus(dev->i2c));
        dev->ext_seq[0x11] = 0x03;
        i2c_gpio_set(dev->i2c, 1, 1);
    }

    mem_mapping_add(&dev->lfb_mapping, 0, 0,
                    sis6306_lfb_readb, sis6306_lfb_readw, sis6306_lfb_readl,
                    sis6306_lfb_writeb, sis6306_lfb_writew, sis6306_lfb_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    mem_mapping_add(&dev->mmio_mapping, 0, 0,
                    sis6306_mmio_readb, sis6306_mmio_readw, sis6306_mmio_readl,
                    sis6306_mmio_writeb, sis6306_mmio_writew, sis6306_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    mem_mapping_disable(&dev->svga.mapping);
    mem_mapping_disable(&dev->lfb_mapping);
    mem_mapping_disable(&dev->mmio_mapping);

    /* The SiS 530 exposes this integrated VGA behind its AGP bridge. */
    pci_add_card(PCI_ADD_AGP,
                 sis6306_pci_read, sis6306_pci_write,
                 dev, &dev->pci_slot);

    sis6306_update_mappings(dev);

    return dev;
}

static void
sis6306_close(void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;

    sis6306_accel_sync(dev);
    dev->accel_thread_run = 0;
    sis6306_accel_wake(dev);
    thread_wait(dev->accel_thread);

    for (unsigned lane = 1; lane < dev->d2_render_threads; lane++) {
        dev->d2_thread_run[lane] = 0;
        thread_set_event(dev->d2_wake_event[lane]);
    }
    for (unsigned lane = 1; lane < dev->d2_render_threads; lane++)
        thread_wait(dev->d2_thread[lane]);

    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++)
        dev->d3_thread_run[lane] = 0;
    sis6306_d3_wake(dev);
    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++)
        thread_wait(dev->d3_thread[lane]);


    io_removehandler(0x03a0, 0x0040,
                     sis6306_in, NULL, NULL,
                     sis6306_out, NULL, NULL,
                     dev);
    if (dev->io_active_base)
        io_removehandler(dev->io_active_base, 0x0080,
                         sis6306_iobar_read, NULL, NULL,
                         sis6306_iobar_write, NULL, NULL, dev);


    if (dev->ddc)
        ddc_close(dev->ddc);
    if (dev->i2c)
        i2c_gpio_close(dev->i2c);

    thread_destroy_event(dev->accel_idle_event);
    thread_destroy_event(dev->accel_not_full_event);
    thread_destroy_event(dev->accel_wake_event);
    free(dev->accel_queue);

    for (unsigned lane = 1; lane < dev->d2_render_threads; lane++) {
        thread_destroy_event(dev->d2_done_event[lane]);
        thread_destroy_event(dev->d2_wake_event[lane]);
    }

    for (unsigned lane = 0; lane < dev->d3_render_threads; lane++) {
        thread_destroy_event(dev->d3_idle_event[lane]);
        thread_destroy_event(dev->d3_not_full_event[lane]);
        thread_destroy_event(dev->d3_wake_event[lane]);
    }
    free(dev->d3_queue);
    free(dev->r180_finish_tsc);

    svga_close(&dev->svga);
    free(dev);
}

static void
sis6306_speed_changed(void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    svga_recalctimings(&dev->svga);
}

static void
sis6306_force_redraw(void *priv)
{
    sis6306_t *dev = (sis6306_t *) priv;
    dev->svga.fullchange = dev->svga.monitor->mon_changeframecount;
}

static const device_config_t sis6306_config[] = {
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = "4", .value = 4 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t sis6306_onboard_device = {
    .name          = "SiS 6306 (530/620) On-Board AGP",
    .internal_name = "sis6306_onboard",
    .flags         = DEVICE_AGP,
    .local         = 1,
    .init          = sis6306_init,
    .close         = sis6306_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = sis6306_speed_changed,
    .force_redraw  = sis6306_force_redraw,
    .config        = sis6306_config
};
