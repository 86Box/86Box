/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header files for the Tandy keyboard and video subsystems.
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com>
 *
 *          Copyright 2025 starfrost
 */
typedef struct t1kvid_t {
    mem_mapping_t mapping;
    mem_mapping_t vram_mapping;

    uint8_t crtc[32];
    int     crtcreg;

    int     array_index;
    uint8_t array[256];
    int     memctrl;
    uint8_t mode;
    uint8_t col;
    uint8_t status;

    uint8_t *vram;
    uint8_t *b8000;
    uint32_t b8000_mask;
    uint32_t b8000_limit;
    uint8_t  planar_ctrl;
    uint8_t  lp_strobe;

    uint8_t  baseline_hsyncpos;
    uint8_t  baseline_vsyncpos;
    bool     baseline_ready;
    int      hsync_offset;
    int      vsync_offset;
    int      vsync_offset_pending;

    int      linepos;
    int      displine;
    int      scanline;
    int      vc;
    int      dispon;
    int      cursorvisible;
    int      cursoron;
    int      blink;
    int      fullchange;
    int      vsynctime;
    int      vadj;
    int      double_type;
    uint16_t memaddr;
    uint16_t memaddr_backup;

    uint64_t   dispontime;
    uint64_t   dispofftime;
    pc_timer_t timer;
    pc_timer_t calib_timer;
    int        firstline;
    int        lastline;

    int composite;
} t1kvid_t;

typedef struct t1keep_t {
    char *path;

    int      state;
    int      count;
    int      addr;
    int      clk;
    uint16_t data;
    uint16_t store[64];
} t1keep_t;

typedef struct tandy_t {
    mem_mapping_t ram_mapping;
    mem_mapping_t rom_mapping; /* SL2 */

    uint8_t *rom; /* SL2 */
    uint8_t  ram_bank;
    uint8_t  rom_bank;   /* SL2 */
    int      rom_offset; /* SL2 */

    uint32_t base;
    uint32_t mask;
    int      is_hx;
    int      is_sl2;

    t1kvid_t *vid;
} tandy_t;

void tandy_vid_init(tandy_t* dev);
uint8_t tandy_vid_in(uint16_t addr, void* priv);
void tandy_vid_out(uint16_t addr, uint8_t val, void *priv);

void tandy_vid_close(void* priv);
void tandy_recalc_address_sl(tandy_t* dev); //this function is needed by both m_ and vid_tandy.c
