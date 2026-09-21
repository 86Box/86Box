/*
 * Clean uPD7220/82720 core for the 86Box upd7220-code branch.
 *
 * This file intentionally contains no 86Box-specific device glue.  The core is
 * callback-driven so a PC-98 front-end can give the text and graphics GDCs
 * independent VRAM spaces while still using the same command processor.
 */
#ifndef EMU_86BOX_VID_UPD7220_H
#define EMU_86BOX_VID_UPD7220_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    UPD7220_SR_DRDY  = 0x01,
    UPD7220_SR_FULL  = 0x02,
    UPD7220_SR_EMPTY = 0x04,
    UPD7220_SR_DRAW  = 0x08,
    UPD7220_SR_DMA   = 0x10,
    UPD7220_SR_VSYNC = 0x20,
    UPD7220_SR_HBLANK= 0x40,
    UPD7220_SR_LPEN  = 0x80
};

typedef uint16_t (*upd7220_vram_read_cb)(void *opaque, uint32_t word_addr);
typedef void     (*upd7220_vram_write_cb)(void *opaque, uint32_t word_addr, uint16_t value);
typedef void     (*upd7220_timing_changed_cb)(void *opaque);
typedef void     (*upd7220_signal_cb)(void *opaque, int level);

typedef struct upd7220_partition_t {
    uint32_t sad;
    uint16_t len;
    uint8_t  im;
    uint8_t  wd;
} upd7220_partition_t;

typedef struct upd7220_figs_t {
    uint8_t  dir;
    uint8_t  type;
    uint8_t  gd;
    uint16_t dc;
    uint16_t d;
    uint16_t d1;
    uint16_t d2;
    uint16_t dm;
} upd7220_figs_t;

typedef struct upd7220_t {
    void *opaque;
    upd7220_vram_read_cb vram_read;
    upd7220_vram_write_cb vram_write;
    upd7220_timing_changed_cb timing_changed;
    upd7220_signal_cb drq_changed;

    /* command/status path */
    uint8_t sr;
    uint8_t cr;
    uint8_t pr[17];
    uint8_t param_ptr;
    uint8_t command_active;

    /* 16-byte hardware FIFO, represented as a real ring */
    uint8_t fifo[16];
    int8_t  fifo_flag[16];
    uint8_t fifo_head;
    uint8_t fifo_tail;
    uint8_t fifo_count;
    uint8_t fifo_dir;

    /* parameter RAM and execution registers */
    uint8_t  ra[16];
    uint8_t  ra_addr;
    uint16_t pattern;
    uint16_t mask;
    uint16_t pitch;
    uint32_t ead;
    uint32_t lad;
    upd7220_figs_t figs;

    /* display/timing state decoded from RESET/SYNC */
    uint8_t mode;
    uint8_t de;
    uint8_t master;
    uint16_t aw;
    uint16_t al;
    uint16_t hs;
    uint16_t hfp;
    uint16_t hbp;
    uint16_t vs;
    uint16_t vfp;
    uint16_t vbp;

    /* cursor/character state */
    uint8_t dc;
    uint8_t sc;
    uint8_t br;
    uint8_t ctop;
    uint8_t cbot;
    uint8_t lr;

    /* zoom */
    uint8_t disp;
    uint8_t gchr;

    /* current data-transfer mode */
    uint8_t bitmap_mod;
    uint8_t transfer_type;
    uint32_t transfer_words_left;

    /* DMA */
    uint8_t dma_type;
    uint8_t dma_mod;
    uint8_t dma_phase;
    uint16_t dma_data;
    uint32_t dma_bytes_left;

    /* light pen edge tracking */
    uint8_t lpen_level;

    /* command processor busy time in 2xWCLK periods */
    uint32_t busy_clocks;
} upd7220_t;

void upd7220_init(upd7220_t *gdc, void *opaque,
                  upd7220_vram_read_cb rd, upd7220_vram_write_cb wr);
void upd7220_reset(upd7220_t *gdc);

/* offset 0 = parameter/status; offset 1 = command/FIFO data */
void    upd7220_write(upd7220_t *gdc, unsigned offset, uint8_t data);
uint8_t upd7220_read(upd7220_t *gdc, unsigned offset);

/* DMA acknowledge side of DMAW/DMAR. */
void    upd7220_dack_write(upd7220_t *gdc, uint8_t data);
uint8_t upd7220_dack_read(upd7220_t *gdc);

void upd7220_lpen(upd7220_t *gdc, int level, uint32_t current_word_addr);
void upd7220_set_retrace(upd7220_t *gdc, int hblank, int vsync);
void upd7220_advance(upd7220_t *gdc, uint32_t two_x_wclk_periods);

void upd7220_get_graphics_partition(const upd7220_t *gdc, unsigned index,
                                    upd7220_partition_t *out);
void upd7220_get_text_partition(const upd7220_t *gdc, unsigned index,
                                upd7220_partition_t *out);

#ifdef __cplusplus
}
#endif
#endif
