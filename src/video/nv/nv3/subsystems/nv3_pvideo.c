/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PVIDEO - Video Overlay
 *
 *          Register meanings from NV3RM.VXD's overlay code (UpdateOverlay at
 *          file 0x56AFB, its interrupt handler at 0x57719); see
 *          NV3-DRIVER-NOTES.md.
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* 0x680200-0x6802FC, as indexes into pvideo.regs, with the registers' names and field widths */
#define PV_SCALE   (0x00 >> 2) /* 0x200 STEP_SIZE: Y 27:16, X 11:0; ((src - 1) << 11) / (dst - 1) */
#define PV_CTRL_Y  (0x04 >> 2) /* 0x204 CONTROL_Y: 0 BLUR (vertical filter, else line replication), 4 LINE */
#define PV_CTRL_X  (0x08 >> 2) /* 0x208 CONTROL_X: 0 WEIGHT, 4 SHARPENING, 8 SMOOTHING */
#define PV_START0  (0x0C >> 2) /* 0x20C/0x210 BUFFn_START: 22:2 */
#define PV_START1  (0x10 >> 2)
#define PV_PITCH0  (0x14 >> 2) /* 0x214/0x218 BUFFn_PITCH: 13:4 */
#define PV_PITCH1  (0x18 >> 2)
#define PV_OFFS0   (0x1C >> 2) /* 0x21C/0x220 BUFFn_OFFSET: X 0 (first pixel of the word), Y 5:4 (off/quarter/half line) */
#define PV_OFFS1   (0x20 >> 2)
#define PV_STATUS  (0x24 >> 2) /* 0x224 OE_STATE: 24 CURRENT_BUFFER, 16+4n BUFFn_IN_USE, 8+4n BUFFn_ERROR, 4n BUFFn_INTR_NOTIFY */
#define PV_READY   (0x28 >> 2) /* 0x228 SU_STATE: 16+4n BUFFn_IN_USE, toggled by the driver when buffer n is ready */
#define PV_POINT   (0x30 >> 2) /* 0x230 WINDOW_START: Y 26:16, X 10:0 */
#define PV_SIZE    (0x34 >> 2) /* 0x234 WINDOW_SIZE: Y 26:16, X 10:0 */
#define PV_KEY     (0x40 >> 2) /* 0x240 KEY: the bits of the desktop's pixel format */
#define PV_CONTROL (0x44 >> 2) /* 0x244 OVERLAY: 0 VIDEO on, 4 KEY on, 8 FORMAT (1 YUY2, 0 CCIR = UYVY) */
#define PV_CSC_R   (0x80 >> 2) /* 0x280-0x288 RED/GREEN/BLUE_CSC: 7:0 offsets, default 105/62/137 */
#define PV_CSC_G   (0x84 >> 2)
#define PV_CSC_B   (0x88 >> 2)
#define PV_CSC_ADJ (0x8C >> 2) /* 0x28C CSC_ADJUST: B/G/R/L flags 0/4/8/12, 16 CHROMA (interpolate, else replicate) */

#define PV(r) (nv3->pvideo.regs[(r)])

/* Hand the scanout a buffer the driver has made ready: buffer n is ready while bit 16+4n of
   0x228 equals that of 0x224. The other buffer than the one shown goes first (the driver flips
   0x224 bit 24 itself when it updates the shown buffer with both idle). Taking a buffer toggles
   its bit 16+4n and its notify bit 4n in 0x224 and raises the interrupt; the driver's handler
   compares the notify bit with 0x22C, calls DirectDraw back and toggles 0x22C. The driver also
   rewrites bit 24 (to 1 after each buffer 0, re-submitting it), so the buffer scanned out is
   kept apart, in pvideo.shown. One buffer per
   vertical retrace: taking it at once let the handler's next submission raise the interrupt
   again before it returned, without end. */
void
nv3_pvideo_vblank(void)
{
    uint32_t cur  = (PV(PV_STATUS) >> 24) & 1;
    uint32_t next = cur ^ 1;
    uint32_t bit  = 1u << (16 + 4 * next);

    if ((PV(PV_READY) ^ PV(PV_STATUS)) & bit) {
        /* the other buffer is not ready; the shown one may be (updated in place) */
        bit = 1u << (16 + 4 * cur);
        if ((PV(PV_READY) ^ PV(PV_STATUS)) & bit)
            return;
        next = cur;
    }
    PV(PV_STATUS) = ((PV(PV_STATUS) & ~0x01000000) ^ bit ^ (1u << (4 * next))) | (next << 24);
    nv3->pvideo.shown = next;
    nv3->pvideo.intr |= 1;
    nv3_pmc_handle_interrupts(true);
}

/* The lines the svga core runs the overlay on (latched at the next frame) */
static void
nv3_pvideo_update(void)
{
    svga_t *svga = &nv3->nvbase.svga;

    svga->overlay.ena       = (PV(PV_CONTROL) & 1) && (PV(PV_SIZE) & 0x7FF) && ((PV(PV_SIZE) >> 16) & 0x7FF);
    svga->overlay.x         = PV(PV_POINT) & 0x7FF;
    svga->overlay.y         = (PV(PV_POINT) >> 16) & 0x7FF;
    svga->overlay.cur_xsize = PV(PV_SIZE) & 0x7FF;
    svga->overlay.cur_ysize = (PV(PV_SIZE) >> 16) & 0x7FF;
    svga->overlay.xoff      = 0;
    svga->overlay.yoff      = 0;
}

/* The colour conversion offsets have no reset value; start from the resman's defaults so
   an overlay set up without them (no resman yet) still converts sensibly */
void
nv3_pvideo_init(void)
{
    PV(PV_CSC_R)   = 105;
    PV(PV_CSC_G)   = 62;
    PV(PV_CSC_B)   = 137;
    PV(PV_CSC_ADJ) = 0x10000;
    /* the SVGA core's overlay follows the (now clear) registers: after a PCI reset it would
       otherwise keep drawing the old window over the BIOS screen */
    nv3_pvideo_update();
}

uint32_t nv3_pvideo_read(uint32_t address)
{
    switch (address)
    {
        case NV3_PVIDEO_INTR:
            return nv3->pvideo.intr;
        case NV3_PVIDEO_INTR_EN:
            return nv3->pvideo.intr_en;
        default:
            if (address >= 0x680200 && address < 0x680300)
                return nv3->pvideo.regs[(address - 0x680200) >> 2];
            return 0;
    }
}

void nv3_pvideo_write(uint32_t address, uint32_t value)
{
    switch (address)
    {
        case NV3_PVIDEO_INTR: /* bit 0: a buffer was taken */
            nv3->pvideo.intr &= ~value;
            nv3_pmc_handle_interrupts(true);
            return;
        case NV3_PVIDEO_INTR_EN:
            nv3->pvideo.intr_en = value & 0x00000001;
            nv3_pmc_handle_interrupts(true);
            return;
        default:
            break;
    }
    if (address < 0x680200 || address >= 0x680300)
        return;

    nv3->pvideo.regs[(address - 0x680200) >> 2] = value;
    switch (address) {
        case NV3_PVIDEO_FIFO_THRESHOLD:
            nv3->pvideo.fifo_threshold = ((value >> 3) & 0x0F) << 3;
            break;
        case NV3_PVIDEO_FIFO_BURST_LENGTH:
            nv3->pvideo.fifo_burst_size = value & 0x03;
            break;
        case NV3_PVIDEO_OVERLAY:
            nv3->pvideo.overlay_settings = value;
            break;
        default:
            break;
    }
    nv3_pvideo_update();
}

static inline uint32_t
nv3_pvideo_clamp(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : (uint32_t) v);
}

/* The colour conversion. The CSC registers hold the constant terms, halved: with the coefficients
   below, BT.601's constants come out as R -2 x 105, G +2 x 62, B -2 x 137 -- the defaults the
   resman writes. NV3DD32's brightness control adds an
   offset to red and blue and takes it from green, which darkens all three alike, as this does. The
   CSC_ADJUST coefficient flags are not documented and stay at 0. */
static inline uint32_t
nv3_pvideo_rgb(int Y, int U, int V)
{
    int y = 298 * Y;                                 /* 1.164 Y, in 1/256 */
    int r = (y + 384 * V) / 256 - 2 * (int) (PV(PV_CSC_R) & 0xFF);
    int g = (y - 96 * U - 192 * V) / 256 + 2 * (int) (PV(PV_CSC_G) & 0xFF);
    int b = (y + 512 * U) / 256 - 2 * (int) (PV(PV_CSC_B) & 0xFF);

    return (nv3_pvideo_clamp(r) << 16) | (nv3_pvideo_clamp(g) << 8) | nv3_pvideo_clamp(b);
}

/* One screen line of the overlay: 4:2:2 from the shown buffer, filtered in x, in y while
   CONTROL_Y BLUR is on (else lines are replicated), chroma interpolated while CSC_ADJUST CHROMA is
   on (else each pair's U and V are replicated), converted, drawn where the primary surface holds
   the key colour if keying is on */
void
nv3_pvideo_draw(svga_t *svga, int displine)
{
    uint32_t buf    = nv3->pvideo.shown;
    uint32_t start  = PV(buf ? PV_START1 : PV_START0) & 0x7FFFFC;
    uint32_t pitch  = PV(buf ? PV_PITCH1 : PV_PITCH0) & 0x3FF0;
    uint32_t offs   = PV(buf ? PV_OFFS1 : PV_OFFS0);
    uint32_t xstep  = PV(PV_SCALE) & 0xFFF;
    uint32_t ystep  = (PV(PV_SCALE) >> 16) & 0xFFF;
    bool     yuy2   = (PV(PV_CONTROL) >> 8) & 1;
    bool     keyed  = (PV(PV_CONTROL) >> 4) & 1;
    bool     blur   = PV(PV_CTRL_Y) & 1;
    bool     chroma = (PV(PV_CSC_ADJ) >> 16) & 1;
    int      ox     = svga->overlay_latch.x;
    int      sy     = displine - svga->y_add;
    int      line   = sy - svga->overlay_latch.y;
    int      w      = svga->overlay_latch.cur_xsize;
    uint32_t mask   = svga->vram_mask;
    int      bpp    = svga->bpp;
    int      bytes  = (bpp + 7) >> 3;
    uint32_t kmask  = (bpp == 8) ? 0xFF : ((bpp == 15) ? 0x7FFF : ((bpp == 16) ? 0xFFFF : 0xFFFFFF));
    uint32_t fb     = (svga->memaddr_latch << 2) + (uint32_t) sy * (svga->rowoffset << 3);

    if (line < 0)
        return;
    if (ox + w > svga->hdisp)
        w = svga->hdisp - ox;

    /* the odd field of an interlaced source starts a quarter or half line down ("bob") */
    uint32_t  yf   = (uint32_t) line * ystep + ((((offs >> 4) & 3) == 1) ? 512 : ((((offs >> 4) & 3) == 2) ? 1024 : 0));
    uint32_t  row0 = start + (yf >> 11) * pitch;
    int       fy   = blur ? ((yf >> 3) & 0xFF) : 0;
    uint32_t *p    = &(buffer32->line[displine])[svga->x_add];

    for (int x = (ox < 0) ? -ox : 0; x < w; x++) {
        int sx = ox + x;

        if (keyed) {
            uint32_t a = fb + (uint32_t) sx * bytes;
            uint32_t pix;
            if (bytes == 1)
                pix = svga->vram[a & mask];
            else if (bytes == 2)
                pix = *(uint16_t *) &svga->vram[a & mask & ~1];
            else
                pix = *(uint32_t *) &svga->vram[a & mask & ~3];
            if ((pix & kmask) != (PV(PV_KEY) & kmask))
                continue;
        }

        uint32_t xf = (uint32_t) x * xstep + ((offs & 1) << 11);
        uint32_t px = xf >> 11;
        int      fx = (xf >> 3) & 0xFF;
        int      c[2][3];

        /* Y of source pixels px and px+1 and U, V of their pairs, on two source lines */
        for (int l = 0; l < (fy ? 2 : 1); l++) {
            uint32_t row = row0 + (l ? pitch : 0);
            int      yv[2], uv[2], vv[2];
            for (int k = 0; k < 2; k++) {
                uint32_t q = px + k;
                uint8_t *m = &svga->vram[(row + (q & ~1u) * 2) & mask & ~3];
                if (yuy2) {
                    yv[k] = m[(q & 1) ? 2 : 0];
                    uv[k] = m[1];
                    vv[k] = m[3];
                } else {
                    yv[k] = m[(q & 1) ? 3 : 1];
                    uv[k] = m[0];
                    vv[k] = m[2];
                }
            }
            int fc  = chroma ? fx : 0;
            c[l][0] = (yv[0] * (256 - fx) + yv[1] * fx) >> 8;
            c[l][1] = (uv[0] * (256 - fc) + uv[1] * fc) >> 8;
            c[l][2] = (vv[0] * (256 - fc) + vv[1] * fc) >> 8;
        }
        if (!fy)
            memcpy(c[1], c[0], sizeof(c[0]));
        int Y = (c[0][0] * (256 - fy) + c[1][0] * fy) >> 8;
        int U = (c[0][1] * (256 - fy) + c[1][1] * fy) >> 8;
        int V = (c[0][2] * (256 - fy) + c[1][2] * fy) >> 8;

        p[sx] = nv3_pvideo_rgb(Y, U, V);
    }
}
