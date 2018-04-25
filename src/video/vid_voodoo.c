/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the 3DFX Voodoo Graphics controller.
 *
 * Version:	@(#)vid_voodoo.c	1.0.13	2018/04/13
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei
 *
 *		Copyright 2008-2018 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../machine/machine.h"
#include "../device.h"
#include "../mem.h"
#include "../pci.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "../plat.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_voodoo.h"
#include "vid_voodoo_dither.h"

#ifdef CLAMP
#undef CLAMP
#endif

#define CLAMP(x) (((x) < 0) ? 0 : (((x) > 0xff) ? 0xff : (x)))
#define CLAMP16(x) (((x) < 0) ? 0 : (((x) > 0xffff) ? 0xffff : (x)))

#define LOD_MAX 8

#define TEX_DIRTY_SHIFT 10

#define TEX_CACHE_MAX 64

enum
{
        VOODOO_1 = 0,
        VOODOO_SB50 = 1,
        VOODOO_2 = 2
};

static uint32_t texture_offset[LOD_MAX+3] =
{
        0,
        256*256,
        256*256 + 128*128,
        256*256 + 128*128 + 64*64,
        256*256 + 128*128 + 64*64 + 32*32,
        256*256 + 128*128 + 64*64 + 32*32 + 16*16,
        256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8,
        256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4,
        256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2,
        256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2 + 1*1,
        256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2 + 1*1 + 1
};

static int tris = 0;

static uint64_t status_time = 0;

typedef union {
        uint32_t i;
        float f;
} int_float;

typedef struct {
        uint8_t b, g, r;
        uint8_t pad;
} rgbp_t;
typedef struct {
        uint8_t b, g, r, a;
} rgba8_t;

typedef union {
        struct {
                uint8_t b, g, r, a;
        } rgba;
        uint32_t u;
} rgba_u;

#define FIFO_SIZE 65536
#define FIFO_MASK (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE (1 << 31)

#define FIFO_ENTRIES (voodoo->fifo_write_idx - voodoo->fifo_read_idx)
#define FIFO_FULL    ((voodoo->fifo_write_idx - voodoo->fifo_read_idx) >= FIFO_SIZE-4)
#define FIFO_EMPTY   (voodoo->fifo_read_idx == voodoo->fifo_write_idx)

#define FIFO_TYPE 0xff000000
#define FIFO_ADDR 0x00ffffff

enum
{
        FIFO_INVALID    = (0x00 << 24),
        FIFO_WRITEL_REG = (0x01 << 24),
        FIFO_WRITEW_FB  = (0x02 << 24),
        FIFO_WRITEL_FB  = (0x03 << 24),
        FIFO_WRITEL_TEX = (0x04 << 24)
};

#define PARAM_SIZE 1024
#define PARAM_MASK (PARAM_SIZE - 1)
#define PARAM_ENTRY_SIZE (1 << 31)

#define PARAM_ENTRIES_1 (voodoo->params_write_idx - voodoo->params_read_idx[0])
#define PARAM_ENTRIES_2 (voodoo->params_write_idx - voodoo->params_read_idx[1])
#define PARAM_FULL_1 ((voodoo->params_write_idx - voodoo->params_read_idx[0]) >= PARAM_SIZE)
#define PARAM_FULL_2 ((voodoo->params_write_idx - voodoo->params_read_idx[1]) >= PARAM_SIZE)
#define PARAM_EMPTY_1   (voodoo->params_read_idx[0] == voodoo->params_write_idx)
#define PARAM_EMPTY_2   (voodoo->params_read_idx[1] == voodoo->params_write_idx)

typedef struct
{
        uint32_t addr_type;
        uint32_t val;
} fifo_entry_t;

static rgba8_t rgb332[0x100], ai44[0x100], rgb565[0x10000], argb1555[0x10000], argb4444[0x10000], ai88[0x10000];

typedef struct voodoo_params_t
{
        int command;

        int32_t vertexAx, vertexAy, vertexBx, vertexBy, vertexCx, vertexCy;

        uint32_t startR, startG, startB, startZ, startA;
        
         int32_t dBdX, dGdX, dRdX, dAdX, dZdX;
        
         int32_t dBdY, dGdY, dRdY, dAdY, dZdY;

        int64_t startW, dWdX, dWdY;

        struct
        {
                int64_t startS, startT, startW, p1;
                int64_t dSdX, dTdX, dWdX, p2;
                int64_t dSdY, dTdY, dWdY, p3;
        } tmu[2];

        uint32_t color0, color1;

        uint32_t fbzMode;
        uint32_t fbzColorPath;
        
        uint32_t fogMode;
        rgbp_t fogColor;
        struct
        {
                uint8_t fog, dfog;
        } fogTable[64];

        uint32_t alphaMode;
        
        uint32_t zaColor;
        
        int chromaKey_r, chromaKey_g, chromaKey_b;
        uint32_t chromaKey;

        uint32_t textureMode[2];
        uint32_t tLOD[2];

        uint32_t texBaseAddr[2], texBaseAddr1[2], texBaseAddr2[2], texBaseAddr38[2];
        
        uint32_t tex_base[2][LOD_MAX+2];
        uint32_t tex_end[2][LOD_MAX+2];
        int tex_width[2];
        int tex_w_mask[2][LOD_MAX+2];
        int tex_w_nmask[2][LOD_MAX+2];
        int tex_h_mask[2][LOD_MAX+2];
        int tex_shift[2][LOD_MAX+2];
        int tex_lod[2][LOD_MAX+2];
        int tex_entry[2];
        int detail_max[2], detail_bias[2], detail_scale[2];
        
        uint32_t draw_offset, aux_offset;

        int tformat[2];
        
        int clipLeft, clipRight, clipLowY, clipHighY;
        
        int sign;
        
        uint32_t front_offset;
        
        uint32_t swapbufferCMD;
        
        uint32_t stipple;
} voodoo_params_t;

typedef struct texture_t
{
        uint32_t base;
        uint32_t tLOD;
        volatile int refcount, refcount_r[2];
        int is16;
        uint32_t palette_checksum;
        uint32_t addr_start[4], addr_end[4];
        uint32_t *data;
} texture_t;

typedef struct voodoo_t
{
        mem_mapping_t mapping;
                
        int pci_enable;

        uint8_t dac_data[8];
        int dac_reg, dac_reg_ff;
        uint8_t dac_readdata;
        uint16_t dac_pll_regs[16];
        
        float pixel_clock;
        int line_time;
        
        voodoo_params_t params;
        
        uint32_t fbiInit0, fbiInit1, fbiInit2, fbiInit3, fbiInit4;
        uint32_t fbiInit5, fbiInit6, fbiInit7; /*Voodoo 2*/
        
        uint32_t initEnable;
        
        uint32_t lfbMode;
        
        uint32_t memBaseAddr;

        int_float fvertexAx, fvertexAy, fvertexBx, fvertexBy, fvertexCx, fvertexCy;

        uint32_t front_offset, back_offset;
        
        uint32_t fb_read_offset, fb_write_offset;
        
        int row_width;
        int block_width;
        
        uint8_t *fb_mem, *tex_mem[2];
        uint16_t *tex_mem_w[2];
               
        int rgb_sel;
        
        uint32_t trexInit1[2];
        
        uint32_t tmuConfig;
        
        int swap_count;
        
        int disp_buffer, draw_buffer;
        int64_t timer_count;
        
        int line;
        svga_t *svga;
        
        uint32_t backPorch;
        uint32_t videoDimensions;
        uint32_t hSync, vSync;
        
        int h_total, v_total, v_disp;
        int h_disp;
        int v_retrace;

        struct
        {
                uint32_t y[4], i[4], q[4];
        } nccTable[2][2];

        rgba_u palette[2][256];
        
        rgba_u ncc_lookup[2][2][256];
        int ncc_dirty[2];

        thread_t *fifo_thread;
        thread_t *render_thread[2];
        event_t *wake_fifo_thread;
        event_t *wake_main_thread;
        event_t *fifo_not_full_event;
        event_t *render_not_full_event[2];
        event_t *wake_render_thread[2];
        
        int voodoo_busy;
        int render_voodoo_busy[2];
        
        int render_threads;
        int odd_even_mask;
        
        int pixel_count[2], texel_count[2], tri_count, frame_count;
        int pixel_count_old[2], texel_count_old[2];
        int wr_count, rd_count, tex_count;
        
        int retrace_count;
        int swap_interval;
        uint32_t swap_offset;
        int swap_pending;
        
        int bilinear_enabled;
        
        int fb_size;
        uint32_t fb_mask;

        int texture_size;
        uint32_t texture_mask;
        
        int dual_tmus;
        int type;
        
        fifo_entry_t fifo[FIFO_SIZE];
        volatile int fifo_read_idx, fifo_write_idx;
	volatile int cmd_read, cmd_written, cmd_written_fifo;

        voodoo_params_t params_buffer[PARAM_SIZE];
        volatile int params_read_idx[2], params_write_idx;
        
        uint32_t cmdfifo_base, cmdfifo_end;
        int cmdfifo_rp;
        volatile int cmdfifo_depth_rd, cmdfifo_depth_wr;
        uint32_t cmdfifo_amin, cmdfifo_amax;
        
        uint32_t sSetupMode;
        struct
        {
                float sVx, sVy;
                float sRed, sGreen, sBlue, sAlpha;
                float sVz, sWb;
                float sW0, sS0, sT0;
                float sW1, sS1, sT1;
        } verts[4];
        int vertex_num;
        int num_verticies;
        
        int flush;

        int scrfilter;
	int scrfilterEnabled;
	int scrfilterThreshold;
	int scrfilterThresholdOld;

        uint32_t last_write_addr;

        uint32_t fbiPixelsIn;
        uint32_t fbiChromaFail;
        uint32_t fbiZFuncFail;
        uint32_t fbiAFuncFail;
        uint32_t fbiPixelsOut;

        uint32_t bltSrcBaseAddr;
        uint32_t bltDstBaseAddr;
        int bltSrcXYStride, bltDstXYStride;
        uint32_t bltSrcChromaRange, bltDstChromaRange;
        int bltSrcChromaMinR, bltSrcChromaMinG, bltSrcChromaMinB;
        int bltSrcChromaMaxR, bltSrcChromaMaxG, bltSrcChromaMaxB;
        int bltDstChromaMinR, bltDstChromaMinG, bltDstChromaMinB;
        int bltDstChromaMaxR, bltDstChromaMaxG, bltDstChromaMaxB;

        int bltClipRight, bltClipLeft;
        int bltClipHighY, bltClipLowY;

        int bltSrcX, bltSrcY;
        int bltDstX, bltDstY;
        int bltSizeX, bltSizeY;
        int bltRop[4];
        uint16_t bltColorFg, bltColorBg;
        
        uint32_t bltCommand;

        struct
        {
                int dst_x, dst_y;
                int cur_x;
                int size_x, size_y;
                int x_dir, y_dir;
                int dst_stride;
        } blt;
                
        rgbp_t clutData[33];
        int clutData_dirty;
        rgbp_t clutData256[256];
        uint32_t video_16to32[0x10000];
        
        uint8_t dirty_line[1024];
        int dirty_line_low, dirty_line_high;
        
        int fb_write_buffer, fb_draw_buffer;
        int buffer_cutoff;

        int64_t read_time, write_time, burst_time;

        int64_t wake_timer;
                
        uint8_t thefilter[256][256]; // pixel filter, feeding from one or two
        uint8_t thefilterg[256][256]; // for green
        uint8_t thefilterb[256][256]; // for blue

        /* the voodoo adds purple lines for some reason */
        uint16_t purpleline[256][3];

        texture_t texture_cache[2][TEX_CACHE_MAX];
        uint8_t texture_present[2][4096];
        int texture_last_removed;
        
        uint32_t palette_checksum[2];
        int palette_dirty[2];

        uint64_t time;
        int render_time[2];
        
        int use_recompiler;        
        void *codegen_data;
        
        struct voodoo_set_t *set;
} voodoo_t;

typedef struct voodoo_set_t
{
        voodoo_t *voodoos[2];
        
        mem_mapping_t snoop_mapping;
        
        int nr_cards;
} voodoo_set_t;

static inline void wait_for_render_thread_idle(voodoo_t *voodoo);

enum
{
        SST_status = 0x000,
        SST_intrCtrl = 0x004,
        
        SST_vertexAx = 0x008,
        SST_vertexAy = 0x00c,
        SST_vertexBx = 0x010,
        SST_vertexBy = 0x014,
        SST_vertexCx = 0x018,
        SST_vertexCy = 0x01c,
        
        SST_startR   = 0x0020,
        SST_startG   = 0x0024,
        SST_startB   = 0x0028,
        SST_startZ   = 0x002c,
        SST_startA   = 0x0030,
        SST_startS   = 0x0034,
        SST_startT   = 0x0038,
        SST_startW   = 0x003c,

        SST_dRdX     = 0x0040,
        SST_dGdX     = 0x0044,
        SST_dBdX     = 0x0048,
        SST_dZdX     = 0x004c,
        SST_dAdX     = 0x0050,
        SST_dSdX     = 0x0054,
        SST_dTdX     = 0x0058,
        SST_dWdX     = 0x005c,
        
        SST_dRdY     = 0x0060,
        SST_dGdY     = 0x0064,
        SST_dBdY     = 0x0068,
        SST_dZdY     = 0x006c,
        SST_dAdY     = 0x0070,
        SST_dSdY     = 0x0074,
        SST_dTdY     = 0x0078,
        SST_dWdY     = 0x007c,
        
        SST_triangleCMD = 0x0080,
        
        SST_fvertexAx = 0x088,
        SST_fvertexAy = 0x08c,
        SST_fvertexBx = 0x090,
        SST_fvertexBy = 0x094,
        SST_fvertexCx = 0x098,
        SST_fvertexCy = 0x09c,
        
        SST_fstartR   = 0x00a0,
        SST_fstartG   = 0x00a4,
        SST_fstartB   = 0x00a8,
        SST_fstartZ   = 0x00ac,
        SST_fstartA   = 0x00b0,
        SST_fstartS   = 0x00b4,
        SST_fstartT   = 0x00b8,
        SST_fstartW   = 0x00bc,

        SST_fdRdX     = 0x00c0,
        SST_fdGdX     = 0x00c4,
        SST_fdBdX     = 0x00c8,
        SST_fdZdX     = 0x00cc,
        SST_fdAdX     = 0x00d0,
        SST_fdSdX     = 0x00d4,
        SST_fdTdX     = 0x00d8,
        SST_fdWdX     = 0x00dc,
        
        SST_fdRdY     = 0x00e0,
        SST_fdGdY     = 0x00e4,
        SST_fdBdY     = 0x00e8,
        SST_fdZdY     = 0x00ec,
        SST_fdAdY     = 0x00f0,
        SST_fdSdY     = 0x00f4,
        SST_fdTdY     = 0x00f8,
        SST_fdWdY     = 0x00fc,
        
        SST_ftriangleCMD = 0x0100,

        SST_fbzColorPath = 0x104,
        SST_fogMode = 0x108,

        SST_alphaMode = 0x10c,        
        SST_fbzMode = 0x110,
        SST_lfbMode = 0x114,
        
        SST_clipLeftRight = 0x118,
        SST_clipLowYHighY = 0x11c,
        
        SST_nopCMD = 0x120,
        SST_fastfillCMD = 0x124,
        SST_swapbufferCMD = 0x128,

        SST_fogColor = 0x12c,        
        SST_zaColor = 0x130,
        SST_chromaKey = 0x134,

        SST_userIntrCMD = 0x13c,
        SST_stipple = 0x140,                        
        SST_color0 = 0x144,
        SST_color1 = 0x148,
        
        SST_fbiPixelsIn = 0x14c,
        SST_fbiChromaFail = 0x150,
        SST_fbiZFuncFail = 0x154,
        SST_fbiAFuncFail = 0x158,
        SST_fbiPixelsOut = 0x15c,

        SST_fogTable00 = 0x160,
        SST_fogTable01 = 0x164,
        SST_fogTable02 = 0x168,
        SST_fogTable03 = 0x16c,
        SST_fogTable04 = 0x170,
        SST_fogTable05 = 0x174,
        SST_fogTable06 = 0x178,
        SST_fogTable07 = 0x17c,
        SST_fogTable08 = 0x180,
        SST_fogTable09 = 0x184,
        SST_fogTable0a = 0x188,
        SST_fogTable0b = 0x18c,
        SST_fogTable0c = 0x190,
        SST_fogTable0d = 0x194,
        SST_fogTable0e = 0x198,
        SST_fogTable0f = 0x19c,
        SST_fogTable10 = 0x1a0,
        SST_fogTable11 = 0x1a4,
        SST_fogTable12 = 0x1a8,
        SST_fogTable13 = 0x1ac,
        SST_fogTable14 = 0x1b0,
        SST_fogTable15 = 0x1b4,
        SST_fogTable16 = 0x1b8,
        SST_fogTable17 = 0x1bc,
        SST_fogTable18 = 0x1c0,
        SST_fogTable19 = 0x1c4,
        SST_fogTable1a = 0x1c8,
        SST_fogTable1b = 0x1cc,
        SST_fogTable1c = 0x1d0,
        SST_fogTable1d = 0x1d4,
        SST_fogTable1e = 0x1d8,
        SST_fogTable1f = 0x1dc,

        SST_cmdFifoBaseAddr = 0x1e0,
        SST_cmdFifoBump = 0x1e4,
        SST_cmdFifoRdPtr = 0x1e8,
        SST_cmdFifoAMin = 0x1ec,
        SST_cmdFifoAMax = 0x1f0,
        SST_cmdFifoDepth = 0x1f4,
        SST_cmdFifoHoles = 0x1f8,
        
        SST_fbiInit4 = 0x200,
        SST_vRetrace = 0x204,
        SST_backPorch = 0x208,
        SST_videoDimensions = 0x20c,
        SST_fbiInit0 = 0x210,
        SST_fbiInit1 = 0x214,
        SST_fbiInit2 = 0x218,
        SST_fbiInit3 = 0x21c,
        SST_hSync = 0x220,
        SST_vSync = 0x224,
        SST_clutData = 0x228,
        SST_dacData = 0x22c,

	SST_scrFilter = 0x230,

        SST_hvRetrace = 0x240,
        SST_fbiInit5 = 0x244,
        SST_fbiInit6 = 0x248,
        SST_fbiInit7 = 0x24c,
        
        SST_sSetupMode = 0x260,
        SST_sVx    = 0x264,
        SST_sVy    = 0x268,
        SST_sARGB  = 0x26c,
        SST_sRed   = 0x270,
        SST_sGreen = 0x274,
        SST_sBlue  = 0x278,
        SST_sAlpha = 0x27c,
        SST_sVz    = 0x280,
        SST_sWb    = 0x284,
        SST_sW0    = 0x288,
        SST_sS0    = 0x28c,
        SST_sT0    = 0x290,
        SST_sW1    = 0x294,
        SST_sS1    = 0x298,
        SST_sT1    = 0x29c,

        SST_sDrawTriCMD = 0x2a0,
        SST_sBeginTriCMD = 0x2a4,
        
        SST_bltSrcBaseAddr = 0x2c0,
        SST_bltDstBaseAddr = 0x2c4,
        SST_bltXYStrides = 0x2c8,        
        SST_bltSrcChromaRange = 0x2cc,
        SST_bltDstChromaRange = 0x2d0,        
        SST_bltClipX = 0x2d4,
        SST_bltClipY = 0x2d8,

        SST_bltSrcXY = 0x2e0,
        SST_bltDstXY = 0x2e4,
        SST_bltSize = 0x2e8,
        SST_bltRop = 0x2ec,
        SST_bltColor = 0x2f0,
        
        SST_bltCommand = 0x2f8,
        SST_bltData = 0x2fc,
        
        SST_textureMode = 0x300,
        SST_tLOD = 0x304,
        SST_tDetail = 0x308,        
        SST_texBaseAddr = 0x30c,
        SST_texBaseAddr1 = 0x310,
        SST_texBaseAddr2 = 0x314,
        SST_texBaseAddr38 = 0x318,
        
        SST_trexInit1 = 0x320,
        
        SST_nccTable0_Y0 = 0x324,
        SST_nccTable0_Y1 = 0x328,
        SST_nccTable0_Y2 = 0x32c,
        SST_nccTable0_Y3 = 0x330,
        SST_nccTable0_I0 = 0x334,
        SST_nccTable0_I1 = 0x338,
        SST_nccTable0_I2 = 0x33c,
        SST_nccTable0_I3 = 0x340,
        SST_nccTable0_Q0 = 0x344,
        SST_nccTable0_Q1 = 0x348,
        SST_nccTable0_Q2 = 0x34c,
        SST_nccTable0_Q3 = 0x350,
        
        SST_nccTable1_Y0 = 0x354,
        SST_nccTable1_Y1 = 0x358,
        SST_nccTable1_Y2 = 0x35c,
        SST_nccTable1_Y3 = 0x360,
        SST_nccTable1_I0 = 0x364,
        SST_nccTable1_I1 = 0x368,
        SST_nccTable1_I2 = 0x36c,
        SST_nccTable1_I3 = 0x370,
        SST_nccTable1_Q0 = 0x374,
        SST_nccTable1_Q1 = 0x378,
        SST_nccTable1_Q2 = 0x37c,
        SST_nccTable1_Q3 = 0x380,

        SST_remap_status = 0x000 | 0x400,
        
        SST_remap_vertexAx = 0x008 | 0x400,
        SST_remap_vertexAy = 0x00c | 0x400,
        SST_remap_vertexBx = 0x010 | 0x400,
        SST_remap_vertexBy = 0x014 | 0x400,
        SST_remap_vertexCx = 0x018 | 0x400,
        SST_remap_vertexCy = 0x01c | 0x400,
        
        SST_remap_startR   = 0x0020 | 0x400,
        SST_remap_startG   = 0x002c | 0x400,
        SST_remap_startB   = 0x0038 | 0x400,
        SST_remap_startZ   = 0x0044 | 0x400,
        SST_remap_startA   = 0x0050 | 0x400,
        SST_remap_startS   = 0x005c | 0x400,
        SST_remap_startT   = 0x0068 | 0x400,
        SST_remap_startW   = 0x0074 | 0x400,

        SST_remap_dRdX     = 0x0024 | 0x400,
        SST_remap_dGdX     = 0x0030 | 0x400,
        SST_remap_dBdX     = 0x003c | 0x400,
        SST_remap_dZdX     = 0x0048 | 0x400,
        SST_remap_dAdX     = 0x0054 | 0x400,
        SST_remap_dSdX     = 0x0060 | 0x400,
        SST_remap_dTdX     = 0x006c | 0x400,
        SST_remap_dWdX     = 0x0078 | 0x400,
        
        SST_remap_dRdY     = 0x0028 | 0x400,
        SST_remap_dGdY     = 0x0034 | 0x400,
        SST_remap_dBdY     = 0x0040 | 0x400,
        SST_remap_dZdY     = 0x004c | 0x400,
        SST_remap_dAdY     = 0x0058 | 0x400,
        SST_remap_dSdY     = 0x0064 | 0x400,
        SST_remap_dTdY     = 0x0070 | 0x400,
        SST_remap_dWdY     = 0x007c | 0x400,
        
        SST_remap_triangleCMD = 0x0080 | 0x400,
        
        SST_remap_fvertexAx = 0x088 | 0x400,
        SST_remap_fvertexAy = 0x08c | 0x400,
        SST_remap_fvertexBx = 0x090 | 0x400,
        SST_remap_fvertexBy = 0x094 | 0x400,
        SST_remap_fvertexCx = 0x098 | 0x400,
        SST_remap_fvertexCy = 0x09c | 0x400,
        
        SST_remap_fstartR   = 0x00a0 | 0x400,
        SST_remap_fstartG   = 0x00ac | 0x400,
        SST_remap_fstartB   = 0x00b8 | 0x400,
        SST_remap_fstartZ   = 0x00c4 | 0x400,
        SST_remap_fstartA   = 0x00d0 | 0x400,
        SST_remap_fstartS   = 0x00dc | 0x400,
        SST_remap_fstartT   = 0x00e8 | 0x400,
        SST_remap_fstartW   = 0x00f4 | 0x400,

        SST_remap_fdRdX     = 0x00a4 | 0x400,
        SST_remap_fdGdX     = 0x00b0 | 0x400,
        SST_remap_fdBdX     = 0x00bc | 0x400,
        SST_remap_fdZdX     = 0x00c8 | 0x400,
        SST_remap_fdAdX     = 0x00d4 | 0x400,
        SST_remap_fdSdX     = 0x00e0 | 0x400,
        SST_remap_fdTdX     = 0x00ec | 0x400,
        SST_remap_fdWdX     = 0x00f8 | 0x400,
        
        SST_remap_fdRdY     = 0x00a8 | 0x400,
        SST_remap_fdGdY     = 0x00b4 | 0x400,
        SST_remap_fdBdY     = 0x00c0 | 0x400,
        SST_remap_fdZdY     = 0x00cc | 0x400,
        SST_remap_fdAdY     = 0x00d8 | 0x400,
        SST_remap_fdSdY     = 0x00e4 | 0x400,
        SST_remap_fdTdY     = 0x00f0 | 0x400,
        SST_remap_fdWdY     = 0x00fc | 0x400,
};

enum
{
        LFB_WRITE_FRONT = 0x0000,
        LFB_WRITE_BACK  = 0x0010,
        LFB_WRITE_MASK  = 0x0030
};

enum
{
        LFB_READ_FRONT = 0x0000,
        LFB_READ_BACK  = 0x0040,
        LFB_READ_AUX   = 0x0080,
        LFB_READ_MASK  = 0x00c0
};

enum
{
        LFB_FORMAT_RGB565 = 0,
        LFB_FORMAT_RGB555 = 1,
        LFB_FORMAT_ARGB1555 = 2,
        LFB_FORMAT_ARGB8888 = 5,
        LFB_FORMAT_DEPTH = 15,
        LFB_FORMAT_MASK = 15
};

enum
{
        LFB_WRITE_COLOUR = 1,
        LFB_WRITE_DEPTH = 2
};

enum
{
        FBZ_CHROMAKEY = (1 << 1),
        FBZ_W_BUFFER = (1 << 3),
        FBZ_DEPTH_ENABLE = (1 << 4),
        
        FBZ_DITHER      = (1 << 8),
        FBZ_RGB_WMASK   = (1 << 9),
        FBZ_DEPTH_WMASK = (1 << 10),
        FBZ_DITHER_2x2  = (1 << 11),
        
        FBZ_DRAW_FRONT = 0x0000,
        FBZ_DRAW_BACK  = 0x4000,
        FBZ_DRAW_MASK  = 0xc000,

        FBZ_DEPTH_BIAS = (1 << 16),
                
        FBZ_DEPTH_SOURCE = (1 << 20),
        
        FBZ_PARAM_ADJUST = (1 << 26)
};

enum
{
        TEX_RGB332 = 0x0,
        TEX_Y4I2Q2 = 0x1,
        TEX_A8 = 0x2,
        TEX_I8 = 0x3,
        TEX_AI8 = 0x4,
        TEX_PAL8 = 0x5,
        TEX_APAL8 = 0x6,
        TEX_ARGB8332 = 0x8,
        TEX_A8Y4I2Q2 = 0x9,
        TEX_R5G6B5 = 0xa,
        TEX_ARGB1555 = 0xb,
        TEX_ARGB4444 = 0xc,
        TEX_A8I8 = 0xd,
        TEX_APAL88 = 0xe
};

enum
{
        TEXTUREMODE_NCC_SEL = (1 << 5),
        TEXTUREMODE_TCLAMPS = (1 << 6),
        TEXTUREMODE_TCLAMPT = (1 << 7),
        TEXTUREMODE_TRILINEAR = (1 << 30)
};

enum
{
        FBIINIT0_VGA_PASS = 1,
        FBIINIT0_GRAPHICS_RESET = (1 << 1)
};

enum
{
        FBIINIT1_MULTI_SST = (1 << 2), /*Voodoo Graphics only*/
        FBIINIT1_VIDEO_RESET = (1 << 8),
        FBIINIT1_SLI_ENABLE = (1 << 23)
};

enum
{
        FBIINIT2_SWAP_ALGORITHM_MASK = (3 << 9)
};

enum
{
        FBIINIT2_SWAP_ALGORITHM_DAC_VSYNC      = (0 << 9),
        FBIINIT2_SWAP_ALGORITHM_DAC_DATA       = (1 << 9),
        FBIINIT2_SWAP_ALGORITHM_PCI_FIFO_STALL = (2 << 9),
        FBIINIT2_SWAP_ALGORITHM_SLI_SYNC       = (3 << 9)
};

enum
{
        FBIINIT3_REMAP = 1
};

enum
{
        FBIINIT5_MULTI_CVG = (1 << 14)
};

enum
{
        FBIINIT7_CMDFIFO_ENABLE = (1 << 8)
};

enum
{
        CC_LOCALSELECT_ITER_RGB = 0,
        CC_LOCALSELECT_TEX = 1,
        CC_LOCALSELECT_COLOR1 = 2,
        CC_LOCALSELECT_LFB = 3
};

enum
{
        CCA_LOCALSELECT_ITER_A = 0,
        CCA_LOCALSELECT_COLOR0 = 1,
        CCA_LOCALSELECT_ITER_Z = 2
};

enum
{
        C_SEL_ITER_RGB = 0,
        C_SEL_TEX      = 1,
        C_SEL_COLOR1   = 2,
        C_SEL_LFB      = 3
};

enum
{
        A_SEL_ITER_A = 0,
        A_SEL_TEX    = 1,
        A_SEL_COLOR1 = 2,
        A_SEL_LFB    = 3
};

enum
{
        CC_MSELECT_ZERO   = 0,
        CC_MSELECT_CLOCAL = 1,
        CC_MSELECT_AOTHER = 2,
        CC_MSELECT_ALOCAL = 3,
        CC_MSELECT_TEX    = 4,
        CC_MSELECT_TEXRGB = 5
};

enum
{
        CCA_MSELECT_ZERO    = 0,
        CCA_MSELECT_ALOCAL  = 1,
        CCA_MSELECT_AOTHER  = 2,
        CCA_MSELECT_ALOCAL2 = 3,
        CCA_MSELECT_TEX     = 4
};

enum
{
        TC_MSELECT_ZERO     = 0,
        TC_MSELECT_CLOCAL   = 1,
        TC_MSELECT_AOTHER   = 2,
        TC_MSELECT_ALOCAL   = 3,
        TC_MSELECT_DETAIL   = 4,
        TC_MSELECT_LOD_FRAC = 5
};

enum
{
        TCA_MSELECT_ZERO     = 0,
        TCA_MSELECT_CLOCAL   = 1,
        TCA_MSELECT_AOTHER   = 2,
        TCA_MSELECT_ALOCAL   = 3,
        TCA_MSELECT_DETAIL   = 4,
        TCA_MSELECT_LOD_FRAC = 5
};

enum
{
        CC_ADD_CLOCAL = 1,
        CC_ADD_ALOCAL = 2
};

enum
{
        CCA_ADD_CLOCAL = 1,
        CCA_ADD_ALOCAL = 2
};

enum
{
        AFUNC_AZERO = 0x0,
        AFUNC_ASRC_ALPHA = 0x1,
        AFUNC_A_COLOR = 0x2,
        AFUNC_ADST_ALPHA = 0x3,
        AFUNC_AONE = 0x4,
        AFUNC_AOMSRC_ALPHA = 0x5,
        AFUNC_AOM_COLOR = 0x6,
        AFUNC_AOMDST_ALPHA = 0x7,
        AFUNC_ASATURATE = 0xf
};

enum
{
        AFUNC_ACOLORBEFOREFOG = 0xf
};

enum
{
        AFUNC_NEVER    = 0,
        AFUNC_LESSTHAN = 1,
        AFUNC_EQUAL = 2,
        AFUNC_LESSTHANEQUAL = 3,
        AFUNC_GREATERTHAN = 4,
        AFUNC_NOTEQUAL = 5,
        AFUNC_GREATERTHANEQUAL = 6,
        AFUNC_ALWAYS = 7
};

enum
{
        DEPTHOP_NEVER    = 0,
        DEPTHOP_LESSTHAN = 1,
        DEPTHOP_EQUAL = 2,
        DEPTHOP_LESSTHANEQUAL = 3,
        DEPTHOP_GREATERTHAN = 4,
        DEPTHOP_NOTEQUAL = 5,
        DEPTHOP_GREATERTHANEQUAL = 6,
        DEPTHOP_ALWAYS = 7
};

enum
{
        FOG_ENABLE   = 0x01,
        FOG_ADD      = 0x02,
        FOG_MULT     = 0x04,
        FOG_ALPHA    = 0x08,
        FOG_Z        = 0x10,
        FOG_W        = 0x18,
        FOG_CONSTANT = 0x20
};

enum
{
        LOD_ODD            = (1 << 18),
        LOD_SPLIT          = (1 << 19),
        LOD_S_IS_WIDER     = (1 << 20),
        LOD_TMULTIBASEADDR = (1 << 24),
        LOD_TMIRROR_S      = (1 << 28),
        LOD_TMIRROR_T      = (1 << 29)
};
enum
{
        CMD_INVALID = 0,
        CMD_DRAWTRIANGLE,
        CMD_FASTFILL,
        CMD_SWAPBUF
};

enum
{
        FBZCP_TEXTURE_ENABLED = (1 << 27)
};

enum
{
        BLTCMD_SRC_TILED = (1 << 14),
        BLTCMD_DST_TILED = (1 << 15)
};

enum
{
        INITENABLE_SLI_MASTER_SLAVE = (1 << 11)
};

#define TEXTUREMODE_MASK 0x3ffff000
#define TEXTUREMODE_PASSTHROUGH 0

#define TEXTUREMODE_LOCAL_MASK 0x00643000
#define TEXTUREMODE_LOCAL  0x00241000

#ifdef ENABLE_VOODOO_LOG
int voodoo_do_log = ENABLE_VOODOO_LOG;
#endif


static void
voodoo_log(const char *fmt, ...)
{
#ifdef ENABLE_VOODOO_LOG
    va_list ap;

    if (voodoo_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


static void voodoo_threshold_check(voodoo_t *voodoo);

static void voodoo_update_ncc(voodoo_t *voodoo, int tmu)
{
        int tbl;
        
        for (tbl = 0; tbl < 2; tbl++)
        {
                int col;
                
                for (col = 0; col < 256; col++)
                {
                        int y = (col >> 4), i = (col >> 2) & 3, q = col & 3;
                        int i_r, i_g, i_b;
                        int q_r, q_g, q_b;
                        
                        y = (voodoo->nccTable[tmu][tbl].y[y >> 2] >> ((y & 3) * 8)) & 0xff;
                        
                        i_r = (voodoo->nccTable[tmu][tbl].i[i] >> 18) & 0x1ff;
                        if (i_r & 0x100)
                                i_r |= 0xfffffe00;
                        i_g = (voodoo->nccTable[tmu][tbl].i[i] >> 9) & 0x1ff;
                        if (i_g & 0x100)
                                i_g |= 0xfffffe00;
                        i_b = voodoo->nccTable[tmu][tbl].i[i] & 0x1ff;
                        if (i_b & 0x100)
                                i_b |= 0xfffffe00;

                        q_r = (voodoo->nccTable[tmu][tbl].q[q] >> 18) & 0x1ff;
                        if (q_r & 0x100)
                                q_r |= 0xfffffe00;
                        q_g = (voodoo->nccTable[tmu][tbl].q[q] >> 9) & 0x1ff;
                        if (q_g & 0x100)
                                q_g |= 0xfffffe00;
                        q_b = voodoo->nccTable[tmu][tbl].q[q] & 0x1ff;
                        if (q_b & 0x100)
                                q_b |= 0xfffffe00;
                        
                        voodoo->ncc_lookup[tmu][tbl][col].rgba.r = CLAMP(y + i_r + q_r);
                        voodoo->ncc_lookup[tmu][tbl][col].rgba.g = CLAMP(y + i_g + q_g);
                        voodoo->ncc_lookup[tmu][tbl][col].rgba.b = CLAMP(y + i_b + q_b);
                        voodoo->ncc_lookup[tmu][tbl][col].rgba.a = 0xff;
                }
        }
}

#define SLI_ENABLED (voodoo->fbiInit1 & FBIINIT1_SLI_ENABLE)
#define TRIPLE_BUFFER ((voodoo->fbiInit2 & 0x10) || (voodoo->fbiInit5 & 0x600) == 0x400)
static void voodoo_recalc(voodoo_t *voodoo)
{
        uint32_t buffer_offset = ((voodoo->fbiInit2 >> 11) & 511) * 4096;
        
        voodoo->params.front_offset = voodoo->disp_buffer*buffer_offset;
        voodoo->back_offset = voodoo->draw_buffer*buffer_offset;

        voodoo->buffer_cutoff = TRIPLE_BUFFER ? (buffer_offset * 4) : (buffer_offset * 3);
        if (TRIPLE_BUFFER)
                voodoo->params.aux_offset = buffer_offset * 3;
        else
                voodoo->params.aux_offset = buffer_offset * 2;

        switch (voodoo->lfbMode & LFB_WRITE_MASK)
        {
                case LFB_WRITE_FRONT:
                voodoo->fb_write_offset = voodoo->params.front_offset;
                voodoo->fb_write_buffer = voodoo->disp_buffer;
                break;
                case LFB_WRITE_BACK:
                voodoo->fb_write_offset = voodoo->back_offset;
                voodoo->fb_write_buffer = voodoo->draw_buffer;
                break;

                default:
                /*BreakNeck sets invalid LFB write buffer select*/
                voodoo->fb_write_offset = voodoo->params.front_offset;
                break;
        }

        switch (voodoo->lfbMode & LFB_READ_MASK)
        {
                case LFB_READ_FRONT:
                voodoo->fb_read_offset = voodoo->params.front_offset;
                break;
                case LFB_READ_BACK:
                voodoo->fb_read_offset = voodoo->back_offset;
                break;
                case LFB_READ_AUX:
                voodoo->fb_read_offset = voodoo->params.aux_offset;
                break;

                default:
                fatal("voodoo_recalc : unknown lfb source\n");
        }
        
        switch (voodoo->params.fbzMode & FBZ_DRAW_MASK)
        {
                case FBZ_DRAW_FRONT:
                voodoo->params.draw_offset = voodoo->params.front_offset;
                voodoo->fb_draw_buffer = voodoo->disp_buffer;
                break;
                case FBZ_DRAW_BACK:
                voodoo->params.draw_offset = voodoo->back_offset;
                voodoo->fb_draw_buffer = voodoo->draw_buffer;
                break;

                default:
                fatal("voodoo_recalc : unknown draw buffer\n");
        }
        
        voodoo->block_width = ((voodoo->fbiInit1 >> 4) & 15) * 2;
        if (voodoo->fbiInit6 & (1 << 30))
                voodoo->block_width += 1;
        if (voodoo->fbiInit1 & (1 << 24))
                voodoo->block_width += 32;
        voodoo->row_width = voodoo->block_width * 32 * 2;

/*        voodoo_log("voodoo_recalc : front_offset %08X  back_offset %08X  aux_offset %08X draw_offset %08x\n", voodoo->params.front_offset, voodoo->back_offset, voodoo->params.aux_offset, voodoo->params.draw_offset);
        voodoo_log("                fb_read_offset %08X  fb_write_offset %08X  row_width %i  %08x %08x\n", voodoo->fb_read_offset, voodoo->fb_write_offset, voodoo->row_width, voodoo->lfbMode, voodoo->params.fbzMode);*/
}

static void voodoo_recalc_tex(voodoo_t *voodoo, int tmu)
{
        int aspect = (voodoo->params.tLOD[tmu] >> 21) & 3;
        int width = 256, height = 256;
        int shift = 8;
        int lod;
        uint32_t base = voodoo->params.texBaseAddr[tmu];
        uint32_t offset = 0;
        int tex_lod = 0;
        
        if (voodoo->params.tLOD[tmu] & LOD_S_IS_WIDER)
                height >>= aspect;
        else
        {
                width >>= aspect;
                shift -= aspect;
        }

        if ((voodoo->params.tLOD[tmu] & LOD_SPLIT) && (voodoo->params.tLOD[tmu] & LOD_ODD))
        {
                width >>= 1;
                height >>= 1;
                shift--;
                tex_lod++;
                if (voodoo->params.tLOD[tmu] & LOD_TMULTIBASEADDR)
                        base = voodoo->params.texBaseAddr1[tmu];
        }
        
        for (lod = 0; lod <= LOD_MAX+1; lod++)
        {
                if (!width)
                        width = 1;
                if (!height)
                        height = 1;
                if (shift < 0)
                        shift = 0;
                voodoo->params.tex_base[tmu][lod] = base + offset;
                if (voodoo->params.tformat[tmu] & 8)
                        voodoo->params.tex_end[tmu][lod] = base + offset + (width * height * 2);
                else
                        voodoo->params.tex_end[tmu][lod] = base + offset + (width * height);
                voodoo->params.tex_w_mask[tmu][lod] = width - 1;
                voodoo->params.tex_w_nmask[tmu][lod] = ~(width - 1);
                voodoo->params.tex_h_mask[tmu][lod] = height - 1;
                voodoo->params.tex_shift[tmu][lod] = shift;
                voodoo->params.tex_lod[tmu][lod] = tex_lod;

                if (!(voodoo->params.tLOD[tmu] & LOD_SPLIT) || ((lod & 1) && (voodoo->params.tLOD[tmu] & LOD_ODD)) || (!(lod & 1) && !(voodoo->params.tLOD[tmu] & LOD_ODD)))
                {
                        if (!(voodoo->params.tLOD[tmu] & LOD_ODD) || lod != 0)
                        {
                                if (voodoo->params.tformat[tmu] & 8)
                                        offset += width * height * 2;
                                else
                                        offset += width * height;

                                if (voodoo->params.tLOD[tmu] & LOD_SPLIT)
                                {
                                        width >>= 2;
                                        height >>= 2;
                                        shift -= 2;
                                        tex_lod += 2;
                                }
                                else
                                {
                                        width >>= 1;
                                        height >>= 1;
                                        shift--;
                                        tex_lod++;
                                }

                                if (voodoo->params.tLOD[tmu] & LOD_TMULTIBASEADDR)
                                {
                                        switch (tex_lod)
                                        {
                                                case 0:
                                                base = voodoo->params.texBaseAddr[tmu];
                                                break;
                                                case 1:
                                                base = voodoo->params.texBaseAddr1[tmu];
                                                break;
                                                case 2:
                                                base = voodoo->params.texBaseAddr2[tmu];
                                                break;
                                                default:
                                                base = voodoo->params.texBaseAddr38[tmu];
                                                break;
                                        }
                                }
                        }
                }
        }
        
        voodoo->params.tex_width[tmu] = width;
}

#define makergba(r, g, b, a)  ((b) | ((g) << 8) | ((r) << 16) | ((a) << 24))

static void use_texture(voodoo_t *voodoo, voodoo_params_t *params, int tmu)
{
        int c, d;
        int lod;
        int lod_min, lod_max;
        uint32_t addr = 0, addr_end;
        uint32_t palette_checksum;

        lod_min = (params->tLOD[tmu] >> 2) & 15;
        lod_max = (params->tLOD[tmu] >> 8) & 15;
        
        if (params->tformat[tmu] == TEX_PAL8 || params->tformat[tmu] == TEX_APAL8 || params->tformat[tmu] == TEX_APAL88)
        {
                if (voodoo->palette_dirty[tmu])
                {
                        palette_checksum = 0;
                        
                        for (c = 0; c < 256; c++)
                                palette_checksum ^= voodoo->palette[tmu][c].u;
                
                        voodoo->palette_checksum[tmu] = palette_checksum;
                        voodoo->palette_dirty[tmu] = 0;
                }
                else
                        palette_checksum = voodoo->palette_checksum[tmu];
        }
        else
                palette_checksum = 0;

        if ((voodoo->params.tLOD[tmu] & LOD_SPLIT) && (voodoo->params.tLOD[tmu] & LOD_ODD) && (voodoo->params.tLOD[tmu] & LOD_TMULTIBASEADDR))
                addr = params->texBaseAddr1[tmu];
        else
                addr = params->texBaseAddr[tmu];

        /*Try to find texture in cache*/
        for (c = 0; c < TEX_CACHE_MAX; c++)
        {
                if (voodoo->texture_cache[tmu][c].base == addr &&
                    voodoo->texture_cache[tmu][c].tLOD == (params->tLOD[tmu] & 0xf00fff) &&
                    voodoo->texture_cache[tmu][c].palette_checksum == palette_checksum)
                {
                        params->tex_entry[tmu] = c;
                        voodoo->texture_cache[tmu][c].refcount++;
                        return;
                }
        }
        
        /*Texture not found, search for unused texture*/
        do
        {
                for (c = 0; c < TEX_CACHE_MAX; c++)
                {
                        voodoo->texture_last_removed++;
                        voodoo->texture_last_removed &= (TEX_CACHE_MAX-1);
                        if (voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount == voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount_r[0] &&
                            (voodoo->render_threads == 1 || voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount == voodoo->texture_cache[tmu][voodoo->texture_last_removed].refcount_r[1]))
                                break;
                }
                if (c == TEX_CACHE_MAX)
                        wait_for_render_thread_idle(voodoo);
        } while (c == TEX_CACHE_MAX);
        if (c == TEX_CACHE_MAX)
                fatal("Texture cache full!\n");

        c = voodoo->texture_last_removed;
        

        if ((voodoo->params.tLOD[tmu] & LOD_SPLIT) && (voodoo->params.tLOD[tmu] & LOD_ODD) && (voodoo->params.tLOD[tmu] & LOD_TMULTIBASEADDR))
                voodoo->texture_cache[tmu][c].base = params->texBaseAddr1[tmu];
        else
                voodoo->texture_cache[tmu][c].base = params->texBaseAddr[tmu];
        voodoo->texture_cache[tmu][c].tLOD = params->tLOD[tmu] & 0xf00fff;

        lod_min = (params->tLOD[tmu] >> 2) & 15;
        lod_max = (params->tLOD[tmu] >> 8) & 15;
//        voodoo_log("  add new texture to %i tformat=%i %08x LOD=%i-%i tmu=%i\n", c, voodoo->params.tformat[tmu], params->texBaseAddr[tmu], lod_min, lod_max, tmu);
        
        lod_min = MIN(lod_min, 8);
        lod_max = MIN(lod_max, 8);
        for (lod = lod_min; lod <= lod_max; lod++)
        {
                uint32_t *base = &voodoo->texture_cache[tmu][c].data[texture_offset[lod]];
                uint32_t tex_addr = params->tex_base[tmu][lod] & voodoo->texture_mask;
                int x, y;
                int shift = 8 - params->tex_lod[tmu][lod];
                rgba_u *pal;
                
                //voodoo_log("  LOD %i : %08x - %08x %i %i,%i\n", lod, params->tex_base[tmu][lod] & voodoo->texture_mask, addr, voodoo->params.tformat[tmu], voodoo->params.tex_w_mask[tmu][lod],voodoo->params.tex_h_mask[tmu][lod]);

                
                switch (params->tformat[tmu])
                {
                        case TEX_RGB332:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr+x) & voodoo->texture_mask];

                                        base[x] = makergba(rgb332[dat].r, rgb332[dat].g, rgb332[dat].b, 0xff);
                                }
                                tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                                base += (1 << shift);
                        }
                        break;

                        case TEX_Y4I2Q2:
                        pal = voodoo->ncc_lookup[tmu][(voodoo->params.textureMode[tmu] & TEXTUREMODE_NCC_SEL) ? 1 : 0];
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr+x) & voodoo->texture_mask];

                                        base[x] = makergba(pal[dat].rgba.r, pal[dat].rgba.g, pal[dat].rgba.b, 0xff);
                                }
                                tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                                base += (1 << shift);
                        }
                        break;
                        
                        case TEX_A8:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr+x) & voodoo->texture_mask];

                                        base[x] = makergba(dat, dat, dat, dat);
                                }
                                tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                                base += (1 << shift);
                        }
                        break;

                        case TEX_I8:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr+x) & voodoo->texture_mask];

                                        base[x] = makergba(dat, dat, dat, 0xff);
                                }
                                tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                                base += (1 << shift);
                        }
                        break;

                        case TEX_AI8:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr+x) & voodoo->texture_mask];

                                        base[x] = makergba((dat & 0x0f) | ((dat << 4) & 0xf0), (dat & 0x0f) | ((dat << 4) & 0xf0), (dat & 0x0f) | ((dat << 4) & 0xf0), (dat & 0xf0) | ((dat >> 4) & 0x0f));
                                }
                                tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                                base += (1 << shift);
                        }
                        break;

                        case TEX_PAL8:
                        pal = voodoo->palette[tmu];
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr+x) & voodoo->texture_mask];

                                        base[x] = makergba(pal[dat].rgba.r, pal[dat].rgba.g, pal[dat].rgba.b, 0xff);
                                }
                                tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                                base += (1 << shift);
                        }
                        break;

                        case TEX_APAL8:
                        pal = voodoo->palette[tmu];
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint8_t dat = voodoo->tex_mem[tmu][(tex_addr+x) & voodoo->texture_mask];
                                        
                                        int r = ((pal[dat].rgba.r & 3) << 6) | ((pal[dat].rgba.g & 0xf0) >> 2) | (pal[dat].rgba.r & 3);
                                        int g = ((pal[dat].rgba.g & 0xf) << 4) | ((pal[dat].rgba.b & 0xc0) >> 4) | ((pal[dat].rgba.g & 0xf) >> 2);
                                        int b = ((pal[dat].rgba.b & 0x3f) << 2) | ((pal[dat].rgba.b & 0x30) >> 4);
                                        int a = (pal[dat].rgba.r & 0xfc) | ((pal[dat].rgba.r & 0xc0) >> 6);
                                        
                                        base[x] = makergba(r, g, b, a);
                                }
                                tex_addr += (1 << voodoo->params.tex_shift[tmu][lod]);
                                base += (1 << shift);
                        }
                        break;

                        case TEX_ARGB8332:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint16_t dat = *(uint16_t *)&voodoo->tex_mem[tmu][(tex_addr + x*2) & voodoo->texture_mask];

                                        base[x] = makergba(rgb332[dat & 0xff].r, rgb332[dat & 0xff].g, rgb332[dat & 0xff].b, dat >> 8);
                                }
                                tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod]+1));
                                base += (1 << shift);
                        }
                        break;

                        case TEX_A8Y4I2Q2:
                        pal = voodoo->ncc_lookup[tmu][(voodoo->params.textureMode[tmu] & TEXTUREMODE_NCC_SEL) ? 1 : 0];
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint16_t dat = *(uint16_t *)&voodoo->tex_mem[tmu][(tex_addr + x*2) & voodoo->texture_mask];

                                        base[x] = makergba(pal[dat & 0xff].rgba.r, pal[dat & 0xff].rgba.g, pal[dat & 0xff].rgba.b, dat >> 8);
                                }
                                tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod]+1));
                                base += (1 << shift);
                        }
                        break;
                                                        
                        case TEX_R5G6B5:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint16_t dat = *(uint16_t *)&voodoo->tex_mem[tmu][(tex_addr + x*2) & voodoo->texture_mask];

                                        base[x] = makergba(rgb565[dat].r, rgb565[dat].g, rgb565[dat].b, 0xff);
                                }
                                tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod]+1));
                                base += (1 << shift);
                        }
                        break;

                        case TEX_ARGB1555:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint16_t dat = *(uint16_t *)&voodoo->tex_mem[tmu][(tex_addr + x*2) & voodoo->texture_mask];

                                        base[x] = makergba(argb1555[dat].r, argb1555[dat].g, argb1555[dat].b, argb1555[dat].a);
                                }
                                tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod]+1));
                                base += (1 << shift);
                        }
                        break;

                        case TEX_ARGB4444:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint16_t dat = *(uint16_t *)&voodoo->tex_mem[tmu][(tex_addr + x*2) & voodoo->texture_mask];

                                        base[x] = makergba(argb4444[dat].r, argb4444[dat].g, argb4444[dat].b, argb4444[dat].a);
                                }
                                tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod]+1));
                                base += (1 << shift);
                        }
                        break;

                        case TEX_A8I8:
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint16_t dat = *(uint16_t *)&voodoo->tex_mem[tmu][(tex_addr + x*2) & voodoo->texture_mask];

                                        base[x] = makergba(dat & 0xff, dat & 0xff, dat & 0xff, dat >> 8);
                                }
                                tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod]+1));
                                base += (1 << shift);
                        }
                        break;

                        case TEX_APAL88:
                        pal = voodoo->palette[tmu];
                        for (y = 0; y < voodoo->params.tex_h_mask[tmu][lod]+1; y++)
                        {
                                for (x = 0; x < voodoo->params.tex_w_mask[tmu][lod]+1; x++)
                                {
                                        uint16_t dat = *(uint16_t *)&voodoo->tex_mem[tmu][(tex_addr + x*2) & voodoo->texture_mask];

                                        base[x] = makergba(pal[dat & 0xff].rgba.r, pal[dat & 0xff].rgba.g, pal[dat & 0xff].rgba.b, dat >> 8);
                                }
                                tex_addr += (1 << (voodoo->params.tex_shift[tmu][lod]+1));
                                base += (1 << shift);
                        }
                        break;

                        default:
                        fatal("Unknown texture format %i\n", params->tformat[tmu]);
                }
        }

        voodoo->texture_cache[tmu][c].is16 = voodoo->params.tformat[tmu] & 8;

        if (params->tformat[tmu] == TEX_PAL8 || params->tformat[tmu] == TEX_APAL8 || params->tformat[tmu] == TEX_APAL88)
                voodoo->texture_cache[tmu][c].palette_checksum = palette_checksum;
        else
                voodoo->texture_cache[tmu][c].palette_checksum = 0;

        if (lod_min == 0)
        {
                voodoo->texture_cache[tmu][c].addr_start[0] = voodoo->params.tex_base[tmu][0];
                voodoo->texture_cache[tmu][c].addr_end[0] = voodoo->params.tex_end[tmu][0];
        }
        else        
                voodoo->texture_cache[tmu][c].addr_start[0] = voodoo->texture_cache[tmu][c].addr_end[0] = 0;

        if (lod_min <= 1 && lod_max >= 1)
        {
                voodoo->texture_cache[tmu][c].addr_start[1] = voodoo->params.tex_base[tmu][1];
                voodoo->texture_cache[tmu][c].addr_end[1] = voodoo->params.tex_end[tmu][1];
        }
        else        
                voodoo->texture_cache[tmu][c].addr_start[1] = voodoo->texture_cache[tmu][c].addr_end[1] = 0;

        if (lod_min <= 2 && lod_max >= 2)
        {
                voodoo->texture_cache[tmu][c].addr_start[2] = voodoo->params.tex_base[tmu][2];
                voodoo->texture_cache[tmu][c].addr_end[2] = voodoo->params.tex_end[tmu][2];
        }
        else        
                voodoo->texture_cache[tmu][c].addr_start[2] = voodoo->texture_cache[tmu][c].addr_end[2] = 0;

        if (lod_max >= 3)
        {
                voodoo->texture_cache[tmu][c].addr_start[3] = voodoo->params.tex_base[tmu][(lod_min > 3) ? lod_min : 3];
                voodoo->texture_cache[tmu][c].addr_end[3] = voodoo->params.tex_end[tmu][(lod_max < 8) ? lod_max : 8];
        }
        else        
                voodoo->texture_cache[tmu][c].addr_start[3] = voodoo->texture_cache[tmu][c].addr_end[3] = 0;


        for (d = 0; d < 4; d++)
        {
                addr = voodoo->texture_cache[tmu][c].addr_start[d];
                addr_end = voodoo->texture_cache[tmu][c].addr_end[d];

                if (addr_end != 0)
                {
                        for (; addr <= addr_end; addr += (1 << TEX_DIRTY_SHIFT))
                                voodoo->texture_present[tmu][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT] = 1;
                }
        }
       
        params->tex_entry[tmu] = c;
        voodoo->texture_cache[tmu][c].refcount++;
}

static void flush_texture_cache(voodoo_t *voodoo, uint32_t dirty_addr, int tmu)
{
        int wait_for_idle = 0;
        int c;
        
        memset(voodoo->texture_present[tmu], 0, sizeof(voodoo->texture_present[0]));
//        voodoo_log("Evict %08x %i\n", dirty_addr, sizeof(voodoo->texture_present));
        for (c = 0; c < TEX_CACHE_MAX; c++)
        {
                if (voodoo->texture_cache[tmu][c].base != -1)
                {
                        int d;
                        
                        for (d = 0; d < 4; d++)
                        {
                                int addr_start = voodoo->texture_cache[tmu][c].addr_start[d];
                                int addr_end = voodoo->texture_cache[tmu][c].addr_end[d];
                                
                                if (addr_end != 0)
                                {
                                        int addr_start_masked = addr_start & voodoo->texture_mask & ~0x3ff;
                                        int addr_end_masked = ((addr_end & voodoo->texture_mask) + 0x3ff) & ~0x3ff;
                                        
                                        if (addr_end_masked < addr_start_masked)
                                                addr_end_masked = voodoo->texture_mask+1;
                                        if (dirty_addr >= addr_start_masked && dirty_addr < addr_end_masked)
                                        {
//                                voodoo_log("  Evict texture %i %08x\n", c, voodoo->texture_cache[tmu][c].base);

                                                if (voodoo->texture_cache[tmu][c].refcount != voodoo->texture_cache[tmu][c].refcount_r[0] ||
                                                    (voodoo->render_threads == 2 && voodoo->texture_cache[tmu][c].refcount != voodoo->texture_cache[tmu][c].refcount_r[1]))
                                                        wait_for_idle = 1;
                                        
                                                voodoo->texture_cache[tmu][c].base = -1;
                                        }
                                        else
                                        {
                                                for (; addr_start <= addr_end; addr_start += (1 << TEX_DIRTY_SHIFT))
                                                        voodoo->texture_present[tmu][(addr_start & voodoo->texture_mask) >> TEX_DIRTY_SHIFT] = 1;
                                        }
                                }
                        }
                }
        }
        if (wait_for_idle)
                wait_for_render_thread_idle(voodoo);
}

typedef struct voodoo_state_t
{
        int xstart, xend, xdir;
        uint32_t base_r, base_g, base_b, base_a, base_z;
        struct
        {
                int64_t base_s, base_t, base_w;
                int lod;
        } tmu[2];
        int64_t base_w;
        int lod;
        int lod_min[2], lod_max[2];
        int dx1, dx2;
        int y, yend, ydir;
        int32_t dxAB, dxAC, dxBC;
        int tex_b[2], tex_g[2], tex_r[2], tex_a[2];
        int tex_s, tex_t;
        int clamp_s[2], clamp_t[2];

        int32_t vertexAx, vertexAy, vertexBx, vertexBy, vertexCx, vertexCy;
        
        uint32_t *tex[2][LOD_MAX+1];
        int tformat;
        
        int *tex_w_mask[2];
        int *tex_h_mask[2];
        int *tex_shift[2];
        int *tex_lod[2];

        uint16_t *fb_mem, *aux_mem;

        int32_t ib, ig, ir, ia;
        int32_t z;
        
        int32_t new_depth;

        int64_t tmu0_s, tmu0_t;
        int64_t tmu0_w;
        int64_t tmu1_s, tmu1_t;
        int64_t tmu1_w;
        int64_t w;
        
        int pixel_count, texel_count;
        int x, x2;
        
        uint32_t w_depth;
        
        float log_temp;
        uint32_t ebp_store;
        uint32_t texBaseAddr;

        int lod_frac[2];
} voodoo_state_t;

static int voodoo_output = 0;

static uint8_t logtable[256] =
{
        0x00,0x01,0x02,0x04,0x05,0x07,0x08,0x09,0x0b,0x0c,0x0e,0x0f,0x10,0x12,0x13,0x15,
        0x16,0x17,0x19,0x1a,0x1b,0x1d,0x1e,0x1f,0x21,0x22,0x23,0x25,0x26,0x27,0x28,0x2a,
        0x2b,0x2c,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x38,0x39,0x3a,0x3b,0x3d,0x3e,
        0x3f,0x40,0x41,0x43,0x44,0x45,0x46,0x47,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x50,0x51,
        0x52,0x53,0x54,0x55,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x60,0x61,0x62,0x63,
        0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6c,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,
        0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,0x80,0x81,0x83,0x84,0x85,
        0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8c,0x8d,0x8e,0x8f,0x90,0x91,0x92,0x93,0x94,
        0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,0xa0,0xa1,0xa2,0xa2,0xa3,
        0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xad,0xae,0xaf,0xb0,0xb1,0xb2,
        0xb3,0xb4,0xb5,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbc,0xbd,0xbe,0xbf,0xc0,
        0xc1,0xc2,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xcd,
        0xce,0xcf,0xd0,0xd1,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd6,0xd7,0xd8,0xd9,0xda,0xda,
        0xdb,0xdc,0xdd,0xde,0xde,0xdf,0xe0,0xe1,0xe1,0xe2,0xe3,0xe4,0xe5,0xe5,0xe6,0xe7,
        0xe8,0xe8,0xe9,0xea,0xeb,0xeb,0xec,0xed,0xee,0xef,0xef,0xf0,0xf1,0xf2,0xf2,0xf3,
        0xf4,0xf5,0xf5,0xf6,0xf7,0xf7,0xf8,0xf9,0xfa,0xfa,0xfb,0xfc,0xfd,0xfd,0xfe,0xff
};

static inline int fastlog(uint64_t val)
{
        uint64_t oldval = val;
        int exp = 63;
        int frac;
        
        if (!val || val & (1ULL << 63))
                return 0x80000000;
        
        if (!(val & 0xffffffff00000000))
        {
                exp -= 32;
                val <<= 32;
        }
        if (!(val & 0xffff000000000000))
        {
                exp -= 16;
                val <<= 16;
        }
        if (!(val & 0xff00000000000000))
        {
                exp -= 8;
                val <<= 8;
        }
        if (!(val & 0xf000000000000000))
        {
                exp -= 4;
                val <<= 4;
        }
        if (!(val & 0xc000000000000000))
        {
                exp -= 2;
                val <<= 2;
        }
        if (!(val & 0x8000000000000000))
        {
                exp -= 1;
                val <<= 1;
        }
        
        if (exp >= 8)
                frac = (oldval >> (exp - 8)) & 0xff;
        else
                frac = (oldval << (8 - exp)) & 0xff;

        return (exp << 8) | logtable[frac];
}

static inline int voodoo_fls(uint16_t val)
{
        int num = 0;
        
//voodoo_log("fls(%04x) = ", val);
        if (!(val & 0xff00))
        {
                num += 8;
                val <<= 8;
        }
        if (!(val & 0xf000))
        {
                num += 4;
                val <<= 4;
        }
        if (!(val & 0xc000))
        {
                num += 2;
                val <<= 2;
        }
        if (!(val & 0x8000))
        {
                num += 1;
                val <<= 1;
        }
//voodoo_log("%i %04x\n", num, val);
        return num;
}

typedef struct voodoo_texture_state_t
{
        int s, t;
        int w_mask, h_mask;
        int tex_shift;
} voodoo_texture_state_t;

static inline void tex_read(voodoo_state_t *state, voodoo_texture_state_t *texture_state, int tmu)
{
        uint32_t dat;
        
        if (texture_state->s & ~texture_state->w_mask)
        {
                if (state->clamp_s[tmu])
                {
                        if (texture_state->s < 0)
                                texture_state->s = 0;
                        if (texture_state->s > texture_state->w_mask)
                                texture_state->s = texture_state->w_mask;
                }
                else
                        texture_state->s &= texture_state->w_mask;
        }
        if (texture_state->t & ~texture_state->h_mask)
        {
                if (state->clamp_t[tmu])
                {
                        if (texture_state->t < 0)
                                texture_state->t = 0;
                        if (texture_state->t > texture_state->h_mask)
                                texture_state->t = texture_state->h_mask;
                }
                else
                        texture_state->t &= texture_state->h_mask;
        }

        dat = state->tex[tmu][state->lod][texture_state->s + (texture_state->t << texture_state->tex_shift)];
        
        state->tex_b[tmu] = dat & 0xff;
        state->tex_g[tmu] = (dat >> 8) & 0xff;
        state->tex_r[tmu] = (dat >> 16) & 0xff;
        state->tex_a[tmu] = (dat >> 24) & 0xff;
}

#define LOW4(x)  ((x & 0x0f) | ((x & 0x0f) << 4))
#define HIGH4(x) ((x & 0xf0) | ((x & 0xf0) >> 4))

static inline void tex_read_4(voodoo_state_t *state, voodoo_texture_state_t *texture_state, int s, int t, int *d, int tmu, int x)
{
        rgba_u dat[4];

        if (((s | (s + 1)) & ~texture_state->w_mask) || ((t | (t + 1)) & ~texture_state->h_mask))
        {
                int c;
                for (c = 0; c < 4; c++)
                {
                        int _s = s + (c & 1);
                        int _t = t + ((c & 2) >> 1);
                
                        if (_s & ~texture_state->w_mask)
                        {
                                if (state->clamp_s[tmu])
                                {
                                        if (_s < 0)
                                                _s = 0;
                                        if (_s > texture_state->w_mask)
                                                _s = texture_state->w_mask;
                                }
                                else
                                        _s &= texture_state->w_mask;
                        }
                        if (_t & ~texture_state->h_mask)
                        {
                                if (state->clamp_t[tmu])
                                {
                                        if (_t < 0)
                                                _t = 0;
                                        if (_t > texture_state->h_mask)
                                                _t = texture_state->h_mask;
                                }
                                else
                                        _t &= texture_state->h_mask;
                        }
                        dat[c].u = state->tex[tmu][state->lod][_s + (_t << texture_state->tex_shift)];
                }
        }
        else
        {
                dat[0].u = state->tex[tmu][state->lod][s +     (t << texture_state->tex_shift)];
                dat[1].u = state->tex[tmu][state->lod][s + 1 + (t << texture_state->tex_shift)];
                dat[2].u = state->tex[tmu][state->lod][s +     ((t + 1) << texture_state->tex_shift)];
                dat[3].u = state->tex[tmu][state->lod][s + 1 + ((t + 1) << texture_state->tex_shift)];
        }

        state->tex_r[tmu] = (dat[0].rgba.r * d[0] + dat[1].rgba.r * d[1] + dat[2].rgba.r * d[2] + dat[3].rgba.r * d[3]) >> 8;
        state->tex_g[tmu] = (dat[0].rgba.g * d[0] + dat[1].rgba.g * d[1] + dat[2].rgba.g * d[2] + dat[3].rgba.g * d[3]) >> 8;
        state->tex_b[tmu] = (dat[0].rgba.b * d[0] + dat[1].rgba.b * d[1] + dat[2].rgba.b * d[2] + dat[3].rgba.b * d[3]) >> 8;
        state->tex_a[tmu] = (dat[0].rgba.a * d[0] + dat[1].rgba.a * d[1] + dat[2].rgba.a * d[2] + dat[3].rgba.a * d[3]) >> 8;
}

static inline void voodoo_get_texture(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int tmu, int x)
{
        voodoo_texture_state_t texture_state;
        int d[4];
        int s, t;
        int tex_lod = state->tex_lod[tmu][state->lod];

        texture_state.w_mask = state->tex_w_mask[tmu][state->lod];
        texture_state.h_mask = state->tex_h_mask[tmu][state->lod];
        texture_state.tex_shift = 8 - tex_lod;

        if (params->tLOD[tmu] & LOD_TMIRROR_S)
        {
                if (state->tex_s & 0x1000)
                        state->tex_s = ~state->tex_s;
        }
        if (params->tLOD[tmu] & LOD_TMIRROR_T)
        {
                if (state->tex_t & 0x1000)
                        state->tex_t = ~state->tex_t;
        }
        
        if (voodoo->bilinear_enabled && params->textureMode[tmu] & 6)
        {
                int _ds, dt;
                        
                state->tex_s -= 1 << (3+tex_lod);
                state->tex_t -= 1 << (3+tex_lod);
        
                s = state->tex_s >> tex_lod;
                t = state->tex_t >> tex_lod;

                _ds = s & 0xf;
                dt = t & 0xf;

                s >>= 4;
                t >>= 4;
//if (x == 80)
//if (voodoo_output)
//        voodoo_log("s=%08x t=%08x _ds=%02x _dt=%02x\n", s, t, _ds, dt);
                d[0] = (16 - _ds) * (16 - dt);
                d[1] =  _ds * (16 - dt);
                d[2] = (16 - _ds) * dt;
                d[3] = _ds * dt;

//                texture_state.s = s;
//                texture_state.t = t;
                tex_read_4(state, &texture_state, s, t, d, tmu, x);

        
/*                state->tex_r = (tex_samples[0].rgba.r * d[0] + tex_samples[1].rgba.r * d[1] + tex_samples[2].rgba.r * d[2] + tex_samples[3].rgba.r * d[3]) >> 8;
                state->tex_g = (tex_samples[0].rgba.g * d[0] + tex_samples[1].rgba.g * d[1] + tex_samples[2].rgba.g * d[2] + tex_samples[3].rgba.g * d[3]) >> 8;
                state->tex_b = (tex_samples[0].rgba.b * d[0] + tex_samples[1].rgba.b * d[1] + tex_samples[2].rgba.b * d[2] + tex_samples[3].rgba.b * d[3]) >> 8;
                state->tex_a = (tex_samples[0].rgba.a * d[0] + tex_samples[1].rgba.a * d[1] + tex_samples[2].rgba.a * d[2] + tex_samples[3].rgba.a * d[3]) >> 8;*/
/*                state->tex_r = tex_samples[0].r;
                state->tex_g = tex_samples[0].g;
                state->tex_b = tex_samples[0].b;
                state->tex_a = tex_samples[0].a;*/
        }
        else
        {
        //        rgba_t tex_samples;
        //        voodoo_texture_state_t texture_state;
//                int s = state->tex_s >> (18+state->lod);
//                int t = state->tex_t >> (18+state->lod);
        //        int s, t;

//                state->tex_s -= 1 << (17+state->lod);
//                state->tex_t -= 1 << (17+state->lod);
        
                s = state->tex_s >> (4+tex_lod);
                t = state->tex_t >> (4+tex_lod);

                texture_state.s = s;
                texture_state.t = t;
                tex_read(state, &texture_state, tmu);

/*                state->tex_r = tex_samples[0].rgba.r;
                state->tex_g = tex_samples[0].rgba.g;
                state->tex_b = tex_samples[0].rgba.b;
                state->tex_a = tex_samples[0].rgba.a;*/
        }
}

static inline void voodoo_tmu_fetch(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int tmu, int x)
{
        if (params->textureMode[tmu] & 1)
        {
                int64_t _w = 0;

                if (tmu)
                {
                        if (state->tmu1_w)
                                _w = (int64_t)((1ULL << 48) / state->tmu1_w);
                        state->tex_s = (int32_t)(((((state->tmu1_s + (1 << 13)) >> 14) * _w) + (1 << 29))  >> 30);
                        state->tex_t = (int32_t)(((((state->tmu1_t + (1 << 13))  >> 14)  * _w) + (1 << 29))  >> 30);
                }
                else
                {
                        if (state->tmu0_w)
                                _w = (int64_t)((1ULL << 48) / state->tmu0_w);
                        state->tex_s = (int32_t)(((((state->tmu0_s + (1 << 13))  >> 14) * _w) + (1 << 29)) >> 30);
                        state->tex_t = (int32_t)(((((state->tmu0_t + (1 << 13))  >> 14)  * _w) + (1 << 29))  >> 30);
                }

                state->lod = state->tmu[tmu].lod + (fastlog(_w) - (19 << 8));
        }
        else
        {
                if (tmu)
                {
                        state->tex_s = (int32_t)(state->tmu1_s >> (14+14));
                        state->tex_t = (int32_t)(state->tmu1_t >> (14+14));
                }
                else
                {
                        state->tex_s = (int32_t)(state->tmu0_s >> (14+14));
                        state->tex_t = (int32_t)(state->tmu0_t >> (14+14));
                }                        
                state->lod = state->tmu[tmu].lod;
        }
                                
        if (state->lod < state->lod_min[tmu])
                state->lod = state->lod_min[tmu];
        else if (state->lod > state->lod_max[tmu])
                state->lod = state->lod_max[tmu];
        state->lod_frac[tmu] = state->lod & 0xff;
        state->lod >>= 8;

        voodoo_get_texture(voodoo, params, state, tmu, x);
}

#define DEPTH_TEST(comp_depth)                          \
        do                                              \
        {                                               \
                switch (depth_op)                       \
                {                                       \
                        case DEPTHOP_NEVER:             \
                        voodoo->fbiZFuncFail++;         \
                        goto skip_pixel;                \
                        case DEPTHOP_LESSTHAN:          \
                        if (!(comp_depth < old_depth))  \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_EQUAL:             \
                        if (!(comp_depth == old_depth)) \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_LESSTHANEQUAL:     \
                        if (!(comp_depth <= old_depth)) \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_GREATERTHAN:       \
                        if (!(comp_depth > old_depth))  \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_NOTEQUAL:          \
                        if (!(comp_depth != old_depth)) \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_GREATERTHANEQUAL:  \
                        if (!(comp_depth >= old_depth)) \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_ALWAYS:            \
                        break;                          \
                }                                       \
        } while (0)

#define APPLY_FOG(src_r, src_g, src_b, z, ia, w)                        \
        do                                                              \
        {                                                               \
                if (params->fogMode & FOG_CONSTANT)                     \
                {                                                       \
                        src_r += params->fogColor.r;                    \
                        src_g += params->fogColor.g;                    \
                        src_b += params->fogColor.b;                    \
                }                                                       \
                else                                                    \
                {                                                       \
                        int fog_r, fog_g, fog_b, fog_a = 0;             \
                        int fog_idx;                                    \
                                                                        \
                        if (!(params->fogMode & FOG_ADD))               \
                        {                                               \
                                fog_r = params->fogColor.r;             \
                                fog_g = params->fogColor.g;             \
                                fog_b = params->fogColor.b;             \
                        }                                               \
                        else                                            \
                                fog_r = fog_g = fog_b = 0;              \
                                                                        \
                        if (!(params->fogMode & FOG_MULT))              \
                        {                                               \
                                fog_r -= src_r;                         \
                                fog_g -= src_g;                         \
                                fog_b -= src_b;                         \
                        }                                               \
                                                                        \
                        switch (params->fogMode & (FOG_Z|FOG_ALPHA))    \
                        {                                               \
                                case 0:                                 \
                                fog_idx = (w_depth >> 10) & 0x3f;       \
                                                                        \
                                fog_a = params->fogTable[fog_idx].fog;  \
                                fog_a += (params->fogTable[fog_idx].dfog * ((w_depth >> 2) & 0xff)) >> 10;      \
                                break;                                  \
                                case FOG_Z:                             \
                                fog_a = (z >> 20) & 0xff;               \
                                break;                                  \
                                case FOG_ALPHA:                         \
                                fog_a = CLAMP(ia >> 12);                \
                                break;                                  \
                                case FOG_W:                             \
                                fog_a = CLAMP((w >> 32) & 0xff);        \
                                break;                                  \
                        }                                               \
                        fog_a++;                                        \
                                                                        \
                        fog_r = (fog_r * fog_a) >> 8;                   \
                        fog_g = (fog_g * fog_a) >> 8;                   \
                        fog_b = (fog_b * fog_a) >> 8;                   \
                                                                        \
                        if (params->fogMode & FOG_MULT)                 \
                        {                                               \
                                src_r = fog_r;                          \
                                src_g = fog_g;                          \
                                src_b = fog_b;                          \
                        }                                               \
                        else                                            \
                        {                                               \
                                src_r += fog_r;                         \
                                src_g += fog_g;                         \
                                src_b += fog_b;                         \
                        }                                               \
                }                                                       \
                                                                        \
                src_r = CLAMP(src_r);                                   \
                src_g = CLAMP(src_g);                                   \
                src_b = CLAMP(src_b);                                   \
        } while (0)

#define ALPHA_TEST(src_a)                               \
        do                                              \
        {                                               \
                switch (alpha_func)                     \
                {                                       \
                        case AFUNC_NEVER:               \
                        voodoo->fbiAFuncFail++;         \
                        goto skip_pixel;                \
                        case AFUNC_LESSTHAN:            \
                        if (!(src_a < a_ref))           \
                        {                               \
                                voodoo->fbiAFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case AFUNC_EQUAL:               \
                        if (!(src_a == a_ref))          \
                        {                               \
                                voodoo->fbiAFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case AFUNC_LESSTHANEQUAL:       \
                        if (!(src_a <= a_ref))          \
                        {                               \
                                voodoo->fbiAFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case AFUNC_GREATERTHAN:         \
                        if (!(src_a > a_ref))           \
                        {                               \
                                voodoo->fbiAFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case AFUNC_NOTEQUAL:            \
                        if (!(src_a != a_ref))          \
                        {                               \
                                voodoo->fbiAFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case AFUNC_GREATERTHANEQUAL:    \
                        if (!(src_a >= a_ref))          \
                        {                               \
                                voodoo->fbiAFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case AFUNC_ALWAYS:              \
                        break;                          \
                }                                       \
        } while (0)

#define ALPHA_BLEND(src_r, src_g, src_b, src_a)                         \
        do                                                              \
        {                                                               \
                int _a;                                                 \
                int newdest_r = 0, newdest_g = 0, newdest_b = 0;        \
                                                                        \
                switch (dest_afunc)                                     \
                {                                                       \
                        case AFUNC_AZERO:                               \
                        newdest_r = newdest_g = newdest_b = 0;          \
                        break;                                          \
                        case AFUNC_ASRC_ALPHA:                          \
                        newdest_r = (dest_r * src_a) / 255;             \
                        newdest_g = (dest_g * src_a) / 255;             \
                        newdest_b = (dest_b * src_a) / 255;             \
                        break;                                          \
                        case AFUNC_A_COLOR:                             \
                        newdest_r = (dest_r * src_r) / 255;             \
                        newdest_g = (dest_g * src_g) / 255;             \
                        newdest_b = (dest_b * src_b) / 255;             \
                        break;                                          \
                        case AFUNC_ADST_ALPHA:                          \
                        newdest_r = (dest_r * dest_a) / 255;            \
                        newdest_g = (dest_g * dest_a) / 255;            \
                        newdest_b = (dest_b * dest_a) / 255;            \
                        break;                                          \
                        case AFUNC_AONE:                                \
                        newdest_r = dest_r;                             \
                        newdest_g = dest_g;                             \
                        newdest_b = dest_b;                             \
                        break;                                          \
                        case AFUNC_AOMSRC_ALPHA:                        \
                        newdest_r = (dest_r * (255-src_a)) / 255;       \
                        newdest_g = (dest_g * (255-src_a)) / 255;       \
                        newdest_b = (dest_b * (255-src_a)) / 255;       \
                        break;                                          \
                        case AFUNC_AOM_COLOR:                           \
                        newdest_r = (dest_r * (255-src_r)) / 255;       \
                        newdest_g = (dest_g * (255-src_g)) / 255;       \
                        newdest_b = (dest_b * (255-src_b)) / 255;       \
                        break;                                          \
                        case AFUNC_AOMDST_ALPHA:                        \
                        newdest_r = (dest_r * (255-dest_a)) / 255;      \
                        newdest_g = (dest_g * (255-dest_a)) / 255;      \
                        newdest_b = (dest_b * (255-dest_a)) / 255;      \
                        break;                                          \
                        case AFUNC_ASATURATE:                           \
                        _a = MIN(src_a, 1-dest_a);                      \
                        newdest_r = (dest_r * _a) / 255;                \
                        newdest_g = (dest_g * _a) / 255;                \
                        newdest_b = (dest_b * _a) / 255;                \
                        break;                                          \
                }                                                       \
                                                                        \
                switch (src_afunc)                                      \
                {                                                       \
                        case AFUNC_AZERO:                               \
                        src_r = src_g = src_b = 0;                      \
                        break;                                          \
                        case AFUNC_ASRC_ALPHA:                          \
                        src_r = (src_r * src_a) / 255;                  \
                        src_g = (src_g * src_a) / 255;                  \
                        src_b = (src_b * src_a) / 255;                  \
                        break;                                          \
                        case AFUNC_A_COLOR:                             \
                        src_r = (src_r * dest_r) / 255;                 \
                        src_g = (src_g * dest_g) / 255;                 \
                        src_b = (src_b * dest_b) / 255;                 \
                        break;                                          \
                        case AFUNC_ADST_ALPHA:                          \
                        src_r = (src_r * dest_a) / 255;                 \
                        src_g = (src_g * dest_a) / 255;                 \
                        src_b = (src_b * dest_a) / 255;                 \
                        break;                                          \
                        case AFUNC_AONE:                                \
                        break;                                          \
                        case AFUNC_AOMSRC_ALPHA:                        \
                        src_r = (src_r * (255-src_a)) / 255;            \
                        src_g = (src_g * (255-src_a)) / 255;            \
                        src_b = (src_b * (255-src_a)) / 255;            \
                        break;                                          \
                        case AFUNC_AOM_COLOR:                           \
                        src_r = (src_r * (255-dest_r)) / 255;           \
                        src_g = (src_g * (255-dest_g)) / 255;           \
                        src_b = (src_b * (255-dest_b)) / 255;           \
                        break;                                          \
                        case AFUNC_AOMDST_ALPHA:                        \
                        src_r = (src_r * (255-dest_a)) / 255;           \
                        src_g = (src_g * (255-dest_a)) / 255;           \
                        src_b = (src_b * (255-dest_a)) / 255;           \
                        break;                                          \
                        case AFUNC_ACOLORBEFOREFOG:                     \
                        fatal("AFUNC_ACOLORBEFOREFOG\n"); \
                        break;                                          \
                }                                                       \
                                                                        \
                src_r += newdest_r;                                     \
                src_g += newdest_g;                                     \
                src_b += newdest_b;                                     \
                                                                        \
                src_r = CLAMP(src_r);                                   \
                src_g = CLAMP(src_g);                                   \
                src_b = CLAMP(src_b);                                   \
        } while(0)


#define _rgb_sel                 ( params->fbzColorPath & 3)
#define a_sel                   ( (params->fbzColorPath >> 2) & 3)
#define cc_localselect          ( params->fbzColorPath & (1 << 4))
#define cca_localselect         ( (params->fbzColorPath >> 5) & 3)
#define cc_localselect_override ( params->fbzColorPath & (1 << 7))
#define cc_zero_other           ( params->fbzColorPath & (1 << 8))
#define cc_sub_clocal           ( params->fbzColorPath & (1 << 9))
#define cc_mselect              ( (params->fbzColorPath >> 10) & 7)
#define cc_reverse_blend        ( params->fbzColorPath & (1 << 13))
#define cc_add                  ( (params->fbzColorPath >> 14) & 3)
#define cc_add_alocal           ( params->fbzColorPath & (1 << 15))
#define cc_invert_output        ( params->fbzColorPath & (1 << 16))
#define cca_zero_other          ( params->fbzColorPath & (1 << 17))
#define cca_sub_clocal          ( params->fbzColorPath & (1 << 18))
#define cca_mselect             ( (params->fbzColorPath >> 19) & 7)
#define cca_reverse_blend       ( params->fbzColorPath & (1 << 22))
#define cca_add                 ( (params->fbzColorPath >> 23) & 3)
#define cca_invert_output       ( params->fbzColorPath & (1 << 25))
#define tc_zero_other (params->textureMode[0] & (1 << 12))
#define tc_sub_clocal (params->textureMode[0] & (1 << 13))
#define tc_mselect    ((params->textureMode[0] >> 14) & 7)
#define tc_reverse_blend (params->textureMode[0] & (1 << 17))
#define tc_add_clocal (params->textureMode[0] & (1 << 18))
#define tc_add_alocal (params->textureMode[0] & (1 << 19))
#define tc_invert_output (params->textureMode[0] & (1 << 20))
#define tca_zero_other (params->textureMode[0] & (1 << 21))
#define tca_sub_clocal (params->textureMode[0] & (1 << 22))
#define tca_mselect    ((params->textureMode[0] >> 23) & 7)
#define tca_reverse_blend (params->textureMode[0] & (1 << 26))
#define tca_add_clocal (params->textureMode[0] & (1 << 27))
#define tca_add_alocal (params->textureMode[0] & (1 << 28))
#define tca_invert_output (params->textureMode[0] & (1 << 29))

#define tc_sub_clocal_1 (params->textureMode[1] & (1 << 13))
#define tc_mselect_1    ((params->textureMode[1] >> 14) & 7)
#define tc_reverse_blend_1 (params->textureMode[1] & (1 << 17))
#define tc_add_clocal_1 (params->textureMode[1] & (1 << 18))
#define tc_add_alocal_1 (params->textureMode[1] & (1 << 19))
#define tca_sub_clocal_1 (params->textureMode[1] & (1 << 22))
#define tca_mselect_1    ((params->textureMode[1] >> 23) & 7)
#define tca_reverse_blend_1 (params->textureMode[1] & (1 << 26))
#define tca_add_clocal_1 (params->textureMode[1] & (1 << 27))
#define tca_add_alocal_1 (params->textureMode[1] & (1 << 28))

#define src_afunc ( (params->alphaMode >> 8) & 0xf)
#define dest_afunc ( (params->alphaMode >> 12) & 0xf)
#define alpha_func ( (params->alphaMode >> 1) & 7)
#define a_ref ( params->alphaMode >> 24)
#define depth_op ( (params->fbzMode >> 5) & 7)
#define dither ( params->fbzMode & FBZ_DITHER)
#define dither2x2 (params->fbzMode & FBZ_DITHER_2x2)

/*Perform texture fetch and blending for both TMUs*/
static inline void voodoo_tmu_fetch_and_blend(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int x)
{
        int r,g,b,a;
        int c_reverse, a_reverse;
//        int c_reverse1, a_reverse1;
        int factor_r = 0, factor_g = 0, factor_b = 0, factor_a = 0;
                                                
        voodoo_tmu_fetch(voodoo, params, state, 1, x);

        if ((params->textureMode[1] & TEXTUREMODE_TRILINEAR) && (state->lod & 1))
        {
                c_reverse = tc_reverse_blend;
                a_reverse = tca_reverse_blend;
        }
        else
        {
                c_reverse = !tc_reverse_blend;
                a_reverse = !tca_reverse_blend;
        }
/*        c_reverse1 = c_reverse;
        a_reverse1 = a_reverse;*/
        if (tc_sub_clocal_1)
        {
                switch (tc_mselect_1)
                {
                        case TC_MSELECT_ZERO:
                        factor_r = factor_g = factor_b = 0;
                        break;
                        case TC_MSELECT_CLOCAL:
                        factor_r = state->tex_r[1];
                        factor_g = state->tex_g[1];
                        factor_b = state->tex_b[1];
                        break;
                        case TC_MSELECT_AOTHER:
                        factor_r = factor_g = factor_b = 0;
                        break;
                        case TC_MSELECT_ALOCAL:
                        factor_r = factor_g = factor_b = state->tex_a[1];
                        break;
                        case TC_MSELECT_DETAIL:
                        factor_r = (params->detail_bias[1] - state->lod) << params->detail_scale[1];
                        if (factor_r > params->detail_max[1])
                                factor_r = params->detail_max[1];
                        factor_g = factor_b = factor_r;
                        break;
                        case TC_MSELECT_LOD_FRAC:
                        factor_r = factor_g = factor_b = state->lod_frac[1];
                        break;
                }
                if (!c_reverse)
                {
                        r = (-state->tex_r[1] * (factor_r + 1)) >> 8;
                        g = (-state->tex_g[1] * (factor_g + 1)) >> 8;
                        b = (-state->tex_b[1] * (factor_b + 1)) >> 8;
                }
                else
                {
                        r = (-state->tex_r[1] * ((factor_r^0xff) + 1)) >> 8;
                        g = (-state->tex_g[1] * ((factor_g^0xff) + 1)) >> 8;
                        b = (-state->tex_b[1] * ((factor_b^0xff) + 1)) >> 8;
                }
                if (tc_add_clocal_1)
                {
                        r += state->tex_r[1];
                        g += state->tex_g[1];
                        b += state->tex_b[1];
                }
                else if (tc_add_alocal_1)
                {
                        r += state->tex_a[1];
                        g += state->tex_a[1];
                        b += state->tex_a[1];
                }
                state->tex_r[1] = CLAMP(r);
                state->tex_g[1] = CLAMP(g);
                state->tex_b[1] = CLAMP(b);
        }
        if (tca_sub_clocal_1)
        {
                switch (tca_mselect_1)
                {
                        case TCA_MSELECT_ZERO:
                        factor_a = 0;
                        break;
                        case TCA_MSELECT_CLOCAL:
                        factor_a = state->tex_a[1];
                        break;
                        case TCA_MSELECT_AOTHER:
                        factor_a = 0;
                        break;
                        case TCA_MSELECT_ALOCAL:
                        factor_a = state->tex_a[1];
                        break;
                        case TCA_MSELECT_DETAIL:
                        factor_a = (params->detail_bias[1] - state->lod) << params->detail_scale[1];
                        if (factor_a > params->detail_max[1])
                                factor_a = params->detail_max[1];
                        break;
                        case TCA_MSELECT_LOD_FRAC:
                        factor_a = state->lod_frac[1];
                        break;
                }
                if (!a_reverse)
                        a = (-state->tex_a[1] * ((factor_a ^ 0xff) + 1)) >> 8;
                else
                        a = (-state->tex_a[1] * (factor_a + 1)) >> 8;
                if (tca_add_clocal_1 || tca_add_alocal_1)
                        a += state->tex_a[1];
                state->tex_a[1] = CLAMP(a);
        }


        voodoo_tmu_fetch(voodoo, params, state, 0, x);

        if ((params->textureMode[0] & TEXTUREMODE_TRILINEAR) && (state->lod & 1))
        {
                c_reverse = tc_reverse_blend;
                a_reverse = tca_reverse_blend;
        }
        else
        {
                c_reverse = !tc_reverse_blend;
                a_reverse = !tca_reverse_blend;
        }
                                                
        if (!tc_zero_other)
        {
                r = state->tex_r[1];
                g = state->tex_g[1];
                b = state->tex_b[1];
        }
        else
                r = g = b = 0;
        if (tc_sub_clocal)
        {
                r -= state->tex_r[0];
                g -= state->tex_g[0];
                b -= state->tex_b[0];
        }
        switch (tc_mselect)
        {
                case TC_MSELECT_ZERO:
                factor_r = factor_g = factor_b = 0;
                break;
                case TC_MSELECT_CLOCAL:
                factor_r = state->tex_r[0];
                factor_g = state->tex_g[0];
                factor_b = state->tex_b[0];
                break;
                case TC_MSELECT_AOTHER:
                factor_r = factor_g = factor_b = state->tex_a[1];
                break;
                case TC_MSELECT_ALOCAL:
                factor_r = factor_g = factor_b = state->tex_a[0];
                break;
                case TC_MSELECT_DETAIL:
                factor_r = (params->detail_bias[0] - state->lod) << params->detail_scale[0];
                if (factor_r > params->detail_max[0])
                        factor_r = params->detail_max[0];
                factor_g = factor_b = factor_r;
                break;
                case TC_MSELECT_LOD_FRAC:
                factor_r = factor_g = factor_b = state->lod_frac[0];
                break;
        }
        if (!c_reverse)
        {
                r = (r * (factor_r + 1)) >> 8;
                g = (g * (factor_g + 1)) >> 8;
                b = (b * (factor_b + 1)) >> 8;
        }
        else
        {
                r = (r * ((factor_r^0xff) + 1)) >> 8;
                g = (g * ((factor_g^0xff) + 1)) >> 8;
                b = (b * ((factor_b^0xff) + 1)) >> 8;
        }
        if (tc_add_clocal)
        {
                r += state->tex_r[0];
                g += state->tex_g[0];
                b += state->tex_b[0];
        }
        else if (tc_add_alocal)
        {
                r += state->tex_a[0];
                g += state->tex_a[0];
                b += state->tex_a[0];
        }
                                        
        if (!tca_zero_other)
                a = state->tex_a[1];
        else
                a = 0;
        if (tca_sub_clocal)
                a -= state->tex_a[0];
        switch (tca_mselect)
        {
                case TCA_MSELECT_ZERO:
                factor_a = 0;
                break;
                case TCA_MSELECT_CLOCAL:
                factor_a = state->tex_a[0];
                break;
                case TCA_MSELECT_AOTHER:
                factor_a = state->tex_a[1];
                break;
                case TCA_MSELECT_ALOCAL:
                factor_a = state->tex_a[0];
                break;
                case TCA_MSELECT_DETAIL:
                factor_a = (params->detail_bias[0] - state->lod) << params->detail_scale[0];
                if (factor_a > params->detail_max[0])
                        factor_a = params->detail_max[0];
                break;
                case TCA_MSELECT_LOD_FRAC:
                factor_a = state->lod_frac[0];
                break;
        }
        if (a_reverse)
                a = (a * ((factor_a ^ 0xff) + 1)) >> 8;
        else
                a = (a * (factor_a + 1)) >> 8;
        if (tca_add_clocal || tca_add_alocal)
                a += state->tex_a[0];


        state->tex_r[0] = CLAMP(r);
        state->tex_g[0] = CLAMP(g);
        state->tex_b[0] = CLAMP(b);
        state->tex_a[0] = CLAMP(a);
                                        
        if (tc_invert_output)
        {
                state->tex_r[0] ^= 0xff;
                state->tex_g[0] ^= 0xff;
                state->tex_b[0] ^= 0xff;
        }
        if (tca_invert_output)
                state->tex_a[0] ^= 0xff;
}

#if ((defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined _WIN32) && !(defined __amd64__) && (defined USE_DYNAREC))
#include "vid_voodoo_codegen_x86.h"
#elif ((defined __amd64__) && (defined USE_DYNAREC))
#include "vid_voodoo_codegen_x86-64.h"
#else
#define NO_CODEGEN
static int voodoo_recomp = 0;
#endif

static void voodoo_half_triangle(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state, int ystart, int yend, int odd_even)
{
/*        int rgb_sel                 = params->fbzColorPath & 3;
        int a_sel                   = (params->fbzColorPath >> 2) & 3;
        int cc_localselect          = params->fbzColorPath & (1 << 4);
        int cca_localselect         = (params->fbzColorPath >> 5) & 3;
        int cc_localselect_override = params->fbzColorPath & (1 << 7);
        int cc_zero_other           = params->fbzColorPath & (1 << 8);
        int cc_sub_clocal           = params->fbzColorPath & (1 << 9);
        int cc_mselect              = (params->fbzColorPath >> 10) & 7;
        int cc_reverse_blend        = params->fbzColorPath & (1 << 13);
        int cc_add                  = (params->fbzColorPath >> 14) & 3;
        int cc_add_alocal           = params->fbzColorPath & (1 << 15);
        int cc_invert_output        = params->fbzColorPath & (1 << 16);
        int cca_zero_other          = params->fbzColorPath & (1 << 17);
        int cca_sub_clocal          = params->fbzColorPath & (1 << 18);
        int cca_mselect             = (params->fbzColorPath >> 19) & 7;
        int cca_reverse_blend       = params->fbzColorPath & (1 << 22);
        int cca_add                 = (params->fbzColorPath >> 23) & 3;
        int cca_invert_output       = params->fbzColorPath & (1 << 25);
        int src_afunc = (params->alphaMode >> 8) & 0xf;
        int dest_afunc = (params->alphaMode >> 12) & 0xf;
        int alpha_func = (params->alphaMode >> 1) & 7;
        int a_ref = params->alphaMode >> 24;
        int depth_op = (params->fbzMode >> 5) & 7;
        int dither = params->fbzMode & FBZ_DITHER;*/
        int texels;
        int c;
#ifndef NO_CODEGEN
        uint8_t (*voodoo_draw)(voodoo_state_t *state, voodoo_params_t *params, int x, int real_y);
#endif
	uint8_t cother_r = 0, cother_g = 0, cother_b = 0;
        int y_diff = SLI_ENABLED ? 2 : 1;

        if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH ||
            (params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL)
                texels = 1;
        else
                texels = 2;

        state->clamp_s[0] = params->textureMode[0] & TEXTUREMODE_TCLAMPS;
        state->clamp_t[0] = params->textureMode[0] & TEXTUREMODE_TCLAMPT;
        state->clamp_s[1] = params->textureMode[1] & TEXTUREMODE_TCLAMPS;
        state->clamp_t[1] = params->textureMode[1] & TEXTUREMODE_TCLAMPT;
//        int last_x;
//        voodoo_log("voodoo_triangle : bottom-half %X %X %X %X %X %i  %i %i %i\n", xstart, xend, dx1, dx2, dx2 * 36, xdir,  y, yend, ydir);

        for (c = 0; c <= LOD_MAX; c++)
        {
                state->tex[0][c] = &voodoo->texture_cache[0][params->tex_entry[0]].data[texture_offset[c]];
                state->tex[1][c] = &voodoo->texture_cache[1][params->tex_entry[1]].data[texture_offset[c]];
        }
        
        state->tformat = params->tformat[0];

        state->tex_w_mask[0] = params->tex_w_mask[0];
        state->tex_h_mask[0] = params->tex_h_mask[0];
        state->tex_shift[0] = params->tex_shift[0];
        state->tex_lod[0] = params->tex_lod[0];
        state->tex_w_mask[1] = params->tex_w_mask[1];
        state->tex_h_mask[1] = params->tex_h_mask[1];
        state->tex_shift[1] = params->tex_shift[1];
        state->tex_lod[1] = params->tex_lod[1];

        if ((params->fbzMode & 1) && (ystart < params->clipLowY))
        {
                int dy = params->clipLowY - ystart;

                state->base_r += params->dRdY*dy;
                state->base_g += params->dGdY*dy;
                state->base_b += params->dBdY*dy;
                state->base_a += params->dAdY*dy;
                state->base_z += params->dZdY*dy;
                state->tmu[0].base_s += params->tmu[0].dSdY*dy;
                state->tmu[0].base_t += params->tmu[0].dTdY*dy;
                state->tmu[0].base_w += params->tmu[0].dWdY*dy;
                state->tmu[1].base_s += params->tmu[1].dSdY*dy;
                state->tmu[1].base_t += params->tmu[1].dTdY*dy;
                state->tmu[1].base_w += params->tmu[1].dWdY*dy;
                state->base_w += params->dWdY*dy;
                state->xstart += state->dx1*dy;
                state->xend   += state->dx2*dy;

                ystart = params->clipLowY;
        }

        if ((params->fbzMode & 1) && (yend >= params->clipHighY))
                yend = params->clipHighY-1;

        state->y = ystart;
//        yend--;

        if (SLI_ENABLED)
        {
                int test_y;
                
                if (params->fbzMode & (1 << 17))
                        test_y = (voodoo->v_disp-1) - state->y;
                else
                        test_y = state->y;
                        
                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (test_y & 1)) ||
                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(test_y & 1)))
                {
                        state->y++;

                        state->base_r += params->dRdY;
                        state->base_g += params->dGdY;
                        state->base_b += params->dBdY;
                        state->base_a += params->dAdY;
                        state->base_z += params->dZdY;
                        state->tmu[0].base_s += params->tmu[0].dSdY;
                        state->tmu[0].base_t += params->tmu[0].dTdY;
                        state->tmu[0].base_w += params->tmu[0].dWdY;
                        state->tmu[1].base_s += params->tmu[1].dSdY;
                        state->tmu[1].base_t += params->tmu[1].dTdY;
                        state->tmu[1].base_w += params->tmu[1].dWdY;
                        state->base_w += params->dWdY;
                        state->xstart += state->dx1;
                        state->xend += state->dx2;
                }
        } 
#ifndef NO_CODEGEN
        if (voodoo->use_recompiler)
                voodoo_draw = voodoo_get_block(voodoo, params, state, odd_even);
        else
                voodoo_draw = NULL;
#endif
              
        if (voodoo_output)
                voodoo_log("dxAB=%08x dxBC=%08x dxAC=%08x\n", state->dxAB, state->dxBC, state->dxAC);
//        voodoo_log("Start %i %i\n", ystart, voodoo->fbzMode & (1 << 17));

        for (; state->y < yend; state->y += y_diff)
        {
                int x, x2;
                int real_y = (state->y << 4) + 8;
                int start_x, start_x2;
                int dx;
                uint16_t *fb_mem, *aux_mem;

                state->ir = state->base_r;
                state->ig = state->base_g;
                state->ib = state->base_b;
                state->ia = state->base_a;
                state->z = state->base_z;
                state->tmu0_s = state->tmu[0].base_s;
                state->tmu0_t = state->tmu[0].base_t;
                state->tmu0_w = state->tmu[0].base_w;
                state->tmu1_s = state->tmu[1].base_s;
                state->tmu1_t = state->tmu[1].base_t;
                state->tmu1_w = state->tmu[1].base_w;
                state->w = state->base_w;

                x = (state->vertexAx << 12) + ((state->dxAC * (real_y - state->vertexAy)) >> 4);

                if (real_y < state->vertexBy)
                        x2 = (state->vertexAx << 12) + ((state->dxAB * (real_y - state->vertexAy)) >> 4);
                else
                        x2 = (state->vertexBx << 12) + ((state->dxBC * (real_y - state->vertexBy)) >> 4);

                if (params->fbzMode & (1 << 17))
                        real_y = (voodoo->v_disp-1) - (real_y >> 4);
                else
                        real_y >>= 4;

                if (SLI_ENABLED)
                {
                        if (((real_y >> 1) & voodoo->odd_even_mask) != odd_even)
                                goto next_line;
                }
                else
                {
                        if ((real_y & voodoo->odd_even_mask) != odd_even)
                                goto next_line;
                }

                start_x = x;

                if (state->xdir > 0)
                        x2 -= (1 << 16);
                else
                        x -= (1 << 16);
                dx = ((x + 0x7000) >> 16) - (((state->vertexAx << 12) + 0x7000) >> 16);
                start_x2 = x + 0x7000;
                x = (x + 0x7000) >> 16;
                x2 = (x2 + 0x7000) >> 16;

                if (voodoo_output)
                        voodoo_log("%03i:%03i : Ax=%08x start_x=%08x  dSdX=%016llx dx=%08x  s=%08x -> ", x, state->y, state->vertexAx << 8, start_x, params->tmu[0].dTdX, dx, state->tmu0_t);

                state->ir += (params->dRdX * dx);
                state->ig += (params->dGdX * dx);
                state->ib += (params->dBdX * dx);
                state->ia += (params->dAdX * dx);
                state->z += (params->dZdX * dx);
                state->tmu0_s += (params->tmu[0].dSdX * dx);
                state->tmu0_t += (params->tmu[0].dTdX * dx);
                state->tmu0_w += (params->tmu[0].dWdX * dx);
                state->tmu1_s += (params->tmu[1].dSdX * dx);
                state->tmu1_t += (params->tmu[1].dTdX * dx);
                state->tmu1_w += (params->tmu[1].dWdX * dx);
                state->w += (params->dWdX * dx);

                if (voodoo_output)
                        voodoo_log("%08llx %lli %lli\n", state->tmu0_t, state->tmu0_t >> (18+state->lod), (state->tmu0_t + (1 << (17+state->lod))) >> (18+state->lod));

                if (params->fbzMode & 1)
                {
                        if (state->xdir > 0)
                        {
                                if (x < params->clipLeft)
                                {
                                        int dx = params->clipLeft - x;

                                        state->ir += params->dRdX*dx;
                                        state->ig += params->dGdX*dx;
                                        state->ib += params->dBdX*dx;
                                        state->ia += params->dAdX*dx;
                                        state->z += params->dZdX*dx;
                                        state->tmu0_s += params->tmu[0].dSdX*dx;
                                        state->tmu0_t += params->tmu[0].dTdX*dx;
                                        state->tmu0_w += params->tmu[0].dWdX*dx;
                                        state->tmu1_s += params->tmu[1].dSdX*dx;
                                        state->tmu1_t += params->tmu[1].dTdX*dx;
                                        state->tmu1_w += params->tmu[1].dWdX*dx;
                                        state->w += params->dWdX*dx;
                                        
                                        x = params->clipLeft;
                                }
                                if (x2 >= params->clipRight)
                                        x2 = params->clipRight-1;
                        }
                        else
                        {
                                if (x >= params->clipRight)
                                {
                                        int dx = (params->clipRight-1) - x;

                                        state->ir += params->dRdX*dx;
                                        state->ig += params->dGdX*dx;
                                        state->ib += params->dBdX*dx;
                                        state->ia += params->dAdX*dx;
                                        state->z += params->dZdX*dx;
                                        state->tmu0_s += params->tmu[0].dSdX*dx;
                                        state->tmu0_t += params->tmu[0].dTdX*dx;
                                        state->tmu0_w += params->tmu[0].dWdX*dx;
                                        state->tmu1_s += params->tmu[1].dSdX*dx;
                                        state->tmu1_t += params->tmu[1].dTdX*dx;
                                        state->tmu1_w += params->tmu[1].dWdX*dx;
                                        state->w += params->dWdX*dx;
                                        
                                        x = params->clipRight-1;
                                }
                                if (x2 < params->clipLeft)
                                        x2 = params->clipLeft;
                        }
                }
                
                if (x2 < x && state->xdir > 0)
                        goto next_line;
                if (x2 > x && state->xdir < 0)
                        goto next_line;

                if (SLI_ENABLED)
                {
                        state->fb_mem = fb_mem = (uint16_t *)&voodoo->fb_mem[params->draw_offset + ((real_y >> 1) * voodoo->row_width)];
                        state->aux_mem = aux_mem = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + ((real_y >> 1) * voodoo->row_width)) & voodoo->fb_mask];
                }
                else
                {
                        state->fb_mem = fb_mem = (uint16_t *)&voodoo->fb_mem[params->draw_offset + (real_y * voodoo->row_width)];
                        state->aux_mem = aux_mem = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + (real_y * voodoo->row_width)) & voodoo->fb_mask];
                }
                
                if (voodoo_output)
                        voodoo_log("%03i: x=%08x x2=%08x xstart=%08x xend=%08x dx=%08x start_x2=%08x\n", state->y, x, x2, state->xstart, state->xend, dx, start_x2);

                state->pixel_count = 0;
                state->texel_count = 0;
                state->x = x;
                state->x2 = x2;
#ifndef NO_CODEGEN
                if (voodoo->use_recompiler)
                {
                        voodoo_draw(state, params, x, real_y);
                }
                else
#endif
                do
                {
                        start_x = x;
                        state->x = x;
                        voodoo->pixel_count[odd_even]++;
                        voodoo->texel_count[odd_even] += texels;
                        voodoo->fbiPixelsIn++;
                        
                        if (voodoo_output)
                                voodoo_log("  X=%03i T=%08x\n", x, state->tmu0_t);
//                        if (voodoo->fbzMode & FBZ_RGB_WMASK)
                        {
                                int update = 1;
                                uint8_t aother;
                                uint8_t clocal_r, clocal_g, clocal_b, alocal;
                                int src_r = 0, src_g = 0, src_b = 0, src_a = 0;
                                int msel_r, msel_g, msel_b, msel_a;
                                uint8_t dest_r, dest_g, dest_b, dest_a;
                                uint16_t dat;
                                int sel;
                                int32_t new_depth, w_depth;

                                if (state->w & 0xffff00000000)
                                        w_depth = 0;
                                else if (!(state->w & 0xffff0000))
                                        w_depth = 0xf001;
                                else
                                {
                                        int exp = voodoo_fls((uint16_t)((uint32_t)state->w >> 16));
                                        int mant = ((~(uint32_t)state->w >> (19 - exp))) & 0xfff;
                                        w_depth = (exp << 12) + mant + 1;
                                        if (w_depth > 0xffff)
                                                w_depth = 0xffff;
                                }

//                                w_depth = CLAMP16(w_depth);
                                
                                if (params->fbzMode & FBZ_W_BUFFER)
                                        new_depth = w_depth;
                                else
                                        new_depth = CLAMP16(state->z >> 12);
                                
                                if (params->fbzMode & FBZ_DEPTH_BIAS)
                                        new_depth = CLAMP16(new_depth + (int16_t)params->zaColor);                                        

                                if (params->fbzMode & FBZ_DEPTH_ENABLE)
                                {
                                        uint16_t old_depth = aux_mem[x];

                                        DEPTH_TEST((params->fbzMode & FBZ_DEPTH_SOURCE) ? (params->zaColor & 0xffff) : new_depth);
                                }
                                
                                dat = fb_mem[x];
                                dest_r = (dat >> 8) & 0xf8;
                                dest_g = (dat >> 3) & 0xfc;
                                dest_b = (dat << 3) & 0xf8;
                                dest_r |= (dest_r >> 5);
                                dest_g |= (dest_g >> 6);
                                dest_b |= (dest_b >> 5);
                                dest_a = 0xff;

                                if (params->fbzColorPath & FBZCP_TEXTURE_ENABLED)
                                {
                                        if ((params->textureMode[0] & TEXTUREMODE_LOCAL_MASK) == TEXTUREMODE_LOCAL || !voodoo->dual_tmus)
                                        {
                                                /*TMU0 only sampling local colour or only one TMU, only sample TMU0*/
                                                voodoo_tmu_fetch(voodoo, params, state, 0, x);
                                        }
                                        else if ((params->textureMode[0] & TEXTUREMODE_MASK) == TEXTUREMODE_PASSTHROUGH)
                                        {
                                                /*TMU0 in pass-through mode, only sample TMU1*/
                                                voodoo_tmu_fetch(voodoo, params, state, 1, x);
                                                
                                                state->tex_r[0] = state->tex_r[1];
                                                state->tex_g[0] = state->tex_g[1];
                                                state->tex_b[0] = state->tex_b[1];
                                                state->tex_a[0] = state->tex_a[1];
                                        }
                                        else
                                        {
                                                voodoo_tmu_fetch_and_blend(voodoo, params, state, x);
                                        }
                                                
                                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                                state->tex_r[0] == params->chromaKey_r &&
                                                state->tex_g[0] == params->chromaKey_g &&
                                                state->tex_b[0] == params->chromaKey_b)
                                        {
                                                voodoo->fbiChromaFail++;
                                                goto skip_pixel;
                                        }
                                }

                                if (voodoo->trexInit1[0] & (1 << 18))
                                {
                                        state->tex_r[0] = state->tex_g[0] = 0;
                                        state->tex_b[0] = voodoo->tmuConfig;
                                }

                                if (cc_localselect_override)
                                        sel = (state->tex_a[0] & 0x80) ? 1 : 0;
                                else
                                        sel = cc_localselect;
                                
                                if (sel)
                                {
                                        clocal_r = (params->color0 >> 16) & 0xff;
                                        clocal_g = (params->color0 >> 8)  & 0xff;
                                        clocal_b =  params->color0        & 0xff;
                                }
                                else
                                {
                                        clocal_r = CLAMP(state->ir >> 12);
                                        clocal_g = CLAMP(state->ig >> 12);
                                        clocal_b = CLAMP(state->ib >> 12);
                                }

                                switch (_rgb_sel)
                                {
                                        case CC_LOCALSELECT_ITER_RGB: /*Iterated RGB*/
                                        cother_r = CLAMP(state->ir >> 12);
                                        cother_g = CLAMP(state->ig >> 12);
                                        cother_b = CLAMP(state->ib >> 12);
                                        break;                                        

                                        case CC_LOCALSELECT_TEX: /*TREX Color Output*/
                                        cother_r = state->tex_r[0];
                                        cother_g = state->tex_g[0];
                                        cother_b = state->tex_b[0];
                                        break;
                                                
                                        case CC_LOCALSELECT_COLOR1: /*Color1 RGB*/
                                        cother_r = (params->color1 >> 16) & 0xff;
                                        cother_g = (params->color1 >> 8)  & 0xff;
                                        cother_b =  params->color1        & 0xff;
                                        break;
                                        
                                        case CC_LOCALSELECT_LFB: /*Linear Frame Buffer*/
                                        cother_r = src_r;
                                        cother_g = src_g;
                                        cother_b = src_b;
                                        break;
                                }

                                switch (cca_localselect)
                                {
                                        case CCA_LOCALSELECT_ITER_A:
                                        alocal = CLAMP(state->ia >> 12);
                                        break;

                                        case CCA_LOCALSELECT_COLOR0:
                                        alocal = (params->color0 >> 24) & 0xff;
                                        break;
                                        
                                        case CCA_LOCALSELECT_ITER_Z:
                                        alocal = CLAMP(state->z >> 20);
                                        break;
                                        
                                        default:
                                        fatal("Bad cca_localselect %i\n", cca_localselect);
                                        alocal = 0xff;
                                        break;
                                }
                                
                                switch (a_sel)
                                {
                                        case A_SEL_ITER_A:
                                        aother = CLAMP(state->ia >> 12);
                                        break;
                                        case A_SEL_TEX:
                                        aother = state->tex_a[0];
                                        break;
                                        case A_SEL_COLOR1:
                                        aother = (params->color1 >> 24) & 0xff;
                                        break;
                                        default:
                                        fatal("Bad a_sel %i\n", a_sel);
                                        aother = 0;
                                        break;
                                }
                                
                                if (cc_zero_other)
                                {
                                        src_r = 0;
                                        src_g = 0;                                
                                        src_b = 0;
                                }
                                else
                                {
                                        src_r = cother_r;
                                        src_g = cother_g;
                                        src_b = cother_b;
                                }

                                if (cca_zero_other)
                                        src_a = 0;
                                else
                                        src_a = aother;

                                if (cc_sub_clocal)
                                {
                                        src_r -= clocal_r;
                                        src_g -= clocal_g;
                                        src_b -= clocal_b;
                                }

                                if (cca_sub_clocal)
                                        src_a -= alocal;

                                switch (cc_mselect)
                                {
                                        case CC_MSELECT_ZERO:
                                        msel_r = 0;
                                        msel_g = 0;
                                        msel_b = 0;
                                        break;
                                        case CC_MSELECT_CLOCAL:
                                        msel_r = clocal_r;
                                        msel_g = clocal_g;
                                        msel_b = clocal_b;
                                        break;
                                        case CC_MSELECT_AOTHER:
                                        msel_r = aother;
                                        msel_g = aother;
                                        msel_b = aother;
                                        break;
                                        case CC_MSELECT_ALOCAL:
                                        msel_r = alocal;
                                        msel_g = alocal;
                                        msel_b = alocal;
                                        break;
                                        case CC_MSELECT_TEX:
                                        msel_r = state->tex_a[0];
                                        msel_g = state->tex_a[0];
                                        msel_b = state->tex_a[0];
                                        break;
                                        case CC_MSELECT_TEXRGB:
                                        msel_r = state->tex_r[0];
                                        msel_g = state->tex_g[0];
                                        msel_b = state->tex_b[0];
                                        break;

                                        default:
                                                fatal("Bad cc_mselect %i\n", cc_mselect);
                                        msel_r = 0;
                                        msel_g = 0;
                                        msel_b = 0;
                                        break;
                                }                                        

                                switch (cca_mselect)
                                {
                                        case CCA_MSELECT_ZERO:
                                        msel_a = 0;
                                        break;
                                        case CCA_MSELECT_ALOCAL:
                                        msel_a = alocal;
                                        break;
                                        case CCA_MSELECT_AOTHER:
                                        msel_a = aother;
                                        break;
                                        case CCA_MSELECT_ALOCAL2:
                                        msel_a = alocal;
                                        break;
                                        case CCA_MSELECT_TEX:
                                        msel_a = state->tex_a[0];
                                        break;

                                        default:
                                                fatal("Bad cca_mselect %i\n", cca_mselect);
                                        msel_a = 0;
                                        break;
                                }                                        

                                if (!cc_reverse_blend)
                                {
                                        msel_r ^= 0xff;
                                        msel_g ^= 0xff;
                                        msel_b ^= 0xff;
                                }
                                msel_r++;
                                msel_g++;
                                msel_b++;

                                if (!cca_reverse_blend)
                                        msel_a ^= 0xff;
                                msel_a++;

                                src_r = (src_r * msel_r) >> 8;
                                src_g = (src_g * msel_g) >> 8;
                                src_b = (src_b * msel_b) >> 8;
                                src_a = (src_a * msel_a) >> 8;

                                switch (cc_add)
                                {
                                        case CC_ADD_CLOCAL:
                                        src_r += clocal_r;
                                        src_g += clocal_g;
                                        src_b += clocal_b;
                                        break;
                                        case CC_ADD_ALOCAL:
                                        src_r += alocal;
                                        src_g += alocal;
                                        src_b += alocal;
                                        break;
                                        case 0:
                                        break;
                                        default:
                                        fatal("Bad cc_add %i\n", cc_add);
                                }

                                if (cca_add)
                                        src_a += alocal;
                                
                                src_r = CLAMP(src_r);
                                src_g = CLAMP(src_g);
                                src_b = CLAMP(src_b);
                                src_a = CLAMP(src_a);

                                if (cc_invert_output)
                                {
                                        src_r ^= 0xff;
                                        src_g ^= 0xff;
                                        src_b ^= 0xff;
                                }
                                if (cca_invert_output)
                                        src_a ^= 0xff;

                                if (params->fogMode & FOG_ENABLE)
                                        APPLY_FOG(src_r, src_g, src_b, state->z, state->ia, state->w);
                                
                                if (params->alphaMode & 1)
                                        ALPHA_TEST(src_a);

                                if (params->alphaMode & (1 << 4))
                                        ALPHA_BLEND(src_r, src_g, src_b, src_a);

                                if (update)
                                {
                                        if (dither)
                                        {
                                                if (dither2x2)
                                                {
                                                        src_r = dither_rb2x2[src_r][real_y & 1][x & 1];
                                                        src_g =  dither_g2x2[src_g][real_y & 1][x & 1];
                                                        src_b = dither_rb2x2[src_b][real_y & 1][x & 1];
                                                }
                                                else
                                                {
                                                        src_r = dither_rb[src_r][real_y & 3][x & 3];
                                                        src_g =  dither_g[src_g][real_y & 3][x & 3];
                                                        src_b = dither_rb[src_b][real_y & 3][x & 3];
                                                }
                                        }
                                        else
                                        {
                                                src_r >>= 3;
                                                src_g >>= 2;
                                                src_b >>= 3;
                                        }

                                        if (params->fbzMode & FBZ_RGB_WMASK)
                                                fb_mem[x] = src_b | (src_g << 5) | (src_r << 11);

                                        if ((params->fbzMode & (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE)) == (FBZ_DEPTH_WMASK | FBZ_DEPTH_ENABLE))
                                                aux_mem[x] = new_depth;
                                }
                        }
                        voodoo_output &= ~2;
                        voodoo->fbiPixelsOut++;
skip_pixel:
                        if (state->xdir > 0)
                        {                                
                                state->ir += params->dRdX;
                                state->ig += params->dGdX;
                                state->ib += params->dBdX;
                                state->ia += params->dAdX;
                                state->z += params->dZdX;
                                state->tmu0_s += params->tmu[0].dSdX;
                                state->tmu0_t += params->tmu[0].dTdX;
                                state->tmu0_w += params->tmu[0].dWdX;
                                state->tmu1_s += params->tmu[1].dSdX;
                                state->tmu1_t += params->tmu[1].dTdX;
                                state->tmu1_w += params->tmu[1].dWdX;
                                state->w += params->dWdX;
                        }
                        else
                        {                                
                                state->ir -= params->dRdX;
                                state->ig -= params->dGdX;
                                state->ib -= params->dBdX;
                                state->ia -= params->dAdX;
                                state->z -= params->dZdX;
                                state->tmu0_s -= params->tmu[0].dSdX;
                                state->tmu0_t -= params->tmu[0].dTdX;
                                state->tmu0_w -= params->tmu[0].dWdX;
                                state->tmu1_s -= params->tmu[1].dSdX;
                                state->tmu1_t -= params->tmu[1].dTdX;
                                state->tmu1_w -= params->tmu[1].dWdX;
                                state->w -= params->dWdX;
                        }

                        x += state->xdir;
                } while (start_x != x2);

                voodoo->pixel_count[odd_even] += state->pixel_count;
                voodoo->texel_count[odd_even] += state->texel_count;
                voodoo->fbiPixelsIn += state->pixel_count;
                
                if (voodoo->params.draw_offset == voodoo->params.front_offset)
                        voodoo->dirty_line[real_y >> 1] = 1;
next_line:
                if (SLI_ENABLED)
                {
                        state->base_r += params->dRdY;
                        state->base_g += params->dGdY;
                        state->base_b += params->dBdY;
                        state->base_a += params->dAdY;
                        state->base_z += params->dZdY;
                        state->tmu[0].base_s += params->tmu[0].dSdY;
                        state->tmu[0].base_t += params->tmu[0].dTdY;
                        state->tmu[0].base_w += params->tmu[0].dWdY;
                        state->tmu[1].base_s += params->tmu[1].dSdY;
                        state->tmu[1].base_t += params->tmu[1].dTdY;
                        state->tmu[1].base_w += params->tmu[1].dWdY;
                        state->base_w += params->dWdY;
                        state->xstart += state->dx1;
                        state->xend += state->dx2;
                }
                state->base_r += params->dRdY;
                state->base_g += params->dGdY;
                state->base_b += params->dBdY;
                state->base_a += params->dAdY;
                state->base_z += params->dZdY;
                state->tmu[0].base_s += params->tmu[0].dSdY;
                state->tmu[0].base_t += params->tmu[0].dTdY;
                state->tmu[0].base_w += params->tmu[0].dWdY;
                state->tmu[1].base_s += params->tmu[1].dSdY;
                state->tmu[1].base_t += params->tmu[1].dTdY;
                state->tmu[1].base_w += params->tmu[1].dWdY;
                state->base_w += params->dWdY;
                state->xstart += state->dx1;
                state->xend += state->dx2;                
        }
        
        voodoo->texture_cache[0][params->tex_entry[0]].refcount_r[odd_even]++;
        voodoo->texture_cache[1][params->tex_entry[1]].refcount_r[odd_even]++;
}
        
static void voodoo_triangle(voodoo_t *voodoo, voodoo_params_t *params, int odd_even)
{
        voodoo_state_t state;
        int vertexAy_adjusted;
        int vertexCy_adjusted;
        int dx, dy;
        
        uint64_t tempdx, tempdy;
        uint64_t tempLOD;
        int LOD;
        int lodbias;
        
	memset(&state, 0x00, sizeof(voodoo_state_t));
        voodoo->tri_count++;
        
        dx = 8 - (params->vertexAx & 0xf);
        if ((params->vertexAx & 0xf) > 8)
                dx += 16;
        dy = 8 - (params->vertexAy & 0xf);
        if ((params->vertexAy & 0xf) > 8)
                dy += 16;

/*        voodoo_log("voodoo_triangle %i %i %i : vA %f, %f  vB %f, %f  vC %f, %f f %i,%i %08x %08x %08x,%08x tex=%i,%i fogMode=%08x\n", odd_even, voodoo->params_read_idx[odd_even], voodoo->params_read_idx[odd_even] & PARAM_MASK, (float)params->vertexAx / 16.0, (float)params->vertexAy / 16.0, 
                                                                     (float)params->vertexBx / 16.0, (float)params->vertexBy / 16.0, 
                                                                     (float)params->vertexCx / 16.0, (float)params->vertexCy / 16.0,
                                                                     (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) ? params->tformat[0] : 0, 
                                                                     (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) ? params->tformat[1] : 0, params->fbzColorPath, params->alphaMode, params->textureMode[0],params->textureMode[1], params->tex_entry[0],params->tex_entry[1], params->fogMode);*/

        state.base_r = params->startR;
        state.base_g = params->startG;
        state.base_b = params->startB;
        state.base_a = params->startA;
        state.base_z = params->startZ;
        state.tmu[0].base_s = params->tmu[0].startS;
        state.tmu[0].base_t = params->tmu[0].startT;
        state.tmu[0].base_w = params->tmu[0].startW;
        state.tmu[1].base_s = params->tmu[1].startS;
        state.tmu[1].base_t = params->tmu[1].startT;
        state.tmu[1].base_w = params->tmu[1].startW;
        state.base_w = params->startW;

        if (params->fbzColorPath & FBZ_PARAM_ADJUST)
        {
                state.base_r += (dx*params->dRdX + dy*params->dRdY) >> 4;
                state.base_g += (dx*params->dGdX + dy*params->dGdY) >> 4;
                state.base_b += (dx*params->dBdX + dy*params->dBdY) >> 4;
                state.base_a += (dx*params->dAdX + dy*params->dAdY) >> 4;
                state.base_z += (dx*params->dZdX + dy*params->dZdY) >> 4;
                state.tmu[0].base_s += (dx*params->tmu[0].dSdX + dy*params->tmu[0].dSdY) >> 4;
                state.tmu[0].base_t += (dx*params->tmu[0].dTdX + dy*params->tmu[0].dTdY) >> 4;
                state.tmu[0].base_w += (dx*params->tmu[0].dWdX + dy*params->tmu[0].dWdY) >> 4;
                state.tmu[1].base_s += (dx*params->tmu[1].dSdX + dy*params->tmu[1].dSdY) >> 4;
                state.tmu[1].base_t += (dx*params->tmu[1].dTdX + dy*params->tmu[1].dTdY) >> 4;
                state.tmu[1].base_w += (dx*params->tmu[1].dWdX + dy*params->tmu[1].dWdY) >> 4;
                state.base_w += (dx*params->dWdX + dy*params->dWdY) >> 4;
        }

        tris++;

        state.vertexAy = params->vertexAy & ~0xffff0000;
        if (state.vertexAy & 0x8000)
                state.vertexAy |= 0xffff0000;
        state.vertexBy = params->vertexBy & ~0xffff0000;
        if (state.vertexBy & 0x8000)
                state.vertexBy |= 0xffff0000;
        state.vertexCy = params->vertexCy & ~0xffff0000;
        if (state.vertexCy & 0x8000)
                state.vertexCy |= 0xffff0000;

        state.vertexAx = params->vertexAx & ~0xffff0000;
        if (state.vertexAx & 0x8000)
                state.vertexAx |= 0xffff0000;
        state.vertexBx = params->vertexBx & ~0xffff0000;
        if (state.vertexBx & 0x8000)
                state.vertexBx |= 0xffff0000;
        state.vertexCx = params->vertexCx & ~0xffff0000;
        if (state.vertexCx & 0x8000)
                state.vertexCx |= 0xffff0000;

        vertexAy_adjusted = (state.vertexAy+7) >> 4;
        vertexCy_adjusted = (state.vertexCy+7) >> 4;

        if (state.vertexBy - state.vertexAy)
                state.dxAB = (int)((((int64_t)state.vertexBx << 12) - ((int64_t)state.vertexAx << 12)) << 4) / (int)(state.vertexBy - state.vertexAy);
        else
                state.dxAB = 0;
        if (state.vertexCy - state.vertexAy)
                state.dxAC = (int)((((int64_t)state.vertexCx << 12) - ((int64_t)state.vertexAx << 12)) << 4) / (int)(state.vertexCy - state.vertexAy);
        else
                state.dxAC = 0;
        if (state.vertexCy - state.vertexBy)
                state.dxBC = (int)((((int64_t)state.vertexCx << 12) - ((int64_t)state.vertexBx << 12)) << 4) / (int)(state.vertexCy - state.vertexBy);
        else
                state.dxBC = 0;

        state.lod_min[0] = (params->tLOD[0] & 0x3f) << 6;
        state.lod_max[0] = ((params->tLOD[0] >> 6) & 0x3f) << 6;
        if (state.lod_max[0] > 0x800)
                state.lod_max[0] = 0x800;
        state.lod_min[1] = (params->tLOD[1] & 0x3f) << 6;
        state.lod_max[1] = ((params->tLOD[1] >> 6) & 0x3f) << 6;
        if (state.lod_max[1] > 0x800)
                state.lod_max[1] = 0x800;
                
        state.xstart = state.xend = state.vertexAx << 8;
        state.xdir = params->sign ? -1 : 1;

        state.y = (state.vertexAy + 8) >> 4;
        state.ydir = 1;


        tempdx = (params->tmu[0].dSdX >> 14) * (params->tmu[0].dSdX >> 14) + (params->tmu[0].dTdX >> 14) * (params->tmu[0].dTdX >> 14);
        tempdy = (params->tmu[0].dSdY >> 14) * (params->tmu[0].dSdY >> 14) + (params->tmu[0].dTdY >> 14) * (params->tmu[0].dTdY >> 14);
        
        if (tempdx > tempdy)
                tempLOD = tempdx;
        else
                tempLOD = tempdy;

        LOD = (int)(log2((double)tempLOD / (double)(1ULL << 36)) * 256);
        LOD >>= 2;

        lodbias = (params->tLOD[0] >> 12) & 0x3f;
        if (lodbias & 0x20)
                lodbias |= ~0x3f;
        state.tmu[0].lod = LOD + (lodbias << 6);


        tempdx = (params->tmu[1].dSdX >> 14) * (params->tmu[1].dSdX >> 14) + (params->tmu[1].dTdX >> 14) * (params->tmu[1].dTdX >> 14);
        tempdy = (params->tmu[1].dSdY >> 14) * (params->tmu[1].dSdY >> 14) + (params->tmu[1].dTdY >> 14) * (params->tmu[1].dTdY >> 14);
        
        if (tempdx > tempdy)
                tempLOD = tempdx;
        else
                tempLOD = tempdy;

        LOD = (int)(log2((double)tempLOD / (double)(1ULL << 36)) * 256);
        LOD >>= 2;

        lodbias = (params->tLOD[1] >> 12) & 0x3f;
        if (lodbias & 0x20)
                lodbias |= ~0x3f;
        state.tmu[1].lod = LOD + (lodbias << 6);


        voodoo_half_triangle(voodoo, params, &state, vertexAy_adjusted, vertexCy_adjusted, odd_even);
}

static inline void wake_render_thread(voodoo_t *voodoo)
{
        thread_set_event(voodoo->wake_render_thread[0]); /*Wake up render thread if moving from idle*/
        if (voodoo->render_threads == 2)
                thread_set_event(voodoo->wake_render_thread[1]); /*Wake up render thread if moving from idle*/
}

static inline void wait_for_render_thread_idle(voodoo_t *voodoo)
{
        while (!PARAM_EMPTY_1 || (voodoo->render_threads == 2 && !PARAM_EMPTY_2) || voodoo->render_voodoo_busy[0] || (voodoo->render_threads == 2 && voodoo->render_voodoo_busy[1]))
        {
                wake_render_thread(voodoo);
                if (!PARAM_EMPTY_1 || voodoo->render_voodoo_busy[0])
                        thread_wait_event(voodoo->render_not_full_event[0], 1);
                if (voodoo->render_threads == 2 && (!PARAM_EMPTY_2 || voodoo->render_voodoo_busy[1]))
                        thread_wait_event(voodoo->render_not_full_event[1], 1);
        }
}

static void render_thread(void *param, int odd_even)
{
        voodoo_t *voodoo = (voodoo_t *)param;
        
        while (1)
        {
                thread_set_event(voodoo->render_not_full_event[odd_even]);
                thread_wait_event(voodoo->wake_render_thread[odd_even], -1);
                thread_reset_event(voodoo->wake_render_thread[odd_even]);
                voodoo->render_voodoo_busy[odd_even] = 1;

                while (!(odd_even ? PARAM_EMPTY_2 : PARAM_EMPTY_1))
                {
                        uint64_t start_time = plat_timer_read();
                        uint64_t end_time;
                        voodoo_params_t *params = &voodoo->params_buffer[voodoo->params_read_idx[odd_even] & PARAM_MASK];
                        
                        voodoo_triangle(voodoo, params, odd_even);

                        voodoo->params_read_idx[odd_even]++;                                                
                        
                        if ((odd_even ? PARAM_ENTRIES_2 : PARAM_ENTRIES_1) > (PARAM_SIZE - 10))
                                thread_set_event(voodoo->render_not_full_event[odd_even]);

                        end_time = plat_timer_read();
                        voodoo->render_time[odd_even] += end_time - start_time;
                }

                voodoo->render_voodoo_busy[odd_even] = 0;
        }
}

static void render_thread_1(void *param)
{
        render_thread(param, 0);
}
static void render_thread_2(void *param)
{
        render_thread(param, 1);
}

static inline void queue_triangle(voodoo_t *voodoo, voodoo_params_t *params)
{
        voodoo_params_t *params_new = &voodoo->params_buffer[voodoo->params_write_idx & PARAM_MASK];

        while (PARAM_FULL_1 || (voodoo->render_threads == 2 && PARAM_FULL_2))
        {
                thread_reset_event(voodoo->render_not_full_event[0]);
                if (voodoo->render_threads == 2)
                        thread_reset_event(voodoo->render_not_full_event[1]);
                if (PARAM_FULL_1)
                {
                        thread_wait_event(voodoo->render_not_full_event[0], -1); /*Wait for room in ringbuffer*/
                }
                if (voodoo->render_threads == 2 && PARAM_FULL_2)
                {
                        thread_wait_event(voodoo->render_not_full_event[1], -1); /*Wait for room in ringbuffer*/
                }
        }
        
        use_texture(voodoo, params, 0);
        if (voodoo->dual_tmus)
                use_texture(voodoo, params, 1);

        memcpy(params_new, params, sizeof(voodoo_params_t));
        
        voodoo->params_write_idx++;
        
        if (PARAM_ENTRIES_1 < 4 || (voodoo->render_threads == 2 && PARAM_ENTRIES_2 < 4))
                wake_render_thread(voodoo);
}

static void voodoo_fastfill(voodoo_t *voodoo, voodoo_params_t *params)
{
        int y;
        int low_y, high_y;

        if (params->fbzMode & (1 << 17))
        {
                high_y = voodoo->v_disp - params->clipLowY;
                low_y = voodoo->v_disp - params->clipHighY;
        }
        else
        {
                low_y = params->clipLowY;
                high_y = params->clipHighY;
        }
        
        if (params->fbzMode & FBZ_RGB_WMASK)
        {
                int r, g, b;
                uint16_t col;

                r = ((params->color1 >> 16) >> 3) & 0x1f;
                g = ((params->color1 >> 8) >> 2) & 0x3f;
                b = (params->color1 >> 3) & 0x1f;
                col = b | (g << 5) | (r << 11);

                if (SLI_ENABLED)
                {
                        for (y = low_y; y < high_y; y += 2)
                        {
                                uint16_t *cbuf = (uint16_t *)&voodoo->fb_mem[(params->draw_offset + (y >> 1) * voodoo->row_width) & voodoo->fb_mask];
                                int x;
                        
                                for (x = params->clipLeft; x < params->clipRight; x++)
                                        cbuf[x] = col;
                        }
                }
                else
                {
                        for (y = low_y; y < high_y; y++)
                        {
                                uint16_t *cbuf = (uint16_t *)&voodoo->fb_mem[(params->draw_offset + y*voodoo->row_width) & voodoo->fb_mask];
                                int x;
                        
                                for (x = params->clipLeft; x < params->clipRight; x++)
                                        cbuf[x] = col;
                        }
                }
        }
        if (params->fbzMode & FBZ_DEPTH_WMASK)
        {        
                if (SLI_ENABLED)
                {
                        for (y = low_y; y < high_y; y += 2)
                        {
                                uint16_t *abuf = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + (y >> 1) * voodoo->row_width) & voodoo->fb_mask];
                                int x;
                
                                for (x = params->clipLeft; x < params->clipRight; x++)
                                        abuf[x] = params->zaColor & 0xffff;
                        }
                }
                else
                {
                        for (y = low_y; y < high_y; y++)
                        {
                                uint16_t *abuf = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + y*voodoo->row_width) & voodoo->fb_mask];
                                int x;
                
                                for (x = params->clipLeft; x < params->clipRight; x++)
                                        abuf[x] = params->zaColor & 0xffff;
                        }
                }
        }
}

enum
{
        SETUPMODE_RGB   = (1 << 0),
        SETUPMODE_ALPHA = (1 << 1),
        SETUPMODE_Z     = (1 << 2),
        SETUPMODE_Wb    = (1 << 3),
        SETUPMODE_W0    = (1 << 4),
        SETUPMODE_S0_T0 = (1 << 5),
        SETUPMODE_W1    = (1 << 6),
        SETUPMODE_S1_T1 = (1 << 7),
        
        SETUPMODE_STRIP_MODE = (1 << 16),
        SETUPMODE_CULLING_ENABLE = (1 << 17),
        SETUPMODE_CULLING_SIGN = (1 << 18),
        SETUPMODE_DISABLE_PINGPONG = (1 << 19)
};

static void triangle_setup(voodoo_t *voodoo)
{
        float dxAB, dxBC, dyAB, dyBC;
        float area;
        int va = 0, vb = 1, vc = 2;
        int reverse_cull = 0;

        if (voodoo->verts[0].sVy < voodoo->verts[1].sVy)
        {
                if (voodoo->verts[1].sVy < voodoo->verts[2].sVy)
                {
                        /* V1>V0, V2>V1, V2>V1>V0*/
                        va = 0; /*OK*/
                        vb = 1;
                        vc = 2;
                }
                else
                {
                        /* V1>V0, V1>V2*/
                        if (voodoo->verts[0].sVy < voodoo->verts[2].sVy)
                        {
                                /* V1>V0, V1>V2, V2>V0, V1>V2>V0*/
                                va = 0;
                                vb = 2;
                                vc = 1;
                                reverse_cull = 1;
                        }
                        else
                        {
                                /* V1>V0, V1>V2, V0>V2, V1>V0>V2*/
                                va = 2;
                                vb = 0;
                                vc = 1;
                        }
                }
        }
        else
        {
                if (voodoo->verts[1].sVy < voodoo->verts[2].sVy)
                {
                        /* V0>V1, V2>V1*/
                        if (voodoo->verts[0].sVy < voodoo->verts[2].sVy)
                        {
                                /* V0>V1, V2>V1, V2>V0, V2>V0>V1*/
                                va = 1;
                                vb = 0;
                                vc = 2;
                                reverse_cull = 1;
                        }
                        else
                        {
                                /* V0>V1, V2>V1, V0>V2, V0>V2>V1*/
                                va = 1;
                                vb = 2;
                                vc = 0;
                        }
                }
                else
                {
                        /*V0>V1>V2*/
                        va = 2;
                        vb = 1;
                        vc = 0;
                        reverse_cull = 1;
                }
        }

        dxAB = voodoo->verts[va].sVx - voodoo->verts[vb].sVx;
        dxBC = voodoo->verts[vb].sVx - voodoo->verts[vc].sVx;
        dyAB = voodoo->verts[va].sVy - voodoo->verts[vb].sVy;
        dyBC = voodoo->verts[vb].sVy - voodoo->verts[vc].sVy;
        
        area = dxAB * dyBC - dxBC * dyAB;

        if (area == 0.0)
        {
                if ((voodoo->sSetupMode & SETUPMODE_CULLING_ENABLE) &&
                    !(voodoo->sSetupMode & SETUPMODE_DISABLE_PINGPONG))
                        voodoo->sSetupMode ^= SETUPMODE_CULLING_SIGN;
                
                return;
        }
                
        dxAB /= area;
        dxBC /= area;
        dyAB /= area;
        dyBC /= area;

        if (voodoo->sSetupMode & SETUPMODE_CULLING_ENABLE)
        {
                int cull_sign = voodoo->sSetupMode & SETUPMODE_CULLING_SIGN;
                int sign = (area < 0.0);
                
                if (!(voodoo->sSetupMode & SETUPMODE_DISABLE_PINGPONG))
                        voodoo->sSetupMode ^= SETUPMODE_CULLING_SIGN;

                if (reverse_cull)
                        sign = !sign;
                
                if (cull_sign && sign)
                        return;
                if (!cull_sign && !sign)
                        return;
        }
        
        voodoo->params.vertexAx = (int32_t)(int16_t)((int32_t)(voodoo->verts[va].sVx * 16.0f) & 0xffff);
        voodoo->params.vertexAy = (int32_t)(int16_t)((int32_t)(voodoo->verts[va].sVy * 16.0f) & 0xffff);
        voodoo->params.vertexBx = (int32_t)(int16_t)((int32_t)(voodoo->verts[vb].sVx * 16.0f) & 0xffff);
        voodoo->params.vertexBy = (int32_t)(int16_t)((int32_t)(voodoo->verts[vb].sVy * 16.0f) & 0xffff);
        voodoo->params.vertexCx = (int32_t)(int16_t)((int32_t)(voodoo->verts[vc].sVx * 16.0f) & 0xffff);
        voodoo->params.vertexCy = (int32_t)(int16_t)((int32_t)(voodoo->verts[vc].sVy * 16.0f) & 0xffff);
        
        if (voodoo->params.vertexAy > voodoo->params.vertexBy || voodoo->params.vertexBy > voodoo->params.vertexCy)
                fatal("triangle_setup wrong order %d %d %d\n", voodoo->params.vertexAy, voodoo->params.vertexBy, voodoo->params.vertexCy);

        if (voodoo->sSetupMode & SETUPMODE_RGB)
        {
                voodoo->params.startR = (int32_t)(voodoo->verts[va].sRed * 4096.0f);
                voodoo->params.dRdX = (int32_t)(((voodoo->verts[va].sRed - voodoo->verts[vb].sRed) * dyBC - (voodoo->verts[vb].sRed - voodoo->verts[vc].sRed) * dyAB) * 4096.0f);
                voodoo->params.dRdY = (int32_t)(((voodoo->verts[vb].sRed - voodoo->verts[vc].sRed) * dxAB - (voodoo->verts[va].sRed - voodoo->verts[vb].sRed) * dxBC) * 4096.0f);
                voodoo->params.startG = (int32_t)(voodoo->verts[va].sGreen * 4096.0f);
                voodoo->params.dGdX = (int32_t)(((voodoo->verts[va].sGreen - voodoo->verts[vb].sGreen) * dyBC - (voodoo->verts[vb].sGreen - voodoo->verts[vc].sGreen) * dyAB) * 4096.0f);
                voodoo->params.dGdY = (int32_t)(((voodoo->verts[vb].sGreen - voodoo->verts[vc].sGreen) * dxAB - (voodoo->verts[va].sGreen - voodoo->verts[vb].sGreen) * dxBC) * 4096.0f);
                voodoo->params.startB = (int32_t)(voodoo->verts[va].sBlue * 4096.0f);
                voodoo->params.dBdX = (int32_t)(((voodoo->verts[va].sBlue - voodoo->verts[vb].sBlue) * dyBC - (voodoo->verts[vb].sBlue - voodoo->verts[vc].sBlue) * dyAB) * 4096.0f);
                voodoo->params.dBdY = (int32_t)(((voodoo->verts[vb].sBlue - voodoo->verts[vc].sBlue) * dxAB - (voodoo->verts[va].sBlue - voodoo->verts[vb].sBlue) * dxBC) * 4096.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_ALPHA)
        {
                voodoo->params.startA = (int32_t)(voodoo->verts[va].sAlpha * 4096.0f);
                voodoo->params.dAdX = (int32_t)(((voodoo->verts[va].sAlpha - voodoo->verts[vb].sAlpha) * dyBC - (voodoo->verts[vb].sAlpha - voodoo->verts[vc].sAlpha) * dyAB) * 4096.0f);
                voodoo->params.dAdY = (int32_t)(((voodoo->verts[vb].sAlpha - voodoo->verts[vc].sAlpha) * dxAB - (voodoo->verts[va].sAlpha - voodoo->verts[vb].sAlpha) * dxBC) * 4096.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_Z)
        {
                voodoo->params.startZ = (int32_t)(voodoo->verts[va].sVz * 4096.0f);
                voodoo->params.dZdX = (int32_t)(((voodoo->verts[va].sVz - voodoo->verts[vb].sVz) * dyBC - (voodoo->verts[vb].sVz - voodoo->verts[vc].sVz) * dyAB) * 4096.0f);
                voodoo->params.dZdY = (int32_t)(((voodoo->verts[vb].sVz - voodoo->verts[vc].sVz) * dxAB - (voodoo->verts[va].sVz - voodoo->verts[vb].sVz) * dxBC) * 4096.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_Wb)
        {
                voodoo->params.startW = (int64_t)(voodoo->verts[va].sWb * 4294967296.0f);
                voodoo->params.dWdX = (int64_t)(((voodoo->verts[va].sWb - voodoo->verts[vb].sWb) * dyBC - (voodoo->verts[vb].sWb - voodoo->verts[vc].sWb) * dyAB) * 4294967296.0f);
                voodoo->params.dWdY = (int64_t)(((voodoo->verts[vb].sWb - voodoo->verts[vc].sWb) * dxAB - (voodoo->verts[va].sWb - voodoo->verts[vb].sWb) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[0].startW = voodoo->params.tmu[1].startW = voodoo->params.startW;
                voodoo->params.tmu[0].dWdX = voodoo->params.tmu[1].dWdX = voodoo->params.dWdX;
                voodoo->params.tmu[0].dWdY = voodoo->params.tmu[1].dWdY = voodoo->params.dWdY;
        }
        if (voodoo->sSetupMode & SETUPMODE_W0)
        {
                voodoo->params.tmu[0].startW = (int64_t)(voodoo->verts[va].sW0 * 4294967296.0f);
                voodoo->params.tmu[0].dWdX = (int64_t)(((voodoo->verts[va].sW0 - voodoo->verts[vb].sW0) * dyBC - (voodoo->verts[vb].sW0 - voodoo->verts[vc].sW0) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[0].dWdY = (int64_t)(((voodoo->verts[vb].sW0 - voodoo->verts[vc].sW0) * dxAB - (voodoo->verts[va].sW0 - voodoo->verts[vb].sW0) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[1].startW = voodoo->params.tmu[0].startW;
                voodoo->params.tmu[1].dWdX = voodoo->params.tmu[0].dWdX;
                voodoo->params.tmu[1].dWdY = voodoo->params.tmu[0].dWdY;
        }
        if (voodoo->sSetupMode & SETUPMODE_S0_T0)
        {
                voodoo->params.tmu[0].startS = (int64_t)(voodoo->verts[va].sS0 * 4294967296.0f);
                voodoo->params.tmu[0].dSdX = (int64_t)(((voodoo->verts[va].sS0 - voodoo->verts[vb].sS0) * dyBC - (voodoo->verts[vb].sS0 - voodoo->verts[vc].sS0) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[0].dSdY = (int64_t)(((voodoo->verts[vb].sS0 - voodoo->verts[vc].sS0) * dxAB - (voodoo->verts[va].sS0 - voodoo->verts[vb].sS0) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[0].startT = (int64_t)(voodoo->verts[va].sT0 * 4294967296.0f);
                voodoo->params.tmu[0].dTdX = (int64_t)(((voodoo->verts[va].sT0 - voodoo->verts[vb].sT0) * dyBC - (voodoo->verts[vb].sT0 - voodoo->verts[vc].sT0) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[0].dTdY = (int64_t)(((voodoo->verts[vb].sT0 - voodoo->verts[vc].sT0) * dxAB - (voodoo->verts[va].sT0 - voodoo->verts[vb].sT0) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[1].startS = voodoo->params.tmu[0].startS;
                voodoo->params.tmu[1].dSdX = voodoo->params.tmu[0].dSdX;
                voodoo->params.tmu[1].dSdY = voodoo->params.tmu[0].dSdY;
                voodoo->params.tmu[1].startT = voodoo->params.tmu[0].startT;
                voodoo->params.tmu[1].dTdX = voodoo->params.tmu[0].dTdX;
                voodoo->params.tmu[1].dTdY = voodoo->params.tmu[0].dTdY;
        }
        if (voodoo->sSetupMode & SETUPMODE_W1)
        {
                voodoo->params.tmu[1].startW = (int64_t)(voodoo->verts[va].sW1 * 4294967296.0f);
                voodoo->params.tmu[1].dWdX = (int64_t)(((voodoo->verts[va].sW1 - voodoo->verts[vb].sW1) * dyBC - (voodoo->verts[vb].sW1 - voodoo->verts[vc].sW1) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[1].dWdY = (int64_t)(((voodoo->verts[vb].sW1 - voodoo->verts[vc].sW1) * dxAB - (voodoo->verts[va].sW1 - voodoo->verts[vb].sW1) * dxBC) * 4294967296.0f);
        }
        if (voodoo->sSetupMode & SETUPMODE_S1_T1)
        {
                voodoo->params.tmu[1].startS = (int64_t)(voodoo->verts[va].sS1 * 4294967296.0f);
                voodoo->params.tmu[1].dSdX = (int64_t)(((voodoo->verts[va].sS1 - voodoo->verts[vb].sS1) * dyBC - (voodoo->verts[vb].sS1 - voodoo->verts[vc].sS1) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[1].dSdY = (int64_t)(((voodoo->verts[vb].sS1 - voodoo->verts[vc].sS1) * dxAB - (voodoo->verts[va].sS1 - voodoo->verts[vb].sS1) * dxBC) * 4294967296.0f);
                voodoo->params.tmu[1].startT = (int64_t)(voodoo->verts[va].sT1 * 4294967296.0f);
                voodoo->params.tmu[1].dTdX = (int64_t)(((voodoo->verts[va].sT1 - voodoo->verts[vb].sT1) * dyBC - (voodoo->verts[vb].sT1 - voodoo->verts[vc].sT1) * dyAB) * 4294967296.0f);
                voodoo->params.tmu[1].dTdY = (int64_t)(((voodoo->verts[vb].sT1 - voodoo->verts[vc].sT1) * dxAB - (voodoo->verts[va].sT1 - voodoo->verts[vb].sT1) * dxBC) * 4294967296.0f);
        }

        voodoo->params.sign = (area < 0.0);

        if (voodoo->ncc_dirty[0])
                voodoo_update_ncc(voodoo, 0);
        if (voodoo->ncc_dirty[1])
                voodoo_update_ncc(voodoo, 1);
        voodoo->ncc_dirty[0] = voodoo->ncc_dirty[1] = 0;

        queue_triangle(voodoo, &voodoo->params);
}

enum
{
        BLIT_COMMAND_SCREEN_TO_SCREEN = 0,
        BLIT_COMMAND_CPU_TO_SCREEN = 1,
        BLIT_COMMAND_RECT_FILL = 2,
        BLIT_COMMAND_SGRAM_FILL = 3
};

enum
{
        BLIT_SRC_1BPP             = (0 << 3),
        BLIT_SRC_1BPP_BYTE_PACKED = (1 << 3),
        BLIT_SRC_16BPP            = (2 << 3),
        BLIT_SRC_24BPP            = (3 << 3),
        BLIT_SRC_24BPP_DITHER_2X2 = (4 << 3),
        BLIT_SRC_24BPP_DITHER_4X4 = (5 << 3)
};

enum
{
        BLIT_SRC_RGB_ARGB = (0 << 6),
        BLIT_SRC_RGB_ABGR = (1 << 6),
        BLIT_SRC_RGB_RGBA = (2 << 6),
        BLIT_SRC_RGB_BGRA = (3 << 6)
};

enum
{
        BLIT_COMMAND_MASK = 7,
        BLIT_SRC_FORMAT = (7 << 3),
        BLIT_SRC_RGB_FORMAT = (3 << 6),
        BLIT_SRC_CHROMA = (1 << 10),
        BLIT_DST_CHROMA = (1 << 12),
        BLIT_CLIPPING_ENABLED = (1 << 16)
};

enum
{
        BLIT_ROP_DST_PASS = (1 << 0),
        BLIT_ROP_SRC_PASS = (1 << 1)
};

#define MIX(src_dat, dst_dat, rop) \
        switch (rop)                                                    \
        {                                                               \
                case 0x0: dst_dat = 0; break;                           \
                case 0x1: dst_dat = ~(src_dat | dst_dat); break;        \
                case 0x2: dst_dat = ~src_dat & dst_dat; break;          \
                case 0x3: dst_dat = ~src_dat; break;                    \
                case 0x4: dst_dat = src_dat & ~dst_dat; break;          \
                case 0x5: dst_dat = ~dst_dat; break;                    \
                case 0x6: dst_dat = src_dat ^ dst_dat; break;           \
                case 0x7: dst_dat = ~(src_dat & dst_dat); break;        \
                case 0x8: dst_dat = src_dat & dst_dat; break;           \
                case 0x9: dst_dat = ~(src_dat ^ dst_dat); break;        \
                case 0xa: dst_dat = dst_dat; break;                     \
                case 0xb: dst_dat = ~src_dat | dst_dat; break;          \
                case 0xc: dst_dat = src_dat; break;                     \
                case 0xd: dst_dat = src_dat | ~dst_dat; break;          \
                case 0xe: dst_dat = src_dat | dst_dat; break;           \
                case 0xf: dst_dat = 0xffff; break;                      \
        }

static void blit_start(voodoo_t *voodoo)
{
        uint64_t dat64;
        int size_x = ABS(voodoo->bltSizeX), size_y = ABS(voodoo->bltSizeY);
        int x_dir = (voodoo->bltSizeX > 0) ? 1 : -1;
        int y_dir = (voodoo->bltSizeY > 0) ? 1 : -1;
        int dst_x;
        int src_y = voodoo->bltSrcY & 0x7ff, dst_y = voodoo->bltDstY & 0x7ff;
        int src_stride = (voodoo->bltCommand & BLTCMD_SRC_TILED) ? ((voodoo->bltSrcXYStride & 0x3f) * 32*2) : (voodoo->bltSrcXYStride & 0xff8);
        int dst_stride = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstXYStride & 0x3f) * 32*2) : (voodoo->bltDstXYStride & 0xff8);
        uint32_t src_base_addr = (voodoo->bltCommand & BLTCMD_SRC_TILED) ? ((voodoo->bltSrcBaseAddr & 0x3ff) << 12) : (voodoo->bltSrcBaseAddr & 0x3ffff8);
        uint32_t dst_base_addr = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstBaseAddr & 0x3ff) << 12) : (voodoo->bltDstBaseAddr & 0x3ffff8);
        int x, y;
        
/*        voodoo_log("blit_start: command=%08x srcX=%i srcY=%i dstX=%i dstY=%i sizeX=%i sizeY=%i color=%04x,%04x\n",
                voodoo->bltCommand, voodoo->bltSrcX, voodoo->bltSrcY, voodoo->bltDstX, voodoo->bltDstY, voodoo->bltSizeX, voodoo->bltSizeY, voodoo->bltColorFg, voodoo->bltColorBg);*/

        wait_for_render_thread_idle(voodoo);

        switch (voodoo->bltCommand & BLIT_COMMAND_MASK)
        {
                case BLIT_COMMAND_SCREEN_TO_SCREEN:
                for (y = 0; y <= size_y; y++)
                {
                        uint16_t *src = (uint16_t *)&voodoo->fb_mem[src_base_addr + src_y*src_stride];
                        uint16_t *dst = (uint16_t *)&voodoo->fb_mem[dst_base_addr + dst_y*dst_stride];
                        int src_x = voodoo->bltSrcX, dst_x = voodoo->bltDstX;
                        
                        for (x = 0; x <= size_x; x++)
                        {
                                uint16_t src_dat = src[src_x];
                                uint16_t dst_dat = dst[dst_x];
                                int rop = 0;

                                if (voodoo->bltCommand & BLIT_CLIPPING_ENABLED)
                                {
                                        if (dst_x < voodoo->bltClipLeft || dst_x >= voodoo->bltClipRight ||
                                            dst_y < voodoo->bltClipLowY || dst_y >= voodoo->bltClipHighY)
                                                goto skip_pixel_blit;
                                }

                                if (voodoo->bltCommand & BLIT_SRC_CHROMA)
                                {
                                        int r = (src_dat >> 11);
                                        int g = (src_dat >> 5) & 0x3f;
                                        int b = src_dat & 0x1f;
                        
                                        if (r >= voodoo->bltSrcChromaMinR && r <= voodoo->bltSrcChromaMaxR &&
                                            g >= voodoo->bltSrcChromaMinG && g <= voodoo->bltSrcChromaMaxG &&
                                            b >= voodoo->bltSrcChromaMinB && b <= voodoo->bltSrcChromaMaxB)
                                                rop |= BLIT_ROP_SRC_PASS;
                                }
                                if (voodoo->bltCommand & BLIT_DST_CHROMA)
                                {
                                        int r = (dst_dat >> 11);
                                        int g = (dst_dat >> 5) & 0x3f;
                                        int b = dst_dat & 0x1f;
                        
                                        if (r >= voodoo->bltDstChromaMinR && r <= voodoo->bltDstChromaMaxR &&
                                            g >= voodoo->bltDstChromaMinG && g <= voodoo->bltDstChromaMaxG &&
                                            b >= voodoo->bltDstChromaMinB && b <= voodoo->bltDstChromaMaxB)
                                                rop |= BLIT_ROP_DST_PASS;
                                }

                                MIX(src_dat, dst_dat, voodoo->bltRop[rop]);
                                
                                dst[dst_x] = dst_dat;
skip_pixel_blit:                                
                                src_x += x_dir;
                                dst_x += x_dir;                                
                        }
                        
                        src_y += y_dir;
                        dst_y += y_dir;
                }
                break;
                
                case BLIT_COMMAND_CPU_TO_SCREEN:
                voodoo->blt.dst_x = voodoo->bltDstX;
                voodoo->blt.dst_y = voodoo->bltDstY;
                voodoo->blt.cur_x = 0;
                voodoo->blt.size_x = size_x;
                voodoo->blt.size_y = size_y;
                voodoo->blt.x_dir = x_dir;
                voodoo->blt.y_dir = y_dir;
                voodoo->blt.dst_stride = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstXYStride & 0x3f) * 32*2) : (voodoo->bltDstXYStride & 0xff8);
                break;
                
                case BLIT_COMMAND_RECT_FILL:
                for (y = 0; y <= size_y; y++)
                {
                        uint16_t *dst;
                        int dst_x = voodoo->bltDstX;
                        
                        if (SLI_ENABLED)
                        {
                                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (voodoo->blt.dst_y & 1)) ||
                                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(voodoo->blt.dst_y & 1)))
                                        goto skip_line_fill;
                                dst = (uint16_t *)&voodoo->fb_mem[dst_base_addr + (dst_y >> 1) * dst_stride];
                        }
                        else                        
                                dst = (uint16_t *)&voodoo->fb_mem[dst_base_addr + dst_y*dst_stride];

                        for (x = 0; x <= size_x; x++)
                        {
                                if (voodoo->bltCommand & BLIT_CLIPPING_ENABLED)
                                {
                                        if (dst_x < voodoo->bltClipLeft || dst_x >= voodoo->bltClipRight ||
                                            dst_y < voodoo->bltClipLowY || dst_y >= voodoo->bltClipHighY)
                                                goto skip_pixel_fill;
                                }

                                dst[dst_x] = voodoo->bltColorFg;
skip_pixel_fill:
                                dst_x += x_dir;                                
                        }
skip_line_fill:                        
                        dst_y += y_dir;
                }
                break;

                case BLIT_COMMAND_SGRAM_FILL:
                /*32x32 tiles - 2kb*/
                dst_y = voodoo->bltDstY & 0x3ff;
                size_x = voodoo->bltSizeX & 0x1ff; //512*8 = 4kb
                size_y = voodoo->bltSizeY & 0x3ff;
                
                dat64 = voodoo->bltColorFg | ((uint64_t)voodoo->bltColorFg << 16) |
                        ((uint64_t)voodoo->bltColorFg << 32) | ((uint64_t)voodoo->bltColorFg << 48);
                
                for (y = 0; y <= size_y; y++)
                {
                        uint64_t *dst;

                        /*This may be wrong*/
                        if (!y)
                        {
                                dst_x = voodoo->bltDstX & 0x1ff;
                                size_x = 511 - dst_x;
                        }
                        else if (y < size_y)
                        {
                                dst_x = 0;
                                size_x = 511;
                        }
                        else
                        {
                                dst_x = 0;
                                size_x = voodoo->bltSizeX & 0x1ff;
                        }
                        
                        dst = (uint64_t *)&voodoo->fb_mem[(dst_y*512*8 + dst_x*8) & voodoo->fb_mask];
                        
                        for (x = 0; x <= size_x; x++)
                                dst[x] = dat64;

                        dst_y++;
                }
                break;
                
                default:
                fatal("bad blit command %08x\n", voodoo->bltCommand);
        }
}

static void blit_data(voodoo_t *voodoo, uint32_t data)
{
        int src_bits = 32;
        uint32_t base_addr = (voodoo->bltCommand & BLTCMD_DST_TILED) ? ((voodoo->bltDstBaseAddr & 0x3ff) << 12) : (voodoo->bltDstBaseAddr & 0x3ffff8);
        uint32_t addr;
        uint16_t *dst;

        if ((voodoo->bltCommand & BLIT_COMMAND_MASK) != BLIT_COMMAND_CPU_TO_SCREEN)
                return;

        if (SLI_ENABLED)
        {
                addr = base_addr + (voodoo->blt.dst_y >> 1) * voodoo->blt.dst_stride;
                dst = (uint16_t *)&voodoo->fb_mem[addr];
        }
        else
        {
                addr = base_addr + voodoo->blt.dst_y*voodoo->blt.dst_stride;
                dst = (uint16_t *)&voodoo->fb_mem[addr];
        }

        if (addr >= voodoo->front_offset && voodoo->row_width)
        {
                int y = (addr - voodoo->front_offset) / voodoo->row_width;
                if (y < voodoo->v_disp)
                        voodoo->dirty_line[y] = 2;
        }

        while (src_bits && voodoo->blt.cur_x <= voodoo->blt.size_x)
        {
                int r = 0, g = 0, b = 0;
                uint16_t src_dat = 0, dst_dat;
                int x = (voodoo->blt.x_dir > 0) ? (voodoo->blt.dst_x + voodoo->blt.cur_x) : (voodoo->blt.dst_x - voodoo->blt.cur_x);
                int rop = 0;
                
                switch (voodoo->bltCommand & BLIT_SRC_FORMAT)
                {
                        case BLIT_SRC_1BPP: case BLIT_SRC_1BPP_BYTE_PACKED:
                        src_dat = (data & 1) ? voodoo->bltColorFg : voodoo->bltColorBg;
                        data >>= 1;
                        src_bits--;
                        break;
                        case BLIT_SRC_16BPP:
                        switch (voodoo->bltCommand & BLIT_SRC_RGB_FORMAT)
                        {
                                case BLIT_SRC_RGB_ARGB: case BLIT_SRC_RGB_RGBA:
                                src_dat = data & 0xffff;
                                break;
                                case BLIT_SRC_RGB_ABGR: case BLIT_SRC_RGB_BGRA:
                                src_dat = ((data & 0xf800) >> 11) | (data & 0x07c0) | ((data & 0x0038) << 11);
                                break;
                        }
                        data >>= 16;
                        src_bits -= 16;
                        break;
                        case BLIT_SRC_24BPP: case BLIT_SRC_24BPP_DITHER_2X2: case BLIT_SRC_24BPP_DITHER_4X4:
                        switch (voodoo->bltCommand & BLIT_SRC_RGB_FORMAT)
                        {
                                case BLIT_SRC_RGB_ARGB:
                                r = (data >> 16) & 0xff;
                                g = (data >> 8) & 0xff;
                                b = data & 0xff;
                                break;
                                case BLIT_SRC_RGB_ABGR:
                                r = data & 0xff;
                                g = (data >> 8) & 0xff;
                                b = (data >> 16) & 0xff;
                                break;
                                case BLIT_SRC_RGB_RGBA:
                                r = (data >> 24) & 0xff;
                                g = (data >> 16) & 0xff;
                                b = (data >> 8) & 0xff;
                                break;
                                case BLIT_SRC_RGB_BGRA:
                                r = (data >> 8) & 0xff;
                                g = (data >> 16) & 0xff;
                                b = (data >> 24) & 0xff;
                                break;
                        }
                        switch (voodoo->bltCommand & BLIT_SRC_FORMAT)
                        {
                                case BLIT_SRC_24BPP:
                                src_dat = (b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8);
                                break;
                                case BLIT_SRC_24BPP_DITHER_2X2:
                                r = dither_rb2x2[r][voodoo->blt.dst_y & 1][x & 1];
                                g =  dither_g2x2[g][voodoo->blt.dst_y & 1][x & 1];
                                b = dither_rb2x2[b][voodoo->blt.dst_y & 1][x & 1];
                                src_dat = (b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8);
                                break;
                                case BLIT_SRC_24BPP_DITHER_4X4:
                                r = dither_rb[r][voodoo->blt.dst_y & 3][x & 3];
                                g =  dither_g[g][voodoo->blt.dst_y & 3][x & 3];
                                b = dither_rb[b][voodoo->blt.dst_y & 3][x & 3];
                                src_dat = (b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8);
                                break;
                        }
                        src_bits = 0;
                        break;
                }

                if (SLI_ENABLED)
                {
                        if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (voodoo->blt.dst_y & 1)) ||
                            ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(voodoo->blt.dst_y & 1)))
                                goto skip_pixel;
                }
                
                if (voodoo->bltCommand & BLIT_CLIPPING_ENABLED)
                {
                        if (x < voodoo->bltClipLeft || x >= voodoo->bltClipRight ||
                            voodoo->blt.dst_y < voodoo->bltClipLowY || voodoo->blt.dst_y >= voodoo->bltClipHighY)
                                goto skip_pixel;
                }

                dst_dat = dst[x];

                if (voodoo->bltCommand & BLIT_SRC_CHROMA)
                {
                        r = (src_dat >> 11);
                        g = (src_dat >> 5) & 0x3f;
                        b = src_dat & 0x1f;
                        
                        if (r >= voodoo->bltSrcChromaMinR && r <= voodoo->bltSrcChromaMaxR &&
                            g >= voodoo->bltSrcChromaMinG && g <= voodoo->bltSrcChromaMaxG &&
                            b >= voodoo->bltSrcChromaMinB && b <= voodoo->bltSrcChromaMaxB)
                                rop |= BLIT_ROP_SRC_PASS;
                }
                if (voodoo->bltCommand & BLIT_DST_CHROMA)
                {
                        r = (dst_dat >> 11);
                        g = (dst_dat >> 5) & 0x3f;
                        b = dst_dat & 0x1f;
                        
                        if (r >= voodoo->bltDstChromaMinR && r <= voodoo->bltDstChromaMaxR &&
                            g >= voodoo->bltDstChromaMinG && g <= voodoo->bltDstChromaMaxG &&
                            b >= voodoo->bltDstChromaMinB && b <= voodoo->bltDstChromaMaxB)
                                rop |= BLIT_ROP_DST_PASS;
                }

                MIX(src_dat, dst_dat, voodoo->bltRop[rop]);

                dst[x] = dst_dat;

skip_pixel:
                voodoo->blt.cur_x++;
        }
        
        if (voodoo->blt.cur_x > voodoo->blt.size_x)
        {
                voodoo->blt.size_y--;
                if (voodoo->blt.size_y >= 0)
                {
                        voodoo->blt.cur_x = 0;
                        voodoo->blt.dst_y += voodoo->blt.y_dir;
                }
        }
}

enum
{
        CHIP_FBI = 0x1,
        CHIP_TREX0 = 0x2,
        CHIP_TREX1 = 0x4,
        CHIP_TREX2 = 0x8
};

static void wait_for_swap_complete(voodoo_t *voodoo)
{
        while (voodoo->swap_pending)
        {
                thread_wait_event(voodoo->wake_fifo_thread, -1);
                thread_reset_event(voodoo->wake_fifo_thread);
                if ((voodoo->swap_pending && voodoo->flush) || FIFO_ENTRIES >= 65536)
                {
                        /*Main thread is waiting for FIFO to empty, so skip vsync wait and just swap*/
                        memset(voodoo->dirty_line, 1, 1024);
                        voodoo->front_offset = voodoo->params.front_offset;
                        if (voodoo->swap_count > 0)
                                voodoo->swap_count--;
                        voodoo->swap_pending = 0;
                        break;
                }
        }
}

static void voodoo_reg_writel(uint32_t addr, uint32_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        union
        {
                uint32_t i;
                float f;
        } tempif;
        int ad21 = addr & (1 << 21);
        int chip = (addr >> 10) & 0xf;
        if (!chip)
                chip = 0xf;
        
        tempif.i = val;
//voodoo_log("voodoo_reg_write_l: addr=%08x val=%08x(%f) chip=%x\n", addr, val, tempif.f, chip);
        addr &= 0x3fc;

        if ((voodoo->fbiInit3 & FBIINIT3_REMAP) && addr < 0x100 && ad21)
                addr |= 0x400;
        switch (addr)
        {
                case SST_swapbufferCMD:
//                voodoo_log("  start swap buffer command\n");

                if (TRIPLE_BUFFER)
                {
                        voodoo->disp_buffer = (voodoo->disp_buffer + 1) % 3;
                        voodoo->draw_buffer = (voodoo->draw_buffer + 1) % 3;
                }
                else
                {
                        voodoo->disp_buffer = !voodoo->disp_buffer;
                        voodoo->draw_buffer = !voodoo->draw_buffer;
                }
                voodoo_recalc(voodoo);

                voodoo->params.swapbufferCMD = val;

                voodoo_log("Swap buffer %08x %d %p %i\n", val, voodoo->swap_count, &voodoo->swap_count, (voodoo == voodoo->set->voodoos[1]) ? 1 : 0);
//                voodoo->front_offset = params->front_offset;
                wait_for_render_thread_idle(voodoo);
                if (!(val & 1))
                {
                        memset(voodoo->dirty_line, 1, 1024);
                        voodoo->front_offset = voodoo->params.front_offset;
                        if (voodoo->swap_count > 0)
                                voodoo->swap_count--;
                }
                else if (TRIPLE_BUFFER)
                {
                        if (voodoo->swap_pending)
                                wait_for_swap_complete(voodoo);
                                
                        voodoo->swap_interval = (val >> 1) & 0xff;
                        voodoo->swap_offset = voodoo->params.front_offset;
                        voodoo->swap_pending = 1;                        
                }
                else
                {
                        voodoo->swap_interval = (val >> 1) & 0xff;
                        voodoo->swap_offset = voodoo->params.front_offset;
                        voodoo->swap_pending = 1;

                        wait_for_swap_complete(voodoo);
                }
                voodoo->cmd_read++;
                break;
                        
                case SST_vertexAx: case SST_remap_vertexAx:
                voodoo->params.vertexAx = val & 0xffff;
                break;
                case SST_vertexAy: case SST_remap_vertexAy:
                voodoo->params.vertexAy = val & 0xffff;
                break;
                case SST_vertexBx: case SST_remap_vertexBx:
                voodoo->params.vertexBx = val & 0xffff;
                break;
                case SST_vertexBy: case SST_remap_vertexBy:
                voodoo->params.vertexBy = val & 0xffff;
                break;
                case SST_vertexCx: case SST_remap_vertexCx:
                voodoo->params.vertexCx = val & 0xffff;
                break;
                case SST_vertexCy: case SST_remap_vertexCy:
                voodoo->params.vertexCy = val & 0xffff;
                break;
                
                case SST_startR: case SST_remap_startR:
                voodoo->params.startR = val & 0xffffff;
                break;
                case SST_startG: case SST_remap_startG:
                voodoo->params.startG = val & 0xffffff;
                break;
                case SST_startB: case SST_remap_startB:
                voodoo->params.startB = val & 0xffffff;
                break;
                case SST_startZ: case SST_remap_startZ:
                voodoo->params.startZ = val;
                break;
                case SST_startA: case SST_remap_startA:
                voodoo->params.startA = val & 0xffffff;
                break;
                case SST_startS: case SST_remap_startS:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startS = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startS = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_startT: case SST_remap_startT:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startT = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startT = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_startW: case SST_remap_startW:
                if (chip & CHIP_FBI)
                        voodoo->params.startW = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startW = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startW = (int64_t)(int32_t)val << 2;
                break;

                case SST_dRdX: case SST_remap_dRdX:
                voodoo->params.dRdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dGdX: case SST_remap_dGdX:
                voodoo->params.dGdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dBdX: case SST_remap_dBdX:
                voodoo->params.dBdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dZdX: case SST_remap_dZdX:
                voodoo->params.dZdX = val;
                break;
                case SST_dAdX: case SST_remap_dAdX:
                voodoo->params.dAdX = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dSdX: case SST_remap_dSdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdX = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdX = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dTdX: case SST_remap_dTdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdX = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdX = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dWdX: case SST_remap_dWdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdX = (int64_t)(int32_t)val << 2;                
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdX = (int64_t)(int32_t)val << 2;                
                if (chip & CHIP_FBI)
                        voodoo->params.dWdX = (int64_t)(int32_t)val << 2;
                break;

                case SST_dRdY: case SST_remap_dRdY:
                voodoo->params.dRdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dGdY: case SST_remap_dGdY:
                voodoo->params.dGdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dBdY: case SST_remap_dBdY:
                voodoo->params.dBdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dZdY: case SST_remap_dZdY:
                voodoo->params.dZdY = val;
                break;
                case SST_dAdY: case SST_remap_dAdY:
                voodoo->params.dAdY = (val & 0xffffff) | ((val & 0x800000) ? 0xff000000 : 0);
                break;
                case SST_dSdY: case SST_remap_dSdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdY = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdY = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dTdY: case SST_remap_dTdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdY = ((int64_t)(int32_t)val) << 14;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdY = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dWdY: case SST_remap_dWdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdY = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdY = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_FBI)
                        voodoo->params.dWdY = (int64_t)(int32_t)val << 2;
                break;

                case SST_triangleCMD: case SST_remap_triangleCMD:
                voodoo->params.sign = val & (1 << 31);

                if (voodoo->ncc_dirty[0])
                        voodoo_update_ncc(voodoo, 0);
                if (voodoo->ncc_dirty[1])
                        voodoo_update_ncc(voodoo, 1);
                voodoo->ncc_dirty[0] = voodoo->ncc_dirty[1] = 0;

                queue_triangle(voodoo, &voodoo->params);

                voodoo->cmd_read++;
                break;

                case SST_fvertexAx: case SST_remap_fvertexAx:
                voodoo->fvertexAx.i = val;
                voodoo->params.vertexAx = (int32_t)(int16_t)(int32_t)(voodoo->fvertexAx.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexAy: case SST_remap_fvertexAy:
                voodoo->fvertexAy.i = val;
                voodoo->params.vertexAy = (int32_t)(int16_t)(int32_t)(voodoo->fvertexAy.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexBx: case SST_remap_fvertexBx:
                voodoo->fvertexBx.i = val;
                voodoo->params.vertexBx = (int32_t)(int16_t)(int32_t)(voodoo->fvertexBx.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexBy: case SST_remap_fvertexBy:
                voodoo->fvertexBy.i = val;
                voodoo->params.vertexBy = (int32_t)(int16_t)(int32_t)(voodoo->fvertexBy.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexCx: case SST_remap_fvertexCx:
                voodoo->fvertexCx.i = val;
                voodoo->params.vertexCx = (int32_t)(int16_t)(int32_t)(voodoo->fvertexCx.f * 16.0f) & 0xffff;
                break;
                case SST_fvertexCy: case SST_remap_fvertexCy:
                voodoo->fvertexCy.i = val;
                voodoo->params.vertexCy = (int32_t)(int16_t)(int32_t)(voodoo->fvertexCy.f * 16.0f) & 0xffff;
                break;

                case SST_fstartR: case SST_remap_fstartR:
                tempif.i = val;
                voodoo->params.startR = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartG: case SST_remap_fstartG:
                tempif.i = val;
                voodoo->params.startG = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartB: case SST_remap_fstartB:
                tempif.i = val;
                voodoo->params.startB = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartZ: case SST_remap_fstartZ:
                tempif.i = val;
                voodoo->params.startZ = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartA: case SST_remap_fstartA:
                tempif.i = val;
                voodoo->params.startA = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fstartS: case SST_remap_fstartS:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startS = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startS = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fstartT: case SST_remap_fstartT:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startT = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startT = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fstartW: case SST_remap_fstartW:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startW = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].startW = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_FBI)
                        voodoo->params.startW = (int64_t)(tempif.f * 4294967296.0f);
                break;

                case SST_fdRdX: case SST_remap_fdRdX:
                tempif.i = val;
                voodoo->params.dRdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdGdX: case SST_remap_fdGdX:
                tempif.i = val;
                voodoo->params.dGdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdBdX: case SST_remap_fdBdX:
                tempif.i = val;
                voodoo->params.dBdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdZdX: case SST_remap_fdZdX:
                tempif.i = val;
                voodoo->params.dZdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdAdX: case SST_remap_fdAdX:
                tempif.i = val;
                voodoo->params.dAdX = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdSdX: case SST_remap_fdSdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdX = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdTdX: case SST_remap_fdTdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdX = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdWdX: case SST_remap_fdWdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdX = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_FBI)
                        voodoo->params.dWdX = (int64_t)(tempif.f * 4294967296.0f);
                break;

                case SST_fdRdY: case SST_remap_fdRdY:
                tempif.i = val;
                voodoo->params.dRdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdGdY: case SST_remap_fdGdY:
                tempif.i = val;
                voodoo->params.dGdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdBdY: case SST_remap_fdBdY:
                tempif.i = val;
                voodoo->params.dBdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdZdY: case SST_remap_fdZdY:
                tempif.i = val;
                voodoo->params.dZdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdAdY: case SST_remap_fdAdY:
                tempif.i = val;
                voodoo->params.dAdY = (int32_t)(tempif.f * 4096.0f);
                break;
                case SST_fdSdY: case SST_remap_fdSdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dSdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dSdY = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdTdY: case SST_remap_fdTdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dTdY = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdWdY: case SST_remap_fdWdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_TREX1)
                        voodoo->params.tmu[1].dWdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_FBI)
                        voodoo->params.dWdY = (int64_t)(tempif.f * 4294967296.0f);
                break;

                case SST_ftriangleCMD:
                voodoo->params.sign = val & (1 << 31);

                if (voodoo->ncc_dirty[0])
                        voodoo_update_ncc(voodoo, 0);
                if (voodoo->ncc_dirty[1])
                        voodoo_update_ncc(voodoo, 1);
                voodoo->ncc_dirty[0] = voodoo->ncc_dirty[1] = 0;

                queue_triangle(voodoo, &voodoo->params);

                voodoo->cmd_read++;
                break;
                
                case SST_fbzColorPath:
                voodoo->params.fbzColorPath = val;
                voodoo->rgb_sel = val & 3;
                break;

                case SST_fogMode:
                voodoo->params.fogMode = val;
                break;
                case SST_alphaMode:
                voodoo->params.alphaMode = val;
                break;
                case SST_fbzMode:
                voodoo->params.fbzMode = val;
                voodoo_recalc(voodoo);
                break;
                case SST_lfbMode:
                voodoo->lfbMode = val;
                voodoo_recalc(voodoo);
                break;

                case SST_clipLeftRight:
                if (voodoo->type >= VOODOO_2)
                {
                        voodoo->params.clipRight = val & 0xfff;
                        voodoo->params.clipLeft = (val >> 16) & 0xfff;
                }
                else
                {
                        voodoo->params.clipRight = val & 0x3ff;
                        voodoo->params.clipLeft = (val >> 16) & 0x3ff;
                }
                break;
                case SST_clipLowYHighY:
                if (voodoo->type >= VOODOO_2)
                {
                        voodoo->params.clipHighY = val & 0xfff;
                        voodoo->params.clipLowY = (val >> 16) & 0xfff;
                }
                else
                {
                        voodoo->params.clipHighY = val & 0x3ff;
                        voodoo->params.clipLowY = (val >> 16) & 0x3ff;
                }
                break;

                case SST_nopCMD:
                voodoo->cmd_read++;
                voodoo->fbiPixelsIn = 0;
                voodoo->fbiChromaFail = 0;
                voodoo->fbiZFuncFail = 0;
                voodoo->fbiAFuncFail = 0;
                voodoo->fbiPixelsOut = 0;
                break;
                case SST_fastfillCMD:
                wait_for_render_thread_idle(voodoo);
                voodoo_fastfill(voodoo, &voodoo->params);
                voodoo->cmd_read++;
                break;

                case SST_fogColor:
                voodoo->params.fogColor.r = (val >> 16) & 0xff;
                voodoo->params.fogColor.g = (val >> 8) & 0xff;
                voodoo->params.fogColor.b = val & 0xff;
                break;
                
                case SST_zaColor:
                voodoo->params.zaColor = val;
                break;
                case SST_chromaKey:
                voodoo->params.chromaKey_r = (val >> 16) & 0xff;
                voodoo->params.chromaKey_g = (val >> 8) & 0xff;
                voodoo->params.chromaKey_b = val & 0xff;
                voodoo->params.chromaKey = val & 0xffffff;
                break;
                case SST_stipple:
                voodoo->params.stipple = val;
                break;
                case SST_color0:
                voodoo->params.color0 = val;
                break;
                case SST_color1:
                voodoo->params.color1 = val;
                break;

                case SST_fogTable00: case SST_fogTable01: case SST_fogTable02: case SST_fogTable03:
                case SST_fogTable04: case SST_fogTable05: case SST_fogTable06: case SST_fogTable07:
                case SST_fogTable08: case SST_fogTable09: case SST_fogTable0a: case SST_fogTable0b:
                case SST_fogTable0c: case SST_fogTable0d: case SST_fogTable0e: case SST_fogTable0f:
                case SST_fogTable10: case SST_fogTable11: case SST_fogTable12: case SST_fogTable13:
                case SST_fogTable14: case SST_fogTable15: case SST_fogTable16: case SST_fogTable17:
                case SST_fogTable18: case SST_fogTable19: case SST_fogTable1a: case SST_fogTable1b:
                case SST_fogTable1c: case SST_fogTable1d: case SST_fogTable1e: case SST_fogTable1f:
                addr = (addr - SST_fogTable00) >> 1;
                voodoo->params.fogTable[addr].dfog   = val & 0xff;
                voodoo->params.fogTable[addr].fog    = (val >> 8) & 0xff;
                voodoo->params.fogTable[addr+1].dfog = (val >> 16) & 0xff;
                voodoo->params.fogTable[addr+1].fog  = (val >> 24) & 0xff;
                break;

                case SST_clutData:
                voodoo->clutData[(val >> 24) & 0x3f].b = val & 0xff;
                voodoo->clutData[(val >> 24) & 0x3f].g = (val >> 8) & 0xff;
                voodoo->clutData[(val >> 24) & 0x3f].r = (val >> 16) & 0xff;
                if (val & 0x20000000)
                {
                        voodoo->clutData[(val >> 24) & 0x3f].b = 255;
                        voodoo->clutData[(val >> 24) & 0x3f].g = 255;
                        voodoo->clutData[(val >> 24) & 0x3f].r = 255;
                }
                voodoo->clutData_dirty = 1;
                break;

                case SST_sSetupMode:
                voodoo->sSetupMode = val;
                break;
                case SST_sVx:
                tempif.i = val;
                voodoo->verts[3].sVx = tempif.f;
//                voodoo_log("sVx[%i]=%f\n", voodoo->vertex_num, tempif.f);
                break;
                case SST_sVy:
                tempif.i = val;
                voodoo->verts[3].sVy = tempif.f;
//                voodoo_log("sVy[%i]=%f\n", voodoo->vertex_num, tempif.f);
                break;
                case SST_sARGB:
                voodoo->verts[3].sBlue  = (float)(val & 0xff);
                voodoo->verts[3].sGreen = (float)((val >> 8) & 0xff);
                voodoo->verts[3].sRed   = (float)((val >> 16) & 0xff);
                voodoo->verts[3].sAlpha = (float)((val >> 24) & 0xff);
                break;                
                case SST_sRed:
                tempif.i = val;
                voodoo->verts[3].sRed = tempif.f;
                break;
                case SST_sGreen:
                tempif.i = val;
                voodoo->verts[3].sGreen = tempif.f;
                break;
                case SST_sBlue:
                tempif.i = val;
                voodoo->verts[3].sBlue = tempif.f;
                break;
                case SST_sAlpha:
                tempif.i = val;
                voodoo->verts[3].sAlpha = tempif.f;
                break;
                case SST_sVz:
                tempif.i = val;
                voodoo->verts[3].sVz = tempif.f;
                break;
                case SST_sWb:
                tempif.i = val;
                voodoo->verts[3].sWb = tempif.f;
                break;
                case SST_sW0:
                tempif.i = val;
                voodoo->verts[3].sW0 = tempif.f;
                break;
                case SST_sS0:
                tempif.i = val;
                voodoo->verts[3].sS0 = tempif.f;
                break;
                case SST_sT0:
                tempif.i = val;
                voodoo->verts[3].sT0 = tempif.f;
                break;
                case SST_sW1:
                tempif.i = val;
                voodoo->verts[3].sW1 = tempif.f;
                break;
                case SST_sS1:
                tempif.i = val;
                voodoo->verts[3].sS1 = tempif.f;
                break;
                case SST_sT1:
                tempif.i = val;
                voodoo->verts[3].sT1 = tempif.f;
                break;

                case SST_sBeginTriCMD:
//                voodoo_log("sBeginTriCMD %i %f\n", voodoo->vertex_num, voodoo->verts[4].sVx);
                voodoo->verts[0] = voodoo->verts[3];
                voodoo->vertex_num = 1;
                voodoo->num_verticies = 1;
                break;
                case SST_sDrawTriCMD:
//                voodoo_log("sDrawTriCMD %i %i %i\n", voodoo->num_verticies, voodoo->vertex_num, voodoo->sSetupMode & SETUPMODE_STRIP_MODE);
                if (voodoo->vertex_num == 3)
                        voodoo->vertex_num = (voodoo->sSetupMode & SETUPMODE_STRIP_MODE) ? 1 : 0;
                voodoo->verts[voodoo->vertex_num] = voodoo->verts[3];

                voodoo->num_verticies++;
                voodoo->vertex_num++;
                if (voodoo->num_verticies == 3)
                {
//                        voodoo_log("triangle_setup\n");
                        triangle_setup(voodoo);
                        
                        voodoo->num_verticies = 2;
                }
                if (voodoo->vertex_num == 4)
                        fatal("sDrawTriCMD overflow\n");
                break;
                
                case SST_bltSrcBaseAddr:
                voodoo->bltSrcBaseAddr = val & 0x3fffff;
                break;
                case SST_bltDstBaseAddr:
//                voodoo_log("Write bltDstBaseAddr %08x\n", val);
                voodoo->bltDstBaseAddr = val & 0x3fffff;
                break;
                case SST_bltXYStrides:
                voodoo->bltSrcXYStride = val & 0xfff;
                voodoo->bltDstXYStride = (val >> 16) & 0xfff;
//                voodoo_log("Write bltXYStrides %08x\n", val);
                break;
                case SST_bltSrcChromaRange:
                voodoo->bltSrcChromaRange = val;
                voodoo->bltSrcChromaMinB = val & 0x1f;
                voodoo->bltSrcChromaMinG = (val >> 5) & 0x3f;
                voodoo->bltSrcChromaMinR = (val >> 11) & 0x1f;
                voodoo->bltSrcChromaMaxB = (val >> 16) & 0x1f;
                voodoo->bltSrcChromaMaxG = (val >> 21) & 0x3f;
                voodoo->bltSrcChromaMaxR = (val >> 27) & 0x1f;
                break;
                case SST_bltDstChromaRange:
                voodoo->bltDstChromaRange = val;
                voodoo->bltDstChromaMinB = val & 0x1f;
                voodoo->bltDstChromaMinG = (val >> 5) & 0x3f;
                voodoo->bltDstChromaMinR = (val >> 11) & 0x1f;
                voodoo->bltDstChromaMaxB = (val >> 16) & 0x1f;
                voodoo->bltDstChromaMaxG = (val >> 21) & 0x3f;
                voodoo->bltDstChromaMaxR = (val >> 27) & 0x1f;
                break;
                case SST_bltClipX:
                voodoo->bltClipRight = val & 0xfff;
                voodoo->bltClipLeft = (val >> 16) & 0xfff;
                break;
                case SST_bltClipY:
                voodoo->bltClipHighY = val & 0xfff;
                voodoo->bltClipLowY = (val >> 16) & 0xfff;
                break;

                case SST_bltSrcXY:
                voodoo->bltSrcX = val & 0x7ff;
                voodoo->bltSrcY = (val >> 16) & 0x7ff;
                break;
                case SST_bltDstXY:
//                voodoo_log("Write bltDstXY %08x\n", val);
                voodoo->bltDstX = val & 0x7ff;
                voodoo->bltDstY = (val >> 16) & 0x7ff;
                if (val & (1 << 31))
                        blit_start(voodoo);
                break;
                case SST_bltSize:
//                voodoo_log("Write bltSize %08x\n", val);
                voodoo->bltSizeX = val & 0xfff;
                if (voodoo->bltSizeX & 0x800)
                        voodoo->bltSizeX |= 0xfffff000;
                voodoo->bltSizeY = (val >> 16) & 0xfff;
                if (voodoo->bltSizeY & 0x800)
                        voodoo->bltSizeY |= 0xfffff000;
                if (val & (1 << 31))
                        blit_start(voodoo);
                break;
                case SST_bltRop:
                voodoo->bltRop[0] = val & 0xf;
                voodoo->bltRop[1] = (val >> 4) & 0xf;
                voodoo->bltRop[2] = (val >> 8) & 0xf;
                voodoo->bltRop[3] = (val >> 12) & 0xf;
                break;
                case SST_bltColor:
//                voodoo_log("Write bltColor %08x\n", val);
                voodoo->bltColorFg = val & 0xffff;
                voodoo->bltColorBg = (val >> 16) & 0xffff;
                break;

                case SST_bltCommand:
                voodoo->bltCommand = val;
//                voodoo_log("Write bltCommand %08x\n", val);
                if (val & (1 << 31))
                        blit_start(voodoo);
                break;
                case SST_bltData:
                blit_data(voodoo, val);
                break;

                case SST_textureMode:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.textureMode[0] = val;
                        voodoo->params.tformat[0] = (val >> 8) & 0xf;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.textureMode[1] = val;
                        voodoo->params.tformat[1] = (val >> 8) & 0xf;
                }
                break;
                case SST_tLOD:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.tLOD[0] = val;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.tLOD[1] = val;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_tDetail:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.detail_max[0] = val & 0xff;
                        voodoo->params.detail_bias[0] = (val >> 8) & 0x3f;
                        voodoo->params.detail_scale[0] = (val >> 14) & 7;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.detail_max[1] = val & 0xff;
                        voodoo->params.detail_bias[1] = (val >> 8) & 0x3f;
                        voodoo->params.detail_scale[1] = (val >> 14) & 7;
                }
                break;
                case SST_texBaseAddr:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.texBaseAddr[0] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.texBaseAddr[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_texBaseAddr1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.texBaseAddr1[0] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.texBaseAddr1[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_texBaseAddr2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.texBaseAddr2[0] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.texBaseAddr2[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                case SST_texBaseAddr38:
                if (chip & CHIP_TREX0)
                {
                        voodoo->params.texBaseAddr38[0] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 0);
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->params.texBaseAddr38[1] = (val & 0x7ffff) << 3;
                        voodoo_recalc_tex(voodoo, 1);
                }
                break;
                
                case SST_trexInit1:
                if (chip & CHIP_TREX0)
                        voodoo->trexInit1[0] = val;
                if (chip & CHIP_TREX1)
                        voodoo->trexInit1[1] = val;
                break;

                case SST_nccTable0_Y0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable0_Y1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable0_Y2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable0_Y3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][0].y[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][0].y[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                
                case SST_nccTable0_I0:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[0] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[0] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_I2:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[2] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[2] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q0:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].q[0] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].q[0] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q2:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[2] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[2] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                if (val & (1 << 31))
                {
                        int p = (val >> 23) & 0xfe;
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->palette[0][p].u = val | 0xff000000;
                                voodoo->palette_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->palette[1][p].u = val | 0xff000000;
                                voodoo->palette_dirty[1] = 1;
                        }
                }
                break;
                        
                case SST_nccTable0_I1:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[1] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[1] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_I3:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].i[3] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].i[3] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q1:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].q[1] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].q[1] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                case SST_nccTable0_Q3:
                if (!(val & (1 << 31)))
                {
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->nccTable[0][0].q[3] = val;
                                voodoo->ncc_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->nccTable[1][0].q[3] = val;
                                voodoo->ncc_dirty[1] = 1;
                        }
                        break;
                }
                if (val & (1 << 31))
                {
                        int p = ((val >> 23) & 0xfe) | 0x01;
                        if (chip & CHIP_TREX0)
                        {
                                voodoo->palette[0][p].u = val | 0xff000000;
                                voodoo->palette_dirty[0] = 1;
                        }
                        if (chip & CHIP_TREX1)
                        {
                                voodoo->palette[1][p].u = val | 0xff000000;
                                voodoo->palette_dirty[1] = 1;
                        }
                }
                break;

                case SST_nccTable1_Y0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Y1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Y2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Y3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].y[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].y[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_I3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].i[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].i[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q0:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[0] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[0] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q1:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[1] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[1] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q2:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[2] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[2] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;
                case SST_nccTable1_Q3:
                if (chip & CHIP_TREX0)
                {
                        voodoo->nccTable[0][1].q[3] = val;
                        voodoo->ncc_dirty[0] = 1;
                }
                if (chip & CHIP_TREX1)
                {
                        voodoo->nccTable[1][1].q[3] = val;
                        voodoo->ncc_dirty[1] = 1;
                }
                break;

                case SST_userIntrCMD:
                fatal("userIntrCMD write %08x from FIFO\n", val);
                break;
        }
}


static uint16_t voodoo_fb_readw(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        int x, y;
        uint32_t read_addr;
        uint16_t temp;
        
        x = (addr >> 1) & 0x3ff;
        y = (addr >> 11) & 0x3ff;

        if (SLI_ENABLED)
        {
                voodoo_set_t *set = voodoo->set;
                
                if (y & 1)
                        voodoo = set->voodoos[1];
                else
                        voodoo = set->voodoos[0];
                        
                y >>= 1;
        }

        read_addr = voodoo->fb_read_offset + (x << 1) + (y * voodoo->row_width);

        if (read_addr > voodoo->fb_mask)
                return 0xffff;

        temp = *(uint16_t *)(&voodoo->fb_mem[read_addr & voodoo->fb_mask]);

//        voodoo_log("voodoo_fb_readw : %08X %08X  %i %i  %08X %08X  %08x:%08x %i\n", addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++);
        return temp;
}
static uint32_t voodoo_fb_readl(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        int x, y;
        uint32_t read_addr;
        uint32_t temp;
        
        x = addr & 0x7fe;
        y = (addr >> 11) & 0x3ff;

        if (SLI_ENABLED)
        {
                voodoo_set_t *set = voodoo->set;
                
                if (y & 1)
                        voodoo = set->voodoos[1];
                else
                        voodoo = set->voodoos[0];
                        
                y >>= 1;
        }

        read_addr = voodoo->fb_read_offset + x + (y * voodoo->row_width);

        if (read_addr > voodoo->fb_mask)
                return 0xffffffff;

        temp = *(uint32_t *)(&voodoo->fb_mem[read_addr & voodoo->fb_mask]);

//        voodoo_log("voodoo_fb_readl : %08X %08x %08X  x=%i y=%i  %08X %08X  %08x:%08x %i ro=%08x rw=%i\n", addr, read_addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++, voodoo->fb_read_offset, voodoo->row_width);
        return temp;
}

static inline uint16_t do_dither(voodoo_params_t *params, rgba8_t col, int x, int y)
{
        int r, g, b;
        
        if (dither)
        {
                if (dither2x2)
                {
                        r = dither_rb2x2[col.r][y & 1][x & 1];
                        g =  dither_g2x2[col.g][y & 1][x & 1];
                        b = dither_rb2x2[col.b][y & 1][x & 1];
                }
                else
                {
                        r = dither_rb[col.r][y & 3][x & 3];
                        g =  dither_g[col.g][y & 3][x & 3];
                        b = dither_rb[col.b][y & 3][x & 3];
                }
        }
        else
        {
                r = col.r >> 3;
                g = col.g >> 2;
                b = col.b >> 3;
        }

        return b | (g << 5) | (r << 11);
}

static void voodoo_fb_writew(uint32_t addr, uint16_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        voodoo_params_t *params = &voodoo->params;
        int x, y;
        uint32_t write_addr, write_addr_aux;
        rgba8_t colour_data;
        uint16_t depth_data;
        uint8_t alpha_data;
        int write_mask = 0;
        
        colour_data.r = colour_data.g = colour_data.b = colour_data.a = 0;

        depth_data = voodoo->params.zaColor & 0xffff;
        alpha_data = voodoo->params.zaColor >> 24;
        
//        while (!RB_EMPTY)
//                thread_reset_event(voodoo->not_full_event);
        
//        voodoo_log("voodoo_fb_writew : %08X %04X\n", addr, val);
        
               
        switch (voodoo->lfbMode & LFB_FORMAT_MASK)
        {
                case LFB_FORMAT_RGB565:
                colour_data = rgb565[val];
                alpha_data = 0xff;
                write_mask = LFB_WRITE_COLOUR;
                break;
                case LFB_FORMAT_RGB555:
                colour_data = argb1555[val];
                alpha_data = 0xff;
                write_mask = LFB_WRITE_COLOUR;
                break;
                case LFB_FORMAT_ARGB1555:
                colour_data = argb1555[val];
                alpha_data = colour_data.a;
                write_mask = LFB_WRITE_COLOUR;
                break;
                case LFB_FORMAT_DEPTH:
                depth_data = val;
                write_mask = LFB_WRITE_DEPTH;
                break;
                
                default:
                fatal("voodoo_fb_writew : bad LFB format %08X\n", voodoo->lfbMode);
        }

        x = addr & 0x7fe;
        y = (addr >> 11) & 0x3ff;
        
        if (SLI_ENABLED)
        {
                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (y & 1)) ||
                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(y & 1)))
                        return;
                y >>= 1;
        }


        if (voodoo->fb_write_offset == voodoo->params.front_offset)
                voodoo->dirty_line[y] = 1;
        
        write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
        write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);
        
//        voodoo_log("fb_writew %08x %i %i %i %08x\n", addr, x, y, voodoo->row_width, write_addr);

        if (voodoo->lfbMode & 0x100)
        {
                {
                        rgba8_t write_data = colour_data;
                        uint16_t new_depth = depth_data;

                        if (params->fbzMode & FBZ_DEPTH_ENABLE)
                        {
                                uint16_t old_depth = *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]);

                                DEPTH_TEST(new_depth);
                        }

                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                write_data.r == params->chromaKey_r &&
                                write_data.g == params->chromaKey_g &&
                                write_data.b == params->chromaKey_b)
                                goto skip_pixel;

                        if (params->fogMode & FOG_ENABLE)
                        {
                                int32_t z = new_depth << 12;
                                int64_t w_depth = (int64_t)(int32_t)new_depth;
                                int32_t ia = alpha_data << 12;

                                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia, w_depth);
                        }

                        if (params->alphaMode & 1)
                                ALPHA_TEST(alpha_data);

                        if (params->alphaMode & (1 << 4))
                        {
                                uint16_t dat = *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]);
                                int dest_r, dest_g, dest_b, dest_a;
                                
                                dest_r = (dat >> 8) & 0xf8;
                                dest_g = (dat >> 3) & 0xfc;
                                dest_b = (dat << 3) & 0xf8;
                                dest_r |= (dest_r >> 5);
                                dest_g |= (dest_g >> 6);
                                dest_b |= (dest_b >> 5);
                                dest_a = 0xff;
                                
                                ALPHA_BLEND(write_data.r, write_data.g, write_data.b, alpha_data);
                        }

                        if (params->fbzMode & FBZ_RGB_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, write_data, x >> 1, y);
                        if (params->fbzMode & FBZ_DEPTH_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = new_depth;

skip_pixel:
                        (void)x;
                }
        }
        else
        {               
                if (write_mask & LFB_WRITE_COLOUR)
                        *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, colour_data, x >> 1, y);
                if (write_mask & LFB_WRITE_DEPTH)
                        *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = depth_data;
        }
}


static void voodoo_fb_writel(uint32_t addr, uint32_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        voodoo_params_t *params = &voodoo->params;
        int x, y;
        uint32_t write_addr, write_addr_aux;
        rgba8_t colour_data[2];
        uint16_t depth_data[2];
        uint8_t alpha_data[2];
        int write_mask = 0, count = 1;

        depth_data[0] = depth_data[1] = voodoo->params.zaColor & 0xffff;
        alpha_data[0] = alpha_data[1] = voodoo->params.zaColor >> 24;
//        while (!RB_EMPTY)
//                thread_reset_event(voodoo->not_full_event);
        
//        voodoo_log("voodoo_fb_writel : %08X %08X\n", addr, val);
        
        switch (voodoo->lfbMode & LFB_FORMAT_MASK)
        {
                case LFB_FORMAT_RGB565:
                colour_data[0] = rgb565[val & 0xffff];
                colour_data[1] = rgb565[val >> 16];
                write_mask = LFB_WRITE_COLOUR;
                count = 2;
                break;
                case LFB_FORMAT_RGB555:
                colour_data[0] = argb1555[val & 0xffff];
                colour_data[1] = argb1555[val >> 16];
                write_mask = LFB_WRITE_COLOUR;
                count = 2;
                break;
                case LFB_FORMAT_ARGB1555:
                colour_data[0] = argb1555[val & 0xffff];
                alpha_data[0] = colour_data[0].a;
                colour_data[1] = argb1555[val >> 16];
                alpha_data[1] = colour_data[1].a;
                write_mask = LFB_WRITE_COLOUR;
                count = 2;
                break;
                
                case LFB_FORMAT_ARGB8888:
                colour_data[0].b = val & 0xff;
                colour_data[0].g = (val >> 8) & 0xff;
                colour_data[0].r = (val >> 16) & 0xff;
                alpha_data[0] = (val >> 24) & 0xff;
                write_mask = LFB_WRITE_COLOUR;
                addr >>= 1;
                break;
                
                case LFB_FORMAT_DEPTH:
                depth_data[0] = val;
                depth_data[1] = val >> 16;
                write_mask = LFB_WRITE_DEPTH;
                count = 2;
                break;
                
                default:
                fatal("voodoo_fb_writel : bad LFB format %08X\n", voodoo->lfbMode);
        }

        x = addr & 0x7fe;
        y = (addr >> 11) & 0x3ff;

        if (SLI_ENABLED)
        {
                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (y & 1)) ||
                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(y & 1)))
                        return;
                y >>= 1;
        }

        if (voodoo->fb_write_offset == voodoo->params.front_offset)
                voodoo->dirty_line[y] = 1;
        
        write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
        write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);
        
//        voodoo_log("fb_writel %08x x=%i y=%i rw=%i %08x wo=%08x\n", addr, x, y, voodoo->row_width, write_addr, voodoo->fb_write_offset);

        if (voodoo->lfbMode & 0x100)
        {
                int c;
                
                for (c = 0; c < count; c++)
                {
                        rgba8_t write_data = colour_data[c];
                        uint16_t new_depth = depth_data[c];

                        if (params->fbzMode & FBZ_DEPTH_ENABLE)
                        {
                                uint16_t old_depth = *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]);

                                DEPTH_TEST(new_depth);
                        }

                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                write_data.r == params->chromaKey_r &&
                                write_data.g == params->chromaKey_g &&
                                write_data.b == params->chromaKey_b)
                                goto skip_pixel;

                        if (params->fogMode & FOG_ENABLE)
                        {
                                int32_t z = new_depth << 12;
                                int64_t w_depth = new_depth;
                                int32_t ia = alpha_data[c] << 12;

                                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia, w_depth);
                        }

                        if (params->alphaMode & 1)
                                ALPHA_TEST(alpha_data[c]);

                        if (params->alphaMode & (1 << 4))
                        {
                                uint16_t dat = *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]);
                                int dest_r, dest_g, dest_b, dest_a;
                                
                                dest_r = (dat >> 8) & 0xf8;
                                dest_g = (dat >> 3) & 0xfc;
                                dest_b = (dat << 3) & 0xf8;
                                dest_r |= (dest_r >> 5);
                                dest_g |= (dest_g >> 6);
                                dest_b |= (dest_b >> 5);
                                dest_a = 0xff;
                                
                                ALPHA_BLEND(write_data.r, write_data.g, write_data.b, alpha_data[c]);
                        }

                        if (params->fbzMode & FBZ_RGB_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, write_data, (x >> 1) + c, y);
                        if (params->fbzMode & FBZ_DEPTH_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = new_depth;

skip_pixel:
                        write_addr += 2;
                        write_addr_aux += 2;
                }
        }
        else
        {
                int c;
                
                for (c = 0; c < count; c++)
                {
                        if (write_mask & LFB_WRITE_COLOUR)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, colour_data[c], (x >> 1) + c, y);
                        if (write_mask & LFB_WRITE_DEPTH)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = depth_data[c];
                        
                        write_addr += 2;
                        write_addr_aux += 2;
                }
        }
}

static void voodoo_tex_writel(uint32_t addr, uint32_t val, void *p)
{
        int lod, s, t;
        voodoo_t *voodoo = (voodoo_t *)p;
        int tmu;

        if (addr & 0x400000)
                return; /*TREX != 0*/
        
        tmu = (addr & 0x200000) ? 1 : 0;
        
        if (tmu && !voodoo->dual_tmus)
                return;

//        voodoo_log("voodoo_tex_writel : %08X %08X %i\n", addr, val, voodoo->params.tformat);
        
        lod = (addr >> 17) & 0xf;
        t = (addr >> 9) & 0xff;
        if (voodoo->params.tformat[tmu] & 8)
                s = (addr >> 1) & 0xfe;
        else
        {
                if (voodoo->params.textureMode[tmu] & (1 << 31))
                        s = addr & 0xfc;
                else
                        s = (addr >> 1) & 0xfc;
        }

        if (lod > LOD_MAX)
                return;
        
//        if (addr >= 0x200000)
//                return;
        
        if (voodoo->params.tformat[tmu] & 8)
                addr = voodoo->params.tex_base[tmu][lod] + s*2 + (t << voodoo->params.tex_shift[tmu][lod])*2;
        else
                addr = voodoo->params.tex_base[tmu][lod] + s + (t << voodoo->params.tex_shift[tmu][lod]);
        if (voodoo->texture_present[tmu][(addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT])
        {
//                voodoo_log("texture_present at %08x %i\n", addr, (addr & voodoo->texture_mask) >> TEX_DIRTY_SHIFT);
                flush_texture_cache(voodoo, addr & voodoo->texture_mask, tmu);
        }
        *(uint32_t *)(&voodoo->tex_mem[tmu][addr & voodoo->texture_mask]) = val;
}

#define WAKE_DELAY (TIMER_USEC * 100)
static inline void wake_fifo_thread(voodoo_t *voodoo)
{
        if (!voodoo->wake_timer)
        {
                /*Don't wake FIFO thread immediately - if we do that it will probably
                  process one word and go back to sleep, requiring it to be woken on
                  almost every write. Instead, wait a short while so that the CPU
                  emulation writes more data so we have more batched-up work.*/
                timer_process();
                voodoo->wake_timer = WAKE_DELAY;
                timer_update_outstanding();
        }
}

static inline void wake_fifo_thread_now(voodoo_t *voodoo)
{
        thread_set_event(voodoo->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

static void voodoo_wake_timer(void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        
        voodoo->wake_timer = 0;

        thread_set_event(voodoo->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

static inline void queue_command(voodoo_t *voodoo, uint32_t addr_type, uint32_t val)
{
        fifo_entry_t *fifo = &voodoo->fifo[voodoo->fifo_write_idx & FIFO_MASK];

        while (FIFO_FULL)
        {
                thread_reset_event(voodoo->fifo_not_full_event);
                if (FIFO_FULL)
                {
                        thread_wait_event(voodoo->fifo_not_full_event, 1); /*Wait for room in ringbuffer*/
                        if (FIFO_FULL)
                                wake_fifo_thread_now(voodoo);
                }
        }

        fifo->val = val;
        fifo->addr_type = addr_type;

        voodoo->fifo_write_idx++;
        
        if (FIFO_ENTRIES > 0xe000)
                wake_fifo_thread(voodoo);
}

static uint16_t voodoo_readw(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        
        addr &= 0xffffff;

        cycles -= voodoo->read_time;
        
        if ((addr & 0xc00000) == 0x400000) /*Framebuffer*/
        {
                if (SLI_ENABLED)
                {
                        voodoo_set_t *set = voodoo->set;
                        int y = (addr >> 11) & 0x3ff;
                
                        if (y & 1)
                                voodoo = set->voodoos[1];
                        else
                                voodoo = set->voodoos[0];
                }

                voodoo->flush = 1;
                while (!FIFO_EMPTY)
                {
                        wake_fifo_thread_now(voodoo);
                        thread_wait_event(voodoo->fifo_not_full_event, 1);
                }
                wait_for_render_thread_idle(voodoo);
                voodoo->flush = 0;
                
                return voodoo_fb_readw(addr, voodoo);
        }

        return 0xffff;
}

static void voodoo_flush(voodoo_t *voodoo)
{
        voodoo->flush = 1;
        while (!FIFO_EMPTY)
        {
                wake_fifo_thread_now(voodoo);
                thread_wait_event(voodoo->fifo_not_full_event, 1);
        }
        wait_for_render_thread_idle(voodoo);
        voodoo->flush = 0;
}

static void wake_fifo_threads(voodoo_set_t *set, voodoo_t *voodoo)
{
        wake_fifo_thread(voodoo);
        if (SLI_ENABLED && voodoo->type != VOODOO_2 && set->voodoos[0] == voodoo)
                wake_fifo_thread(set->voodoos[1]);
}

static uint32_t voodoo_readl(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        uint32_t temp = 0;
        int fifo_size;
        voodoo->rd_count++;
        addr &= 0xffffff;
        
        cycles -= voodoo->read_time;

        if (addr & 0x800000) /*Texture*/
        {
        }
        else if (addr & 0x400000) /*Framebuffer*/
        {
                if (SLI_ENABLED)
                {
                        voodoo_set_t *set = voodoo->set;
                        int y = (addr >> 11) & 0x3ff;
                
                        if (y & 1)
                                voodoo = set->voodoos[1];
                        else
                                voodoo = set->voodoos[0];
                }

                voodoo->flush = 1;
                while (!FIFO_EMPTY)
                {
                        wake_fifo_thread_now(voodoo);
                        thread_wait_event(voodoo->fifo_not_full_event, 1);
                }
                wait_for_render_thread_idle(voodoo);
                voodoo->flush = 0;
                
                temp = voodoo_fb_readl(addr, voodoo);
        }
        else switch (addr & 0x3fc)
        {
                case SST_status:
                {
                        int fifo_entries = FIFO_ENTRIES;
                        int swap_count = voodoo->swap_count;
                        int written = voodoo->cmd_written + voodoo->cmd_written_fifo;
                        int busy = (written - voodoo->cmd_read) || (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr);

                        if (SLI_ENABLED && voodoo->type != VOODOO_2)
                        {
                                voodoo_t *voodoo_other = (voodoo == voodoo->set->voodoos[0]) ? voodoo->set->voodoos[1] : voodoo->set->voodoos[0];
                                int other_written = voodoo_other->cmd_written + voodoo_other->cmd_written_fifo;
                                                        
                                if (voodoo_other->swap_count > swap_count)
                                        swap_count = voodoo_other->swap_count;
                                if ((voodoo_other->fifo_write_idx - voodoo_other->fifo_read_idx) > fifo_entries)
                                        fifo_entries = voodoo_other->fifo_write_idx - voodoo_other->fifo_read_idx;
                                if ((other_written - voodoo_other->cmd_read) ||
                                    (voodoo_other->cmdfifo_depth_rd != voodoo_other->cmdfifo_depth_wr))
                                        busy = 1;
                                if (!voodoo_other->voodoo_busy)
                                        wake_fifo_thread(voodoo_other);
                        }
                        
                        fifo_size = 0xffff - fifo_entries;
                        temp = fifo_size << 12;
                        if (fifo_size < 0x40)
                                temp |= fifo_size;
                        else
                                temp |= 0x3f;
                        if (swap_count < 7)
                                temp |= (swap_count << 28);
                        else
                                temp |= (7 << 28);
                        if (!voodoo->v_retrace)
                                temp |= 0x40;

                        if (busy)
                                temp |= 0x380; /*Busy*/

                        if (!voodoo->voodoo_busy)
                                wake_fifo_thread(voodoo);
                }
                break;

                case SST_fbzColorPath:
                voodoo_flush(voodoo);
                temp = voodoo->params.fbzColorPath;
                break;
                case SST_fogMode:
                voodoo_flush(voodoo);
                temp = voodoo->params.fogMode;
                break;
                case SST_alphaMode:
                voodoo_flush(voodoo);
                temp = voodoo->params.alphaMode;
                break;
                case SST_fbzMode:
                voodoo_flush(voodoo);
                temp = voodoo->params.fbzMode;
                break;                        
                case SST_lfbMode:
                voodoo_flush(voodoo);
                temp = voodoo->lfbMode;
                break;
                case SST_clipLeftRight:
                voodoo_flush(voodoo);
                temp = voodoo->params.clipRight | (voodoo->params.clipLeft << 16);
                break;
                case SST_clipLowYHighY:
                voodoo_flush(voodoo);
                temp = voodoo->params.clipHighY | (voodoo->params.clipLowY << 16);
                break;

                case SST_stipple:
                voodoo_flush(voodoo);
                temp = voodoo->params.stipple;
                break;
                case SST_color0:
                voodoo_flush(voodoo);
                temp = voodoo->params.color0;
                break;
                case SST_color1:
                voodoo_flush(voodoo);
                temp = voodoo->params.color1;
                break;
                
                case SST_fbiPixelsIn:
                temp = voodoo->fbiPixelsIn & 0xffffff;
                break;
                case SST_fbiChromaFail:
                temp = voodoo->fbiChromaFail & 0xffffff;
                break;
                case SST_fbiZFuncFail:
                temp = voodoo->fbiZFuncFail & 0xffffff;
                break;
                case SST_fbiAFuncFail:
                temp = voodoo->fbiAFuncFail & 0xffffff;
                break;
                case SST_fbiPixelsOut:
                temp = voodoo->fbiPixelsOut & 0xffffff;
                break;

                case SST_fbiInit4:
                temp = voodoo->fbiInit4;
                break;
                case SST_fbiInit0:
                temp = voodoo->fbiInit0;
                break;
                case SST_fbiInit1:
                temp = voodoo->fbiInit1;
                break;              
                case SST_fbiInit2:
                if (voodoo->initEnable & 0x04)
                        temp = voodoo->dac_readdata;
                else
                        temp = voodoo->fbiInit2;
                break;
                case SST_fbiInit3:
                temp = voodoo->fbiInit3 | (1 << 10) | (2 << 8);
                break;

                case SST_vRetrace:
                timer_clock();
                temp = voodoo->line & 0x1fff;
                break;
                case SST_hvRetrace:
                timer_clock();
                temp = voodoo->line & 0x1fff;
                temp |= ((((voodoo->line_time - voodoo->timer_count) * voodoo->h_total) / voodoo->timer_count) << 16) & 0x7ff0000;
                break;

                case SST_fbiInit5:
                temp = voodoo->fbiInit5 & ~0x1ff;
                break;
                case SST_fbiInit6:
                temp = voodoo->fbiInit6;
                break;
                case SST_fbiInit7:
                temp = voodoo->fbiInit7 & ~0xff;
                break;

                case SST_cmdFifoBaseAddr:
                temp = voodoo->cmdfifo_base >> 12;
                temp |= (voodoo->cmdfifo_end >> 12) << 16;
                break;
                
                case SST_cmdFifoRdPtr:
                temp = voodoo->cmdfifo_rp;
                break;
                case SST_cmdFifoAMin:
                temp = voodoo->cmdfifo_amin;
                break;
                case SST_cmdFifoAMax:
                temp = voodoo->cmdfifo_amax;
                break;
                case SST_cmdFifoDepth:
                temp = voodoo->cmdfifo_depth_wr - voodoo->cmdfifo_depth_rd;
                break;
                
                default:
                fatal("voodoo_readl  : bad addr %08X\n", addr);
                temp = 0xffffffff;
        }
        
        return temp;
}

static void voodoo_writew(uint32_t addr, uint16_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        voodoo->wr_count++;
        addr &= 0xffffff;

        if (addr == voodoo->last_write_addr+4)
                cycles -= voodoo->burst_time;
        else
                cycles -= voodoo->write_time;
        voodoo->last_write_addr = addr;

        if ((addr & 0xc00000) == 0x400000) /*Framebuffer*/
                queue_command(voodoo, addr | FIFO_WRITEW_FB, val);
}

static void voodoo_pixelclock_update(voodoo_t *voodoo)
{
        int m  =  (voodoo->dac_pll_regs[0] & 0x7f) + 2;
        int n1 = ((voodoo->dac_pll_regs[0] >>  8) & 0x1f) + 2;
        int n2 = ((voodoo->dac_pll_regs[0] >> 13) & 0x07);
        float t = (14318184.0 * ((float)m / (float)n1)) / (float)(1 << n2);
        double clock_const;
        int line_length;
        
        if ((voodoo->dac_data[6] & 0xf0) == 0x20 ||
            (voodoo->dac_data[6] & 0xf0) == 0x60 ||
            (voodoo->dac_data[6] & 0xf0) == 0x70)
                t /= 2.0f;
                
        line_length = (voodoo->hSync & 0xff) + ((voodoo->hSync >> 16) & 0x3ff);
        
//        voodoo_log("Pixel clock %f MHz hsync %08x line_length %d\n", t, voodoo->hSync, line_length);
        
        voodoo->pixel_clock = t;

        clock_const = cpuclock / t;
        voodoo->line_time = (int)((double)line_length * clock_const * (double)(1 << TIMER_SHIFT));
}

static void voodoo_writel(uint32_t addr, uint32_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;

        voodoo->wr_count++;

        addr &= 0xffffff;
        
        if (addr == voodoo->last_write_addr+4)
                cycles -= voodoo->burst_time;
        else
                cycles -= voodoo->write_time;
        voodoo->last_write_addr = addr;

        if (addr & 0x800000) /*Texture*/
        {
                voodoo->tex_count++;
                queue_command(voodoo, addr | FIFO_WRITEL_TEX, val);
        }
        else if (addr & 0x400000) /*Framebuffer*/
        {
                queue_command(voodoo, addr | FIFO_WRITEL_FB, val);
        }
        else if ((addr & 0x200000) && (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE))
        {
//                voodoo_log("Write CMDFIFO %08x(%08x) %08x  %08x\n", addr, voodoo->cmdfifo_base + (addr & 0x3fffc), val, (voodoo->cmdfifo_base + (addr & 0x3fffc)) & voodoo->fb_mask);
                *(uint32_t *)&voodoo->fb_mem[(voodoo->cmdfifo_base + (addr & 0x3fffc)) & voodoo->fb_mask] = val;
                voodoo->cmdfifo_depth_wr++;
                if ((voodoo->cmdfifo_depth_wr - voodoo->cmdfifo_depth_rd) < 20)
                        wake_fifo_thread(voodoo);
        }
        else switch (addr & 0x3fc)
        {
                case SST_intrCtrl:
                fatal("intrCtrl write %08x\n", val);
                break;

                case SST_userIntrCMD:
                fatal("userIntrCMD write %08x\n", val);
                break;
                
                case SST_swapbufferCMD:
                voodoo->cmd_written++;
                voodoo->swap_count++;
                if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE)
                        return;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_threads(voodoo->set, voodoo);
                break;
                case SST_triangleCMD:
                if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE)
                        return;
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_threads(voodoo->set, voodoo);
                break;
                case SST_ftriangleCMD:
                if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE)
                        return;
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_threads(voodoo->set, voodoo);
                break;
                case SST_fastfillCMD:
                if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE)
                        return;
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_threads(voodoo->set, voodoo);
                break;
                case SST_nopCMD:
                if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE)
                        return;
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_threads(voodoo->set, voodoo);
                break;
                        
                case SST_fbiInit4:
                if (voodoo->initEnable & 0x01)
                {
                        voodoo->fbiInit4 = val;
                        voodoo->read_time = pci_nonburst_time + pci_burst_time * ((voodoo->fbiInit4 & 1) ? 2 : 1);
//                        voodoo_log("fbiInit4 write %08x - read_time=%i\n", val, voodoo->read_time);
                }
                break;
                case SST_backPorch:
                voodoo->backPorch = val;
                break;
                case SST_videoDimensions:
                voodoo->videoDimensions = val;
                voodoo->h_disp = (val & 0xfff) + 1;
                voodoo->v_disp = (val >> 16) & 0xfff;
                break;
                case SST_fbiInit0:
                if (voodoo->initEnable & 0x01)
                {
                        voodoo->fbiInit0 = val;
                        if (voodoo->set->nr_cards == 2)
                                svga_set_override(voodoo->svga, (voodoo->set->voodoos[0]->fbiInit0 | voodoo->set->voodoos[1]->fbiInit0) & 1);
                        else
                                svga_set_override(voodoo->svga, val & 1);
                        if (val & FBIINIT0_GRAPHICS_RESET)
                        {
                                /*Reset display/draw buffer selection. This may not actually
                                  happen here on a real Voodoo*/
                                voodoo->disp_buffer = 0;
                                voodoo->draw_buffer = 1;
                                voodoo_recalc(voodoo);
                                voodoo->front_offset = voodoo->params.front_offset;
                        }
                }
                break;
                case SST_fbiInit1:
                if (voodoo->initEnable & 0x01)
                {
                        if ((voodoo->fbiInit1 & FBIINIT1_VIDEO_RESET) && !(val & FBIINIT1_VIDEO_RESET))
                        {
                                voodoo->line = 0;
                                voodoo->swap_count = 0;
                                voodoo->retrace_count = 0;
                        }
                        voodoo->fbiInit1 = (val & ~5) | (voodoo->fbiInit1 & 5);
                        voodoo->write_time = pci_nonburst_time + pci_burst_time * ((voodoo->fbiInit1 & 2) ? 1 : 0);
                        voodoo->burst_time = pci_burst_time * ((voodoo->fbiInit1 & 2) ? 2 : 1);
//                        voodoo_log("fbiInit1 write %08x - write_time=%i burst_time=%i\n", val, voodoo->write_time, voodoo->burst_time);
                }
                break;
                case SST_fbiInit2:
                if (voodoo->initEnable & 0x01)
                {
                        voodoo->fbiInit2 = val;
                        voodoo_recalc(voodoo);
                }
                break;
                case SST_fbiInit3:
                if (voodoo->initEnable & 0x01)
                        voodoo->fbiInit3 = val;
                break;

                case SST_hSync:
                voodoo->hSync = val;
                voodoo->h_total = (val & 0xffff) + (val >> 16);
                voodoo_pixelclock_update(voodoo);
                break;
                case SST_vSync:
                voodoo->vSync = val;
                voodoo->v_total = (val & 0xffff) + (val >> 16);
                break;
                
                case SST_clutData:
                voodoo->clutData[(val >> 24) & 0x3f].b = val & 0xff;
                voodoo->clutData[(val >> 24) & 0x3f].g = (val >> 8) & 0xff;
                voodoo->clutData[(val >> 24) & 0x3f].r = (val >> 16) & 0xff;
                if (val & 0x20000000)
                {
                        voodoo->clutData[(val >> 24) & 0x3f].b = 255;
                        voodoo->clutData[(val >> 24) & 0x3f].g = 255;
                        voodoo->clutData[(val >> 24) & 0x3f].r = 255;
                }
                voodoo->clutData_dirty = 1;
                break;

                case SST_dacData:
                voodoo->dac_reg = (val >> 8) & 7;
                voodoo->dac_readdata = 0xff;
                if (val & 0x800)
                {
//                        voodoo_log("  dacData read %i %02X\n", voodoo->dac_reg, voodoo->dac_data[7]);
                        if (voodoo->dac_reg == 5)
                        {
                                switch (voodoo->dac_data[7])
                                {
        				case 0x01: voodoo->dac_readdata = 0x55; break;
        				case 0x07: voodoo->dac_readdata = 0x71; break;
        				case 0x0b: voodoo->dac_readdata = 0x79; break;
                                }
                        }
                        else
                                voodoo->dac_readdata = voodoo->dac_data[voodoo->dac_readdata & 7];
                }
                else
                {
                        if (voodoo->dac_reg == 5)
                        {
                                if (!voodoo->dac_reg_ff)
                                        voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] = (voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] & 0xff00) | val;
                                else
                                        voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] = (voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] & 0xff) | (val << 8);
//                                voodoo_log("Write PLL reg %x %04x\n", voodoo->dac_data[4] & 0xf, voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf]);
                                voodoo->dac_reg_ff = !voodoo->dac_reg_ff;
                                if (!voodoo->dac_reg_ff)
                                        voodoo->dac_data[4]++;

                        }
                        else
                        {
                                voodoo->dac_data[voodoo->dac_reg] = val & 0xff;
                                voodoo->dac_reg_ff = 0;
                        }
                        voodoo_pixelclock_update(voodoo);
                }
                break;

		case SST_scrFilter:
		if (voodoo->initEnable & 0x01)
		{
			voodoo->scrfilterEnabled = 1;
			voodoo->scrfilterThreshold = val; 	/* update the threshold values and generate a new lookup table if necessary */
		
			if (val < 1) 
				voodoo->scrfilterEnabled = 0;
			voodoo_threshold_check(voodoo);		
			voodoo_log("Voodoo Filter: %06x\n", val);
		}
		break;

                case SST_fbiInit5:
                if (voodoo->initEnable & 0x01)
                        voodoo->fbiInit5 = (val & ~0x41e6) | (voodoo->fbiInit5 & 0x41e6);
                break;
                case SST_fbiInit6:
                if (voodoo->initEnable & 0x01)
                        voodoo->fbiInit6 = val;
                break;
                case SST_fbiInit7:
                if (voodoo->initEnable & 0x01)
                        voodoo->fbiInit7 = val;
                break;

                case SST_cmdFifoBaseAddr:
                voodoo->cmdfifo_base = (val & 0x3ff) << 12;
                voodoo->cmdfifo_end = ((val >> 16) & 0x3ff) << 12;
//                voodoo_log("CMDFIFO base=%08x end=%08x\n", voodoo->cmdfifo_base, voodoo->cmdfifo_end);
                break;

                case SST_cmdFifoRdPtr:
                voodoo->cmdfifo_rp = val;
                break;
                case SST_cmdFifoAMin:
                voodoo->cmdfifo_amin = val;
                break;
                case SST_cmdFifoAMax:
                voodoo->cmdfifo_amax = val;
                break;
                case SST_cmdFifoDepth:
                voodoo->cmdfifo_depth_rd = 0;
                voodoo->cmdfifo_depth_wr = val & 0xffff;
                break;

                default:
                if (voodoo->fbiInit7 & FBIINIT7_CMDFIFO_ENABLE)
                {
                        voodoo_log("Unknown register write in CMDFIFO mode %08x %08x\n", addr, val);
                }
                else
                {
                        queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                }
                break;
        }
}

static uint16_t voodoo_snoop_readw(uint32_t addr, void *p)
{
        voodoo_set_t *set = (voodoo_set_t *)p;
        
        return voodoo_readw(addr, set->voodoos[0]);
}
static uint32_t voodoo_snoop_readl(uint32_t addr, void *p)
{
        voodoo_set_t *set = (voodoo_set_t *)p;
        
        return voodoo_readl(addr, set->voodoos[0]);
}

static void voodoo_snoop_writew(uint32_t addr, uint16_t val, void *p)
{
        voodoo_set_t *set = (voodoo_set_t *)p;

        voodoo_writew(addr, val, set->voodoos[0]);
        voodoo_writew(addr, val, set->voodoos[1]);
}
static void voodoo_snoop_writel(uint32_t addr, uint32_t val, void *p)
{
        voodoo_set_t *set = (voodoo_set_t *)p;

        voodoo_writel(addr, val, set->voodoos[0]);
        voodoo_writel(addr, val, set->voodoos[1]);
}

static uint32_t cmdfifo_get(voodoo_t *voodoo)
{
        uint32_t val;
        
        while (voodoo->cmdfifo_depth_rd == voodoo->cmdfifo_depth_wr)
        {
                thread_wait_event(voodoo->wake_fifo_thread, -1);
                thread_reset_event(voodoo->wake_fifo_thread);
        }

        val = *(uint32_t *)&voodoo->fb_mem[voodoo->cmdfifo_rp & voodoo->fb_mask];
        
        voodoo->cmdfifo_depth_rd++;
        voodoo->cmdfifo_rp += 4;

//        voodoo_log("  CMDFIFO get %08x\n", val);
        return val;
}

static inline float cmdfifo_get_f(voodoo_t *voodoo)
{
        union
        {
                uint32_t i;
                float f;
        } tempif;
        
        tempif.i = cmdfifo_get(voodoo);
        return tempif.f;
}

enum
{
        CMDFIFO3_PC_MASK_RGB   = (1 << 10),
        CMDFIFO3_PC_MASK_ALPHA = (1 << 11),
        CMDFIFO3_PC_MASK_Z     = (1 << 12),
        CMDFIFO3_PC_MASK_Wb    = (1 << 13),
        CMDFIFO3_PC_MASK_W0    = (1 << 14),
        CMDFIFO3_PC_MASK_S0_T0 = (1 << 15),
        CMDFIFO3_PC_MASK_W1    = (1 << 16),
        CMDFIFO3_PC_MASK_S1_T1 = (1 << 17),
        
        CMDFIFO3_PC = (1 << 28)
};

static void fifo_thread(void *param)
{
        voodoo_t *voodoo = (voodoo_t *)param;
        
        while (1)
        {
                thread_set_event(voodoo->fifo_not_full_event);
                thread_wait_event(voodoo->wake_fifo_thread, -1);
                thread_reset_event(voodoo->wake_fifo_thread);
                voodoo->voodoo_busy = 1;
                while (!FIFO_EMPTY)
                {
                        uint64_t start_time = plat_timer_read();
                        uint64_t end_time;
                        fifo_entry_t *fifo = &voodoo->fifo[voodoo->fifo_read_idx & FIFO_MASK];

                        switch (fifo->addr_type & FIFO_TYPE)
                        {
                                case FIFO_WRITEL_REG:
                                voodoo_reg_writel(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                                break;
                                case FIFO_WRITEW_FB:
                                wait_for_render_thread_idle(voodoo);
                                voodoo_fb_writew(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                                break;
                                case FIFO_WRITEL_FB:
                                wait_for_render_thread_idle(voodoo);
                                voodoo_fb_writel(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                                break;
                                case FIFO_WRITEL_TEX:
                                if (!(fifo->addr_type & 0x400000))
                                        voodoo_tex_writel(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                                break;
                        }
                        voodoo->fifo_read_idx++;
                        fifo->addr_type = FIFO_INVALID;

                        if (FIFO_ENTRIES > 0xe000)
                                thread_set_event(voodoo->fifo_not_full_event);

                        end_time = plat_timer_read();
                        voodoo->time += end_time - start_time;
                }

                while (voodoo->cmdfifo_depth_rd != voodoo->cmdfifo_depth_wr)
                {
                        uint64_t start_time = plat_timer_read();
                        uint64_t end_time;
                        uint32_t header = cmdfifo_get(voodoo);
                        uint32_t addr;
                        uint32_t mask;
                        int smode;
                        int num;
                        int num_verticies;
                        int v_num;
                
//                voodoo_log(" CMDFIFO header %08x at %08x\n", header, voodoo->cmdfifo_rp);
                
                        switch (header & 7)
                        {
                                case 0:
//                                voodoo_log("CMDFIFO0\n");
                                switch ((header >> 3) & 7)
                                {
                                        case 0: /*NOP*/
                                        break;
                                        
                                        case 3: /*JMP local frame buffer*/
                                        voodoo->cmdfifo_rp = (header >> 4) & 0xfffffc;
//                                        voodoo_log("JMP to %08x %04x\n", voodoo->cmdfifo_rp, header);
                                        break;
                                        
                                        default:
                                        fatal("Bad CMDFIFO0 %08x\n", header);
                                }
                                break;
                                        
                                case 1:
                                num = header >> 16;
                                addr = (header & 0x7ff8) >> 1;
//                                voodoo_log("CMDFIFO1 addr=%08x\n",addr);
                                while (num--)
                                {
                                        uint32_t val = cmdfifo_get(voodoo);
                                        if ((addr & 0x3ff) == SST_triangleCMD || (addr & 0x3ff) == SST_ftriangleCMD ||
                                            (addr & 0x3ff) == SST_fastfillCMD || (addr & 0x3ff) == SST_nopCMD)
                                                voodoo->cmd_written_fifo++;
                                                
                                        voodoo_reg_writel(addr, val, voodoo);
                                
                                        if (header & (1 << 15))
                                                addr += 4;
                                }
                                break;
                        
                                case 3:
                                num = (header >> 29) & 7;                        
                                mask = header;//(header >> 10) & 0xff;
                                smode = (header >> 22) & 0xf;
                                voodoo_reg_writel(SST_sSetupMode, ((header >> 10) & 0xff) | (smode << 16), voodoo);
                                num_verticies = (header >> 6) & 0xf;
                                v_num = 0;
                                if (((header >> 3) & 7) == 2)
                                        v_num = 1;
//                                voodoo_log("CMDFIFO3: num=%i verts=%i mask=%02x\n", num, num_verticies, (header >> 10) & 0xff);
//                                voodoo_log("CMDFIFO3 %02x %i\n", (header >> 10), (header >> 3) & 7);

                                while (num_verticies--)
                                {
                                        voodoo->verts[3].sVx = cmdfifo_get_f(voodoo);
                                        voodoo->verts[3].sVy = cmdfifo_get_f(voodoo);
                                        if (mask & CMDFIFO3_PC_MASK_RGB)
                                        {
                                                if (header & CMDFIFO3_PC)
                                                {
                                                        uint32_t val = cmdfifo_get(voodoo);
                                                        voodoo->verts[3].sBlue  = (float)(val & 0xff);
                                                        voodoo->verts[3].sGreen = (float)((val >> 8) & 0xff);
                                                        voodoo->verts[3].sRed   = (float)((val >> 16) & 0xff);
                                                        voodoo->verts[3].sAlpha = (float)((val >> 24) & 0xff);
                                                }
                                                else
                                                {
                                                        voodoo->verts[3].sRed = cmdfifo_get_f(voodoo);
                                                        voodoo->verts[3].sGreen = cmdfifo_get_f(voodoo);
                                                        voodoo->verts[3].sBlue = cmdfifo_get_f(voodoo);
                                                }
                                        }
                                        if ((mask & CMDFIFO3_PC_MASK_ALPHA) && !(header & CMDFIFO3_PC))
                                                voodoo->verts[3].sAlpha = cmdfifo_get_f(voodoo);
                                        if (mask & CMDFIFO3_PC_MASK_Z)
                                                voodoo->verts[3].sVz = cmdfifo_get_f(voodoo);
                                        if (mask & CMDFIFO3_PC_MASK_Wb)
                                                voodoo->verts[3].sWb = cmdfifo_get_f(voodoo);
                                        if (mask & CMDFIFO3_PC_MASK_W0)
                                                voodoo->verts[3].sW0 = cmdfifo_get_f(voodoo);
                                        if (mask & CMDFIFO3_PC_MASK_S0_T0)
                                        {
                                                voodoo->verts[3].sS0 = cmdfifo_get_f(voodoo);
                                                voodoo->verts[3].sT0 = cmdfifo_get_f(voodoo);
                                        }
                                        if (mask & CMDFIFO3_PC_MASK_W1)
                                                voodoo->verts[3].sW1 = cmdfifo_get_f(voodoo);
                                        if (mask & CMDFIFO3_PC_MASK_S1_T1)
                                        {
                                                voodoo->verts[3].sS1 = cmdfifo_get_f(voodoo);
                                                voodoo->verts[3].sT1 = cmdfifo_get_f(voodoo);
                                        }
                                        if (v_num)
                                                voodoo_reg_writel(SST_sDrawTriCMD, 0, voodoo);
                                        else
                                                voodoo_reg_writel(SST_sBeginTriCMD, 0, voodoo);
                                        v_num++;
                                        if (v_num == 3 && ((header >> 3) & 7) == 0)
                                                v_num = 0;
                                }
                                break;

                                case 4:
                                num = (header >> 29) & 7;
                                mask = (header >> 15) & 0x3fff;
                                addr = (header & 0x7ff8) >> 1;
//                                voodoo_log("CMDFIFO4 addr=%08x\n",addr);
                                while (mask)
                                {
                                        if (mask & 1)
                                        {
                                                uint32_t val = cmdfifo_get(voodoo);
                                                if ((addr & 0x3ff) == SST_triangleCMD || (addr & 0x3ff) == SST_ftriangleCMD ||
                                                    (addr & 0x3ff) == SST_fastfillCMD || (addr & 0x3ff) == SST_nopCMD)
                                                        voodoo->cmd_written_fifo++;

                                                voodoo_reg_writel(addr, val, voodoo);
                                        }
                                        
                                        addr += 4;
                                        mask >>= 1;
                                }
                                while (num--)
                                        cmdfifo_get(voodoo);
                                break;
                        
                                case 5:
                                if (header & 0x3fc0000)
                                        fatal("CMDFIFO packet 5 has byte disables set %08x\n", header);
                                num = (header >> 3) & 0x7ffff;
                                addr = cmdfifo_get(voodoo) & 0xffffff;
//                                voodoo_log("CMDFIFO5 addr=%08x num=%i\n", addr, num);
                                switch (header >> 30)
                                {
                                        case 2: /*Framebuffer*/
                                        while (num--)
                                        {
                                                uint32_t val = cmdfifo_get(voodoo);
                                                voodoo_fb_writel(addr, val, voodoo);
                                                addr += 4;
                                        }
                                        break;
                                        case 3: /*Texture*/
                                        while (num--)
                                        {
                                                uint32_t val = cmdfifo_get(voodoo);
                                                voodoo_tex_writel(addr, val, voodoo);
                                                addr += 4;
                                        }
                                        break;
                                                
                                        default:
                                        fatal("CMDFIFO packet 5 bad space %08x %08x\n", header, voodoo->cmdfifo_rp);
                                }
                                break;
                        
                                default:
                                voodoo_log("Bad CMDFIFO packet %08x %08x\n", header, voodoo->cmdfifo_rp);
                        }

                        end_time = plat_timer_read();
                        voodoo->time += end_time - start_time;
                }
                voodoo->voodoo_busy = 0;
        }
}

static void voodoo_recalcmapping(voodoo_set_t *set)
{
        if (set->nr_cards == 2)
        {
                if (set->voodoos[0]->pci_enable && set->voodoos[0]->memBaseAddr)
                {
                        if (set->voodoos[0]->type == VOODOO_2 && set->voodoos[1]->initEnable & (1 << 23))
                        {
                                voodoo_log("voodoo_recalcmapping (pri) with snoop : memBaseAddr %08X\n", set->voodoos[0]->memBaseAddr);
                                mem_mapping_disable(&set->voodoos[0]->mapping);
                                mem_mapping_set_addr(&set->snoop_mapping, set->voodoos[0]->memBaseAddr, 0x01000000);
                        }
                        else if (set->voodoos[1]->pci_enable && (set->voodoos[0]->memBaseAddr == set->voodoos[1]->memBaseAddr))
                        {
                                voodoo_log("voodoo_recalcmapping (pri) (sec) same addr : memBaseAddr %08X\n", set->voodoos[0]->memBaseAddr);
                                mem_mapping_disable(&set->voodoos[0]->mapping);
                                mem_mapping_disable(&set->voodoos[1]->mapping);
                                mem_mapping_set_addr(&set->snoop_mapping, set->voodoos[0]->memBaseAddr, 0x01000000);
                                return;
                        }
                        else
                        {
                                voodoo_log("voodoo_recalcmapping (pri) : memBaseAddr %08X\n", set->voodoos[0]->memBaseAddr);
                                mem_mapping_disable(&set->snoop_mapping);
                                mem_mapping_set_addr(&set->voodoos[0]->mapping, set->voodoos[0]->memBaseAddr, 0x01000000);
                        }
                }
                else
                {
                        voodoo_log("voodoo_recalcmapping (pri) : disabled\n");
                        mem_mapping_disable(&set->voodoos[0]->mapping);
                }

                if (set->voodoos[1]->pci_enable && set->voodoos[1]->memBaseAddr)
                {
                        voodoo_log("voodoo_recalcmapping (sec) : memBaseAddr %08X\n", set->voodoos[1]->memBaseAddr);
                        mem_mapping_set_addr(&set->voodoos[1]->mapping, set->voodoos[1]->memBaseAddr, 0x01000000);
                }
                else
                {
                        voodoo_log("voodoo_recalcmapping (sec) : disabled\n");
                        mem_mapping_disable(&set->voodoos[1]->mapping);
                }
        }
        else
        {
                voodoo_t *voodoo = set->voodoos[0];
                
                if (voodoo->pci_enable && voodoo->memBaseAddr)
                {
                        voodoo_log("voodoo_recalcmapping : memBaseAddr %08X\n", voodoo->memBaseAddr);
                        mem_mapping_set_addr(&voodoo->mapping, voodoo->memBaseAddr, 0x01000000);
                }
                else
                {
                        voodoo_log("voodoo_recalcmapping : disabled\n");
                        mem_mapping_disable(&voodoo->mapping);
                }
        }
}

uint8_t voodoo_pci_read(int func, int addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;

        if (func)
                return 0;

//        voodoo_log("Voodoo PCI read %08X PC=%08x\n", addr, cpu_state.pc);

        switch (addr)
        {
                case 0x00: return 0x1a; /*3dfx*/
                case 0x01: return 0x12;
                
                case 0x02:
                if (voodoo->type == VOODOO_2)
                        return 0x02; /*Voodoo 2*/
                else
                        return 0x01; /*SST-1 (Voodoo Graphics)*/
                case 0x03: return 0x00;
                
                case 0x04: return voodoo->pci_enable ? 0x02 : 0x00; /*Respond to memory accesses*/

                case 0x08: return 2; /*Revision ID*/
                case 0x09: return 0; /*Programming interface*/
                case 0x0a: return 0;
                case 0x0b: return 0x04;
                
                case 0x10: return 0x00; /*memBaseAddr*/
                case 0x11: return 0x00;
                case 0x12: return 0x00;
                case 0x13: return voodoo->memBaseAddr >> 24;

                case 0x40:
                return voodoo->initEnable & 0xff;
                case 0x41:
                if (voodoo->type == VOODOO_2)
                        return 0x50 | ((voodoo->initEnable >> 8) & 0x0f);
                return (voodoo->initEnable >> 8) & 0x0f;
                case 0x42:
                return (voodoo->initEnable >> 16) & 0xff;
                case 0x43:
                return (voodoo->initEnable >> 24) & 0xff;
        }
        return 0;
}

void voodoo_pci_write(int func, int addr, uint8_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        
        if (func)
                return;

//        voodoo_log("Voodoo PCI write %04X %02X PC=%08x\n", addr, val, cpu_state.pc);

        switch (addr)
        {
                case 0x04:
                voodoo->pci_enable = val & 2;
                voodoo_recalcmapping(voodoo->set);
                break;
                
                case 0x13:
                voodoo->memBaseAddr = val << 24;
                voodoo_recalcmapping(voodoo->set);
                break;
                
                case 0x40:
                voodoo->initEnable = (voodoo->initEnable & ~0x000000ff) | val;
                break;
                case 0x41:
                voodoo->initEnable = (voodoo->initEnable & ~0x0000ff00) | (val << 8);
                break;
                case 0x42:
                voodoo->initEnable = (voodoo->initEnable & ~0x00ff0000) | (val << 16);
                voodoo_recalcmapping(voodoo->set);
                break;
                case 0x43:
                voodoo->initEnable = (voodoo->initEnable & ~0xff000000) | (val << 24);
                voodoo_recalcmapping(voodoo->set);
                break;
        }
}

static void voodoo_calc_clutData(voodoo_t *voodoo)
{
        int c;
        
        for (c = 0; c < 256; c++)
        {
                voodoo->clutData256[c].r = (voodoo->clutData[c >> 3].r*(8-(c & 7)) +
                                           voodoo->clutData[(c >> 3)+1].r*(c & 7)) >> 3;
                voodoo->clutData256[c].g = (voodoo->clutData[c >> 3].g*(8-(c & 7)) +
                                           voodoo->clutData[(c >> 3)+1].g*(c & 7)) >> 3;
                voodoo->clutData256[c].b = (voodoo->clutData[c >> 3].b*(8-(c & 7)) +
                                           voodoo->clutData[(c >> 3)+1].b*(c & 7)) >> 3;
        }

        for (c = 0; c < 65536; c++)
        {
                int r = (c >> 8) & 0xf8;
                int g = (c >> 3) & 0xfc;
                int b = (c << 3) & 0xf8;
//                r |= (r >> 5);
//                g |= (g >> 6);
//                b |= (b >> 5);
                
                voodoo->video_16to32[c] = (voodoo->clutData256[r].r << 16) | (voodoo->clutData256[g].g << 8) | voodoo->clutData256[b].b;
        }
}



#define FILTDIV 256

static int FILTCAP, FILTCAPG, FILTCAPB = 0;	/* color filter threshold values */

static void voodoo_generate_filter_v1(voodoo_t *voodoo)
{
        int g, h;
        float difference, diffg, diffb;
        float thiscol, thiscolg, thiscolb, lined;
	float fcr, fcg, fcb;
	
	fcr = FILTCAP * 5;
	fcg = FILTCAPG * 6;
	fcb = FILTCAPB * 5;

        for (g=0;g<FILTDIV;g++)         // pixel 1
        {
                for (h=0;h<FILTDIV;h++)      // pixel 2
                {
                        difference = (float)(h - g);
                        diffg = difference;
                        diffb = difference;

			thiscol = thiscolg = thiscolb = g;

                        if (difference > FILTCAP)
                                difference = FILTCAP;
                        if (difference < -FILTCAP)
                                difference = -FILTCAP;

                        if (diffg > FILTCAPG)
                                diffg = FILTCAPG;
                        if (diffg < -FILTCAPG)
                                diffg = -FILTCAPG;

                        if (diffb > FILTCAPB)
                                diffb = FILTCAPB;
                        if (diffb < -FILTCAPB)
                                diffb = -FILTCAPB;
			
			// hack - to make it not bleed onto black
			//if (g == 0){
			//difference = diffg = diffb = 0;
			//}
			
			if ((difference < fcr) || (-difference > -fcr))
        			thiscol =  g + (difference / 2);
			if ((diffg < fcg) || (-diffg > -fcg))
        			thiscolg =  g + (diffg / 2);		/* need these divides so we can actually undither! */
			if ((diffb < fcb) || (-diffb > -fcb))
        			thiscolb =  g + (diffb / 2);

                        if (thiscol < 0)
                                thiscol = 0;
                        if (thiscol > FILTDIV-1)
                                thiscol = FILTDIV-1;

                        if (thiscolg < 0)
                                thiscolg = 0;
                        if (thiscolg > FILTDIV-1)
                                thiscolg = FILTDIV-1;

                        if (thiscolb < 0)
                                thiscolb = 0;
                        if (thiscolb > FILTDIV-1)
                                thiscolb = FILTDIV-1;

                        voodoo->thefilter[g][h] = thiscol;
                        voodoo->thefilterg[g][h] = thiscolg;
                        voodoo->thefilterb[g][h] = thiscolb;
                }

                lined = g + 4;
                if (lined > 255)
                        lined = 255;
                voodoo->purpleline[g][0] = lined;
                voodoo->purpleline[g][2] = lined;

                lined = g + 0;
                if (lined > 255)
                        lined = 255;
                voodoo->purpleline[g][1] = lined;
        }
}

static void voodoo_generate_filter_v2(voodoo_t *voodoo)
{
        int g, h;
        float difference;
        float thiscol, thiscolg, thiscolb, lined;
	float clr, clg, clb = 0;
	float fcr, fcg, fcb = 0;

	// pre-clamping

	fcr = FILTCAP;
	fcg = FILTCAPG;
	fcb = FILTCAPB;

	if (fcr > 32) fcr = 32;
	if (fcg > 32) fcg = 32;
	if (fcb > 32) fcb = 32;

        for (g=0;g<256;g++)         	// pixel 1 - our target pixel we want to bleed into
        {
		for (h=0;h<256;h++)      // pixel 2 - our main pixel
		{
			float avg;
			float avgdiff;
		
			difference = (float)(g - h);
			avg = (float)((g + g + g + g + h) / 5);
			avgdiff = avg - (float)((g + h + h + h + h) / 5);
			if (avgdiff < 0) avgdiff *= -1;
			if (difference < 0) difference *= -1;
		
			thiscol = thiscolg = thiscolb = g;
	
			// try lighten
			if (h > g)
			{
				clr = clg = clb = avgdiff;
		
				if (clr>fcr) clr=fcr;
                                if (clg>fcg) clg=fcg;
				if (clb>fcb) clb=fcb;
		
		
				thiscol = g + clr;
				thiscolg = g + clg;
				thiscolb = g + clb;
		
				if (thiscol>g+FILTCAP)
					thiscol=g+FILTCAP;
				if (thiscolg>g+FILTCAPG)
					thiscolg=g+FILTCAPG;
				if (thiscolb>g+FILTCAPB)
					thiscolb=g+FILTCAPB;
		
		
				if (thiscol>g+avgdiff)
					thiscol=g+avgdiff;
				if (thiscolg>g+avgdiff)
					thiscolg=g+avgdiff;
				if (thiscolb>g+avgdiff)
					thiscolb=g+avgdiff;
		
			}
	
			if (difference > FILTCAP)
				thiscol = g;
			if (difference > FILTCAPG)
				thiscolg = g;
			if (difference > FILTCAPB)
				thiscolb = g;
	
			// clamp 
			if (thiscol < 0) thiscol = 0;
			if (thiscolg < 0) thiscolg = 0;
			if (thiscolb < 0) thiscolb = 0;
	
			if (thiscol > 255) thiscol = 255;
			if (thiscolg > 255) thiscolg = 255;
			if (thiscolb > 255) thiscolb = 255;
	
			// add to the table
			voodoo->thefilter[g][h] = (thiscol);
			voodoo->thefilterg[g][h] = (thiscolg);
			voodoo->thefilterb[g][h] = (thiscolb);
	
			// debug the ones that don't give us much of a difference
			//if (difference < FILTCAP)
			//voodoo_log("Voodoofilter: %ix%i - %f difference, %f average difference, R=%f, G=%f, B=%f\n", g, h, difference, avgdiff, thiscol, thiscolg, thiscolb);	
                }

                lined = g + 3;
                if (lined > 255)
                        lined = 255;
                voodoo->purpleline[g][0] = lined;
                voodoo->purpleline[g][1] = 0;
                voodoo->purpleline[g][2] = lined;
        }
}

static void voodoo_threshold_check(voodoo_t *voodoo)
{
	int r, g, b;

	if (!voodoo->scrfilterEnabled)
		return;	/* considered disabled; don't check and generate */

	/* Check for changes, to generate anew table */
	if (voodoo->scrfilterThreshold != voodoo->scrfilterThresholdOld)
	{
		r = (voodoo->scrfilterThreshold >> 16) & 0xFF;
		g = (voodoo->scrfilterThreshold >> 8 ) & 0xFF;
		b = voodoo->scrfilterThreshold & 0xFF;
		
		FILTCAP = r;
		FILTCAPG = g;
		FILTCAPB = b;
		
		voodoo_log("Voodoo Filter Threshold Check: %06x - RED %i GREEN %i BLUE %i\n", voodoo->scrfilterThreshold, r, g, b);	

		voodoo->scrfilterThresholdOld = voodoo->scrfilterThreshold;

		if (voodoo->type == VOODOO_2)
			voodoo_generate_filter_v2(voodoo);
		else
			voodoo_generate_filter_v1(voodoo);
	}
}

static void voodoo_filterline_v1(voodoo_t *voodoo, uint8_t *fil, int column, uint16_t *src, int line)
{
	int x;
	
	// Scratchpad for avoiding feedback streaks
        uint8_t fil3[(voodoo->h_disp) * 3];  

	/* 16 to 32-bit */
        for (x=0; x<column;x++)
        {
		fil[x*3] 	=	((src[x] & 31) << 3);
		fil[x*3+1] 	=	(((src[x] >> 5) & 63) << 2);
 		fil[x*3+2] 	=	(((src[x] >> 11) & 31) << 3);

		// Copy to our scratchpads
 		fil3[x*3+0] 	= fil[x*3+0];
 		fil3[x*3+1] 	= fil[x*3+1];
 		fil3[x*3+2] 	= fil[x*3+2];
        }


        /* lines */

        if (line & 1)
        {
                for (x=0; x<column;x++)
                {
                        fil[x*3] = voodoo->purpleline[fil[x*3]][0];
                        fil[x*3+1] = voodoo->purpleline[fil[x*3+1]][1];
                        fil[x*3+2] = voodoo->purpleline[fil[x*3+2]][2];
                }
        }


        /* filtering time */

        for (x=1; x<column;x++)
        {
                fil3[(x)*3]   = voodoo->thefilterb[fil[x*3]][fil[	(x-1)		*3]];
                fil3[(x)*3+1] = voodoo->thefilterg[fil[x*3+1]][fil[	(x-1)		*3+1]];
                fil3[(x)*3+2] = voodoo->thefilter[fil[x*3+2]][fil[	(x-1)		*3+2]];
        }

        for (x=1; x<column;x++)
        {
                fil[(x)*3]   = voodoo->thefilterb[fil3[x*3]][fil3[	(x-1)		*3]];
                fil[(x)*3+1] = voodoo->thefilterg[fil3[x*3+1]][fil3[	(x-1)		*3+1]];
                fil[(x)*3+2] = voodoo->thefilter[fil3[x*3+2]][fil3[	(x-1)		*3+2]];
        }

        for (x=1; x<column;x++)
        {
                fil3[(x)*3]   = voodoo->thefilterb[fil[x*3]][fil[	(x-1)		*3]];
                fil3[(x)*3+1] = voodoo->thefilterg[fil[x*3+1]][fil[	(x-1)		*3+1]];
                fil3[(x)*3+2] = voodoo->thefilter[fil[x*3+2]][fil[	(x-1)		*3+2]];
        }

        for (x=0; x<column-1;x++)
        {
                fil[(x)*3]   = voodoo->thefilterb[fil3[x*3]][fil3[	(x+1)		*3]];
                fil[(x)*3+1] = voodoo->thefilterg[fil3[x*3+1]][fil3[	(x+1)		*3+1]]; 
                fil[(x)*3+2] = voodoo->thefilter[fil3[x*3+2]][fil3[	(x+1)		*3+2]]; 
        }
}


static void voodoo_filterline_v2(voodoo_t *voodoo, uint8_t *fil, int column, uint16_t *src, int line)
{
	int x;

	// Scratchpad for blending filter
        uint8_t fil3[(voodoo->h_disp) * 3];  
	
	/* 16 to 32-bit */
        for (x=0; x<column;x++)
        {
		// Blank scratchpads
 		fil3[x*3+0] = fil[x*3+0] =	((src[x] & 31) << 3);
 		fil3[x*3+1] = fil[x*3+1] =	(((src[x] >> 5) & 63) << 2);
 		fil3[x*3+2] = fil[x*3+2] =	(((src[x] >> 11) & 31) << 3);
        }

        /* filtering time */

	for (x=1; x<column-3;x++)
        {
		fil3[(x+3)*3]   = voodoo->thefilterb	[((src[x+3] & 31) << 3)]		[((src[x] & 31) << 3)];
		fil3[(x+3)*3+1] = voodoo->thefilterg	[(((src[x+3] >> 5) & 63) << 2)]		[(((src[x] >> 5) & 63) << 2)];
		fil3[(x+3)*3+2] = voodoo->thefilter	[(((src[x+3] >> 11) & 31) << 3)]	[(((src[x] >> 11) & 31) << 3)];

		fil[(x+2)*3]   = voodoo->thefilterb	[fil3[(x+2)*3]][((src[x] & 31) << 3)];
		fil[(x+2)*3+1] = voodoo->thefilterg	[fil3[(x+2)*3+1]][(((src[x] >> 5) & 63) << 2)];
		fil[(x+2)*3+2] = voodoo->thefilter	[fil3[(x+2)*3+2]][(((src[x] >> 11) & 31) << 3)];

		fil3[(x+1)*3]   = voodoo->thefilterb	[fil[(x+1)*3]][((src[x] & 31) << 3)];
		fil3[(x+1)*3+1] = voodoo->thefilterg	[fil[(x+1)*3+1]][(((src[x] >> 5) & 63) << 2)];
		fil3[(x+1)*3+2] = voodoo->thefilter	[fil[(x+1)*3+2]][(((src[x] >> 11) & 31) << 3)];

		fil[(x-1)*3]   = voodoo->thefilterb	[fil3[(x-1)*3]][((src[x] & 31) << 3)];
		fil[(x-1)*3+1] = voodoo->thefilterg	[fil3[(x-1)*3+1]][(((src[x] >> 5) & 63) << 2)];
		fil[(x-1)*3+2] = voodoo->thefilter	[fil3[(x-1)*3+2]][(((src[x] >> 11) & 31) << 3)];
        }

	// unroll for edge cases

	fil3[(column-3)*3]   = voodoo->thefilterb	[((src[column-3] & 31) << 3)]		[((src[column] & 31) << 3)];
	fil3[(column-3)*3+1] = voodoo->thefilterg	[(((src[column-3] >> 5) & 63) << 2)]	[(((src[column] >> 5) & 63) << 2)];
	fil3[(column-3)*3+2] = voodoo->thefilter	[(((src[column-3] >> 11) & 31) << 3)]	[(((src[column] >> 11) & 31) << 3)];

	fil3[(column-2)*3]   = voodoo->thefilterb	[((src[column-2] & 31) << 3)]		[((src[column] & 31) << 3)];
	fil3[(column-2)*3+1] = voodoo->thefilterg	[(((src[column-2] >> 5) & 63) << 2)]	[(((src[column] >> 5) & 63) << 2)];
	fil3[(column-2)*3+2] = voodoo->thefilter	[(((src[column-2] >> 11) & 31) << 3)]	[(((src[column] >> 11) & 31) << 3)];

	fil3[(column-1)*3]   = voodoo->thefilterb	[((src[column-1] & 31) << 3)]		[((src[column] & 31) << 3)];
	fil3[(column-1)*3+1] = voodoo->thefilterg	[(((src[column-1] >> 5) & 63) << 2)]	[(((src[column] >> 5) & 63) << 2)];
	fil3[(column-1)*3+2] = voodoo->thefilter	[(((src[column-1] >> 11) & 31) << 3)]	[(((src[column] >> 11) & 31) << 3)];

	fil[(column-2)*3]   = voodoo->thefilterb	[fil3[(column-2)*3]][((src[column] & 31) << 3)];
	fil[(column-2)*3+1] = voodoo->thefilterg	[fil3[(column-2)*3+1]][(((src[column] >> 5) & 63) << 2)];
	fil[(column-2)*3+2] = voodoo->thefilter		[fil3[(column-2)*3+2]][(((src[column] >> 11) & 31) << 3)];

	fil[(column-1)*3]   = voodoo->thefilterb	[fil3[(column-1)*3]][((src[column] & 31) << 3)];
	fil[(column-1)*3+1] = voodoo->thefilterg	[fil3[(column-1)*3+1]][(((src[column] >> 5) & 63) << 2)];
	fil[(column-1)*3+2] = voodoo->thefilter		[fil3[(column-1)*3+2]][(((src[column] >> 11) & 31) << 3)];

	fil3[(column-1)*3]   = voodoo->thefilterb	[fil[(column-1)*3]][((src[column] & 31) << 3)];
	fil3[(column-1)*3+1] = voodoo->thefilterg	[fil[(column-1)*3+1]][(((src[column] >> 5) & 63) << 2)];
	fil3[(column-1)*3+2] = voodoo->thefilter	[fil[(column-1)*3+2]][(((src[column] >> 11) & 31) << 3)];
}

void voodoo_callback(void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
	int y_add = (enable_overscan && !suppress_overscan) ? (overscan_y >> 1) : 0;
	int x_add = (enable_overscan && !suppress_overscan) ? 8 : 0;

        if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS)
        {
                if (voodoo->line < voodoo->v_disp)
                {
                        voodoo_t *draw_voodoo;
                        int draw_line;
                        
                        if (SLI_ENABLED)
                        {
                                if (voodoo == voodoo->set->voodoos[1])
                                        goto skip_draw;
                                
                                if (((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) ? 1 : 0) == (voodoo->line & 1))
                                        draw_voodoo = voodoo;
                                else
                                        draw_voodoo = voodoo->set->voodoos[1];
                                draw_line = voodoo->line >> 1;
                        }
                        else
                        {
                                if (!(voodoo->fbiInit0 & 1))
                                        goto skip_draw;
                                draw_voodoo = voodoo;
                                draw_line = voodoo->line;
                        }
                        
                        if (draw_voodoo->dirty_line[draw_line])
                        {
                                uint32_t *p = &((uint32_t *)buffer32->line[voodoo->line + y_add])[32 + x_add];
                                uint16_t *src = (uint16_t *)&draw_voodoo->fb_mem[draw_voodoo->front_offset + draw_line*draw_voodoo->row_width];
                                int x;

                                draw_voodoo->dirty_line[draw_line] = 0;
                                
                                if (voodoo->line < voodoo->dirty_line_low)
                                {
                                        voodoo->dirty_line_low = voodoo->line;
                                        video_wait_for_buffer();
                                }
                                if (voodoo->line > voodoo->dirty_line_high)
                                        voodoo->dirty_line_high = voodoo->line;
                                
                                if (voodoo->scrfilter && voodoo->scrfilterEnabled)
                                {
                                        uint8_t fil[(voodoo->h_disp) * 3];              /* interleaved 24-bit RGB */

                			if (voodoo->type == VOODOO_2)
	                                        voodoo_filterline_v2(voodoo, fil, voodoo->h_disp, src, voodoo->line);
					else
                                        	voodoo_filterline_v1(voodoo, fil, voodoo->h_disp, src, voodoo->line);

                                        for (x = 0; x < voodoo->h_disp; x++)
                                        {
                                                p[x] = (voodoo->clutData256[fil[x*3]].b << 0 | voodoo->clutData256[fil[x*3+1]].g << 8 | voodoo->clutData256[fil[x*3+2]].r << 16);
                                        }
                                }
                                else
                                {
                                        for (x = 0; x < voodoo->h_disp; x++)
                                        {
                                                p[x] = draw_voodoo->video_16to32[src[x]];
                                        }
                                }
                        }
                }
        }
skip_draw:
        if (voodoo->line == voodoo->v_disp)
        {
//                voodoo_log("retrace %i %i %08x %i\n", voodoo->retrace_count, voodoo->swap_interval, voodoo->swap_offset, voodoo->swap_pending);
                voodoo->retrace_count++;
                if (SLI_ENABLED && (voodoo->fbiInit2 & FBIINIT2_SWAP_ALGORITHM_MASK) == FBIINIT2_SWAP_ALGORITHM_SLI_SYNC)
                {
                        if (voodoo == voodoo->set->voodoos[0])
                        {
                                voodoo_t *voodoo_1 = voodoo->set->voodoos[1];

                                /*Only swap if both Voodoos are waiting for buffer swap*/
                                if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval) &&
                                    voodoo_1->swap_pending && (voodoo_1->retrace_count > voodoo_1->swap_interval))
                                {
                                        memset(voodoo->dirty_line, 1, 1024);
                                        voodoo->retrace_count = 0;
                                        voodoo->front_offset = voodoo->swap_offset;
                                        if (voodoo->swap_count > 0)
                                                voodoo->swap_count--;
                                        voodoo->swap_pending = 0;

                                        memset(voodoo_1->dirty_line, 1, 1024);
                                        voodoo_1->retrace_count = 0;
                                        voodoo_1->front_offset = voodoo_1->swap_offset;
                                        if (voodoo_1->swap_count > 0)
                                                voodoo_1->swap_count--;
                                        voodoo_1->swap_pending = 0;
                                        
                                        thread_set_event(voodoo->wake_fifo_thread);
                                        thread_set_event(voodoo_1->wake_fifo_thread);
                                        
                                        voodoo->frame_count++;
                                        voodoo_1->frame_count++;
                                }
                        }
                }
                else
                {
                        if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval))
                        {
                                memset(voodoo->dirty_line, 1, 1024);
                                voodoo->retrace_count = 0;
                                voodoo->front_offset = voodoo->swap_offset;
                                if (voodoo->swap_count > 0)
                                        voodoo->swap_count--;
                                voodoo->swap_pending = 0;
                                thread_set_event(voodoo->wake_fifo_thread);
                                voodoo->frame_count++;
                        }
                }
                voodoo->v_retrace = 1;
        }
        voodoo->line++;
        
        if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS)
        {
                if (voodoo->line == voodoo->v_disp)
                {
                        if (voodoo->dirty_line_high > voodoo->dirty_line_low)
                                svga_doblit(0, voodoo->v_disp, voodoo->h_disp, voodoo->v_disp-1, voodoo->svga);
                        if (voodoo->clutData_dirty)
                        {
                                voodoo->clutData_dirty = 0;
                                voodoo_calc_clutData(voodoo);
                        }
                        voodoo->dirty_line_high = -1;
                        voodoo->dirty_line_low = 2000;
                }
        }
        
        if (voodoo->line >= voodoo->v_total)
        {
                voodoo->line = 0;
                voodoo->v_retrace = 0;
        }
        if (voodoo->line_time)
                voodoo->timer_count += voodoo->line_time;
        else
                voodoo->timer_count += TIMER_USEC * 32;
}

static void voodoo_add_status_info(char *s, int max_len, void *p)
{
        voodoo_set_t *voodoo_set = (voodoo_set_t *)p;
        voodoo_t *voodoo = voodoo_set->voodoos[0];
        voodoo_t *voodoo_slave = voodoo_set->voodoos[1];
        char temps[512], temps2[256];
        int pixel_count_current[2];
        int pixel_count_total;
        int texel_count_current[2];
        int texel_count_total;
        int render_time[2];
        uint64_t new_time = plat_timer_read();
        uint64_t status_diff = new_time - status_time;
        status_time = new_time;

        if (!status_diff)
                status_diff = 1;

        svga_add_status_info(s, max_len, &voodoo->svga);
        
        pixel_count_current[0] = voodoo->pixel_count[0];
        pixel_count_current[1] = voodoo->pixel_count[1];
        texel_count_current[0] = voodoo->texel_count[0];
        texel_count_current[1] = voodoo->texel_count[1];
        render_time[0] = voodoo->render_time[0];
        render_time[1] = voodoo->render_time[1];
        if (voodoo_set->nr_cards == 2)
        {
                pixel_count_current[0] += voodoo_slave->pixel_count[0];
                pixel_count_current[1] += voodoo_slave->pixel_count[1];
                texel_count_current[0] += voodoo_slave->texel_count[0];
                texel_count_current[1] += voodoo_slave->texel_count[1];
                render_time[0] = (render_time[0] + voodoo_slave->render_time[0]) / 2;
                render_time[1] = (render_time[1] + voodoo_slave->render_time[1]) / 2;
        }
        pixel_count_total = (pixel_count_current[0] + pixel_count_current[1]) - (voodoo->pixel_count_old[0] + voodoo->pixel_count_old[1]);
        texel_count_total = (texel_count_current[0] + texel_count_current[1]) - (voodoo->texel_count_old[0] + voodoo->texel_count_old[1]);
        sprintf(temps, "%f Mpixels/sec (%f)\n%f Mtexels/sec (%f)\n%f ktris/sec\n%f%% CPU (%f%% real)\n%d frames/sec (%i)\n%f%% CPU (%f%% real)\n"/*%d reads/sec\n%d write/sec\n%d tex/sec\n*/,
                (double)pixel_count_total/1000000.0,
                ((double)pixel_count_total/1000000.0) / ((double)render_time[0] / status_diff),
                (double)texel_count_total/1000000.0,
                ((double)texel_count_total/1000000.0) / ((double)render_time[0] / status_diff),
                (double)voodoo->tri_count/1000.0, ((double)voodoo->time * 100.0) / timer_freq, ((double)voodoo->time * 100.0) / status_diff, voodoo->frame_count, voodoo_recomp,
                ((double)voodoo->render_time[0] * 100.0) / timer_freq, ((double)voodoo->render_time[0] * 100.0) / status_diff);
        if (voodoo->render_threads == 2)
        {
                sprintf(temps2, "%f%% CPU (%f%% real)\n",
                        ((double)voodoo->render_time[1] * 100.0) / timer_freq, ((double)voodoo->render_time[1] * 100.0) / status_diff);
                strncat(temps, temps2, sizeof(temps)-1);
        }
        if (voodoo_set->nr_cards == 2)
        {
                sprintf(temps2, "%f%% CPU (%f%% real)\n",
                        ((double)voodoo_slave->render_time[0] * 100.0) / timer_freq, ((double)voodoo_slave->render_time[0] * 100.0) / status_diff);
                strncat(temps, temps2, sizeof(temps)-1);
                        
                if (voodoo_slave->render_threads == 2)
                {
                        sprintf(temps2, "%f%% CPU (%f%% real)\n",
                                ((double)voodoo_slave->render_time[1] * 100.0) / timer_freq, ((double)voodoo_slave->render_time[1] * 100.0) / status_diff);
                        strncat(temps, temps2, sizeof(temps)-1);
                }
        }
        strncat(s, temps, max_len);

        voodoo->pixel_count_old[0] = pixel_count_current[0];
        voodoo->pixel_count_old[1] = pixel_count_current[1];
        voodoo->texel_count_old[0] = texel_count_current[0];
        voodoo->texel_count_old[1] = texel_count_current[1];
        voodoo->tri_count = voodoo->frame_count = 0;
        voodoo->rd_count = voodoo->wr_count = voodoo->tex_count = 0;
        voodoo->time = 0;
        voodoo->render_time[0] = voodoo->render_time[1] = 0;
        if (voodoo_set->nr_cards == 2)
        {
                voodoo_slave->pixel_count_old[0] = pixel_count_current[0];
                voodoo_slave->pixel_count_old[1] = pixel_count_current[1];
                voodoo_slave->texel_count_old[0] = texel_count_current[0];
                voodoo_slave->texel_count_old[1] = texel_count_current[1];
                voodoo_slave->tri_count = voodoo_slave->frame_count = 0;
                voodoo_slave->rd_count = voodoo_slave->wr_count = voodoo_slave->tex_count = 0;
                voodoo_slave->time = 0;
                voodoo_slave->render_time[0] = voodoo_slave->render_time[1] = 0;
        }
        voodoo_recomp = 0;
}

static void voodoo_speed_changed(void *p)
{
        voodoo_set_t *voodoo_set = (voodoo_set_t *)p;
        
        voodoo_pixelclock_update(voodoo_set->voodoos[0]);
        voodoo_set->voodoos[0]->read_time = pci_nonburst_time + pci_burst_time * ((voodoo_set->voodoos[0]->fbiInit4 & 1) ? 2 : 1);
        voodoo_set->voodoos[0]->write_time = pci_nonburst_time + pci_burst_time * ((voodoo_set->voodoos[0]->fbiInit1 & 2) ? 1 : 0);
        voodoo_set->voodoos[0]->burst_time = pci_burst_time * ((voodoo_set->voodoos[0]->fbiInit1 & 2) ? 2 : 1);
        if (voodoo_set->nr_cards == 2)
        {
                voodoo_pixelclock_update(voodoo_set->voodoos[1]);
                voodoo_set->voodoos[1]->read_time = pci_nonburst_time + pci_burst_time * ((voodoo_set->voodoos[1]->fbiInit4 & 1) ? 2 : 1);
                voodoo_set->voodoos[1]->write_time = pci_nonburst_time + pci_burst_time * ((voodoo_set->voodoos[1]->fbiInit1 & 2) ? 1 : 0);
                voodoo_set->voodoos[1]->burst_time = pci_burst_time * ((voodoo_set->voodoos[1]->fbiInit1 & 2) ? 2 : 1);
        }
//        voodoo_log("Voodoo read_time=%i write_time=%i burst_time=%i %08x %08x\n", voodoo->read_time, voodoo->write_time, voodoo->burst_time, voodoo->fbiInit1, voodoo->fbiInit4);
}

void *voodoo_card_init()
{
        int c;
        voodoo_t *voodoo = malloc(sizeof(voodoo_t));
        memset(voodoo, 0, sizeof(voodoo_t));

        voodoo->bilinear_enabled = device_get_config_int("bilinear");
        voodoo->scrfilter = device_get_config_int("dacfilter");
        voodoo->texture_size = device_get_config_int("texture_memory");
        voodoo->texture_mask = (voodoo->texture_size << 20) - 1;
        voodoo->fb_size = device_get_config_int("framebuffer_memory");
        voodoo->fb_mask = (voodoo->fb_size << 20) - 1;
        voodoo->render_threads = device_get_config_int("render_threads");
        voodoo->odd_even_mask = voodoo->render_threads - 1;
#ifndef NO_CODEGEN
        voodoo->use_recompiler = device_get_config_int("recompiler");
#endif                        
        voodoo->type = device_get_config_int("type");
        switch (voodoo->type)
        {
                case VOODOO_1:
                voodoo->dual_tmus = 0;
                break;
                case VOODOO_SB50:
                voodoo->dual_tmus = 1;
                break;
                case VOODOO_2:
                voodoo->dual_tmus = 1;
                break;
        }
        
	if (voodoo->type == VOODOO_2) /*generate filter lookup tables*/
		voodoo_generate_filter_v2(voodoo);
	else
		voodoo_generate_filter_v1(voodoo);
        
        pci_add_card(PCI_ADD_NORMAL, voodoo_pci_read, voodoo_pci_write, voodoo);

        mem_mapping_add(&voodoo->mapping, 0, 0, NULL, voodoo_readw, voodoo_readl, NULL, voodoo_writew, voodoo_writel,     NULL, MEM_MAPPING_EXTERNAL, voodoo);

        voodoo->fb_mem = malloc(4 * 1024 * 1024);
        voodoo->tex_mem[0] = malloc(voodoo->texture_size * 1024 * 1024);
        if (voodoo->dual_tmus)
                voodoo->tex_mem[1] = malloc(voodoo->texture_size * 1024 * 1024);
        voodoo->tex_mem_w[0] = (uint16_t *)voodoo->tex_mem[0];
        voodoo->tex_mem_w[1] = (uint16_t *)voodoo->tex_mem[1];
        
        for (c = 0; c < TEX_CACHE_MAX; c++)
        {
                voodoo->texture_cache[0][c].data = malloc((256*256 + 256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2) * 4);
                voodoo->texture_cache[0][c].base = -1; /*invalid*/
                voodoo->texture_cache[0][c].refcount = 0;
                if (voodoo->dual_tmus)
                {
                        voodoo->texture_cache[1][c].data = malloc((256*256 + 256*256 + 128*128 + 64*64 + 32*32 + 16*16 + 8*8 + 4*4 + 2*2) * 4);
                        voodoo->texture_cache[1][c].base = -1; /*invalid*/
                        voodoo->texture_cache[1][c].refcount = 0;
                }
        }

        timer_add(voodoo_callback, &voodoo->timer_count, TIMER_ALWAYS_ENABLED, voodoo);
        
        voodoo->svga = svga_get_pri();
        voodoo->fbiInit0 = 0;

        voodoo->wake_fifo_thread = thread_create_event();
        voodoo->wake_render_thread[0] = thread_create_event();
        voodoo->wake_render_thread[1] = thread_create_event();
        voodoo->wake_main_thread = thread_create_event();
        voodoo->fifo_not_full_event = thread_create_event();
        voodoo->render_not_full_event[0] = thread_create_event();
        voodoo->render_not_full_event[1] = thread_create_event();
        voodoo->fifo_thread = thread_create(fifo_thread, voodoo);
        voodoo->render_thread[0] = thread_create(render_thread_1, voodoo);
        if (voodoo->render_threads == 2)
                voodoo->render_thread[1] = thread_create(render_thread_2, voodoo);

        timer_add(voodoo_wake_timer, &voodoo->wake_timer, &voodoo->wake_timer, (void *)voodoo);
        
        for (c = 0; c < 0x100; c++)
        {
                rgb332[c].r = c & 0xe0;
                rgb332[c].g = (c << 3) & 0xe0;
                rgb332[c].b = (c << 6) & 0xc0;
                rgb332[c].r = rgb332[c].r | (rgb332[c].r >> 3) | (rgb332[c].r >> 6);
                rgb332[c].g = rgb332[c].g | (rgb332[c].g >> 3) | (rgb332[c].g >> 6);
                rgb332[c].b = rgb332[c].b | (rgb332[c].b >> 2);
                rgb332[c].b = rgb332[c].b | (rgb332[c].b >> 4);
                rgb332[c].a = 0xff;
                
                ai44[c].a = (c & 0xf0) | ((c & 0xf0) >> 4);
                ai44[c].r = (c & 0x0f) | ((c & 0x0f) << 4);
                ai44[c].g = ai44[c].b = ai44[c].r;
        }
                
        for (c = 0; c < 0x10000; c++)
        {
                rgb565[c].r = (c >> 8) & 0xf8;
                rgb565[c].g = (c >> 3) & 0xfc;
                rgb565[c].b = (c << 3) & 0xf8;
                rgb565[c].r |= (rgb565[c].r >> 5);
                rgb565[c].g |= (rgb565[c].g >> 6);
                rgb565[c].b |= (rgb565[c].b >> 5);
                rgb565[c].a = 0xff;

                argb1555[c].r = (c >> 7) & 0xf8;
                argb1555[c].g = (c >> 2) & 0xf8;
                argb1555[c].b = (c << 3) & 0xf8;
                argb1555[c].r |= (argb1555[c].r >> 5);
                argb1555[c].g |= (argb1555[c].g >> 5);
                argb1555[c].b |= (argb1555[c].b >> 5);
                argb1555[c].a = (c & 0x8000) ? 0xff : 0;

                argb4444[c].a = (c >> 8) & 0xf0;
                argb4444[c].r = (c >> 4) & 0xf0;
                argb4444[c].g = c & 0xf0;
                argb4444[c].b = (c << 4) & 0xf0;
                argb4444[c].a |= (argb4444[c].a >> 4);
                argb4444[c].r |= (argb4444[c].r >> 4);
                argb4444[c].g |= (argb4444[c].g >> 4);
                argb4444[c].b |= (argb4444[c].b >> 4);
                
                ai88[c].a = (c >> 8);
                ai88[c].r = c & 0xff;
                ai88[c].g = c & 0xff;
                ai88[c].b = c & 0xff;
        }
#ifndef NO_CODEGEN
        voodoo_codegen_init(voodoo);
#endif

        voodoo->disp_buffer = 0;
        voodoo->draw_buffer = 1;
        
        return voodoo;
}

void *voodoo_init()
{
        voodoo_set_t *voodoo_set = malloc(sizeof(voodoo_set_t));
        uint32_t tmuConfig = 1;
        int type;
        memset(voodoo_set, 0, sizeof(voodoo_set_t));
        
        type = device_get_config_int("type");
        
        voodoo_set->nr_cards = device_get_config_int("sli") ? 2 : 1;
        voodoo_set->voodoos[0] = voodoo_card_init();
        voodoo_set->voodoos[0]->set = voodoo_set;
        if (voodoo_set->nr_cards == 2)
        {
                voodoo_set->voodoos[1] = voodoo_card_init();
                                
                voodoo_set->voodoos[1]->set = voodoo_set;

                if (type == VOODOO_2)
                {
                        voodoo_set->voodoos[0]->fbiInit5 |= FBIINIT5_MULTI_CVG;
                        voodoo_set->voodoos[1]->fbiInit5 |= FBIINIT5_MULTI_CVG;
                }
                else
                {
                        voodoo_set->voodoos[0]->fbiInit1 |= FBIINIT1_MULTI_SST;
                        voodoo_set->voodoos[1]->fbiInit1 |= FBIINIT1_MULTI_SST;
                }
        }

        switch (type)
        {
                case VOODOO_1:
                if (voodoo_set->nr_cards == 2)
                        tmuConfig = 1 | (3 << 3);
                else
                        tmuConfig = 1;
                break;
                case VOODOO_SB50:
                if (voodoo_set->nr_cards == 2)
                        tmuConfig = 1 | (3 << 3) | (3 << 6) | (2 << 9);
                else
                        tmuConfig = 1 | (3 << 6);
                break;
                case VOODOO_2:
                tmuConfig = 1 | (3 << 6);
                break;
        }
        
        voodoo_set->voodoos[0]->tmuConfig = tmuConfig;
        if (voodoo_set->nr_cards == 2)
                voodoo_set->voodoos[1]->tmuConfig = tmuConfig;

        mem_mapping_add(&voodoo_set->snoop_mapping, 0, 0, NULL, voodoo_snoop_readw, voodoo_snoop_readl, NULL, voodoo_snoop_writew, voodoo_snoop_writel,     NULL, MEM_MAPPING_EXTERNAL, voodoo_set);
                
        return voodoo_set;
}

void voodoo_card_close(voodoo_t *voodoo)
{
#ifndef RELEASE_BUILD
        FILE *f;
#endif
        int c;
        
#ifndef RELEASE_BUILD        
        f = rom_fopen(L"texram.dmp", L"wb");
        fwrite(voodoo->tex_mem[0], voodoo->texture_size*1024*1024, 1, f);
        fclose(f);
        if (voodoo->dual_tmus)
        {
                f = rom_fopen(L"texram2.dmp", L"wb");
                fwrite(voodoo->tex_mem[1], voodoo->texture_size*1024*1024, 1, f);
                fclose(f);
        }
#endif

        thread_kill(voodoo->fifo_thread);
        thread_kill(voodoo->render_thread[0]);
        if (voodoo->render_threads == 2)
                thread_kill(voodoo->render_thread[1]);
        thread_destroy_event(voodoo->fifo_not_full_event);
        thread_destroy_event(voodoo->wake_main_thread);
        thread_destroy_event(voodoo->wake_fifo_thread);
        thread_destroy_event(voodoo->wake_render_thread[0]);
        thread_destroy_event(voodoo->wake_render_thread[1]);
        thread_destroy_event(voodoo->render_not_full_event[0]);
        thread_destroy_event(voodoo->render_not_full_event[1]);

        for (c = 0; c < TEX_CACHE_MAX; c++)
        {
                if (voodoo->dual_tmus)
                        free(voodoo->texture_cache[1][c].data);
                free(voodoo->texture_cache[0][c].data);
        }
#ifndef NO_CODEGEN
        voodoo_codegen_close(voodoo);
#endif
        free(voodoo->fb_mem);
        if (voodoo->dual_tmus)
                free(voodoo->tex_mem[1]);
        free(voodoo->tex_mem[0]);
        free(voodoo);
}

void voodoo_close(void *p)
{
        voodoo_set_t *voodoo_set = (voodoo_set_t *)p;
        
        if (voodoo_set->nr_cards == 2)
                voodoo_card_close(voodoo_set->voodoos[1]);
        voodoo_card_close(voodoo_set->voodoos[0]);
        
        free(voodoo_set);
}

static const device_config_t voodoo_config[] =
{
        {
                .name = "type",
                .description = "Voodoo type",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "Voodoo Graphics",
                                .value = VOODOO_1
                        },
                        {
                                .description = "Obsidian SB50 + Amethyst (2 TMUs)",
                                .value = VOODOO_SB50
                        },
                        {
                                .description = "Voodoo 2",
                                .value = VOODOO_2
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 0
        },
        {
                .name = "framebuffer_memory",
                .description = "Framebuffer memory size",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "2 MB",
                                .value = 2
                        },
                        {
                                .description = "4 MB",
                                .value = 4
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 2
        },
        {
                .name = "texture_memory",
                .description = "Texture memory size",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "2 MB",
                                .value = 2
                        },
                        {
                                .description = "4 MB",
                                .value = 4
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 2
        },
        {
                .name = "bilinear",
                .description = "Bilinear filtering",
                .type = CONFIG_BINARY,
                .default_int = 1
        },
        {
                .name = "dacfilter",
                .description = "Screen Filter",
                .type = CONFIG_BINARY,
                .default_int = 0
        },
        {
                .name = "render_threads",
                .description = "Render threads",
                .type = CONFIG_SELECTION,
                .selection =
                {
                        {
                                .description = "1",
                                .value = 1
                        },
                        {
                                .description = "2",
                                .value = 2
                        },
                        {
                                .description = ""
                        }
                },
                .default_int = 2
        },
        {
                .name = "sli",
                .description = "SLI",
                .type = CONFIG_BINARY,
                .default_int = 0
        },
#ifndef NO_CODEGEN
        {
                .name = "recompiler",
                .description = "Recompiler",
                .type = CONFIG_BINARY,
                .default_int = 1
        },
#endif
        {
                .type = -1
        }
};

const device_t voodoo_device =
{
        "3DFX Voodoo Graphics",
        DEVICE_PCI,
	0,
        voodoo_init,
        voodoo_close,
	NULL,
        NULL,
        voodoo_speed_changed,
        NULL,
        voodoo_add_status_info,
        voodoo_config
};
