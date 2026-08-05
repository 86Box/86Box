/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          GL-free ImGui rasterizer for the Qt software renderer.
 */
#include "qt_osd_raster.hpp"

#include <algorithm>
#include <cmath>

#include <QImage>

#include "imgui.h"

namespace {

struct Atlas {
    const unsigned char *pixels = nullptr; /* RGBA8, row-major */
    int                  w      = 0;
    int                  h      = 0;
};

/* The atlas lives in the ImGui font context. */
Atlas
fetch_atlas()
{
    Atlas a;
    unsigned char *px = nullptr;
    int            w  = 0;
    int            h  = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    a.pixels = px;
    a.w      = w;
    a.h      = h;
    return a;
}

inline float
edge(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

inline int
clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Blend a straight-alpha source over a premultiplied-ARGB32 destination. */
inline void
blend_pixel(uint32_t *dst, int sr, int sg, int sb, int sa)
{
    if (sa <= 0)
        return;

    /* Premultiply the source. */
    const int pr = sr * sa / 255;
    const int pg = sg * sa / 255;
    const int pb = sb * sa / 255;

    const uint32_t d   = *dst;
    const int      da  = (d >> 24) & 0xff;
    const int      dr  = (d >> 16) & 0xff;
    const int      dg  = (d >> 8) & 0xff;
    const int      db  = d & 0xff;
    const int      inv = 255 - sa;

    const int oa  = sa + da * inv / 255;
    const int orr = pr + dr * inv / 255; /* 'or' is a reserved C++ token */
    const int og  = pg + dg * inv / 255;
    const int ob  = pb + db * inv / 255;

    *dst = ((uint32_t) clampi(oa, 0, 255) << 24) | ((uint32_t) clampi(orr, 0, 255) << 16)
        | ((uint32_t) clampi(og, 0, 255) << 8) | (uint32_t) clampi(ob, 0, 255);
}

void
raster_triangle(QImage &target, const Atlas &atlas, float scale,
                const ImDrawVert &a, const ImDrawVert &b, const ImDrawVert &c,
                int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    const float ax = a.pos.x * scale, ay = a.pos.y * scale;
    const float bx = b.pos.x * scale, by = b.pos.y * scale;
    const float cx = c.pos.x * scale, cy = c.pos.y * scale;

    const float area = edge(ax, ay, bx, by, cx, cy);
    if (area == 0.0f)
        return;
    const float inv_area = 1.0f / area;

    int min_x = (int) std::floor(std::min({ ax, bx, cx }));
    int max_x = (int) std::ceil(std::max({ ax, bx, cx }));
    int min_y = (int) std::floor(std::min({ ay, by, cy }));
    int max_y = (int) std::ceil(std::max({ ay, by, cy }));

    min_x = std::max(min_x, clip_x0);
    min_y = std::max(min_y, clip_y0);
    max_x = std::min(max_x, clip_x1);
    max_y = std::min(max_y, clip_y1);
    if (min_x >= max_x || min_y >= max_y)
        return;

    const int      stride = target.bytesPerLine() / 4;
    uint32_t      *base   = reinterpret_cast<uint32_t *>(target.bits());
    const int      aw     = atlas.w;
    const int      ah     = atlas.h;

    /* Unpack vertex colours (ImU32 is 0xAABBGGRR). */
    const int ar = a.col & 0xff, ag = (a.col >> 8) & 0xff, ab = (a.col >> 16) & 0xff, aa = (a.col >> 24) & 0xff;
    const int br = b.col & 0xff, bg = (b.col >> 8) & 0xff, bb = (b.col >> 16) & 0xff, ba = (b.col >> 24) & 0xff;
    const int cr = c.col & 0xff, cg = (c.col >> 8) & 0xff, cb = (c.col >> 16) & 0xff, ca = (c.col >> 24) & 0xff;

    for (int y = min_y; y < max_y; y++) {
        uint32_t  *row    = base + (size_t) y * stride;
        const float py    = y + 0.5f;
        for (int x = min_x; x < max_x; x++) {
            const float px = x + 0.5f;

            float w0 = edge(bx, by, cx, cy, px, py) * inv_area;
            float w1 = edge(cx, cy, ax, ay, px, py) * inv_area;
            float w2 = edge(ax, ay, bx, by, px, py) * inv_area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;

            /* Interpolate colour. */
            int col_r = (int) (w0 * ar + w1 * br + w2 * cr);
            int col_g = (int) (w0 * ag + w1 * bg + w2 * cg);
            int col_b = (int) (w0 * ab + w1 * bb + w2 * cb);
            int col_a = (int) (w0 * aa + w1 * ba + w2 * ca);

            /* Sample the atlas (nearest). */
            int tr = 255, tg = 255, tb = 255, ta = 255;
            if (atlas.pixels && aw > 0 && ah > 0) {
                const float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x;
                const float v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
                const int   sx = clampi((int) (u * aw), 0, aw - 1);
                const int   sy = clampi((int) (v * ah), 0, ah - 1);
                const unsigned char *texel = atlas.pixels + ((size_t) sy * aw + sx) * 4;
                tr = texel[0];
                tg = texel[1];
                tb = texel[2];
                ta = texel[3];
            }

            blend_pixel(&row[x], col_r * tr / 255, col_g * tg / 255,
                        col_b * tb / 255, col_a * ta / 255);
        }
    }
}

} /* namespace */

void
osd_raster_render(ImDrawData *draw_data, QImage &target, float scale)
{
    if (!draw_data || target.isNull())
        return;

    const Atlas atlas = fetch_atlas();

    const int tw = target.width();
    const int th = target.height();

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *cmds  = draw_data->CmdLists[n];
        const ImDrawVert *verts = cmds->VtxBuffer.Data;
        const ImDrawIdx  *idx   = cmds->IdxBuffer.Data;

        for (int ci = 0; ci < cmds->CmdBuffer.Size; ci++) {
            const ImDrawCmd &cmd = cmds->CmdBuffer[ci];
            if (cmd.UserCallback) {
                cmd.UserCallback(cmds, &cmd);
                continue;
            }

            const int clip_x0 = clampi((int) (cmd.ClipRect.x * scale), 0, tw);
            const int clip_y0 = clampi((int) (cmd.ClipRect.y * scale), 0, th);
            const int clip_x1 = clampi((int) std::ceil(cmd.ClipRect.z * scale), 0, tw);
            const int clip_y1 = clampi((int) std::ceil(cmd.ClipRect.w * scale), 0, th);
            if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1)
                continue;

            const ImDrawIdx *tri = idx + cmd.IdxOffset;
            for (unsigned int e = 0; e + 2 < cmd.ElemCount; e += 3) {
                const ImDrawVert &a = verts[cmd.VtxOffset + tri[e + 0]];
                const ImDrawVert &b = verts[cmd.VtxOffset + tri[e + 1]];
                const ImDrawVert &c = verts[cmd.VtxOffset + tri[e + 2]];
                raster_triangle(target, atlas, scale, a, b, c,
                                clip_x0, clip_y0, clip_x1, clip_y1);
            }
        }
    }
}
