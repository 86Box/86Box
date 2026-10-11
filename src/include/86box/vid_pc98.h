/* PC-9801 planar graphics, GRCG, EGC and font-window core. */
#ifndef EMU_86BOX_VID_PC98_H
#define EMU_86BOX_VID_PC98_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <86box/vid_upd7220.h>
#include <86box/pc98_rom.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC98_GVRAM_PAGES       2u
#define PC98_GVRAM_PLANES      4u
#define PC98_GVRAM_WORDS       0x4000u
#define PC98_IPL_WINDOW_SIZE   0x18000u
#define PC98_IPL_REGION_SIZE   0x30000u
/* Backward source compatibility: the visible CPU IPL window is 96 KiB. */
#define PC98_IPL_ROM_SIZE      PC98_IPL_WINDOW_SIZE
#define PC98_FONT_DUMP_SIZE    0x46800u
#define PC98_RAW_KANJI_SIZE    0x80000u
#define PC98_FONT_ADDRESS_SIZE 0x100000u

enum pc98_plane {
    PC98_PLANE_B = 0,
    PC98_PLANE_R = 1,
    PC98_PLANE_G = 2,
    PC98_PLANE_E = 3
};

typedef struct pc98_grcg_t {
    uint8_t mode;
    uint8_t tile[4];
    uint8_t tile_index;
} pc98_grcg_t;

typedef struct pc98_egc_t {
    uint16_t regs[8];      /* 4A0,4A2,...4AE */
    uint16_t pat[4];
    uint16_t fg[4];       /* physical foreground latches, also GRCG tiles */
    uint16_t bg[4];
    uint16_t src[4];
    uint16_t leftover[4];
    uint16_t mask;
    uint16_t cpu_data;    /* NEC CPU-source readback latch */
    int32_t count;
    uint8_t first;
    uint8_t start;
    uint8_t loaded;
} pc98_egc_t;

typedef struct pc98_video_t {
    upd7220_t text_gdc;
    upd7220_t graph_gdc;

    uint16_t gvram[PC98_GVRAM_PAGES][PC98_GVRAM_PLANES][PC98_GVRAM_WORDS];
    uint16_t tvram[0x2000];

    pc98_grcg_t grcg;
    pc98_egc_t egc;
    uint8_t egc_enabled;

    uint8_t cpu_page;
    uint8_t display_page;
    uint8_t graph_clock_fast; /* Board SW2-8 clock latch, retained on reset. */

    uint16_t font_addr;
    uint8_t font_line;
    uint8_t font_lr;
    uint8_t kac;

    uint8_t palette[4];      /* digital palette register readback */
    uint8_t palette_index;
    uint8_t analog_palette[16][3]; /* indexed RGB, 4 bits per component */
    uint8_t txt_scroll[6];
    uint8_t memsw[16];      /* A3FE2-A3FFF memory-switch bytes */
    uint8_t video_ff[8];
    uint8_t ex_video_ff[4];
    uint8_t gfx_ff;

    uint8_t *ipl_rom;      /* optional, owned by caller unless load helper used */
    size_t ipl_size;
    uint8_t *font_rom;     /* allocated as 0x100000 Kanji/CG address space */
    size_t font_dump_size;
    uint8_t owns_ipl;
    uint8_t owns_font;
} pc98_video_t;

void pc98_video_init(pc98_video_t *v);
void pc98_video_destroy(pc98_video_t *v);
void pc98_video_reset(pc98_video_t *v);

/* ROM hooks. These load user-supplied dumps; no ROM images are embedded. */
int pc98_video_load_ipl(pc98_video_t *v, const char *path);
int pc98_video_load_font(pc98_video_t *v, const char *path);
/* Install a T98Tools-style 0x46800-byte FONT.ROM dump into the native
 * 1 MiB PC-98 Kanji/CG address space used by the A4000h window. */
int pc98_video_install_font_dump(pc98_video_t *v, const uint8_t *dump, size_t size);
/* Install FONT.ROM together with MAME's assembled 0x80000-byte raw Kanji
 * region (24256c-x01.bin through x04).  The raw ROMs provide JIS symbol,
 * hiragana and katakana rows that are not present in FONT.ROM alone. */
int pc98_video_install_font_set(pc98_video_t *v, const uint8_t *dump, size_t size,
                                const uint8_t *raw_kanji, size_t raw_size);

/* PC-98 video I/O subset: 70-7f, a0-af and 4a0-4af. */
uint8_t  pc98_video_in8(pc98_video_t *v, uint16_t port);
void     pc98_video_out8(pc98_video_t *v, uint16_t port, uint8_t data);
uint16_t pc98_video_in16(pc98_video_t *v, uint16_t port);
void     pc98_video_out16(pc98_video_t *v, uint16_t port, uint16_t data);

/* Planar GVRAM access through GRCG/EGC. word_off is 0..0x3fff. */
uint16_t pc98_gvram_read16(pc98_video_t *v, unsigned plane, uint16_t word_off, uint16_t mem_mask);
void     pc98_gvram_write16(pc98_video_t *v, unsigned plane, uint16_t word_off, uint16_t data, uint16_t mem_mask);
uint8_t  pc98_gvram_read8(pc98_video_t *v, unsigned plane, uint16_t byte_off);
void     pc98_gvram_write8(pc98_video_t *v, unsigned plane, uint16_t byte_off, uint8_t data);

/* Helpers exposed for unit testing and a future 86Box memory-map wrapper. */
uint16_t pc98_egc_rop(uint8_t truth, uint16_t src, uint16_t pat, uint16_t dst);
uint32_t pc98_font_window_offset(const pc98_video_t *v);
/* Decode an A4000h CG-window offset. Ordinary Kanji use the CPU byte
 * lane for L/R; 56xx/57xx programmable characters use the A5h LR latch. */
uint32_t pc98_cg_window_offset(const pc98_video_t *v, uint32_t window_off,
                               int *writable);
/* Shared by the A4000h window and the A5h/A9h port pair; see vid_pc98.c.
 * `writable` reports whether the selection is the programmable area. */
uint32_t pc98_cg_offset(const pc98_video_t *v, uint32_t line, uint32_t lr,
                        int *writable);

#ifdef EMU_DEVICE_H
extern const device_t pc9801_video_device;
extern const device_t pc9801bx4_video_device;
extern int pc9801_video_firmware_available(void);
extern int pc9801_video_firmware_available_for_model(pc98_model_t model);
pc98_video_t *pc9801_video_get_core(void *priv);
pc98_model_t pc9801_video_get_model(void *priv);
/* Any write at motherboard port 64h rearms the next CRTV interrupt. */
void pc9801_video_vrtc_reset(void *priv);
#endif

#ifdef __cplusplus
}
#endif
#endif
