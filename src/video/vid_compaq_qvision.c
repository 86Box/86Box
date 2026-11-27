/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compaq QVision 1024/E (CPQ3011) and 1024/I (CPQ3021), 1 MiB, single controller.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/eisa.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

/* The ISA variant currently reuses the available 1024/E ROM dump. */
#define QVISION_ROM_EVEN "roms/video/compaq_qvision/TMS27PC256@PLCC32_U40.bin"
#define QVISION_ROM_ODD  "roms/video/compaq_qvision/TMS27PC256@PLCC32_U21.bin"

typedef struct {
    bool     active, mono, transparent, primary_only, rop_enabled, invert, packed4;
    uint32_t dest, pitch, width, height, row, bit;
    uint8_t  align, foreground, background, mask, rop, rotate;
} qvision_host_t;

typedef struct {
    svga_t         svga;
    rom_t          bios;
    mem_mapping_t  highmap;
    qvision_host_t host;
    uint8_t        bios_data[0x10000], bios_vga[0x8000];
    uint8_t        registers[65536];
    uint8_t        board_control, setup, select, video_enable, option_select;
    uint8_t        pattern[8], pattern_count, slot;
    uint32_t       high_base;
} qvision_t;

static const uint16_t qvision_ports[] = {
    0x0102, 0x13c0, 0x23c0, 0x33c0, 0x43c0, 0x46e8,
    0x53c0, 0x63c0, 0x73c0, 0x83c0, 0x93c0
};

static bool
qvision_selected(qvision_t *s)
{
    return (s->board_control & 1) && (s->select & 15) == (s->setup & 15);
}

static bool
qvision_vga_enabled(qvision_t *s)
{
    return qvision_selected(s) && (s->video_enable & 8) && (s->option_select & 1);
}

static bool
qvision_packed(qvision_t *s)
{
    return s->svga.gdcreg[0x40] & 1;
}

static void
qvision_dirty(qvision_t *s, uint32_t start, uint32_t size)
{
    for (uint32_t p = start >> 12; p <= ((start + size - 1) >> 12); p++)
        s->svga.changedvram[p & 255] = changeframecount;
}

static void
qvision_update_rom(qvision_t *s)
{
    /* C85.4 selects the extended ROM; the AVGA-only view leaves C6000-C67FF open. */
    s->bios.rom = (s->setup & 0x10) ? s->bios_data + 0x8000 : s->bios_vga;
    mem_mapping_set_exec(&s->bios.mapping, s->bios.rom);
}

static void
qvision_update_map(qvision_t *s)
{
    uint32_t base = ((uint32_t) s->svga.gdcreg[0x48] << 20) | ((uint32_t) (s->svga.gdcreg[0x49] & 15) << 28);
    /* VGA output uses the RAMDAC's separate 8-bit input port. */
    bt48x_set_port_select(s->svga.ramdac, qvision_packed(s), &s->svga);
    mem_mapping_disable(&s->highmap);
    s->high_base = base;
    if (s->board_control & 1)
        mem_mapping_enable(&s->bios.mapping);
    else
        mem_mapping_disable(&s->bios.mapping);
    if (qvision_vga_enabled(s)) {
        mem_mapping_enable(&s->svga.mapping);
        if (base >= (1U << 20)) {
            mem_mapping_set_addr(&s->highmap, base, 1U << 20);
            mem_mapping_enable(&s->highmap);
        }
    } else
        mem_mapping_disable(&s->svga.mapping);
}

static void
qvision_render_4bpp(svga_t *svga)
{
    uint8_t packed_4bpp = svga->packed_4bpp;
    /* The RAMDAC's packed pixel port bypasses the VGA attribute palette. */
    svga->packed_4bpp = 1;
    svga_render_8bpp_highres(svga);
    svga->packed_4bpp = packed_4bpp;
}

static void
qvision_recalctimings(svga_t *svga)
{
    qvision_t *s = svga->priv;
    if (qvision_packed(s)) {
        svga->memaddr_latch |= (svga->gdcreg[0x42] & 0x0c) << 14;
        svga->rowoffset |= (svga->gdcreg[0x42] & 3) << 8;
        if (!svga->rowoffset)
            svga->rowoffset = 0x100;
        svga->split  = 0x7fff;
        svga->lowres = 0;
        switch (svga->bpp) {
            case 4:
                svga->render = qvision_render_4bpp;
                break;
            case 8:
                svga->render = svga_render_8bpp_highres;
                break;
            case 15:
                svga->render = svga_render_15bpp_highres;
                break;
            case 16:
                svga->render = svga_render_16bpp_highres;
                break;
            default:
                svga->render = svga_render_32bpp_highres;
                break;
        }
        bt48x_recalctimings(svga->ramdac, svga);
    }
}

static uint32_t
qvision_engine_reg(qvision_t *s, unsigned port, unsigned size)
{
    uint32_t value = 0;

    for (unsigned i = 0; i < size; i++)
        value |= (uint32_t) s->registers[port + i] << (i * 8);
    return value;
}

static uint8_t
qvision_rop(unsigned rop, uint8_t source, uint8_t dest)
{
    return ((rop & 1) ? ~source & ~dest : 0) | ((rop & 2) ? ~source & dest : 0) | ((rop & 4) ? source & ~dest : 0) | ((rop & 8) ? source & dest : 0);
}

static int
qvision_engine_pitch(qvision_t *s, unsigned port, bool xy)
{
    /* XY commands use the bitmap layout, independently of the linear delta. */
    if (xy)
        return (s->registers[0x63cb] & 0x0c) == 8 ? 2048 : 1024;
    return (int16_t) qvision_engine_reg(s, port, 2) * ((s->registers[0x63ca] & 7) == 1 ? 8 : 4);
}

static uint8_t
qvision_pixel_read(qvision_t *s, uint32_t address, bool packed4)
{
    uint8_t value = s->svga.vram[(packed4 ? address >> 1 : address) & (s->svga.vram_max - 1)];
    return packed4 ? (value >> ((address & 1) ? 0 : 4)) & 15 : value;
}

static void
qvision_pixel_write(qvision_t *s, uint32_t address, uint8_t value, bool packed4)
{
    uint32_t offset = (packed4 ? address >> 1 : address) & (s->svga.vram_max - 1);
    if (packed4) {
        unsigned shift       = (address & 1) ? 0 : 4;
        s->svga.vram[offset] = (s->svga.vram[offset] & ~(15 << shift)) | ((value & 15) << shift);
    } else
        s->svga.vram[offset] = value;
}

/*
 * Synchronous pattern subset of the Triton datapath. Unsupported
 * sources/masks remain unexecuted and are reported at command submission.
 */
static bool
qvision_pattern_blt(qvision_t *s, uint8_t command)
{
    svga_t  *vga      = &s->svga;
    uint8_t  datapath = vga->gdcreg[0x5a], color = s->pattern[0];
    uint8_t  control   = s->registers[0x63ca];
    bool     packed4   = (control & 7) == 1;
    unsigned width     = qvision_engine_reg(s, 0x23c2, 2);
    unsigned height    = qvision_engine_reg(s, 0x23c4, 2);
    int      pitch     = qvision_engine_pitch(s, 0x23ce, s->registers[0x33cf] & 0x80);
    uint32_t start     = qvision_engine_reg(s, 0x63cc, 4);
    int      direction = command & 0x40 ? -1 : 1;
    uint8_t  mask      = (datapath & 4) ? 0xff : vga->gdcreg[8];
    unsigned phase     = qvision_engine_reg(s, 0x63c0, 4) & 63;
    bool     xy8       = (s->registers[0x33cf] & 0x80) && (control & 7) == 3;
    unsigned pattern_x = xy8 ? start & 0xffff : phase;
    unsigned pattern_y = xy8 ? start >> 16 : phase >> 3;

    if (!qvision_vga_enabled(s) || !qvision_packed(s) || ((control & ~0x10) != 1 && (control & ~0x10) != 3 && (control & ~0x10) != 5 && (control & ~0x10) != 7 && control != 0x1b) || !(vga->gdcreg[0x10] & 8) || (datapath & 0x23) != 2 || (datapath & 0x0c) > 4 || vga->seqregs[2] != 0xff || (datapath & 0xc0) == 0x80 || (s->pattern_count && s->pattern_count != 8) || (command & ~0x41) || (uint64_t) width * height > (16U << 20)) {
        return false;
    }
    if (s->registers[0x33cf] & 0x80) {
        /* XY destination in bitmap byte coordinates. */
        start = (start >> 16) * pitch + (start & 0xffff);
        if (command & 0x40)
            pitch = -pitch;
    } else if (packed4)
        start >>= 2;
    else if ((control & 7) != 3)
        /* Linear high-color addresses are in bits; widths remain in bytes. */
        start >>= 3;
    for (unsigned y = 0; y < height; y++) {
        uint32_t row = start + y * pitch;

        for (unsigned x = 0; x < width; x++) {
            uint32_t offset  = row + direction * x;
            uint8_t  dest    = qvision_pixel_read(s, offset, packed4);
            bool     primary = true;
            if (s->registers[0x63ca] & 0x10) {
                /* Eight-bit XY brushes retain their screen phase across clips. */
                unsigned px  = pattern_x + (xy8 ? direction : 1) * x;
                unsigned py  = pattern_y + (xy8 ? direction : 1) * y;
                unsigned bit = ((s->pattern[py & 7] >> (7 - (px & 7))) & 1) ^ (control == 0x1b);
                primary      = bit;
                if ((datapath & 0x10) && !bit)
                    continue;
                color = vga->gdcreg[bit ? 0x43 : 0x44];
            } else if (packed4) {
                unsigned pixel = phase + x;
                color          = (s->pattern[(pixel >> 1) & 7] >> ((pixel & 1) ? 4 : 0)) & 15;
            } else
                color = s->pattern[(xy8 ? pattern_x + direction * x : x) & 7];
            uint8_t value = (datapath & 0xc0) && ((datapath & 0xc0) != 0x40 || primary) ? qvision_rop(s->registers[0x33c7], color, dest) : color;

            qvision_pixel_write(s, offset, (dest & ~mask) | (value & mask), packed4);
        }
    }
    qvision_dirty(s, 0, vga->vram_max);
    s->registers[0x33ce] &= ~1;
    return true;
}

static bool
qvision_screen_blt(qvision_t *s, uint8_t command)
{
    svga_t  *vga          = &s->svga;
    uint8_t  datapath     = vga->gdcreg[0x5a];
    uint8_t  control      = s->registers[0x63ca];
    bool     packed4      = (control & 7) == 1;
    uint32_t source       = qvision_engine_reg(s, 0x63c0, 4);
    uint32_t dest         = qvision_engine_reg(s, 0x63cc, 4);
    int      source_pitch = qvision_engine_pitch(s, 0x23ca, s->registers[0x33cf] & 0x40);
    int      dest_pitch   = qvision_engine_pitch(s, 0x23ce, s->registers[0x33cf] & 0x80);
    unsigned width        = qvision_engine_reg(s, 0x23c2, 2);
    unsigned height       = qvision_engine_reg(s, 0x23c4, 2);
    int      direction    = command & 0x40 ? -1 : 1;
    uint8_t  mask         = datapath & 4 ? 0xff : vga->gdcreg[8];

    if (!qvision_vga_enabled(s) || !qvision_packed(s) || (vga->gdcreg[0x10] & 0x48) != 8 || vga->seqregs[2] != 0xff || (control != 1 && control != 3 && control != 5 && control != 7) || (datapath & 0x33) != 1 || (datapath & 0x0c) > 4 || ((datapath & 0xc0) != 0 && (datapath & 0xc0) != 0xc0) || (command & ~0xc1) || (uint64_t) width * height > (16U << 20)) {
        return false;
    }
    if (s->registers[0x33cf] & 0x40) {
        source = (source >> 16) * source_pitch + (source & 0xffff);
        if (command & 0x40)
            source_pitch = -source_pitch;
    } else if (packed4)
        source >>= 2;
    else if (control != 3)
        source >>= 3;
    if (s->registers[0x33cf] & 0x80) {
        dest = (dest >> 16) * dest_pitch + (dest & 0xffff);
        if (command & 0x40)
            dest_pitch = -dest_pitch;
    } else if (packed4)
        dest >>= 2;
    else if (control != 3)
        dest >>= 3;
    for (unsigned y = 0; y < height; y++) {
        uint32_t src_row = source + y * source_pitch;
        uint32_t dst_row = dest + y * dest_pitch;

        for (unsigned x = 0; x < width; x++) {
            uint32_t src   = src_row + direction * x;
            uint32_t dst   = dst_row + direction * x;
            uint8_t  color = qvision_pixel_read(s, src, packed4);
            uint8_t  old   = qvision_pixel_read(s, dst, packed4);

            if (datapath & 0xc0) {
                color = qvision_rop(s->registers[0x33c7], color, old);
            }
            qvision_pixel_write(s, dst, (old & ~mask) | (color & mask), packed4);
        }
    }
    qvision_dirty(s, 0, vga->vram_max);
    s->registers[0x33ce] &= ~1;
    return true;
}

static bool
qvision_screen_mono_blt(qvision_t *s, uint8_t command)
{
    svga_t  *vga          = &s->svga;
    uint8_t  datapath     = vga->gdcreg[0x5a];
    uint8_t  control      = s->registers[0x63ca];
    uint32_t source       = qvision_engine_reg(s, 0x63c0, 4);
    uint32_t dest         = qvision_engine_reg(s, 0x63cc, 4);
    bool     dest_xy      = s->registers[0x33cf] & 0x80;
    int      source_pitch = qvision_engine_pitch(s, 0x23ca, false);
    int      dest_pitch   = qvision_engine_pitch(s, 0x23ce, dest_xy);
    unsigned width        = qvision_engine_reg(s, 0x23c2, 2);
    unsigned height       = qvision_engine_reg(s, 0x23c4, 2);
    uint8_t  mask         = datapath & 4 ? 0xff : vga->gdcreg[8];

    if (!qvision_vga_enabled(s) || !qvision_packed(s) || (vga->gdcreg[0x10] & 0x48) != 8 || vga->seqregs[2] != 0xff || (control != 0x13 && control != 0x1b) || command != 1 || (datapath & 3) != 1 || (datapath & 0x30) > 0x10 || (datapath & 0x0c) > 4 || (datapath & 0xc0) == 0x80 || (s->registers[0x33cf] & 0x40) || (!dest_xy && (dest & 7)) || (uint64_t) width * height > (16U << 20))
        return false;

    /* Cached monochrome sources and linear destinations use bit addresses. */
    dest = dest_xy ? (dest >> 16) * dest_pitch + (dest & 0xffff) : dest >> 3;
    for (unsigned y = 0; y < height; y++) {
        uint32_t src_row = source + y * source_pitch * 8;
        uint32_t dst_row = dest + y * dest_pitch;

        for (unsigned x = 0; x < width; x++) {
            uint32_t bit_address = src_row + x;
            unsigned bit         = ((qvision_pixel_read(s, bit_address >> 3, false) >> (7 - (bit_address & 7))) & 1) ^ (control == 0x1b);
            if ((datapath & 0x10) && !bit)
                continue;
            uint32_t address = dst_row + x;
            uint8_t  old     = qvision_pixel_read(s, address, false);
            uint8_t  color   = vga->gdcreg[bit ? 0x43 : 0x44];
            if ((datapath & 0xc0) && ((datapath & 0xc0) != 0x40 || bit))
                color = qvision_rop(s->registers[0x33c7], color, old);
            qvision_pixel_write(s, address, (old & ~mask) | (color & mask), false);
        }
    }
    qvision_dirty(s, 0, vga->vram_max);
    s->registers[0x33ce] &= ~1;
    return true;
}

static void
qvision_host_stop(qvision_t *s)
{
    s->host.active = false;
    s->registers[0x33ce] &= ~1;
    qvision_update_map(s);
}

static bool
qvision_host_start(qvision_t *s, uint8_t command)
{
    svga_t         *vga      = &s->svga;
    qvision_host_t *host     = &s->host;
    uint8_t         datapath = vga->gdcreg[0x5a];
    uint8_t         control  = s->registers[0x63ca];
    bool            packed4  = (control & 7) == 1;
    bool            plane    = control == 9;
    unsigned        width    = qvision_engine_reg(s, 0x23c2, 2);
    unsigned        height   = qvision_engine_reg(s, 0x23c4, 2);

    if (!qvision_vga_enabled(s) || !qvision_packed(s) || (vga->gdcreg[0x10] & 0x48) != 8 || vga->seqregs[2] != 0xff || (control != 9 && control != 0x11 && control != 0x19 && control != 3 && control != 0x13 && control != 0x1b) || command != 1 || (control == 3 && (datapath & 0x30)) || (datapath & 3) != 0 || (datapath & 0x0c) > 4 || (datapath & 0x30) > 0x10 || (datapath & 0xc0) == 0x80 || (uint64_t) width * height > (16U << 20)) {
        return false;
    }
    *host = (qvision_host_t) {
        .active = width && height,
        .mono   = plane || !!(control & 0x10),
        /* Complement mono source bits before foreground/background expansion. */
        .invert       = !plane && !!(control & 8),
        .packed4      = packed4,
        .rotate       = packed4 ? vga->gdcreg[3] & 7 : 0,
        .dest         = qvision_engine_reg(s, 0x63cc, 4),
        .pitch        = qvision_engine_pitch(s, 0x23ce, s->registers[0x33cf] & 0x80),
        .width        = width,
        .height       = height,
        .align        = s->registers[0x63c0] & ((plane || (control & 0x10)) ? 7 : 3),
        .foreground   = plane ? 15 : vga->gdcreg[0x43],
        .background   = plane ? 0 : vga->gdcreg[0x44],
        .mask         = (datapath & 4) ? 0xff : vga->gdcreg[8],
        .rop          = s->registers[0x33c7],
        .rop_enabled  = datapath & 0xc0,
        .primary_only = (datapath & 0xc0) == 0x40,
        .transparent  = datapath & 0x10,
    };
    if (s->registers[0x33cf] & 0x80) {
        host->dest = (host->dest >> 16) * host->pitch + (host->dest & 0xffff);
    } else if (packed4)
        host->dest >>= 2;
    if (!host->active) {
        s->registers[0x33ce] &= ~1;
    }
    qvision_update_map(s);
    return true;
}

static void
qvision_host_pixel(qvision_t *s, uint8_t color, bool primary)
{
    qvision_host_t *host   = &s->host;
    uint32_t        offset = host->dest + host->row * host->pitch + host->bit - host->align;
    uint8_t         dest   = qvision_pixel_read(s, offset, host->packed4);

    if (host->rop_enabled && (!host->primary_only || primary)) {
        color = qvision_rop(host->rop, color, dest);
    }
    qvision_pixel_write(s, offset, (dest & ~host->mask) | (color & host->mask), host->packed4);
    qvision_dirty(s, host->packed4 ? offset >> 1 : offset, 1);
}

static void
qvision_host_write(qvision_t *s, uint32_t addr,
                   uint64_t value, unsigned size)
{
    qvision_host_t *host = &s->host;

    unsigned row_end = host->align + host->width;

    for (unsigned i = 0; i < size; i++) {
        uint8_t data = value >> (i * 8);
        if (host->rotate)
            data = (data >> host->rotate) | (data << (8 - host->rotate));

        if (host->mono) {
            for (unsigned b = 0; b < 8; b++, host->bit++) {
                unsigned bit = ((data >> (7 - b)) & 1) ^ host->invert;

                if (host->bit < host->align || host->bit >= host->align + host->width || (host->transparent && !bit)) {
                    continue;
                }
                qvision_host_pixel(s, bit ? host->foreground : host->background,
                                   bit);
            }
        } else {
            if (host->bit >= host->align)
                qvision_host_pixel(s, data, true);
            host->bit++;
        }
        if (host->bit >= row_end) {
            /*
             * Monochrome rows end at byte boundaries. Xdec packs
             * several narrow glyph rows into a single bus write; only
             * bytes after the final glyph row are discarded.
             */
            host->bit = 0;
            if (++host->row == host->height) {
                qvision_host_stop(s);
                break;
            }
            if (!host->mono) {
                /*
                 * Packed X images pad the last bus word of each row.
                 * In contrast, Xdec packs multiple narrow mono rows into
                 * a word. Do not interpret color padding as another row.
                 */
                break;
            }
        }
    }
}

/* QVision presents the Bt484 RS lines in three extended port groups. */
static int
qvision_dac_register(uint16_t port)
{
    switch (port) {
        case 0x3c8:
            return 0;
        case 0x3c9:
            return 1;
        case 0x3c6:
            return 2;
        case 0x3c7:
            return 3;
        case 0x83c8:
            return 4;
        case 0x83c9:
            return 5;
        case 0x83c6:
            return 6;
        case 0x83c7:
            return 7;
        case 0x13c8:
            return 8;
        case 0x13c9:
            return 9;
        case 0x13c6:
            return 10;
        case 0x13c7:
            return 11;
        case 0x93c8:
            return 12;
        case 0x93c9:
            return 13;
        case 0x93c6:
            return 14;
        case 0x93c7:
            return 15;
        default:
            return -1;
    }
}

static uint16_t
qvision_vga_port(qvision_t *s, uint16_t port)
{
    if (((port & 0xfff0) == 0x3b0 || (port & 0xfff0) == 0x3d0) && !(s->svga.miscout & 1))
        port ^= 0x60;
    return port;
}

static uint8_t
qvision_in(uint16_t port, void *priv)
{
    qvision_t *s    = priv;
    svga_t    *svga = &s->svga;
    int        rs   = qvision_dac_register(port);
    if (port == 0x83c4)
        return s->select;
    if (!qvision_selected(s))
        return 0xff;
    if (port == 0x46e8)
        return s->video_enable;
    if (port == 0x102)
        return s->option_select;
    if (port >= 0x3b0 && port <= 0x3df && !qvision_vga_enabled(s))
        return 0xff;
    if (rs >= 0)
        return bt48x_ramdac_in(((rs & 2) ? 0x3c4 : 0x3c8) + (rs & 3), rs & 4, rs & 8, svga->ramdac, svga);
    if (port == 0x63ca)
        return s->registers[port] | (s->host.active ? 0x40 : 0);
    if (port >= 0x3b0 && port <= 0x3df) {
        port = qvision_vga_port(s, port);
        if (port == 0x3cf)
            return svga->gdcreg[svga->gdcaddr];
        if (port == 0x3d4)
            return svga->crtcreg;
        if (port == 0x3d5)
            return svga->crtc[svga->crtcreg];
        return svga_in(port, svga);
    }
    return s->registers[port];
}

static void
qvision_out(uint16_t port, uint8_t value, void *priv)
{
    qvision_t *s     = priv;
    svga_t    *svga  = &s->svga;
    unsigned   index = svga->gdcaddr;
    int        rs    = qvision_dac_register(port);
    if (port == 0x83c4) {
        s->select = value & 15;
        qvision_update_map(s);
        return;
    }
    if (!qvision_selected(s))
        return;
    if (port == 0x46e8 || port == 0x102) {
        if (port == 0x46e8)
            s->video_enable = value;
        else
            s->option_select = value;
        qvision_update_map(s);
        return;
    }
    if (port >= 0x3b0 && port <= 0x3df && !qvision_vga_enabled(s))
        return;
    if (rs >= 0) {
        bt48x_ramdac_out(((rs & 2) ? 0x3c4 : 0x3c8) + (rs & 3), rs & 4, rs & 8, value, svga->ramdac, svga);
        return;
    }
    if (port == 0x3ce) {
        svga->gdcaddr = value;
        return;
    }
    if (port == 0x3cf && index >= 9) {
        if (index == 0x0c)
            return;
        if (index != 0x0f && (svga->gdcreg[0x0f] & 15) != 5)
            return;
        if (index > 0x40 && !(svga->gdcreg[0x10] & 8))
            return;
        svga->gdcreg[index] = value;
        if (index == 0x10 && (value & 0x40))
            qvision_host_stop(s);
        if (index == 0x40 || index == 0x48 || index == 0x49)
            qvision_update_map(s);
        svga->fullchange = changeframecount;
        svga_recalctimings(svga);
        return;
    }
    if (port == 0x3c5 && svga->seqaddr == 2 && qvision_packed(s)) {
        svga->seqregs[2] = value;
        return;
    }
    if (port == 0x3c5 && svga->seqaddr == 0 && (value & 3) != 3) {
        qvision_host_stop(s);
        svga->gdcreg[0x40] &= ~1;
        svga->gdcreg[0x48] = svga->gdcreg[0x49] = 0;
        qvision_update_map(s);
    }
    if (port >= 0x3b0 && port <= 0x3df) {
        port = qvision_vga_port(s, port);
        if (port == 0x3d4) {
            svga->crtcreg = value & 0x3f;
            return;
        }
        if (port == 0x3d5) {
            if (svga->crtcreg < 7 && (svga->crtc[0x11] & 0x80))
                return;
            if (svga->crtcreg == 7 && (svga->crtc[0x11] & 0x80))
                value = (svga->crtc[7] & ~0x10) | (value & 0x10);
            svga->crtc[svga->crtcreg] = value;
            svga->fullchange          = changeframecount;
            svga_recalctimings(svga);
        }
        svga_out(port, value, svga);
        return;
    }
    s->registers[port] = value;
    if (port >= 0x33ca && port <= 0x33cd) {
        if (s->pattern_count == 8 || port != 0x33ca + (s->pattern_count & 3))
            s->pattern_count = 0;
        if (port == 0x33ca + (s->pattern_count & 3))
            s->pattern[s->pattern_count++] = value;
        return;
    }
    if (port == 0x63ca) {
        s->registers[port] &= ~0xc0;
        return;
    }
    if (port == 0x33ce) {
        if (s->host.active) {
            qvision_host_stop(s);
            s->registers[port] = value;
        }
        if (value & 1) {
            if (!qvision_host_start(s, value) && !qvision_pattern_blt(s, value) && !qvision_screen_blt(s, value) && !qvision_screen_mono_blt(s, value))
                pclog("QVision: unsupported BLT %02x ctl=%02x dp=%02x\n", value, s->registers[0x63ca], svga->gdcreg[0x5a]);
        }
    }
}

static uint16_t
qvision_inw(uint16_t p, void *priv)
{
    return qvision_in(p, priv) | ((uint16_t) qvision_in(p + 1, priv) << 8);
}
static uint32_t
qvision_inl(uint16_t p, void *priv)
{
    return qvision_inw(p, priv) | ((uint32_t) qvision_inw(p + 2, priv) << 16);
}
static void
qvision_outw(uint16_t p, uint16_t v, void *priv)
{
    qvision_out(p, v, priv);
    qvision_out(p + 1, v >> 8, priv);
}
static void
qvision_outl(uint16_t p, uint32_t v, void *priv)
{
    qvision_outw(p, v, priv);
    qvision_outw(p + 2, v >> 16, priv);
}

static uint8_t
qvision_mem_read(uint32_t a, void *priv)
{
    qvision_t *s = priv;
    if (!qvision_vga_enabled(s))
        return 0xff;
    if (a >= s->high_base && s->high_base >= (1U << 20))
        return s->svga.vram[(a - s->high_base) & 0xfffff];
    if (qvision_packed(s)) {
        unsigned bank   = s->svga.gdcreg[(a & 0x8000) && (s->svga.gdcreg[0x40] & 0x40) ? 0x46 : 0x45];
        unsigned mask   = s->svga.gdcreg[0x40] & 0x40 ? 0x7fff : 0xffff;
        uint32_t offset = (bank << 12) + (a & mask);
        if (s->svga.bpp == 4) {
            uint8_t value = 0, plane = 1 << (s->svga.gdcreg[0x41] & 3);
            for (unsigned bit = 0; bit < 8; bit++) {
                if (qvision_pixel_read(s, offset * 8 + bit, true) & plane)
                    value |= 0x80 >> bit;
            }
            return value;
        }
        return s->svga.vram[offset & 0xfffff];
    }
    return svga_read(a, &s->svga);
}
static uint16_t
qvision_mem_readw(uint32_t a, void *p)
{
    return qvision_mem_read(a, p) | ((uint16_t) qvision_mem_read(a + 1, p) << 8);
}
static uint32_t
qvision_mem_readl(uint32_t a, void *p)
{
    return qvision_mem_readw(a, p) | ((uint32_t) qvision_mem_readw(a + 2, p) << 16);
}
static void
qvision_mem_write_data(uint32_t a, uint32_t v, unsigned size, qvision_t *s)
{
    if (!qvision_vga_enabled(s))
        return;
    if (s->host.active) {
        qvision_host_write(s, a, v, size);
        return;
    }
    for (unsigned i = 0; i < size; i++) {
        if (a >= s->high_base && s->high_base >= (1U << 20)) {
            unsigned offset      = (a + i - s->high_base) & 0xfffff;
            s->svga.vram[offset] = v >> (8 * i);
            qvision_dirty(s, offset, 1);
        } else if (qvision_packed(s)) {
            unsigned bank   = s->svga.gdcreg[((a + i) & 0x8000) && (s->svga.gdcreg[0x40] & 0x40) ? 0x46 : 0x45];
            unsigned mask   = s->svga.gdcreg[0x40] & 0x40 ? 0x7fff : 0xffff;
            unsigned offset = ((bank << 12) + ((a + i) & mask)) & 0xfffff;
            if (s->svga.bpp == 4) {
                /* The CPU planar aperture preserves read/modify/write bit order. */
                uint8_t data       = v >> (8 * i);
                uint8_t datapath   = s->svga.gdcreg[0x5a];
                uint8_t plane_mask = (datapath & 4) ? 15 : s->svga.gdcreg[8] & 15;
                uint8_t control    = s->registers[0x63ca];
                bool    expand     = control & 0x10;
                for (unsigned bit = 0; bit < 8; bit++) {
                    uint32_t pixel   = offset * 8 + bit;
                    uint8_t  old     = qvision_pixel_read(s, pixel, true);
                    bool     primary = !!(data & (0x80 >> bit)) ^ (expand && (control & 8));
                    if (expand && (datapath & 0x10) && !primary)
                        continue;
                    uint8_t color = expand ? s->svga.gdcreg[primary ? 0x43 : 0x44] : primary ? 15 : 0;
                    if ((!expand && (datapath & 0xc0)) || (datapath & 0xc0) == 0xc0 ||
                        ((datapath & 0xc0) == 0x40 && primary) || ((datapath & 0xc0) == 0x80 && !primary))
                        color = qvision_rop(s->registers[0x33c7], color, old);
                    qvision_pixel_write(s, pixel, (old & ~plane_mask) | (color & plane_mask), true);
                }
                qvision_dirty(s, offset * 4, 4);
                continue;
            }
            s->svga.vram[offset] = v >> (8 * i);
            qvision_dirty(s, offset, 1);
        } else
            svga_write(a + i, v >> (8 * i), &s->svga);
    }
}
static void
qvision_mem_write(uint32_t a, uint8_t v, void *p)
{
    qvision_mem_write_data(a, v, 1, p);
}
static void
qvision_mem_writew(uint32_t a, uint16_t v, void *p)
{
    qvision_mem_write_data(a, v, 2, p);
}
static void
qvision_mem_writel(uint32_t a, uint32_t v, void *p)
{
    qvision_mem_write_data(a, v, 4, p);
}

static uint8_t
qvision_eisa_in(uint16_t p, void *priv)
{
    qvision_t *s = priv;
    return (p & 0xfff) == 0xc84 ? s->board_control : (p & 0xfff) == 0xc85 ? s->setup
                                                                          : 0xff;
}
static void
qvision_eisa_out(uint16_t p, uint8_t v, void *priv)
{
    qvision_t *s = priv;
    if ((p & 0xfff) == 0xc84)
        s->board_control = v & 1;
    if ((p & 0xfff) == 0xc85) {
        uint8_t old = s->setup;
        s->setup    = v & 0x1f;
        if ((old ^ s->setup) & 0x10)
            qvision_update_rom(s);
    }
    qvision_update_map(s);
}
static void
qvision_reset(void *priv)
{
    qvision_t *s = priv;
    memset(&s->host, 0, sizeof(s->host));
    memset(s->registers, 0, sizeof(s->registers));
    s->pattern_count = 0;
    s->board_control = 1;
    /* C85.4 follows the ECU default; its hardware reset value is unverified. */
    s->setup             = 0x10;
    s->video_enable      = 8;
    s->option_select     = 1;
    s->select            = 0;
    s->svga.gdcreg[0x0c] = 0x37;
    qvision_update_rom(s);
    qvision_update_map(s);
}
static void *
qvision_init(const device_t *info)
{
    qvision_t             *s      = calloc(1, sizeof(*s));
    const uint8_t          id[]   = { 0x0e, 0x11, info->local >> 8, info->local & 0xff };
    static video_timings_t timing = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };
    if (!rom_load_interleaved(QVISION_ROM_EVEN, QVISION_ROM_ODD, 0, sizeof(s->bios_data), 0, s->bios_data)) {
        free(s);
        return NULL;
    }
    memcpy(s->bios_vga, s->bios_data, sizeof(s->bios_vga));
    memset(s->bios_vga + 0x6000, 0xff, 0x800);
    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing);
    svga_init(info, &s->svga, s, 1U << 20, qvision_recalctimings, qvision_in, qvision_out, NULL, NULL);
    s->svga.ramdac            = device_add(&bt484_ramdac_device);
    s->svga.dac_hwcursor_draw = bt48x_hwcursor_draw;
    s->svga.packed_chain4     = 1;
    mem_mapping_set_handler(&s->svga.mapping, qvision_mem_read, qvision_mem_readw, qvision_mem_readl,
                            qvision_mem_write, qvision_mem_writew, qvision_mem_writel);
    mem_mapping_set_p(&s->svga.mapping, s);
    mem_mapping_add(&s->highmap, 0, 0, qvision_mem_read, qvision_mem_readw, qvision_mem_readl,
                    qvision_mem_write, qvision_mem_writew, qvision_mem_writel, NULL, MEM_MAPPING_EXTERNAL, s);
    s->bios.rom  = s->bios_data + 0x8000;
    s->bios.sz   = 0x8000;
    s->bios.mask = 0x7fff;
    mem_mapping_add(&s->bios.mapping, 0xc0000, 0x8000, rom_read, rom_readw, rom_readl,
                    NULL, NULL, NULL, s->bios.rom, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM_WS, &s->bios);
    if (info->flags & DEVICE_EISA) {
        for (s->slot = 1; s->slot <= EISA_MAX_SLOTS; s->slot++) {
            if (!eisa_slot_occupied(s->slot) && eisa_add(s->slot, id, qvision_eisa_in, qvision_eisa_out, NULL, s))
                break;
        }
    }
    io_sethandler(0x3b0, 0x30, qvision_in, qvision_inw, qvision_inl, qvision_out, qvision_outw, qvision_outl, s);
    for (unsigned i = 0; i < sizeof(qvision_ports) / sizeof(qvision_ports[0]); i++) {
        uint16_t p = qvision_ports[i];
        io_sethandler(p, p == 0x102 || p == 0x46e8 ? 1 : 16,
                      qvision_in, qvision_inw, qvision_inl, qvision_out, qvision_outw, qvision_outl, s);
    }
    qvision_reset(s);
    return s;
}
static void
qvision_close(void *p)
{
    qvision_t *s = p;
    if (s->slot)
        eisa_remove(s->slot);
    svga_close(&s->svga);
    free(s);
}
static void
qvision_speed_changed(void *p)
{
    qvision_t *s = p;
    svga_recalctimings(&s->svga);
}
static void
qvision_force_redraw(void *p)
{
    qvision_t *s       = p;
    s->svga.fullchange = changeframecount;
}
static int
qvision_available(void)
{
    return rom_present(QVISION_ROM_EVEN) && rom_present(QVISION_ROM_ODD);
}

const device_t compaq_qvision_1024e_device = {
    .name          = "Compaq QVision 1024/E",
    .internal_name = "compaq_qvision_1024e",
    .flags         = DEVICE_EISA,
    .local         = 0x3011,
    .init          = qvision_init,
    .close         = qvision_close,
    .reset         = qvision_reset,
    .available     = qvision_available,
    .speed_changed = qvision_speed_changed,
    .force_redraw  = qvision_force_redraw
};

const device_t compaq_qvision_1024i_device = {
    .name          = "Compaq QVision 1024/I",
    .internal_name = "compaq_qvision_1024i",
    .flags         = DEVICE_ISA16,
    .local         = 0x3021,
    .init          = qvision_init,
    .close         = qvision_close,
    .reset         = qvision_reset,
    .available     = qvision_available,
    .speed_changed = qvision_speed_changed,
    .force_redraw  = qvision_force_redraw
};
