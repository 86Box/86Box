/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PVIDEO, the video overlay.
 *
 *          The same unit as the RIVA 128's, at the same addresses with the
 *          same fields (nv3_pvideo.c, where the behaviour was worked out
 *          from that chip's resman): two YUY2/UYVY buffers handed over at
 *          vertical blank with an interrupt, scaled and filtered into a
 *          window, colour-keyed against the primary surface. The buffer
 *          start reaches the TNT2's 32 MB.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *          Connor Hyde, <mario64crashed@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

#define NV4_PVIDEO_INTR    0x680100 /* 0: a buffer was taken */
#define NV4_PVIDEO_INTR_EN 0x680140

/* 0x680200-0x6802FC, as indexes into pvideo_regs */
#define PV_SCALE   (0x200 >> 2) /* STEP_SIZE: Y 27:16, X 11:0; ((src - 1) << 11) / (dst - 1) */
#define PV_CTRL_Y  (0x204 >> 2) /* CONTROL_Y: 0 BLUR (vertical filter, else line replication), 4 LINE */
#define PV_CTRL_X  (0x208 >> 2) /* CONTROL_X: 0 WEIGHT, 4 SHARPENING, 8 SMOOTHING */
#define PV_START0  (0x20C >> 2) /* BUFFn_START */
#define PV_START1  (0x210 >> 2)
#define PV_PITCH0  (0x214 >> 2) /* BUFFn_PITCH: 13:4 */
#define PV_PITCH1  (0x218 >> 2)
#define PV_OFFS0   (0x21C >> 2) /* BUFFn_OFFSET: X 0 (first pixel of the word), Y 5:4 (off/quarter/half line) */
#define PV_OFFS1   (0x220 >> 2)
#define PV_STATUS  (0x224 >> 2) /* OE_STATE: 24 CURRENT_BUFFER, 16+4n BUFFn_IN_USE, 8+4n BUFFn_ERROR, 4n BUFFn_INTR_NOTIFY */
#define PV_READY   (0x228 >> 2) /* SU_STATE: 16+4n BUFFn_IN_USE, toggled by the driver when buffer n is ready */
#define PV_POINT   (0x230 >> 2) /* WINDOW_START: Y 26:16, X 10:0 */
#define PV_SIZE    (0x234 >> 2) /* WINDOW_SIZE: Y 26:16, X 10:0 */
#define PV_KEY     (0x240 >> 2) /* KEY: the bits of the desktop's pixel format */
#define PV_CONTROL (0x244 >> 2) /* OVERLAY: 0 VIDEO on, 4 KEY on, 8 FORMAT (1 YUY2, 0 CCIR = UYVY) */
#define PV_CSC_R   (0x280 >> 2) /* RED/GREEN/BLUE_CSC: 7:0 offsets, default 105/62/137 */
#define PV_CSC_G   (0x284 >> 2)
#define PV_CSC_B   (0x288 >> 2)
#define PV_CSC_ADJ (0x28C >> 2) /* CSC_ADJUST: B/G/R/L flags 0/4/8/12, 16 CHROMA (interpolate, else replicate) */

#define PV(r) (nv4->pvideo_regs[(r)])

/* The lines the svga core runs the overlay on (latched at the next frame) */
static void
nv4_pvideo_update(nv4_t *nv4)
{
    svga_t *svga = &nv4->svga;

    svga->overlay.ena       = (PV(PV_CONTROL) & 1) && (PV(PV_SIZE) & 0x7FF) && ((PV(PV_SIZE) >> 16) & 0x7FF);
    svga->overlay.x         = PV(PV_POINT) & 0x7FF;
    svga->overlay.y         = (PV(PV_POINT) >> 16) & 0x7FF;
    svga->overlay.cur_xsize = PV(PV_SIZE) & 0x7FF;
    svga->overlay.cur_ysize = (PV(PV_SIZE) >> 16) & 0x7FF;
    svga->overlay.xoff      = 0;
    svga->overlay.yoff      = 0;
}

/* The colour conversion offsets have no reset value; start from the
   resman's defaults so an overlay set up without them still converts
   sensibly. The svga core's overlay follows the (now clear) registers. */
void
nv4_pvideo_reset(nv4_t *nv4)
{
    memset(nv4->pvideo_regs, 0, sizeof(nv4->pvideo_regs));
    nv4->pvideo_intr    = 0;
    nv4->pvideo_intr_en = 0;
    nv4->pvideo_shown   = 0;
    PV(PV_CSC_R)        = 105;
    PV(PV_CSC_G)        = 62;
    PV(PV_CSC_B)        = 137;
    PV(PV_CSC_ADJ)      = 0x10000;
    nv4_pvideo_update(nv4);
}

uint32_t
nv4_pvideo_pending(nv4_t *nv4)
{
    return nv4->pvideo_intr & nv4->pvideo_intr_en;
}

/* Hand the scanout a buffer the driver has made ready: buffer n is ready
   while bit 16+4n of SU_STATE equals that of OE_STATE. The other buffer
   than the one shown goes first. Taking a buffer toggles its bit 16+4n
   and its notify bit 4n in OE_STATE and raises the interrupt; the
   driver's handler compares the notify bit with RM_STATE, calls
   DirectDraw back and toggles RM_STATE. The driver also rewrites bit 24,
   so the buffer scanned out is kept apart, in pvideo_shown. One buffer
   per vertical retrace: taking it at once lets the handler's next
   submission raise the interrupt again before it returns, without end. */
void
nv4_pvideo_vblank(nv4_t *nv4)
{
    uint32_t cur  = (PV(PV_STATUS) >> 24) & 1;
    uint32_t next = cur ^ 1;
    uint32_t bit  = 1u << (16 + 4 * next);

    if (!(PV(PV_CONTROL) & 1))
        return;
    if ((PV(PV_READY) ^ PV(PV_STATUS)) & bit) {
        /* the other buffer is not ready; the shown one may be (updated in place) */
        bit = 1u << (16 + 4 * cur);
        if ((PV(PV_READY) ^ PV(PV_STATUS)) & bit)
            return;
        next = cur;
    }
    PV(PV_STATUS)     = ((PV(PV_STATUS) & ~0x01000000) ^ bit ^ (1u << (4 * next))) | (next << 24);
    nv4->pvideo_shown = next;
    nv4->pvideo_intr |= 1;
    nv4_pmc_update_irq(nv4);
}

uint32_t
nv4_pvideo_read(nv4_t *nv4, uint32_t addr)
{
    switch (addr) {
        case NV4_PVIDEO_INTR:
            return nv4->pvideo_intr;
        case NV4_PVIDEO_INTR_EN:
            return nv4->pvideo_intr_en;
        default:
            if (addr >= 0x680200)
                return nv4->pvideo_regs[((addr - NV4_PVIDEO_START) >> 2) % (sizeof(nv4->pvideo_regs) / 4)];
            return 0;
    }
}

void
nv4_pvideo_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case NV4_PVIDEO_INTR:
            nv4->pvideo_intr &= ~val;
            nv4_pmc_update_irq(nv4);
            return;
        case NV4_PVIDEO_INTR_EN:
            nv4->pvideo_intr_en = val & 1;
            nv4_pmc_update_irq(nv4);
            return;
        default:
            break;
    }
    if (addr < 0x680200)
        return;
    nv4->pvideo_regs[((addr - NV4_PVIDEO_START) >> 2) % (sizeof(nv4->pvideo_regs) / 4)] = val;
    nv4_pvideo_update(nv4);
}

static inline uint32_t
nv4_pvideo_clamp(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : (uint32_t) v);
}

/* The colour conversion. The CSC registers hold the constant terms,
   halved: with the coefficients below, BT.601's constants come out as
   R -2 x 105, G +2 x 62, B -2 x 137 -- the resman's defaults. The
   driver's brightness control adds an offset to red and blue and takes
   it from green, which darkens all three alike, as this does. The
   CSC_ADJUST coefficient flags are not documented and stay at 0. */
static inline uint32_t
nv4_pvideo_rgb(const nv4_t *nv4, int Y, int U, int V)
{
    int y = 298 * Y; /* 1.164 Y, in 1/256 */
    int r = (y + 384 * V) / 256 - 2 * (int) (PV(PV_CSC_R) & 0xFF);
    int g = (y - 96 * U - 192 * V) / 256 + 2 * (int) (PV(PV_CSC_G) & 0xFF);
    int b = (y + 512 * U) / 256 - 2 * (int) (PV(PV_CSC_B) & 0xFF);

    return (nv4_pvideo_clamp(r) << 16) | (nv4_pvideo_clamp(g) << 8) | nv4_pvideo_clamp(b);
}

/* One screen line of the overlay: 4:2:2 from the shown buffer, filtered
   in x, in y while CONTROL_Y BLUR is on (else lines are replicated),
   chroma interpolated while CSC_ADJUST CHROMA is on (else each pair's U
   and V are replicated), converted, drawn where the primary surface
   holds the key colour if keying is on. */
void
nv4_pvideo_draw(svga_t *svga, int displine)
{
    nv4_t   *nv4    = (nv4_t *) svga->priv;
    uint32_t buf    = nv4->pvideo_shown;
    uint32_t start  = PV(buf ? PV_START1 : PV_START0) & 0x1FFFFFC;
    uint32_t pitch  = PV(buf ? PV_PITCH1 : PV_PITCH0) & 0x3FF0;
    uint32_t offs   = PV(buf ? PV_OFFS1 : PV_OFFS0);
    uint32_t xstep  = PV(PV_SCALE) & 0xFFF;
    uint32_t ystep  = (PV(PV_SCALE) >> 16) & 0xFFF;
    int      yuy2   = (PV(PV_CONTROL) >> 8) & 1;
    int      keyed  = (PV(PV_CONTROL) >> 4) & 1;
    int      blur   = PV(PV_CTRL_Y) & 1;
    int      chroma = (PV(PV_CSC_ADJ) >> 16) & 1;
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
                uint32_t       q = px + k;
                const uint8_t *m = &svga->vram[(row + (q & ~1u) * 2) & mask & ~3];

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

        p[sx] = nv4_pvideo_rgb(nv4, Y, U, V);
    }
}
