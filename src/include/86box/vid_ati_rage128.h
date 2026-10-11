/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- shared device state and prototypes.
 *
 *          The device state struct every block of the card works on, the
 *          inline helpers more than one block needs (card address
 *          routing, the tile transform, the command ring size), and the
 *          entry points each block's file exports to the others.
 *
 *          Terms used below: the "CPU thread" is 86Box's emulation
 *          thread, which runs the guest's register accesses; the "CCE
 *          thread" runs the command processor (vid_ati_rage128_pm4.c).
 *          3D drawing has three renderers, called "lanes": the C
 *          interpreter, the span JIT and the optional GPU backend.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Software Development Guide",
 *              SDK-G04000 Rev 0.01, August 1999. Cited as "SDK: ...".
 *
 *          [3] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Registers for CCE 3D Packets", 1999. Cited by its title
 *              and register; it has no page numbers.
 *
 *          [4] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Multimedia Registers", 1999. Cited the same way.
 *
 *          [5] ATI Technologies, "Rage 128 Register Reference Supplement:
 *              Bus Master Registers", 1999. Cited the same way.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#ifndef VIDEO_ATI_RAGE128_H
#define VIDEO_ATI_RAGE128_H

#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/vid_svga.h>

#ifdef ENABLE_RAGE128_LOG
extern int rage128_do_log;

void rage128_log(const char *fmt, ...);
#else
#    define rage128_log(fmt, ...)
#endif

/* Parameter block for composing the OV0 overlay on the optional GPU
   backend. The field order is the push-constant layout of the compute
   kernel in vid_ati_rage128_gpu_ov0.comp, so a change here must be made
   there too. The compose call is described at its declaration below. */
typedef struct r128_ov0_frame_t {
    uint32_t fmt, w, h, vram_mask;
    uint32_t h_step_y, h_step_c, h_rel0_y, h_rel0_c;
    uint32_t p1_x0, p1_xe, p2_x0, p2_xe, p3_x0, p3_xe;
    uint32_t v_acc0_y, v_acc0_c, v_inc;
    uint32_t src_h_y, src_h_c;
    uint32_t base0, pitch_y, base_u, pitch_u, base_v, pitch_v;
    uint32_t bright, sat_u, sat_v;
    uint32_t vkey_clr, vkey_msk;
} r128_ov0_frame_t;

/* One queued MPEG-2 motion-compensation launch: a copy of the MC control
   registers taken at the MC_START_CNTL write that launched it. Launches
   wait in a queue because the residual they add is complete only after
   the macroblock's sixth 8x8 block has been transformed. */
typedef struct rage128_mc_cmd_t {
    uint32_t x1, y1; /* SRC1 half-pel accumulators           */
    uint32_t adj1, idct_en, sec_tex;
    uint32_t x2, y2, adj2; /* SRC2, second reference               */
    uint32_t dx, dy, adjd; /* DST position within the surface      */
    uint32_t slot1;        /* reference slot indices               */
    int32_t  slot2;        /* -1 = no second reference             */
    uint32_t dst_addr, pred, w, h;
} rage128_mc_cmd_t;

/* LFB access probe counters (see rage128_t.lfbt). */
#define RAGE128_LFBT_BKTS 64
typedef struct rage128_lfbt_t {
    uint64_t total;
    uint64_t n[2][3]; /* [write][b, w, l] */
    uint64_t reg[4];  /* front, dst, z, other */
    uint64_t split;   /* column-straddling deliveries */
    uint64_t sampled;
    uint64_t xlate_ns, barrier_ns, store_ns;
    uint64_t tcd_ns, rng_ns; /* barrier: texcache dirty, range scan */
    uint64_t hits, quiesce_ns, flush_ns;
    uint32_t bkt_key[RAGE128_LFBT_BKTS], bkt_used, bkt_last, bkt_dropped;
    uint64_t bkt_cnt[RAGE128_LFBT_BKTS];
    int      sample;   /* this access is being clocked */
    int      in_split; /* byte handlers: part of a split */
} rage128_lfbt_t;

/* Monotonic nanosecond clock for every profiling timestamp (the GPU
   backend's prof_now is this function). Some probes stamp an access path
   that takes under 100 ns several times, so the clock must cost far less
   than the path it measures. On macOS it reads the raw uptime counter,
   mach_absolute_time, and scales it to nanoseconds itself with the
   timebase ratio numer/denom (125/3 for the 24 MHz counter of Apple
   silicon). clock_gettime_nsec_np on the raw uptime clock returns the
   same values, but it fetches the timebase and converts on every read;
   in a profiled Quake 3 run a third of the clock's samples were in that
   conversion. Measured in a tight loop on an M1 Pro: about 10 ns per
   read this way, 11 ns through clock_gettime_nsec_np and 21 ns through
   clock_gettime(CLOCK_MONOTONIC).
   The timebase is cached in one 64-bit word (numer in the high half,
   denom in the low half), so a thread reads either 0, and fetches the
   timebase itself, or both halves at once. Each file that includes this
   header has its own copy of the cache.
   The two mach functions are declared here instead of including
   <mach/mach_time.h>, because its type headers collide with 86Box macros
   that most device files define before this header (cpu.h `cycles`,
   plat.h `fallthrough`, thread.h `thread_t`). Both declarations match
   the macOS SDK's (kern_return_t is int; the struct tag is the SDK's
   own, left incomplete here, and holds numer then denom as two
   uint32_t), so a file that also includes the real header still
   compiles. */
#ifdef __APPLE__
struct mach_timebase_info;
extern uint64_t mach_absolute_time(void);
extern int      mach_timebase_info(struct mach_timebase_info *info);
#endif

static inline uint64_t
rage128_now_ns(void)
{
#ifdef __APPLE__
    static _Atomic uint64_t tb_pack;
    uint64_t                tb = atomic_load_explicit(&tb_pack, memory_order_relaxed);

    if (__builtin_expect(tb == 0, 0)) {
        uint32_t info[2] = { 0, 1 }; /* numer, denom */

        mach_timebase_info((struct mach_timebase_info *) (void *) info);
        tb = ((uint64_t) info[0] << 32) | info[1];
        atomic_store_explicit(&tb_pack, tb, memory_order_relaxed);
    }
    return mach_absolute_time() * (tb >> 32) / (uint32_t) tb;
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
#endif
}

typedef struct rage128_t {
    svga_t svga;
    rom_t  bios_rom;

    mem_mapping_t lfb_mapping;  /* BAR0 linear framebuffer        */
    mem_mapping_t agp_mapping;  /* BAR0 upper half: AGP image      */
    mem_mapping_t mmio_mapping; /* BAR2 register block             */

    uint8_t pci_regs[256];
    uint8_t pci_slot;

    uint32_t lfb_base;
    uint32_t mmio_base;
    uint32_t io_base;
    uint32_t io_mapped; /* I/O BAR window installed at this base, 0 if none */
    uint32_t mm_index;  /* MM_INDEX: register pointer for the indexed
                           MM_INDEX / MM_DATA access through I/O or MMIO */

    int vga_disabled; /* VGA_DISABLE board strap (CONFIG_XSTRAP bit 0, RRG:
                         CONFIG_XSTRAP, p. 3-11 / PDF 29): a board built as
                         a secondary card powers up with the whole legacy
                         VGA decode off */
    int has_bios_rom; /* a flash ROM sits behind the chip's ROM interface;
                         with none, the expansion ROM BAR (PCI config 0x30)
                         reads back as zero, which says "no ROM" */

    uint32_t bios_scratch[4]; /* BIOS_0_SCRATCH to BIOS_3_SCRATCH: scratch
                                 memory for the video BIOS (RRG:
                                 BIOS_0_SCRATCH, p. 3-85 / PDF 103) */

    /* Chip core configuration registers (vid_ati_rage128.c). Their
       defaults and read-only bits follow the guide, with the cites next
       to the defines in vid_ati_rage128_regs.h. */
    uint32_t bus_cntl;
    uint32_t bus_cntl1;
    uint32_t config_cntl;
    uint32_t gen_reset_cntl;
    uint32_t config_memsize;
    uint32_t test_debug_cntl;
    uint32_t test_debug_mux;
    uint32_t hw_debug;
    uint32_t host_path_cntl;
    uint32_t mem_cntl;
    uint32_t ext_mem_cntl;
    uint32_t mem_addr_config;
    uint32_t mem_intf_cntl;
    uint32_t mem_str_cntl;
    uint32_t mem_init_lat_timer;
    uint32_t mem_sdram_mode_reg;
    uint32_t pad_ctlr_strength;
    uint32_t pc_misc_ctl;
    uint32_t videomux_cntl;
    uint32_t surface_delay;
    /* The four SURFACE windows that translate CPU aperture addresses into
       tiled memory, 0x0B04-0x0B3C (RRG: SURFACE0_LOWER_BOUND, p. 3-225 /
       PDF 243). A window is on when its SURFn_PITCHSEL is nonzero (RRG:
       SURFACE0_INFO, p. 3-228 / PDF 246). The guide gives the fields but
       no description of the translation; the model routes an access
       inside an enabled window
       through r128_tile_off, so the CPU sees memory in the same layout
       the tiled 2D, 3D and scanout paths use. surf_xlate_on is set while
       any window is on, so the aperture handlers skip the window search
       with one test when none is. */
    uint32_t surf_lower[4];
    uint32_t surf_upper[4];
    uint32_t surf_info[4];
    int      surf_xlate_on;
    /* OV0 hardware video overlay (vid_ati_rage128_ov0.c). The block's 64
       dwords (0x0400-0x04FF) are stored twice: shadow holds the last
       write and is what reads return, active is what the compositor
       uses. Drivers update the overlay by setting OV0_LOCK in
       OV0_REG_LOAD_CNTL, writing the set and clearing the lock (SDK:
       Autonomous Update, p. 7-24 / PDF 194; xf86-video-r128 r128_video.c
       R128DisplayVideo422). While the lock is held, writes go to shadow
       only. A register reaches active in two parts: the bits outside its
       double-buffered fields move at the write, or at the unlock for a
       write made under the lock; the double-buffered fields move at the
       next vertical blank while the lock is clear and
       OV0_DOUBLE_BUFFER_REGS is set in OV0_SCALE_CNTL, and at the write
       or the unlock like the other bits while it is clear (Multimedia
       supplement: OV0_REG_LOAD_CNTL, OV0_SCALE_CNTL). pending has one
       bit per dword of the block for a register whose double-buffered
       fields the vertical blank commit still has to take; it is atomic
       because the display callback that commits and the register traffic
       that queues can run on different threads. */
    struct {
        uint32_t         shadow[64];
        uint32_t         active[64];
        _Atomic uint64_t pending;
        /* The scaler's internal copy of OV0_SOFT_EOF_TOGGLE (bit 6 of
           OV0_AUTO_FLIP_CNTRL, held at that bit position) and the field
           description it took when that copy last changed state:
           OV0_SOFT_BUF_NUM in bits [2:0] and OV0_SOFT_BUF_ODD in bit 4.
           The supplement says the buffer number and the odd flag are
           ignored until the internal, double-buffered toggle changes
           state, which happens at the start of a vertical blank with
           OV0_LOCK clear (Multimedia supplement: OV0_AUTO_FLIP_CNTRL),
           so the compositor reads the buffer number from flip_sel, not
           from the active register. Both are 0 at reset. */
        uint32_t         eof_toggle;
        uint32_t         flip_sel;
        /* Compose state for the optional GPU backend. gpu_frame is
           frame_count + 1 for the frame the last dispatch ran in (0 =
           none yet), gpu_ok and gpu_w / gpu_h its result and size for the
           per-line check, gpu_f the parameter block it composed with.
           The CPU compositor reads the active registers on every line,
           so a line whose parameter block differs from gpu_f (a register
           applied mid-frame, such as an OV0_AUTO_FLIP_CNTRL write during
           the scan) dispatches the compose again and the GPU output
           follows the same per-line values. */
        uint32_t         gpu_frame;
        uint32_t         gpu_w, gpu_h;
        int              gpu_ok;
        r128_ov0_frame_t gpu_f;
        /* VRAM range (masked) of the source buffers the last compose
           read. The re-dispatch test above compares registers only and
           cannot see new pixel data, so a CPU store inside this range
           clears gpu_frame and the next line composes again. */
        uint32_t gpu_src_lo, gpu_src_hi;
        /* Witness for OV0_VBLANK_DURING_LOCK, bit 1 of OV0_REG_LOAD_CNTL.
           The supplement updates that status only when OV0_LOCK goes from
           1 to 0, to 1 when a vertical blank happened while the lock was
           held (Multimedia supplement: OV0_REG_LOAD_CNTL). vblank_seq
           counts vertical blank starts, lock_vblank_seq holds the count
           at the lock's rising edge, and vblank_during_lock is the status
           latched at its falling edge, which reads return until the next
           falling edge. The counter is atomic because the display callback
           and the register traffic can run on different threads. It is a
           separate counter, not frame_count or the guest-clearable
           CRTC_VBLANK_SAVE flag, so neither a frame-count consumer nor a
           guest clearing CRTC_STATUS can disturb the witness. */
        atomic_uint vblank_seq;
        uint32_t    lock_vblank_seq;
        uint32_t    vblank_during_lock;
    } ov0;
    uint32_t ov0_reg_load_cntl; /* OV0_REG_LOAD_CNTL (0x0410), the overlay
                                   load lock; the read-only OV0_LOCK_READBACK
                                   (bit 3) and OV0_VBLANK_DURING_LOCK (bit 1)
                                   are never stored, reads synthesize them */
    /* DVD subpicture block (vid_ati_rage128_ov0.c). Registers keep what
       was written and read it back (only the new-frame strobe,
       RAGE128_SUBPIC_CNTL_NEW_FRAME, reads as 0): the DVD driver's
       subpicture parser
       read-modify-writes them and would lose the display enable
       otherwise. The palette RAM sits behind the index / data pair at
       0x057c / 0x0580. */
    struct {
        uint32_t regs[19]; /* 0x0540-0x0588 as written; reads return these */
        uint32_t pal[16];
        /* Copy of regs and pal the compositor draws from, refreshed once
           per scanout frame. The driver double-buffers the subpicture
           pixel data and switches buffers in the middle of a scan, so
           reading the live registers on every line tears the subtitle. */
        uint32_t active[19];
        uint32_t apal[16];
        uint32_t frame_stamp; /* frame_count of the last commit */
    } subpic;
    uint32_t agp_cntl_b;
    uint32_t agp_base; /* AGP_BASE (0x170): physical base of the AGP
                          aperture; an engine address in the upper 32 MB
                          of card space goes to the bus as AGP_BASE plus
                          its low 25 bits (RRG: DST_OFFSET, p. 3-137 /
                          PDF 155) */
    uint32_t agp_cntl; /* AGP_CNTL 0x174 */
    uint32_t gui_debug0;
    uint32_t pc_gui_mode;
    uint32_t bm_chunk_val[2];

    /* Display block: PLL register file (vid_ati_rage128_display.c). */
    uint32_t pll_regs[0x20];      /* PLL_ADDR is 5 bits; defined 0x01-0x13 */
    uint32_t clock_cntl_index;    /* PLL_ADDR / PLL_WR_EN / PPLL_DIV_SEL  */
    int      ppll_update_pending; /* PPLL_ATOMIC_UPDATE_R state           */
    uint32_t ppll_work[5];        /* working PPLL_REF_DIV + PPLL_DIV_0..3 the
                                     clock computation consumes; reloaded from
                                     pll_regs at the atomic-update boundary   */
    uint64_t pll_test_zero_tsc;   /* tsc when PLL_TEST_CNTL.TEST_COUNT was written */
    uint8_t  pll_test_count_base; /* TEST_COUNT value at that write       */
    double   pll_test_acc;        /* free-running ticks observed across register
                                     accesses (fractional: per-access credit
                                     scales with the test clock); see
                                     rage128_pll_test_count                 */

    /* Display block: CRTC / DAC / palette state. */
    uint32_t crtc_gen_cntl;
    uint32_t crtc_ext_cntl;
    /* VGA_BLINK_RATE pacing: fractional accumulator plus the synthesized
       core blink step (see rage128_vblank_start). */
    uint32_t vga_blink_acc;
    uint32_t vga_blink_step;
    uint32_t bank_w[2]; /* VGA aperture banking: VRAM offsets of the
                           A0000-A7FFF and A8000-AFFFF halves. With
                           paging on they are the MEM_VGA_WP_SEL /
                           MEM_VGA_RP_SEL page numbers times the page
                           size, 8 KB, or 32 KB when
                           CRTC_EXT_CNTL.VGA_ATI_LINEAR is set (the
                           VBIOS VBE window handler programs pages
                           for either size; RE:
                           Rage128progl_zerostate.VBI @c000:45a6);
                           with paging off, 0 and 0x8000. */
    uint32_t bank_r[2];
    uint32_t dac_cntl; /* bits 23:0 (bit 7, the read-only
                          DAC_CMP_OUTPUT, kept clear); the DAC_MASK
                          field [31:24] lives in dac_mask_prog */
    /* DAC_MASK as programmed. svga->dac_mask is the mask in effect, which
       is 0xff while the extended display path is on, because the guide
       gives the mask effect in VGA modes only (RRG: DAC_CNTL, p. 3-125 /
       PDF 143). Reads of 3C6 and DAC_CNTL need the programmed value, so it
       is kept here. */
    uint8_t  dac_mask_prog;
    uint32_t crtc_h_total_disp;
    uint32_t crtc_h_sync_strt_wid;
    uint32_t crtc_v_total_disp;
    uint32_t crtc_v_disp_active; /* CRTC_V_DISP [10:0] the extended raster
                                    runs on. Copied from CRTC_V_TOTAL_DISP
                                    when an MMIO write changes that field
                                    and when CRTC_EXT_DISP_EN turns on; a
                                    write to VGA CRTC[07] or [12] changes
                                    only the value CRTC_V_TOTAL_DISP reads
                                    back. */
    uint32_t    crtc_v_sync_strt_wid;
    uint32_t    crtc_vline;  /* CRTC_VLINE_CRNT_VLINE[10:0] compare  */
    pc_timer_t  vline_timer; /* fires at the compare line, re-armed per frame */
    uint32_t    crtc_gui_trig_vline;
    atomic_int  vline_in_window; /* CRTC_GUI_TRIG_VLINE[31] as published per scanline */
    atomic_uint vline_rise_seq;  /* window entries: WAIT_UNTIL EVENT_RE_CRTC_VLINE */
    atomic_uint vline_fall_seq;  /* window exits:   WAIT_UNTIL EVENT_FE_CRTC_VLINE */
    uint32_t    crtc_debug;
    uint32_t    crtc_offset;         /* [24:0] display base, bytes           */
    uint32_t    crtc_offset_latched; /* base the CRTC scans out (vblank latch) */
    uint32_t    scanout_seed_base;   /* byte base [24:0] the svga core's scan of
                                        this frame started from (CPU thread
                                        only). Each fetch is offset by
                                        crtc_offset_latched minus this value, so
                                        a flip taken mid-frame moves the lines
                                        still to come without any thread
                                        changing the core's live scan state.
                                        This is the CRTC_OFFSET_FLIP_CNTL = 1
                                        case, a new offset taken at a
                                        horizontal blank (RRG: CRTC_OFFSET_CNTL,
                                        p. 3-72 / PDF 90). */
    uint32_t scanout_snap_cur;       /* byte base, in the hidden part of VRAM, of
                                        the present snapshot the CRTC scans this
                                        frame; 0 = scan the guest's buffer. CPU
                                        thread only, chosen at each vsync and
                                        held until the next. */
    uint64_t scanout_snap_seq;       /* copy number of the snapshot on display:
                                        the full re-render is armed only when the
                                        (base, number) pair changes. */
    int scanout_hazard;              /* 1 = GPU writes overlap this frame's scan
                                        range (checked once at vsync), so each
                                        scanned line waits on its own fence; 0 =
                                        no fences this frame. CPU thread only; 1
                                        after reset until the first check. */
    int scanout_idle_frames;         /* consecutive clean hazard checks. A check
                                        can miss a submit that starts writing
                                        the scanned buffer later in the frame,
                                        so going fence-free takes 3 in a row. */
    uint32_t (*scan_remap_orig)(struct svga_t *svga, uint32_t in_addr);
    /* remap_func the svga core chose; the
       device's wrapper adds the fetch offset
       above on top of it. */
    int scanout_tiled;               /* this frame scans through the tile
                                        transform (CRTC_TILE_EN, set up once per
                                        frame at vsync). CPU thread only. */
    uint32_t scanout_tile_pitch;     /* surface pitch in bytes               */
    uint32_t scanout_tile_line;      /* start pixel's line within its tile
                                        (CRTC_TILE_LINE & 15)                */
    uint32_t scanout_tile_xin;       /* start pixel's byte within its 64-byte
                                        tile column (base & 63)              */
    uint32_t scanout_tile_c0;        /* byte address of the top-left of the
                                        start pixel's tile: the anchor every
                                        fetch offsets from via r128_tile_off */
    uint32_t crtc_offset_cntl;       /* tile control bits                    */
    uint32_t crtc_tile_line_latched; /* CRTC_TILE_LINE [4:0] as the CRTC
                                        shows it. CRTC_OFFSET_LOCK holds
                                        CRTC_OFFSET and CRTC_TILE_LINE
                                        together, so the line is taken
                                        with the offset at the flip point
                                        and the programmed value waits in
                                        crtc_offset_cntl (RRG:
                                        CRTC_OFFSET_CNTL, p. 3-71 / PDF 89) */
    uint32_t crtc_pitch;             /* [9:0] pixels / 8                     */
    uint32_t cur_offset;             /* hardware cursor image address        */
    uint32_t cur_horz_vert_posn;     /* cursor screen position               */
    uint32_t cur_horz_vert_off;      /* cursor edge offsets                  */
    uint32_t cur_offset_act;         /* copies the display draws from; they  */
    uint32_t cur_posn_act;           /* are refreshed from the programmed    */
    uint32_t cur_hvoff_act;          /* values while the lock is not held    */
    int      cur_lock;               /* CUR_LOCK: one flag, readable as bit
                                        31 of all three cursor registers; it
                                        holds CUR_OFFSET, CUR_HORZ_VERT_POSN
                                        and CUR_HORZ_VERT_OFF so a multi-write
                                        update shows at once (RRG:
                                        CUR_OFFSET, p. 3-81 / PDF 99) */
    uint32_t cur_clr0;               /* cursor color 0, 24bpp                */
    uint32_t cur_clr1;               /* cursor color 1, 24bpp                */
    uint32_t ovr_clr;
    uint32_t ovr_wid_left_right;
    uint32_t ovr_wid_top_bottom;
    uint32_t ovr_left; /* left and top border widths in pixels. */
    uint32_t ovr_top;  /* The svga core splits a border total
                          evenly between the two sides, while the
                          card programs each edge on its own, so
                          these are set again at each vsync.    */
    uint32_t dac_ext_cntl;
    /* Last stage, the analog DAC. While dac_const_on is set the composed
       output is replaced by dac_const_color (black for DAC_PDWN, the
       forced color for DAC_FORCE_DATA_EN), which is why the cursor and
       overlay are not drawn then. */
    int      dac_const_on;
    uint32_t dac_const_color;
    int      dac_blank_frame; /* extended-mode blank held all frame: the
                                 CRC's "not blank" excludes every pixel  */
    int      dac_crc_state;   /* 0 idle, 1 armed, 2 running (one field) */
    uint32_t dac_crc_sig;     /* DAC_CRC_SIG[23:0], latched at completion */
    uint32_t dda_config;
    uint32_t dda_on_off;
    uint32_t vga_dda_config;
    uint32_t vga_dda_on_off;
    uint32_t mem_vga_wp_sel;      /* VGA aperture write-bank pages        */
    uint32_t mem_vga_rp_sel;      /* VGA aperture read-bank pages         */
    uint32_t palette_index;       /* [7:0] write index, [23:16] read index */
    uint32_t frame_count;         /* CRTC_CRNT_FRAME[20:0]                */
    uint32_t snapshot_vh_counts;  /* SNAPSHOT_VH_COUNTS latched capture   */
    uint32_t snapshot_f_count;    /* SNAPSHOT_F_COUNT latched capture     */
    uint32_t n_vif_count;         /* N_VIF_COUNT [9:0] + [31] genlock sel */
    uint32_t snapshot_vif_cntl;   /* SNAPSHOT_VIF_COUNT control [25:24]   */
    int      crtc_offset_pending; /* CRTC_GUI_TRIG_OFFSET, bit 30: a new
                                     offset is written but not yet shown */
    int crtc_offset_hblank;       /* CRTC_OFFSET_FLIP_CNTL = 1: take the
                                     new offset at the next hblank        */
    int crtc_offset_lock;         /* CRTC_OFFSET_LOCK, bit 31; both bits
                                     read back through CRTC_OFFSET and
                                     CRTC_OFFSET_CNTL (RRG: CRTC_OFFSET,
                                     pp. 3-70-3-71 / PDF 88-89)          */
    int vblank_save;              /* CRTC_STATUS[1] sticky vblank         */

    /* Interrupt controller (GEN_INT_CNTL/STATUS 0x0040/0x0044). */
    uint32_t gen_int_cntl;   /* enable mask                          */
    uint32_t gen_int_status; /* latched events (write 1 to ack)      */
    uint8_t  irq_state;      /* PCI INTA assert shadow (pci_set_irq) */

    /* 2D GUI engine context (vid_ati_rage128_2d.c). Registers hold the
       packed spec encodings; the executors decode on use. */
    uint32_t dp_gui_master_cntl;
    uint32_t dp_brush_frgd_clr;
    uint32_t dp_brush_bkgd_clr;
    uint32_t brush_yx;             /* anchor: x [4:0] / y [12:8] added to the screen
                                      coordinate of the 8x8 pattern; [20:16] the 32x1
                                      line-pattern phase, written back under POLY_LINE */
    uint32_t brush_data[32];       /* BRUSH_DATA0..31: dwords 0-1 = 8x8 mono
                                      pattern (byte n = row n); the full file
                                      is the 32x32 mono brush (dword n =
                                      row n, MSB = leftmost pixel) */
    uint8_t brush_tile[8 * 8 * 4]; /* packet 8x1/1x8 brush expanded to the
                                      8x8 mono rows or color tile it repeats as */
    uint32_t dp_src_frgd_clr;
    uint32_t dp_src_bkgd_clr;
    uint32_t dp_cntl;
    uint32_t dp_datatype;
    uint32_t dp_mix;
    uint32_t dp_write_mask;
    uint32_t clr_cmp_clr_src;
    uint32_t clr_cmp_clr_dst;
    uint32_t clr_cmp_cntl;
    uint32_t clr_cmp_mask;
    uint32_t aux_sc_cntl;
    uint32_t aux_sc_rect[3][4]; /* AUX1-3 x {LEFT,RIGHT,TOP,BOTTOM}, 0x1664-0x1690 */
    uint32_t default_offset;
    uint32_t default_pitch;
    uint32_t default_sc_bottom_right;
    uint32_t sc_top_left;
    uint32_t sc_bottom_right;
    uint32_t src_offset; /* bytes */
    uint32_t src_pitch;  /* pixels */
    uint32_t dst_offset; /* bytes */
    uint32_t dst_pitch;  /* pixels */
    /* The plain SRC_PITCH / DST_PITCH registers as written (pitch [9:0]
       in units of 8 pixels, tile bit [16], and for DST also DST_PITCH_ADJ
       [18:17], RRG: DST_PITCH, p. 3-138 / PDF 156). A write to the packed
       SRC_PITCH_OFFSET / DST_PITCH_OFFSET forms updates them too, so both
       forms read back from one place. */
    uint32_t src_pitch_reg;
    uint32_t dst_pitch_reg;
    /* Source scissor (right and bottom only; there is no left/top pair). */
    uint32_t src_sc_right;
    uint32_t src_sc_bottom;
    /* Values for 2D operations started by a register write (the XFree86
       and Linux 2D path): the driver sets these, then a write to
       DST_WIDTH_HEIGHT, DST_HEIGHT_WIDTH or another register that carries
       the size starts the operation (rage128_2d_gui_op in
       vid_ati_rage128_2d.c). */
    int32_t  gui_dst_x;
    int32_t  gui_dst_y;
    int32_t  gui_src_x;
    int32_t  gui_src_y;
    uint32_t gui_dst_w; /* DST_WIDTH latch, 13:0 */
    uint32_t gui_dst_h; /* DST_HEIGHT latch, 13:0 */
    /* Bresenham line registers (the X server's 2D line path; see
       rage128_2d_bres_line). ERR, INC and DEC are 20-bit signed, LNTH is
       14 bits. */
    uint32_t bres_err;
    uint32_t bres_inc;
    uint32_t bres_dec;
    uint32_t bres_lnth;
    /* Color gradient for a 2D fill started with DP_GUI_MASTER_CNTL bit 27
       (GMC_3D_FCN_EN) set. The nine registers at 0x1a40-0x1a60 are not
       named in the guides; the Windows 2000 display driver's gradient
       fill writes them as one burst, per channel a slope along x, a
       slope along y and a start value, all 8.16 fixed point (RE:
       ati2dvaa.dll @00021f28). */
    uint32_t grad_start[3];   /* start R, G, B: 0x1a48 / 0x1a54 / 0x1a60 */
    int32_t  grad_slope_x[3]; /* signed per-pixel x slope: 0x1a40 / 0x1a4c / 0x1a58 */
    int32_t  grad_slope_y[3]; /* signed per-pixel y slope: 0x1a44 / 0x1a50 / 0x1a5c */
    int      grad_valid;      /* a burst was written since DP_GUI_MASTER_CNTL
                                 was last loaded with bit 27 clear */
    uint32_t gui_scratch[6];
    uint32_t scale_scr_height_width;
    uint32_t scl_palette[256]; /* 2D scaler palette, loaded by the
                                  LOAD_PALETTE packet (0x2c; SDK:
                                  LOAD_PALETTE, p. F-46 / PDF 336) as
                                  0x00RRGGBB entries. The Windows 98
                                  driver lowers each channel by 4/2/4
                                  first, so the truncating pack to the
                                  destination format rounds (RE:
                                  ati2drau.drv @0004:5373). An 8 bpp
                                  stretch source reads its colors here. */

    /* Host-data stream started by a register write, the path the XFree86
       XAA driver uses for color expansion. A register-triggered op whose
       DP_GUI_MASTER_CNTL source field DP_SRC_SOURCE is 3 or 4 (data
       through HOST_DATA; RRG: DP_GUI_MASTER_CNTL, p. 3-174 / PDF 192)
       records the destination rectangle and source datatype here. The
       raster dwords then arrive through HOST_DATA0-7 (0x17c0-0x17dc) and
       HOST_DATA_LAST (0x17e0), and the HOST_DATA_LAST write paints them
       (RRG: HOST_DATA_LAST, p. 3-153 / PDF 171). */
    int      hostdata_active;
    int      hostdata_x, hostdata_y, hostdata_w, hostdata_h;
    int      hostdata_srcdt;
    uint32_t hostdata_ndw;
    uint32_t hostdata_buf[1024];

    /* PM4/CCE command processor (vid_ati_rage128_pm4.c). */
    uint32_t pm4_buffer_offset;
    uint32_t pm4_buffer_cntl;
    uint32_t pm4_wm_cntl;
    uint32_t pm4_rptr_addr;
    uint32_t pm4_rptr;
    uint32_t pm4_wptr;
    uint32_t pm4_wptr_delay;
    uint32_t pm4_vc_debug_config;
    uint32_t pm4_microcode_addr;
    uint32_t pm4_micro_cntl;
    uint32_t pm4_iw_indoff;    /* indirect buffer bus address */
    uint32_t pm4_iw_indsize;   /* indirect buffer dwords; write fires the fetch */
    uint32_t pm4_fifo_data[2]; /* PM4_FIFO_DATA_EVEN/ODD latches (PIO ingress) */
    uint32_t pci_gart_page;
    int      pm4_ind_busy; /* an indirect buffer is being walked (walks do
                              not nest); it also keeps a CPU-thread INDSIZE
                              write off the shared ind_pl payload buffer */
    int pm4_ind_pending;   /* INDSIZE was written while a walk ran (by a
                              packet of that walk); the walk, when done,
                              starts on the newly written pair */

    /* Ring packet framing kept across pump calls, so the indirect-buffer
       submit recognizer below only ever matches a packet header. */
    uint32_t pump_frame_rem; /* payload dwords the open packet still owes */
    uint32_t pump_ib_state;  /* 1 = saw the header of a type-0 packet that
                                writes PM4_IW_INDOFF and PM4_IW_INDSIZE,
                                2 = saw its address dword */
    uint32_t pump_ib_addr;
    uint32_t pump_stall_live; /* submits whose body found no FIFO room
                                 behind a flip stall and is read live */

    /* CCE execution thread (vid_ati_rage128_pm4.c). The CPU thread copies
       ring dwords into this FIFO inside the device when the guest writes
       the ring's write pointer (the pump); the CCE thread parses and runs
       them, so a draw does not run inside the guest's MMIO write. */
    uint32_t   *cce_fifo;      /* RAGE128_CCE_FIFO_DWORDS dwords */
    atomic_uint cce_fifo_rd;   /* consumer index (CCE thread)    */
    atomic_uint cce_fifo_wr;   /* producer index (CPU thread)    */
    uint32_t   *cce_fifo_rptr; /* one per cce_fifo slot: the ring read pointer
                                  just past this dword (0xffffffff = a dword
                                  injected by an MMIO write, no ring advance).
                                  It turns executor progress back into a ring
                                  pointer, so the read pointer reported to the
                                  host counts dwords executed, not fetched.   */
    uint8_t *cce_fifo_tag;     /* one per cce_fifo slot: 0 = ring or injected
                                  dword, 1 = indirect-buffer body copied in by
                                  the pump, 2 = RAGE128_CCE_TAG_LIVE below.
                                  The tag moves with its dword, so an aborted
                                  packet cannot leave the indirect walker and
                                  the stream disagreeing about which dwords
                                  are body.                                  */
#define RAGE128_CCE_TAG_IB 1
/* Set on a submit packet's size dword when the body found no FIFO room
   behind a flip stall. The walker then reads the body from guest memory,
   and this tag marks that as expected rather than a lost copy. */
#define RAGE128_CCE_TAG_LIVE 2
/* Size of the three arrays above, the FIFO between the pump (CPU thread)
   and the executor (CCE thread). 2^19 dwords is 2 MB, twice the 1 MB ring
   of the SDK's example setup (SDK: Load the CCE Registers, p. 5-5 /
   PDF 101); the pump copies a larger ring through it in pieces. */
#define RAGE128_CCE_FIFO_DWORDS (1u << 19)
#define RAGE128_CCE_FIFO_MASK   (RAGE128_CCE_FIFO_DWORDS - 1)
    atomic_uint cce_retire_rptr;  /* ring read pointer the executor has finished
                                     executing up to                              */
    uint32_t cce_retire_pending;  /* read pointer of the dwords popped so far for
                                the current packet; owned by the CCE thread.
                                The CPU thread stores it only while the
                                executor is stopped (device reset, DL_RPTR
                                write, GUI reset). It is copied to
                                cce_retire_rptr only after the packet has run,
                                so a type-3 draw is not reported complete
                                before it has been drawn.                     */
    uint8_t  cce_pop_tag;         /* tag of the last ring dword popped; CCE thread */
    uint32_t cce_shadow_last;     /* last read pointer written to the host's
                                     read-pointer copy; CPU (submit) thread    */
    uint32_t  *cce_pl;            /* type-3 payload, ring executor  */
    uint32_t  *ind_pl;            /* type-3 payload, indirect parse */
    atomic_int cce_executing;     /* CCE thread is inside a packet */
    atomic_int cce_starved;       /* CCE thread is blocked waiting for more FIFO
                                     dwords: lets the drain wait tell a packet
                                     that can never complete from one still running
                                     (also used by the replay harness) */
    atomic_int cce_hung;          /* an indirect-buffer chain was found to never
                                     end: the engine reads busy and the executor
                                     stays parked until the next engine reset */
    atomic_int cce_batch_pending; /* parallel rasterizer: primitives queued but
                                 not yet drawn and joined. Keeps PM4_STAT busy
                                 in the gap after a packet ends (cce_executing
                                 is 0) and before the flush, so a status poll
                                 does not read idle while workers still owe
                                 pixels (render_threads > 1).               */
    atomic_int cce_drain_req;     /* CPU thread is waiting for idle */
    atomic_int cce_room_req;      /* CPU thread is waiting for FIFO room: ends a
                                     pacer hold early, never a flip stall */
    atomic_int cce_flip_stalled;  /* executor is inside a WAIT_UNTIL flip stall */
    atomic_int cce_abort;         /* dead bus or engine reset: drop the current
                                     packet */
    atomic_int cce_parked;        /* executor saw cce_abort and stopped. It then
                                     touches neither cce_fifo_rd nor the retire
                                     pointers, so the CPU thread's reset stores
                                     cannot race it. cce_executing does not tell
                                     that, since it also drops between packets. */
    atomic_int gui_idle_event;    /* engine went from busy to idle since the last
                                     check; turned into GEN_INT_STATUS bit 19 on
                                     the CPU thread only */
    int         cce_thread_run;
    thread_t   *cce_thread;
    const char *cap_tag;       /* multi-chip board: "-<tag>" appended to the
                                  RAGE128_CAPTURE path so the chips do not
                                  write one file; NULL on a single chip */
    int cap_pipe;              /* cap_file is a zstd process opened with popen
                                  (path ends in .zst): close it with pclose,
                                  not fclose */
    FILE *cap_file;            /* RAGE128_CAPTURE command-stream capture; NULL
                                  = off. Text records, one per line, in the
                                  order they happen (each fprintf call is
                                  atomic across threads):
                                    D <dword>          ring dword consumed
                                    J <dword>          MMIO-injected dword consumed
                                    I <addr> <val>     device bus read (indirect/vertex)
                                    B <addr> <len> <hex> bus data with payload: a
                                                       gathered block (texture level,
                                                       staged span, blit source) or a
                                                       run of contiguous dword reads
                                    W <addr> <len>     device bus write, address only
                                    R <off> <val> <mask> direct CPU register write
                                    X <vm> <len>       block bus read without
                                                       payload; this code does
                                                       not write it, the replay
                                                       harness still reads it  */
    atomic_int cap_recs;       /* records written, for the periodic flush */
    event_t   *cce_wake_event; /* work queued                   */
    event_t   *cce_idle_event; /* FIFO drained + engine idle    */
    event_t   *cce_flip_event; /* vsync consumed a pending flip */
    pc_timer_t cce_pump_timer; /* resumes ring->FIFO copy       */

    /* Realtime pacer. It keeps emulation speed at 100% by making the
       engine take longer: the guest sees a slower card, never a slower
       machine. At each vsync, once at least 250 ms of wall time have
       passed, the controller compares wall time with emulated time over
       that window and adjusts a hold applied at every flip
       (pace_hold_us). Slip past the band raises the hold by three times
       the excess (at most 25 ms a window); a window inside the band
       lowers it by three times the distance below the band edge, or by
       a small floor of hold / 32 + 200 us when that is larger; a window
       with no 3D rendering lowers it by 2 ms. The hold is clamped to
       0-100 ms, and the CCE thread sleeps it off at each flip. The frame
       rate therefore settles at the highest the host can carry at 100%
       speed, and with spare host time the hold falls to zero and costs
       nothing. Only windows with 3D rendering raise the hold, so slip
       while booting or drawing 2D does not slow the first frames of a
       session. The hold is never lifted to recover frame rate: if the
       CPU side alone runs slow, the hold sits at its maximum and the
       slip it cannot remove is what the window reports. */
    int      pace_enabled;    /* "realtime_pacing": 0 off, else on */
    int      pace_band_x;     /* slip band, us per wall ms (5 = 0.5%) */
    uint64_t pace_emu_us_tot; /* emulated us over all control windows */
    /* Time the CPU thread spends waiting for the engine to go idle inside
       a CRTC_OFFSET flip write, the per-frame present barrier for clients
       that flip without a WAIT_UNTIL (the Quake III driver does). Summed
       per pace window and cleared with it; CPU thread only. */
    uint64_t flip_stall_ns;
    /* Wall time the emulation thread spends per pace window outside guest
       execution in a 3D program. All CPU-thread-only and cleared by the
       pace window. The slices overlap and are kept cheap: mmr / mmw
       include whatever the register access sets off (the ring pump behind
       a WPTR write, an engine wait inside a read); pump_win is the
       timer-driven pump only; svga_win, the scanline render slice,
       includes the scanout read fences that rf= also reports.
       mmr / mmw are timed only with R128_MMIO_PROF (mmio_prof): a guest
       that spins on a status register reads MMIO millions of times a
       second, and two clock reads per access cost more than the handler
       they time. pump / svga / svgap are timed only with R128_SLICE_PROF
       (slice_prof): svga_poll runs about twice per scanline and the
       render wrapper once per line, and timing them all the time cost
       about 0.9% of the emulation thread in a release build running
       Quake III. All five are printed and nothing else reads them, so
       they stay 0 when their option is off. */
    int      mmio_prof;
    int      slice_prof;
    uint64_t pump_win_ns;
    uint64_t mmr_win_ns;
    uint64_t mmw_win_ns;
    uint64_t lfbw_win_ns; /* cpu-barrier store path: quiesce+flush */
    uint64_t svga_win_ns;
    uint64_t svgap_win_ns; /* whole svga_poll slice; svga_win is its
                              render sub-slice */
    void (*svga_render_orig)(svga_t *svga);
    /* Extended modes 320 pixels wide are shown with every pixel doubled,
       as a monitor stretches the low dot clock across the screen; this
       is the renderer the doubling wrapper calls. NULL when not doubled. */
    void (*render_hdbl_base)(svga_t *svga);
    /* Cache of "the palette is the identity ramp" for the fast scanline
       path. Every palette write path the device owns sets lut_dirty
       (PALETTE_DATA writes, VGA port 3C9, and the mode set as a catch-
       all), and the identity check runs again only then. */
    int lut_dirty;
    int lut_identity;
    /* Per-channel tables, each value already shifted into its place in
       a 32 bpp pixel, so a non-identity palette costs three loads and
       two ORs per pixel instead of the svga core's per-pixel call. */
    uint32_t lut_r[256];
    uint32_t lut_g[256];
    uint32_t lut_b[256];
    /* Frame conversion on a worker thread for frames scanned from a
       present snapshot. At vsync the snapshot choice wakes a worker that
       converts the snapshot, which does not change while shown, into
       conv_buf; the line renderer copies rows the worker has finished
       and converts the others itself, with the same result either way.
       A palette write sets conv_stale, which stops the worker and
       discards every row not yet used, so lines after the write use the
       new palette as they do in the svga core's own renderer. */
    thread_t *conv_thread;
    event_t  *conv_wake;
    int       conv_run;
    int       conv_active; /* CPU thread: this frame has a job */
    int       conv_verify; /* R128_CONV_VERIFY: compare worker rows
                              and the fast 16 bpp line with the svga
                              core's own conversion */
    uint8_t   *conv_buf;
    uint32_t   conv_buf_sz;
    uint32_t   conv_base;   /* masked VRAM byte addr of row 0 */
    uint32_t   conv_stride; /* source bytes per row */
    uint32_t   conv_count;  /* pixels per row */
    uint32_t   conv_lines;
    atomic_int conv_done; /* rows the worker has finished */
    atomic_int conv_stale;
    atomic_int conv_busy;
    uint64_t   conv_rows_win; /* rows served from conv_buf, per window */
    uint64_t   conv_mismatch;
    uint64_t   conv_transient;   /* verify: line changed under the compare */
    uint64_t   pace_wall_ms_tot; /* wall ms over all control windows */
    atomic_int pace_3d_seen;     /* Producers coalesce 3D activity; the
                                   governor exchanges it to consume a window. */
    atomic_int pace_hold_us;     /* standing engine hold per flip */
    /* Time rage128_pace_consume slept in the window, as wall time and as
       the 1 ms steps it asked for (the CCE thread adds, the vsync window
       prints them as slept= and clears them): a host timer coarser than
       1 ms shows as far more slept than asked. */
    _Atomic uint64_t pace_sleep_ns;
    _Atomic uint64_t pace_sleep_ask_ns;
    uint32_t         pace_wall_ms_last;
    uint64_t         pace_tsc_last;
    /* Frame timeline probe (R128_GPU_PROF only): a CRTC_OFFSET flip on
       the CCE thread opens a frame record, each step is timestamped where
       it happens, and the next flip closes the record. Register reads are
       counted per target register for the whole run (tel_reads) and again
       during the gap from a flip to the first draw (ftl_reads), which
       shows what the guest polls and whether it waits between frames or
       inside one. Profiling only: a race between threads can lose a
       count, never change behavior. */
    int                   ftl_en;
    uint64_t              ftl_flip_ns;  /* CCE: flip write executing */
    uint64_t              ftl_fence_ns; /* CCE: flip-tail fence wait, this flip */
    uint64_t              ftl_idle_ns;  /* CCE: starved of ring work since the flip */
    uint64_t              ftl_idle_t0;
    uint64_t              ftl_draw_ns; /* CCE: first 3D draw captured after the flip */
    int                   ftl_open;
    atomic_uint_least64_t ftl_latch_ns; /* CPU: flip latched into scanout */
    atomic_int            ftl_gap;
    uint32_t              ftl_reads[0x4000 >> 2]; /* one bin per dword register */
    uint32_t              tel_reads[0x4000 >> 2]; /* same bins, every read */
    /* Telemetry, same prof-only rules: the guest's engine-done polls
       (DL_RPTR, DL_WPTR, GUI_STAT reads; CPU thread) and the executor's
       busy time (CCE thread), both monotonic, sampled per interval. */
    uint64_t tel_polls;
    uint64_t cce_busy_ns;
    uint64_t cce_busy_t0;
    /* LFB access probe, prof-only, CPU thread, all monotonic (the
       interval printer diffs snapshots). Every aperture access is binned
       by direction x width, by target region (untranslated card offset
       against the scanout, DST and Z extents) and by 64 KB bucket; the
       handler slices (xlate / barrier / store) are clocked on one access
       in eight -- two clock reads per access would cost more than the
       path they measure. The barrier hit path is clocked on every hit. */
    rage128_lfbt_t lfbt;

    /* 3D engine register context (vid_ati_rage128_3d.c). Registers hold
       the values as written and are decoded where they are used. */
    struct rage128_t3d_ctx {
        uint32_t fpu_setup; /* PM4_VC_FPU_SETUP (0x71c): FRONT_DIR [0],
                               BACKFACE_CULLING_FN [2:1],
                               FRONTFACE_CULLING_FN [4:3] (Registers for
                               CCE 3D Packets, PM4_VC_FPU_SETUP). The
                               face function applies to every triangle of
                               every vertex walk, the inline ring walk of
                               3D_RNDR_GEN_PRIM included
                               (r3d_vc_face_draw): the triangle is culled,
                               drawn as points or lines, or drawn solid,
                               and bit 18 set draws every triangle solid.
                               The Windows 98 Direct3D driver culls on
                               the CPU and sets that bit, so both
                               windings reach the rasterizer. */
        uint32_t scale_3d_cntl;
        uint32_t scale_3d_datatype;
        uint32_t composite_shadow_id;
        uint32_t clr_cmp_clr_3d;
        uint32_t clr_cmp_msk_3d;
        uint32_t setup_cntl;
        uint32_t setup_cntl_pm4;
        uint32_t window_xy_offset;
        uint32_t z_offset;
        uint32_t z_pitch;
        uint32_t z_sten_cntl;
        uint32_t tex_cntl;
        uint32_t misc_3d_state_cntl;
        uint32_t tex_clr_cmp_clr;
        uint32_t tex_clr_cmp_msk;
        uint32_t fog_color;
        uint32_t prim_tex_cntl;
        uint32_t prim_tex_combine_cntl;
        uint32_t tex_size_pitch;
        uint32_t prim_tex_offset[11];
        uint32_t sec_tex_cntl;
        uint32_t sec_tex_combine_cntl;
        uint32_t sec_tex_offset[11];
        uint32_t constant_color;
        uint32_t prim_tex_border_color;
        uint32_t sec_tex_border_color;
        uint32_t sten_ref_mask;
        uint32_t plane_3d_mask;
        uint32_t tex_palette[256]; /* palette of 8-bit color-index textures
                                      (datatype 2), stored as ARGB8888 with
                                      alpha 0xff. It sits in t3d so each draw
                                      copies it with the rest of the texture
                                      state, and a raster worker never reads
                                      it while the next upload changes it. */
        uint8_t fog_table[256];    /* 8-bit fog factors (255 = no fog), used
                                      while FOG_TABLE_EN (bit 14 of
                                      MISC_3D_STATE_CNTL_REG; RRG:
                                      MISC_3D_STATE_CNTL_REG, p. 3-257 / PDF 275)
                                      is set. In t3d so each draw copies it. */
    } t3d;
    uint32_t fog_table_wr_index; /* FOG_TABLE_DATA write pointer, which
                                    advances on each write; only used while
                                    uploading, so draws do not copy it. */
    /* A 3D_RNDR_GEN_INDX_PRIM draw whose vertex count (the NUM field of
       VC_CNTL) needs more index dwords than its own packet carried. It
       waits here while NEXT_VERTEX_BUNDLE packets, which continue the
       previous indexed draw, supply the rest (SDK: 3D_RNDR_GEN_INDX_PRIM,
       p. F-57 / PDF 347; SDK: NEXT_VERTEX_BUNDLE, p. F-59 / PDF 349). idx
       has the layout of a 0x23 packet payload
       (four leading dwords, then the index pairs), so the draw code runs
       over it unchanged. Draws do not copy it: nothing is drawn until the
       last index has arrived. */
    struct rage128_vc_bundle {
        int       pending;
        uint32_t  base, fmt, stride, prim, num;
        uint32_t  have; /* index dwords collected so far */
        uint32_t *idx;  /* 4 + 32768 dwords: NUM reaches 65535 */
    } vc_bundle;
    /* Texture palette upload. The guide does not describe these
       registers; the offsets come from the Windows 98 Direct3D driver's
       palette uploads: RAGE128_TEX_PALETTE_INDEX (0x1968) sets the write
       pointer and each RAGE128_TEX_PALETTE_DATA (0x196c) write stores one
       entry and advances it. */
    uint32_t tex_pal_wr_index; /* palette write pointer; only used while
                                  uploading, so draws do not copy it */
    uint32_t tex_pal_gen;      /* incremented on each palette data write, so
                                  the GPU backend can tell two draws' palette
                                  copies are identical without comparing them
                                  (equal number = same contents) */

    void    *i2c;        /* GPIO_MONID bit-banged DDC I2C bus */
    void    *ddc;        /* DDC EDID EEPROM attached to that bus */
    uint32_t gpio_monid; /* MONID pad A/EN/MASK latch (Y reads live) */

    /* AMC connector pads (AMCGPIO_MASK/A/EN latches; Y reads live).
       amcgpio_in = pin levels driven from outside for pads this chip
       does not drive (0 = nothing attached); amcgpio_notify fires after
       each latch write so a board wired to the pads can see edges. */
    uint32_t amcgpio_mask;
    uint32_t amcgpio_a;
    uint32_t amcgpio_en;
    uint32_t amcgpio_in;
    void (*amcgpio_notify)(struct rage128_t *dev, void *priv);
    void *amcgpio_priv;

    uint16_t pci_device_id;
    uint16_t pci_subsys_vendor; /* ADAPTER_ID (PCI config 0x2c): set from the ROM
                                   straps at reset, then by any write to the
                                   ADAPTER_ID_W alias at 0x4c */
    uint16_t pci_subsys_id;
    uint32_t subsys_strap; /* vendor | id << 16 that a board supplies to a chip
                              with no flash of its own; 0 = read them from the
                              ROM, or with no ROM use 0x1002 and the device id */
    int is_agp;            /* board bus: DEVICE_AGP flag at init; selects slot
                              type, cap chain, ROM image, and ref oscillator */
    double   ref_freq_hz;  /* XTALIN: 27.0 MHz (AGP board) / 29.5 MHz (PCI) */
    int      vram_size;    /* bytes */
    uint32_t vram_mask;

    int   render_threads; /* parallel rasterizer worker count (1/2/4/8) */
    void *raster;         /* opaque tile/worker context (raster.c)      */
    /* Serializes rage128_raster_flush. Both the CCE executor and the CPU
       thread reach a flush, and no state either can read proves the other
       is not inside one: cce_executing is 0 from the wake signal until the
       executor stores it, so a drain can leave and flush while the woken
       executor is on its way to the same flush. Created before the
       executor thread exists. */
    mutex_t *raster_flush_mtx;
    int      jit_enabled;      /* "recompiler" config option (per-VM cfg)    */
    void    *jit;              /* opaque span-JIT context (vid_ati_rage128_jit.c) */
    void    *synctel;          /* GPU-offload flush-point telemetry, NULL = off
                                  (vid_ati_rage128_synctel.c); hot paths guard on
                                  this pointer before calling in */
    void *census;              /* 3D draw-state census, NULL = off
                                  (vid_ati_rage128_census.c); same guard */
    void *gpu;                 /* optional GPU raster backend, NULL = off (the
                                  default; vid_ati_rage128_gpu.c). The CPU
                                  interpreter and JIT stay the default renderer
                                  and the per-draw fallback; hot paths test
                                  this pointer before calling in */
    int gpu_defer;             /* GPU backend enabled but not built yet: the
                                  slow Vulkan setup waits for the guest's first
                                  access to an engine register, so it cannot
                                  race the host window's event setup at
                                  launch */
    int gpu_mode_cfg;          /* gpu_raster as read at device init; device
                                  config reads do not work once init has
                                  returned */
    int      gpu_telemetry_en; /* R128_GPU_TELEMETRY set at device init */
    uint8_t *vram_aligned;     /* svga.vram re-seated on the GPU backend's
                                  aligned allocator, which is not free()-
                                  compatible on every host; rage128_gpu_close
                                  owns the release, svga_close must not see it */

    /* Staging for texture levels the workers do not read straight from
       VRAM. The 3D texel sampler runs on the raster workers, and the AGP
       bus read path (dma_bm_read) is not thread-safe, so a level that
       lies in the AGP half of card space (r128_card_is_agp) is copied
       into this arena on the submit thread when the draw state is
       captured, and workers read the copy. A tiled level is copied here
       too, untiled, so every lane samples it as a linear image. The arena
       is reset only in rage128_raster_flush, after all workers are joined
       (in serial mode, per draw at capture), so its base pointer never
       moves while a batch is in flight. */
    struct rage128_tex_stage {
        uint8_t *arena; /* byte arena, allocated on first use         */
        uint32_t cap;   /* allocated bytes                            */
        uint32_t used;  /* bytes handed out; reset with the arena      */
        uint32_t gen;   /* batch number; incremented at each reset    */
        /* Cache generation for the GPU backend's reuse of staged levels
           across draws. A staged copy of a tiled level in local VRAM stays
           valid until tgen changes, which happens on any store into the
           source range below or when the arena is reset. Copies from AGP
           memory are never reused: the guest writes AGP memory without
           the device seeing it. */
        uint32_t tgen;
        /* Card-address range [src_lo, src_hi) covering every cached level's
           source (equal = none). Written under the lock below; atomic so
           the CPU store check can read them without the lock first. A
           stale value there gives an order the lock could also have
           produced (the store counted before the copy was published). */
        atomic_uint src_lo;
        atomic_uint src_hi;
        /* reset counters by site (per-draw capture / raster flush), the
           flushes r3d_stage_reserve forces when the arena is full (a
           subset of n_rec_flush), arena growths, and the highest fill */
        uint64_t n_rec_draw;
        uint64_t n_rec_flush;
        uint64_t n_rec_wrap;
        uint64_t n_grow;
        uint64_t n_hit;  /* cross-draw cache hits                      */
        uint64_t n_kill; /* cache invalidations (store hit aggregate)  */
        uint32_t peak_used;
        uint32_t ent_count;
        struct {
            uint32_t vm_base;   /* texture offset register value as
                                   written, mode bits included (key)      */
            uint32_t len;       /* byte length of the staged level (key)  */
            uint32_t tbpp;      /* 2 or 4 (key)                           */
            uint32_t tiled_pb;  /* untiling pitch, 0 = linear (key: two
                                   shapes of equal byte length lay the
                                   arena out differently)                 */
            uint32_t arena_off; /* byte offset into arena                 */
            uint32_t gen;       /* gen (per-batch entry) or tgen (cached
                                   entry) when it was staged              */
            uint32_t pd;        /* 1 = valid for this batch only (AGP, or
                                   no GPU backend), 0 = cached            */
        } ent[256];
        /* Orders tgen, src_lo, src_hi and n_kill between the CPU store
           check and the staging thread. Each locked section is a few
           loads and stores, so a spin lock costs less than a mutex. All
           other tex_stage fields are used by the submit thread only. */
        atomic_int lock;
    } tex_stage;

    /* Per-draw cache of decoded vertices for the indexed vertex walk over
       system memory: each distinct source index is read from the bus and
       decoded by VC_FORMAT once per draw instead of once per triangle
       that uses it. An entry is valid only if its tag equals the draw
       number `cur`, so nothing is cleared between draws. SUBMIT-THREAD
       ONLY (the same rule as tex_stage). */
    struct rage128_vtx_cache {
        struct r3d_vtx_t *v;   /* decoded vertex per source index      */
        uint32_t         *gen; /* generation tag per entry; 0 = never  */
        uint32_t          cap; /* entries allocated                    */
        uint32_t          cur; /* current draw generation; starts at 1 */
    } vtx_cache;

    /* Staging for a surface the engine reads and writes (one per kind of
       surface) that is either in AGP memory or tiled. Workers cannot use
       the bus, and svga.vram[addr & vram_mask] would fold an AGP card
       address onto local VRAM, so the surface bytes are read on the
       submit thread at capture, read and written by the draw in the
       arena, and written back at the batch barrier (rage128_raster_flush;
       in serial mode the arena is reused at capture). rb_guard_rt allows
       one such surface per batch. s2d_dst and s2d_src hold the short-lived
       per-operation windows of the 2D engine (r128_surf_map) and are never
       in use across a batch. */
    struct rage128_span_stage {
        uint8_t *arena;  /* allocated on first use; holds the bytes from vm  */
        uint32_t cap;    /* allocated bytes (grown in powers of two)         */
        uint32_t vm;     /* card address of the staged surface (bit 25 set
                            for AGP), or the linear-image card address of a
                            tiled window                                    */
        uint32_t len;    /* staged byte length                               */
        int      active; /* 1 = a staged surface waits for write-back        */
        /* Tiled window: the arena holds the untiled linear image of
           [vm, vm+len), and reads and write-backs go through the 64-byte
           by 16-line tile transform at (tbase, tpitch). Zero for linear
           staging. */
        uint32_t tiled;
        uint32_t tbase, tpitch;
        /* Copy of the staged bytes taken when they were read in. The
           write-back stores only bytes the engine changed from this copy,
           so a guest store into staged bytes the engine did not write is
           not overwritten. Real memory has no staging copy, so such a
           store simply stays; the model keeps the same result.
           shadow_ok 0 = no copy (allocation failed), and the whole range
           is written back. */
        uint8_t *shadow;
        uint32_t shadow_cap;
        int      shadow_ok;
    } z_stage;
    struct rage128_span_stage c_stage; /* 3D color/render target  */
    struct rage128_span_stage s2d_dst; /* 2D dst window scratch   */
    struct rage128_span_stage s2d_src; /* 2D src window scratch   */

    /* R128_GPU_2D=verify comparison for queued 2D operations whose
       result the CPU works out (keyed blit, stretch, line, gradient):
       the GPU result is saved, the destination's earlier contents put
       back, the operation's own CPU walk run again, and the two results
       compared byte for byte at the end of the operation. Only one
       operation is checked at a time (the 2D engine is synchronous). */
    struct rage128_2d_vq {
        uint8_t *pre; /* dst contents before the operation; the
                         GPU result follows at +rows*rowlen    */
        uint32_t addr, pitch, rowlen, rows;
        int      op;    /* qskip/qdone table index               */
        int      armed; /* GPU result saved, compare at the end  */
    } vq2d;

    /* MPEG-2 macroblock assist (vid_ati_rage128_mpeg.c). The four MC
       control registers persist between packets -- a packet is a list of
       writes to them -- so the decoded fields live here, not per packet. */
    struct {
        uint32_t tex_off[11]; /* 0x1840..0x1868: src1 reference slots 0-10 */
        uint32_t sec_off[11]; /* 0x1880..0x18a8: slot 11, then src2 0-5    */
        uint32_t tex_cntl;
        uint32_t scale_pitch;     /* 0x199c, bytes >> 3 */
        uint32_t sec_scale_pitch; /* 0x1980, bytes >> 3 */
        uint32_t src1_cntl, src2_cntl, dst_cntl, start_cntl;
        uint32_t x1, y1, adj1, idct_en, sec_tex;
        uint32_t x2, y2, adj2;
        uint32_t dx, dy, adjd;

        int     intra, alt_scan, rowperm, cperm; /* current packet mode */
        int     blk, pos, dirty;                 /* block / scan position */
        int16_t blk_data[6][64];                 /* coefficients, then pixels */
        int16_t resid[32][16];                   /* assembled residual rows */

        rage128_mc_cmd_t cmd[6];
        int              ncmd;

        /* M2IA authentication port (0x1f88 / 0x1f8c), which the guides
           do not describe (the model follows the ATI DVD driver's
           authentication routines; see the defines in
           vid_ati_rage128_regs.h), and the coefficient scramble a
           completed session turns on. m2ia_state is the port's own
           state, not the driver's; m2ia_key is the running scramble
           key, zero until a session completes. */
        int      m2ia_state;
        int      m2ia_reset_req;
        uint32_t m2ia_seq;
        uint32_t m2ia_w88, m2ia_w8c;
        uint32_t m2ia_key1, m2ia_resp1, m2ia_key2, m2ia_resp2;
        uint32_t m2ia_key;
    } mc;
} rage128_t;

/* Card address routing, shared by every engine that masters memory (2D
   source and destination, 3D color, Z and texture, CCE fetch). An engine
   offset is a byte address in one flat 64 MB card space split at 32 MB by
   bit 25: below is local VRAM, above is AGP system memory, reached
   through the active GART (rage128_pm4_bus_read_block and
   rage128_pm4_bus_write_block). The split comes from the chip: the
   read-only AGP_APER_OFFSET holds 0x2000000 with bits 24:0 hardwired to
   zero (RRG: AGP_APER_OFFSET, p. 3-187 / PDF 205), and DST_OFFSET says
   "the lower 32MB maps to frame buffer, the upper 32MB to AGP_BASE +
   DST_OFFSET(24:0)" (RRG: DST_OFFSET, p. 3-137 / PDF 155). Scanout and
   the hardware cursor are the exception only because their offset
   registers are 25 bits wide and cannot reach the AGP half. */
#define R128_CARD_AGP_HALF 0x02000000u

static inline int
r128_card_is_agp(uint32_t card_addr)
{
    return (card_addr & R128_CARD_AGP_HALF) != 0;
}

/* Column rule for compositing when a 320-pixel mode is shown doubled.
   Graphics, hardware cursor and overlay leave the chip as one pixel
   stream to the DAC (RRG: DAC_CNTL, p. 3-125 / PDF 143, DAC_CRC_EN), and
   the cursor position is given in those same screen pixels (RRG:
   CUR_HORZ_VERT_POSN, pp. 3-81-3-82 / PDF 99-100). So the cursor and the
   overlay stretch with the graphics, and scan pixel x covers output
   columns x << shift to (x << shift) + (1 << shift) - 1. */
static inline int
rage128_hdbl_shift(const rage128_t *dev)
{
    return dev->render_hdbl_base != NULL;
}

static inline void
rage128_hdbl_put(uint32_t *line, int shift, int x, uint32_t rgb)
{
    for (int k = 0; k < (1 << shift); k++)
        line[(x << shift) + k] = rgb;
}

static inline void
rage128_hdbl_xor(uint32_t *line, int shift, int x, uint32_t mask)
{
    for (int k = 0; k < (1 << shift); k++)
        line[(x << shift) + k] ^= mask;
}

/* The tile transform, shared by every user of tiled memory (3D color, Z
   and texture, the 2D DST_TILE / SRC_TILE surfaces, tiled scanout). A
   tile is 64 bytes by 16 lines, 1 KB, stored row by row: byte (xb, y) of
   the linear image, at pitch_b bytes per row, lands at
   16 * pitch_b * (y DIV 16) + 1K * (xb DIV 64) + 64 * (y MOD 16) +
   (xb MOD 64) from the surface base. This is the guide's CRTC_OFFSET
   formula (RRG: CRTC_OFFSET, p. 3-70 / PDF 88); the guide prints its
   third term as 64 * (start line DIV 16), but its own example on the same
   page (line 3 starts at 0xC0) shows MOD is meant. The guide requires
   the pitch to be a multiple of 64 bytes (RRG: CRTC_PITCH, p. 3-74 /
   PDF 92) and the packed offsets are 32-byte aligned; the transform
   itself needs neither. It is split into an x part (offset within one
   tile row) and a y part (the row's base: tile row plus line within the
   tile) for loops that compute the y part once per row. */
static inline uint32_t
r128_tile_x(uint32_t xb)
{
    return ((xb >> 6) << 10) + (xb & 63u);
}

static inline uint32_t
r128_tile_y(uint32_t y, uint32_t pitch_b)
{
    return 16u * pitch_b * (y >> 4) + ((y & 15u) << 6);
}

static inline uint32_t
r128_tile_off(uint32_t xb, uint32_t y, uint32_t pitch_b)
{
    return r128_tile_y(y, pitch_b) + r128_tile_x(xb);
}

/* Tile-row bounds. The rows of a tiled surface are spread across whole
   tile rows (16 lines each), so the bytes the transform can produce for
   rows [y0, y1] (inclusive) are the tile rows that contain them: from the
   start of y0's tile row to the end of y1's. The 64-bit forms cannot
   overflow on a large pitch times row count; the 32-bit forms keep the
   low 32 bits, which is the value their callers expect. */
static inline uint64_t
r128_tile_row_start64(uint32_t y0, uint64_t pitch_b)
{
    return 16ull * pitch_b * (y0 >> 4);
}

static inline uint64_t
r128_tile_rows_bytes64(uint32_t y1, uint64_t pitch_b)
{
    return 16ull * pitch_b * ((y1 >> 4) + 1u);
}

static inline uint32_t
r128_tile_row_start(uint32_t y0, uint32_t pitch_b)
{
    return (uint32_t) r128_tile_row_start64(y0, pitch_b);
}

static inline uint32_t
r128_tile_rows_bytes(uint32_t y1, uint32_t pitch_b)
{
    return (uint32_t) r128_tile_rows_bytes64(y1, pitch_b);
}

/* Byte range [lo, hi) of rows [y0, y1] (inclusive) of a linear or tiled
   surface at base, in 64 bits. The rasterizer and the GPU backend use it
   for their overlap checks and clamp or truncate it to their own address
   width. */
static inline void
r128_rows_bytes64(uint32_t base, uint32_t stride, int tiled,
                  uint32_t y0, uint32_t y1, uint64_t *lo, uint64_t *hi)
{
    if (tiled) {
        *lo = (uint64_t) base + r128_tile_row_start64(y0, stride);
        *hi = (uint64_t) base + r128_tile_rows_bytes64(y1, stride);
    } else {
        *lo = (uint64_t) base + (uint64_t) y0 * stride;
        *hi = (uint64_t) base + ((uint64_t) y1 + 1u) * stride;
    }
}

/* Row stride of a surface whose tile bit is honored: whole 64-byte tile
   columns, with any remainder below one column dropped. The Windows 98
   Direct3D driver (version 4.12) tiles its Z surface with pitch field
   129 (2064 bytes at 16 bpp) over memory laid out at 2048 bytes per row
   and clears it through the 2D engine, so both engines must use 2048.
   That driver case was observed; dropping the remainder in general
   follows the guide's rule that a tiled pitch is a multiple of the
   64-byte tile width (RRG: CRTC_PITCH, p. 3-74 / PDF 92), and no test on
   a real chip backs it. A pitch under one column is never tiled
   (r128_tiled_ok) and keeps its exact stride, as does any untiled
   surface. */
static inline uint32_t
r128_tile_stride(uint32_t pitch_b, int tile_bit)
{
    uint32_t tb = pitch_b & ~63u;

    return (tile_bit && tb) ? tb : pitch_b;
}

/* A surface's tile bit is honored only for a pitch the transform is
   defined for, a nonzero multiple of 64 bytes (the guide requires that of
   a tiled pitch; with any other pitch the transform maps some bytes of
   different rows to the same place). Any other
   surface stays linear. Callers pass the stride the surface is addressed
   at, r128_tile_stride for a tiled one. The 2D and 3D code both use this
   test so the two engines decide alike. */
static inline int
r128_tiled_ok(uint32_t tile_bit, uint32_t pitch_b)
{
    return tile_bit && pitch_b && !(pitch_b & 63u);
}

/* YCbCr (video range, BT.601) to RGB888, the one integer matrix used by
   both the OV0 scaler and the YUV texture datatypes; callers OR in their
   own alpha byte. The guides do not document either converter's matrix,
   or whether the texture unit's matches the overlay's, so both use this
   standard one rather than two guesses. */
static inline uint32_t
r128_yuv_to_rgb(int y, int cb, int cr)
{
    int c = y - 16;
    int d = cb - 128;
    int e = cr - 128;
    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;

    if (r < 0)
        r = 0;
    else if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    else if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    else if (b > 255)
        b = 255;
    return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}

/* vid_ati_rage128_3d.c: byte size of one mip level */
extern uint32_t r3d_level_bytes(uint32_t dt, uint32_t s3tc, uint32_t lw,
                                uint32_t lh);

/* A resolved surface window for the synchronous (2D) engines. Local
   VRAM: the plain svga.vram[a & vram_mask] access (rel 0, mask
   vram_mask). AGP or tiled: a staged copy in an arena; mask is a
   power-of-two bound so a stray index cannot leave the arena. */
typedef struct r128_surf_t {
    uint8_t                   *base; /* backing bytes                        */
    uint32_t                   rel;  /* card address of base[0]              */
    uint32_t                   mask; /* wrap bound applied to (addr - rel)   */
    struct rage128_span_stage *st;   /* staged backing store, NULL = local   */
} r128_surf_t;

static inline uint8_t *
r128_surf_at(const r128_surf_t *s, uint32_t card_addr)
{
    return &s->base[(card_addr - s->rel) & s->mask];
}

/* A contiguous run of `len` bytes starting at card_addr, or NULL when the
   run would wrap the backing bound (local VRAM near vram_mask, or a staged
   arena edge). Fast-path 2D executors use it to bulk-copy/fill a row only
   when every byte maps monotonically; otherwise they fall to the per-byte
   walk that honors the wrap. */
static inline uint8_t *
r128_surf_run(const r128_surf_t *s, uint32_t card_addr, uint32_t len)
{
    uint32_t idx = (card_addr - s->rel) & s->mask;

    if ((uint64_t) idx + len > (uint64_t) s->mask + 1u)
        return NULL;
    return &s->base[idx];
}

/* Unified resolver primitives (vid_ati_rage128_mem.c). All are
   submit/CPU-thread only (they reach the bus helpers). */
extern int  r128_span_stage_acquire(rage128_t *dev, struct rage128_span_stage *st,
                                    uint32_t vm, uint32_t extent);
extern int  r128_span_stage_acquire_tiled(rage128_t                 *dev,
                                          struct rage128_span_stage *st,
                                          uint32_t tbase, uint32_t tpitch,
                                          uint32_t extent);
extern void r128_span_stage_writeback(rage128_t *dev, struct rage128_span_stage *st);
extern int  r128_surf_map(rage128_t *dev, r128_surf_t *s, struct rage128_span_stage *st,
                          uint32_t lo, uint32_t len);
extern int  r128_surf_map_tiled(rage128_t *dev, r128_surf_t *s,
                                struct rage128_span_stage *st, uint32_t tbase,
                                uint32_t tpitch, uint32_t lo, uint32_t len);
extern int  r128_card_copy_tiled(rage128_t *dev, uint32_t tbase, uint32_t tpitch,
                                 uint32_t lin, uint8_t *buf, uint32_t len, int dir);
extern void r128_surf_commit(rage128_t *dev, r128_surf_t *s);
extern void r128_surf_release(r128_surf_t *s);

/* "This mip slot is VRAM-resident, sample svga.vram & vram_mask". */
#define R128_TEX_STAGE_NONE 0xffffffffu

/* One decoded vertex: the FTLVERTEX fields that VC_FORMAT can select
   (SDK: FTLVERTEX, p. F-51 / PDF 341), with absent fields given their
   defaults. Shared by the 3D assembler (vid_ati_rage128_3d.c) and the
   parallel rasterizer (vid_ati_rage128_raster.c). */
typedef struct r3d_vtx_t {
    float    x, y, z, rhw;
    uint32_t diffuse; /* ARGB */
    uint32_t spec;    /* fog factor [31:24], specular R, G, B [23:0] */
    float    s, t;
    float    s2, t2;
    float    rhw2; /* second texcoord-set reciprocal-w (VC_FORMAT RHW2 0x200) */
} r3d_vtx_t;

/* PRIM_TEXTURE_COMBINE_CNTL_C / SEC_TEX_COMBINE_CNTL_C fields, decoded once
   per draw for r3d_tex_combine (the bit layout is given where they are
   decoded). */
typedef struct r3d_comb_desc_t {
    uint32_t comb, fmsb, cfac, ifac;
    uint32_t comba, afac, ifaca;
} r3d_comb_desc_t;

/* Immutable per-stage sampler constants; the mutable mip-slot cache stays
   in the per-worker triangle descriptor. */
typedef struct r3d_stage_hdr_t {
    uint32_t tsp;              /* TEX_SIZE_PITCH_C halfword for this stage */
    uint32_t clamp_s, clamp_t; /* named around the cpu.h cs/ds macros */
    uint32_t dt, s3tc;
    uint32_t aone; /* Alpha-bearing texture with TEX_MAP_AEN
                      (SCALE_3D_CNTL bit 30) clear: the
                      texel's alpha reads as 0xff
                      (Registers for CCE 3D Packets,
                      SCALE_3D_CNTL) */
    uint32_t border;
    uint32_t minb, mag;
    int      mipdis, top;
} r3d_stage_hdr_t;

/* Flat per-draw state: every decoded register field the rasterizer uses,
   computed once per draw; it is also the key the span JIT compiles a
   block for. It must depend only on the captured registers (no pointers,
   no device state, padding zeroed with memset), so copying it and
   comparing it with memcmp to drop duplicate states stays valid. What
   each field means, and where that comes from, is given where it is
   computed (r3d_draw_state_derive in vid_ati_rage128_3d.c). */
typedef struct rage128_draw_state_t {
    /* destination */
    int      draw_ok; /* dst datatype has a decoder; 0 = reject */
    uint32_t dst_dt;
    int      bpp;
    uint32_t wmask; /* PLANE_3D_MASK_C raw bit-plane mask */
    int      dither;
    int      stip_en; /* brush type 9, the 32x32 mono pattern
                         with clear bits left alone (RRG:
                         DP_DATATYPE, p. 3-169 / PDF 187): a
                         polygon stipple that drops fragments
                         on clear bits; interpreter only     */
    int c_tiled;      /* dst tile bit: tiled addressing of the
                         color target, when it was not staged
                         untiled; the JIT and GPU lanes reach
                         it through the tile-column row walk
                         (rage128_3d_jit_row)                */
    int aux_on;
    /* aux scissors, enabled rectangles only (disabled ones stay zero so
       equal states compare equal): enable and mode bits as in
       AUX_SC_CNTL; coordinates sign-extended from 14 bits, inclusive */
    uint32_t aux_cntl;
    int32_t  aux_x0[3], aux_x1[3], aux_y0[3], aux_y1[3];
    int      sx0, sy0, sx1, sy1; /* scissor, inclusive */
    /* subpixel format / window offset */
    int   sub;
    float subf;
    int   rnd; /* FPU_ROUND_EN, PM4_VC_FPU_SETUP bit 15:
                  round, not truncate, float to int */
    int32_t slim;
    int32_t woxi, woyi; /* WINDOW_XY_OFFSET, subunits */
    /* depth + stencil */
    int      z_en, z_wr;
    uint32_t zfn;
    int      zbpp;
    uint32_t zmax;
    int      zshift;
    uint32_t zrowpx;
    int      z_tiled; /* Z_PITCH_C bit 16: tiled Z addressing,
                         when Z was not staged untiled; same
                         row walk as c_tiled                */
    int      sten_on;
    uint32_t sfn, sfail_op, zpass_op, zfail_op;
    uint32_t sref, svmask, swmask;
    int      sshift;
    /* shade */
    int solid_on; /* PM4_COLOR_FCN 0: every vertex takes
                     CONSTANT_COLOR_C (cc) */
    int flat_on;  /* replicate flat_src's vertex color */
    int flat_src; /* provoking vertex index 0/1/2 */
    /* texture stages */
    int tex_en, sec_en;
    int tex_tiled; /* some level an enabled stage can sample
                      has a nonzero tile mode (bits 31:30 of
                      its texture offset register) and was
                      not staged untiled; interpreter only */
    int   premult, do_persp;
    int   sec_persp_diff; /* the secondary stage's perspective enable
                             (SEC_TEX_CNTL_C SEC_TEX_PERSPECTIVE_DIS,
                             bit 14, clear) differs from do_persp; 0,
                             both stages alike, is how every driver
                             seen programs them and what a zeroed
                             state means. 0 when the stage is off  */
    int   sel_w;          /* SEC_SRC_SEL_W (SEC_TEX_CNTL_C bit 15): the
                             secondary stage's W is the vertex rhw2,
                             not rhw; 0 when the stage is off       */
    int   need_lod, need_lod2;
    float lod_bias;
    float texw0, texh0, texw1, texh1;
    int   sec_sel;                    /* SEC_SRC_SEL_ST (SEC_TEX_CNTL_C bit 0):
                                         stage 1 samples the second ST set */
    int             wrap_s0, wrap_t0; /* cylindrical wrap of ST set 1 / set 2 */
    int             wrap_s1, wrap_t1;
    r3d_stage_hdr_t sh[2];
    r3d_comb_desc_t comb[2];
    /* chroma keys, permuted into converted-texel space */
    int      need_ck, ck3d_on, ckc_on;
    uint32_t ckfn;
    uint32_t ck3d_clr, ck3d_msk, ckc_clr, ckc_msk;
    /* combine constant color, decoded ARGB */
    float cc[4];
    /* specular / fog */
    int   spec_en;
    int   fog_en, fog_table_en;
    float fogr, fogg, fogb;
    /* alpha test */
    int      atest_en;
    uint32_t atest_fn, atest_ref;
    /* alpha blend */
    int      alpha_en;
    uint32_t bsrc, bdst, bfcn;
    /* Placed at the end on purpose: compiled helper-call blocks embed
       field offsets, so a field added in the middle would change every
       dump of their call shapes. */
    uint8_t soa_selftex; /* a stage's sample range overlaps the
                            destination or Z rows inside the
                            scissor. The vector (SoA) loop reads
                            all texels before it stores, so such
                            draws use the scalar block, which
                            keeps the interpreter's pixel order */
    /* Texture lighting (TEX_CNTL_C TEX_LIGHT_FN [17:14], ALPHA_LIGHT_FN
       [20:18]; Registers for CCE 3D Packets, TEX_CNTL_C), plus bit 6,
       which the supplement lists as reserved and the model reads as the
       function's high bit. It is a third combine after the texture
       stages: its "texel" is the stage output and its input is the
       iterated vertex color. lcomb is its combine descriptor, and
       light_on turns the pass on in every lane. */
    r3d_comb_desc_t lcomb;
    uint8_t         light_on;
} rage128_draw_state_t;

/* Per-draw copy of the rasterizer state. A worker draws a triangle with
   the engine state as it was when the triangle was submitted, not the
   engine's current state, because the state changes between draws within
   a batch. This holds exactly the register state the rasterizer reads
   (the whole 3D context plus the 2D datapath fields it uses). VRAM and
   textures are not copied; a draw that reads what an earlier draw of the
   same batch wrote (render to texture) is handled by flushing the batch
   first, not by copying the surface. */
#define R3D_TEX_RNG_MAX 11 /* one piece per mip slot at most */

typedef struct rage128_raster_state_t {
    struct rage128_t3d_ctx t3d;
    uint32_t               dst_offset;
    uint32_t               dst_pitch;
    uint32_t               dst_tiled; /* dst tile bit at capture (bit 31 of
                                         the packed form, bit 16 of the
                                         plain DST_PITCH; both are kept in
                                         dst_pitch_reg bit 16) */
    uint32_t dp_datatype;
    uint32_t sc_top_left;
    uint32_t sc_bottom_right;
    uint32_t aux_sc_cntl; /* aux scissors at submit */
    uint32_t aux_sc_rect[3][4];
    /* 32x32 polygon stipple rows, copied at capture when the brush type
       selects the stipple (all zero otherwise, so equal states still
       compare equal). Row = dst y & 31, bit 31 = leftmost pixel. */
    uint32_t stipple[32];
    /* Computed at capture for the parallel rasterizer's render-to-texture
       check: dst bytes per pixel, and the VRAM byte range each enabled
       texture stage samples (tex_lo == tex_hi marks a disabled stage). */
    uint32_t dst_bpp;
    uint32_t tex_lo[2];
    uint32_t tex_hi[2];
    /* The same bytes as separate pieces (adjacent mip levels merged,
       sorted by lo). A driver may put one level of a chain past another
       surface, and a single overall range would then mark that whole
       surface as sampled. Unused entries stay zero so equal states
       compare equal. */
    uint32_t tex_rng_lo[2][R3D_TEX_RNG_MAX];
    uint32_t tex_rng_hi[2][R3D_TEX_RNG_MAX];
    uint32_t tex_nrng[2];
    /* Byte offset of each mip slot in the texture staging arena
       (R128_TEX_STAGE_NONE = the slot is read from svga.vram &
       vram_mask). Plain integers, so the copy and memcmp in
       rb_intern_state stay valid; workers add them to
       dev->tex_stage.arena, which does not move during a batch. */
    uint32_t prim_stage_off[11];
    uint32_t sec_stage_off[11];
    /* 1 = this draw's Z buffer is staged in dev->z_stage.arena (index
       zaddr - t3d.z_offset) because it is in AGP memory, which the
       rasterizer cannot read directly; 0 = Z is read and written in
       svga.vram & vram_mask. Only AGP residence stages a Z or color
       surface (r3d_plan_zbuffer, r3d_plan_cbuffer); a tiled surface in
       local memory is addressed in place through the tile transform.
       A plain int for the same reason as the stage offsets above. */
    int z_staged;
    /* Same flag for the color target, staged in dev->c_stage.arena
       (index daddr - dst_offset). */
    int c_staged;
    /* 1 = that staged AGP surface is tiled and its copy is detiled: the
       arena holds the linear image, so every lane addresses it
       linearly. 0 for a linear surface and for any tiled surface that
       is not staged; a tiled surface then goes through the tile
       transform (c_tiled / z_tiled). */
    int z_detiled;
    int c_detiled;
    /* An AGP color, Z or texture staging failed (dead bus or out of
       memory). A failed AGP read cannot turn into an access to local
       VRAM, so the submit functions drop the draw instead of using the
       svga.vram & vram_mask path. */
    int stage_dead;
    /* Flat per-draw decode of the fields above (r3d_draw_state_derive). */
    rage128_draw_state_t d;
} rage128_raster_state_t;

/* Does texture stage st sample any byte of [lo, hi)? Tests the pieces,
   not the union: the gap between two scattered levels is never fetched. */
static inline int
r3d_tex_rng_hit(const rage128_raster_state_t *rs, int st, uint32_t lo, uint32_t hi)
{
    if (hi <= lo)
        return 0;
    for (uint32_t i = 0; i < rs->tex_nrng[st]; i++)
        if (rs->tex_rng_lo[st][i] < hi && rs->tex_rng_hi[st][i] > lo)
            return 1;
    return 0;
}

/* Sampler state for one stage, resolved per triangle; every field is
   constant across the triangle's pixels. Slots are filled in on first
   use, so a small triangle does not pay for levels it never samples
   (JIT blocks that sample inline fill them all first; see
   r128_jit_tri_t.texctx). It lives on the stack of rage128_3d_tri, so
   each worker has its own; the staging arena base it holds stays valid
   until the flush, which joins the workers. */
typedef struct r3d_stage_desc_t {
    uint32_t tsp;
    /* named to avoid the cpu.h cs/ds macros, like r3d_stage_hdr_t */
    uint32_t        clamp_s, clamp_t, dt, s3tc;
    uint32_t        amask; /* ORed into fetched texels: 0xff000000 when aone */
    uint32_t        border;
    uint32_t        minb, mag;
    int             mipdis, top;
    const uint32_t *pal;
    uint16_t        slot_valid; /* bit per resolved slot below */
    /* Bit per slot: the level's offset bits [31:30] select a tile mode,
       the transform is defined for its format and pitch, and the level
       was not staged untiled, so it is sampled from VRAM through the
       transform. Kept out of the slot struct because the JIT emitters
       build sizeof(r3d_slot_desc_t) into their code as the slot[] stride
       in several places (texture and SoA headers, both architectures);
       draws with tiled texture levels are not compiled anyway. */
    uint16_t slot_tiled;
    struct r3d_slot_desc_t {
        uint32_t       lw, lh;
        const uint8_t *texbase;
        uint32_t       base, mask;
    } slot[11];
} r3d_stage_desc_t;

/* Per-triangle texture-stage context: everything the per-pixel texture
   block consumes that is not in rs->d. Built once per triangle in
   rage128_3d_tri (always; the fields are cheap). sd0 and sd1 are the
   sampler descriptors that fill their slots on first use, so the struct
   is changing stack state private to one worker. Compiled spans get the
   same pointer through r128_jit_tri_t.texctx: blocks that call helpers
   pass it back to rage128_texstage_run, and blocks that sample inline
   read its fields (and the slots filled beforehand) directly by
   offsetof. */
typedef struct r3d_texctx_t {
    rage128_t                    *dev;
    const rage128_raster_state_t *rs;
    float                         sta, stb, stc, tta, ttb, ttc; /* first ST set, multiplied by rhw
                                                                   when the draw does it (premult) */
    float s2a, s2b, s2c, t2a, t2b, t2c;                         /* second ST set (S2_T2)          */
    float arhw, brhw, crhw;                                     /* vertex rhw                     */
    float dSdx, dSdy, dTdx, dTdy;                               /* first-set screen gradients     */
    float dWdx, dWdy;
    float dS2dx, dS2dy, dT2dx, dT2dy; /* second-set gradients, used with
                                         SEC_SRC_SEL_ST                 */
    r3d_stage_desc_t sd0, sd1;
    /* The secondary stage's W per vertex (rhw2 under SEC_SRC_SEL_W,
       else rhw) and its screen gradients. They sit after the sampler
       descriptors so that every field offset the compiled spans bake
       in for the other members stays where it is. */
    float a2rhw, b2rhw, c2rhw;
    float dW2dx, dW2dy;
} r3d_texctx_t;

/* Defined in vid_ati_rage128_3d.c (uses the texture-addressing helpers).
   Non-const dev: capture writes the staging arena, always on the
   submit thread. */
extern void rage128_raster_state_capture(rage128_t *dev, rage128_raster_state_t *rs);
/* Staging helpers (vid_ati_rage128_pm4.c / vid_ati_rage128_raster.c). */
extern int rage128_pm4_bus_read_block(rage128_t *dev, uint32_t vm, uint8_t *dst, uint32_t len);
extern int rage128_raster_nthreads(rage128_t *dev);
/* Device->guest GART writes -- write-back twin of the staging reads
   (vid_ati_rage128_pm4.c). SUBMIT-THREAD ONLY. */
extern int rage128_pm4_bus_write_block(rage128_t *dev, uint32_t vm, const uint8_t *src, uint32_t len);

/* ---- Span JIT (vid_ati_rage128_jit.c; per-architecture backends in
   vid_ati_rage128_codegen_<arch>.h, ARM64 and x86-64) ----
   A compiled block rasterizes whole scanlines for one
   rage128_draw_state_t: every value that follows from the state is built
   into the code as an immediate, so only per-triangle values pass
   through this struct. Winding is normalized: for a clockwise triangle
   the C setup negates the edge seeds, edge steps and invs (exactly: an
   int64 negate, and a float sign flip does not change how e * invs
   rounds), so compiled code always keeps a pixel when the sign bits of
   e0, e1 and e2 are all clear. */
typedef struct r128_jit_tri_t {
    uint8_t *vram; /* dev->svga.vram                              */
    uint8_t *zptr; /* staged Z arena base, NULL = Z in local VRAM */
    uint8_t *cptr; /* staged color arena base, NULL = local VRAM  */
    uint32_t vram_mask;
    uint32_t z_base, z_lim;
    uint32_t c_base, c_lim;
    int32_t  x0, x1;                 /* inclusive column walk range                 */
    int64_t  e0dxi, e1dxi, e2dxi;    /* winding-normalized edge x-steps      */
    float    invs;                   /* winding-normalized barycentric scale        */
    double   dZdx;                   /* depth plane x-gradient (double DDA)         */
    float    vca[4], vcb[4], vcc[4]; /* vertex RGBA, flat fold applied    */
    /* Per-triangle texture-stage context, an r3d_texctx_t (defined above)
       held as a plain pointer: blocks that call helpers pass it back to
       rage128_texstage_run for each textured pixel, and blocks that sample
       inline read its fields by offset. */
    void          *texctx;
    float          fog[4];                 /* vertex fog factors fga/fgb/fgc, [3] unused  */
    float          spa[4], spb[4], spc[4]; /* vertex specular RGB, [3] unused   */
    const uint8_t *fog_table;              /* -> rs->t3d.fog_table[256], table-fog only */
    /* 1 for each edge whose row seed carries the fill-rule bias, else 0.
       The bias decides coverage only: after winding normalization a
       biased edge value is one less than the true one, so compiled code
       adds these back before it converts an edge value to a weight. */
    int64_t e0b, e1b, e2b;
} r128_jit_tri_t;

/* The interpreter's per-pixel texture-stage block (both stages, chroma
   keys, combine), extracted so compiled spans call the interpreter's own
   code -- bit-exact by construction. Takes the r3d_texctx_t built per
   triangle, the barycentric weights, and col (in: iterated vertex color,
   out: combined color). Returns 0 when a chroma key discards the pixel.
   (vid_ati_rage128_3d.c) */
extern int rage128_texstage_run(void *tc, float w0, float w1, float w2, float *col);

/* Rasterize one owned row. e0/e1/e2 are the winding-normalized row seeds,
   zline the row's depth DDA seed, drow/zrow the row base VRAM addresses.
   Returns (rx1 << 32) | rx0 -- the written-pixel column range for dirty
   marking; rx0 == 0xffffffff means the row wrote nothing. */
typedef uint64_t (*r128_jit_span_fn)(const r128_jit_tri_t *tri,
                                     int64_t e0, int64_t e1, int64_t e2,
                                     double zline, uint32_t drow, uint32_t zrow,
                                     int32_t py);

/* Which span-JIT emitter this build has, if any. Shared with
   vid_ati_rage128.c so the "recompiler" config option is offered on
   exactly the hosts that can act on it. */
#if defined(__aarch64__) || defined(_M_ARM64)
#    define R128_JIT_HAVE_BACKEND 1
#    define R128_JIT_BACKEND_A64  1
#elif defined(__x86_64__) || defined(_M_X64)
#    define R128_JIT_HAVE_BACKEND 1
#    define R128_JIT_BACKEND_A64  0
#else
#    define R128_JIT_HAVE_BACKEND 0
#endif

extern void rage128_jit_init(rage128_t *dev);
extern void rage128_jit_close(rage128_t *dev);
/* Find or compile the block for ds. SUBMIT-THREAD ONLY (single-threaded
   cache by construction: workers only ever execute already-published
   blocks). NULL = state not JITable (or JIT off) -> interpreter. */
extern r128_jit_span_fn rage128_jit_get_block(rage128_t *dev, const rage128_draw_state_t *dstate);
/* 1 = a block compiled for ds samples its textures INLINE (no
   rage128_texstage_run call), so the caller must pre-resolve every mip
   slot the block can touch before running it. Pure function of ds --
   exactly the predicate the generator used at compile time. */
extern int rage128_jit_tex_inline(const rage128_draw_state_t *dstate);
extern int rage128_jit_verify_mode(rage128_t *dev);
/* Span-shape statistics (R128_JIT_PROF=1): row and pixel counts added
   once per triangle by the raster loop and printed at close, to size
   what compiling whole triangles instead of rows would gain. */
extern void rage128_jit_stat_rows(rage128_t *dev, int count_tri,
                                  uint64_t rows, uint64_t px, uint64_t empty);
/* Verify harness (R128_JIT=verify): pre saves the row, runs the JIT,
   snapshots its writes and restores the row (returns 0 if the row was
   skipped); the interpreter then rasterizes as usual and post compares
   its memory against the JIT snapshot, logging any divergence. Tiled
   rows (c_tld/z_tld) save/compare through the same 64Bx16 transform the
   render path uses and drive the JIT through rage128_3d_jit_row. */
extern int  rage128_jit_verify_pre(rage128_t *dev, r128_jit_tri_t *tri,
                                   const rage128_draw_state_t *dstate, r128_jit_span_fn fn,
                                   int64_t e0, int64_t e1, int64_t e2, double zline,
                                   uint32_t drow, uint32_t zrow, int32_t py,
                                   int c_tld, int z_tld);
extern void rage128_jit_verify_post(rage128_t *dev, const r128_jit_tri_t *tri,
                                    const rage128_draw_state_t *dstate,
                                    int32_t py, int32_t rx0, int32_t rx1);
/* Read-only verify counters (harness assertions; zeros when not in
   verify mode). tiled counts rows verified through the tile transform --
   the coverage proof that tiled rows are compared, not skipped. */
extern void rage128_jit_verify_stats(rage128_t *dev, uint64_t *rows,
                                     uint64_t *tiled, uint64_t *skipped,
                                     uint64_t *mismatch_rows);

/* Legacy VGA passthrough (vid_ati_rage128_vga.c). */
extern uint8_t rage128_vga_in(uint16_t addr, void *priv);
extern void    rage128_vga_out(uint16_t addr, uint8_t val, void *priv);

/* Display block: PLL / CRTC / DAC / palette / scanout
   (vid_ati_rage128_display.c). */
extern int      rage128_display_reg_read(rage128_t *dev, uint32_t off, uint32_t *val);
extern int      rage128_display_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask);
extern void     rage128_display_reset(rage128_t *dev);
extern void     rage128_recalctimings(svga_t *svga);
extern void     rage128_dac_mask_apply(rage128_t *dev);
extern void     rage128_vblank_start(svga_t *svga);
extern void     rage128_hwcursor_draw(svga_t *svga, int displine);
extern uint32_t rage128_conv_16to32(svga_t *svga, uint16_t color, uint8_t bpp);
extern void     rage128_update_banking(rage128_t *dev);
extern void     rage128_updatemapping(rage128_t *dev);
extern void     rage128_vsync_callback(svga_t *svga);
extern void     rage128_crtc_offset_hblank_tick(rage128_t *dev);
extern void     rage128_gui_trig_vline_tick(rage128_t *dev);
extern void     rage128_conv_close(rage128_t *dev);
extern void     rage128_vline_timer(void *priv);
extern void     rage128_gen_int_update(rage128_t *dev);
extern void     rage128_gen_int_vblank(rage128_t *dev);
extern void     rage128_gen_int_vsync(rage128_t *dev);

/* OV0 hardware video overlay window and front-end scaler
   (vid_ati_rage128_ov0.c); its entry points follow the MPEG-2 ones. */
/* vid_ati_rage128_mpeg.c: MPEG-2 macroblock assist (PACKET3 op 0x31). */
extern int  rage128_mpeg_reg_read(rage128_t *dev, uint32_t off, uint32_t *val);
extern int  rage128_mpeg_reg_write(rage128_t *dev, uint32_t off, uint32_t val,
                                   uint32_t mask);
extern int  rage128_mpeg_packet3(rage128_t *dev, uint32_t hdr, const uint32_t *pl,
                                 uint32_t count);
extern void rage128_mpeg_reset(rage128_t *dev);

extern int  rage128_ov0_reg_read(rage128_t *dev, uint32_t off, uint32_t *val);
extern int  rage128_ov0_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask);
extern void rage128_ov0_reset(rage128_t *dev);
extern void rage128_ov0_update(rage128_t *dev);
extern void rage128_ov0_vblank(rage128_t *dev);
extern int  rage128_subpic_reg_read(rage128_t *dev, uint32_t off, uint32_t *val);
extern int  rage128_subpic_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask);
extern void rage128_overlay_draw(svga_t *svga, int displine);

/* One chip of a board (vid_ati_rage128.c). A single-chip board is the
   device itself; a multi-chip board (vid_ati_rage128_maxx.c) builds each
   chip with its own straps and forwards the device hooks to both. */
typedef struct rage128_chip_cfg_t {
    int      vga_disabled; /* VGA_DISABLE strap: no legacy VGA decode */
    uint32_t subsys_strap; /* subsystem vendor | id << 16 the board supplies to a
                              chip with no flash; 0 = the chip reads its own ROM */
    const char *rom_path;  /* expansion ROM image; NULL = no flash on this
                              chip, so it advertises no expansion ROM */
    const char *cap_tag;   /* suffix for this chip's RAGE128_CAPTURE file */
} rage128_chip_cfg_t;
extern rage128_t            *rage128_chip_init(const device_t *info, const rage128_chip_cfg_t *cfg);
extern void                  rage128_chip_close(rage128_t *dev);
extern void                  rage128_chip_reset(rage128_t *dev);
extern void                  rage128_chip_speed_changed(rage128_t *dev);
extern void                  rage128_chip_force_redraw(rage128_t *dev);
extern const device_config_t rage128_config[];

/* Central register file (vid_ati_rage128.c) -- entry for the PM4 packet
   type-0 path so packets reach every block's registers. */
extern void rage128_reg_poke(rage128_t *dev, uint32_t off, uint32_t val);
/* AMCGPIO_Y as the pins read now (chip-driven pads show A, the rest
   show amcgpio_in); a board on the pads reads the other chip through it. */
extern uint32_t rage128_amcgpio_pins(const rage128_t *dev);

/* 2D GUI engine (vid_ati_rage128_2d.c). */
extern int rage128_2d_reg_read(rage128_t *dev, uint32_t off, uint32_t *val);
extern int rage128_2d_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask);
extern int rage128_aux_sc_pass(uint32_t cntl, const uint32_t rect[3][4], int x, int y);

/* 14-bit signed coordinate field (S.14.0), shared by the 2D clippers and
   the 3D draw-state capture. */
static inline int
rage128_sx14(uint32_t v)
{
    return ((int32_t) ((v & 0x3fff) << 18)) >> 18;
}
extern void rage128_2d_packet3(rage128_t *dev, uint32_t hdr, const uint32_t *pl, uint32_t count);
extern void rage128_2d_set_pitch_offset(rage128_t *dev, int is_dst, uint32_t val);
extern void rage128_2d_reset(rage128_t *dev);

/* PM4/CCE command processor (vid_ati_rage128_pm4.c). */
extern int  rage128_pm4_reg_read(rage128_t *dev, uint32_t off, uint32_t *val);
extern int  rage128_pm4_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask);
extern int  rage128_pm4_bus_read(rage128_t *dev, uint32_t bus_addr, uint32_t *val);
extern void rage128_pm4_reset(rage128_t *dev);
extern void rage128_pm4_thread_init(rage128_t *dev);
extern void rage128_pm4_thread_close(rage128_t *dev);
extern void rage128_pm4_drain_wait(rage128_t *dev);
extern int  rage128_pm4_bus_master_ok(rage128_t *dev);
extern void rage128_pm4_kick(rage128_t *dev);
extern void rage128_pm4_gui_reset(rage128_t *dev);
extern void rage128_raster_abandon(rage128_t *dev);
extern void rage128_pm4_flip_notify(rage128_t *dev);
extern void rage128_pace_consume(rage128_t *dev);
extern int  rage128_pm4_enqueue_write(rage128_t *dev, uint32_t off, uint32_t val);
extern int  rage128_pm4_fifo_data_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask);

/* Set on the CCE thread only; lets the register dispatch tell engine
   pokes coming from the packet executor apart from direct CPU access. */
extern _Thread_local int rage128_on_cce_thread;
extern int               rage128_guest_cpu(const char **family, const char **model, unsigned *mhz);

/* Per-thread state for the command-stream capture. Both flags must be
   thread-local: the device-wide pm4_ind_busy is seen by both threads, so
   using it would make the CPU thread's ring pump record its fetches as
   execution while the CCE thread parses, and make a CCE parse hide real
   CPU register writes. rage128_in_indirect means "this thread is inside
   an indirect-buffer parse"; its private partner rage128_ring_fetch
   (vid_ati_rage128_pm4.c) marks the pump's own ring reads, which stay
   unrecorded even when the pump runs from inside a CPU-thread parse. */
extern _Thread_local int rage128_in_indirect;

/* Hand capture records to the OS every 1024 records. The failures worth
   capturing usually hang the guest, so the file is often never closed
   cleanly, and whatever is still in stdio's buffer then is the part that
   mattered. Flushing limits the loss to under 1024 records; data passed
   to write(2) survives a forced quit of the process, so no fsync is
   needed. */
static inline void
rage128_cap_tick(rage128_t *dev)
{
    if ((atomic_fetch_add(&dev->cap_recs, 1) & 0x3ff) == 0x3ff)
        fflush(dev->cap_file);
}

/* Ring size in dwords, minus 1. PM4_BUFFER_CNTL_SIZE, the low 6 bits of
   PM4_BUFFER_CNTL, is log2 of the ring size in dwords minus 1, and the
   size is a power of two of at least 2 dwords (SDK: Load the CCE
   Registers, p. 5-5 / PDF 101), so 0 is a 2-dword ring. The CCE 3D
   supplement states the same field as log2 of the size in qwords, which
   is the same value, but gives a 256-dword minimum (Registers for CCE 3D
   Packets, PM4_BUFFER_CNTL); the mask applies the formula to every value.
   Values past 30 exceed the 32-bit pointers; the mask keeps the low 32
   bits. */
static inline uint32_t
rage128_pm4_ring_mask(rage128_t *dev)
{
    uint32_t l2 = dev->pm4_buffer_cntl & 0x3f;

    return (uint32_t) ((2ull << l2) - 1);
}

/* rage128_pm4_vm_addr turns a CCE address in the chip's virtual memory
   space (the PM4_BUFFER_OFFSET ring base, the vertex array base of an
   indexed draw) into the bus address rage128_pm4_bus_read uses. With the
   AGP aperture in use the result is AGP_BASE plus bits 24:0, as for the
   upper half of card space (RRG: DST_OFFSET, p. 3-137 / PDF 155); bit 25
   (R128_AGP_OFFSET in the Linux DRM) is what marks that half, and the
   Linux DRM writes the ring base as ring_start | R128_AGP_OFFSET (linux
   r128 DRM r128_cce.c r128_cce_init_ring_buffer). With the PCI GART in
   use the value is a GART bus offset that rage128_pm4_bus_read looks up
   in the on-chip table, so it passes through unchanged.
   PM4_IW_INDOFF goes through the same function. The Windows 98
   Direct3D driver has been observed writing it relative to the aperture
   (AGP_BASE must be added), while the Linux DRM writes the absolute
   buf->bus_address (linux r128 DRM r128_state.c
   r128_cce_dispatch_indirect). One
   resolver serves both: AGP_BASE is 32 MB aligned and those buffers lie
   in the aperture's first 32 MB, so AGP_BASE + (absolute & 0x01ffffff)
   equals the absolute address. */
/* Whether a CCE bus access goes through the on-chip PCI GART table.
   PCI_GART_PAGE bit 0 = 0 turns the table on. On an AGP board that alone
   is not enough: the chip masters through the chipset's AGP aperture
   unless BM_CHUNK_0_VAL forces its PM4 reads, or every transfer, onto the
   PCI bus (SDK: RAGE 128 PCI GART, p. 2-24 / PDF 40; Bus Master Registers,
   BM_CHUNK_0_VAL). A PCI board has no aperture path, so there the table
   bit decides alone. */
#define RAGE128_BM_PTR_FORCE_TO_PCI    (1u << 21)
#define RAGE128_BM_PM4_RD_FORCE_TO_PCI (1u << 22)
#define RAGE128_BM_GLOBAL_FORCE_TO_PCI (1u << 23)
static inline int
rage128_gart_walk(rage128_t *dev)
{
    if (dev->pci_gart_page & 1)
        return 0;
    if (!dev->is_agp)
        return 1;
    return !!(dev->bm_chunk_val[0]
              & (RAGE128_BM_PM4_RD_FORCE_TO_PCI | RAGE128_BM_GLOBAL_FORCE_TO_PCI));
}

static inline uint32_t
rage128_pm4_vm_addr(rage128_t *dev, uint32_t vm)
{
    if (!rage128_gart_walk(dev)) /* AGP aperture (PCI GART disabled or not forced) */
        return dev->agp_base + (vm & 0x01ffffffu);
    return vm; /* PCI GART bus offset: bus_read walks the on-chip table */
}

/* The CCE reads the ring by bus mastering only while it runs, that is
   while PM4_BUFFER_CNTL_FIFO_MODE (bits 31:28) selects a bus-master mode
   rather than 0, the non-CCE mode (Registers for CCE 3D Packets,
   PM4_BUFFER_CNTL). The Linux DRM's stop path clears the mode before it
   touches the pointers: r128_do_cce_stop writes R128_PM4_NONPM4, and the
   engine reset that r128_cce_stop runs next zeroes WPTR and then RPTR
   (linux r128 DRM r128_cce.c r128_do_cce_stop, r128_do_cce_reset). So
   while the ring is being reset the engine is stopped, and a moment where
   rptr != wptr reads as idle instead of as a nearly full ring, which
   would fetch the whole uninitialized ring as commands (seen when
   switching virtual terminals under X).

   The test does not also require the PM4_MICRO_FREERUN bit of
   PM4_MICRO_CNTL. The Linux DRM sets that bit at start (r128_do_cce_start),
   and so does the Windows 98 driver, but the Windows 2000 miniport has
   been observed loading the CCE microcode and starting the ring in mode 2
   (R128_PM4_192BM) without ever setting it; requiring it stopped that
   ring (black screen). The drivers observed all clear the mode on stop,
   so the protection above still holds.

   Only the even modes 2, 4, 6 and 8 bus-master the command stream; modes
   1, 3, 5, 7 and 15 take it by PIO through PM4_FIFO_DATA_EVEN / ODD
   (rage128_pm4_pio), so the ring fetch must stay off there (mode list in
   Registers for CCE 3D Packets, PM4_BUFFER_CNTL; xf86-video-r128 defines
   R128_PM4_192PIO to R128_PM4_64PIO_64VCPIO_64INDPIO). */
static inline int
rage128_pm4_ring_bm(rage128_t *dev)
{
    uint32_t mode = dev->pm4_buffer_cntl >> 28;

    return mode != 0 && mode <= 8 && (mode & 1) == 0;
}

/* PIO modes 1, 3, 5, 7 and 15: the host writes the packets into the CCE
   FIFO itself through PM4_FIFO_DATA_EVEN / ODD (SDK: Load the CCE
   Registers, p. 5-6 / PDF 102; SDK: Ring Buffer Server, p. 5-13 /
   PDF 109). */
static inline int
rage128_pm4_pio(rage128_t *dev)
{
    uint32_t mode = dev->pm4_buffer_cntl >> 28;

    return mode == 15 || (mode <= 7 && (mode & 1));
}

/* CCE FIFO dwords a mode gives the command packet stream: 192 in modes 1
   and 2, 128 in 3 and 4, 64 in 5 to 8 and 15 (Registers for CCE 3D
   Packets, PM4_BUFFER_CNTL_FIFO_MODE; linux r128 DRM r128_cce.c
   r128_do_init_cce, cce_fifo_size). Mode 0, the non-CCE mode, has no
   packet region (cce_fifo_size 0), and the reserved modes 9 to 14 list
   none. */
static inline uint32_t
rage128_pm4_fifo_dwords(rage128_t *dev)
{
    switch (dev->pm4_buffer_cntl >> 28) {
        case 1:
        case 2:
            return 192;
        case 3:
        case 4:
            return 128;
        case 5:
        case 6:
        case 7:
        case 8:
        case 15:
            return 64;
        default:
            return 0;
    }
}

/* Engine has queued or in-flight work (any thread may ask). The unfetched-
   ring term is gated on the engine running: a stopped CCE will never fetch
   [rptr,wptr), so during a ring reset (rptr transiently != wptr while
   stopped) the engine is idle, not active. */
static inline int
rage128_pm4_active(rage128_t *dev)
{
    return (atomic_load(&dev->cce_fifo_rd) != atomic_load(&dev->cce_fifo_wr))
        || atomic_load(&dev->cce_executing)
        || atomic_load(&dev->cce_hung)
        || atomic_load(&dev->cce_batch_pending)
        || (rage128_pm4_ring_bm(dev)
            && dev->pm4_rptr != (dev->pm4_wptr & rage128_pm4_ring_mask(dev)));
}

/* 3D engine (vid_ati_rage128_3d.c). rage128_3d_packet3 returns 1 when the
   opcode belongs to the 3D block (handled or knowingly skipped), 0 to fall
   back to the 2D dispatcher. */
/* Replay-harness hook, called each drain-wait iteration: 1 = fed the
   executor, re-check; 2 = nothing left for this packet, abort it. NULL in
   production. */
extern int (*rage128_harness_drain_hook)(rage128_t *dev);
extern int  rage128_3d_reg_read(rage128_t *dev, uint32_t off, uint32_t *val);
extern int  rage128_3d_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask);
extern int  rage128_3d_packet3(rage128_t *dev, uint32_t hdr, const uint32_t *pl, uint32_t count);
extern void rage128_3d_reset(rage128_t *dev);
/* Write a staged Z or color buffer (AGP, or tiled) back to card or guest
   memory. Safe to call again. SUBMIT-THREAD ONLY, at the batch barrier. */
extern void rage128_z_stage_writeback(rage128_t *dev);
extern void rage128_c_stage_writeback(rage128_t *dev);

/* Rasterizer (vid_ati_rage128_3d.c). thr_id and thr_mask select the
   scanlines a call owns; see r128_row_owned (thr_mask 0 owns every row,
   the serial path). Called by the parallel raster workers and by the
   serial submit. */
/* Scanline ownership. thr_mask > 0: a power-of-two worker count n, and
   row y belongs to worker (y & thr_mask) with thr_mask = n - 1.
   thr_mask == 0: serial, every row is owned. thr_mask < 0: a worker
   count n = -thr_mask that is not a power of two, and row y belongs to
   worker y mod n. Any map that gives each row exactly one owner is
   correct, because the per-row values step on every row whoever owns
   it. */
static inline int
r128_row_owned(int32_t y, int thr_id, int thr_mask)
{
    if (thr_mask > 0)
        return (y & thr_mask) == thr_id;
    if (thr_mask == 0)
        return 1;
    return (uint32_t) y % (uint32_t) -thr_mask == (uint32_t) thr_id;
}
/* jfn: the span-JIT block looked up for rs->d at submit time (NULL =
   interpret). The submitter looks it up, never a worker, so the block
   cache is used by one thread only (see rage128_jit_get_block). */
/* Run one JIT row over a color or Z surface that may be tiled: the row
   is walked in 64-byte tile-column pieces, each with its base moved so
   the block's own base + x * bpp lands on the tiled byte; a fully linear
   row is a single jfn call. Restores jt->x0 / x1. Returns the block's
   packed written-column range. Exported for the JIT verify mode, which
   must run the JIT through the same walk it checks. */
extern uint64_t rage128_3d_jit_row(r128_jit_tri_t *jt, r128_jit_span_fn jfn,
                                   int64_t e0, int64_t e1, int64_t e2,
                                   double zline, uint32_t drow, uint32_t zrow,
                                   int32_t py, int c_tld, int z_tld,
                                   uint32_t bpp, uint32_t zbpp);
extern void     rage128_3d_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                               int thr_id, int thr_mask,
                               const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c,
                               r128_jit_span_fn jfn);
extern void     rage128_3d_line(rage128_t *dev, const rage128_raster_state_t *rs,
                                int thr_id, int thr_mask,
                                const r3d_vtx_t *a, const r3d_vtx_t *b,
                                r128_jit_span_fn jfn);
extern void     rage128_3d_point(rage128_t *dev, const rage128_raster_state_t *rs,
                                 int thr_id, int thr_mask, const r3d_vtx_t *v,
                                 r128_jit_span_fn jfn);
/* Lines are a Bresenham walk through the triangle rasterizer's per-pixel
   pipeline (rage128_3d_line: one pixel per step along the major axis,
   the last pixel drawn unless SU_POLY_LINE, SETUP_CNTL bit 18, marks a
   line that is not the last of a polyline; Registers for CCE 3D Packets,
   SETUP_CNTL). Every lane, the GPU capture included, runs the walk
   through the same span callback a triangle uses. A point is drawn as a
   unit square split into two triangles: v holds the two vertex triples,
   drawn in order. The interpreter and the GPU submit path both use this
   function, so both draw the same triangles. */
extern void r3d_point_tris(const r3d_vtx_t *p, r3d_vtx_t v[6]);

/* Parallel rasterizer (vid_ati_rage128_raster.c). The 3D assembler submits
   primitives with the state captured at submit; long-lived workers draw
   interleaved, non-overlapping sets of scanlines, with the same result as
   the serial path bit for bit. rage128_raster_flush finishes the batch
   and joins the workers, and must run before anything depends on the
   drawn pixels (present, a 2D operation on the surface, a change of
   render target, an engine drain). */
extern void rage128_raster_init(rage128_t *dev);
extern void rage128_raster_close(rage128_t *dev);
extern void rage128_raster_submit_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                                      const r3d_vtx_t *a, const r3d_vtx_t *b, const r3d_vtx_t *c);
extern void rage128_raster_submit_line(rage128_t *dev, const rage128_raster_state_t *rs,
                                       const r3d_vtx_t *a, const r3d_vtx_t *b);
extern void rage128_raster_submit_point(rage128_t *dev, const rage128_raster_state_t *rs,
                                        const r3d_vtx_t *v);
extern void rage128_raster_flush(rage128_t *dev);

/* GPU-offload flush-point telemetry (vid_ati_rage128_synctel.c).
   Counters enabled by R128_GPU_SYNC_TELEMETRY on the CPU renderer's
   paths, which they do not change: they count how often a GPU backend
   that batches draws into segments would have to wait for the GPU. With
   the GPU backend on, the option also turns off asynchronous present,
   present snapshots and the GPU 2D queue, since those bypass the
   counting points. Every entry point below except init and close
   expects the caller to have checked dev->synctel != NULL, so the cost
   when off is one load and one branch. */
extern void rage128_synctel_init(rage128_t *dev);
extern void rage128_synctel_close(rage128_t *dev);
/* One primitive submitted with capture rs; b/c NULL for line/point. */
extern void rage128_synctel_draw(rage128_t *dev, const rage128_raster_state_t *rs,
                                 const r3d_vtx_t *a, const r3d_vtx_t *b,
                                 const r3d_vtx_t *c);
/* Guest CPU access to local VRAM (LFB / MM_DATA aperture); addr is the
   masked local card address. */
extern void rage128_synctel_cpu_read(rage128_t *dev, uint32_t addr, uint32_t len);
extern void rage128_synctel_cpu_write(rage128_t *dev, uint32_t addr, uint32_t len);
/* A 2D executor surface window over card bytes [lo, lo+len). */
extern void rage128_synctel_2d_span(rage128_t *dev, uint32_t lo, uint32_t len,
                                    int is_write);
/* Scanout fetch of one line's bytes. */
extern void rage128_synctel_scanline(rage128_t *dev, uint32_t addr, uint32_t len);
/* Scanout base latched (a flip taken): the point where a GPU backend
   would naturally wait anyway. */
extern void rage128_synctel_present(rage128_t *dev);
/* Per-vblank CSV row, then the counters are cleared. CPU thread (vsync
   callback). */
extern void rage128_synctel_frame(rage128_t *dev);

/* 3D draw-state census (vid_ati_rage128_census.c): every distinct
   rage128_draw_state_t a guest programs, with how often it was used and
   which lanes accept it, one line per state at close. Logging only, and
   off unless R128_STATE_CENSUS=<path> is set. rage128_census_draw
   expects the caller to have checked dev->census != NULL; b and c are
   NULL for a point, c is NULL for a line. */
extern void rage128_census_init(rage128_t *dev);
extern void rage128_census_close(rage128_t *dev);
extern void rage128_census_draw(rage128_t *dev, const rage128_raster_state_t *rs,
                                const r3d_vtx_t *b, const r3d_vtx_t *c);
/* Which lane would take a state, for the census, with no side effects:
   JIT 0 = refused, 1 = scalar block, 2 = SoA block; GPU = kernel id,
   -1 = CPU path, -2 = no backend. */
extern int rage128_jit_census_verdict(const rage128_draw_state_t *dstate);
extern int rage128_gpu_census_kernel(rage128_t *dev, const rage128_raster_state_t *rs);

/* ---- Optional GPU raster backend (vid_ati_rage128_gpu.c) ----
   Off by default; turned on per VM by the "gpu_raster" device option.
   R128_GPU, when set, overrides the option (0 = off, verify = on and
   every draw also replayed by the interpreter and compared, any other
   value = on); R128_GPU_2D=verify turns on the 2D comparisons. The
   backend turns itself off, with a logged reason, when the host's Vulkan
   stack cannot support it. The CPU interpreter and JIT stay the default
   renderer and the per-draw fallback. All entry points except init and
   close expect the caller to have checked dev->gpu != NULL. */
#define R128_GPU_OFF    0
#define R128_GPU_ON     1
#define R128_GPU_VERIFY 2
/* Device-init context only (reads the device config). */
extern int  rage128_gpu_mode(void);
extern void rage128_gpu_init(rage128_t *dev);
extern void rage128_gpu_lazy_init(rage128_t *dev, uint32_t off);
extern void rage128_gpu_close(rage128_t *dev);
/* 1 = the draw was added to the GPU segment (the caller must not draw
   it); 0 = the state is not supported and the segment and overlap
   tracking are already settled, so the caller draws it on the CPU.
   SUBMIT/CCE THREAD ONLY. */
extern int rage128_gpu_submit_tri(rage128_t *dev, const rage128_raster_state_t *rs,
                                  const r3d_vtx_t *a, const r3d_vtx_t *b,
                                  const r3d_vtx_t *c);
/* Line submit: one draw whose spans are the Bresenham walk's runs, with
   the same return contract as rage128_gpu_submit_tri (0 = the caller
   walks it on the CPU). Point submit: the two triangles of
   r3d_point_tris, each submitted as a triangle and drawn on the CPU if
   refused, so it always returns 1 when the backend is on. */
extern int rage128_gpu_submit_line(rage128_t *dev, const rage128_raster_state_t *rs,
                                   const r3d_vtx_t *a, const r3d_vtx_t *b);
extern int rage128_gpu_submit_point(rage128_t *dev, const rage128_raster_state_t *rs,
                                    const r3d_vtx_t *v);
#define R128_GPU_2D_REG_GUI  0x100u
#define R128_GPU_2D_REG_BRES 0x101u
/* Ordering point for a 2D operation: submit the pending segment so the
   operation lands after it, without waiting for it. tag is the packet
   opcode, or R128_GPU_2D_REG_GUI / R128_GPU_2D_REG_BRES for an operation
   started by a register write. CCE thread, or the CPU thread only once
   the CCE is stopped. An operation done on the CPU waits, only for the
   bytes it touches, when it maps its surface (rage128_gpu_2d_barrier). */
extern void rage128_gpu_flush(rage128_t *dev, uint32_t tag);
/* SOFT_RESET_GUI: drop the pending segment's draws that were not
   yet sent, without drawing them. CPU thread, executor parked. */
extern void rage128_gpu_abandon(rage128_t *dev);
/* Wait for GPU work on local bytes [lo, lo+len) before a 2D operation's
   CPU walk touches them; writes = the walk stores there, so queued GPU
   texture reads of those bytes must finish too. */
extern void rage128_gpu_2d_barrier(rage128_t *dev, uint32_t lo, uint32_t len,
                                   int writes);
/* Queue a solid rectangle fill at any dst bpp (pixel = the low bpp bytes
   of color) on the GPU, after the pending segment. 1 = queued (the
   caller skips its store loop); 0 = not eligible (the caller fills on
   the CPU). route only selects the statistics counter: 0 solid paint,
   1 mono-expand run, 2 pattern run. */
extern int rage128_gpu_2d_fill(rage128_t *dev, uint32_t addr, uint32_t pitch,
                               uint32_t len, uint32_t rows, uint32_t color,
                               int bpp, int route);
/* The same for a tiled dst: byte columns [xb0, xb1) of linear rows
   [y0, y1] of the surface at tbase with stride tpitch (whole tile
   columns), written in the same tiled layout the CPU walk writes, and
   ordered after the segments in flight by its place in the queue.
   0 = not eligible (right edge past the stride, 24 bpp, a column edge
   not on a dword, AGP or past the end of VRAM, too many pieces), and
   the caller fills on the CPU. */
extern int rage128_gpu_2d_fill_tiled(rage128_t *dev, uint32_t tbase,
                                     uint32_t tpitch, uint32_t xb0,
                                     uint32_t xb1, uint32_t y0, uint32_t y1,
                                     uint32_t color, int bpp);
/* Counts one whole operation queued with a dst that is not 32 bpp
   (0 mono, 1 host color, 2 pattern, 3 solid fill). */
extern void rage128_gpu_2d_nb(rage128_t *dev, int table);
/* Staging ring for queued 2D copies whose source the CPU produces
   (mono-expanded text, host data, pattern pixels): reserve the source
   bytes (NULL = no room, the caller draws on the CPU), write them, then
   queue the rows x len copy. spitch 0 repeats one staged row on every
   dst row (screen-aligned 8x8 pattern tiles). route: 0 host color,
   1 mono opaque, 2 pattern; 3 and up count in queue table route + 1
   (8 = pattern-mono, table 9). Otherwise the same contract as
   rage128_gpu_2d_fill. */
extern void *rage128_gpu_2d_stage(rage128_t *dev, uint32_t bytes,
                                  uint32_t *off);
extern int   rage128_gpu_2d_copy(rage128_t *dev, uint32_t addr, uint32_t pitch,
                                 uint32_t len, uint32_t rows, uint32_t soff,
                                 uint32_t spitch, int route);
/* Queue a general 2D read-modify-write operation, out = (d & A) |
   (~d & B), which any ROP3 with a write mask at any dst bpp reduces to.
   The A and B byte planes are staged at aoff and boff with row stride
   len; dst row r uses plane row r & amod (0 = one row for all,
   7 = 8-row pattern tile, 0xffffffff = a row per dst row). Same contract
   as rage128_gpu_2d_fill. */
extern int rage128_gpu_2d_rmw(rage128_t *dev, uint32_t addr, uint32_t pitch,
                              uint32_t len, uint32_t rows, uint32_t aoff,
                              uint32_t boff, uint32_t amod);
/* Queue a SRCCOPY blit from VRAM to VRAM, already reduced to a plain
   byte copy by the caller. When the source and destination rectangles
   overlap, the copy goes through the staging ring. Same contract as
   rage128_gpu_2d_fill. */
extern int rage128_gpu_2d_blit(rage128_t *dev, uint32_t addr, uint32_t pitch,
                               uint32_t src, uint32_t spitch, uint32_t len,
                               uint32_t rows);
/* Counts why a queue refused an operation, for the mono (0), host color
   (1), pattern paint (2), screen blit (3), keyed blit (4), stretch (5),
   line (6), gradient (7), rmw (8) and pattern-mono (9) queues; the why
   bits are rop, write mask, aux scissor, pattern or fragmentation, bpp,
   memory domain, staging. */
extern void rage128_gpu_2d_qskip(rage128_t *dev, int table, int why);
extern void rage128_gpu_2d_tiled_skip(rage128_t *dev);
/* Counts one whole operation fully queued on tables 4 to 7 (per
   rectangle; the run and byte counts come from the fill and copy
   calls). */
extern void rage128_gpu_2d_qdone(rage128_t *dev, int table);
/* Counts one whole operation queued with per-pixel aux scissor tests
   (aux scissors on, and the whole-rectangle test failed); printed as
   auxseg= for tables 0 to 3 and 8. */
extern void rage128_gpu_2d_qaux(rage128_t *dev, int table);
/* 1 = R128_GPU_2D=verify is on: queue paths whose result the CPU works
   out must set up the comparison (vq2d) instead of returning queued. */
extern int rage128_gpu_2d_verifying(rage128_t *dev);
/* Comparison result for one operation on tables 4 to 7. */
extern void rage128_gpu_2d_verify_bad(rage128_t *dev, int table,
                                      uint64_t bad);
/* GPU span capture, passed to rage128_3d_tri as its jfn. Reads the
   pre-resolved slot cache directly (like an inline-textured JIT block),
   so the tri setup must pre-resolve every reachable mip slot whenever
   this is the jfn -- the JIT's tex-inline predicate does not cover it. */
extern uint64_t rage128_gpu_capture_span(const r128_jit_tri_t *tri,
                                         int64_t e0, int64_t e1, int64_t e2,
                                         double zline, uint32_t drow,
                                         uint32_t zrow, int32_t py);
/* Covered columns [*cx0, *cx1] of one whole row under the capture's own
   edge clip; 0 when the row has none. Lets the tiled row walk skip the
   tile-column pieces the capture would reject. */
extern int rage128_gpu_capture_row_cover(const r128_jit_tri_t *tri,
                                         int64_t e0, int64_t e1, int64_t e2,
                                         int32_t py, int32_t *cx0,
                                         int32_t *cx1);
/* Counting version of the capture: the exact span and pixel count of a
   draw that the bounding-box estimate refused. It writes nothing, and
   the walker must not run the verify or statistics paths around it,
   since it only measures and does not draw. */
extern uint64_t rage128_gpu_count_span(const r128_jit_tri_t *tri,
                                       int64_t e0, int64_t e1, int64_t e2,
                                       double zline, uint32_t drow,
                                       uint32_t zrow, int32_t py);
/* Scanout base latched: submit every outstanding slot and wait for it. */
extern void rage128_gpu_present(rage128_t *dev);
/* The guest CPU or the scanout touches local bytes [addr, addr+len):
   wait if the segment being built or a slot in flight owns them. */
extern void rage128_gpu_cpu_barrier(rage128_t *dev, uint32_t addr, uint32_t len, int writes);
/* Scanout reads one line: it waits only for the writes of a frame that
   has been flipped to. Writes into the buffer on display can tear, as
   they would on a chip that scans memory directly. Returns 0 when the
   line was sent before an unfinished write landed (the caller marks it
   to be drawn again). */
extern int rage128_gpu_scan_barrier(rage128_t *dev, uint32_t addr, uint32_t len);
/* CCE executor, between packets (ring or indirect buffer) and while it
   waits for a packet's next dword: submit the pending segment if a
   scanned line asked for it. No-op off the CCE thread. */
extern void rage128_gpu_scan_serve(rage128_t *dev);
/* CRTC_OFFSET write: everything queued so far is the flipped frame. */
extern void rage128_gpu_flip_mark(rage128_t *dev);
/* Present snapshots: at a flip the GPU copies the finished frame into
   the hidden half of the VRAM allocation, and the CRTC scans the newest
   complete copy, so no thread waits for the GPU at a flip or during the
   scan. */
extern void rage128_gpu_present_copy(rage128_t *dev, uint32_t src,
                                     uint32_t len);
extern int  rage128_gpu_snap_pick(rage128_t *dev, uint32_t want_src,
                                  uint32_t *dst, uint64_t *seq);
/* One check per frame in place of a read fence per scanline: is the GPU
   writing inside the frame's scan range right now? */
extern int rage128_gpu_scan_hazard(rage128_t *dev, uint32_t lo,
                                   uint32_t len);
/* GUI_STAT GUI_ACTIVE: a segment is pending (it is sent here) or in
   flight on the GPU. CPU thread, CCE idle. */
extern int rage128_gpu_engine_busy(rage128_t *dev);
/* End of a pace window: returns the window's read-fence wait in us,
   runs the controller that limits how many segments may be in flight,
   and reports the current limit. CPU thread. */
extern uint64_t rage128_gpu_pace_window(rage128_t *dev, uint32_t wall_ms,
                                        unsigned *cap_out);
/* Wait at the end of a flip for GPU writes in the new scan range; CCE
   thread only. */
extern void rage128_gpu_present_fence(rage128_t *dev, uint32_t src,
                                      uint32_t len);
/* The same wait for clients that never flip, taken on the CCE thread
   when it runs out of work and would otherwise sleep, so the scanout's
   per-line barrier does not have to take it. */
extern void rage128_scanout_fence_idle(rage128_t *dev);
/* Frame timeline probe: a CRTC_OFFSET flip running on the CCE thread
   closes the previous frame's record and opens the next. Profiling
   only. */
extern void rage128_gpu_ftl_flip(rage128_t *dev);
extern void rage128_gpu_2d_fill_skip(rage128_t *dev, int why, uint32_t rop,
                                     uint32_t wmask);
/* Texture staging arena and the GPU. The arena is imported as a second
   bound buffer, so the kernels sample staged levels without a copy.
   These calls are SUBMIT/CPU-THREAD ONLY, like the arena resets.
   quiesce: finish every queued or in-flight segment that uses staged
   bytes; callers must run it before resetting the arena. idle: 1 when no
   queued or in-flight segment uses the arena, so it may be reset
   without waiting. grow (rage128_gpu_stage_grow, below): move the arena
   to at least need bytes and import it again; returns 1 = grown, 0 =
   failed (arena unchanged), -1 = the arena is not GPU-managed (the
   caller reallocates it as in the CPU-only path). */
extern void rage128_gpu_stage_quiesce(rage128_t *dev, int per_draw);
extern int  rage128_gpu_stage_idle(rage128_t *dev);

/* A store into card bytes [lo, lo+len) may have changed bytes that a
   staged level cached across draws was copied from, so drop the cache.
   This is one compare against the combined source range; per-batch
   staging is not affected. Callers: the CPU store barrier, the 2D
   per-operation surface map, the queued 2D operations, and the 3D
   render-target setup (render to texture: the batch flush brings VRAM
   up to date, not the cached copy). */
static inline void
r128_texcache_lock(struct rage128_tex_stage *ts)
{
    while (atomic_exchange_explicit(&ts->lock, 1, memory_order_acquire))
        ;
}

static inline void
r128_texcache_unlock(struct rage128_tex_stage *ts)
{
    atomic_store_explicit(&ts->lock, 0, memory_order_release);
}

static inline void
r128_texcache_dirty(rage128_t *dev, uint32_t lo, uint32_t len)
{
    struct rage128_tex_stage *ts  = &dev->tex_stage;
    uint32_t                  slo = atomic_load_explicit(&ts->src_lo, memory_order_relaxed);
    uint32_t                  shi = atomic_load_explicit(&ts->src_hi, memory_order_relaxed);

    /* Every guest aperture store comes through here, and taking the lock
       costs two atomic read-modify-writes, so a miss is decided without
       it. A possible hit is tested again under the lock, and that test
       decides. */
    if (shi == slo || lo >= shi || (uint64_t) lo + len <= slo)
        return;
    r128_texcache_lock(ts);
    if (ts->src_hi != ts->src_lo && lo < ts->src_hi
        && (uint64_t) lo + len > ts->src_lo) {
        ts->tgen++;
        ts->src_lo = 0;
        ts->src_hi = 0;
        ts->n_kill++;
    }
    r128_texcache_unlock(ts);
}
extern int rage128_gpu_stage_grow(rage128_t *dev, uint32_t need);
/* Staged color and Z targets and the GPU: the c_stage and z_stage
   arenas are imported as bound buffers, so the kernels write staged
   surfaces without a copy. quiesce: finish every queued or in-flight
   segment that writes staged bytes; it must run before either arena is
   written back to memory. grow: the same contract as
   rage128_gpu_stage_grow for the arena st (-1 = not a GPU-managed
   arena, and the caller reallocates it itself). Both SUBMIT/CPU-THREAD
   ONLY. */
extern void rage128_gpu_czstage_quiesce(rage128_t *dev);
extern int  rage128_gpu_czstage_grow(rage128_t                 *dev,
                                     struct rage128_span_stage *st,
                                     uint32_t                   need);

/* OV0 overlay compose. Once per frame, and again when a parameter
   changes mid-frame, all the source-side work for the overlay window
   (nearest-sample scaling, color space conversion, brightness and
   saturation, the video-key compare) runs as one GPU dispatch into a
   row buffer the host can read. The svga per-scanline overlay call then
   only tests the graphics key and overwrites from that buffer, so the
   order of composition (graphics, then overlay, then hardware cursor)
   is unchanged. r128_ov0_frame_t is defined above rage128_t. Called by
   the compositor on the thread that runs the svga scanline render;
   compose waits for its own fence, so the rows are valid on return. */
extern int rage128_gpu_ov0_compose(rage128_t *dev, const r128_ov0_frame_t *f);
/* One row of the last compose ([23:0] RGB, [24] alpha, [25] video-key
   match), NULL when none is valid. */
extern const uint32_t *rage128_gpu_ov0_row(rage128_t *dev, uint32_t row);
extern int             rage128_gpu_ov0_verifying(rage128_t *dev);
extern void            rage128_gpu_ov0_bad(rage128_t *dev, uint64_t bad);

#endif /*VIDEO_ATI_RAGE128_H*/
