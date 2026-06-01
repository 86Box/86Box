/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATi Mach64 graphics card emulation headers.
 *          The ATi Mach64 is a 1994 Windows 2D accelerator.
 *          Technical information is available at: https://bitsavers.org/components/ati/RRG-S00700-05_mach64_Register_Reference_Guide_1999410.pdf
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Connor Hyde, <mario64crashed@gmail.com> <https://starfrost.net>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2026 Connor Hyde.
 */


#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdatomic.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_xga.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_ati_eeprom.h>
#include <86box/bswap.h>

#ifdef CLAMP
#    undef CLAMP
#endif

#define BIOS_ROM_PATH     "roms/video/mach64/bios.bin"
#define BIOS_ISA_ROM_PATH "roms/video/mach64/M64-1994.VBI"
#define BIOS_VLB_ROM_PATH "roms/video/mach64/mach64_vlb_vram.bin"
#define BIOS_ROMCT_PATH   "roms/video/mach64/mach64-68b110b8cddfd546595673.bin"
#define BIOS_ROMVT_PATH   "roms/video/mach64/mach64vt-660c60c135839345779942.bin"
#define BIOS_ROMVT2_PATH  "roms/video/mach64/atimach64vt2pci.bin"

#define FIFO_SIZE         65536
#define FIFO_MASK         (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE   (1 << 31)

#define FIFO_ENTRIES      (mach64->fifo_write_idx - mach64->fifo_read_idx)
#define FIFO_FULL         ((mach64->fifo_write_idx - mach64->fifo_read_idx) >= FIFO_SIZE)
#define FIFO_EMPTY        (mach64->fifo_read_idx == mach64->fifo_write_idx)

#define FIFO_TYPE         0xff000000
#define FIFO_ADDR         0x00ffffff

enum {
    FIFO_INVALID     = (0x00 << 24),
    FIFO_WRITE_BYTE  = (0x01 << 24),
    FIFO_WRITE_WORD  = (0x02 << 24),
    FIFO_WRITE_DWORD = (0x03 << 24)
};

enum
{
    MACH64_IO_BASE_2EC = 0x02EC,    // Sparse I/O Base Address is 0x02ec
    MACH64_IO_BASE_1CC = 0x01CC,    // Sparse I/O Base Address is 0x01cc
    MACH64_IO_BASE_1C8 = 0x01C8,    // Sparse I/O Base Address is 0x01c8
    MACH64_IO_BASE_INVALID
};

typedef struct fifo_entry_t {
    uint32_t addr_type;
    uint32_t val;
} fifo_entry_t;

enum {
    MACH64_GX = 0,
    MACH64_CT,
    MACH64_VT,
    MACH64_VT2,
    MACH64_VT3
};

#define MACH64_FLAG_ONBOARD (1 << 19)
#define MACH64_PCI_IOCONFIG 0x40        // "User Defined Configuration"

typedef struct mach64_t {
    mem_mapping_t linear_mapping;
    mem_mapping_t mmio_mapping;
    mem_mapping_t linear_mapping_big_endian;
    mem_mapping_t mmio_linear_mapping;
    mem_mapping_t mmio_linear_mapping_2;

    ati_eeprom_t eeprom;
    svga_t       svga;

    rom_t bios_rom;

    uint8_t regs[256];
    int     index;

    int type;
    int pci;
    int vlb;

    uint8_t pci_slot;
    uint8_t irq_state;

    uint8_t on_board;

    uint8_t pci_regs[256];
    uint8_t int_line;

    int bank_r[2];
    int bank_w[2];

    uint32_t vram_size;
    uint32_t vram_mask;

    uint32_t config_cntl;

    uint32_t context_load_cntl;
    uint32_t context_mask;

    uint32_t crtc_gen_cntl;
    uint8_t  crtc_int_cntl;
    uint32_t crtc_h_sync_strt_wid;
    uint32_t crtc_h_total_disp;
    uint32_t crtc_v_sync_strt_wid;
    uint32_t crtc_v_total_disp;
    uint32_t crtc_off_pitch;

    uint32_t clock_cntl;

    uint32_t clr_cmp_clr;
    uint32_t clr_cmp_cntl;
    uint32_t clr_cmp_mask;

    uint32_t cur_horz_vert_off;
    uint32_t cur_horz_vert_posn;
    uint32_t cur_offset;

    uint32_t gp_io;

    uint32_t dac_cntl;

    uint32_t dp_bkgd_clr;
    uint32_t dp_frgd_clr;
    uint32_t dp_mix;
    uint32_t dp_pix_width;
    uint32_t dp_src;
    uint32_t dp_set_gui_engine;

    uint32_t dst_bres_lnth;
    uint32_t dst_bres_dec;
    uint32_t dst_bres_err;
    uint32_t dst_bres_inc;

    uint32_t dst_cntl;
    uint32_t dst_height_width;
    uint32_t dst_off_pitch;
    uint32_t dst_y_x;

    uint32_t gen_test_cntl;

    uint32_t gui_traj_cntl;

    uint32_t host_cntl;

    uint32_t mem_cntl;

    uint32_t ovr_clr;
    uint32_t ovr_wid_left_right;
    uint32_t ovr_wid_top_bottom;

    uint32_t pat_cntl;
    uint32_t pat_reg0;
    uint32_t pat_reg1;

    uint32_t sc_left_right;
    uint32_t sc_top_bottom;

    uint32_t scratch_reg0;
    uint32_t scratch_reg1;
    uint32_t scratch_reg2;
    uint32_t scratch_reg3;

    uint32_t src_cntl;
    uint32_t src_off_pitch;
    uint32_t src_y_x;
    uint32_t src_y_x_start;
    uint32_t src_height1_width1;
    uint32_t src_height2_width2;

    uint32_t write_mask;
    uint32_t chain_mask;

    uint32_t linear_base;
    uint32_t io_base;

    struct {
        int op;

        int      dst_x;
        int      dst_y;
        int      dst_x_start;
        int      dst_y_start;
        int      src_x;
        int      src_y;
        int      src_x_start;
        int      src_y_start;
        int      xinc;
        int      yinc;
        int      x_count;
        int      y_count;
        int      xx_count;
        int      src_x_count;
        int      src_y_count;
        int      src_width1;
        int      src_height1;
        int      src_width2;
        int      src_height2;
        uint32_t src_offset;
        uint32_t src_pitch;
        uint32_t dst_offset;
        uint32_t dst_pitch;
        int      mix_bg;
        int      mix_fg;
        int      source_bg;
        int      source_fg;
        int      source_mix;
        int      source_host;
        int      dst_width;
        int      dst_height;
        int      busy;
        int      pattern[8][8];
        uint8_t  pattern_clr4x2[2][4];
        uint8_t  pattern_clr8x1[8];
        uint8_t  pattern_clr8x8[8][8];
        int      sc_left;
        int      sc_right;
        int      sc_top;
        int      sc_bottom;
        int      dst_pix_width;
        int      src_pix_width;
        int      host_pix_width;
        int      dst_size;
        int      src_size;
        int      host_size;
        int      temp_cnt;

        uint32_t dp_bkgd_clr;
        uint32_t dp_frgd_clr;
        uint32_t write_mask;

        uint32_t clr_cmp_clr;
        uint32_t clr_cmp_mask;
        int      clr_cmp_fn;
        int      clr_cmp_src;

        int err;
        int poly_draw;
    } accel;

#ifdef DMA_BM
    struct {
        ATOMIC_INT  state;

        ATOMIC_UINT frame_buf_offset, system_buf_addr, command, status;

        ATOMIC_BOOL system_triggered;

        mutex_t *lock;
    } dma;
#endif

    fifo_entry_t fifo[FIFO_SIZE];
    ATOMIC_INT   fifo_read_idx;
    ATOMIC_INT   fifo_write_idx;
    ATOMIC_INT   blitter_busy;

    thread_t *fifo_thread;
    event_t  *wake_fifo_thread;
    event_t  *fifo_not_full_event;

    uint64_t blitter_time;
    uint64_t status_time;

    uint16_t pci_id;
    uint32_t config_chip_id;
    uint32_t block_decoded_io;
    int      use_block_decoded_io;

    int     pll_addr;
    uint8_t pll_regs[16];
    double  pll_freq[4];

    uint32_t config_stat0;

    uint32_t cur_clr0;
    uint32_t cur_clr1;

    uint32_t overlay_dat[2048];
    uint32_t overlay_graphics_key_clr;
    uint32_t overlay_graphics_key_msk;
    uint32_t overlay_video_key_clr;
    uint32_t overlay_video_key_msk;
    uint32_t overlay_key_cntl;
    uint32_t overlay_scale_inc;
    uint32_t overlay_scale_cntl;
    uint32_t overlay_y_x_start;
    uint32_t overlay_y_x_end;

    uint32_t scaler_height_width;
    int      scaler_format;
    int      scaler_update;
    int      scaler_yuv_aper;

    uint32_t buf_offset[2];
    uint32_t buf_pitch[2];

    uint32_t scaler_buf_offset[2];
    uint32_t scaler_buf_pitch;
    uint32_t overlay_exclusive_horz, overlay_exclusive_vert;

    uint32_t vga_dsp_config;
    uint32_t vga_dsp_on_off;
    uint32_t dsp_config;
    uint32_t dsp_on_off;

    int overlay_v_acc;

    uint32_t overlay_uv_addr;
    uint32_t overlay_cur_y;
    uint32_t overlay_base;

    uint8_t thread_run;
    void   *i2c;
    void   *i2c_tv;
    void   *ddc;
} mach64_t;

extern video_timings_t timing_mach64_isa;
extern video_timings_t timing_mach64_vlb;
extern video_timings_t timing_mach64_pci;

enum {
    SRC_BG      = 0,
    SRC_FG      = 1,
    SRC_HOST    = 2,
    SRC_BLITSRC = 3,
    SRC_PAT     = 4
};

enum {
    MONO_SRC_1       = 0,
    MONO_SRC_PAT     = 1,
    MONO_SRC_HOST    = 2,
    MONO_SRC_BLITSRC = 3
};

enum {
    BPP_1  = 0,
    BPP_4  = 1,
    BPP_8  = 2,
    BPP_15 = 3,
    BPP_16 = 4,
    BPP_24 = 5,
    BPP_32 = 6
};

enum {
    OP_RECT,
    OP_LINE
};

enum {
    SRC_PATT_EN     = 1,
    SRC_PATT_ROT_EN = 2,
    SRC_LINEAR_EN   = 4,
    SRC_BYTE_ALIGN  = 8,
    SRC_8x8x8_BRUSH = 32,

    SRC_8x8x8_BRUSH_LOADED = 1 << 12
};

enum {
    DP_BYTE_PIX_ORDER = (1 << 24)
};

#define WIDTH_1BIT 3

extern int mach64_width[8];

int mach64_width[8] = { WIDTH_1BIT, 0, 0, 1, 1, 2, 2, 0 };

enum {
    DST_X_DIR      = 0x01,
    DST_Y_DIR      = 0x02,
    DST_Y_MAJOR    = 0x04,
    DST_X_TILE     = 0x08,
    DST_Y_TILE     = 0x10,
    DST_LAST_PEL   = 0x20,
    DST_POLYGON_EN = 0x40,
    DST_24_ROT_EN  = 0x80
};

enum {
    HOST_BYTE_ALIGN = (1 << 0)
};

void     mach64_write(uint32_t addr, uint8_t val, void *priv);
void     mach64_writew(uint32_t addr, uint16_t val, void *priv);
void     mach64_writel(uint32_t addr, uint32_t val, void *priv);
uint8_t  mach64_read(uint32_t addr, void *priv);
uint16_t mach64_readw(uint32_t addr, void *priv);
uint32_t mach64_readl(uint32_t addr, void *priv);
void     mach64_updatemapping(mach64_t *mach64);
void     mach64_recalctimings(svga_t *svga);
void     mach64_start_fill(mach64_t *mach64);
void     mach64_start_line(mach64_t *mach64);
void     mach64_blit(uint32_t cpu_dat, int count, mach64_t *mach64);
void     mach64_load_context(mach64_t *mach64);
void     mach64_overlay_draw(svga_t *svga, int displine);
void     mach64_queue(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type);

uint8_t  mach64_ext_readb(uint32_t addr, void *priv);
uint16_t mach64_ext_readw(uint32_t addr, void *priv);
uint32_t mach64_ext_readl(uint32_t addr, void *priv);
void     mach64_ext_writeb(uint32_t addr, uint8_t val, void *priv);
void     mach64_ext_writew(uint32_t addr, uint16_t val, void *priv);
void     mach64_ext_writel(uint32_t addr, uint32_t val, void *priv);
void     mach64_fifo_thread(void *param);
void     mach64_wake_fifo_thread(mach64_t *mach64);
void     mach64_wait_fifo_idle(mach64_t *mach64);

uint8_t  mach64_readb_be(uint32_t addr, void *priv);
void     mach64_writeb_be(uint32_t addr, uint8_t val, void *priv);

#ifdef ENABLE_MACH64_LOG
int mach64_do_log = ENABLE_MACH64_LOG;

static void
mach64_log(const char *fmt, ...)
{
    va_list ap;

    if (mach64_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mach64_log(fmt, ...)
#endif

extern mach64_t* reset_state[2];




#define READ8(addr, var)                \
    switch ((addr) &3) {                \
        case 0:                         \
            ret = (var) &0xff;          \
            break;                      \
        case 1:                         \
            ret = ((var) >> 8) & 0xff;  \
            break;                      \
        case 2:                         \
            ret = ((var) >> 16) & 0xff; \
            break;                      \
        case 3:                         \
            ret = ((var) >> 24) & 0xff; \
            break;                      \
    }

#define WRITE8(addr, var, val)                        \
    switch ((addr) &3) {                              \
        case 0:                                       \
            var = (var & 0xffffff00) | (val);         \
            break;                                    \
        case 1:                                       \
            var = (var & 0xffff00ff) | ((val) << 8);  \
            break;                                    \
        case 2:                                       \
            var = (var & 0xff00ffff) | ((val) << 16); \
            break;                                    \
        case 3:                                       \
            var = (var & 0x00ffffff) | ((val) << 24); \
            break;                                    \
    }
