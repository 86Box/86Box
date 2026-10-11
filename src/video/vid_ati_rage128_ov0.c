/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- OV0 video overlay.
 *
 *          This file holds the overlay's register latch model (a shadow
 *          set and an active set, with the OV0_REG_LOAD_CNTL lock between
 *          them) and the per-scanline compositor that scales the YUV or
 *          RGB source window onto the screen. The DVD subpicture
 *          registers, which blend into the overlay's pixels, are here too.
 *
 *          The Pro register guide describes only OV0_COL_CONV from this
 *          block. The field layouts come from the Multimedia Registers
 *          supplement [2] and from the XFree86 driver (the DDX), which
 *          drives the block for Xv in xf86-video-r128 r128_video.c
 *          (R128DisplayVideo422, R128DisplayVideo420, R128ResetVideo).
 *          The DDX's register header names the registers and some fields
 *          as macros, such as R128_OV0_KEY_CNTL. Where the supplement and
 *          the DDX disagree, the comment at that spot says so. No public
 *          document gives the subpicture register layout; it is modeled
 *          from what the driver writes.
 *
 *          The compositor is a model, not the chip's datapath: it takes
 *          the nearest source pixel and line, where the chip filters with
 *          two- and four-tap filters.
 *
 *          Relevant literature:
 *
 *          [1] ATI Technologies, "RAGE 128 PRO Register Reference Guide",
 *              RRG-G04500-C Rev 1.01, January 2000. Cited below as
 *              "RRG: <register>, p. <printed page> / PDF <viewer page>".
 *
 *          [2] ATI Technologies, "RAGE 128 Register Reference Supplement:
 *              Multimedia Registers", 1999 (the OV0 field layouts). It has
 *              no page numbers; cited as "Multimedia supplement:
 *              <register>".
 *
 *          [3] ATI Technologies, "RAGE 128 VR / RAGE 128 GL Graphics
 *              Controller Specifications", GCS-C04100 Rev 0.05, November
 *              1998 (the subpicture unit). Cited as "GCS: ...".
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_ati_rage128.h>
#include <86box/vid_ati_rage128_regs.h>

#define OV0_ACT(dev, off) ((dev)->ov0.active[RAGE128_OV0_REG(off)])

/* Log, at most 16 times in all, an OV0 setting the compositor does not
   model, so a guest that uses one shows in the log instead of failing
   silently. */
static void
ov0_log_unhandled(const char *what, uint32_t val)
{
    static int logged = 0;

    if (logged++ < 16)
        rage128_log("[r128 OV0] unhandled: %s = %08x\n", what, val);
}

/* ------------------------------------------------------------------ */
/* Latch model.                                                        */
/* ------------------------------------------------------------------ */

/* The double-buffered fields of the block. With OV0_DOUBLE_BUFFER_REGS
   (bit 24 of OV0_SCALE_CNTL) set, these fields move from the programmed
   register into the working register only at vertical blank, so that a
   scaler setup written over several registers takes effect as one
   (Multimedia supplement: OV0_SCALE_CNTL). Every other bit of these
   registers, and every other register of the block, takes effect at the
   write; the buffer addresses and pitches in particular are not double
   buffered (SDK: Overlay Autonomous Updating, p. 7-12 / PDF 182).
   The supplement's list of double-buffered fields omits the window
   start and end coordinates. Buffering them is modeled on the SDK's
   statement that the overlay position can be changed autonomously,
   like the scale ratio (same page). The same list includes the
   deinterlace pattern, but the field's own description says it is not
   double buffered (Multimedia supplement: OV0_DEINTERLACE_PATTERN).
   The table follows the field description, so only the length field
   is buffered. */
static const struct {
    uint32_t off;
    uint32_t mask;
} ov0_buffered_fields[] = {
    { RAGE128_OV0_Y_X_START,              0x07ff07ff },
    { RAGE128_OV0_Y_X_END,                0x07ff07ff },
    { RAGE128_OV0_SCALE_CNTL,             0x00ff8000 }, /* smart switch and burst */
    { RAGE128_OV0_V_INC,                  0x03ffff00 },
    { RAGE128_OV0_P1_V_ACCUM_INIT,        0x03ff8003 },
    { RAGE128_OV0_P23_V_ACCUM_INIT,       0x01ff8003 },
    { RAGE128_OV0_P1_BLANK_LINES_AT_TOP,  0x0fff0fff },
    { RAGE128_OV0_P23_BLANK_LINES_AT_TOP, 0x07ff07ff },
    { RAGE128_OV0_AUTO_FLIP_CNTL,         RAGE128_OV0_SOFT_EOF_TOGGLE },
    { RAGE128_OV0_DEINTERLACE_PATTERN,    0xf0000000 }, /* pattern length */
    { RAGE128_OV0_H_INC,                  0x3fff3fff },
    { RAGE128_OV0_STEP_BY,                0x00000707 },
    { RAGE128_OV0_P1_H_ACCUM_INIT,        0xf00f8000 },
    { RAGE128_OV0_P23_H_ACCUM_INIT,       0x700f8000 },
    { RAGE128_OV0_P1_X_START_END,         0x000f07ff },
    { RAGE128_OV0_P2_X_START_END,         0x000f03ff },
    { RAGE128_OV0_P3_X_START_END,         0x000f03ff },
};

/* The double-buffered field mask of block register idx, 0 for a
   register with no such field. */
static uint32_t
ov0_buffered_mask(int idx)
{
    for (size_t i = 0; i < sizeof(ov0_buffered_fields) / sizeof(ov0_buffered_fields[0]); i++)
        if (RAGE128_OV0_REG(ov0_buffered_fields[i].off) == (uint32_t) idx)
            return ov0_buffered_fields[i].mask;
    return 0;
}

/* Whether the double-buffered fields wait for the vertical blank. The
   bit is itself not double buffered, so the shadow copy is the value
   in force. */
static inline int
ov0_double_buffered(rage128_t *dev)
{
    return !!(dev->ov0.shadow[RAGE128_OV0_REG(RAGE128_OV0_SCALE_CNTL)]
              & RAGE128_OV0_SCALER_DOUBLE_BUFFER);
}

/* The scaler's field submit. OV0_SOFT_EOF_TOGGLE, the double-buffered
   bit 6 of OV0_AUTO_FLIP_CNTRL, has just reached the active set; when
   it differs from the scaler's internal copy, the copy takes the new
   state and the scaler takes the field the register describes, the
   buffer number OV0_SOFT_BUF_NUM and the odd flag OV0_SOFT_BUF_ODD.
   Until then both are ignored, so a driver that fills an unused buffer
   set, writes its number and flips the toggle in one dword gets the new
   buffer whole at the vertical blank, never part-way down a frame
   (Multimedia supplement: OV0_AUTO_FLIP_CNTRL; SDK: Overlay Autonomous
   Updating, p. 7-12 / PDF 182). A buffer number written without a
   toggle change switches nothing. */
static void
ov0_take_flip(rage128_t *dev)
{
    uint32_t flip   = OV0_ACT(dev, RAGE128_OV0_AUTO_FLIP_CNTL);
    uint32_t toggle = flip & RAGE128_OV0_SOFT_EOF_TOGGLE;

    if (toggle == dev->ov0.eof_toggle)
        return;
    dev->ov0.eof_toggle = toggle;
    dev->ov0.flip_sel   = flip & (RAGE128_OV0_SOFT_BUF_NUM | RAGE128_OV0_SOFT_BUF_ODD);
}

/* Publish block register idx from the shadow set, after an unlocked
   write to it or at the unlock. The bits outside its double-buffered
   fields reach the active set now. The fields reach it now too while
   OV0_DOUBLE_BUFFER_REGS is clear; while it is set they are left as
   they are and the register is marked pending for the vertical blank
   commit. With the bit clear the toggle of OV0_AUTO_FLIP_CNTRL has just
   moved, so the field submit runs here, the way the pending fields are
   released at once in that state; the supplement describes the toggle
   only as double buffered, so taking the flip at the write is
   modeled. */
static void
ov0_publish(rage128_t *dev, int idx)
{
    uint32_t held = ov0_double_buffered(dev) ? ov0_buffered_mask(idx) : 0;

    dev->ov0.active[idx] = (dev->ov0.active[idx] & held) | (dev->ov0.shadow[idx] & ~held);
    if (held)
        atomic_fetch_or_explicit(&dev->ov0.pending, 1ull << idx, memory_order_release);
    else if (idx == RAGE128_OV0_REG(RAGE128_OV0_AUTO_FLIP_CNTL))
        ov0_take_flip(dev);
}

/* Commit the double-buffered fields of every pending register from the
   shadow set into the active set, run the field submit when the toggle
   of OV0_AUTO_FLIP_CNTRL was among them, and refresh the overlay
   geometry once. Called at the vertical blank while the lock is clear,
   and from the write path when OV0_DOUBLE_BUFFER_REGS is cleared with
   registers still pending. That release is modeled: the documents do
   not say what clearing the bit does to fields waiting for the vertical
   blank. */
static void
ov0_commit_pending(rage128_t *dev)
{
    uint64_t pend = atomic_exchange_explicit(&dev->ov0.pending, 0, memory_order_acq_rel);

    if (!pend)
        return;
    for (int idx = 0; idx < 64; idx++) {
        if (pend & (1ull << idx)) {
            uint32_t m = ov0_buffered_mask(idx);

            dev->ov0.active[idx] = (dev->ov0.active[idx] & ~m) | (dev->ov0.shadow[idx] & m);
        }
    }
    if (pend & (1ull << RAGE128_OV0_REG(RAGE128_OV0_AUTO_FLIP_CNTL)))
        ov0_take_flip(dev);
    rage128_ov0_update(dev);
}

/* Vertical blank: the point where the double-buffered fields take
   effect. OV0_LOCK holds the commit back, so the pending registers keep
   waiting and the next vertical blank with the lock clear takes them
   (Multimedia supplement: OV0_REG_LOAD_CNTL). Called from
   rage128_vblank_start after the completed frame's CRC capture, since
   that frame was composed from the old working set. */
void
rage128_ov0_vblank(rage128_t *dev)
{
    if (dev->ov0_reg_load_cntl & RAGE128_OV0_REG_LD_CTL_LOCK)
        return;
    ov0_commit_pending(dev);
}

/* The unlock: publish every register the lock held in the shadow set,
   the way an unlocked write publishes one, and refresh the svga core's
   overlay window. The core latches svga->overlay once per frame and
   calls overlay_draw for each scanline the window covers. The locked
   values' double-buffered fields then take effect at the next vertical
   blank after the unlock (Multimedia supplement: OV0_REG_LOAD_CNTL);
   the unlock itself commits nothing. Holding the bits outside those
   fields until the unlock is modeled: the supplement documents the
   lock as holding back only the vertical blank transfer. */
static void
rage128_ov0_apply(rage128_t *dev)
{
    for (int idx = 0; idx < 64; idx++)
        ov0_publish(dev, idx);
    rage128_ov0_update(dev);
    if (!ov0_double_buffered(dev))
        ov0_commit_pending(dev);
}

void
rage128_ov0_update(rage128_t *dev)
{
    svga_t  *svga  = &dev->svga;
    uint32_t scale = OV0_ACT(dev, RAGE128_OV0_SCALE_CNTL);
    uint32_t strt  = OV0_ACT(dev, RAGE128_OV0_Y_X_START);
    uint32_t end   = OV0_ACT(dev, RAGE128_OV0_Y_X_END);
    int      x1    = strt & 0xfff;
    int      y1    = (strt >> 16) & 0xfff;
    int      y2    = (end >> 16) & 0xfff;

    /* The model shows the overlay only while the extended display path
       is on (CRTC_EXT_DISP_EN), as it does the hardware cursor.
       OV0_SOFT_RESET, bit 31 of OV0_SCALE_CNTL, holds the scaler in
       reset (Multimedia supplement: OV0_SCALE_CNTL). */
    svga->overlay.ena = (scale & RAGE128_OV0_SCALER_ENABLE)
        && !(scale & RAGE128_OV0_SCALER_SOFT_RESET)
        && (dev->crtc_gen_cntl & RAGE128_CRTC_EXT_DISP_EN);
    svga->overlay.x    = x1;
    svga->overlay.y    = y1;
    svga->overlay.xoff = 0;
    svga->overlay.yoff = 0;
    /* OV0_Y_X_END is the last pixel and line of the window, inclusive
       (Multimedia supplement: OV0_Y_X_END). The Windows 2000 driver
       subtracts one from its right and bottom edges before writing it
       (RE: ati2dvaa.dll @241ea). The DDX writes the exclusive edge of
       its clip box (R128DisplayVideo422), one past the end; that extra
       column and line lie outside the area it paints with the color
       key, so graphics shows there. */
    svga->overlay.cur_ysize = (y2 >= y1) ? (y2 - y1 + 1) : 0;
    if (!svga->overlay.cur_ysize)
        svga->overlay.ena = 0;
}

int
rage128_ov0_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    if (off < RAGE128_OV0_BLOCK_BASE || off > RAGE128_OV0_BLOCK_END)
        return 0;

    if (off == RAGE128_OV0_REG_LOAD_CNTL) {
        /* OV0_LOCK_READBACK (bit 3) reports that a requested lock has
           taken effect. The supplement says it normally takes before the
           first read (Multimedia supplement: OV0_REG_LOAD_CNTL), and both
           the DDX (R128DisplayVideo422) and the Windows 98 DirectX HAL
           (RE: ati3draa.dll @b00c3fc0) spin on it after setting OV0_LOCK.
           The model grants the lock at once, so the bit reads back equal
           to OV0_LOCK. OV0_VBLANK_DURING_LOCK (bit 1) is the status
           latched at the last falling edge of OV0_LOCK (see the write
           path); a read during a lock shows that previous interval, not
           the vertical blanks of the one in progress. Neither bit is ever
           in the stored word, so both are ORed in here. */
        *val = dev->ov0_reg_load_cntl;
        if (*val & RAGE128_OV0_REG_LD_CTL_LOCK)
            *val |= RAGE128_OV0_REG_LD_CTL_LOCK_READBACK;
        if (dev->ov0.vblank_during_lock)
            *val |= RAGE128_OV0_REG_LD_CTL_VBLANK_DURING_LOCK;
        return 1;
    }

    /* Registers read back the programmed (shadow) values. */
    *val = dev->ov0.shadow[RAGE128_OV0_REG(off)];
    return 1;
}

int
rage128_ov0_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    int idx;

    if (off < RAGE128_OV0_BLOCK_BASE || off > RAGE128_OV0_BLOCK_END)
        return 0;

    if (off == RAGE128_OV0_REG_LOAD_CNTL) {
        int was_locked = !!(dev->ov0_reg_load_cntl & RAGE128_OV0_REG_LD_CTL_LOCK);
        int locked;

        /* OV0_LOCK_READBACK (bit 3) and OV0_VBLANK_DURING_LOCK (bit 1)
           are read-only status (Multimedia supplement: OV0_REG_LOAD_CNTL),
           so a write can neither set nor clear them: both stay out of
           the stored word and the read path synthesizes them. */
        dev->ov0_reg_load_cntl = ((dev->ov0_reg_load_cntl & ~mask) | (val & mask))
            & ~(RAGE128_OV0_REG_LD_CTL_LOCK_READBACK
                | RAGE128_OV0_REG_LD_CTL_VBLANK_DURING_LOCK);
        locked = !!(dev->ov0_reg_load_cntl & RAGE128_OV0_REG_LD_CTL_LOCK);

        /* OV0_VBLANK_DURING_LOCK is updated only when OV0_LOCK goes from 1
           to 0, to 1 when a vertical blank happened while the lock was
           held and to 0 otherwise. The rising edge takes the vertical
           blank count; a repeated OV0_LOCK=1 write is not an edge and
           keeps that snapshot. The falling edge compares the count with
           the snapshot and latches the result, which then holds through
           every write and the next lock until the following falling
           edge. */
        if (!was_locked && locked)
            dev->ov0.lock_vblank_seq = atomic_load_explicit(&dev->ov0.vblank_seq,
                                                            memory_order_relaxed);
        if (was_locked && !locked) {
            dev->ov0.vblank_during_lock = atomic_load_explicit(&dev->ov0.vblank_seq,
                                                               memory_order_relaxed)
                != dev->ov0.lock_vblank_seq;
            /* Clearing OV0_LOCK publishes the shadow set (see
               rage128_ov0_apply): the double-buffered fields wait for the
               next vertical blank, everything else takes effect now. A
               write that leaves the lock clear or sets it again is not an
               unlock and publishes nothing. */
            rage128_ov0_apply(dev);
        }
        return 1;
    }

    idx                  = RAGE128_OV0_REG(off);
    dev->ov0.shadow[idx] = (dev->ov0.shadow[idx] & ~mask) | (val & mask);

    /* With the lock clear, a write is published at once: its bits
       outside the double-buffered fields reach the active set, and the
       fields do too unless OV0_DOUBLE_BUFFER_REGS holds them for the
       vertical blank (see ov0_publish). The DDX writes the key and
       color-adjust registers this way (R128ResetVideo). The window
       registers refresh the svga core's overlay geometry for the bits
       that took effect; the commit refreshes it again for the fields.
       Clearing OV0_DOUBLE_BUFFER_REGS with registers pending releases
       them, since nothing holds them any more. */
    if (!(dev->ov0_reg_load_cntl & RAGE128_OV0_REG_LD_CTL_LOCK)) {
        ov0_publish(dev, idx);
        if (off == RAGE128_OV0_SCALE_CNTL || off == RAGE128_OV0_Y_X_START
            || off == RAGE128_OV0_Y_X_END)
            rage128_ov0_update(dev);
        if (!ov0_double_buffered(dev))
            ov0_commit_pending(dev);
    }
    return 1;
}

void
rage128_ov0_reset(rage128_t *dev)
{
    memset(&dev->ov0, 0, sizeof(dev->ov0));
    memset(&dev->subpic, 0, sizeof(dev->subpic));
    /* The atomic vertical blank counter and pending mask are stored
       through their own interface rather than left to the memset: no
       register waits for a vertical blank after a reset, and the snapshot
       and the latched OV0_VBLANK_DURING_LOCK status start at 0 with the
       rest. */
    atomic_store_explicit(&dev->ov0.vblank_seq, 0, memory_order_relaxed);
    atomic_store_explicit(&dev->ov0.pending, 0, memory_order_relaxed);
    dev->ov0.lock_vblank_seq    = 0;
    dev->ov0.vblank_during_lock = 0;
    dev->ov0_reg_load_cntl      = 0;
    rage128_ov0_update(dev);
}

/* ------------------------------------------------------------------ */
/* DVD subpicture block (0x0540-0x0588).                               */
/* ------------------------------------------------------------------ */

/* No public document gives this block's register layout. The GCS lists
   the unit: a 2 bpp DVD subpicture decoder with button support, and a
   scaler (GCS: DVD Subpicture Decoder and Scaler, p. 4-4 / PDF 52). The
   registers here are modeled from what the driver writes.

   Reads return the programmed value. The driver read-modify-writes
   RAGE128_SUBPIC_CNTL around each update, so a read that returned
   anything else would make it drop the display enable. Writes collect in
   subpic.regs, and the compositor copies them to subpic.active once per
   frame. The supplement's OV0_LOCK text has the subpicture registers
   updated at vertical blank along with the scaler's (Multimedia
   supplement: OV0_REG_LOAD_CNTL), and the driver rewrites the pixel data
   pointer and flips its double buffer mid-frame without a strobe, so
   reading the registers live on each line would tear the subtitle.
   RAGE128_SUBPIC_CNTL_NEW_FRAME reads back clear, like a strobe that has
   been taken. */

int
rage128_subpic_reg_read(rage128_t *dev, uint32_t off, uint32_t *val)
{
    if (off < RAGE128_SUBPIC_BLOCK_BASE || off > RAGE128_SUBPIC_BLOCK_END)
        return 0;

    if (off == RAGE128_SUBPIC_PALETTE_DATA)
        *val = dev->subpic.pal[dev->subpic.regs[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_PALETTE_INDEX)] & 0xf];
    else if (off == RAGE128_SUBPIC_CNTL)
        *val = dev->subpic.regs[0] & ~RAGE128_SUBPIC_CNTL_NEW_FRAME;
    else
        *val = dev->subpic.regs[RAGE128_SUBPIC_REG(off)];
    return 1;
}

int
rage128_subpic_reg_write(rage128_t *dev, uint32_t off, uint32_t val, uint32_t mask)
{
    uint32_t idx;

    if (off < RAGE128_SUBPIC_BLOCK_BASE || off > RAGE128_SUBPIC_BLOCK_END)
        return 0;

    idx                   = RAGE128_SUBPIC_REG(off);
    dev->subpic.regs[idx] = (dev->subpic.regs[idx] & ~mask) | (val & mask);

    if (off == RAGE128_SUBPIC_PALETTE_DATA)
        dev->subpic.pal[dev->subpic.regs[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_PALETTE_INDEX)] & 0xf] = dev->subpic.regs[idx];

    return 1;
}

/* Blend the subpicture into one video pixel, in YCbCr before the
   conversion to RGB. Output pixel (out_x, out_row) maps into subpicture
   space through the block's own 16.16 step and start registers; the
   model takes the nearest subpicture pixel, where the GCS lists a
   two-tap scaler. The pixel's 2 bpp class n picks a palette index from
   bits [19+4n:16+4n] of RAGE128_SUBPIC_COLOR_CONTRAST and a contrast
   from bits [3+4n:4n], so class 0 (background) is the low nibble of each
   half, the order of the DVD subpicture color and contrast commands.
   Contrast 0 to 15 is modeled as a linear blend from transparent to
   opaque. */
static inline void
subpic_blend(rage128_t *dev, int out_x, int out_row, int *y, int *cb, int *cr)
{
    uint32_t *r = dev->subpic.active;

    if (!(r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_CNTL)] & RAGE128_SUBPIC_CNTL_DISPLAY_EN))
        return;

    uint32_t pitch = r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_PITCH_LENGTH)] & 0xffff;
    if (!pitch)
        return;

    int su = (int) ((r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_H_ACC)]
                     + (uint32_t) out_x * r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_H_STEP)])
                    >> 16);
    int sv = (int) ((r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_V_ACC)]
                     + (uint32_t) out_row * r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_V_STEP)])
                    >> 16);

    uint32_t das = r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_DAREA_START)];
    uint32_t dae = r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_DAREA_END)];
    int      x0 = das & 0x3ff, y0 = (das >> 16) & 0x3ff;
    int      x1 = dae & 0x3ff, y1 = (dae >> 16) & 0x3ff;

    if (su < x0 || su > x1 || sv < y0 || sv > y1)
        return;

    /* 2 bpp, least significant pair first: pixel n of a byte is bits
       [2n+1:2n]. This order is the one that decodes a captured subtitle
       correctly; most significant first decodes solid menu rectangles
       the same way but garbles text. RAGE128_SUBPIC_PXD_A points at the
       display area's first row, y0, not at row 0: the driver's buffer
       holds exactly the display-area rows and other data follows it, so
       the row index is taken relative to y0. */
    uint32_t a = (r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_PXD_A)]
                  + (uint32_t) (sv - y0) * pitch + ((uint32_t) su >> 2))
        & dev->vram_mask;
    int cl = (dev->svga.vram[a] >> ((su & 3) * 2)) & 3;

    /* Button highlight: inside the rectangle given by RAGE128_SUBPIC_HL_TOP
       and RAGE128_SUBPIC_HL_BOTTOM, RAGE128_SUBPIC_HL_COLOR_CONTRAST
       replaces the global color and contrast word. */
    uint32_t cc  = r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_COLOR_CONTRAST)];
    uint32_t hlt = r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_HL_TOP)];
    uint32_t hlb = r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_HL_BOTTOM)];

    if ((hlb & 0x3ff) > (hlt & 0x3ff) && ((hlb >> 16) & 0x3ff) > ((hlt >> 16) & 0x3ff)
        && su >= (int) (hlt & 0x3ff) && su <= (int) (hlb & 0x3ff)
        && sv >= (int) ((hlt >> 16) & 0x3ff) && sv <= (int) ((hlb >> 16) & 0x3ff))
        cc = r[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_HL_COLOR_CONTRAST)];

    int k = (cc >> (cl * 4)) & 0xf;

    if (!k)
        return;

    uint32_t pe = dev->subpic.apal[(cc >> (16 + cl * 4)) & 0xf];
    int      sy = (pe >> 16) & 0xff, scb = (pe >> 8) & 0xff, scr = pe & 0xff;

    *y  = (*y * (15 - k) + sy * k + 7) / 15;
    *cb = (*cb * (15 - k) + scb * k + 7) / 15;
    *cr = (*cr * (15 - k) + scr * k + 7) / 15;
}

/* ------------------------------------------------------------------ */
/* Compositor.                                                         */
/* ------------------------------------------------------------------ */

/* Rebuild a .20 horizontal start from OV0_P1_H_ACCUM_INIT or
   OV0_P23_H_ACCUM_INIT. Bits [19:15] are the fraction that sets the
   filter blend for the first output pixel, already at their .20
   position. The integer part is OV0_PRESHIFT_P1_TO [31:28] (or
   OV0_PRESHIFT_P23_TO [30:28]), the number of source pixels the
   four-tap filter takes in before the first output pixel (Multimedia
   supplement: OV0_P1_H_ACCUM_INIT); it moves to bit 20 and up. int_mask
   is 0xf for P1 and 0x7 for P23. The DDX builds these registers from a
   16.16 value in R128DisplayVideo422 and R128DisplayVideo420. The
   vertical start registers need no rebuild: their field at [25:15] or
   [24:15] is one fixed-point value already at its .20 position. */
static inline uint32_t
ov0_h_accum_init(uint32_t reg, uint32_t int_mask)
{
    return (((reg >> 28) & int_mask) << 20) | (reg & 0x000f8000);
}

/* YCbCr to opaque RGB through the shared BT.601 matrix, r128_yuv_to_rgb.
   The caller has already applied OV0_COLOUR_CNTL: the brightness offset
   to Y and the saturation gains to Cb and Cr. The programmable matrix in
   OV0_COL_CONV (RRG: OV0_COL_CONV, p. 3-217 / PDF 235) and the color
   temperature bit OV0_Y2R_TEMP are not modeled. */
static inline uint32_t
ov0_yuv_to_rgb(int y, int cb, int cr)
{
    return 0xff000000u | r128_yuv_to_rgb(y, cb, cr);
}

/* Graphics key: the model compares the framebuffer pixel as stored,
   ahead of the palette and DAC. The DDX sets OV0_GRAPHICS_KEY_MSK to
   (1 << depth) - 1 (R128ResetVideo), a mask that fits the stored pixel,
   and the supplement widens the key and mask to 32 bits for the spare
   byte of 32 bpp modes (Multimedia supplement: OV0_GRAPHICS_KEY_CLR).
   So at 8 bpp the compare is on the palette index, not its color, and
   loading a gamma ramp does not change which pixels match. The scanline renderer has already advanced
   svga->memaddr, but memaddr_backup holds this line's start address
   until the end-of-line advance, so screen x maps linearly from it to the
   same bytes the renderer read. */
static inline int
ov0_gfx_key_match(rage128_t *dev, int x)
{
    svga_t  *svga = &dev->svga;
    uint32_t clr  = OV0_ACT(dev, RAGE128_OV0_GRAPHICS_KEY_CLR);
    uint32_t msk  = OV0_ACT(dev, RAGE128_OV0_GRAPHICS_KEY_MSK);
    int      bpb  = (svga->bpp == 15) ? 2 : ((svga->bpp + 7) >> 3);
    uint32_t ma   = svga->memaddr_backup + (uint32_t) x * (uint32_t) bpb;
    uint32_t px;

    if (svga->remap_required)
        ma = svga->remap_func(svga, ma);
    ma &= svga->vram_display_mask;
    switch (bpb) {
        case 1:
            px = svga->vram[ma];
            break;
        case 2:
            px = svga->vram[ma]
                | ((uint32_t) svga->vram[(ma + 1) & svga->vram_display_mask] << 8);
            break;
        default:
            /* 24 and 32 bpp. The supplement widens OV0_GRAPHICS_KEY_CLR
               and OV0_GRAPHICS_KEY_MSK to 32 bits for the fourth byte of
               a 32 bpp pixel (Multimedia supplement:
               OV0_GRAPHICS_KEY_MSK), so that byte joins the compare at
               32 bpp and the mask decides whether it counts. At 24 bpp
               the next byte belongs to the following pixel and stays
               out. */
            px = svga->vram[ma]
                | ((uint32_t) svga->vram[(ma + 1) & svga->vram_display_mask] << 8)
                | ((uint32_t) svga->vram[(ma + 2) & svga->vram_display_mask] << 16);
            if (bpb == 4)
                px |= (uint32_t) svga->vram[(ma + 3) & svga->vram_display_mask] << 24;
            break;
    }
    return ((px ^ clr) & msk) == 0;
}

/* One key function from OV0_KEY_CNTL, OV0_VIDEO_KEY_FN [2:0] or
   OV0_GRAPHICS_KEY_FN [6:4]: returns 1 when the key lets video through.
   In the supplement 0 is false, 1 is true, 4 is true where the masked
   pixel differs from the masked key, 5 is true where they are equal, and
   2 and 3 are reserved (Multimedia supplement: OV0_KEY_CNTL).
   OV0_CMP_MIX then ORs or ANDs the two results. The supplement does not
   say which layer a true result selects; the model shows video, which
   is what the drivers' values need. The Windows 98 DirectX HAL builds
   its value from 0x11 (both true: no key), 0x114 (source key) and an OR
   of 0x50 (destination key), and writes 0 to hide the overlay (RE:
   ati3draa.dll @b00bd970). The DDX header has 4 and 5 the other way
   round (R128_GRAPHIC_KEY_FN_EQ is 0x40, R128_GRAPHIC_KEY_FN_NE is
   0x50), but the DDX only ever writes 0x50 and paints the key color
   where video should show, which matches the supplement. The Windows
   2000 driver writes video key function 3, a reserved code, for its
   source key (RE: ati2dvaa.dll @24cd8); the model treats 3 as 4, a guess
   from that use. */
static inline int
ov0_key_fn(uint32_t fn, int eq)
{
    switch (fn & 7) {
        case 1:
            return 1;
        case 3:
        case 4:
            return !eq;
        case 5:
            return eq;
        default: /* FALSE + reserved encodings */
            return 0;
    }
}

/* Bytes one plane's fetches can reach from its base: the active lines
   times the pitch, plus the bytes of one line's read. The product is
   formed in 64 bits because the pitch field, bits 25:4 of
   OV0_VID_BUF_PITCH0_VALUE, times the 12-bit line count of
   OV0_P1_BLANK_LINES_AT_TOP (Multimedia supplement) passes 32 bits.
   Every fetch is masked by the VRAM size, so a longer span reads no more
   than the whole of VRAM and is cut to that size; the 32-bit barrier
   length and cached-source bounds built from it then never wrap. */
static uint32_t
ov0_gpu_src_len(const rage128_t *dev, uint32_t lines, uint32_t pitch, uint32_t row_len)
{
    uint64_t len = (uint64_t) lines * pitch + row_len;

    return (len > (uint64_t) dev->vram_size) ? (uint32_t) dev->vram_size : (uint32_t) len;
}

/* Wait for queued GPU writes into one plane's source bytes before the
   compositor reads them. The fetches are masked by the VRAM size, so a
   plane that crosses the top of VRAM also reads from the bottom, and
   one linear range cannot name both pieces: such a plane claims the
   whole of VRAM, the same rule rng_fold applies to the GPU's own
   store ranges. A plane that ends at or below the top keeps its exact
   range. */
static void
ov0_gpu_read_barrier(rage128_t *dev, uint32_t base, uint32_t len, uint32_t vmask)
{
    uint32_t lo   = base & vmask;
    uint32_t hi   = lo + len;
    uint32_t size = vmask + 1u;

    if (hi > size) {
        lo = 0;
        hi = size;
    }
    rage128_gpu_cpu_barrier(dev, lo, hi - lo, 0);
}

/* GPU lane. One GPU dispatch does the source-side work for the whole
   window: nearest-pixel scaling, YUV to RGB conversion, color adjust and
   the video-key compare. The per-line caller then only applies the keys
   and stores the pixels, so the drawing order (graphics, then overlay,
   then hardware cursor) stays the same as on the CPU path.

   The dispatch runs once per frame, and again whenever the parameter
   block for this line differs from the one last composed. The CPU
   compositor reads the active registers on every line, so a register
   change part-way down a frame, such as a buffer flip through
   OV0_AUTO_FLIP_CNTL, shows from that line on; the second dispatch makes
   the GPU lane show it on the same line.

   Each dispatch first runs read barriers over the source buffers, so GPU
   writes queued into them land before either compositor reads them.
   Returns the composed row, or NULL when this line falls back to the
   CPU compositor. */
static const uint32_t *
ov0_gpu_frame_row(rage128_t *dev, const r128_ov0_frame_t *f, int fmt,
                  int p1_xe, int p2_xe, int p3_xe, uint32_t vmask, int width,
                  int dda_row)
{
    if (dev->ov0.gpu_frame != dev->frame_count + 1
        || (dev->ov0.gpu_ok && memcmp(f, &dev->ov0.gpu_f, sizeof(*f)))) {
        uint32_t rl_y = (uint32_t) (p1_xe + 1)
            * ((fmt == RAGE128_OV0_SCALER_SOURCE_YUV12)       ? 1
                   : (fmt == RAGE128_OV0_SCALER_SOURCE_32BPP) ? 4
                                                              : 2);

        uint32_t len_y  = ov0_gpu_src_len(dev, f->src_h_y, f->pitch_y, rl_y);
        uint32_t src_lo = f->base0 & vmask;
        uint32_t src_hi = src_lo + len_y;

        ov0_gpu_read_barrier(dev, f->base0, len_y, vmask);
        if (fmt == RAGE128_OV0_SCALER_SOURCE_YUV12) {
            uint32_t rl_u  = (uint32_t) (p2_xe + 1);
            uint32_t rl_v  = (uint32_t) (p3_xe + 1);
            uint32_t len_u = ov0_gpu_src_len(dev, f->src_h_c, f->pitch_u, rl_u);
            uint32_t len_v = ov0_gpu_src_len(dev, f->src_h_c, f->pitch_v, rl_v);
            uint32_t lo_u  = f->base_u & vmask;
            uint32_t lo_v  = f->base_v & vmask;
            uint32_t hi_u  = lo_u + len_u;
            uint32_t hi_v  = lo_v + len_v;

            if (lo_u < src_lo)
                src_lo = lo_u;
            if (lo_v < src_lo)
                src_lo = lo_v;
            if (hi_u > src_hi)
                src_hi = hi_u;
            if (hi_v > src_hi)
                src_hi = hi_v;
            ov0_gpu_read_barrier(dev, f->base_u, len_u, vmask);
            ov0_gpu_read_barrier(dev, f->base_v, len_v, vmask);
        }
        /* Both compositors mask every fetch by the VRAM size, so a
           source that crosses the top of VRAM reads its remaining bytes
           from the bottom. The bounds the store barrier compares against
           are one [lo, hi) interval and cannot name those two pieces, so
           a crossing source claims the whole of VRAM: every store then
           invalidates the frame, which costs a recompose but never
           misses one. */
        if (src_hi > (uint32_t) dev->vram_size) {
            src_lo = 0;
            src_hi = (uint32_t) dev->vram_size;
        }
        dev->ov0.gpu_src_lo = src_lo;
        dev->ov0.gpu_src_hi = src_hi;
        dev->ov0.gpu_ok     = rage128_gpu_ov0_compose(dev, f);
        dev->ov0.gpu_f      = *f;
        dev->ov0.gpu_w      = f->w;
        dev->ov0.gpu_h      = f->h;
        dev->ov0.gpu_frame  = dev->frame_count + 1;
    }
    if (dev->ov0.gpu_ok && (uint32_t) width == dev->ov0.gpu_w
        && (uint32_t) dda_row < dev->ov0.gpu_h)
        return rage128_gpu_ov0_row(dev, (uint32_t) dda_row);
    return NULL;
}

void
rage128_overlay_draw(svga_t *svga, int displine)
{
    rage128_t *dev   = (rage128_t *) svga->priv;
    uint32_t   scale = OV0_ACT(dev, RAGE128_OV0_SCALE_CNTL);
    int        fmt   = (scale & RAGE128_OV0_SCALER_SURFAC_FORMAT) >> RAGE128_OV0_SCALER_FORMAT_SHIFT;
    uint32_t  *line  = buffer32->line[displine];
    uint8_t   *vram  = svga->vram;
    uint32_t   vmask = dev->vram_mask;

    /* The overlay is mixed in ahead of the DAC. With the DAC powered down,
       DAC_PDWN (RRG: DAC_CNTL, p. 3-125 / PDF 143), or its inputs forced,
       DAC_FORCE_DATA_EN (RRG: DAC_EXT_CNTL, p. 3-217 / PDF 235), none of
       it reaches the screen. */
    if (dev->dac_const_on)
        return;

    /* Destination window in screen pixels, x in the low half and y in the
       high half. OV0_Y_X_END is inclusive (see rage128_ov0_update). The
       svga core has already gated the vertical range; x is clipped
       here. */
    uint32_t strt    = OV0_ACT(dev, RAGE128_OV0_Y_X_START);
    uint32_t end     = OV0_ACT(dev, RAGE128_OV0_Y_X_END);
    int      dst_x1  = strt & 0xfff;
    int      dst_x2  = (end & 0xfff) + 1;
    int      out_row = svga->displine - svga->overlay_latch.y;
    /* Row index for the vertical DDA. The drivers' V_INC settings show
       that the accumulator steps once per line the CRTC scans: the DDX
       doubles V_INC for an interlaced mode and halves it for a
       double-scanned one (R128DisplayVideo422), and the Windows 2000
       driver doubles it when CRTC_INTERLACE_EN (RRG: CRTC_GEN_CNTL,
       p. 3-61 / PDF 79) is set (RE: ati2dvaa.dll @40c67). Under interlace
       the svga core draws one field per sweep and steps displine by 2, so
       the index is the line within the field and each field starts the
       accumulator over. Under CRTC_DBL_SCAN_EN displine already counts
       physical lines. */
    int dda_row = svga->interlace ? (out_row >> 1) : out_row;

    /* Horizontal steps. OV0_H_INC holds OV0_P1_H_INC [13:0] for luma and
       OV0_P23_H_INC [29:16] for chroma, each a 2.12 number. OV0_STEP_BY
       holds OV0_P1_H_STEP_BY [2:0] and OV0_P23_H_STEP_BY [10:8]; a code
       n of 1 or more decimates the source by 2^(n - 1) ahead of the H_INC
       scaler (Multimedia supplement: OV0_H_INC, OV0_STEP_BY). The scaler
       makes one output pixel per ECP clock, and ECP_DIV sets how many
       screen pixels each one is repeated across (RRG: VCLK_ECP_CNTL,
       p. 3-95 / PDF 113), so the DDX multiplies H_INC by 2^ECP_DIV
       (R128DisplayVideo422). The model computes every screen pixel
       instead of repeating, so its .20 step per screen pixel is
       (H_INC << (STEP_BY - 1)) << (8 - ECP_DIV). At 1:1 with ECP_DIV 0
       the DDX writes H_INC 0x1000 and STEP_BY 1, which gives 0x100000,
       1.0. */
    uint32_t h_inc_reg = OV0_ACT(dev, RAGE128_OV0_H_INC);
    uint32_t step_reg  = OV0_ACT(dev, RAGE128_OV0_STEP_BY);
    uint32_t v_inc     = OV0_ACT(dev, RAGE128_OV0_V_INC); /* .20 */
    int      ecp_div   = (dev->pll_regs[RAGE128_PLL_VCLK_ECP_CNTL] >> RAGE128_ECP_DIV_SHIFT) & 3;
    uint32_t h_inc_y   = h_inc_reg & 0x3fff;
    uint32_t h_inc_c   = (h_inc_reg >> 16) & 0x3fff;
    int      step_y    = step_reg & 7;
    int      step_c    = (step_reg >> 8) & 7;
    uint32_t h_step_y, h_step_c;

    /* The two shifts fold into one count. STEP_BY 0 steps by 1, the same
       as 1 (Multimedia supplement: OV0_STEP_BY), so it is raised to 1.
       The three-bit code keeps the count at 14 or less, so the shifted
       14-bit H_INC field stays within 32 bits. Bits of either register
       outside the documented fields are reserved and take no part. */
    if (step_y < 1)
        step_y = 1;
    if (step_c < 1)
        step_c = 1;
    step_y += 8 - ecp_div - 1;
    step_c += 8 - ecp_div - 1;
    h_step_y = h_inc_y << step_y;
    h_step_c = h_inc_c << step_c;

    /* Horizontal DDA starts. The DDX loads OV0_P1_H_ACCUM_INIT and
       OV0_P23_H_ACCUM_INIT with the source-left fraction plus 2.5 plus
       half a source step (R128DisplayVideo422), the start the chip's
       four-tap filter wants. The model takes the nearest pixel instead,
       so it subtracts that bias to make output pixel 0 land on the first
       source pixel at 1:1.
       The first source pixel within the line's first octword (16 bytes)
       is OV0_P1_X_START [19:16], and the last is OV0_P1_X_END [10:0],
       counted from pixel 0 of that octword (Multimedia supplement:
       OV0_P1_X_START_END). P2 and P3 have the same start field and a
       ten-bit end field, [9:0] (Multimedia supplement:
       OV0_P2_X_START_END); the bits around these fields are reserved
       and take no part. */
    uint32_t h_acc0_y = ov0_h_accum_init(OV0_ACT(dev, RAGE128_OV0_P1_H_ACCUM_INIT), 0xf);
    uint32_t h_acc0_c = ov0_h_accum_init(OV0_ACT(dev, RAGE128_OV0_P23_H_ACCUM_INIT), 0x7);
    /* The register keeps only bits [19:11] of the DDX's 16.16 start, so
       the bias is cut to the same bits before it is subtracted. At full
       precision the bias can exceed the start recovered from the
       register, by the low bits of H_INC, and the unsigned difference
       would wrap. The floor at 0 covers a driver that programs a start
       below 2.5 plus half a step. */
    uint32_t h_bias_y = ((0x28000 + (h_inc_y << 3)) & 0xff800) << 4;
    uint32_t h_bias_c = ((0x28000 + (h_inc_c << 3)) & 0xff800) << 4;
    uint32_t h_rel0_y = (h_acc0_y > h_bias_y) ? h_acc0_y - h_bias_y : 0;
    uint32_t h_rel0_c = (h_acc0_c > h_bias_c) ? h_acc0_c - h_bias_c : 0;

    /* Planar chroma: P2 positions the Cb plane and P3 the Cr plane. The
       supplement requires the two to be programmed alike (Multimedia
       supplement: OV0_P3_X_START_END) and the DDX does so
       (R128DisplayVideo420). The model runs one chroma DDA for both and
       clamps each plane to its own end. */
    uint32_t p1_se = OV0_ACT(dev, RAGE128_OV0_P1_X_START_END);
    uint32_t p2_se = OV0_ACT(dev, RAGE128_OV0_P2_X_START_END);
    uint32_t p3_se = OV0_ACT(dev, RAGE128_OV0_P3_X_START_END);
    int      p1_x0 = (p1_se >> 16) & 0x0f;
    int      p1_xe = p1_se & 0x07ff;
    int      p2_x0 = (p2_se >> 16) & 0x0f;
    int      p2_xe = p2_se & 0x03ff;
    int      p3_x0 = (p3_se >> 16) & 0x0f;
    int      p3_xe = p3_se & 0x03ff;

    /* Vertical DDA. OV0_V_INC is a 6.12 number at [25:8], so the whole
       register reads as a .20 value. The DDX loads OV0_P1_V_ACCUM_INIT
       with the source-top fraction plus 1.5 lines (R128DisplayVideo420);
       the integer part is the number of lines the chip loads into its
       line buffers before the first output line (Multimedia supplement:
       OV0_P1_V_ACCUM_INIT). OV0_P1_ACTIVE_LINES_M1 [27:16] is the source
       height minus one. For planar 4:2:0 the model steps chroma at half
       the luma rate from the P23 registers, the way the DDX programs
       them. */
    uint32_t v_acc0_y = OV0_ACT(dev, RAGE128_OV0_P1_V_ACCUM_INIT) & 0x03ff8000;
    uint32_t v_acc0_c = OV0_ACT(dev, RAGE128_OV0_P23_V_ACCUM_INIT) & 0x01ff8000;
    int      src_h_y  = ((OV0_ACT(dev, RAGE128_OV0_P1_BLANK_LINES_AT_TOP) >> 16) & 0xfff) + 1;
    int      src_h_c  = ((OV0_ACT(dev, RAGE128_OV0_P23_BLANK_LINES_AT_TOP) >> 16) & 0x7ff) + 1;
    int      src_y, src_yc;

    /* Source buffers. Each of OV0_VID_BUF0_BASE_ADRS to
       OV0_VID_BUF5_BASE_ADRS holds in [25:4] the address of the octword
       with the first pixel, and in bit 0 a pitch select
       (OV0_VID_BUF0_PITCH_SEL for buffer 0) that picks
       OV0_VID_BUF_PITCH1_VALUE over OV0_VID_BUF_PITCH0_VALUE (Multimedia
       supplement: OV0_VID_BUF0_BASE_ADRS). The DDX sets it on the planar
       chroma buffers (R128DisplayVideo420).

       OV0_SOFT_BUF_NUM, bits [2:0] of OV0_AUTO_FLIP_CNTL, picks the
       buffer: 0 to 5 for a packed surface, 0 or 3 for a planar one, whose
       next two registers hold U and V (Multimedia supplement:
       OV0_AUTO_FLIP_CNTRL). The scaler takes the number only when its
       internal OV0_SOFT_EOF_TOGGLE changes state, at an unlocked vertical
       blank, so the compositor reads the number the last toggle change
       submitted (see ov0_take_flip), not the register. The DDX leaves
       the number 0 and rewrites the first three registers for every
       image. The Windows 2000 driver alternates between sets 0 and 3: it
       writes the new set's three registers, sets OV0_SOFT_BUF_NUM to 0
       or 3 and toggles OV0_SOFT_EOF_TOGGLE (RE: ati2dvaa.dll @23d5f and
       @23e51), so its new buffer shows whole from the next frame.
       Deinterlacing (OV0_DEINTERLACE_PATTERN, with the submitted
       OV0_SOFT_BUF_ODD) and tiled surfaces are not modeled. */
    int      buf_n = dev->ov0.flip_sel & RAGE128_OV0_SOFT_BUF_NUM;
    uint32_t buf0, buf1, buf2;

    if (buf_n > 5)
        buf_n = 5;
    buf0 = dev->ov0.active[RAGE128_OV0_REG(RAGE128_OV0_VID_BUF0_BASE_ADRS) + buf_n];
    buf1 = dev->ov0.active[RAGE128_OV0_REG(RAGE128_OV0_VID_BUF0_BASE_ADRS) + ((buf_n < 4) ? buf_n + 1 : 5)];
    buf2 = dev->ov0.active[RAGE128_OV0_REG(RAGE128_OV0_VID_BUF0_BASE_ADRS) + ((buf_n < 3) ? buf_n + 2 : 5)];
    /* OV0_VID_BUF_PITCH0_VALUE and OV0_VID_BUF_PITCH1_VALUE hold the
       pitch in bytes in bits 25:4; bits 3:0 are reserved and a pitch is
       a whole number of octwords (Multimedia supplement:
       OV0_VID_BUF_PITCH0_VALUE). The mask keeps the field at its byte
       position, so the row address math below adds it directly. */
    uint32_t pitch0 = OV0_ACT(dev, RAGE128_OV0_VID_BUF_PITCH0_VALUE) & 0x03fffff0u;
    uint32_t pitch1 = OV0_ACT(dev, RAGE128_OV0_VID_BUF_PITCH1_VALUE) & 0x03fffff0u;
    uint32_t base_y, base_u, base_v;

    /* Color adjust (Multimedia supplement: OV0_COLOUR_CNTL).
       OV0_BRIGHTNESS [6:0] is a signed offset added to Y, and
       OV0_SATURATION_U [12:8] and OV0_SATURATION_V [20:16] are 1.4 gains
       on Cb and Cr, 16 being 1.0. */
    uint32_t colour = OV0_ACT(dev, RAGE128_OV0_COLOUR_CNTL);
    int      bright = (int) ((int8_t) ((colour & 0x7f) << 1)) >> 1; /* sign-extend 7 bit */
    int      sat_u  = (colour >> 8) & 0x1f;
    int      sat_v  = (colour >> 16) & 0x1f;

    uint32_t key_cntl  = OV0_ACT(dev, RAGE128_OV0_KEY_CNTL);
    uint32_t vkey_fn   = key_cntl & RAGE128_OV0_VIDEO_KEY_FN_MASK;
    uint32_t gkey_fn   = (key_cntl & RAGE128_OV0_GRAPHIC_KEY_FN_MASK) >> RAGE128_OV0_GRAPHIC_KEY_FN_SHIFT;
    uint32_t vkey_clr  = OV0_ACT(dev, RAGE128_OV0_VIDEO_KEY_CLR);
    uint32_t vkey_msk  = OV0_ACT(dev, RAGE128_OV0_VIDEO_KEY_MSK);
    uint32_t vmask_rgb = (vkey_msk & 0xff) * 0x010101;
    int      mix_and   = !!(key_cntl & RAGE128_OV0_CMP_MIX_AND);

    int x1, x2, i;

    const uint32_t *grow       = NULL;
    int             gpu_verify = 0;
    uint64_t        vbad       = 0;

    if (out_row < 0)
        return;

    switch (fmt) {
        case RAGE128_OV0_SCALER_SOURCE_15BPP:
        case RAGE128_OV0_SCALER_SOURCE_16BPP:
        case RAGE128_OV0_SCALER_SOURCE_32BPP:
        case RAGE128_OV0_SCALER_SOURCE_YUV12:
        case RAGE128_OV0_SCALER_SOURCE_VYUY422:
        case RAGE128_OV0_SCALER_SOURCE_YVYU422:
            break;
        default:
            ov0_log_unhandled("SCALE_CNTL surface format", scale);
            return;
    }

    /* Source lines for this output line. The model takes the nearest
       line; the chip's two- and four-tap filters (OV0_FILTER_CNTL and
       OV0_FOUR_TAP_COEF_0 to OV0_FOUR_TAP_COEF_4) are not modeled.
       Subtracting 0x180000 removes the DDX's 1.5-line bias. The
       accumulator sum stays a 32-bit value, as the register is; the
       bias comes off it in signed 64-bit arithmetic, and a start below
       the bias floors at the first line instead of wrapping to a large
       line number that the upper clamp would turn into the last one.
       The model clamps to the last active line; the supplement says the
       chip shows blank lines below the active lines, and above them as
       set by OV0_P1_BLNK_LN_AT_TOP_M1 (Multimedia supplement:
       OV0_P1_BLANK_LINES_AT_TOP), which the model does not do. */
    {
        uint32_t acc_y = v_acc0_y + (uint32_t) dda_row * v_inc;
        uint32_t acc_c = v_acc0_c + (uint32_t) dda_row * (v_inc >> 1);
        int64_t  rel_y = (int64_t) acc_y - 0x180000;
        int64_t  rel_c = (int64_t) acc_c - 0x180000;

        src_y  = (int) ((rel_y < 0 ? 0 : rel_y) >> 20);
        src_yc = (int) ((rel_c < 0 ? 0 : rel_c) >> 20);
    }
    if (src_y > src_h_y - 1)
        src_y = src_h_y - 1;
    if (src_yc > src_h_c - 1)
        src_yc = src_h_c - 1;

    base_y = ((buf0 & 0xfffffff0) + (uint32_t) src_y * ((buf0 & 1) ? pitch1 : pitch0));
    base_u = ((buf1 & 0xfffffff0) + (uint32_t) src_yc * ((buf1 & 1) ? pitch1 : pitch0));
    base_v = ((buf2 & 0xfffffff0) + (uint32_t) src_yc * ((buf2 & 1) ? pitch1 : pitch0));

    /* Clip to the active display width in scan pixels. In the doubled
       320-pixel presentation (rage128_hdbl_shift) svga->hdisp holds the
       doubled width, so the clip undoes the shift and rage128_hdbl_put
       applies it when storing. */
    int sh = rage128_hdbl_shift(dev);

    x1 = dst_x1;
    x2 = dst_x2;
    if (x2 > (svga->hdisp >> sh))
        x2 = svga->hdisp >> sh;
    if (x1 < 0)
        x1 = 0;
    if (x2 <= x1)
        return;

    /* Copy the subpicture registers to the active set once per frame (see
       the subpicture block above). The subpicture is blended inside the
       overlay's pixel path; the GPU compose does not do that blend, so a
       frame with the subpicture on uses the CPU compositor. */
    if (dev->subpic.frame_stamp != dev->frame_count) {
        dev->subpic.frame_stamp = dev->frame_count;
        memcpy(dev->subpic.active, dev->subpic.regs, sizeof(dev->subpic.active));
        memcpy(dev->subpic.apal, dev->subpic.pal, sizeof(dev->subpic.apal));
    }

    int sp_on = !!(dev->subpic.active[RAGE128_SUBPIC_REG(RAGE128_SUBPIC_CNTL)]
                   & RAGE128_SUBPIC_CNTL_DISPLAY_EN);

    if (dev->gpu && !sp_on) {
        r128_ov0_frame_t f;

        f.fmt       = (uint32_t) fmt;
        f.w         = (uint32_t) (x2 - x1);
        f.h         = (uint32_t) (svga->interlace ? (svga->overlay_latch.cur_ysize + 1) >> 1
                                                  : svga->overlay_latch.cur_ysize);
        f.vram_mask = vmask;
        f.h_step_y  = h_step_y;
        f.h_step_c  = h_step_c;
        f.h_rel0_y  = h_rel0_y;
        f.h_rel0_c  = h_rel0_c;
        f.p1_x0     = (uint32_t) p1_x0;
        f.p1_xe     = (uint32_t) p1_xe;
        f.p2_x0     = (uint32_t) p2_x0;
        f.p2_xe     = (uint32_t) p2_xe;
        f.p3_x0     = (uint32_t) p3_x0;
        f.p3_xe     = (uint32_t) p3_xe;
        f.v_acc0_y  = v_acc0_y;
        f.v_acc0_c  = v_acc0_c;
        f.v_inc     = v_inc;
        f.src_h_y   = (uint32_t) src_h_y;
        f.src_h_c   = (uint32_t) src_h_c;
        f.base0     = buf0 & 0xfffffff0;
        f.pitch_y   = (buf0 & 1) ? pitch1 : pitch0;
        f.base_u    = buf1 & 0xfffffff0;
        f.pitch_u   = (buf1 & 1) ? pitch1 : pitch0;
        f.base_v    = buf2 & 0xfffffff0;
        f.pitch_v   = (buf2 & 1) ? pitch1 : pitch0;
        f.bright    = (uint32_t) bright;
        f.sat_u     = (uint32_t) sat_u;
        f.sat_v     = (uint32_t) sat_v;
        f.vkey_clr  = vkey_clr;
        f.vkey_msk  = vkey_msk;

        grow = ov0_gpu_frame_row(dev, &f, fmt, p1_xe, p2_xe, p3_xe, vmask,
                                 x2 - x1, dda_row);
    }
    gpu_verify = grow != NULL && rage128_gpu_ov0_verifying(dev);

    if (grow != NULL && !gpu_verify) {
        for (i = 0; i < x2 - x1; i++) {
            uint32_t wv  = grow[i];
            uint32_t rgb = (wv & 0xffffff)
                | ((wv & 0x1000000) ? 0xff000000u : 0);
            int gpass      = ov0_key_fn(gkey_fn, ov0_gfx_key_match(dev, x1 + i));
            int vpass      = ov0_key_fn(vkey_fn, (int) ((wv >> 25) & 1));
            int show_video = mix_and ? (gpass && vpass) : (gpass || vpass);

            if (show_video)
                rage128_hdbl_put(line + svga->x_add, sh, x1 + i, rgb);
        }
        return;
    }

    for (i = 0; i < x2 - x1; i++) {
        int      sx = p1_x0 + (int) ((h_rel0_y + (uint32_t) i * h_step_y) >> 20);
        int      hc, sxc_u, sxc_v;
        uint32_t rgb;
        uint32_t vpx = 0;
        int      y, cb, cr;

        if (sx < 0)
            sx = 0;
        if (sx > p1_xe)
            sx = p1_xe;

        switch (fmt) {
            case RAGE128_OV0_SCALER_SOURCE_YUV12:
                hc    = (int) ((h_rel0_c + (uint32_t) i * h_step_c) >> 20);
                sxc_u = p2_x0 + hc;
                sxc_v = p3_x0 + hc;
                if (sxc_u < 0)
                    sxc_u = 0;
                if (sxc_u > p2_xe)
                    sxc_u = p2_xe;
                if (sxc_v < 0)
                    sxc_v = 0;
                if (sxc_v > p3_xe)
                    sxc_v = p3_xe;
                y   = vram[(base_y + sx) & vmask];
                cb  = vram[(base_u + sxc_u) & vmask];
                cr  = vram[(base_v + sxc_v) & vmask];
                vpx = ((uint32_t) cr << 16) | ((uint32_t) cb << 8) | y;
                break;
            case RAGE128_OV0_SCALER_SOURCE_VYUY422: /* YUY2: Y0 U Y1 V */
                y   = vram[(base_y + sx * 2) & vmask];
                cb  = vram[(base_y + (sx & ~1) * 2 + 1) & vmask];
                cr  = vram[(base_y + (sx & ~1) * 2 + 3) & vmask];
                vpx = ((uint32_t) cr << 16) | ((uint32_t) cb << 8) | y;
                break;
            case RAGE128_OV0_SCALER_SOURCE_YVYU422: /* UYVY: U Y0 V Y1 */
                y   = vram[(base_y + sx * 2 + 1) & vmask];
                cb  = vram[(base_y + (sx & ~1) * 2) & vmask];
                cr  = vram[(base_y + (sx & ~1) * 2 + 2) & vmask];
                vpx = ((uint32_t) cr << 16) | ((uint32_t) cb << 8) | y;
                break;
            case RAGE128_OV0_SCALER_SOURCE_15BPP:
                {
                    uint16_t p = vram[(base_y + sx * 2) & vmask]
                        | ((uint16_t) vram[(base_y + sx * 2 + 1) & vmask] << 8);
                    vpx = p;
                    y = cb = cr = 0;
                    break;
                }
            case RAGE128_OV0_SCALER_SOURCE_16BPP:
                {
                    uint16_t p = vram[(base_y + sx * 2) & vmask]
                        | ((uint16_t) vram[(base_y + sx * 2 + 1) & vmask] << 8);
                    vpx = p;
                    y = cb = cr = 0;
                    break;
                }
            default:
                { /* 32BPP */
                    uint32_t a = (base_y + sx * 4) & vmask;
                    vpx        = vram[a] | ((uint32_t) vram[(a + 1) & vmask] << 8)
                        | ((uint32_t) vram[(a + 2) & vmask] << 16)
                        | ((uint32_t) vram[(a + 3) & vmask] << 24);
                    y = cb = cr = 0;
                    break;
                }
        }

        switch (fmt) {
            case RAGE128_OV0_SCALER_SOURCE_15BPP:
                rgb = video_15to32[vpx & 0xffff];
                break;
            case RAGE128_OV0_SCALER_SOURCE_16BPP:
                rgb = video_16to32[vpx & 0xffff];
                break;
            case RAGE128_OV0_SCALER_SOURCE_32BPP:
                rgb = vpx & 0xffffff;
                break;
            default:
                y = y + bright;
                if (y < 0)
                    y = 0;
                else if (y > 255)
                    y = 255;
                cb = 128 + (((cb - 128) * sat_u) >> 4);
                cr = 128 + (((cr - 128) * sat_v) >> 4);
                if (sp_on)
                    subpic_blend(dev, i, out_row, &y, &cb, &cr);
                rgb = ov0_yuv_to_rgb(y, cb, cr);
                break;
        }

        /* The video key compares RGB output after color adjustment and
           subpicture blending. Replicating OV0_VIDEO_KEY_MSK[7:0] across
           the three channels excludes alpha and reserved mask bits
           (Multimedia supplement: OV0_VIDEO_KEY_CLR, OV0_VIDEO_KEY_MSK). */
        int veq = ((rgb ^ vkey_clr) & vmask_rgb) == 0;

        /* R128_GPU=verify: the CPU result is the reference. Compare the
           GPU row's pixel and video-key bit with it before compositing. */
        if (gpu_verify) {
            uint32_t wv = grow[i];

            if (((wv & 0xffffff) | ((wv & 0x1000000) ? 0xff000000u : 0)) != rgb
                || (int) ((wv >> 25) & 1)
                    != veq) {
                /* Log enough per-pixel state to tell a math difference,
                   which repeats with sx or phase, from a timing one, where
                   frame content or registers changed between the frame
                   dispatch and this line. */
                static int dumped = 0;

                if (dumped < 24)
                    pclog("[r128 OV0 vdump %d] frame=%u row=%d/%d i=%d fmt=%d "
                          "cpu=%08x gpu=%08x vpx=%08x sx=%d buf0=%08x "
                          "flip=%d\n",
                          dumped++, dev->frame_count, out_row, dda_row, i, fmt, rgb,
                          wv, vpx, sx, buf0, buf_n);
                vbad++;
            }
        }

        /* A true mix result shows video over graphics (see ov0_key_fn).
           The DDX paints its clip region with the key color and sets
           OV0_KEY_CNTL to 0x50 (R128ResetVideo, R128PutImage), so video
           shows exactly where the graphics pixel equals the key. The
           video key uses the same RGB equality for composition and GPU
           verification. */
        {
            int gpass      = ov0_key_fn(gkey_fn, ov0_gfx_key_match(dev, x1 + i));
            int vpass      = ov0_key_fn(vkey_fn, veq);
            int show_video = mix_and ? (gpass && vpass) : (gpass || vpass);

            if (show_video)
                rage128_hdbl_put(line + svga->x_add, sh, x1 + i, rgb);
        }
    }
    if (vbad)
        rage128_gpu_ov0_bad(dev, vbad);
}
