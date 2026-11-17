/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef VIDEO_VERITE_CORE_H
#define VIDEO_VERITE_CORE_H

typedef struct verite_core verite_core;

typedef struct verite_board {
    uint32_t (*read)(void *opaque, uint8_t reg, uint8_t len);
    void (*write)(void *opaque, uint8_t reg, uint32_t value, uint8_t len);
    uint32_t (*dma_read)(void *opaque, uint32_t address);
} verite_board;

verite_core *verite_core_new(void);
void         verite_core_free(verite_core *core);
void         verite_core_reset(verite_core *core);
uint32_t     verite_core_read(verite_core *core, uint8_t reg, uint8_t len,
                              const uint8_t *vram, size_t size);
void         verite_core_write(verite_core *core, uint8_t reg, uint32_t value, uint8_t len,
                               uint8_t *vram, size_t size, const verite_board *board, void *opaque);
void         verite_core_run(verite_core *core, uint64_t now_ns, uint8_t *vram, size_t size,
                             const verite_board *board, void *opaque);
void         verite_core_retrace(verite_core *core);
uint32_t     verite_core_status(verite_core *core);
uint32_t     verite_core_bank(const verite_core *core);
uint8_t      verite_core_mode(const verite_core *core);

#endif
