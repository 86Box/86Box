/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PCjx video-side helpers.
 */
#include <stdint.h>
#include <string.h>

#include <86box/timer.h>
#include <86box/video.h>
#include <86box/mem.h>
#include <86box/vid_cga_comp.h>
#include <86box/vid_pcjx.h>

#include "cpu.h"

#define PCJX_VIDEO_IO_PAGE2 0x01
#define PCJX_VIDEO_IO_EX    0x02
#define PCJX_VIDEO_IO_PAGE1 0x04
#define PCJX_VIDEO_IO_CRTC  0x08

static void pcjx_video_sync_active_view(pcjx_video_t *video);
static uint8_t pcjx_video_renderer_uses_secondary(const pcjx_video_t *video);
static uint8_t pcjx_video_effective_mode_viewport(const pcjx_video_t *video);
static uint8_t pcjx_video_columns(const pcjx_video_t *video);
static uint16_t pcjx_video_cursoraddr(const pcjx_video_t *video);
static void pcjx_video_waitstates(void);

static void
pcjx_video_reset_graphics_state(pcjx_video_t *video)
{
    if (video == NULL)
        return;

    memset(&video->text, 0, sizeof(video->text));
    memset(&video->graphics, 0, sizeof(video->graphics));
}

static void
pcjx_video_reset_extended_text_state(pcjx_video_t *video)
{
    if (video == NULL)
        return;

    video->ex_text.row_start = 0;
    video->ex_text.scanline  = 0;
    video->ex_text.valid     = 0;
}

void
pcjx_video_reset_extended_graphics_state(pcjx_video_t *video)
{
    if (video == NULL)
        return;

    pcjx_video_reset_graphics_state(video);
    pcjx_video_reset_extended_text_state(video);
    video->ex_graphics_gma       = 0;
    video->ex_graphics_gma_base  = 0;
    video->ex_graphics_gra       = 0;
    video->ex_graphics_valid     = 0;
}

void
pcjx_video_notify_display_restart(pcjx_video_t *video)
{
    pcjx_video_reset_extended_graphics_state(video);
}

static uint8_t
pcjx_video_gate_active_mask(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return video->gate_io_mask & 0x03;
}

static uint8_t
pcjx_video_gate_status_viewport(uint8_t gate_mask)
{
    if (gate_mask & 0x01)
        return 0;

    return 1;
}

static uint8_t
pcjx_video_control_io_enabled(const pcjx_video_t *video, uint8_t mask)
{
    if (video == NULL)
        return 0;

    return !!(video->control_io_mask & mask);
}

static uint8_t
pcjx_video_font_byte(const pcjx_video_t *video, uint32_t offset)
{
    if ((video == NULL) || (video->font_rom == NULL) || (video->font_rom_mask == 0))
        return 0;

    return video->font_rom[offset & video->font_rom_mask];
}

uint8_t
pcjx_video_is_extended_active(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return !!(video->gate.palette_mask[1] & 0x80);
}

static uint8_t
pcjx_video_extended_graphics_mode(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return !!(video->ex.regs[2] & 0x02);
}

static uint8_t
pcjx_video_extended_video_enabled(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return !!(video->ex.regs[2] & 0x08);
}

static uint8_t
pcjx_video_columns(const pcjx_video_t *video)
{
    return (video != NULL) ? video->host.crtc[1] : 0;
}

static uint16_t
pcjx_video_cursoraddr(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return (uint16_t) ((video->host.crtc[15] | (video->host.crtc[14] << 8)) & 0x3fff);
}

static void
pcjx_video_waitstates(void)
{
    static const uint8_t ws_array[16] = { 0, 1, 1, 1, 2, 2, 2, 3,
                                          3, 3, 4, 4, 4, 5, 5, 5 };

    cycles -= ws_array[cycles & 0xf];
}

uint16_t
pcjx_video_extended_render_width(const pcjx_video_t *video)
{
    uint8_t cell_width;
    uint8_t columns;

    if (video == NULL)
        return 0;

    columns    = pcjx_video_columns(video);
    cell_width = pcjx_video_extended_graphics_mode(video) ? 16 : 9;
    return columns * cell_width;
}

static void
pcjx_video_mark_changed(pcjx_video_t *video)
{
    if (video == NULL)
        return;

    video->change_requested = 1;
}

static void
pcjx_video_mark_renderer_changed(pcjx_video_t *video)
{
    uint8_t viewport;

    if (video == NULL)
        return;

    viewport = pcjx_video_effective_mode_viewport(video);
    update_cga16_color(video->gate.mode1[viewport], video->gate.border_color & 0x0f);
    video->timings_dirty = 1;
    pcjx_video_mark_changed(video);
}

static void
pcjx_video_sync_registers(pcjx_video_t *video, uint8_t viewport)
{
    if (video == NULL)
        return;

    (void) viewport;

    pcjx_video_mark_renderer_changed(video);
}

static uint8_t
pcjx_video_viewport_output_enabled(const pcjx_video_t *video, uint8_t viewport)
{
    if (video == NULL)
        return 1;

    return !!(video->gate.mode1[viewport] & 0x08);
}

uint8_t
pcjx_video_needs_combined_render(const pcjx_video_t *video)
{
    uint8_t superimpose_mode;

    if (video == NULL)
        return 0;

    if (video->gate.palette_mask[1] & 0x80)
        return 0;

    superimpose_mode = video->gate.superimpose & 0x0f;
    return superimpose_mode >= 0x02;
}

uint8_t
pcjx_video_combine_pixels(const pcjx_video_t *video, uint8_t pixel1,
                          uint8_t pixel2)
{
    uint8_t superimpose_mode;

    if (video == NULL)
        return pixel1 & 0x0f;

    superimpose_mode = video->gate.superimpose & 0x0f;
    pixel1          &= 0x0f;
    pixel2          &= 0x0f;

    switch (superimpose_mode) {
        case 0x00:
            return pixel1;

        case 0x01:
            return pixel2;

        case 0x02:
            return (pixel1 == (video->gate.trans_palette & 0x0f)) ? pixel2 : pixel1;

        case 0x03:
            return (pixel1 != (video->gate.trans_palette & 0x0f)) ? pixel2 : pixel1;

        case 0x06:
            if (video->gate.mode1[1] & 0x80)
                return (uint8_t) (((pixel1 | (pixel2 << 2)) ^ 0x0a) & 0x0f);
            break;

        default:
            break;
    }

    if ((superimpose_mode == 0x04) || (superimpose_mode == 0x05) ||
        (superimpose_mode == 0x06) || (superimpose_mode == 0x07))
        return (pixel1 ^ pixel2) & 0x0f;

    if ((superimpose_mode >= 0x08) && (superimpose_mode <= 0x0b))
        return (pixel1 & pixel2) & 0x0f;

    if (superimpose_mode >= 0x0c)
        return (pixel1 | pixel2) & 0x0f;

    return pixel1;
}

static uint8_t
pcjx_video_renderer_uses_secondary(const pcjx_video_t *video)
{
    uint8_t superimpose_mode;
    uint8_t enable1;
    uint8_t enable2;

    if (video == NULL)
        return 0;

    if (video->gate.palette_mask[1] & 0x80)
        return 0;

    superimpose_mode = video->gate.superimpose & 0x0f;
    if (superimpose_mode == 0x01)
        return 1;

    enable1 = pcjx_video_viewport_output_enabled(video, 0);
    enable2 = pcjx_video_viewport_output_enabled(video, 1);

    if (((superimpose_mode == 0x04) || (superimpose_mode == 0x05) || (superimpose_mode == 0x07) ||
         ((superimpose_mode == 0x06) && !(video->gate.mode1[1] & 0x80)) ||
         ((superimpose_mode >= 0x0c) && (superimpose_mode <= 0x0f))) &&
        !enable1 && enable2)
        return 1;

    return 0;
}

static uint8_t
pcjx_video_effective_mode_viewport(const pcjx_video_t *video)
{
    uint8_t superimpose_mode;

    if (video == NULL)
        return 0;

    if (video->gate.palette_mask[1] & 0x80)
        return 0;

    superimpose_mode = video->gate.superimpose & 0x0f;
    if (superimpose_mode == 0x01)
        return 1;

    if ((superimpose_mode >= 0x02) &&
        !(video->gate.mode1[0] & 0x01) &&
        (video->gate.mode1[1] & 0x01))
        return 1;

    return 0;
}

void
pcjx_video_set_host_state(pcjx_video_t *video,
                          const pcjx_video_host_state_t *state)
{
    if ((video == NULL) || (state == NULL))
        return;

    video->host = *state;
}

void
pcjx_video_get_host_state(const pcjx_video_t *video,
                          pcjx_video_host_state_t *state)
{
    if ((video == NULL) || (state == NULL))
        return;

    *state = video->host;
}

uint8_t
pcjx_video_consume_timings_dirty(pcjx_video_t *video)
{
    uint8_t timings_dirty;

    if (video == NULL)
        return 0;

    timings_dirty       = video->timings_dirty;
    video->timings_dirty = 0;
    return timings_dirty;
}

uint8_t
pcjx_video_consume_change_requested(pcjx_video_t *video)
{
    uint8_t change_requested;

    if (video == NULL)
        return 0;

    change_requested       = video->change_requested;
    video->change_requested = 0;
    return change_requested;
}

void
pcjx_video_get_effective_render_mode(const pcjx_video_t *video,
                                     uint8_t *mode1, uint8_t *mode2)
{
    uint8_t viewport;

    if ((mode1 == NULL) && (mode2 == NULL))
        return;

    viewport = pcjx_video_effective_mode_viewport(video);
    if (mode1 != NULL)
        *mode1 = (video != NULL) ? video->gate.mode1[viewport] : 0;
    if (mode2 != NULL)
        *mode2 = (video != NULL) ? video->gate.mode2[viewport] : 0;
}

int8_t
pcjx_video_render_x_bias(const pcjx_video_t *video)
{
    return (video != NULL) ? 48 : 0;
}

int8_t
pcjx_video_render_y_bias(const pcjx_video_t *video)
{
    return (video != NULL) ? 2 : 0;
}

double
pcjx_video_timing_scale(const pcjx_video_t *video)
{
    if (video == NULL)
        return 1.0;

    return (video->ex.regs[2] & 0x02) ? 2.0 : (9.0 / 8.0);
}

static void
pcjx_video_correct_display_position(pcjx_video_t *video, int16_t new_start_x,
                                    int16_t new_start_y)
{
    uint16_t dx;
    uint16_t dy;

    if (video == NULL)
        return;

    if (!video->display.initialized) {
        video->display.start_x       = new_start_x;
        video->display.start_y       = new_start_y;
        video->display.pending_x     = new_start_x;
        video->display.pending_y     = new_start_y;
        video->display.pending_count = 0;
        video->display.initialized   = 1;
        return;
    }

    if ((video->display.pending_x == new_start_x) &&
        (video->display.pending_y == new_start_y)) {
        if (video->display.pending_count == 0) {
            video->display.start_x = new_start_x;
            video->display.start_y = new_start_y;
        } else
            video->display.pending_count--;

        return;
    }

    video->display.pending_x = new_start_x;
    video->display.pending_y = new_start_y;

    dx = (new_start_x > video->display.start_x) ?
             (new_start_x - video->display.start_x) :
             (video->display.start_x - new_start_x);
    dy = (new_start_y > video->display.start_y) ?
             (new_start_y - video->display.start_y) :
             (video->display.start_y - new_start_y);

    if ((dx + dy) < 5)
        video->display.pending_count = 16;
    else
        video->display.pending_count = 2;
}

void
pcjx_video_update_display_position(pcjx_video_t *video, int16_t raw_start_x,
                                   int16_t raw_start_y)
{
    pcjx_video_correct_display_position(video, raw_start_x, raw_start_y);
}

void
pcjx_video_get_display_position(const pcjx_video_t *video, int16_t *start_x,
                                int16_t *start_y)
{
    if (start_x != NULL)
        *start_x = ((video != NULL) && video->display.initialized) ?
                       video->display.start_x : 0;
    if (start_y != NULL)
        *start_y = ((video != NULL) && video->display.initialized) ?
                       video->display.start_y : 0;
}

uint8_t
pcjx_video_get_frame_blit_geometry(const pcjx_video_t *video,
                                   int16_t firstline, int16_t render_ho_d,
                                   uint8_t double_type, int16_t *blit_x,
                                   int16_t *blit_y, uint16_t *frame_width,
                                   uint16_t *frame_height)
{
    int16_t render_y_bias;
    int16_t raw_blit_y;
    int16_t stable_x;
    int16_t stable_y;
    uint8_t extended_active;

    if (video == NULL)
        return 0;

    extended_active = pcjx_video_is_extended_active(video);

    render_y_bias = pcjx_video_render_y_bias(video);
    if (double_type > 0)
        raw_blit_y = (firstline << 1) + 16 + (render_y_bias << 1);
    else
        raw_blit_y = firstline + 8 + render_y_bias;

    stable_x = render_ho_d;
    stable_y = raw_blit_y;
    if (video->display.initialized)
        pcjx_video_get_display_position(video, &stable_x, &stable_y);

    if (blit_x != NULL)
        *blit_x = stable_x;
    if (frame_width != NULL)
        *frame_width = extended_active ? 720 : 640;
    if (frame_height != NULL)
        *frame_height = extended_active ? 525 : 400;

    if (blit_y == NULL)
        return 1;

    *blit_y = stable_y;

    return 1;
}

static uint32_t
pcjx_video_secondary_crt_base(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return (uint32_t) (video->page_reg[1] & 0x03) << 14;
}

static void
pcjx_video_apply_gate_array_reg(pcjx_video_t *video, uint8_t viewport, uint8_t reg,
                                uint8_t val)
{
    uint8_t sync_active = 0;

    if (video == NULL)
        return;

    switch (reg) {
        case 0x00:
            video->gate.mode1[viewport] = val;
            video->gate.mode1_programmed[viewport] = 1;
            pcjx_video_reset_graphics_state(video);
            sync_active     = 1;
            break;

        case 0x01:
            video->gate.palette_mask[viewport] = val;
            sync_active            = 1;
            break;

        case 0x02:
            video->gate.border_color = val & 0x0f;
            sync_active         = 1;
            break;

        case 0x03:
            video->gate.mode2[viewport] = val;
            pcjx_video_reset_graphics_state(video);
            sync_active     = 1;
            break;

        case 0x04:
            video->gate.reset = val;
            break;

        case 0x05:
            video->gate.trans_palette = val;
            break;

        case 0x06:
            video->gate.superimpose = val;
            sync_active             = 1;
            break;

        default:
            if ((reg >= 0x10) && (reg <= 0x1f)) {
                video->gate.palette[reg & 0x0f] = val & 0x0f;
                sync_active                = 1;
            }
            break;
    }

    if (sync_active)
        pcjx_video_sync_active_view(video);
}

static uint8_t
pcjx_video_gate_array_status(const pcjx_video_t *video)
{
    uint8_t ret = 0x06;

    if (video == NULL)
        return ret;

    if (video->host.status & 0x01)
        ret |= 0x01;
    if (video->host.status & 0x08)
        ret |= 0x08;

    if (video->last_color & (1 << (video->gate.status_index & 0x03)))
        ret |= 0x10;

    return ret;
}

void
pcjx_video_set_gate_array_io_mask(pcjx_video_t *video, uint8_t io_mask)
{
    if (video == NULL)
        return;

    video->gate_io_mask = io_mask & 0x03;
}

void
pcjx_video_set_control_io_mask(pcjx_video_t *video, uint8_t io_mask)
{
    if (video == NULL)
        return;

    video->control_io_mask = io_mask & (PCJX_VIDEO_IO_PAGE2 | PCJX_VIDEO_IO_EX |
                                        PCJX_VIDEO_IO_PAGE1 | PCJX_VIDEO_IO_CRTC);
}

void
pcjx_video_set_font_rom(pcjx_video_t *video, const uint8_t *font_rom,
                        uint32_t font_rom_size)
{
    if (video == NULL)
        return;

    video->font_rom = font_rom;
    if ((font_rom != NULL) && (font_rom_size != 0))
        video->font_rom_mask = font_rom_size - 1;
    else
        video->font_rom_mask = 0;
}

static void
pcjx_video_clear_extended_regs(pcjx_video_t *video)
{
    if (video == NULL)
        return;

    pcjx_video_reset_extended_graphics_state(video);
    video->ex.regs[2] = 0;
    video->ex.regs[4] = 0;
    video->ex.regs[5] = 0;
    video->ex.regs[6] = 0;
    video->ex.regs[7] = 0;
}

static void
pcjx_video_apply_extended_reg(pcjx_video_t *video, uint8_t reg, uint8_t val)
{
    if (video == NULL)
        return;

    switch (reg & 0x07) {
        case 0x00:
            video->ex.access = 1;
            break;

        case 0x01:
            video->ex.access = 0;
            pcjx_video_clear_extended_regs(video);
            pcjx_video_mark_changed(video);
            break;

        case 0x02:
            pcjx_video_reset_extended_graphics_state(video);
            video->ex.regs[2] = val;
            pcjx_video_mark_changed(video);
            break;

        case 0x03:
            video->ex.regs[3]            = val;
            video->ex.palette[(val >> 6) & 0x03] = val & 0x0f;
            pcjx_video_mark_changed(video);
            break;

        case 0x04:
            video->ex.regs[4] = val;
            break;

        case 0x05:
        case 0x06:
        case 0x07:
            video->ex.regs[reg & 0x07] = val;
            pcjx_video_mark_changed(video);
            break;

        default:
            break;
    }
}

static uint8_t
pcjx_video_read_extended_reg(const pcjx_video_t *video)
{
    uint8_t reg;

    if (video == NULL)
        return 0;

    reg = video->ex.index & 0x07;
    switch (reg) {
        case 0x00:
        case 0x01:
            return video->ex.access;

        case 0x03:
            return video->ex.regs[3];

        default:
            return video->ex.regs[reg];
    }
}

static uint32_t
pcjx_program_offset(const pcjx_video_t *video, uint32_t addr)
{
    uint32_t bank_base;
    uint32_t mask;

    if (video->program_size == 0)
        return 0;

    bank_base = (uint32_t) ((video->page_reg[0] >> 3) & 0x07) << 14;
    mask      = video->program_size - 1;
    return (bank_base + (addr & 0x7fff)) & mask;
}

static uint32_t
pcjx_vram2_offset(const pcjx_video_t *video, uint32_t addr)
{
    uint32_t bank_base = (uint32_t) ((video->page_reg[1] >> 3) & 0x03) << 14;

    return (bank_base + ((addr & 0xffff) ^ 0x8000)) & 0xffff;
}

void
pcjx_video_b800_write(pcjx_video_t *video, uint32_t addr, uint8_t val)
{
    uint32_t offset;

    if (video == NULL)
        return;

    offset = addr & 0x3fff;
    if (pcjx_video_renderer_uses_secondary(video)) {
        uint32_t vram2_offset = (pcjx_video_secondary_crt_base(video) + offset) & 0xffff;

        video->vram2[vram2_offset] = val;
        pcjx_video_mark_changed(video);
        return;
    }

    if (video->program_size == 0)
        return;

    ram[pcjx_program_offset(video, offset)] = val;
}

uint8_t
pcjx_video_b800_read(const pcjx_video_t *video, uint32_t addr)
{
    uint32_t offset;

    if (video == NULL)
        return 0xff;

    offset = addr & 0x3fff;
    if (pcjx_video_renderer_uses_secondary(video))
        return video->vram2[(pcjx_video_secondary_crt_base(video) + offset) & 0xffff];

    if (video->program_size == 0)
        return 0xff;

    return ram[pcjx_program_offset(video, offset)];
}

static void
pcjx_video_sync_active_view(pcjx_video_t *video)
{
    uint8_t register_viewport;

    if (video == NULL)
        return;

    register_viewport = pcjx_video_effective_mode_viewport(video);
    pcjx_video_sync_registers(video, register_viewport);
}

static uint8_t
pcjx_first_window_read(uint32_t addr, void *priv)
{
    const pcjx_video_t *video = (const pcjx_video_t *) priv;

    if ((video == NULL) || (video->program_size == 0))
        return 0xff;

    pcjx_video_waitstates();
    return ram[pcjx_program_offset(video, addr)];
}

static void
pcjx_first_window_write(uint32_t addr, uint8_t val, void *priv)
{
    const pcjx_video_t *video = (const pcjx_video_t *) priv;

    if ((video == NULL) || (video->program_size == 0))
        return;

    pcjx_video_waitstates();
    ram[pcjx_program_offset(video, addr)] = val;
}

static uint8_t
pcjx_second_window_read(uint32_t addr, void *priv)
{
    const pcjx_video_t *video = (const pcjx_video_t *) priv;

    if (video == NULL)
        return 0xff;

    pcjx_video_waitstates();
    return video->vram2[pcjx_vram2_offset(video, addr)];
}

static void
pcjx_second_window_write(uint32_t addr, uint8_t val, void *priv)
{
    pcjx_video_t *video = (pcjx_video_t *) priv;
    uint32_t      offset;

    if (video == NULL)
        return;

    pcjx_video_waitstates();
    offset               = pcjx_vram2_offset(video, addr);
    video->vram2[offset] = val;
    if (pcjx_video_renderer_uses_secondary(video))
        pcjx_video_mark_changed(video);
}

static void
pcjx_video_clear_mapping(mem_mapping_t *mapping)
{
    if (mapping == NULL)
        return;

    if (mapping->size != 0)
        mem_set_mem_state_both(mapping->base, mapping->size,
                               MEM_READ_EXTANY | MEM_WRITE_EXTANY);

    mem_mapping_disable(mapping);
}

static void
pcjx_video_clear_first_window(pcjx_video_t *video)
{
    if (video == NULL)
        return;

    pcjx_video_clear_mapping(&video->first_window_mapping);
}

void
pcjx_video_init(pcjx_video_t *video, uint32_t program_size)
{
    memset(video, 0, sizeof(*video));
    video->program_size = program_size;
    pcjx_video_reset_extended_graphics_state(video);

    mem_mapping_add(&video->first_window_mapping, 0, 0,
                    pcjx_first_window_read, NULL, NULL,
                    pcjx_first_window_write, NULL, NULL,
                    NULL, 0, video);
    mem_mapping_disable(&video->first_window_mapping);

    mem_mapping_add(&video->second_window_mapping, 0, 0,
                    pcjx_second_window_read, NULL, NULL,
                    pcjx_second_window_write, NULL, NULL,
                    NULL, 0, video);
    mem_mapping_disable(&video->second_window_mapping);
}

void
pcjx_video_close(pcjx_video_t *video)
{
    pcjx_video_clear_first_window(video);
    pcjx_video_clear_mapping(&video->second_window_mapping);
}

void
pcjx_video_apply_first_window(pcjx_video_t *video, uint32_t base,
                              uint32_t size, int can_read, int can_write)
{
    pcjx_video_clear_first_window(video);

    if ((video == NULL) || (video->program_size == 0))
        return;

    if ((size != 0x8000) || !can_read || !can_write)
        return;

    mem_mapping_set_addr(&video->first_window_mapping, base, size);
    mem_mapping_enable(&video->first_window_mapping);
    mem_set_mem_state_both(base, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
}

void
pcjx_video_apply_second_window(pcjx_video_t *video, uint32_t base,
                               uint32_t size, int can_read, int can_write)
{
    pcjx_video_clear_mapping(&video->second_window_mapping);

    if (video == NULL)
        return;

    /* The current PCjr-derived core only exposes one 32 KiB render backing store. */
    if ((size != 0x8000) || !can_read || !can_write)
        return;

    mem_mapping_set_addr(&video->second_window_mapping, base, size);
    mem_mapping_enable(&video->second_window_mapping);
    mem_set_mem_state_both(base, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
}

uint8_t
pcjx_video_blank_color(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return video->gate.border_color & 0x0f;
}

static uint8_t
pcjx_video_mode_uses_hires(uint8_t mode1)
{
    return !!(mode1 & 0x01);
}

static uint8_t
pcjx_video_cell_width(const pcjx_video_t *video, uint8_t superimpose_mode)
{
    if (video == NULL)
        return 0;

    if (((superimpose_mode != 0x01) &&
         pcjx_video_mode_uses_hires(video->gate.mode1[0])) ||
        ((superimpose_mode != 0x00) &&
         pcjx_video_mode_uses_hires(video->gate.mode1[1])))
        return 8;

    return 16;
}

static uint32_t
pcjx_video_primary_crt_base(const pcjx_video_t *video)
{
    uint32_t base;

    if ((video == NULL) || (video->program_size == 0))
        return 0;

    base = (uint32_t) (video->page_reg[0] & 0x07) << 14;
    return base & (video->program_size - 1);
}

static uint8_t
pcjx_video_viewport_uses_program_memory(const pcjx_video_t *video, uint8_t viewport)
{
    if (video == NULL)
        return 0;

    if ((viewport & 0x01) == 0)
        return 1;

    return !!(video->gate.mode2[1] & 0x10);
}

static uint32_t
pcjx_video_viewport_readtop(const pcjx_video_t *video, uint8_t viewport)
{
    if (pcjx_video_viewport_uses_program_memory(video, viewport)) {
        if ((viewport & 0x01) == 0)
            return pcjx_video_primary_crt_base(video);

        if (video->program_size == 0)
            return 0;

        return ((uint32_t) (video->page_reg[1] & 0x03) << 14) &
               (video->program_size - 1);
    }

    if (viewport == 0)
        return pcjx_video_primary_crt_base(video);

    return pcjx_video_secondary_crt_base(video);
}

static uint8_t
pcjx_video_viewport_read_byte(const pcjx_video_t *video, uint8_t viewport,
                              uint32_t offset)
{
    if (video == NULL)
        return 0;

    if (pcjx_video_viewport_uses_program_memory(video, viewport)) {
        if (video->program_size == 0)
            return 0;

        return ram[offset & (video->program_size - 1)];
    }

    return video->vram2[offset & 0xffff];
}

static void
pcjx_video_fill_raw_pixels(uint8_t *pixels, uint8_t width, uint8_t color)
{
    if ((pixels == NULL) || (width <= 0))
        return;

    memset(pixels, color & 0x0f, (size_t) width);
}

typedef struct pcjx_video_text_render_state_t {
    uint8_t  blink_phase;
    uint8_t  mode1;
    uint8_t  mode2;
    uint8_t  ra;
    uint8_t  cursor;
} pcjx_video_text_render_state_t;

typedef struct pcjx_video_viewport_render_state_t {
    uint16_t text_memaddr;
    uint16_t graphics_gma;
    uint16_t cursoraddr;
    uint8_t  blink_phase;
    uint8_t  graphics_gra;
    uint8_t  text_ra;
} pcjx_video_viewport_render_state_t;

typedef struct pcjx_video_extended_text_render_state_t {
    uint16_t base_memaddr;
    uint16_t cursoraddr;
    uint8_t  blink_phase;
    uint8_t  scanline;
} pcjx_video_extended_text_render_state_t;

typedef struct pcjx_video_extended_render_state_t {
    uint16_t base_memaddr;
    uint16_t cursoraddr;
    uint8_t  blink_phase;
    uint8_t  scanline;
    uint16_t graphics_gma;
    uint8_t  graphics_gra;
} pcjx_video_extended_render_state_t;

static void
pcjx_video_render_raw_text_25(const pcjx_video_t *video, uint8_t viewport,
                              uint16_t memaddr,
                              const pcjx_video_text_render_state_t *state,
                              uint8_t *pixels)
{
    uint32_t voff;
    uint8_t chr;
    uint8_t attr;
    uint8_t bg;
    uint8_t fg;
    uint32_t glyph_addr;
    uint8_t glyph = 0x00;

    if (state == NULL)
        return;

    voff   = pcjx_video_viewport_readtop(video, viewport) | ((memaddr << 1) & 0x3fff);
    chr    = pcjx_video_viewport_read_byte(video, viewport, voff);
    attr   = pcjx_video_viewport_read_byte(video, viewport, voff | 1);
    bg     = (uint8_t) ((attr >> 4) & 0x0f);
    fg     = (uint8_t) (attr & 0x0f);

    if (state->mode2 & 0x02) {
        bg &= 0x07;
        if ((attr & 0x80) && state->blink_phase)
            fg = bg;
    }

    glyph_addr = ((uint32_t) chr << 5) + 1 + (viewport ? 0 : 16);
    if (state->cursor)
        glyph = 0xff;
    else if (state->ra < 8)
        glyph = pcjx_video_font_byte(video, glyph_addr | ((uint32_t) state->ra << 1));

    if (pcjx_video_mode_uses_hires(state->mode1)) {
        for (uint8_t pixel = 0; pixel < 8; pixel++, glyph <<= 1)
            pixels[pixel] = (glyph & 0x80) ? fg : bg;
        return;
    }

    for (uint8_t pixel = 0; pixel < 8; pixel++, glyph <<= 1) {
        uint8_t color = (glyph & 0x80) ? fg : bg;

        pixels[pixel << 1]       = color;
        pixels[(pixel << 1) + 1] = color;
    }
}

static void
pcjx_video_render_raw_text_kanji(const pcjx_video_t *video, uint16_t memaddr,
                                 const pcjx_video_text_render_state_t *state,
                                 uint8_t *pixels)
{
    uint32_t voff;
    uint8_t chr;
    uint8_t attr;
    uint8_t bg;
    uint8_t fg;
    uint32_t glyph_addr;
    uint8_t glyph = 0x00;

    if (state == NULL)
        return;

    voff   = pcjx_video_viewport_readtop(video, 1) | ((memaddr << 1) & 0x3fff);
    chr    = pcjx_video_viewport_read_byte(video, 1, voff);
    attr   = pcjx_video_viewport_read_byte(video, 1, voff | 1);
    bg     = (uint8_t) ((attr >> 4) & 0x07);
    fg     = (uint8_t) (attr & 0x07);

    if (!(attr & 0x80))
        glyph_addr = (uint32_t) chr << 5;
    else {
        uint8_t  adjacent_chr;
        uint16_t code;
        uint8_t  right;

        if (!(attr & 0x08)) {
            adjacent_chr = pcjx_video_viewport_read_byte(video, 1,
                                                         pcjx_video_viewport_readtop(video, 1) |
                                                             ((((uint32_t) memaddr << 1) + 2) & 0x3fff));
            code         = (uint16_t) ((chr << 8) | adjacent_chr);
            right        = 0;
        } else {
            adjacent_chr = pcjx_video_viewport_read_byte(video, 1,
                                                         pcjx_video_viewport_readtop(video, 1) |
                                                             ((((uint32_t) memaddr << 1) - 2) & 0x3fff));
            code         = (uint16_t) ((adjacent_chr << 8) | chr);
            right        = 1;
        }

        if (code >= 0xf000)
            code = (uint16_t) ((code & 0x3f) | 0x8400);

        glyph_addr = ((uint32_t) (code & 0x1fff) << 5) + right;
    }

    if (state->cursor)
        glyph = 0xff;
    else if (state->ra < 16)
        glyph = pcjx_video_font_byte(video, glyph_addr | ((uint32_t) state->ra << 1));

    if (pcjx_video_mode_uses_hires(state->mode1)) {
        for (uint8_t pixel = 0; pixel < 8; pixel++, glyph <<= 1)
            pixels[pixel] = (glyph & 0x80) ? fg : bg;
        return;
    }

    for (uint8_t pixel = 0; pixel < 8; pixel++, glyph <<= 1) {
        uint8_t color = (glyph & 0x80) ? fg : bg;

        pixels[pixel << 1]       = color;
        pixels[(pixel << 1) + 1] = color;
    }
}

static void
pcjx_video_render_raw_graphics(const pcjx_video_t *video, uint8_t viewport,
                               uint16_t gma, uint8_t graphics_row,
                               uint8_t mode1, uint8_t mode2,
                               uint8_t *pixels)
{
    uint32_t readtop;
    uint32_t voff;
    uint8_t  d1;
    uint8_t  d2;
    uint8_t  colors;
    uint8_t  graphics_bank;

    readtop = pcjx_video_viewport_readtop(video, viewport);
    graphics_bank = graphics_row & (pcjx_video_mode_uses_hires(mode1) ? 0x03 : 0x01);
    if (mode1 & 0x10)
        colors = 4;
    else if (mode2 & 0x08)
        colors = 1;
    else
        colors = 2;

    if (pcjx_video_mode_uses_hires(mode1))
        voff = (readtop & ~0x7fffU) | (((uint32_t) graphics_bank << 13) & 0x6000U) |
             (((uint32_t) gma << 1) & 0x1fffU);
    else
        voff = (readtop & ~0x3fffU) | (((uint32_t) graphics_bank << 13) & 0x2000U) |
             (((uint32_t) gma << 1) & 0x1fffU);

    d1 = pcjx_video_viewport_read_byte(video, viewport, voff);
    d2 = pcjx_video_viewport_read_byte(video, viewport, voff | 1);

    switch (colors) {
        case 1:
            for (uint8_t pixel = 0; pixel < 8; pixel++, d1 <<= 1)
                pixels[pixel] = (d1 & 0x80) >> 7;
            for (uint8_t pixel = 0; pixel < 8; pixel++, d2 <<= 1)
                pixels[pixel + 8] = (d2 & 0x80) >> 7;
            return;

        case 4:
            if (pcjx_video_mode_uses_hires(mode1)) {
                pixels[0] = pixels[1] = d1 >> 4;
                pixels[2] = pixels[3] = d1 & 0x0f;
                pixels[4] = pixels[5] = d2 >> 4;
                pixels[6] = pixels[7] = d2 & 0x0f;
                return;
            }

            pixels[0]  = pixels[1]  = pixels[2]  = pixels[3]  = d1 >> 4;
            pixels[4]  = pixels[5]  = pixels[6]  = pixels[7]  = d1 & 0x0f;
            pixels[8]  = pixels[9]  = pixels[10] = pixels[11] = d2 >> 4;
            pixels[12] = pixels[13] = pixels[14] = pixels[15] = d2 & 0x0f;
            return;

        default:
            if (pcjx_video_mode_uses_hires(mode1)) {
                for (uint8_t pixel = 0; pixel < 8; pixel++, d1 <<= 1, d2 <<= 1)
                    pixels[pixel] = (uint8_t) (((d1 & 0x80) >> 7) | ((d2 & 0x80) >> 6));
                return;
            }

            for (uint8_t pixel = 0; pixel < 4; pixel++, d1 <<= 2) {
                uint8_t color = (d1 & 0xc0) >> 6;

                pixels[pixel << 1]       = color;
                pixels[(pixel << 1) + 1] = color;
            }
            for (uint8_t pixel = 0; pixel < 4; pixel++, d2 <<= 2) {
                uint8_t color = (d2 & 0xc0) >> 6;
                uint8_t base  = 8 + (pixel << 1);

                pixels[base]     = color;
                pixels[base + 1] = color;
            }
            return;
    }
}

static uint16_t
pcjx_video_graphics_gma(pcjx_video_t *video, uint8_t viewport,
                        uint16_t start_memaddr, uint8_t mode1)
{
    if (video == NULL)
        return start_memaddr;

    (void) mode1;

    if (!video->graphics.valid[viewport])
        return start_memaddr;

    return video->graphics.gma[viewport];
}

static void
pcjx_video_begin_non_extended_display(pcjx_video_t *video,
                                      uint16_t start_memaddr)
{
    if (video == NULL)
        return;

    for (uint8_t viewport = 0; viewport < 2; viewport++) {
        if (video->graphics.valid[viewport])
            continue;

        video->graphics.gma[viewport]      = start_memaddr;
        video->graphics.gma_base[viewport] = start_memaddr;
        video->graphics.gra[viewport]      = 0;
        video->graphics.valid[viewport]    = 1;
    }
}

static void
pcjx_video_tick_graphics_state(pcjx_video_t *video, uint8_t viewport,
                               uint8_t mode1, uint8_t columns)
{
    uint8_t  repeat_count;
    uint16_t next_gma;

    if (video == NULL)
        return;

    repeat_count = pcjx_video_mode_uses_hires(mode1) ? 4 : 2;
    if (!video->graphics.valid[viewport])
        return;

    next_gma = (uint16_t) (video->graphics.gma[viewport] + columns);
    if ((video->graphics.gra[viewport] + 1) >= repeat_count) {
        video->graphics.gra[viewport]      = 0;
        video->graphics.gma_base[viewport] = next_gma;
        video->graphics.gma[viewport]      = next_gma;
        return;
    }

    video->graphics.gra[viewport]++;
    video->graphics.gma[viewport] = video->graphics.gma_base[viewport];
}

static void
pcjx_video_advance_viewport_line(pcjx_video_t *video, uint8_t viewport,
                                 uint8_t columns)
{
    uint8_t mode1;

    if (video == NULL)
        return;

    mode1 = video->gate.mode1[viewport & 0x01];
    if (mode1 & 0x02)
        pcjx_video_tick_graphics_state(video, viewport & 0x01, mode1,
                                       columns);
}

static void
pcjx_video_render_raw_cell(const pcjx_video_t *video, uint8_t viewport,
                           const pcjx_video_viewport_render_state_t *state,
                           uint8_t column, uint8_t *pixels)
{
    pcjx_video_text_render_state_t text_state;
    uint16_t text_memaddr;
    uint8_t  mode1;
    uint8_t  mode2;

    if ((video == NULL) || (state == NULL) || (pixels == NULL))
        return;

    mode1 = video->gate.mode1[viewport];
    mode2 = video->gate.mode2[viewport];
    text_memaddr = (uint16_t) (state->text_memaddr + column);

    text_state.blink_phase = state->blink_phase;
    text_state.mode1       = mode1;
    text_state.mode2       = mode2;
    text_state.ra          = state->text_ra;
    text_state.cursor      = (uint8_t) ((text_memaddr == state->cursoraddr) &&
                                   video->host.cursorvisible &&
                                   video->host.cursoron);

    pcjx_video_fill_raw_pixels(pixels, 16, 0x00);

    if (!(mode1 & 0x02) && viewport && (mode1 & 0x40))
        pcjx_video_render_raw_text_kanji(video,
                                         text_memaddr,
                                         &text_state,
                                         pixels);
    else if (!(mode1 & 0x02))
        pcjx_video_render_raw_text_25(video, viewport,
                                      text_memaddr,
                                      &text_state,
                                      pixels);
    else
        pcjx_video_render_raw_graphics(video, viewport,
                                       (uint16_t) (state->graphics_gma + column),
                                       state->graphics_gra,
                                       mode1, mode2, pixels);
}

static uint8_t
pcjx_video_resolve_cell_pixel(const pcjx_video_t *video,
                              uint8_t superimpose_mode,
                              uint8_t enable1, uint8_t enable2,
                              const uint8_t *primary_cell,
                              const uint8_t *secondary_cell, uint8_t pixel)
{
    uint8_t pixel1;
    uint8_t pixel2;

    if ((video == NULL) || (primary_cell == NULL) || (secondary_cell == NULL))
        return 0;

    pixel1 = primary_cell[pixel] & 0x0f;
    pixel2 = secondary_cell[pixel] & 0x0f;

    switch (superimpose_mode) {
        case 0x00:
            return pixel1;

        case 0x01:
            return pixel2;

        default:
            break;
    }

    if (!enable1)
        pixel1 = 0;
    if (!enable2)
        pixel2 = 0;

    switch (superimpose_mode) {
        case 0x02:
            return (pixel1 == (video->gate.trans_palette & 0x0f)) ? pixel2 : pixel1;

        case 0x03:
            return (pixel1 != (video->gate.trans_palette & 0x0f)) ? pixel2 : pixel1;

        case 0x06:
            if (video->gate.mode1[1] & 0x80)
                return (uint8_t) (((pixel1 | (pixel2 << 2)) ^ 0x0a) & 0x0f);
            break;

        default:
            break;
    }

    if ((superimpose_mode == 0x04) || (superimpose_mode == 0x05) ||
        (superimpose_mode == 0x06) || (superimpose_mode == 0x07))
        return (pixel1 ^ pixel2) & 0x0f;

    if ((superimpose_mode >= 0x08) && (superimpose_mode <= 0x0b))
        return (pixel1 & pixel2) & 0x0f;

    if (superimpose_mode >= 0x0c)
        return (pixel1 | pixel2) & 0x0f;

    return pixel1;
}

static void
pcjx_video_render_non_extended_cells(const pcjx_video_t *video, int line,
                                     int ho_d, uint8_t cell_width,
                                     uint8_t superimpose_mode,
                                     uint8_t enable1, uint8_t enable2,
                                     const pcjx_video_viewport_render_state_t *viewport_state)
{
    uint8_t columns;

    if ((video == NULL) || (viewport_state == NULL))
        return;

    columns = pcjx_video_columns(video);

    for (uint8_t column = 0; column < columns; column++) {
        uint8_t primary_cell[16];
        uint8_t secondary_cell[16];
        int     cell_x;

        pcjx_video_fill_raw_pixels(primary_cell, 16, 0x00);
        pcjx_video_fill_raw_pixels(secondary_cell, 16, 0x00);

        if (enable1 && (superimpose_mode != 0x01))
            pcjx_video_render_raw_cell(video, 0, &viewport_state[0],
                                       column, primary_cell);
        if (enable2 && (superimpose_mode != 0x00))
            pcjx_video_render_raw_cell(video, 1, &viewport_state[1],
                                       column, secondary_cell);

        cell_x = ho_d + (column * cell_width);
        for (uint8_t pixel = 0; pixel < cell_width; pixel++) {
            uint8_t color = pcjx_video_resolve_cell_pixel(video,
                                                          (uint8_t) superimpose_mode,
                                                          enable1,
                                                          enable2,
                                                          primary_cell,
                                                          secondary_cell,
                                                          pixel);

            buffer32->line[line][cell_x + pixel] =
                (video->gate.palette[color & 0x0f] & 0x0f) + 16;
        }
    }
}

uint8_t
pcjx_video_render_line(pcjx_video_t *video, int line, int ho_s, int ho_d)
{
    uint16_t start_memaddr;
    uint16_t cursoraddr;
    uint8_t  superimpose_mode;
    uint8_t  blink_phase;
    uint8_t  enable1;
    uint8_t  enable2;
    uint8_t  columns;
    uint16_t text_memaddr;
    uint16_t graphics_memaddr[2];
    uint8_t  text_ra;
    pcjx_video_viewport_render_state_t viewport_state[2];
    uint8_t  cell_width;
    uint16_t render_width;

    if (video == NULL)
        return 0;

    if (pcjx_video_render_extended(video, line, ho_d))
        return 1;

    start_memaddr    = video->host.memaddr;
    cursoraddr       = pcjx_video_cursoraddr(video);
    blink_phase      = video->host.blink & 0x10;
    superimpose_mode = video->gate.superimpose & 0x0f;
    enable1          = pcjx_video_viewport_output_enabled(video, 0);
    enable2          = pcjx_video_viewport_output_enabled(video, 1);
    columns          = pcjx_video_columns(video);
    cell_width       = pcjx_video_cell_width(video, superimpose_mode);
    render_width     = columns * cell_width;
    if ((cell_width <= 0) || (render_width <= 0) || (render_width > 2048)) {
        video->host.memaddr = (uint16_t) (video->host.memaddr + columns);
        return 1;
    }

    pcjx_video_begin_non_extended_display(video, start_memaddr);
    text_memaddr       = start_memaddr;
    graphics_memaddr[0] = pcjx_video_graphics_gma(video, 0,
                                                  start_memaddr,
                                                  video->gate.mode1[0]);
    graphics_memaddr[1] = pcjx_video_graphics_gma(video, 1,
                                                  start_memaddr,
                                                  video->gate.mode1[1]);
    text_ra            = (uint8_t) (video->host.scanline & 0x1f);

    viewport_state[0].text_memaddr     = text_memaddr;
    viewport_state[0].graphics_gma     = graphics_memaddr[0];
    viewport_state[0].cursoraddr       = cursoraddr;
    viewport_state[0].blink_phase      = blink_phase;
    viewport_state[0].graphics_gra     = video->graphics.gra[0];
    viewport_state[0].text_ra          = text_ra;
    viewport_state[1].text_memaddr     = text_memaddr;
    viewport_state[1].graphics_gma     = graphics_memaddr[1];
    viewport_state[1].cursoraddr       = cursoraddr;
    viewport_state[1].blink_phase      = blink_phase;
    viewport_state[1].graphics_gra     = video->graphics.gra[1];
    viewport_state[1].text_ra          = text_ra;

    hline(buffer32, 0, line, render_width + ho_s,
          (video->gate.border_color & 0x0f) + 16);

    if ((!enable1 && (superimpose_mode == 0x00)) ||
        (!enable2 && (superimpose_mode == 0x01))) {
        pcjx_video_advance_viewport_line(video, 0, columns);
        pcjx_video_advance_viewport_line(video, 1, columns);
        video->host.memaddr = (uint16_t) (start_memaddr + columns);
        return 1;
    }

    pcjx_video_render_non_extended_cells(video, line, ho_d, cell_width,
                                         superimpose_mode, enable1, enable2,
                                         viewport_state);

    if (render_width > 0)
        video->last_color = (uint8_t) ((buffer32->line[line][ho_d] - 16) & 0x0f);

    pcjx_video_advance_viewport_line(video, 0, columns);
    pcjx_video_advance_viewport_line(video, 1, columns);
    video->host.memaddr = (uint16_t) (start_memaddr + columns);
    return 1;
}

static uint8_t
pcjx_video_extended_text_byte(const pcjx_video_t *video, uint32_t offset)
{
    if (video == NULL)
        return 0;

    return video->vram2[offset & 0xffff];
}

static void
pcjx_video_begin_extended_display(pcjx_video_t *video, uint16_t start_memaddr)
{
    if (video == NULL)
        return;

    if (!video->ex_text.valid && !video->ex_graphics_valid)
        video->ex_text.framecount++;

    if (!video->ex_text.valid)
        video->ex_text.valid = 1;

    if (!video->ex_graphics_valid) {
        video->ex_graphics_gma      = start_memaddr;
        video->ex_graphics_gma_base = start_memaddr;
        video->ex_graphics_gra      = 0;
        video->ex_graphics_valid    = 1;
    }
}

static uint8_t
pcjx_video_extended_text_scanline(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return (uint8_t) (video->host.scanline & 0x1f);
}

static uint8_t
pcjx_video_extended_text_blink_phase(const pcjx_video_t *video)
{
    if (video == NULL)
        return 0;

    return (video->ex_text.framecount & 0x3f) < 0x10;
}

static void
pcjx_video_fill_extended_cell(int line, int ef_x, uint8_t width, uint8_t color)
{
    for (uint8_t pixel = 0; pixel < width; pixel++)
        buffer32->line[line][ef_x + pixel] = color + 16;
}

static void
pcjx_video_decode_extended_text_attr(const pcjx_video_t *video, uint8_t attr,
                                     uint8_t blink_phase, uint8_t *bg,
                                     uint8_t *fg, uint8_t *line_color,
                                     uint8_t *hidechar)
{
    if ((video == NULL) || (bg == NULL) || (fg == NULL) ||
        (line_color == NULL) || (hidechar == NULL))
        return;

    *line_color = video->ex.regs[5] & 0x0f;
    *hidechar   = 0;

    if (video->ex.regs[5] & 0x80) {
        uint8_t color = ((video->ex.regs[5] & 0x40) ? 8 : 0) |
                        ((attr & 0x08) ? 0 : 4) |
                        ((attr & 0x40) ? 0 : 2) |
                        ((attr & 0x80) ? 0 : 1);

        if (attr & 0x04) {
            *bg = color;
            *fg = (video->ex.regs[7] >> 4) & 0x0f;
        } else {
            *bg = (video->ex.regs[7] >> 4) & 0x0f;
            *fg = color;
        }
        return;
    }

    if (attr & 0x04) {
        *bg = 0x07;
        *fg = 0x00;
    } else {
        *bg = 0x00;
        *fg = 0x07;
    }

    if (attr & 0x08) {
        *bg |= 0x08;
        *fg |= 0x08;
    }

    if ((attr & 0x80) && blink_phase)
        *hidechar = 1;
    *line_color = *fg;
}

static void
pcjx_video_render_extended_text_glyph(int line, int ef_x, uint8_t attr,
                                      uint8_t glyph, uint8_t bg, uint8_t fg)
{
    uint8_t shift;

    shift = (attr & 0x02) ? 0 : 1;
    for (uint8_t pixel = 0; pixel < 8; pixel++) {
        uint8_t color = (glyph & (1 << (7 - pixel))) ? fg : bg;

        buffer32->line[line][ef_x + pixel + shift] = color + 16;
    }
}

static uint32_t
pcjx_video_extended_text_glyph_addr(const pcjx_video_t *video, uint16_t memaddr,
                                    uint8_t chr, uint8_t attr)
{
    uint32_t addr;

    if ((video == NULL) || !(attr & 0x01))
        return (uint32_t) chr << 5;

    if (!(attr & 0x02)) {
        uint8_t next_chr = pcjx_video_extended_text_byte(video,
                                                         (((uint32_t) memaddr + 1) << 1) & 0x3fff);
        uint16_t code    = (uint16_t) ((chr << 8) | next_chr);

        if (code >= 0xf000)
            code = (uint16_t) ((code & 0x3f) | 0x8400);
        addr = ((uint32_t) (code & 0x1fff) << 5);
    } else {
        uint8_t prev_chr = pcjx_video_extended_text_byte(video,
                                                         (((uint32_t) (memaddr - 1)) << 1) & 0x3fff);
        uint16_t code    = (uint16_t) ((prev_chr << 8) | chr);

        if (code >= 0xf000)
            code = (uint16_t) ((code & 0x3f) | 0x8400);
        addr = (((uint32_t) (code & 0x1fff) << 5) | 0x01);
    }

    return addr;
}

static uint8_t
pcjx_video_extended_text_glyph_row(const pcjx_video_t *video,
                                   uint32_t glyph_addr, uint8_t scanline)
{
    if ((scanline < 2) || (scanline >= 18))
        return 0x00;

    return pcjx_video_font_byte(video,
                                glyph_addr | ((uint32_t) (scanline - 2) << 1));
}

static uint8_t
pcjx_video_render_extended_text_cell(const pcjx_video_t *video, int line,
                                     int ho_d, uint16_t memaddr,
                                     const pcjx_video_extended_text_render_state_t *state,
                                     uint8_t prev_drawcursor)
{
    uint32_t      voff;
    uint8_t       chr;
    uint8_t       attr;
    uint32_t      glyph_addr;
    uint8_t       glyph;
    uint8_t       bg;
    uint8_t       fg;
    uint8_t       line_color;
    uint8_t       hidechar = 0;
    uint8_t       drawcursor;
    int16_t       ef_x;
    uint8_t       render_cursor;

    if ((video == NULL) || (state == NULL))
        return 0;

    voff       = (uint32_t) ((memaddr << 1) & 0x3fff);
    chr        = pcjx_video_extended_text_byte(video, voff);
    attr       = pcjx_video_extended_text_byte(video, voff | 1);
    drawcursor = (memaddr == state->cursoraddr) && video->host.cursorvisible && video->host.cursoron;
    render_cursor = drawcursor || (prev_drawcursor && !!(video->ex.regs[2] & 0x20));
    ef_x       = (((int) memaddr - (int) state->base_memaddr) * 9) + ho_d;

    pcjx_video_decode_extended_text_attr(video, attr, state->blink_phase,
                                         &bg, &fg, &line_color, &hidechar);
    glyph_addr = pcjx_video_extended_text_glyph_addr(video, memaddr, chr, attr);
    glyph = hidechar ? 0x00 :
            pcjx_video_extended_text_glyph_row(video, glyph_addr,
                                               state->scanline);
    pcjx_video_fill_extended_cell(line, ef_x, 9, bg);
    pcjx_video_render_extended_text_glyph(line, ef_x, attr, glyph, bg, fg);
    if (attr & 0x10)
        buffer32->line[line][ef_x] = line_color + 16;

    if ((attr & 0x20) && (state->scanline == 0))
        pcjx_video_fill_extended_cell(line, ef_x, 9, line_color);
    else if ((attr & 0x40) && !(video->ex.regs[5] & 0x80) &&
             (state->scanline == 18))
        pcjx_video_fill_extended_cell(line, ef_x, 9, fg);

    if (render_cursor) {
        if (!(video->ex.regs[5] & 0x80) && (video->ex.regs[2] & 0x01)) {
            for (uint8_t pixel = 0; pixel < 9; pixel++)
                buffer32->line[line][ef_x + pixel] ^= 0x0f;
        } else
            pcjx_video_fill_extended_cell(line, ef_x, 9, fg);
    }

    return drawcursor;
}

static void
pcjx_video_write_gate_array_view(pcjx_video_t *video, uint8_t viewport, uint8_t val)
{
    if (viewport == 0) {
        if (!video->gate.latched[0]) {
            video->gate.index[0]   = val;
            video->gate.status_index = val;
            video->gate.latched[0] = 1;
        } else {
            video->gate.latched[0] = 0;
            pcjx_video_apply_gate_array_reg(video, 0, video->gate.index[0], val);
        }
        return;
    }

    if (!video->gate.latched[1]) {
        video->gate.index[1]   = val;
        video->gate.status_index = val;
        video->gate.latched[1] = 1;
    } else {
        video->gate.latched[1] = 0;
        pcjx_video_apply_gate_array_reg(video, 1, video->gate.index[1], val);
    }
}

static uint8_t
pcjx_video_write_gate_array(pcjx_video_t *video, uint8_t val)
{
    uint8_t gate_mask;

    gate_mask = pcjx_video_gate_active_mask(video);
    if (gate_mask == 0)
        return 1;

    if (gate_mask & 0x01)
        pcjx_video_write_gate_array_view(video, 0, val);
    if (gate_mask & 0x02)
        pcjx_video_write_gate_array_view(video, 1, val);

    return 1;
}


static void
pcjx_video_render_extended_graphics_word_unicolor(const pcjx_video_t *video,
                                                  int line, int ef_x,
                                                  uint16_t data)
{
    uint8_t base_color = (video->ex.regs[5] & 0x40) ? 8 : 0;

    for (uint8_t pixel = 0; pixel < 16; pixel++, data <<= 1)
        buffer32->line[line][ef_x + pixel] = (((data & 0x8000) ? 7 : 0) | base_color) + 16;
}

static void
pcjx_video_render_extended_graphics_word_2color(const pcjx_video_t *video,
                                                int line, int ef_x,
                                                uint16_t data)
{
    for (uint8_t pixel = 0; pixel < 16; pixel++, data <<= 1)
        buffer32->line[line][ef_x + pixel] = (video->ex.palette[(data >> 15) & 0x01] & 0x0f) + 16;
}

static void
pcjx_video_render_extended_graphics_word_4color(const pcjx_video_t *video,
                                                int line, int ef_x,
                                                uint16_t data)
{
    for (uint8_t pixel = 0; pixel < 16; pixel += 2, data <<= 2) {
        uint8_t color = video->ex.palette[(data >> 14) & 0x03] & 0x0f;

        buffer32->line[line][ef_x + pixel]     = color + 16;
        buffer32->line[line][ef_x + pixel + 1] = color + 16;
    }
}

static void
pcjx_video_render_extended_graphics_word(const pcjx_video_t *video, int line,
                                         int ef_x, uint16_t data)
{
    if (!(video->ex.regs[5] & 0x80)) {
        pcjx_video_render_extended_graphics_word_unicolor(video, line, ef_x,
                                                          data);
        return;
    }

    if (video->ex.regs[6] & 0x08) {
        pcjx_video_render_extended_graphics_word_2color(video, line, ef_x,
                                                        data);
        return;
    }

    pcjx_video_render_extended_graphics_word_4color(video, line, ef_x, data);
}

static void
pcjx_video_render_extended_graphics_cell(const pcjx_video_t *video, int line,
                                         int ho_d, uint8_t column,
                                         uint16_t graphics_gma,
                                         uint8_t graphics_gra)
{
    uint32_t bank_base;
    uint32_t voff;
    uint16_t data;
    int16_t  ef_x;

    if (video == NULL)
        return;

    bank_base = ((((uint32_t) graphics_gra << 15) & 0x8000U) ^ 0x8000U);
    voff      = (bank_base | (((uint32_t) (graphics_gma + column) << 1) & 0x7fffU)) & 0xffffU;
    data      = (uint16_t) ((video->vram2[voff] << 8) |
                            video->vram2[(voff + 1) & 0xffff]);
    ef_x      = ho_d + (column * 16);

    pcjx_video_render_extended_graphics_word(video, line, ef_x, data);
}

static uint8_t
pcjx_video_render_extended_cell(const pcjx_video_t *video, int line, int ho_d,
                                uint8_t column, uint8_t graphics_mode,
                                const pcjx_video_extended_render_state_t *state,
                                uint8_t prev_drawcursor)
{
    pcjx_video_extended_text_render_state_t text_state;

    if ((video == NULL) || (state == NULL))
        return prev_drawcursor;

    if (graphics_mode) {
        pcjx_video_render_extended_graphics_cell(video, line, ho_d, column,
                                                 state->graphics_gma,
                                                 state->graphics_gra);
        return prev_drawcursor;
    }

    text_state.base_memaddr = state->base_memaddr;
    text_state.cursoraddr   = state->cursoraddr;
    text_state.blink_phase  = state->blink_phase;
    text_state.scanline     = state->scanline;

    return pcjx_video_render_extended_text_cell(video, line, ho_d,
                                                (uint16_t) (state->base_memaddr + column),
                                                &text_state,
                                                prev_drawcursor);
}

static uint16_t
pcjx_video_extended_graphics_gma(pcjx_video_t *video, uint16_t start_memaddr)
{
    if ((video == NULL) || !video->ex_graphics_valid)
        return start_memaddr;

    return video->ex_graphics_gma;
}

static void
pcjx_video_tick_extended_graphics_state(pcjx_video_t *video, uint8_t columns)
{
    uint16_t next_gma;

    if ((video == NULL) || !video->ex_graphics_valid)
        return;

    next_gma = (uint16_t) (video->ex_graphics_gma + columns);
    if ((video->ex_graphics_gra + 1) >= 2) {
        video->ex_graphics_gra      = 0;
        video->ex_graphics_gma_base = next_gma;
        video->ex_graphics_gma      = next_gma;
        return;
    }

    video->ex_graphics_gra++;
    video->ex_graphics_gma = video->ex_graphics_gma_base;
}


uint8_t
pcjx_video_render_extended(pcjx_video_t *video, int line, int ho_d)
{
    pcjx_video_extended_render_state_t state;
    uint16_t start_memaddr;
    uint16_t cursoraddr;
    uint8_t  scanline;
    uint8_t  blink_phase;
    uint8_t  graphics_mode;
    uint8_t  prev_drawcursor = 0;
    uint8_t  columns;
    uint16_t width;

    if (!pcjx_video_is_extended_active(video))
        return 0;

    width = pcjx_video_extended_render_width(video);
    if ((video == NULL) || (width <= 0))
        return 1;

    columns      = pcjx_video_columns(video);
    start_memaddr = video->host.memaddr;
    cursoraddr    = pcjx_video_cursoraddr(video);
    scanline      = pcjx_video_extended_text_scanline(video);
    blink_phase   = pcjx_video_extended_text_blink_phase(video);
    graphics_mode = pcjx_video_extended_graphics_mode(video);
    pcjx_video_begin_extended_display(video, start_memaddr);

    state.base_memaddr = start_memaddr;
    state.cursoraddr   = cursoraddr;
    state.blink_phase  = blink_phase;
    state.scanline     = scanline;
    state.graphics_gma = pcjx_video_extended_graphics_gma(video, start_memaddr);
    state.graphics_gra = video->ex_graphics_gra;

    hline(buffer32, 0, line, width + ho_d, (video->gate.border_color & 0x0f) + 16);
    video->last_color = video->gate.border_color & 0x0f;
    if (!pcjx_video_extended_video_enabled(video)) {
        if (graphics_mode) {
            pcjx_video_tick_extended_graphics_state(video, columns);
            video->host.memaddr = video->ex_graphics_gma;
        } else
            video->host.memaddr = (uint16_t) (start_memaddr + columns);
        return 1;
    }

    for (uint8_t column = 0; column < columns; column++)
        prev_drawcursor = pcjx_video_render_extended_cell(video, line, ho_d,
                                                          column, graphics_mode,
                                                          &state,
                                                          prev_drawcursor);

    if (graphics_mode) {
        pcjx_video_tick_extended_graphics_state(video, columns);
        video->host.memaddr = video->ex_graphics_gma;
    } else
        video->host.memaddr = (uint16_t) (start_memaddr + columns);

    if (width > 0)
        video->last_color = (uint8_t) ((buffer32->line[line][ho_d] - 16) & 0x0f);

    return 1;
}

uint8_t
pcjx_video_out(pcjx_video_t *video, uint16_t addr, uint8_t val)
{
    if (video == NULL)
        return 0;

    switch (addr) {
        case 0x3d0:
        case 0x3d1:
        case 0x3d2:
        case 0x3d3:
        case 0x3d4:
        case 0x3d5:
        case 0x3d6:
        case 0x3d7:
            if (!pcjx_video_control_io_enabled(video, PCJX_VIDEO_IO_CRTC))
                return 1;
            break;

        default:
            break;
    }

    switch (addr) {
        case 0x3da:
            return pcjx_video_write_gate_array(video, val);

        case 0x3d9:
            if (!pcjx_video_control_io_enabled(video, PCJX_VIDEO_IO_PAGE2))
                return 1;
            video->page_reg[1] = val;
            pcjx_video_reset_extended_graphics_state(video);
            pcjx_video_sync_active_view(video);
            return 1;

        case 0x3dd:
            if (!pcjx_video_control_io_enabled(video, PCJX_VIDEO_IO_EX)) {
                video->ex.latched = 0;
                return 1;
            }

            if (!video->ex.latched) {
                video->ex.index   = val;
                video->ex.latched = 1;
            } else {
                video->ex.latched = 0;
                pcjx_video_apply_extended_reg(video, video->ex.index, val);
            }
            return 1;

        case 0x3df:
            if (!pcjx_video_control_io_enabled(video, PCJX_VIDEO_IO_PAGE1))
                return 1;
            video->page_reg[0] = val;
            pcjx_video_reset_extended_graphics_state(video);
            pcjx_video_sync_active_view(video);
            return 1;

        default:
            break;
    }

    return 0;
}

uint8_t
pcjx_video_in(pcjx_video_t *video, uint16_t addr, uint8_t *val)
{
    uint8_t gate_mask;

    if ((video == NULL) || (val == NULL))
        return 0;

    switch (addr) {
        case 0x3d0:
        case 0x3d1:
        case 0x3d2:
        case 0x3d3:
        case 0x3d4:
        case 0x3d5:
        case 0x3d6:
        case 0x3d7:
            if (!pcjx_video_control_io_enabled(video, PCJX_VIDEO_IO_CRTC)) {
                *val = 0xff;
                return 1;
            }
            break;

        default:
            break;
    }

    if (addr == 0x3da) {
        gate_mask = pcjx_video_gate_active_mask(video);
        if (gate_mask == 0) {
            *val = 0xff;
            return 1;
        }

        if (gate_mask & 0x01)
            video->gate.latched[0] = 0;
        if (gate_mask & 0x02)
            video->gate.latched[1] = 0;

        (void) pcjx_video_gate_status_viewport(gate_mask);
        *val = pcjx_video_gate_array_status(video);
        return 1;
    }

    if (addr == 0x3dd) {
        if (!pcjx_video_control_io_enabled(video, PCJX_VIDEO_IO_EX)) {
            video->ex.latched = 0;
            *val              = 0xff;
            return 1;
        }

        video->ex.latched = 0;
        *val              = pcjx_video_read_extended_reg(video);
        return 1;
    }

    return 0;
}