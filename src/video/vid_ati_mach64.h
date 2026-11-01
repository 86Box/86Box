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
#include <86box/pic.h>
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

#define BIOS_ROM_PATH                 "roms/video/mach64/bios.bin"
#define BIOS_ISA_ROM_PATH             "roms/video/mach64/M64-1994.VBI"
#define BIOS_VLB_ROM_PATH             "roms/video/mach64/mach64_vlb_vram.bin"
#define BIOS_XPRESSION_VLB_27802_PATH "roms/video/mach64/xpression_vlb_113-27802-101.bin"
#define BIOS_XPRESSION_VLB_27804_PATH "roms/video/mach64/xpression_vlb_113-27804-101.bin"
#define BIOS_ROMCT_PATH               "roms/video/mach64/mach64-68b110b8cddfd546595673.bin"
#define BIOS_ROMVT_PATH               "roms/video/mach64/mach64vt-660c60c135839345779942.bin"
#define BIOS_ROMVT2_PATH              "roms/video/mach64/atimach64vt2pci.bin"
#define BIOS_ROMGTB_PATH              "roms/video/mach64/ARS2D.bin"
/* 3D Rage IIC PCI BIOSes (PCI ID 4756h), dumped from 27C512 EPROMs. */
#define BIOS_RAGE2C_61801_PATH        "roms/video/mach64/rage2c_pci_113-61801-101.bin"
#define BIOS_RAGE2C_38816_PATH        "roms/video/mach64/rage2c_pci_113-38816-100.bin"
/* 3D Rage Pro (AGP, PCI ID GB, 4742h) BIOSes: 32K images, two from
   27C512 EPROMs (lower half), two read out as 32K. */
#define BIOS_RAGEPRO_TURBO_PATH       "roms/video/mach64/ragepro_agp_turbo_3.091.bin"
#define BIOS_RAGEPRO_49801_2000_PATH  "roms/video/mach64/ragepro_agp_113-49801-101_2000.bin"
#define BIOS_RAGEPRO_49801_1998_PATH  "roms/video/mach64/ragepro_agp_113-49801-101_1998.bin"
#define BIOS_RAGEPRO_40212_PATH       "roms/video/mach64/ragepro_agp_113-40212-103.bin"
/* 3D Rage Pro PCI (PQFP, PCI ID GP, 4750h), SGRAM: ATI 113-41901-104,
   BK3.9.0/3.077, 1997/10/20 ("MACH64GPPCIMTSGU"), read out as 32K. */
#define BIOS_RAGEPRO_PCI_41901_PATH   "roms/video/mach64/ragepro_pci_113-41901-104.bin"

#define FIFO_SIZE         65536
#define FIFO_MASK         (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE   (1 << 31)

#define FIFO_ENTRIES      (mach64->fifo_write_idx - mach64->fifo_read_idx)
#define FIFO_FULL         ((mach64->fifo_write_idx - mach64->fifo_read_idx) >= FIFO_SIZE)
#define FIFO_EMPTY        mach64_fifo_empty(mach64)

#define FIFO_TYPE         0xff000000
#define FIFO_ADDR         0x00ffffff
#define FIFO_ADDR_SETUP   0x00000800 /* a dword of the Rage Pro's setup engine, block 1 */

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
    MACH64_GTB, /* 3D Rage II+; before the VT3, whose GP_IO DDC it does not have */
    MACH64_VT3
};

#define MACH64_FLAG_ONBOARD (1 << 19)
#define MACH64_FLAG_DRAM    (1 << 17) /* the board's memory is DRAM (256Kx16), not VRAM */
#define MACH64_FLAG_RAGE2C  (1 << 20) /* 3D Rage IIC, the GT-B under PCI ID GV (4756h) */
#define MACH64_FLAG_RAGEPRO (1 << 21) /* 3D Rage Pro, the GT-B core plus its own setup engine, bus master and AGP */
#define MACH64_FLAG_PQFP    (1 << 22) /* The Rage Pro's PCI PQFP package, PCI ID GP (4750h) */
#define MACH64_PCI_IOCONFIG 0x40        // "User Defined Configuration"

typedef struct mach64_3d_t     mach64_3d_t;
typedef struct mach64_timing_t mach64_timing_t;

typedef struct mach64_t {
    mem_mapping_t linear_mapping;
    mem_mapping_t mmio_mapping;
    mem_mapping_t linear_mapping_big_endian;
    mem_mapping_t mmio_linear_mapping;

    ati_eeprom_t eeprom;
    svga_t       svga;

    rom_t bios_rom;

    uint8_t regs[256];
    int     index;
    uint8_t ati_io[2]; /* GDC 50h and 51h: extended register address and offset */

    int type;
    int pci;
    int vlb;

    uint8_t pci_slot;
    uint8_t irq_state;
    int     isa_irq;        /* ISA/VLB interrupt jumper; 0 = not fitted */
    int     isa_8bit;       /* ISA card in an 8-bit slot: byte cycles only */
    int     isa_irq_raised;

    uint8_t on_board;
    uint8_t ragepro; /* 3D Rage Pro identity, setup engine and bus master */
    uint8_t agp;     /* On the AGP: its capability and timings */

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
    uint32_t crtc_int_cntl;
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
    uint32_t gp_io_cntl; /* GP_IO_CNTL (VT) */
    uint32_t vt_blk1[256]; /* the VT's register block 1, as its fields read back */

    uint32_t dac_cntl;

    uint32_t dp_bkgd_clr;
    uint32_t dp_frgd_clr;
    uint32_t dp_mix;
    uint32_t dp_pix_width;
    uint32_t dp_src;
    uint32_t dp_set_gui_engine;
    uint32_t usr_dst_pitch; /* USR_DST_PITCH (Rage Pro), DP_SET_GUI_ENGINE's pitch 0 */

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
    uint32_t bus_cntl;   /* BUS_CNTL */
    uint32_t crtc_vline; /* CRTC_VLINE, 10:0 */

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
        int inc;           /* Bresenham terms, sign-extended from 18 bits */
        int dec;
        uint32_t poly_offset; /* polygon boundary source, in bits */
        int rot0;          /* packed 24 bpp component at the start of a row */
        int rot;
        int skip_byte;
        int row_ended;     /* for HOST_BYTE_ALIGN */
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

    thread_t  *fifo_thread;
    event_t   *wake_fifo_thread;
    atomic_int fifo_wake_pending; /* wake_fifo_thread set, the thread not yet up */
    mutex_t   *fifo_mutex; /* held while a FIFO entry runs, by either thread */

    uint64_t blitter_time;
    uint64_t status_time;

    uint16_t pci_id;
    uint32_t config_chip_id;
    uint32_t block_decoded_io;
    int      use_block_decoded_io;
    uint16_t fixed_io; /* Block I/O base pinned for software that hard-codes it (0 = BIOS) */

    int     pll_addr;
    uint8_t pll_regs[64];
    double  pll_freq[4];

    uint32_t config_stat0;

    uint32_t cur_clr0;
    uint32_t cur_clr1;

    uint32_t overlay_dat[2048];
    uint32_t overlay_dat_next[2048]; /* the source line after it, for the vertical blend */
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

    /* 3D Rage II+ (GT-B) */
    mach64_3d_t     *gt3d;        /* 3D engine and scaler, vid_ati_mach64_3d.c */
    mach64_timing_t *timing;      /* draw engine timing, vid_ati_mach64_accel.c */
    int              render_threads; /* threads drawing a large trapezoid's rows */
    mem_mapping_t    aux_mapping; /* the register aperture at BAR2 */
    uint32_t         aux_base;
    uint8_t          gtb_regs[256]; /* control registers kept as written */
    uint8_t          genena;        /* 46E8h */
    uint8_t          genvs;         /* 102h */

    /* The 3D Rage Pro's bus master and its own block 1
       registers (vid_ati_mach64_ragepro.c). */
    struct {
        uint32_t blk1[256];     /* block 1 as it reads back, for the Rage Pro's registers */
        uint32_t src_cntl;      /* SRC_CNTL as written, ahead of the FIFO */
        uint32_t dp_set_gui_engine; /* DP_SET_GUI_ENGINE as written, for its bus master bits */
        uint32_t gui_table;     /* BM_GUI_TABLE: the next descriptor, and the circular buffer size (1:0) */
        uint32_t system_table;  /* BM_SYSTEM_TABLE as written */
        uint32_t frame_buf_offset; /* the descriptor being run: BM_FRAME_BUF_OFFSET */
        uint32_t system_mem_addr;  /* BM_SYSTEM_MEM_ADDR */
        uint32_t command;          /* BM_COMMAND */
        uint32_t status;           /* BM_STATUS */
        uint32_t agp_base;
        uint32_t agp_cntl;
        uint32_t gui_cntl;
        int      pkt_count;     /* GUI master register packet: the writes left */
        uint32_t pkt_reg;       /*   and the next register, as a MM offset index */
        int      running;       /* a bus master transfer is being run */
        uint64_t descriptors;   /* run since reset, for the debug dump */
        uint64_t triangles;     /* setup engine triangles drawn */
        uint64_t pixels;        /* and their pixels */
        int      trace;         /* log the next N triangles */
        int      wlog;          /* log the next N GUI register writes */
        uint32_t last_dst;      /* the last triangle's destination base and pitch, bytes */
        uint32_t last_pitch;
        uint32_t i2c_cntl0;     /* I2C_CNTL_0 but for GO, which the engine clears */
        uint32_t i2c_cntl1;     /* I2C_CNTL_1; its 7:0 are the data FIFO's port */
        uint8_t  i2c_fifo[16];  /* the I2C engine's data FIFO */
        int      i2c_wptr;      /* where the next byte written goes */
        int      i2c_rptr;      /* and where the next one read comes from */
        uint8_t  i2c_slave;     /* the address byte of the transfer's last start */
    } rp;
} mach64_t;

static inline int
mach64_fifo_empty(const mach64_t *mach64)
{
    return mach64->fifo_read_idx == mach64->fifo_write_idx;
}

extern video_timings_t timing_mach64_isa;
extern video_timings_t timing_mach64_vlb;
extern video_timings_t timing_mach64_pci;

enum {
    SRC_BG      = 0,
    SRC_FG      = 1,
    SRC_HOST    = 2,
    SRC_BLITSRC = 3,
    SRC_PAT     = 4,
    SRC_3D      = 5 /* GT: the scaler or the 3D engine */
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
    SRC_LINE_X_DIR  = 16,
    SRC_8x8x8_BRUSH = 32,

    SRC_8x8x8_BRUSH_LOADED = 1 << 12
};

enum {
    DP_BYTE_PIX_ORDER = (1 << 24)
};

#define WIDTH_1BIT 3
#define WIDTH_4BIT 4

extern int mach64_width[8];


enum {
    DST_X_DIR      = 0x01,
    DST_Y_DIR      = 0x02,
    DST_Y_MAJOR    = 0x04,
    DST_X_TILE     = 0x08,
    DST_Y_TILE     = 0x10,
    DST_LAST_PEL   = 0x20,
    DST_POLYGON_EN = 0x40,
    DST_24_ROT_EN  = 0x80,
    DST_BRES_SIGN  = 0x800,
    DST_POLYGON_RTEDGE_DIS = 0x1000 /* CT */
};

enum {
    HOST_BYTE_ALIGN = (1 << 0)
};

void     mach64_write(uint32_t addr, uint8_t val, void *priv);
void     mach64_writew(uint32_t addr, uint16_t val, void *priv);
void     mach64_writel(uint32_t addr, uint32_t val, void *priv);
uint8_t  mach64_read(uint32_t addr, void *priv);
uint8_t *mach64_debug_ptr(uint32_t addr, int write, void *priv);
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
void     mach64_reset_engine(mach64_t *mach64);

/* The 3D Rage Pro (vid_ati_mach64_ragepro.c). Block 1 offsets
   are within the 1K block, 0-3FCh; the writes return 0 for a register that
   is not the Rage Pro's own. */
void     mach64_ragepro_reset(mach64_t *mach64);
int      mach64_ragepro_blk1_write(mach64_t *mach64, uint32_t addr, uint32_t val, int bytes);
int      mach64_ragepro_blk1_read(mach64_t *mach64, uint32_t addr, uint32_t *val);
int      mach64_ragepro_gui_write(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type);
void     mach64_ragepro_bm_eol(mach64_t *mach64);   /* in vid_ati_mach64.c: BUSMASTER_EOL_INT */
void     mach64_ragepro_dump(mach64_t *mach64);
uint8_t  mach64_ragepro_i2c_readb(mach64_t *mach64, uint32_t reg);
void     mach64_ragepro_i2c_writeb(mach64_t *mach64, uint32_t reg, uint8_t val);
void     mach64_timing_dump(mach64_t *mach64);

/* The setup engine, in vid_ati_mach64_3d.c. reg is the dword index of the
   block 1 register, 90h-CFh (VERTEX_1_S at 90h, as 190h in ATI's MM index). */
int      mach64_3d_setup_write(mach64_t *mach64, int reg, uint32_t val);
int      mach64_3d_setup_read(mach64_t *mach64, int reg, uint32_t *val);
void     mach64_3d_ragepro_dump(mach64_t *mach64);

uint8_t  mach64_readb_be(uint32_t addr, void *priv);
void     mach64_writeb_be(uint32_t addr, uint8_t val, void *priv);

/* What a 3D draw takes from the GT-B's memory, for the timing model. */
typedef struct mach64_3d_work_t {
    uint32_t pixels;
    uint32_t rows;
    uint32_t dst_bits;
    uint32_t tex_bits;
    uint32_t texels; /* fetched per pixel */
    int      z_read;
    int      z_write;
    int      dst_read;
} mach64_3d_work_t;

mach64_3d_t     *mach64_3d_init(mach64_t *mach64);
void             mach64_3d_close(mach64_3d_t *ctx);
int              mach64_3d_read(mach64_t *mach64, uint32_t addr, uint32_t *val);
int              mach64_3d_write(mach64_t *mach64, uint32_t addr, uint32_t val, uint32_t type);
mach64_timing_t *mach64_timing_init(void);
void             mach64_timing_close(mach64_timing_t *timing);
void             mach64_timing_3d(mach64_t *mach64, const mach64_3d_work_t *work);
int              mach64_timing_status(mach64_t *mach64, uint32_t *used, int *busy);

#ifdef ENABLE_MACH64_LOG
extern int mach64_do_log;

void
mach64_log(const char *fmt, ...);
#else
#    define mach64_log(fmt, ...)
#endif

extern mach64_t* reset_state[2];




/* The command FIFO's entries. The GT-B has 48; the Rage Pro's
   GUI_CNTL CMDFIFO_SIZE_MODE (1:0) makes it 192 (0, the default), 128 or 64
   (the DRI driver's mach64_drv.h), and its Direct3D HAL waits for 128 free
   in GUI_STAT. */
static inline uint32_t
mach64_fifo_depth(const mach64_t *mach64)
{
    if (!mach64->ragepro)
        return 48;
    switch (mach64->rp.gui_cntl & 3) {
        case 1:
            return 128;
        case 2:
            return 64;
        default:
            return 192;
    }
}

/* MEM_BNDRY (MEM_CNTL 17:16): 0, 256K, 512K or 1M, enabled by
   MEM_BNDRY_EN (bit 18), on the GX and CT (RRG 3-67). The VT's MEM_CNTL
   has no boundary: those bits are its refresh rate and DLL reset
   (VT/RAGE RRG 4-10). */
static inline int
mach64_mem_bndry_en(const mach64_t *mach64)
{
    return (mach64->type <= MACH64_CT) && (mach64->mem_cntl & (1 << 18));
}

static inline uint32_t
mach64_mem_bndry(const mach64_t *mach64)
{
    const uint32_t n = (mach64->mem_cntl >> 16) & 3;

    return n ? (0x20000u << n) : 0;
}

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
