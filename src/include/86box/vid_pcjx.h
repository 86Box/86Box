#pragma once

#include <stdint.h>

#include <86box/mem.h>

typedef struct pcjx_video_host_state_s {
    uint8_t  crtc[32];
    uint8_t  status;
    uint16_t memaddr;
    int      scanline;
    int      blink;
    int      cursorvisible;
    int      cursoron;
} pcjx_video_host_state_t;

typedef struct pcjx_gate_array_s {
    uint8_t index[2];
    uint8_t latched[2];
    uint8_t status_index;
    uint8_t mode1[2];
    uint8_t mode1_programmed[2];
    uint8_t palette_mask[2];
    uint8_t mode2[2];
    uint8_t border_color;
    uint8_t reset;
    uint8_t trans_palette;
    uint8_t superimpose;
    uint8_t palette[16];
} pcjx_gate_array_t;

typedef struct pcjx_extended_video_s {
    uint8_t index;
    uint8_t latched;
    uint8_t access;
    uint8_t palette[4];
    uint8_t regs[8];
} pcjx_extended_video_t;

typedef struct pcjx_graphics_state_s {
    uint16_t gma[2];
    uint16_t gma_base[2];
    uint8_t  gra[2];
    uint8_t  valid[2];
} pcjx_graphics_state_t;

typedef struct pcjx_text_state_s {
    uint16_t row_start;
    uint8_t  ra;
    uint8_t  valid;
} pcjx_text_state_t;

typedef struct pcjx_extended_text_state_s {
    uint16_t row_start;
    uint8_t  scanline;
    uint8_t  valid;
    uint8_t  framecount;
} pcjx_extended_text_state_t;

typedef struct pcjx_display_position_s {
    int      start_x;
    int      start_y;
    int      pending_x;
    int      pending_y;
    uint8_t  pending_count;
    uint8_t  initialized;
} pcjx_display_position_t;

typedef struct pcjx_video_s {
    mem_mapping_t first_window_mapping;
    mem_mapping_t second_window_mapping;
    uint32_t      program_size;
    uint32_t      font_rom_mask;
    uint8_t       page_reg[2];
    uint8_t       gate_io_mask;
    uint8_t       control_io_mask;
    uint8_t       timings_dirty;
    uint8_t       change_requested;
    const uint8_t *font_rom;
    pcjx_video_host_state_t host;
    pcjx_gate_array_t gate;
    pcjx_extended_video_t ex;
    pcjx_text_state_t text;
    pcjx_extended_text_state_t ex_text;
    pcjx_display_position_t display;
    pcjx_graphics_state_t graphics;
    uint16_t      ex_graphics_gma;
    uint16_t      ex_graphics_gma_base;
    uint8_t       ex_graphics_gra;
    uint8_t       ex_graphics_valid;
    uint8_t       last_color;
    uint8_t       vram2[0x10000];
} pcjx_video_t;

void pcjx_video_reset_extended_graphics_state(pcjx_video_t *video);
void pcjx_video_notify_display_restart(pcjx_video_t *video);
void pcjx_video_init(pcjx_video_t *video, uint32_t program_size);
void pcjx_video_close(pcjx_video_t *video);
void pcjx_video_set_host_state(pcjx_video_t *video,
                               const pcjx_video_host_state_t *state);
void pcjx_video_get_host_state(const pcjx_video_t *video,
                               pcjx_video_host_state_t *state);
void pcjx_video_update_display_position(pcjx_video_t *video,
                                        int raw_start_x,
                                        int raw_start_y);
void pcjx_video_get_display_position(const pcjx_video_t *video,
                                     int *start_x, int *start_y);
int pcjx_video_consume_timings_dirty(pcjx_video_t *video);
int pcjx_video_consume_change_requested(pcjx_video_t *video);
void pcjx_video_apply_first_window(pcjx_video_t *video, uint32_t base,
                                   uint32_t size, int can_read,
                                   int can_write);
void pcjx_video_apply_second_window(pcjx_video_t *video, uint32_t base,
                                    uint32_t size, int can_read,
                                    int can_write);
void pcjx_video_set_font_rom(pcjx_video_t *video, const uint8_t *font_rom,
                             uint32_t font_rom_size);
void pcjx_video_set_gate_array_io_mask(pcjx_video_t *video, uint8_t io_mask);
void pcjx_video_set_control_io_mask(pcjx_video_t *video, uint8_t io_mask);
int pcjx_video_is_extended_active(const pcjx_video_t *video);
void pcjx_video_get_effective_render_mode(const pcjx_video_t *video,
                                          uint8_t *mode1, uint8_t *mode2);
int pcjx_video_render_x_bias(const pcjx_video_t *video);
int pcjx_video_render_y_bias(const pcjx_video_t *video);
double pcjx_video_timing_scale(const pcjx_video_t *video);
int pcjx_video_get_frame_blit_geometry(const pcjx_video_t *video,
                                       int firstline, int render_ho_d,
                                       int double_type, int *blit_x,
                                       int *blit_y, int *frame_width,
                                       int *frame_height);
int pcjx_video_extended_render_width(const pcjx_video_t *video);
int pcjx_video_render_extended(pcjx_video_t *video, int line, int ho_d);
int pcjx_video_render_line(pcjx_video_t *video, int line, int ho_s, int ho_d);
int pcjx_video_needs_combined_render(const pcjx_video_t *video);
uint8_t pcjx_video_combine_pixels(const pcjx_video_t *video, uint8_t pixel1,
                                  uint8_t pixel2);
uint8_t pcjx_video_blank_color(const pcjx_video_t *video);
void pcjx_video_b800_write(pcjx_video_t *video, uint32_t addr, uint8_t val);
uint8_t pcjx_video_b800_read(const pcjx_video_t *video, uint32_t addr);
int  pcjx_video_out(pcjx_video_t *video, uint16_t addr, uint8_t val);
int  pcjx_video_in(pcjx_video_t *video, uint16_t addr, uint8_t *val);